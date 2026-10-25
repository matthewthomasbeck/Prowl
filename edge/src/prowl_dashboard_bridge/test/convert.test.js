import { test } from 'node:test'
import assert from 'node:assert/strict'
import { inflateSync } from 'node:zlib'
import { ImuAverage, mapMessage, motorStatusMessage, motorsMode, poseMessage, scanMessage, yawFromQuaternion } from '../bridge/convert.js'

const header = { stamp: { sec: 100, nanosec: 500000000 }, frame_id: 'map' }
const close = (actual, expected, tolerance = 1e-3) =>
  assert.ok(Math.abs(actual - expected) <= tolerance, `${actual} is not close to ${expected}`)

// A unit quaternion for a rotation of `yaw` around z.
const aroundZ = (yaw) => ({ x: 0, y: 0, z: Math.sin(yaw / 2), w: Math.cos(yaw / 2) })

test('yaw from a quaternion, counter-clockwise from +x', () => {
  close(yawFromQuaternion(aroundZ(0)), 0)
  close(yawFromQuaternion(aroundZ(Math.PI / 2)), Math.PI / 2)
  close(yawFromQuaternion(aroundZ(-2.5)), -2.5)
  close(Math.abs(yawFromQuaternion(aroundZ(Math.PI))), Math.PI)
})

test('pose message', () => {
  const msg = { header, pose: { position: { x: 1.23456, y: -2, z: 0 }, orientation: aroundZ(1) } }
  assert.deepEqual(poseMessage(msg), { type: 'pose', t: 100.5, x: 1.235, y: -2, theta: 1 })
})

function laserScan(ranges, { angleMin = 0, increment = Math.PI / 2 } = {}) {
  return { header, angle_min: angleMin, angle_increment: increment, range_min: 0.1, range_max: 10, ranges }
}
const noOffset = { x: 0, y: 0, yaw: 0 }

test('scan points at the map origin when there is no pose yet', () => {
  // Bins at 0, 90, 180 and 270 degrees.
  const { type, t, points } = scanMessage(laserScan([1, 2, 3, 4]), { pose: null, laser: noOffset, maxPoints: 180 })
  assert.equal(type, 'scan')
  assert.equal(t, 100.5)
  assert.deepEqual(points, [[1, 0], [0, 2], [-3, 0], [-0, -4]])
})

test('scan points move with the robot pose and the LiDAR mounting', () => {
  // Robot at (1, 2) facing +y; LiDAR 0.1 m ahead of base_link, mounted backwards.
  const pose = { x: 1, y: 2, theta: Math.PI / 2 }
  const laser = { x: 0.1, y: 0, yaw: Math.PI }
  const { points } = scanMessage(laserScan([1]), { pose, laser, maxPoints: 180 })
  // The LiDAR's 0 degrees points behind the robot: 0.9 m behind base_link, which is -y on the map.
  close(points[0][0], 1)
  close(points[0][1], 1.1)
})

test('scan with a negative angle_increment, like the LD19 (bins go clockwise)', () => {
  const { points } = scanMessage(laserScan([1, 2], { increment: -Math.PI / 2 }), { pose: null, laser: noOffset, maxPoints: 180 })
  // Bin 1 is 90 degrees clockwise from the front: the robot's right, -y.
  close(points[1][0], 0)
  close(points[1][1], -2)
})

test('scan drops invalid readings', () => {
  const ranges = [NaN, Infinity, 0.05, 11, 0, 5]
  const { points } = scanMessage(laserScan(ranges, { increment: 0.1 }), { pose: null, laser: noOffset, maxPoints: 180 })
  assert.equal(points.length, 1)
  close(Math.hypot(...points[0]), 5)
})

test('scan keeps the nearest reading of each group', () => {
  // 8 bins down to 2 points: groups [5, 2, 9, NaN] and [7, 7, 0.5, 3].
  const ranges = [5, 2, 9, NaN, 7, 7, 0.5, 3]
  const { points } = scanMessage(laserScan(ranges, { increment: 0.01 }), { pose: null, laser: noOffset, maxPoints: 2 })
  assert.equal(points.length, 2)
  close(Math.hypot(...points[0]), 2)
  close(Math.hypot(...points[1]), 0.5)
})

test('scan never sends more than maxPoints', () => {
  const ranges = Array.from({ length: 456 }, () => 3)
  const { points } = scanMessage(laserScan(ranges, { increment: 0.0138 }), { pose: null, laser: noOffset, maxPoints: 180 })
  assert.ok(points.length <= 180, `${points.length} points`)
  assert.ok(points.length >= 150, `${points.length} points`)
})

function pngPixels(base64, width, height) {
  const png = Buffer.from(base64, 'base64')
  // IDAT is the second chunk, straight after the 8-byte signature and the 25-byte IHDR chunk.
  const length = png.readUInt32BE(33)
  assert.equal(png.toString('ascii', 37, 41), 'IDAT')
  const raw = inflateSync(png.subarray(41, 41 + length))
  const rows = []
  for (let row = 0; row < height; row++) rows.push([...raw.subarray(row * (width + 1) + 1, (row + 1) * (width + 1))])
  return rows
}

test('map message: gray levels, and the grid flipped so the image top is max y', () => {
  // Grid row 0 is the bottom of the map (lowest y).
  const grid = {
    header,
    info: { width: 3, height: 2, resolution: 0.05, origin: { position: { x: -1.25, y: 2.5, z: 0 } } },
    data: [
      -1, 0, 24, // bottom row
      25, 64, 100, // top row
    ],
  }
  const msg = mapMessage(grid)
  assert.deepEqual(
    { ...msg, data: undefined },
    { type: 'map', resolution: 0.05, width: 3, height: 2, origin: [-1.25, 2.5], encoding: 'png_base64', data: undefined },
  )
  assert.deepEqual(pngPixels(msg.data, 3, 2), [
    [205, 205, 0],
    [205, 254, 254],
  ])
})

test('motor status message', () => {
  const wheels = ['front_left', 'rear_left', 'front_right', 'rear_right']
  assert.deepEqual(motorStatusMessage({ data: Float64Array.from([0.5, -0.12345, 0, 1]) }, wheels), {
    type: 'motor_status',
    wheels,
    duties: [0.5, -0.123, 0, 1],
  })
})

function imuReading(gyroZ, accelZ, sec) {
  return {
    header: { stamp: { sec, nanosec: 0 }, frame_id: 'imu_link' },
    angular_velocity: { x: 0.01, y: 0, z: gyroZ },
    linear_acceleration: { x: 0, y: 0.2, z: accelZ },
  }
}

test('IMU readings are averaged into one message, then it starts again', () => {
  const average = new ImuAverage()
  assert.equal(average.take(), null)
  average.add(imuReading(0.5, 9.0, 10))
  average.add(imuReading(1.5, 9.6, 11))
  assert.deepEqual(average.take(), { type: 'imu', t: 11, gyro: [0.01, 0, 1], accel: [0, 0.2, 9.3], samples: 2 })
  assert.equal(average.take(), null)
})

test('motors mode from the motor driver dry_run parameter', () => {
  assert.equal(motorsMode({ values: [{ type: 1, bool_value: true }] }), 'dry_run')
  assert.equal(motorsMode({ values: [{ type: 1, bool_value: false }] }), 'live')
  // a driver without the parameter answers with PARAMETER_NOT_SET (0)
  assert.equal(motorsMode({ values: [{ type: 0, bool_value: false }] }), null)
  assert.equal(motorsMode({ values: [] }), null)
})
