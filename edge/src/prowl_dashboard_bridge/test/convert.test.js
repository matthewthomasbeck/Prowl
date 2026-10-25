import { test } from 'node:test'
import assert from 'node:assert/strict'
import { ImuAverage, motorStatusMessage, motorsMode, scanMessage } from '../bridge/convert.js'

const header = { stamp: { sec: 100, nanosec: 500000000 }, frame_id: 'laser_link' }
const close = (actual, expected, tolerance = 1e-3) =>
  assert.ok(Math.abs(actual - expected) <= tolerance, `${actual} is not close to ${expected}`)

function laserScan(ranges, { angleMin = 0, increment = Math.PI / 2 } = {}) {
  return { header, angle_min: angleMin, angle_increment: increment, range_min: 0.1, range_max: 10, ranges }
}
const noOffset = { x: 0, y: 0, yaw: 0 }

test('scan points around the robot: x forward, y left', () => {
  // Bins at 0, 90, 180 and 270 degrees.
  const { type, t, points } = scanMessage(laserScan([1, 2, 3, 4]), { laser: noOffset, maxPoints: 180 })
  assert.equal(type, 'scan')
  assert.equal(t, 100.5)
  assert.deepEqual(points, [[1, 0], [0, 2], [-3, 0], [-0, -4]])
})

test('scan points are moved by the LiDAR mounting', () => {
  // The LiDAR 0.1 m ahead of base_link and 0.2 m to the left, mounted backwards.
  const laser = { x: 0.1, y: 0.2, yaw: Math.PI }
  const { points } = scanMessage(laserScan([1]), { laser, maxPoints: 180 })
  // The LiDAR's 0 degrees points behind the robot: 0.9 m behind base_link.
  close(points[0][0], -0.9)
  close(points[0][1], 0.2)
})

test('scan with a negative angle_increment, like the LD19 (bins go clockwise)', () => {
  const { points } = scanMessage(laserScan([1, 2], { increment: -Math.PI / 2 }), { laser: noOffset, maxPoints: 180 })
  // Bin 1 is 90 degrees clockwise from the front: the robot's right, -y.
  close(points[1][0], 0)
  close(points[1][1], -2)
})

test('scan drops invalid readings', () => {
  const ranges = [NaN, Infinity, 0.05, 11, 0, 5]
  const { points } = scanMessage(laserScan(ranges, { increment: 0.1 }), { laser: noOffset, maxPoints: 180 })
  assert.equal(points.length, 1)
  close(Math.hypot(...points[0]), 5)
})

test('scan keeps the nearest reading of each group', () => {
  // 8 bins down to 2 points: groups [5, 2, 9, NaN] and [7, 7, 0.5, 3].
  const ranges = [5, 2, 9, NaN, 7, 7, 0.5, 3]
  const { points } = scanMessage(laserScan(ranges, { increment: 0.01 }), { laser: noOffset, maxPoints: 2 })
  assert.equal(points.length, 2)
  close(Math.hypot(...points[0]), 2)
  close(Math.hypot(...points[1]), 0.5)
})

test('scan never sends more than maxPoints', () => {
  const ranges = Array.from({ length: 456 }, () => 3)
  const { points } = scanMessage(laserScan(ranges, { increment: 0.0138 }), { laser: noOffset, maxPoints: 180 })
  assert.ok(points.length <= 180, `${points.length} points`)
  assert.ok(points.length >= 150, `${points.length} points`)
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
  assert.deepEqual(average.take(), { type: 'imu', t: 11, gyro: [0.01, 0, 1], accel: [0, 0.2, 9.3] })
  assert.equal(average.take(), null)
})

test('motors mode from the motor driver dry_run parameter', () => {
  assert.equal(motorsMode({ values: [{ type: 1, bool_value: true }] }), 'dry_run')
  assert.equal(motorsMode({ values: [{ type: 1, bool_value: false }] }), 'live')
  // a driver without the parameter answers with PARAMETER_NOT_SET (0)
  assert.equal(motorsMode({ values: [{ type: 0, bool_value: false }] }), null)
  assert.equal(motorsMode({ values: [] }), null)
})
