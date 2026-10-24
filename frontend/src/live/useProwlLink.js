import { useEffect, useRef, useState } from 'react'
import { recolorMap } from './liveMap.js'

// ---- The live connection to the Prowl backend ----
// Opens the backend's /ws/dashboard WebSocket on the same address as this page. During `npm run dev`,
// Vite forwards it to the backend (see vite.config.js). When the backend serves the built dashboard,
// it is simply the same server.
//
// If there is no backend, the connection just fails and keeps retrying quietly, and the dashboard
// shows its demo data. Nothing here invents data: everything comes from the backend's messages.
//
// The backend sends one "snapshot" with the whole current state, then one message per update.
// Pose updates arrive about 10 times a second, so updates are collected and handed to React a few
// times a second instead of re-drawing the page for every single message.

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
  status: null, // { mode, battery_v, ... }
  analysis: null, // { explored_m2, warnings }
}

// A ?token=... on this page's address is passed on: it's the operator token for the backend, or the drive
// token for the robot bridge (edge/src/prowl_dashboard_bridge).
function socketUrl() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  const token = new URLSearchParams(window.location.search).get('token')
  const query = token ? `?token=${encodeURIComponent(token)}` : ''
  return `${protocol}//${window.location.host}/ws/dashboard${query}`
}

export function useProwlLink() {
  const [connection, setConnection] = useState('connecting') // 'connecting', 'online' or 'offline'
  const [data, setData] = useState(empty)
  const store = useRef({ ...empty }) // always the newest state; copied into React a few times a second
  const dirty = useRef(false)
  const socket = useRef(null)

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
          })
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
          console.warn('Prowl backend:', m.error)
          break
        default:
          break // scans, commands and anything new are not used by this dashboard
      }
    }

    function connect() {
      const ws = new WebSocket(socketUrl())
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
  }, [])

  // Sends a message to the backend. Returns false if there is no connection right now.
  function send(message) {
    const ws = socket.current
    if (!ws || ws.readyState !== WebSocket.OPEN) return false
    ws.send(JSON.stringify(message))
    return true
  }

  return { connection, ...data, send }
}

function pick(review) {
  const out = {}
  if (review.status != null) out.status = review.status
  if (review.note != null) out.note = review.note
  return out
}
