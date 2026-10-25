// Prowl dashboard bridge: a ROS2 node (rclnodejs) that serves the dashboard over a WebSocket.
//
// ROS side: subscribes to /scan, /motor_status, /imu/data_raw and the camera's video and stills, publishes
// /cmd_vel, calls /camera/capture, and asks the motor driver whether it's in dry run. Every still is also
// saved as a photo.
// Dashboard side: ws://<pi>:<port>/ws/dashboard, speaking the messages in docs/protocol.md, and the camera
// and the saved photos over plain HTTP under /api/camera/.
//
// Incoming topics arrive at their own rates; only the newest of each is kept, and it's sent to the
// dashboards at the (lower) rates set in the config, so the browser never gets 50 messages a second.

import rclnodejs from 'rclnodejs'
import { CameraFeed } from './camera.js'
import { DriveGate } from './drive.js'
import { ImuAverage, motorStatusMessage, motorsMode, scanMessage, stampSeconds } from './convert.js'
import { PhotoStore } from './photos.js'
import { startServer } from './server.js'

const { Node, Parameter, ParameterType, QoS } = rclnodejs
const { HistoryPolicy, ReliabilityPolicy, DurabilityPolicy } = QoS

// Wheel order of /motor_status, as published by prowl_motor_driver.
const WHEELS = ['front_left', 'rear_left', 'front_right', 'rear_right']
const DRIVE_CHECK_MS = 50
const CAPTURE_TIMEOUT_MS = 20000 // a full-resolution still normally takes about 1 s
const MOTORS_CHECK_MS = 2000 // how often the motor driver is asked whether it's in dry run
// The robot counts as connected while one of these sent something within stale_after seconds.
const ROBOT_TOPICS = ['scan', 'motor_status', 'imu', 'camera']

const PARAMS = {
  host: ['0.0.0.0', ParameterType.PARAMETER_STRING],
  port: [8000n, ParameterType.PARAMETER_INTEGER],
  drive_token: ['', ParameterType.PARAMETER_STRING],
  linear_speed: [1.0, ParameterType.PARAMETER_DOUBLE],
  angular_speed: [3.34, ParameterType.PARAMETER_DOUBLE],
  drive_timeout: [0.3, ParameterType.PARAMETER_DOUBLE],
  mission_name: ['Prowl live', ParameterType.PARAMETER_STRING],
  laser_x: [0.0, ParameterType.PARAMETER_DOUBLE],
  laser_y: [0.0, ParameterType.PARAMETER_DOUBLE],
  laser_yaw: [0.0, ParameterType.PARAMETER_DOUBLE],
  max_scan_points: [180n, ParameterType.PARAMETER_INTEGER],
  scan_rate: [5.0, ParameterType.PARAMETER_DOUBLE],
  motor_status_rate: [10.0, ParameterType.PARAMETER_DOUBLE],
  imu_rate: [5.0, ParameterType.PARAMETER_DOUBLE],
  stale_after: [2.0, ParameterType.PARAMETER_DOUBLE],
  photo_dir: ['data/photos', ParameterType.PARAMETER_STRING],
  max_photos: [200n, ParameterType.PARAMETER_INTEGER],
}

function readParams(node) {
  const values = {}
  for (const [name, [initial, type]] of Object.entries(PARAMS)) {
    const value = node.declareParameter(new Parameter(name, type, initial)).value
    values[name] = type === ParameterType.PARAMETER_INTEGER ? Number(value) : value
  }
  return values
}

function checkParams(p) {
  const positive = ['linear_speed', 'angular_speed', 'drive_timeout', 'max_scan_points', 'scan_rate', 'motor_status_rate', 'imu_rate', 'stale_after', 'max_photos']
  for (const name of positive) {
    if (!(p[name] > 0)) throw new Error(`parameter ${name} must be > 0, got ${p[name]}`)
  }
}

const round1 = (value) => Math.round(value * 10) / 10

async function main() {
  await rclnodejs.init()
  rclnodejs.removeSignalHandlers() // ours below publishes a stop before shutting ROS down
  const node = new Node('dashboard_bridge')
  const log = node.getLogger()
  const p = readParams(node)
  checkParams(p)

  const startedAt = Date.now() / 1000
  const laser = { x: p.laser_x, y: p.laser_y, yaw: p.laser_yaw }

  // Newest data per topic. `fresh` means it arrived since it was last sent to the dashboards.
  const latest = {
    scan: { msg: null, fresh: false, at: null },
    motor_status: { msg: null, fresh: false, at: null },
    imu: { msg: null, fresh: false, at: null }, // msg: the newest average sent, readings pile up in `imu`
    camera: { msg: null, fresh: false, at: null }, // only `at`: the frames themselves live in `camera`
  }
  const imu = new ImuAverage()

  function store(topic, msg) {
    Object.assign(latest[topic], { msg, fresh: true, at: Date.now() / 1000 })
  }

  // Best effort: never holds up the LiDAR driver, and works with its reliable publisher.
  node.createSubscription('sensor_msgs/msg/LaserScan', '/scan', { qos: QoS.profileSensorData }, (msg) => store('scan', msg))
  node.createSubscription('std_msgs/msg/Float64MultiArray', '/motor_status', (msg) => store('motor_status', msg))
  // The IMU driver publishes nothing until its gyro calibration is done (about 2 s after it starts).
  node.createSubscription('sensor_msgs/msg/Imu', '/imu/data_raw', { qos: QoS.profileSensorData }, (msg) => {
    imu.add(msg)
    latest.imu.at = Date.now() / 1000
  })

  const captureClient = node.createClient('std_srvs/srv/Trigger', '/camera/capture')
  const camera = new CameraFeed({
    async requestCapture() {
      if (!captureClient.isServiceServerAvailable()) return { ok: false, error: "the camera driver isn't running" }
      try {
        const response = await captureClient.sendRequestAsync({}, { timeout: CAPTURE_TIMEOUT_MS })
        return response.success ? { ok: true, message: response.message } : { ok: false, error: response.message }
      } catch (err) {
        return { ok: false, error: `no answer from the camera driver: ${err.message}` }
      }
    },
  })
  // Best effort, like the driver publishes it; a slow bridge skips frames rather than holding it up.
  node.createSubscription('sensor_msgs/msg/CompressedImage', '/camera/image_raw/compressed', { qos: QoS.profileSensorData }, (msg) => {
    camera.pushFrame(Buffer.from(msg.data), stampSeconds(msg.header))
    store('camera', null)
  })
  // Transient local, to match the driver, so the newest still arrives even if it was taken before the bridge started.
  const stillQos = new QoS(HistoryPolicy.RMW_QOS_POLICY_HISTORY_KEEP_LAST, 1, ReliabilityPolicy.RMW_QOS_POLICY_RELIABILITY_RELIABLE, DurabilityPolicy.RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)
  // Relative to where the bridge was started: /ws, i.e. edge/, in the dev container.
  const photos = new PhotoStore({ dir: p.photo_dir, maxPhotos: p.max_photos, log })
  log.info(`saving photos in ${p.photo_dir} (${photos.count} there now, at most ${p.max_photos})`)
  let stillPhoto = null // the newest still's saved photo, or null if it wasn't saved
  node.createSubscription('sensor_msgs/msg/CompressedImage', '/camera/still/compressed', { qos: stillQos }, (msg) => {
    const jpeg = Buffer.from(msg.data)
    const t = stampSeconds(msg.header)
    // The driver re-sends its newest still to the bridge when it starts. That one was saved (or deleted)
    // by the bridge's last run, so only stills taken since this run started are saved.
    stillPhoto = t >= startedAt ? photos.add(jpeg, t) : photos.find(t)
    server.broadcast({ ...camera.setStill(jpeg, t), photo: stillPhoto })
  })

  const cmdVel = node.createPublisher('geometry_msgs/msg/Twist', '/cmd_vel')
  const gate = new DriveGate({
    linearSpeed: p.linear_speed,
    angularSpeed: p.angular_speed,
    timeout: p.drive_timeout,
    publish: (twist) =>
      cmdVel.publish({
        linear: { x: twist.linear_x, y: twist.linear_y, z: 0 },
        angular: { x: 0, y: 0, z: twist.angular_z },
      }),
  })

  // Whether drive requests can move the robot: 'live', 'dry_run' (the motor driver computes duties but
  // leaves the pins alone), or null while the motor driver isn't answering. robot.launch.py starts it in
  // dry run unless live:=true, and the motor panel restarts it in either, so it's asked again and again.
  const motorParams = node.createClient('rcl_interfaces/srv/GetParameters', '/motor_driver/get_parameters')
  let motors = null
  let checkingMotors = false
  async function checkMotors() {
    if (checkingMotors) return
    checkingMotors = true
    let mode = null
    try {
      if (motorParams.isServiceServerAvailable()) {
        mode = motorsMode(await motorParams.sendRequestAsync({ names: ['dry_run'] }, { timeout: MOTORS_CHECK_MS }))
      }
    } catch {
      // no answer in time: the driver is gone or stuck
    } finally {
      checkingMotors = false
    }
    if (mode === motors) return
    motors = mode
    if (mode === 'dry_run') log.warn("the motor driver is in dry run: driving won't move the robot (./prowl --live drives for real)")
    else if (mode === 'live') log.info('the motor driver is live: driving moves the robot')
    else log.warn("the motor driver isn't answering: driving won't move the robot")
  }

  function statusMessage() {
    const now = Date.now() / 1000
    const ages = {}
    for (const [topic, entry] of Object.entries(latest)) ages[topic] = entry.at === null ? null : round1(now - entry.at)
    return { type: 'status', motors, topic_age_s: ages }
  }

  // Connected = at least one of the robot's drivers is sending. The bridge itself is always up when it
  // answers, so it's the drivers that say whether there's a robot to look at.
  function robotConnected() {
    const now = Date.now() / 1000
    return ROBOT_TOPICS.some((topic) => latest[topic].at !== null && now - latest[topic].at <= p.stale_after)
  }
  let wasConnected = false
  function checkConnected() {
    const connected = robotConnected()
    if (connected !== wasConnected) server.broadcast({ type: connected ? 'robot_connected' : 'robot_disconnected' })
    wasConnected = connected
  }

  function scanNow() {
    return scanMessage(latest.scan.msg, { laser, maxPoints: p.max_scan_points })
  }

  function snapshot(canDrive) {
    return {
      type: 'snapshot',
      session: { name: p.mission_name, started_at: startedAt, robot_connected: robotConnected() },
      latest: {
        status: statusMessage(),
        scan: latest.scan.msg ? scanNow() : null,
        motor_status: latest.motor_status.msg ? motorStatusMessage(latest.motor_status.msg, WHEELS) : null,
        imu: latest.imu.msg,
        camera_still: camera.still ? { ...camera.stillMessage(), photo: stillPhoto && photos.find(stillPhoto.t) } : null,
      },
      can_command: canDrive,
    }
  }

  const server = startServer({
    host: p.host,
    port: p.port,
    driveToken: p.drive_token,
    gate,
    snapshot,
    camera,
    photos,
    log,
  })
  server.httpServer.on('error', (err) => {
    log.error(`web server failed: ${err.message}`)
    shutdown(1)
  })
  if (!p.drive_token) log.warn('drive_token is empty: anyone who can reach this port can drive the robot')

  // Sends the newest message of a topic every `period` seconds, if a new one came in.
  function every(period, topic, convert) {
    return setInterval(() => {
      const entry = latest[topic]
      if (!entry.fresh) return
      entry.fresh = false
      try {
        server.broadcast(convert(entry.msg))
      } catch (err) {
        log.warn(`couldn't convert ${topic}: ${err.message}`)
      }
    }, period * 1000)
  }

  const timers = [
    every(1 / p.scan_rate, 'scan', () => scanNow()),
    every(1 / p.motor_status_rate, 'motor_status', (msg) => motorStatusMessage(msg, WHEELS)),
    setInterval(() => {
      const message = imu.take()
      if (!message) return
      latest.imu.msg = message
      server.broadcast(message)
    }, 1000 / p.imu_rate),
    setInterval(() => {
      checkConnected()
      server.broadcast(statusMessage())
    }, 1000),
    setInterval(() => gate.tick(), DRIVE_CHECK_MS),
    setInterval(checkMotors, MOTORS_CHECK_MS),
  ]
  checkMotors()

  let stopping = false
  function shutdown(code) {
    if (stopping) return
    stopping = true
    gate.shutdown() // a stop goes out before ROS shuts down
    timers.forEach(clearInterval)
    server.close()
    node.destroy()
    rclnodejs.shutdown()
    process.exit(code)
  }
  process.on('SIGINT', () => shutdown(0))
  process.on('SIGTERM', () => shutdown(0))

  log.info(
    `dashboard bridge up: top speeds ${p.linear_speed} m/s and ${p.angular_speed} rad/s, ` +
      `stops ${p.drive_timeout} s after the last drive request`,
  )
  node.spin()
}

main().catch((err) => {
  console.error(`[dashboard_bridge] ${err.stack || err}`)
  process.exit(1)
})
