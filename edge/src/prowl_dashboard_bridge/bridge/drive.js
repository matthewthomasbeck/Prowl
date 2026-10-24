// Decides what reaches /cmd_vel. Dashboards send drive requests as fractions of the configured top
// speeds; this turns them into velocities and makes sure the robot stops when they stop coming.
//
// Rules:
// - One dashboard drives at a time. It keeps control while its requests keep coming; others are told
//   someone else is driving.
// - Requests have to keep coming (the client module sends them 10 times a second while a control is
//   held). After `timeout` seconds without one, a single stop is published and control is released.
//   The motor driver has its own timeout as a second safety net.
// - Any dashboard may send stop, whether or not it is the one driving.
// - When the driving dashboard disconnects, or the bridge shuts down, a stop is published.

const ZERO = { linear_x: 0, linear_y: 0, angular_z: 0 }

function fraction(value) {
  if (value === undefined || value === null) return 0
  if (typeof value !== 'number' || !Number.isFinite(value)) return null
  return Math.max(-1, Math.min(1, value))
}

export class DriveGate {
  // publish(twist) receives {linear_x, linear_y, angular_z}; now() returns seconds.
  constructor({ linearSpeed, angularSpeed, timeout, publish, now = () => performance.now() / 1000 }) {
    this.linearSpeed = linearSpeed
    this.angularSpeed = angularSpeed
    this.timeout = timeout
    this.publish = publish
    this.now = now
    this.driver = null // the client that has control
    this.lastRequest = 0
  }

  get driving() {
    return this.driver !== null
  }

  // request: {x, y, turn}, each in [-1, 1]: x forward, y left (strafe), turn counter-clockwise.
  drive(client, request) {
    const x = fraction(request.x)
    const y = fraction(request.y)
    const turn = fraction(request.turn)
    if (x === null || y === null || turn === null) return { ok: false, error: 'x, y and turn must be numbers' }
    if (this.driver !== null && this.driver !== client) return { ok: false, error: 'another dashboard is driving' }
    if (x === 0 && y === 0 && turn === 0) {
      this.stop()
      return { ok: true }
    }

    // A diagonal (x and y both 1) is limited to the same top speed as straight driving.
    const length = Math.hypot(x, y)
    const scale = length > 1 ? 1 / length : 1
    this.driver = client
    this.lastRequest = this.now()
    this.publish({
      linear_x: x * scale * this.linearSpeed,
      linear_y: y * scale * this.linearSpeed,
      angular_z: turn * this.angularSpeed,
    })
    return { ok: true }
  }

  stop() {
    this.driver = null
    this.publish(ZERO)
  }

  disconnect(client) {
    if (this.driver === client) this.stop()
  }

  // Called regularly: releases control once requests have stopped coming.
  tick() {
    if (this.driver !== null && this.now() - this.lastRequest > this.timeout) this.stop()
  }

  shutdown() {
    if (this.driver !== null) this.stop()
  }
}
