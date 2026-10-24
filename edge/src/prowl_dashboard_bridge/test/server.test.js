import { test } from 'node:test'
import assert from 'node:assert/strict'
import { WebSocket } from 'ws'
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

async function start({ driveToken = '' } = {}) {
  const gate = fakeGate()
  const server = startServer({
    host: '127.0.0.1',
    port: 0,
    driveToken,
    gate,
    snapshot: (canDrive) => ({ type: 'snapshot', can_command: canDrive }),
    log: quiet,
  })
  await new Promise((resolve) => server.httpServer.once('listening', resolve))
  const { port } = server.httpServer.address()
  return { server, gate, base: `127.0.0.1:${port}` }
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
  assert.deepEqual(await watcher.next(), { type: 'error', error: 'driving needs the operator token', for_type: 'drive' })
  watcher.send(JSON.stringify({ type: 'stop' }))

  const operator = await connect(`ws://${base}/ws/dashboard?token=secret`)
  assert.deepEqual(await operator.next(), { type: 'snapshot', can_command: true })
  operator.send(JSON.stringify({ type: 'drive', x: 1 }))
  await until(() => gate.calls.length === 2)
  assert.deepEqual(gate.calls, [['stop'], ['drive', 1]])
  watcher.close()
  operator.close()
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
  ws.send(JSON.stringify({ type: 'review', finding_id: 'P-001' }))
  assert.deepEqual(await ws.next(), { type: 'error', error: `the robot bridge doesn't handle "review"`, for_type: 'review' })
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
  server.broadcast({ type: 'pose', x: 1, y: 2, theta: 0 })
  assert.deepEqual(await a.next(), { type: 'pose', x: 1, y: 2, theta: 0 })
  assert.deepEqual(await b.next(), { type: 'pose', x: 1, y: 2, theta: 0 })
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

test('GET /health, the test page at /, and other paths are not found', async (t) => {
  const { server, base } = await start()
  t.after(() => server.close())
  const health = await fetch(`http://${base}/health`)
  assert.deepEqual(await health.json(), { ok: true, dashboards: 0, driving: false })
  for (const path of ['/', '/?token=secret']) {
    const page = await fetch(`http://${base}${path}`)
    assert.equal(page.status, 200)
    assert.match(page.headers.get('content-type'), /^text\/html/)
    assert.match(await page.text(), /<title>Prowl test page<\/title>/)
  }
  assert.equal((await fetch(`http://${base}/other`)).status, 404)
})

test('rejects WebSockets on other paths', async (t) => {
  const { server, base } = await start()
  t.after(() => server.close())
  await assert.rejects(connect(`ws://${base}/ws/robot`))
})
