// The camera's newest video frame and newest still, for the HTTP endpoints in server.js. No ROS in
// here: index.js feeds it from /camera/image_raw/compressed and /camera/still/compressed, and passes
// in `requestCapture`, which calls the camera driver's /camera/capture service.

const STILL_WAIT_MS = 3000 // after a capture, how long to wait for the picture itself to arrive

export class CameraFeed {
  // requestCapture() resolves to {ok: true, message} or {ok: false, error}.
  constructor({ requestCapture }) {
    this.requestCapture = requestCapture
    this.frame = null // {jpeg: Buffer, t}
    this.still = null // {jpeg: Buffer, t}
    this.capturing = false
    this.stillWaiters = []
  }

  pushFrame(jpeg, t) {
    this.frame = { jpeg, t }
  }

  // Stores a new still and returns the camera_still message announcing it.
  setStill(jpeg, t) {
    this.still = { jpeg, t }
    const waiters = this.stillWaiters
    this.stillWaiters = []
    waiters.forEach((resolve) => resolve())
    return this.stillMessage()
  }

  // What the dashboard gets told about the newest still, or null before the first one.
  stillMessage() {
    if (!this.still) return null
    const { t } = this.still
    // The stamp in the URL makes each new picture a new address, so browsers don't show a cached one.
    return { type: 'camera_still', t, url: `/api/camera/still.jpg?t=${t}` }
  }

  // Asks the driver for a picture, one at a time, and waits for it to arrive. Resolves to
  // {ok: true, message} or {ok: false, error, busy}.
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
      return { ok: true, message: result.message }
    } finally {
      this.capturing = false
      this.stillWaiters = []
    }
  }
}
