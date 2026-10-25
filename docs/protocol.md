# Dashboard bridge protocol

The bridge (`prowl_dashboard_bridge`) runs on the robot and serves the dashboard (`frontend/`) straight
from ROS2: a WebSocket for the live data and driving, and plain HTTP for the camera and the saved photos
(see [Camera](#camera)).

Every message is one JSON object with a `"type"`. Units are metres, radians and seconds; `t` is when a
reading was taken, in seconds (Unix time). Directions follow REP 103: x forward, y left, angles
counter-clockwise.

## Connecting

`ws://<robot>:8000/ws/dashboard[?token=<drive_token>]`

The dashboard's web server forwards `/ws` and `/api` to the bridge (`frontend/vite.config.js`), so the
browser only ever uses the dashboard's own address. Anyone who can reach the port can watch. Driving needs
the bridge's `drive_token` parameter in `token`; if `drive_token` is empty (the default), anyone can drive.
`GET /health` returns `{"ok": true, "dashboards": 1, "driving": false}`; `./prowl` waits for it before it
prints the dashboard's address. Anything else on the port is a `404`.

## Robot → dashboard

**snapshot**: always the first message. Treat it as "replace all state".
```json
{"type": "snapshot",
 "session": {"name": "Prowl live", "started_at": 1792879880.0, "robot_connected": true},
 "latest": {"status": {}, "scan": {}, "motor_status": {}, "imu": {}, "camera_still": {}},
 "can_command": true}
```
`latest` holds the newest message of each type below, or `null` before the first one. `session.name` is
the `mission_name` parameter and `session.started_at` is when the bridge started (the dashboard's clock
counts from it). `can_command` says whether this connection may drive. `session.robot_connected` is true
while the LiDAR, motor driver, IMU or camera sent something in the last `stale_after` (2) seconds; the
bridge itself being up doesn't count.

Then, at the rates set in `edge/src/prowl_dashboard_bridge/config/dashboard_bridge.yaml` (only when something
new came in):

| type | from | default rate | shape |
|---|---|---|---|
| `scan` | `/scan` | 5 Hz | `{"t", "points": [[x, y], ...]}` around the robot, at most `max_scan_points` (180) |
| `motor_status` | `/motor_status` | 10 Hz | `{"wheels": ["front_left", "rear_left", "front_right", "rear_right"], "duties": [0.5, 0.5, 0.5, 0.5]}` |
| `imu` | `/imu/data_raw` | 5 Hz | `{"t", "gyro": [x, y, z], "accel": [x, y, z]}` |
| `status` | the bridge | 1 Hz | `{"motors": "live", "topic_age_s": {"scan": 0.1, "motor_status": 0, "imu": 0.1, "camera": 0}}` |
| `camera_still` | `/camera/still/compressed` | each time a photo is taken | `{"t", "url": "/api/camera/still.jpg?t=1792898607.41", "photo": {saved photo}}` |
| `photo_deleted` | the bridge | when a dashboard deletes a photo | `{"name": "prowl-20261024-221120-413"}` |
| `robot_connected`, `robot_disconnected` | the bridge | when `session.robot_connected` changes | `{}` |

- **scan**: points in the robot's own frame (`base_link`): x forward, y left. The robot doesn't track its
  position, so a scan is always what it sees around it now. Each point is the nearest valid reading in its
  group of LiDAR bins, so thin obstacles don't disappear, and it's placed with the LiDAR's mounting
  (`laser_x`, `laser_y`, `laser_yaw`).
- **motor_status** duties are what the motor driver actually applies, from -1 to 1; positive pushes the
  robot forward.
- **imu**: the average of every reading since the last message (about 20 at 100 Hz), so a short bump still
  shows. `gyro` is turn rate in rad/s and `accel` is acceleration in m/s² with gravity in, each [x, y, z] in
  `imu_link` (x forward, y left, z up). The accelerometer isn't calibrated yet and reads gravity about 7 %
  low, so use it for tilt angles, not magnitudes. Nothing arrives for the first 2 s while the gyro calibrates.
- **status**: `topic_age_s` is how many seconds ago each topic last arrived (`null` = never), so the
  dashboard can tell a stalled sensor from a quiet one. `camera` is the video. `motors` says whether driving
  can move the robot: `"live"`, `"dry_run"` (the motor driver works out duties, so `motor_status` changes,
  but leaves the pins alone), or `null` when the motor driver isn't answering. The bridge asks the motor
  driver for its `dry_run` parameter every 2 s. `robot.launch.py` starts it in dry run unless `live:=true`
  (`./prowl --live`).
- **camera_still**: a new full-resolution photo, whoever asked for it. Show it from `url`; the `t` in it
  makes every photo a new address, so a browser never shows a cached one. The snapshot has the newest one.
  `photo` is its saved copy, as listed by `GET /api/camera/photos`, or `null` if it wasn't saved (it was
  taken before this bridge started, or has been deleted).
- A dashboard that falls more than 1 MB behind skips updates until it catches up.
- `pong` answers `ping`. `error` (`{"error": "...", "for_type": "drive"}`) answers a message that was
  rejected; the connection stays open.

## Dashboard → robot

**drive**: fractions of the robot's top speeds (`linear_speed` 1.0 m/s, `angular_speed` 3.34 rad/s), each
from -1 to 1. 1 is full motor duty. Missing fields count as 0.
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

## Camera

Plain HTTP on the same port. Anyone who can reach the port can watch and take photos; no token.

| Request | Answer |
|---|---|
| `GET /api/camera/frame.jpg` | The newest video frame (1280×720) as one JPEG. `503` with `{"ok": false, "error"}` before the first frame |
| `GET /api/camera/still.jpg` | The newest full-resolution photo (4608×2592 JPEG, about 1 MB). `404` before the first one |
| `POST /api/camera/capture` | Takes a full-resolution photo, about 1 s. `200` with `{"ok": true, "message": "4608x2592, 958 KB on /camera/still/compressed"}`. `409` while another photo is being taken, `503` with `{"ok": false, "error"}` if the camera driver isn't running or failed |

The dashboard's Camera page shows the video by asking for `frame.jpg` again as soon as the last one has
arrived, at most 15 times a second, so it's never more than a frame behind, however slow the link. The
video pauses for about a second while a photo is taken. Every dashboard also gets the new photo as a
`camera_still` message. Every answer has `Cache-Control: no-store`.

### Saved photos

Every photo is also saved on the robot, in `photo_dir` (`edge/data/photos/`, which git ignores), and kept
until it's deleted. Past `max_photos` (200, about 1 MB each), the oldest is deleted.

| Request | Answer |
|---|---|
| `GET /api/camera/photos` | `{"ok": true, "photos": [{"name": "prowl-20261024-221120-413", "t", "width", "height", "bytes", "url": "/api/camera/photos/prowl-20261024-221120-413.jpg"}]}`, newest first |
| `GET /api/camera/photos/<name>.jpg` | The photo. It never changes under its name, so it may be cached. `404` if there's no such photo |
| `DELETE /api/camera/photos/<name>.jpg` | Deletes it and sends every dashboard `photo_deleted`. Needs the drive token (`?token=` or `Authorization: Bearer`) when one is set: `403` without it, `404` if there's no such photo |

Photos can show people near the robot, so the dashboard's Photos page has a Delete button.

## ROS side

| Topic | Type | Direction |
|---|---|---|
| `/scan` | `sensor_msgs/LaserScan` (subscribed best effort) | in |
| `/motor_status` | `std_msgs/Float64MultiArray` | in |
| `/imu/data_raw` | `sensor_msgs/Imu` (subscribed best effort) | in |
| `/motor_driver/get_parameters` | `rcl_interfaces/GetParameters` | service called every 2 s for `dry_run` (`status.motors`) |
| `/cmd_vel` | `geometry_msgs/Twist` | out, only while a dashboard drives, plus the stops above |
| `/camera/image_raw/compressed` | `sensor_msgs/CompressedImage` (JPEG, subscribed best effort) | in |
| `/camera/still/compressed` | `sensor_msgs/CompressedImage` (JPEG, transient local) | in |
| `/camera/capture` | `std_srvs/Trigger` | service called for `POST /api/camera/capture` |
