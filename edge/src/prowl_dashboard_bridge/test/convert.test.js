import { test } from 'node:test'
import assert from 'node:assert/strict'
import { ImuAverage, gpsMessage, motorStatusMessage, motorsMode, scanMessage } from '../bridge/convert.js'

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

function navSatFix(status, latitude, longitude) {
  return { header: { stamp: { sec: 200, nanosec: 250000000 }, frame_id: 'gps_link' }, status: { status, service: 1 }, latitude, longitude, altitude: 547.6 }
}
function gpsStatus(values) {
  return { level: 0, name: 'gps_driver: receiver', message: '', hardware_id: '', values: Object.entries(values).map(([key, value]) => ({ key, value })) }
}
const threeDStatus = gpsStatus({
  fix: '3d',
  quality: '1',
  utc: '1039426045.00',
  altitude_msl: '499.6',
  satellites_used: '2',
  satellites_in_view: '2',
  hdop: '1.20',
  vdop: '2.00',
  pdop: '2.30',
  satellites: '[[7,29,116,41,true],[10,7,189,null,false]]',
})

test('gps with a fix: position, altitude above sea level, speed and course, satellites', () => {
  const velocity = { twist: { linear: { x: 1, y: -1, z: 0 } } } // east and south: course 135 degrees
  const m = gpsMessage(navSatFix(0, 47.2852331234, 8.5652654321), { velocity, status: threeDStatus })
  assert.equal(m.type, 'gps')
  assert.equal(m.t, 200.25)
  assert.equal(m.fix, '3d')
  assert.equal(m.lat, 47.2852331)
  assert.equal(m.lon, 8.5652654)
  assert.equal(m.alt, 499.6)
  close(m.speed, Math.SQRT2, 0.01)
  assert.equal(m.course, 135)
  assert.equal(m.satellites_used, 2)
  assert.deepEqual([m.hdop, m.vdop, m.pdop], [1.2, 2, 2.3])
  assert.equal(m.utc, 1039426045)
  assert.deepEqual(m.satellites, [
    { id: 7, elevation: 29, azimuth: 116, cno: 41, used: true },
    { id: 10, elevation: 7, azimuth: 189, cno: null, used: false },
  ])
})

test('gps course is 0-360, clockwise from north', () => {
  const west = gpsMessage(navSatFix(0, 1, 2), { velocity: { twist: { linear: { x: -2, y: 0, z: 0 } } }, status: threeDStatus })
  assert.equal(west.course, 270)
  assert.equal(west.speed, 2)
})

test('gps without a fix: no position, speed or course; the receiver status still shows', () => {
  const status = gpsStatus({ fix: 'none', quality: '0', utc: '', altitude_msl: '', satellites_used: '0', satellites_in_view: '0', hdop: '99.99', vdop: '99.99', pdop: '99.99', satellites: '[]' })
  const velocity = { twist: { linear: { x: 1, y: 1, z: 0 } } }
  const m = gpsMessage(navSatFix(-1, NaN, NaN), { velocity, status })
  assert.equal(m.fix, 'none')
  assert.deepEqual([m.lat, m.lon, m.alt, m.speed, m.course, m.utc], [null, null, null, null, null, null])
  assert.equal(m.hdop, 99.99)
  assert.deepEqual(m.satellites, [])
  // The result has to survive JSON: no NaN anywhere.
  assert.deepEqual(JSON.parse(JSON.stringify(m)), m)
})

test('gps without a status yet, or with an unreadable satellite list', () => {
  const m = gpsMessage(navSatFix(0, 10, 20))
  assert.equal(m.fix, '3d')
  assert.deepEqual([m.lat, m.lon], [10, 20])
  assert.deepEqual([m.alt, m.hdop, m.satellites_used, m.speed], [null, null, 0, null])
  assert.deepEqual(m.satellites, [])
  assert.deepEqual(gpsMessage(navSatFix(0, 10, 20), { status: gpsStatus({ fix: '2d', satellites: '[[' }) }).satellites, [])
  assert.equal(gpsMessage(navSatFix(0, 10, 20), { status: gpsStatus({ fix: '2d' }) }).fix, '2d')
})
