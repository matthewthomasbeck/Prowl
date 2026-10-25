import { test } from 'node:test'
import assert from 'node:assert/strict'
import { CameraFeed, jpegSize } from '../bridge/camera.js'

// The start of a JPEG: SOI, an APP0 segment, then a baseline frame header (SOF0) for width x height.
function jpegHeader(width, height, sof = 0xc0) {
  const app0 = [0xff, 0xe0, 0x00, 0x06, 0x4a, 0x46, 0x49, 0x46]
  const sofSegment = [0xff, sof, 0x00, 0x0b, 0x08, height >> 8, height & 0xff, width >> 8, width & 0xff, 0x01, 0x01, 0x11, 0x00]
  return Buffer.from([0xff, 0xd8, ...app0, ...sofSegment, 0xff, 0xd9])
}

test('jpegSize reads the frame header', () => {
  assert.deepEqual(jpegSize(jpegHeader(4608, 2592)), { width: 4608, height: 2592 })
  assert.deepEqual(jpegSize(jpegHeader(1280, 720, 0xc2)), { width: 1280, height: 720 }) // progressive
})

test('jpegSize gives null for anything else', () => {
  assert.equal(jpegSize(Buffer.from('not a jpeg')), null)
  assert.equal(jpegSize(Buffer.from([0xff, 0xd8, 0xff, 0xd9])), null) // no frame header
  assert.equal(jpegSize(jpegHeader(64, 48).subarray(0, 16)), null) // cut off
})

test('frame listeners get every frame until they unsubscribe', () => {
  const feed = new CameraFeed({ requestCapture: async () => ({ ok: true }) })
  const seen = []
  const stop = feed.onFrame((frame) => seen.push(frame.t))
  feed.pushFrame(Buffer.from([1]), 1)
  feed.pushFrame(Buffer.from([2]), 2)
  stop()
  feed.pushFrame(Buffer.from([3]), 3)
  assert.deepEqual(seen, [1, 2])
  assert.equal(feed.frame.t, 3)
})

test('a new still is announced with its size and a fresh address', () => {
  const feed = new CameraFeed({ requestCapture: async () => ({ ok: true }) })
  assert.equal(feed.stillMessage(), null)
  const jpeg = jpegHeader(4608, 2592)
  assert.deepEqual(feed.setStill(jpeg, 12.5), {
    type: 'camera_still',
    t: 12.5,
    width: 4608,
    height: 2592,
    bytes: jpeg.length,
    url: '/api/camera/still.jpg?t=12.5',
  })
})

test('capture waits for the picture and returns it', async () => {
  const feed = new CameraFeed({
    async requestCapture() {
      feed.setStill(jpegHeader(4608, 2592), 7) // the driver publishes before it answers
      return { ok: true, message: '4608x2592, 900 KB' }
    },
  })
  const result = await feed.capture()
  assert.equal(result.ok, true)
  assert.equal(result.message, '4608x2592, 900 KB')
  assert.equal(result.still.t, 7)
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
