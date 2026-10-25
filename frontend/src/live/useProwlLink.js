import { useEffect, useRef, useState, useSyncExternalStore } from 'react'
import { recolorMap } from './liveMap.js'

// ---- The live connection to the Prowl backend ----
// Opens the backend's /ws/dashboard WebSocket on the same address as this page. During `npm run dev`,
// Vite forwards it to the backend (see vite.config.js). When the backend serves the built dashboard,
// it is simply the same server.
//
// If there is no backend, the connection just fails and keeps retrying quietly, and the dashboard
// shows its demo data. Nothing here invents data: everything comes from the backend's messages.
//
// It also works straight with the robot's own server, the dashboard bridge (edge/src/prowl_dashboard_bridge,
// messages in its PROTOCOL.md), which sends LiDAR scans, wheel duties, IMU readings and camera photos too, and
// says in its snapshot what it can do ("capabilities"). The backend sends no such list.
//
// The backend sends one "snapshot" with the whole current state, then one message per update.
// Pose updates arrive about 10 times a second, so updates are collected and handed to React a few
// times a second instead of re-drawing the page for every single message.
//
// The robot's fastest readings (LiDAR scans 5 times a second, wheel duties 10, IMU 5) skip that and don't
// re-draw the page at all: they're kept in link.live, and only the parts that show them read them, with
// useLiveValue(link, 'scan' | 'motorStatus' | 'imu'). Re-drawing the whole dashboard for each of them is
// what made it lag behind the robot.

const FLUSH_MS = 150
const TRAIL_STEP_M = 0.05

const empty = {
  session: null, // the run being shown, or null when no robot has connected yet
  robotConnected: false,
  mission: null, // { name, zone, simulated } from the robot
  pose: null, // { x, y, theta } in metres
  trail: [], // [[x, y], ...] in metres
  map: null, // the LiDAR map, recolored: { href, origin, width, height, resolution }
  items: [], // detections: camera samples and LiDAR obstacles, each with a display code like "P-001"
  reviews: {}, // code -> { status, note } set by reviewers on any dashboard
  reports: [], // queued reports, oldest first
  status: null, // backend: { mode, battery_v, ... }; robot: { mode, driving, motors, dashboards, topic_age_s }
  analysis: null, // { explored_m2, warnings }
  canCommand: false, // may this page drive? (the right token, or the robot has no drive token set)
  capabilities: null, // what the server can do; null from the backend (see BACKEND_CAPABILITIES)
  cameraStill: null, // robot: the newest photo { t, width, height, bytes, url, photo }
  photosChanged: 0, // robot: goes up by one whenever a photo is taken or deleted, so lists can reload
  lastError: null, // { error, forType, at } the newest error the server sent, e.g. "another dashboard is driving"
}

// The fast readings, in link.live rather than in the state above (see the top of this file)
const emptyLive = {
  scan: null, // { t, points: [[x, y], ...] } in metres: the newest LiDAR scan
  motorStatus: null, // robot: { wheels: [4 names], duties: [-1..1 x4] }, what the motor driver applies now
  imu: null, // robot: { t, gyro: [x, y, z] rad/s, accel: [x, y, z] m/s² }, averaged over a fraction of a second
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

// What the Python backend can do. It sends no capabilities, so these are assumed for it.
const BACKEND_CAPABILITIES = {
  drive: 'step', // one drive command per click (not used by this dashboard)
  strafe: false,
  autopilot: true,
  camera: false,
  photos: false,
  imu: false,
  detections: true,
  reviews: true,
  map: true,
}

// The token is the operator token for the backend, or the drive token for the robot. It comes from the
// token box (the argument), or else from ?token=... on this page's address. Watching needs none.
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
  const store = useRef({ ...empty }) // always the newest state; copied into React a few times a second
  const dirty = useRef(false)
  const socket = useRef(null)
  const [live] = useState(createLiveStore)

  useEffect(() => {
    let stopped = false
    let retries = 0
    let retryTimer = null
    let mapVersion = 0 // a newer map can finish decoding before an older one; only keep the newest

    function update(changes) {
      store.current = { ...store.current, ...changes }
      dirty.current = true
    }

    function showMap(message) {
      const version = ++mapVersion
      recolorMap(message).then((map) => {
        if (!stopped && version === mapVersion) update({ map })
      })
    }

    function handle(m) {
      const s = store.current
      switch (m.type) {
        case 'snapshot': {
          const latest = m.latest || {}
          update({
            ...empty,
            session: m.session,
            robotConnected: !!m.session?.robot_connected,
            mission: latest.mission || null,
            pose: latest.pose || null,
            trail: m.trail || [],
            items: latest.detections?.items || [],
            reviews: m.reviews || {},
            reports: m.reports || [],
            status: latest.status || null,
            analysis: latest.analysis || null,
            canCommand: !!m.can_command,
            capabilities: m.capabilities || null,
            cameraStill: latest.camera_still || null,
            photosChanged: s.photosChanged + 1,
          })
          live.set({ scan: latest.scan || null, motorStatus: latest.motor_status || null, imu: latest.imu || null })
          if (latest.map) showMap(latest.map)
          break
        }
        case 'pose': {
          const last = s.trail[s.trail.length - 1]
          const moved = !last || Math.hypot(m.x - last[0], m.y - last[1]) >= TRAIL_STEP_M
          update({ pose: m, trail: moved ? [...s.trail, [m.x, m.y]] : s.trail })
          break
        }
        case 'map':
          showMap(m)
          break
        case 'detections':
          update({ items: m.items })
          break
        case 'mission':
          update({ mission: m })
          break
        case 'status':
          update({ status: m })
          break
        case 'analysis':
          update({ analysis: m })
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
          update({ cameraStill: m, photosChanged: s.photosChanged + 1 })
          break
        case 'photo_deleted':
          update({ photosChanged: s.photosChanged + 1 })
          break
        case 'review':
          update({ reviews: { ...s.reviews, [m.finding_id]: { ...s.reviews[m.finding_id], ...pick(m) } } })
          break
        case 'report':
          update({ reports: [...s.reports, m] })
          break
        case 'robot_connected':
          update({ robotConnected: true })
          break
        case 'robot_disconnected':
          update({ robotConnected: false })
          break
        case 'session_ended':
          update({ robotConnected: false, session: s.session && { ...s.session, live: false } })
          break
        case 'error':
          console.warn('Prowl server:', m.error)
          update({ lastError: { error: m.error, forType: m.for_type ?? null, at: Date.now() / 1000 } })
          break
        default:
          break // commands, pongs and anything new are not used by this dashboard
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

    const flushTimer = setInterval(() => {
      if (!dirty.current) return
      dirty.current = false
      setData(store.current)
    }, FLUSH_MS)

    connect()
    return () => {
      stopped = true
      clearTimeout(retryTimer)
      clearInterval(flushTimer)
      socket.current?.close()
    }
  }, [token, live])

  // Sends a message to the backend. Returns false if there is no connection right now.
  function send(message) {
    const ws = socket.current
    if (!ws || ws.readyState !== WebSocket.OPEN) return false
    ws.send(JSON.stringify(message))
    return true
  }

  const capabilities = { ...BACKEND_CAPABILITIES, ...data.capabilities }
  return { connection, ...data, capabilities, live, send }
}

function pick(review) {
  const out = {}
  if (review.status != null) out.status = review.status
  if (review.note != null) out.note = review.note
  return out
}
