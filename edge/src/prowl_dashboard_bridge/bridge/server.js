// The dashboard side of the bridge: an HTTP server with GET /health, the camera and the saved photos
// under /api/camera/, and the /ws/dashboard WebSocket (see docs/protocol.md). No ROS in here; index.js
// passes in the snapshot, the drive gate, the camera feed and the photo store.

import http from 'node:http'
import { timingSafeEqual } from 'node:crypto'
import { WebSocketServer, WebSocket } from 'ws'

const PATH = '/ws/dashboard'
const MAX_INCOMING_BYTES = 16 * 1024
// A dashboard more than this far behind (e.g. on bad Wi-Fi) skips updates until it catches up, so it
// can't make the bridge buffer without limit. Scans and wheel duties come again within a fraction of a second.
const MAX_BUFFERED_BYTES = 1024 * 1024
const HEARTBEAT_MS = 10000
const NO_CACHE = { 'Cache-Control': 'no-store' }

function tokenMatches(given, expected) {
  const a = Buffer.from(given || '')
  const b = Buffer.from(expected)
  return a.length === b.length && timingSafeEqual(a, b)
}

function sendJson(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json', ...NO_CACHE })
  res.end(JSON.stringify(body))
}

function sendJpeg(res, jpeg) {
  res.writeHead(200, { 'Content-Type': 'image/jpeg', 'Content-Length': jpeg.length, ...NO_CACHE })
  res.end(jpeg)
}

function sendSavedJpeg(res, jpeg) {
  // A saved photo never changes under its name, so the browser may keep it (the photos page shows many)
  res.writeHead(200, { 'Content-Type': 'image/jpeg', 'Content-Length': jpeg.length, 'Cache-Control': 'private, max-age=86400, immutable' })
  res.end(jpeg)
}

// The token an HTTP request carries: ?token=... or an "Authorization: Bearer ..." header.
function requestToken(req) {
  const fromQuery = new URL(req.url, 'http://bridge').searchParams.get('token')
  if (fromQuery) return fromQuery
  const header = req.headers.authorization || ''
  return header.startsWith('Bearer ') ? header.slice(7) : ''
}

const PHOTO_PATH = /^\/api\/camera\/photos\/([A-Za-z0-9-]+)\.jpg$/

// snapshot(canDrive) returns the snapshot message for a new dashboard. camera is a CameraFeed (camera.js)
// and photos a PhotoStore (photos.js).
export function startServer({ host, port, driveToken, gate, snapshot, camera, photos, log }) {
  const clients = new Set()

  async function capture(res) {
    const result = await camera.capture()
    if (result.ok) {
      log.info(`camera still: ${result.message}`)
      sendJson(res, 200, { ok: true, message: result.message })
    } else {
      sendJson(res, result.busy ? 409 : 503, { ok: false, error: result.error })
    }
  }

  const httpServer = http.createServer((req, res) => {
    const path = req.url.split('?')[0]
    if (req.method === 'GET' && path === '/health') {
      sendJson(res, 200, { ok: true, dashboards: clients.size, driving: gate.driving })
      return
    }
    if (req.method === 'GET' && path === '/api/camera/frame.jpg') {
      if (camera.frame) sendJpeg(res, camera.frame.jpeg)
      else sendJson(res, 503, { ok: false, error: 'no video from the camera yet' })
      return
    }
    if (req.method === 'GET' && path === '/api/camera/still.jpg') {
      if (camera.still) sendJpeg(res, camera.still.jpeg)
      else sendJson(res, 404, { ok: false, error: 'no picture taken yet' })
      return
    }
    if (req.method === 'POST' && path === '/api/camera/capture') {
      req.resume() // no body needed
      capture(res).catch((err) => sendJson(res, 500, { ok: false, error: err.message }))
      return
    }
    if (req.method === 'GET' && path === '/api/camera/photos') {
      sendJson(res, 200, { ok: true, photos: photos.list() })
      return
    }
    const photo = path.match(PHOTO_PATH)
    if (photo && req.method === 'GET') {
      const jpeg = photos.read(photo[1])
      if (jpeg) sendSavedJpeg(res, jpeg)
      else sendJson(res, 404, { ok: false, error: 'no such photo' })
      return
    }
    // Deleting needs the drive token, when one is set: taking photos is harmless, losing them isn't.
    if (photo && req.method === 'DELETE') {
      req.resume()
      if (driveToken && !tokenMatches(requestToken(req), driveToken)) {
        sendJson(res, 403, { ok: false, error: 'deleting photos needs the drive token' })
      } else if (!photos.remove(photo[1])) {
        sendJson(res, 404, { ok: false, error: 'no such photo' })
      } else {
        log.info(`photo deleted: ${photo[1]}`)
        broadcast({ type: 'photo_deleted', name: photo[1] })
        sendJson(res, 200, { ok: true })
      }
      return
    }
    res.writeHead(404, { 'Content-Type': 'text/plain' })
    res.end(
      'Prowl dashboard bridge: the dashboard connects a WebSocket to /ws/dashboard and gets the camera from ' +
        '/api/camera/ (frame.jpg, still.jpg, capture, photos). The dashboard itself is at the address ./prowl prints.\n',
    )
  })
  const wss = new WebSocketServer({ server: httpServer, path: PATH, maxPayload: MAX_INCOMING_BYTES })

  function send(client, message) {
    if (client.ws.readyState === WebSocket.OPEN) client.ws.send(JSON.stringify(message))
  }

  function handle(client, message) {
    switch (message.type) {
      case 'drive': {
        if (!client.canDrive) return send(client, { type: 'error', error: 'driving needs the drive token', for_type: 'drive' })
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

  function broadcast(message) {
    if (clients.size === 0) return
    const text = JSON.stringify(message)
    for (const client of clients) {
      if (client.ws.readyState === WebSocket.OPEN && client.ws.bufferedAmount <= MAX_BUFFERED_BYTES) {
        client.ws.send(text)
      }
    }
  }

  httpServer.listen(port, host, () => log.info(`listening on ws://${host}:${port}${PATH}`))

  return {
    httpServer,
    get dashboards() {
      return clients.size
    },
    broadcast,
    close() {
      clearInterval(heartbeat)
      for (const client of clients) client.ws.close(1001, 'bridge shutting down')
      wss.close()
      httpServer.close()
    },
  }
}
