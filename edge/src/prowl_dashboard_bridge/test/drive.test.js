import { test } from 'node:test'
import assert from 'node:assert/strict'
import { DriveGate } from '../bridge/drive.js'

const ZERO = { linear_x: 0, linear_y: 0, angular_z: 0 }

// A gate with a clock the test moves by hand, recording everything it publishes.
function makeGate() {
  const clock = { now: 0 }
  const published = []
  const gate = new DriveGate({
    linearSpeed: 0.3,
    angularSpeed: 1.0,
    timeout: 0.3,
    publish: (twist) => published.push(twist),
    now: () => clock.now,
  })
  return { gate, published, clock }
}

const close = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-9, `${actual} is not ${expected}`)

test('scales fractions to the top speeds', () => {
  const { gate, published } = makeGate()
  assert.deepEqual(gate.drive('a', { x: 0.5, y: -1, turn: 0.25 }), { ok: true })
  // (0.5, -1) is longer than 1, so it is scaled down to length 1.
  const length = Math.hypot(0.5, 1)
  close(published[0].linear_x, (0.5 / length) * 0.3)
  close(published[0].linear_y, (-1 / length) * 0.3)
  close(published[0].angular_z, 0.25)
  assert.equal(gate.driving, true)
})

test('a diagonal is no faster than straight driving', () => {
  const { gate, published } = makeGate()
  gate.drive('a', { x: 1, y: 1 })
  close(Math.hypot(published[0].linear_x, published[0].linear_y), 0.3)
})

test('clamps values outside [-1, 1] and treats missing ones as 0', () => {
  const { gate, published } = makeGate()
  gate.drive('a', { x: 5, turn: -9 })
  assert.deepEqual(published[0], { linear_x: 0.3, linear_y: 0, angular_z: -1 })
})

test('rejects values that are not numbers, without publishing', () => {
  const { gate, published } = makeGate()
  for (const bad of [{ x: '1' }, { y: NaN }, { turn: Infinity }, { x: true }]) {
    assert.equal(gate.drive('a', bad).ok, false)
  }
  assert.deepEqual(published, [])
  assert.equal(gate.driving, false)
})

test('all zeros is a stop and releases control', () => {
  const { gate, published } = makeGate()
  gate.drive('a', { x: 1 })
  gate.drive('a', { x: 0, y: 0, turn: 0 })
  assert.deepEqual(published.at(-1), ZERO)
  assert.equal(gate.driving, false)
  assert.equal(gate.drive('b', { x: 1 }).ok, true)
})

test('only one dashboard drives at a time', () => {
  const { gate, published } = makeGate()
  gate.drive('a', { x: 1 })
  assert.deepEqual(gate.drive('b', { x: -1 }), { ok: false, error: 'another dashboard is driving' })
  assert.equal(published.length, 1)
})

test('stops once after requests stop coming, then lets others drive', () => {
  const { gate, published, clock } = makeGate()
  gate.drive('a', { x: 1 })
  clock.now = 0.2
  gate.tick()
  assert.equal(published.length, 1, 'still within the timeout')
  gate.drive('a', { x: 1 }) // the next request restarts the timeout
  clock.now = 0.45
  gate.tick()
  assert.equal(published.length, 2)
  clock.now = 0.51
  gate.tick()
  assert.deepEqual(published.at(-1), ZERO)
  gate.tick()
  assert.equal(published.length, 3, 'the stop is published only once')
  assert.equal(gate.drive('b', { x: 1 }).ok, true)
})

test('the driving dashboard disconnecting stops the robot; another one disconnecting does not', () => {
  const { gate, published } = makeGate()
  gate.drive('a', { x: 1 })
  gate.disconnect('b')
  assert.equal(published.length, 1)
  gate.disconnect('a')
  assert.deepEqual(published.at(-1), ZERO)
  assert.equal(gate.driving, false)
})

test('stop works from anyone, even when nobody is driving', () => {
  const { gate, published } = makeGate()
  gate.stop()
  assert.deepEqual(published, [ZERO])
})

test('shutdown stops only if something is driving', () => {
  const { gate, published } = makeGate()
  gate.shutdown()
  assert.deepEqual(published, [])
  gate.drive('a', { x: 1 })
  gate.shutdown()
  assert.deepEqual(published.at(-1), ZERO)
})
