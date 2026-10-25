import { test } from 'node:test'
import assert from 'node:assert/strict'
import { CameraFeed } from '../bridge/camera.js'

test('a new still is announced with a fresh address', () => {
  const feed = new CameraFeed({ requestCapture: async () => ({ ok: true }) })
  assert.equal(feed.stillMessage(), null)
  assert.deepEqual(feed.setStill(Buffer.from('still'), 12.5), {
    type: 'camera_still',
    t: 12.5,
    url: '/api/camera/still.jpg?t=12.5',
  })
})

test('capture waits for the picture and returns it', async () => {
  const feed = new CameraFeed({
    async requestCapture() {
      feed.setStill(Buffer.from('still'), 7) // the driver publishes before it answers
      return { ok: true, message: '4608x2592, 900 KB' }
    },
  })
  assert.deepEqual(await feed.capture(), { ok: true, message: '4608x2592, 900 KB' })
  assert.equal(feed.still.t, 7)
  assert.equal(feed.capturing, false)
})

test('capture passes on failures, and takes one picture at a time', async () => {
  let finish
  const feed = new CameraFeed({ requestCapture: () => new Promise((resolve) => (finish = resolve)) })
  const first = feed.capture()
  assert.deepEqual(await feed.capture(), { ok: false, busy: true, error: 'a picture is already being taken' })
  finish({ ok: false, error: "the camera driver isn't running" })
  assert.deepEqual(await first, { ok: false, error: "the camera driver isn't running" })
  assert.equal(feed.capturing, false)
})
