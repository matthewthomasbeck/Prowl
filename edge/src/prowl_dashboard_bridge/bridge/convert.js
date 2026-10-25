// Turns ROS messages into the dashboard's JSON messages (see docs/protocol.md). No ROS calls in here,
// so it can be tested with plain objects.

function round(value, places) {
  const scale = 10 ** places
  return Math.round(value * scale) / scale
}

export function stampSeconds(header) {
  return header.stamp.sec + header.stamp.nanosec / 1e9
}

// sensor_msgs/LaserScan -> {type: 'scan', t, points: [[x, y], ...]} around the robot (base_link): x forward,
// y left, in metres.
//
// The scan is in the LiDAR's frame; `laser` places the LiDAR on the robot ({x, y, yaw} in base_link).
//
// To keep the message small, bins are taken in groups of `step` and only the nearest valid reading in
// each group is kept: nearest, so a thin obstacle never disappears from the picture.
export function scanMessage(msg, { laser, maxPoints }) {
  const ranges = msg.ranges
  const step = Math.max(1, Math.ceil(ranges.length / maxPoints))
  const points = []

  for (let first = 0; first < ranges.length; first += step) {
    let best = -1
    for (let i = first; i < Math.min(first + step, ranges.length); i++) {
      const r = ranges[i]
      if (Number.isFinite(r) && r >= msg.range_min && r <= msg.range_max && (best < 0 || r < ranges[best])) {
        best = i
      }
    }
    if (best < 0) continue

    const angle = msg.angle_min + best * msg.angle_increment + laser.yaw
    points.push([round(laser.x + ranges[best] * Math.cos(angle), 3), round(laser.y + ranges[best] * Math.sin(angle), 3)])
  }
  return { type: 'scan', t: round(stampSeconds(msg.header), 3), points }
}

// std_msgs/Float64MultiArray from the motor driver -> {type: 'motor_status', wheels, duties}
export function motorStatusMessage(msg, wheels) {
  return { type: 'motor_status', wheels, duties: Array.from(msg.data, (duty) => round(duty, 3)) }
}

// The motor driver's answer to rcl_interfaces/GetParameters for ['dry_run'] -> 'dry_run' or 'live',
// or null if it has no such boolean parameter.
const PARAMETER_BOOL = 1 // rcl_interfaces/ParameterType
export function motorsMode(response) {
  const value = response?.values?.[0]
  if (!value || value.type !== PARAMETER_BOOL) return null
  return value.bool_value ? 'dry_run' : 'live'
}

// sensor_msgs/Imu readings (100 a second from the IMU driver) -> {type: 'imu', t, gyro, accel}
//
// The dashboard only gets a few a second, so rather than one reading picked at random, it gets the
// average of every reading since the last message: steadier, and a short bump still moves it.
// gyro is angular velocity in rad/s and accel is linear acceleration in m/s² (gravity included), each
// [x, y, z] in imu_link (x forward, y left, z up when the board is mounted as docs/hardware.md says).
export class ImuAverage {
  constructor() {
    this.clear()
  }

  clear() {
    this.gyro = [0, 0, 0]
    this.accel = [0, 0, 0]
    this.samples = 0
    this.t = null
  }

  add(msg) {
    const { angular_velocity: w, linear_acceleration: a } = msg
    this.gyro[0] += w.x
    this.gyro[1] += w.y
    this.gyro[2] += w.z
    this.accel[0] += a.x
    this.accel[1] += a.y
    this.accel[2] += a.z
    this.samples += 1
    this.t = stampSeconds(msg.header)
  }

  // The average so far as a message, and starts again. null if nothing came in.
  take() {
    if (this.samples === 0) return null
    const n = this.samples
    const message = {
      type: 'imu',
      t: round(this.t, 3),
      gyro: this.gyro.map((sum) => round(sum / n, 4)),
      accel: this.accel.map((sum) => round(sum / n, 3)),
    }
    this.clear()
    return message
  }
}

// The GPS driver's /gps/fix (sensor_msgs/NavSatFix), with its newest /gps/vel (geometry_msgs/TwistStamped,
// east-north-up) and /gps/status (diagnostic_msgs/DiagnosticStatus) -> {type: 'gps', ...}: see
// docs/protocol.md. Pass velocity only while it's current: the driver stops sending it when the fix goes.
const NAVSAT_NO_FIX = -1 // sensor_msgs/NavSatStatus
export function gpsMessage(fix, { velocity = null, status = null } = {}) {
  const values = Object.fromEntries((status?.values ?? []).map(({ key, value }) => [key, value]))
  const number = (key) => {
    const value = values[key] === undefined || values[key] === '' ? NaN : Number(values[key])
    return Number.isFinite(value) ? value : null
  }
  const hasFix = fix.status.status !== NAVSAT_NO_FIX && Number.isFinite(fix.latitude) && Number.isFinite(fix.longitude)
  const fixType = values.fix === '2d' || values.fix === '3d' ? values.fix : '3d'

  let speed = null
  let course = null
  if (hasFix && velocity) {
    const { x: east, y: north } = velocity.twist.linear
    speed = round(Math.hypot(east, north), 2)
    course = round(((Math.atan2(east, north) * 180) / Math.PI + 360) % 360, 1)
  }

  let satellites = []
  try {
    satellites = JSON.parse(values.satellites ?? '[]').map(([id, elevation, azimuth, cno, used]) => ({ id, elevation, azimuth, cno, used }))
  } catch {
    // an unreadable list: show none
  }

  return {
    type: 'gps',
    t: round(stampSeconds(fix.header), 3),
    fix: hasFix ? fixType : 'none',
    lat: hasFix ? round(fix.latitude, 7) : null,
    lon: hasFix ? round(fix.longitude, 7) : null,
    alt: hasFix ? number('altitude_msl') : null,
    speed,
    course,
    satellites_used: number('satellites_used') ?? 0,
    hdop: number('hdop'),
    vdop: number('vdop'),
    pdop: number('pdop'),
    utc: number('utc'),
    satellites,
  }
}
