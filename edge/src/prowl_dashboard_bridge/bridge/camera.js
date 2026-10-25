// The camera's newest video frame and newest still, for the HTTP endpoints in server.js. No ROS in
// here: index.js feeds it from /camera/image_raw/compressed and /camera/still/compressed, and passes
// in `requestCapture`, which calls the camera driver's /camera/capture service.

const STILL_WAIT_MS = 3000 // after a capture, how long to wait for the picture itself to arrive

// Width and height from a JPEG's start-of-frame header, or null if it isn't a JPEG.
export function jpegSize(jpeg) {
  if (jpeg.length < 4 || jpeg[0] !== 0xff || jpeg[1] !== 0xd8) return null
  let i = 2
  while (i + 4 <= jpeg.length) {
    if (jpeg[i] !== 0xff) return null
    const marker = jpeg[i + 1]
    if (marker === 0xff) {
      i += 1 // fill byte
      continue
    }
    // SOF0-SOF15 are 0xC0-0xCF, except DHT (C4), JPG (C8) and DAC (CC). After the length come the
    // sample precision (1 byte), then height and width (2 bytes each).
    if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc) {
      if (i + 9 > jpeg.length) return null
      return { width: jpeg.readUInt16BE(i + 7), height: jpeg.readUInt16BE(i + 5) }
    }
    if (marker === 0xd9 || marker === 0xda) return null // end of image, or image data, with no frame header
    i += 2 + jpeg.readUInt16BE(i + 2)
  }
  return null
}

export class CameraFeed {
  // requestCapture() resolves to {ok: true, message} or {ok: false, error}.
  constructor({ requestCapture }) {
    this.requestCapture = requestCapture
    this.frame = null // {jpeg: Buffer, t}
    this.still = null // {jpeg: Buffer, t, width, height}
    this.capturing = false
    this.frameListeners = new Set()
    this.stillWaiters = []
  }

  pushFrame(jpeg, t) {
    this.frame = { jpeg, t }
    for (const listener of this.frameListeners) listener(this.frame)
  }

  // Calls listener(frame) for every new video frame until the returned function is called.
  onFrame(listener) {
    this.frameListeners.add(listener)
    return () => this.frameListeners.delete(listener)
  }

  // Stores a new still and returns the camera_still message announcing it.
  setStill(jpeg, t) {
    const size = jpegSize(jpeg)
    this.still = { jpeg, t, width: size?.width ?? null, height: size?.height ?? null }
    const waiters = this.stillWaiters
    this.stillWaiters = []
    waiters.forEach((resolve) => resolve())
    return this.stillMessage()
  }

  // What the dashboard gets told about the newest still, or null before the first one.
  stillMessage() {
    if (!this.still) return null
    const { t, width, height, jpeg } = this.still
    // The stamp in the URL makes each new picture a new address, so browsers don't show a cached one.
    return { type: 'camera_still', t, width, height, bytes: jpeg.length, url: `/api/camera/still.jpg?t=${t}` }
  }

  // Asks the driver for a picture, one at a time, and waits for it to arrive. Resolves to
  // {ok: true, message, still} or {ok: false, error, busy}.
  async capture() {
    if (this.capturing) return { ok: false, busy: true, error: 'a picture is already being taken' }
    this.capturing = true
    try {
      const arrived = new Promise((resolve) => this.stillWaiters.push(resolve))
      const result = await this.requestCapture()
      if (!result.ok) return { ok: false, error: result.error }
      // The driver publishes the picture just before it answers, so it's normally here already.
      let timer
      const late = new Promise((resolve) => (timer = setTimeout(resolve, STILL_WAIT_MS)))
      await Promise.race([arrived, late])
      clearTimeout(timer)
      return { ok: true, message: result.message, still: this.stillMessage() }
    } finally {
      this.capturing = false
      this.stillWaiters = []
    }
  }
}
