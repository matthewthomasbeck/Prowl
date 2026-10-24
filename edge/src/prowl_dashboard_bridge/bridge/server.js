// The dashboard side of the bridge: an HTTP server with GET /health and a test page at GET /, and the
// /ws/dashboard WebSocket (see PROTOCOL.md). No ROS in here; index.js passes in the snapshot and the drive gate.

import http from 'node:http'
import { readFileSync } from 'node:fs'
import { timingSafeEqual } from 'node:crypto'
import { WebSocketServer, WebSocket } from 'ws'

const PATH = '/ws/dashboard'
const MAX_INCOMING_BYTES = 16 * 1024
// A dashboard more than this far behind (e.g. on bad Wi-Fi) skips updates until it catches up, so it
// can't make the bridge buffer without limit. Pose and scan come again within a fraction of a second.
const MAX_BUFFERED_BYTES = 1024 * 1024
const HEARTBEAT_MS = 10000
// A page for checking the robot from a browser without the dashboard (LiDAR, wheel duties, driving).
const TEST_PAGE = readFileSync(new URL('./test_page.html', import.meta.url))

function tokenMatches(given, expected) {
  const a = Buffer.from(given || '')
  const b = Buffer.from(expected)
  return a.length === b.length && timingSafeEqual(a, b)
}

// snapshot(canDrive) returns the snapshot message for a new dashboard.
export function startServer({ host, port, driveToken, gate, snapshot, log }) {
  const clients = new Set()

  const httpServer = http.createServer((req, res) => {
    // The page keeps any ?token= in its address, for its WebSocket.
    if (req.method === 'GET' && req.url.split('?')[0] === '/') {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' })
      res.end(TEST_PAGE)
      return
    }
    if (req.method === 'GET' && req.url === '/health') {
      res.writeHead(200, { 'Content-Type': 'application/json' })
      res.end(JSON.stringify({ ok: true, dashboards: clients.size, driving: gate.driving }))
      return
    }
    res.writeHead(404, { 'Content-Type': 'text/plain' })
    res.end('Prowl dashboard bridge: connect a WebSocket to /ws/dashboard, or open / for the test page\n')
  })
  const wss = new WebSocketServer({ server: httpServer, path: PATH, maxPayload: MAX_INCOMING_BYTES })

  function send(client, message) {
    if (client.ws.readyState === WebSocket.OPEN) client.ws.send(JSON.stringify(message))
  }

  function handle(client, message) {
    switch (message.type) {
      case 'drive': {
        if (!client.canDrive) return send(client, { type: 'error', error: 'driving needs the operator token', for_type: 'drive' })
        const result = gate.drive(client, message)
        if (!result.ok) send(client, { type: 'error', error: result.error, for_type: 'drive' })
        return
      }
      case 'stop':
        gate.stop() // anyone may stop the robot
        return
      case 'ping':
        return send(client, { type: 'pong' })
      default:
        return send(client, { type: 'error', error: `the robot bridge doesn't handle "${message.type}"`, for_type: message.type })
    }
  }

  wss.on('connection', (ws, req) => {
    const token = new URL(req.url, 'http://bridge').searchParams.get('token')
    const client = { ws, alive: true, canDrive: !driveToken || tokenMatches(token, driveToken) }
    clients.add(client)
    log.info(`dashboard connected from ${req.socket.remoteAddress} (${client.canDrive ? 'can drive' : 'watch only'})`)
    send(client, snapshot(client.canDrive))

    ws.on('pong', () => {
      client.alive = true
    })
    ws.on('message', (data, isBinary) => {
      let message
      try {
        message = isBinary ? null : JSON.parse(data.toString())
      } catch {
        message = null
      }
      if (!message || typeof message !== 'object' || typeof message.type !== 'string') {
        return send(client, { type: 'error', error: 'messages must be JSON objects with a "type"' })
      }
      handle(client, message)
    })
    ws.on('close', () => {
      clients.delete(client)
      gate.disconnect(client)
      log.info(`dashboard disconnected (${clients.size} left)`)
    })
    ws.on('error', (err) => log.warn(`dashboard socket error: ${err.message}`))
  })

  // Drops dashboards that stopped answering pings, e.g. a laptop that went to sleep.
  const heartbeat = setInterval(() => {
    for (const client of clients) {
      if (!client.alive) {
        client.ws.terminate()
        continue
      }
      client.alive = false
      client.ws.ping()
    }
  }, HEARTBEAT_MS)

  httpServer.listen(port, host, () => log.info(`listening on ws://${host}:${port}${PATH}`))

  return {
    httpServer,
    get dashboards() {
      return clients.size
    },
    broadcast(message) {
      if (clients.size === 0) return
      const text = JSON.stringify(message)
      for (const client of clients) {
        if (client.ws.readyState === WebSocket.OPEN && client.ws.bufferedAmount <= MAX_BUFFERED_BYTES) {
          client.ws.send(text)
        }
      }
    },
    close() {
      clearInterval(heartbeat)
      for (const client of clients) client.ws.close(1001, 'bridge shutting down')
      wss.close()
      httpServer.close()
    },
  }
}
