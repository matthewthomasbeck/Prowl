// Turns ROS messages into the dashboard's JSON messages (see PROTOCOL.md). No ROS calls in here,
// so it can be tested with plain objects.

import { encodeGrayPng } from './png.js'

// Occupancy values in nav_msgs/OccupancyGrid are 0-100, or -1 for unknown. These thresholds are the
// usual ROS map ones (free below 0.25, occupied from 0.65); the gray values are what the dashboard
// expects: 0 = occupied, 205 = unknown, 254 = free.
const FREE_BELOW = 25
const OCCUPIED_FROM = 65
const PIXEL_OCCUPIED = 0
const PIXEL_UNKNOWN = 205
const PIXEL_FREE = 254

function round(value, places) {
  const scale = 10 ** places
  return Math.round(value * scale) / scale
}

// Heading around z from a unit quaternion (REP 103: counter-clockwise from +x).
export function yawFromQuaternion(q) {
  return Math.atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z))
}

export function stampSeconds(header) {
  return header.stamp.sec + header.stamp.nanosec / 1e9
}

// geometry_msgs/PoseStamped in the map frame -> {type: 'pose', t, x, y, theta}
export function poseMessage(msg) {
  return {
    type: 'pose',
    t: round(stampSeconds(msg.header), 3),
    x: round(msg.pose.position.x, 3),
    y: round(msg.pose.position.y, 3),
    theta: round(yawFromQuaternion(msg.pose.orientation), 4),
  }
}

// sensor_msgs/LaserScan -> {type: 'scan', points: [[x, y], ...]} in the map frame.
//
// The scan is in the LiDAR's frame. `laser` places the LiDAR on the robot ({x, y, yaw} in base_link),
// and `pose` places the robot on the map ({x, y, theta}). Without a pose yet, the robot is taken to be
// at the map origin facing +x.
//
// To keep the message small, bins are taken in groups of `step` and only the nearest valid reading in
// each group is kept: nearest, so a thin obstacle never disappears from the picture.
export function scanMessage(msg, { pose, laser, maxPoints }) {
  const ranges = msg.ranges
  const step = Math.max(1, Math.ceil(ranges.length / maxPoints))
  const robot = pose || { x: 0, y: 0, theta: 0 }
  const cosR = Math.cos(robot.theta)
  const sinR = Math.sin(robot.theta)
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
    const bx = laser.x + ranges[best] * Math.cos(angle) // in base_link
    const by = laser.y + ranges[best] * Math.sin(angle)
    points.push([round(robot.x + bx * cosR - by * sinR, 3), round(robot.y + bx * sinR + by * cosR, 3)])
  }
  return { type: 'scan', t: round(stampSeconds(msg.header), 3), points }
}

// nav_msgs/OccupancyGrid -> {type: 'map', ..., encoding: 'png_base64', data}
//
// The grid is row-major starting at its origin (bottom-left, row 0 = lowest y). PNG row 0 is the top
// of the image, so rows are written in reverse. The origin's orientation is ignored: the grid is
// taken to be axis-aligned with the map frame.
export function mapMessage(msg) {
  const { width, height, resolution, origin } = msg.info
  const cells = msg.data
  const pixels = new Uint8Array(width * height)
  for (let row = 0; row < height; row++) {
    const src = row * width
    const dst = (height - 1 - row) * width
    for (let col = 0; col < width; col++) {
      const value = cells[src + col]
      pixels[dst + col] =
        value < 0 ? PIXEL_UNKNOWN : value < FREE_BELOW ? PIXEL_FREE : value >= OCCUPIED_FROM ? PIXEL_OCCUPIED : PIXEL_UNKNOWN
    }
  }
  return {
    type: 'map',
    resolution: round(resolution, 6),
    width,
    height,
    origin: [round(origin.position.x, 4), round(origin.position.y, 4)],
    encoding: 'png_base64',
    data: encodeGrayPng(width, height, pixels).toString('base64'),
  }
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

// sensor_msgs/Imu readings (100 a second from the IMU driver) -> {type: 'imu', t, gyro, accel, samples}
//
// The dashboard only gets a few a second, so rather than one reading picked at random, it gets the
// average of every reading since the last message: steadier, and a short bump still moves it.
// gyro is angular velocity in rad/s and accel is linear acceleration in m/s² (gravity included), each
// [x, y, z] in imu_link (x forward, y left, z up when the board is mounted as the roadmap says).
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
      samples: n,
    }
    this.clear()
    return message
  }
}
