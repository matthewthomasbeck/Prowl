// Prowl dashboard bridge: a ROS2 node (rclnodejs) that serves the dashboard over a WebSocket.
//
// ROS side: subscribes to /robot_pose, /scan, /map and /motor_status, and publishes /cmd_vel.
// Dashboard side: ws://<pi>:<port>/ws/dashboard, speaking the message shapes in PROTOCOL.md, which
// match the backend's /ws/dashboard so the dashboard's live code works with either.
//
// Incoming topics arrive at their own rates; only the newest of each is kept, and it's sent to the
// dashboards at the (lower) rates set in the config, so the browser never gets 50 messages a second.

import rclnodejs from 'rclnodejs'
import { DriveGate } from './drive.js'
import { mapMessage, motorStatusMessage, poseMessage, scanMessage } from './convert.js'
import { startServer } from './server.js'

const { Node, Parameter, ParameterType, QoS } = rclnodejs

// Wheel order of /motor_status, as published by prowl_motor_driver.
const WHEELS = ['front_left', 'rear_left', 'front_right', 'rear_right']
const TRAIL_STEP_M = 0.05 // a new trail point once the robot has moved this far
const TRAIL_MAX_POINTS = 10000
const DRIVE_CHECK_MS = 50

const PARAMS = {
  host: ['0.0.0.0', ParameterType.PARAMETER_STRING],
  port: [8000n, ParameterType.PARAMETER_INTEGER],
  drive_token: ['', ParameterType.PARAMETER_STRING],
  linear_speed: [0.3, ParameterType.PARAMETER_DOUBLE],
  angular_speed: [1.0, ParameterType.PARAMETER_DOUBLE],
  drive_timeout: [0.3, ParameterType.PARAMETER_DOUBLE],
  mission_name: ['Prowl live', ParameterType.PARAMETER_STRING],
  zone: [[], ParameterType.PARAMETER_DOUBLE_ARRAY],
  laser_x: [0.0, ParameterType.PARAMETER_DOUBLE],
  laser_y: [0.0, ParameterType.PARAMETER_DOUBLE],
  laser_yaw: [0.0, ParameterType.PARAMETER_DOUBLE],
  max_scan_points: [180n, ParameterType.PARAMETER_INTEGER],
  pose_rate: [10.0, ParameterType.PARAMETER_DOUBLE],
  scan_rate: [5.0, ParameterType.PARAMETER_DOUBLE],
  motor_status_rate: [10.0, ParameterType.PARAMETER_DOUBLE],
  map_period: [1.0, ParameterType.PARAMETER_DOUBLE],
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
  const positive = ['linear_speed', 'angular_speed', 'drive_timeout', 'max_scan_points', 'pose_rate', 'scan_rate', 'motor_status_rate', 'map_period']
  for (const name of positive) {
    if (!(p[name] > 0)) throw new Error(`parameter ${name} must be > 0, got ${p[name]}`)
  }
  if (p.zone.length !== 0 && (p.zone.length !== 4 || !(p.zone[2] > 0 && p.zone[3] > 0))) {
    throw new Error('parameter zone must be empty or [x, y, width, height] with width and height > 0')
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
  const sessionId = `robot-${new Date().toISOString().replace(/[-:]/g, '').slice(0, 15)}`
  const mission = { type: 'mission', name: p.mission_name, simulated: false }
  if (p.zone.length === 4) mission.zone = { x: p.zone[0], y: p.zone[1], width: p.zone[2], height: p.zone[3] }
  const laser = { x: p.laser_x, y: p.laser_y, yaw: p.laser_yaw }

  // Newest data per topic. `fresh` means it arrived since it was last sent to the dashboards.
  const latest = {
    pose: { msg: null, fresh: false, at: null },
    scan: { msg: null, fresh: false, at: null, sent: null },
    map: { msg: null, fresh: false, at: null, sent: null },
    motor_status: { msg: null, fresh: false, at: null },
  }
  const trail = []

  function store(topic, msg) {
    Object.assign(latest[topic], { msg, fresh: true, at: Date.now() / 1000 })
  }

  node.createSubscription('geometry_msgs/msg/PoseStamped', '/robot_pose', (msg) => {
    const pose = poseMessage(msg)
    store('pose', pose)
    const last = trail[trail.length - 1]
    if (!last || Math.hypot(pose.x - last[0], pose.y - last[1]) >= TRAIL_STEP_M) {
      trail.push([pose.x, pose.y])
      if (trail.length > TRAIL_MAX_POINTS) trail.shift()
    }
  })
  // Best effort: never holds up the LiDAR driver, and works with its reliable publisher.
  node.createSubscription('sensor_msgs/msg/LaserScan', '/scan', { qos: QoS.profileSensorData }, (msg) => store('scan', msg))
  node.createSubscription('nav_msgs/msg/OccupancyGrid', '/map', (msg) => store('map', msg))
  node.createSubscription('std_msgs/msg/Float64MultiArray', '/motor_status', (msg) => store('motor_status', msg))

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

  function statusMessage() {
    const now = Date.now() / 1000
    const ages = {}
    for (const [topic, entry] of Object.entries(latest)) ages[topic] = entry.at === null ? null : round1(now - entry.at)
    return { type: 'status', mode: 'manual', driving: gate.driving, dashboards: server?.dashboards ?? 0, topic_age_s: ages }
  }

  function scanNow() {
    return scanMessage(latest.scan.msg, { pose: latest.pose.msg, laser, maxPoints: p.max_scan_points })
  }

  function snapshot(canDrive) {
    return {
      type: 'snapshot',
      session: { id: sessionId, name: p.mission_name, started_at: startedAt, live: true, robot_connected: true },
      latest: {
        mission,
        pose: latest.pose.msg,
        map: latest.map.sent,
        scan: latest.scan.msg ? scanNow() : null,
        status: statusMessage(),
        motor_status: latest.motor_status.msg ? motorStatusMessage(latest.motor_status.msg, WHEELS) : null,
      },
      trail,
      reviews: {},
      reports: [],
      following: true,
      can_command: canDrive,
    }
  }

  const server = startServer({ host: p.host, port: p.port, driveToken: p.drive_token, gate, snapshot, log })
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
        const message = convert(entry.msg)
        if ('sent' in entry) entry.sent = message
        server.broadcast(message)
      } catch (err) {
        log.warn(`couldn't convert ${topic}: ${err.message}`)
      }
    }, period * 1000)
  }

  const timers = [
    every(1 / p.pose_rate, 'pose', (pose) => pose),
    every(1 / p.scan_rate, 'scan', () => scanNow()),
    every(1 / p.motor_status_rate, 'motor_status', (msg) => motorStatusMessage(msg, WHEELS)),
    every(p.map_period, 'map', mapMessage),
    setInterval(() => server.broadcast(statusMessage()), 1000),
    setInterval(() => gate.tick(), DRIVE_CHECK_MS),
  ]

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
