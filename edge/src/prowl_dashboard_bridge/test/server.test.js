import { test } from 'node:test'
import assert from 'node:assert/strict'
import { WebSocket } from 'ws'
import { CameraFeed } from '../bridge/camera.js'
import { startServer } from '../bridge/server.js'

const quiet = { info() {}, warn() {}, error() {} }

// A stand-in for DriveGate that records calls.
function fakeGate() {
  const calls = []
  return {
    calls,
    driving: false,
    drive(client, request) {
      calls.push(['drive', request.x])
      return request.x === 9 ? { ok: false, error: 'nope' } : { ok: true }
    },
    stop() {
      calls.push(['stop'])
    },
    disconnect() {
      calls.push(['disconnect'])
    },
  }
}

// A stand-in for PhotoStore with one photo in it.
function fakePhotos() {
  const name = 'prowl-20261024-120000-000'
  const entry = { name, t: 1, width: 4, height: 3, bytes: 5, url: `/api/camera/photos/${name}.jpg` }
  const kept = new Map([[name, Buffer.from('photo')]])
  return {
    name,
    list: () => (kept.size ? [entry] : []),
    read: (n) => kept.get(n) ?? null,
    remove: (n) => kept.delete(n),
  }
}

async function start({ driveToken = '', requestCapture = async () => ({ ok: true, message: 'taken' }), photos = fakePhotos() } = {}) {
  const gate = fakeGate()
  const camera = new CameraFeed({ requestCapture })
  const server = startServer({
    host: '127.0.0.1',
    port: 0,
    driveToken,
    gate,
    snapshot: (canDrive) => ({ type: 'snapshot', can_command: canDrive }),
    camera,
    photos,
    log: quiet,
  })
  await new Promise((resolve) => server.httpServer.once('listening', resolve))
  const { port } = server.httpServer.address()
  return { server, gate, camera, base: `127.0.0.1:${port}` }
}

// Connects and collects messages; next() waits for the next one.
async function connect(url) {
  const ws = new WebSocket(url)
  const queue = []
  const waiting = []
  ws.on('message', (data) => {
    const message = JSON.parse(data.toString())
    const resolve = waiting.shift()
    resolve ? resolve(message) : queue.push(message)
  })
  await new Promise((resolve, reject) => {
    ws.once('open', resolve)
    ws.once('error', reject)
  })
  ws.next = () => (queue.length ? Promise.resolve(queue.shift()) : new Promise((resolve) => waiting.push(resolve)))
  return ws
}

const until = async (check) => {
  for (let i = 0; i < 100 && !check(); i++) await new Promise((resolve) => setTimeout(resolve, 10))
  assert.ok(check())
}

test('a new dashboard gets a snapshot first, and can drive when no token is set', async (t) => {
  const { server, gate, base } = await start()
  t.after(() => server.close())
  const ws = await connect(`ws://${base}/ws/dashboard`)
  assert.deepEqual(await ws.next(), { type: 'snapshot', can_command: true })
  ws.send(JSON.stringify({ type: 'drive', x: 0.5 }))
  ws.send(JSON.stringify({ type: 'ping' }))
  assert.deepEqual(await ws.next(), { type: 'pong' })
  assert.deepEqual(gate.calls, [['drive', 0.5]])
  ws.close()
})

test('with a drive token, only dashboards that give it can drive; anyone can stop', async (t) => {
  const { server, gate, base } = await start({ driveToken: 'secret' })
  t.after(() => server.close())

  const watcher = await connect(`ws://${base}/ws/dashboard?token=wrong`)
  assert.deepEqual(await watcher.next(), { type: 'snapshot', can_command: false })
  watcher.send(JSON.stringify({ type: 'drive', x: 1 }))
  assert.deepEqual(await watcher.next(), { type: 'error', error: 'driving needs the drive token', for_type: 'drive' })
  watcher.send(JSON.stringify({ type: 'stop' }))

  const driver = await connect(`ws://${base}/ws/dashboard?token=secret`)
  assert.deepEqual(await driver.next(), { type: 'snapshot', can_command: true })
  driver.send(JSON.stringify({ type: 'drive', x: 1 }))
  await until(() => gate.calls.length === 2)
  assert.deepEqual(gate.calls, [['stop'], ['drive', 1]])
  watcher.close()
  driver.close()
})

test('reports rejected drives, bad messages and unknown types', async (t) => {
  const { server, base } = await start()
  t.after(() => server.close())
  const ws = await connect(`ws://${base}/ws/dashboard`)
  await ws.next()
  ws.send(JSON.stringify({ type: 'drive', x: 9 }))
  assert.deepEqual(await ws.next(), { type: 'error', error: 'nope', for_type: 'drive' })
  ws.send('not json')
  assert.equal((await ws.next()).type, 'error')
  ws.send(JSON.stringify([1, 2]))
  assert.equal((await ws.next()).type, 'error')
  ws.send(JSON.stringify({ type: 'teleport' }))
  assert.deepEqual(await ws.next(), { type: 'error', error: `the robot bridge doesn't handle "teleport"`, for_type: 'teleport' })
  ws.close()
})

test('broadcast reaches every dashboard', async (t) => {
  const { server, base } = await start()
  t.after(() => server.close())
  const a = await connect(`ws://${base}/ws/dashboard`)
  const b = await connect(`ws://${base}/ws/dashboard`)
  await a.next()
  await b.next()
  assert.equal(server.dashboards, 2)
  server.broadcast({ type: 'status', motors: 'live' })
  assert.deepEqual(await a.next(), { type: 'status', motors: 'live' })
  assert.deepEqual(await b.next(), { type: 'status', motors: 'live' })
  a.close()
  b.close()
})

test('a dashboard disconnecting is passed on to the gate', async (t) => {
  const { server, gate, base } = await start()
  t.after(() => server.close())
  const ws = await connect(`ws://${base}/ws/dashboard`)
  await ws.next()
  ws.close()
  await until(() => gate.calls.some(([name]) => name === 'disconnect'))
  assert.equal(server.dashboards, 0)
})

test('GET /health, and other paths are not found', async (t) => {
  const { server, base } = await start()
  t.after(() => server.close())
  const health = await fetch(`http://${base}/health`)
  assert.deepEqual(await health.json(), { ok: true, dashboards: 0, driving: false })
  for (const path of ['/', '/other', '/api/camera/video.mjpg']) {
    assert.equal((await fetch(`http://${base}${path}`)).status, 404)
  }
})

test('rejects WebSockets on other paths', async (t) => {
  const { server, base } = await start()
  t.after(() => server.close())
  await assert.rejects(connect(`ws://${base}/ws/robot`))
})

test('the newest video frame and still as single JPEGs', async (t) => {
  const { server, camera, base } = await start()
  t.after(() => server.close())
  const frame = await fetch(`http://${base}/api/camera/frame.jpg`)
  assert.equal(frame.status, 503)
  assert.equal((await frame.json()).ok, false)
  assert.equal((await fetch(`http://${base}/api/camera/still.jpg`)).status, 404)

  camera.pushFrame(Buffer.from('frame-1'), 1)
  camera.setStill(Buffer.from('still-1'), 2)
  const latest = await fetch(`http://${base}/api/camera/frame.jpg`)
  assert.equal(latest.headers.get('content-type'), 'image/jpeg')
  assert.equal(latest.headers.get('cache-control'), 'no-store')
  assert.equal(await latest.text(), 'frame-1')
  assert.equal(await (await fetch(`http://${base}/api/camera/still.jpg?t=2`)).text(), 'still-1')
})

test('POST /api/camera/capture takes a picture, or says why not', async (t) => {
  let camera
  let answer = { ok: true, message: '4608x2592, 900 KB' }
  const started = await start({
    async requestCapture() {
      if (answer.ok) camera.setStill(Buffer.from('still'), 5)
      return answer
    },
  })
  camera = started.camera
  t.after(() => started.server.close())
  const url = `http://${started.base}/api/camera/capture`

  const ok = await fetch(url, { method: 'POST' })
  assert.equal(ok.status, 200)
  assert.deepEqual(await ok.json(), { ok: true, message: '4608x2592, 900 KB' })
  assert.equal(camera.still.t, 5)

  answer = { ok: false, error: "the camera driver isn't running" }
  const failed = await fetch(url, { method: 'POST' })
  assert.equal(failed.status, 503)
  assert.deepEqual(await failed.json(), { ok: false, error: "the camera driver isn't running" })
  assert.equal((await fetch(url)).status, 404) // GET isn't a capture
})

test('a second capture while one is running gets 409', async (t) => {
  let finish
  const { server, base } = await start({ requestCapture: () => new Promise((resolve) => (finish = resolve)) })
  t.after(() => server.close())
  const url = `http://${base}/api/camera/capture`
  const first = fetch(url, { method: 'POST' })
  await until(() => finish)
  const second = await fetch(url, { method: 'POST' })
  assert.equal(second.status, 409)
  finish({ ok: false, error: 'stopped' })
  assert.equal((await first).status, 503)
})

test('the photos: listed, served, and deleted only with the token', async (t) => {
  const photos = fakePhotos()
  const { server, base } = await start({ driveToken: 'secret', photos })
  t.after(() => server.close())
  const watcher = await connect(`ws://${base}/ws/dashboard`)
  await watcher.next() // the snapshot

  const list = await (await fetch(`http://${base}/api/camera/photos`)).json()
  assert.equal(list.ok, true)
  assert.equal(list.photos[0].name, photos.name)
  const jpeg = await fetch(`http://${base}/api/camera/photos/${photos.name}.jpg`)
  assert.equal(jpeg.headers.get('content-type'), 'image/jpeg')
  assert.match(jpeg.headers.get('cache-control'), /immutable/)
  assert.equal(await jpeg.text(), 'photo')
  assert.equal((await fetch(`http://${base}/api/camera/photos/nope.jpg`)).status, 404)
  assert.equal((await fetch(`http://${base}/api/camera/photos/..%2F..%2Fsecret.jpg`)).status, 404)

  const url = `http://${base}/api/camera/photos/${photos.name}.jpg`
  assert.equal((await fetch(url, { method: 'DELETE' })).status, 403)
  assert.equal((await fetch(url, { method: 'DELETE', headers: { Authorization: 'Bearer wrong' } })).status, 403)
  assert.equal((await fetch(`${url}?token=secret`, { method: 'DELETE' })).status, 200)
  assert.deepEqual(await watcher.next(), { type: 'photo_deleted', name: photos.name })
  assert.equal((await fetch(url, { method: 'DELETE', headers: { Authorization: 'Bearer secret' } })).status, 404)
  assert.deepEqual((await (await fetch(`http://${base}/api/camera/photos`)).json()).photos, [])
  watcher.close()
})
