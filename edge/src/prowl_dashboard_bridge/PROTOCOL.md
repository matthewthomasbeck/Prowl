# Dashboard bridge protocol

The bridge (`prowl_dashboard_bridge`) runs on the robot and serves the dashboard straight from ROS2, with
no backend in between. It speaks a subset of the backend's dashboard protocol (`backend/PROTOCOL.md` in the
Prowl repo), so the dashboard's live code (`frontend/src/live/useProwlLink.js`) works with either one. It
adds `motor_status`, plus `drive` and `stop` for driving.
[FRONTEND_HOOKUP.md](FRONTEND_HOOKUP.md) says what the dashboard needs to change to use them.

Every message is one JSON object with a `"type"`. Units are metres and radians; positions are in the SLAM
`map` frame; `theta` is 0 along +x, counter-clockwise positive (REP 103).

## Connecting

`ws://<robot>:8000/ws/dashboard[?token=<drive_token>]`

Anyone who can reach the port can watch. Driving needs the bridge's `drive_token` parameter in `token`;
if `drive_token` is empty (the default), anyone can drive. `GET /health` returns
`{"ok": true, "dashboards": 1, "driving": false}`. `GET /` serves a test page that uses this protocol (see the README).

## Robot → dashboard

**snapshot**: always the first message. Treat it as "replace all state", as with the backend.
```json
{"type": "snapshot",
 "session": {"id": "robot-20261024T221120", "name": "Prowl live", "started_at": 1792879880.0, "live": true, "robot_connected": true},
 "latest": {"mission": {}, "pose": {}, "map": {}, "scan": {}, "status": {}, "motor_status": {}},
 "trail": [[1.2, 1.2], [1.3, 1.2]], "reviews": {}, "reports": [], "following": true, "can_command": true}
```
`latest` holds the newest message of each type below, or `null` before the first one. `session.id` is new
each time the bridge starts. `can_command` says whether this connection may drive.

Then, at the rates set in `config/dashboard_bridge.yaml` (only when something new came in):

| type | from | default rate | shape |
|---|---|---|---|
| `mission` | parameters | in the snapshot only | `{"name": "Prowl live", "simulated": false, "zone": {"x", "y", "width", "height"}}`, `zone` only if set |
| `pose` | `/robot_pose` | 10 Hz | `{"t", "x", "y", "theta"}`, as in the backend protocol |
| `scan` | `/scan` | 5 Hz | `{"t", "points": [[x, y], ...]}` in the map frame, at most `max_scan_points` (180) |
| `map` | `/map` | every 1 s | grayscale PNG, exactly as in the backend protocol (0 occupied, 205 unknown, 254 free, row 0 = max y) |
| `motor_status` | `/motor_status` | 10 Hz | `{"wheels": ["front_left", "rear_left", "front_right", "rear_right"], "duties": [0.5, 0.5, 0.5, 0.5]}` |
| `status` | the bridge | 1 Hz | `{"mode": "manual", "driving": false, "dashboards": 1, "topic_age_s": {"pose": 0.1, "scan": 0.1, "map": 0.5, "motor_status": 0}}` |

- **scan**: each point is the nearest valid reading in its group of LiDAR bins, so thin obstacles don't
  disappear. It's placed with the newest pose and the LiDAR's mounting (`laser_x`, `laser_y`, `laser_yaw`).
  Before any pose arrives, the robot is taken to be at the map origin, facing +x.
- **motor_status** duties are what the motor driver actually applies, from -1 to 1; positive pushes the
  robot forward.
- **status**: `topic_age_s` is how many seconds ago each topic last arrived (`null` = never), so the
  dashboard can tell a stalled sensor from a quiet one.
- A dashboard that falls more than 1 MB behind skips updates until it catches up.
- `pong` answers `ping`. `error` (`{"error": "...", "for_type": "drive"}`) answers a message that was
  rejected; the connection stays open.

Not sent (the backend's, not the robot's): `detections`, `analysis`, `review`, `report`, `robot_connected`,
`robot_disconnected`, `session_ended`, and replay. The bridge answers `review` and `report` with an `error`.

## Dashboard → robot

**drive**: fractions of the robot's top speeds (`linear_speed` 0.3 m/s, `angular_speed` 1.0 rad/s), each
from -1 to 1. Missing fields count as 0.
```json
{"type": "drive", "x": 1, "y": 0, "turn": -0.5}
```
`x` is forward, `y` is left (strafe), `turn` is counter-clockwise. Values outside [-1, 1] are clamped;
`x` and `y` together are limited to the straight-line top speed. The bridge publishes it on `/cmd_vel`.

**Drive messages have to keep coming:** send the current command about 10 times a second while a control is
held. 0.3 s (`drive_timeout`) after the last one, the bridge publishes one stop. Send all zeros on
release so the robot stops at once. `frontend/src/live/useDrive.js` does all of this.

- One dashboard drives at a time. Its control lasts while its drive messages keep coming; anyone else's
  drive gets `error: "another dashboard is driving"`.
- **stop**: `{"type": "stop"}` stops the robot whoever is driving, and needs no token.
- The robot stops when the driving dashboard disconnects and when the bridge shuts down. As a last
  resort, the motor driver stops by itself 0.5 s after `/cmd_vel` goes quiet.

**ping**: `{"type": "ping"}` gets `{"type": "pong"}` back.

## ROS side

| Topic | Type | Direction |
|---|---|---|
| `/robot_pose` | `geometry_msgs/PoseStamped` (map frame) | in |
| `/scan` | `sensor_msgs/LaserScan` (subscribed best effort) | in |
| `/map` | `nav_msgs/OccupancyGrid` | in |
| `/motor_status` | `std_msgs/Float64MultiArray` | in |
| `/cmd_vel` | `geometry_msgs/Twist` | out, only while a dashboard drives, plus the stops above |

`prowl_brain` will publish `/cmd_vel` too. Nothing keeps the two apart yet, so don't drive from the
dashboard while the brain is driving.
