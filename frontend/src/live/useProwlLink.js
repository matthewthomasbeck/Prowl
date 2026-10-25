import { useEffect, useRef, useState, useSyncExternalStore } from 'react'

// ---- The live connection to the robot ----
// Opens the robot's /ws/dashboard WebSocket on the same address as this page: the dashboard's web server
// forwards it to the dashboard bridge on the robot (edge/src/prowl_dashboard_bridge, messages in
// docs/protocol.md; see vite.config.js). If the robot can't be reached, it keeps retrying quietly.
//
// The robot sends one "snapshot" with the whole current state, then one message per update.
//
// The robot's fastest readings (LiDAR scans 5 times a second, wheel duties 10, IMU 5) don't re-draw the page
// at all: they're kept in link.live, and only the parts that show them read them, with
// useLiveValue(link, 'scan' | 'motorStatus' | 'imu'). Re-drawing the whole dashboard for each of them is
// what made it lag behind the robot.

const empty = {
  session: null, // { name, started_at, robot_connected } once the robot has answered, else null
  robotConnected: false, // are the robot's drivers sending? (the bridge answering isn't enough)
  status: null, // { motors, topic_age_s }
  canCommand: false, // may this page drive? (the right token, or the robot has no drive token set)
  cameraStill: null, // the newest photo { t, url, photo }
  photosChanged: 0, // goes up by one whenever a photo is taken or deleted, so lists can reload
  lastError: null, // { error, forType, at } the newest error the robot sent, e.g. "another dashboard is driving"
}

// The fast readings, in link.live rather than in the state above (see the top of this file)
const emptyLive = {
  scan: null, // { t, points: [[x, y], ...] } in metres around the robot (x forward, y left): the newest LiDAR scan
  motorStatus: null, // { wheels: [4 names], duties: [-1..1 x4] }, what the motor driver applies now
  imu: null, // { t, gyro: [x, y, z] rad/s, accel: [x, y, z] m/s² }, averaged over a fraction of a second
}

// A tiny store for the fast readings: the parts of the page that show one subscribe to it.
function createLiveStore() {
  let values = emptyLive
  const listeners = new Set()
  return {
    get: (key) => values[key],
    set(changes) {
      values = { ...values, ...changes }
      listeners.forEach((listener) => listener())
    },
    subscribe(listener) {
      listeners.add(listener)
      return () => listeners.delete(listener)
    },
  }
}

// The newest scan, motorStatus or imu. Re-draws only the component that calls it, whenever a new one arrives.
export function useLiveValue(link, key) {
  return useSyncExternalStore(link.live.subscribe, () => link.live.get(key))
}

// The robot's drive token. It comes from the token box (the argument), or else from ?token=... on this
// page's address. Watching needs none.
export function pageToken(token = '') {
  return token || new URLSearchParams(window.location.search).get('token') || ''
}

function socketUrl(token) {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  const chosen = pageToken(token)
  const query = chosen ? `?token=${encodeURIComponent(chosen)}` : ''
  return `${protocol}//${window.location.host}/ws/dashboard${query}`
}

// Changing the token reconnects, and the new snapshot says whether it was accepted (can_command).
export function useProwlLink(token = '') {
  const [connection, setConnection] = useState('connecting') // 'connecting', 'online' or 'offline'
  const [data, setData] = useState(empty)
  const socket = useRef(null)
  const [live] = useState(createLiveStore)

  useEffect(() => {
    let stopped = false
    let retries = 0
    let retryTimer = null

    function update(changes) {
      setData((current) => ({ ...current, ...(typeof changes === 'function' ? changes(current) : changes) }))
    }

    function handle(m) {
      switch (m.type) {
        case 'snapshot': {
          const latest = m.latest || {}
          update((current) => ({
            ...empty,
            session: m.session,
            robotConnected: !!m.session?.robot_connected,
            status: latest.status || null,
            canCommand: !!m.can_command,
            cameraStill: latest.camera_still || null,
            photosChanged: current.photosChanged + 1,
          }))
          live.set({ scan: latest.scan || null, motorStatus: latest.motor_status || null, imu: latest.imu || null })
          break
        }
        case 'status':
          update({ status: m })
          break
        case 'scan':
          live.set({ scan: m })
          break
        case 'motor_status':
          live.set({ motorStatus: m })
          break
        case 'imu':
          live.set({ imu: m })
          break
        case 'camera_still':
          update((current) => ({ cameraStill: m, photosChanged: current.photosChanged + 1 }))
          break
        case 'photo_deleted':
          update((current) => ({ photosChanged: current.photosChanged + 1 }))
          break
        case 'robot_connected':
          update({ robotConnected: true })
          break
        case 'robot_disconnected':
          update({ robotConnected: false })
          break
        case 'error':
          console.warn('Prowl robot:', m.error)
          update({ lastError: { error: m.error, forType: m.for_type ?? null, at: Date.now() / 1000 } })
          break
        default:
          break // pongs and anything new are not used by this dashboard
      }
    }

    function connect() {
      const ws = new WebSocket(socketUrl(token))
      socket.current = ws
      ws.onopen = () => {
        retries = 0
        setConnection('online')
      }
      ws.onmessage = (event) => handle(JSON.parse(event.data))
      ws.onclose = () => {
        if (socket.current === ws) socket.current = null
        if (stopped) return
        setConnection('offline')
        retryTimer = setTimeout(connect, Math.min(10000, 1000 * 2 ** retries++))
      }
    }

    connect()
    return () => {
      stopped = true
      clearTimeout(retryTimer)
      socket.current?.close()
    }
  }, [token, live])

  // Sends a message to the robot. Returns false if there is no connection right now.
  function send(message) {
    const ws = socket.current
    if (!ws || ws.readyState !== WebSocket.OPEN) return false
    ws.send(JSON.stringify(message))
    return true
  }

  return { connection, ...data, live, send }
}
