# The ROS2 packages

Six packages in `edge/src/`, all written for Prowl. Each one's parameters are in its `config/` folder, and
each has a launch file for running it on its own:

```bash
./prowl dev ros2 launch prowl_ld19_driver ld19_driver.launch.py
./prowl dev ros2 topic hz /scan
```

`./prowl` runs all six together through `prowl_dashboard_bridge/launch/robot.launch.py`. Ctrl-C stops a
launch. The sensor drivers never touch the motors, so they're safe to run any time, but **the motor driver's
own launch file runs it live** ([safety.md](safety.md)). Each sensor driver reads its sensor on a background
thread, and if the sensor is missing or stops answering it keeps retrying every second, so it can start
before the sensor is plugged in.

To override one parameter on top of the config file, name the node: `-p dashboard_bridge:port:=8042`. A
plain `-p port:=8042` loses to the file's value.

## Motor driver (`prowl_motor_driver`)

Executable `motor_driver_node`, node name `motor_driver`. It drives the four motors through the pigpio
daemon.

| Topic | Type | Direction | Meaning |
|---|---|---|---|
| `/cmd_vel` | `geometry_msgs/Twist` | in | Whole-robot motion: `linear.x` forward (m/s), `linear.y` left, i.e. strafing (m/s), `angular.z` counter-clockwise turn (rad/s). Mixed into mecanum wheel duties |
| `/wheel_cmd` | `std_msgs/Float64MultiArray` | in | One duty per wheel in [-1, 1], in the order front_left, rear_left, front_right, rear_right. Positive pushes the robot forward |
| `/motor_status` | `std_msgs/Float64MultiArray` | out | The four duties actually applied (after ramping), same order, at `loop_rate` |

The most recent message on either input wins. The driver is open loop: with no encoders, a speed is just a
fraction of `max_linear_speed` mapped straight to duty cycle.

Parameters (`config/motor_driver.yaml`):

| Parameter | Value | Meaning |
|---|---|---|
| `dry_run` | `false` | `true` runs without touching GPIO. `./prowl` and the panel override it |
| `pwm_frequency` | 2000 | Hz; pigpio picks its nearest supported rate |
| `max_duty` | 0.98 | The H-bridge manual recommends staying at or below about 98 % |
| `loop_rate` | 50.0 | Hz |
| `cmd_timeout` | 0.5 | Seconds without a command before the motors stop |
| `max_accel` | 2.0 | Duty per second (0 to full in 0.5 s) |
| `wheel_separation` | 0.3 | m, left–right distance between wheel centers (TODO: measure) |
| `wheel_base` | 0.3 | m, front–rear distance between axles (TODO: measure) |
| `max_linear_speed` | 1.0 | m/s at full duty (TODO: measure) |
| `<wheel>.in1` / `.in2` / `.en` / `.inverted` | see [hardware.md](hardware.md#motors) | BCM pins (`en: -1` = not wired) and direction flip |

Running it without `./prowl` or the panel:

```bash
# Dry run, no GPIO
./prowl dev ros2 run prowl_motor_driver motor_driver_node --ros-args \
  --params-file install/prowl_motor_driver/share/prowl_motor_driver/config/motor_driver.yaml -p dry_run:=true

# A quick check from a second terminal: strafe left gives [-0.5, 0.5, 0.5, -0.5]
./prowl dev ros2 topic pub -r 20 /cmd_vel geometry_msgs/msg/Twist "{linear: {y: 0.5}}"
./prowl dev ros2 topic echo /motor_status

# Live: the launch file uses the config as is, which has dry_run: false
./prowl dev ros2 launch prowl_motor_driver motor_driver.launch.py
```

### Motor panel

`./prowl panel` opens a terminal control panel for testing the motors without the dashboard. It starts and
stops its own motor driver, drives the wheels through `/wheel_cmd`, and shows `/motor_status` live.

It opens with the driver stopped. `N` starts it in **dry run**: the driver runs and the bars move, but no
GPIO pin is touched. To drive the real motors, press `L` (the panel shows `next start: LIVE`), then `N`. LIVE
lasts for one run: once the driver stops, the next start is a dry run again. If another motor driver is
already running (say, `./prowl`'s), the panel says so instead of starting a second one, and the header shows
`RUNNING (started outside the panel)`.

| Key | Action |
|---|---|
| Hold `W` / `S`, or ↑ / ↓ | Forward / backward |
| Hold `A` / `D` | Strafe left / right |
| Hold `Z` / `C`, or ← / → | Turn left / right |
| `Space` | Stop |
| `1`–`4` | Run one wheel for 3 s (front_left, rear_left, front_right, rear_right) |
| `T` | Run all four wheels for 3 s |
| `+` / `-` | Speed (duty 0.05–1.00, starts at 0.50), for driving and the wheel tests |
| `N` | Start / stop the driver |
| `L` | Switch the next start between dry run and LIVE (only while the driver is stopped) |
| `Q` or Ctrl-C | Quit; this stops the driver and the motors coast |

The wheels only turn while a key is repeating. A single tap moves for 0.7 s; after you let go of a held key,
the stop goes out within about 0.3 s. If the connection drops, keys stop arriving and the robot stops the same
way, and the driver stops by itself 0.5 s after the last command.

## LD19 LiDAR (`prowl_ld19_driver`)

Executable `ld19_node`, node name `ld19_driver`. Written from the
[LD19 Development Manual](https://www.elecrow.com/download/product/SLD06360F/LD19_Development%20Manual_V2.3.pdf):
230400 baud, 47-byte packets with a CRC-8, 12 points each.

| Topic | Type | Notes |
|---|---|---|
| `/scan` | `sensor_msgs/LaserScan` | One message per rotation (10 Hz), `frame_id` `laser_link`. 720 bins of 0.5°; `angle_min` 0 and `angle_increment` negative, so bin i is i × 0.5° clockwise from the LiDAR's front, in the order it was measured. Empty bins are `+inf`. Stamped at the start of the rotation |

Parameters (`config/ld19_driver.yaml`): `port` (`/dev/ttyS0`), `frame_id`, `range_min`/`range_max`
(0.02–12 m), `bins` (720), `min_intensity` (drops weaker points; a white target within 6 m reads about 200)
and `stats_period`. Every `stats_period` seconds it logs its rates: about 10 rotations/s and 416 packets/s,
with no CRC errors.

**`./prowl lidar [port]`** opens a terminal viewer that reads the sensor directly, without ROS: link health,
the nearest object in each direction, and a top-down picture of the live scan. It can't run while `ld19_node`
does, because the serial port can only be opened once.

## MPU6050 IMU (`prowl_mpu6050_driver`)

Executable `mpu6050_node`, node name `mpu6050_driver`. Written from the InvenSense
[MPU-6000/6050 register map](https://cdn.sparkfun.com/datasheets/Sensors/Accelerometers/RM-MPU-6000A.pdf).

| Topic | Type | Notes |
|---|---|---|
| `/imu/data_raw` | `sensor_msgs/Imu` | 100 Hz, `frame_id` `imu_link`. Angular velocity (rad/s, gyro bias removed) and acceleration (m/s², gravity included). No orientation: `orientation_covariance[0]` is −1 |

Parameters (`config/mpu6050_driver.yaml`): `bus` (1), `address` (`0x68`), `frame_id`, `rate` (100 Hz),
`accel_range` (±4 g), `gyro_range` (±500 °/s), `dlpf` (44 Hz low-pass filter), `calibration_time`,
`calibration_max_stddev`, `vibration_period` and `stats_period`.

**Gyro calibration:** for the first 2 s after it starts, the node averages the gyro to find its bias, so keep
the robot still; nothing is published until that's done. To tell whether the robot moved, it averages the
gyro over 0.1 s blocks (`vibration_period`, one LiDAR rotation) and checks that the block averages agree to
within `calibration_max_stddev` (0.3 °/s). The spinning LiDAR shakes the board at 10 Hz, and averaging over
one rotation cancels that, while turning or bumping the robot doesn't. If the robot moved, it tries again, up
to 3 times, then uses the last average and logs a warning. The log line after calibration shows the bias, the
raw noise, and the motion figure it checked.

The accelerometer isn't calibrated yet: at rest it reads gravity about 7 % low (9.1 instead of 9.81 m/s²),
which doesn't change tilt angles. The gyro bias drifts with temperature, about 0.1 °/s per °C, and is only
measured at startup. See [roadmap.md](roadmap.md).

**`./prowl imu [bus address]`** opens a terminal monitor that talks to the chip directly, without ROS: the
readings, tilt, gyro bias and statistics, with keys to change the ranges, the filter and the rate. It can't
run while `mpu6050_node` does, because two readers would steal each other's samples.

## Camera (`prowl_camera_driver`)

Executable `camera_node`, node name `camera_driver`. It talks to the
[Camera Module 3](https://www.raspberrypi.com/documentation/accessories/camera.html) through
[libcamera](https://libcamera.org/api-html/), the camera stack the Pi's own `rpicam` apps use, and encodes
JPEGs with TurboJPEG.

| Name | Type | Notes |
|---|---|---|
| `/camera/image_raw/compressed` | `sensor_msgs/CompressedImage` | Live video: 1280×720 JPEG at 30 fps, about 50 KB a frame. `frame_id` `camera_link`, stamped when the frame started exposing. Best effort (sensor data QoS) |
| `/camera/still/compressed` | `sensor_msgs/CompressedImage` | A 4608×2592 JPEG (about 1 MB) for each `/camera/capture`. Transient local, so a late subscriber still gets the newest one |
| `/camera/capture` | `std_srvs/Trigger` (service) | Takes a full-resolution photo. Answers once it's published, with its size, after about 1 s. Callers that ask while one is being taken all get that one |

How it works:

- The video reads the sensor's **2304×1296 mode**, its whole field of view binned 2×2, and the Pi's ISP
  scales it to 1280×720. Left to itself, libcamera would pick a 1536×864 crop of the middle for 720p and lose a
  third of the wide lens's view. `sensor_width`/`sensor_height` choose the mode.
- Frames come out of the ISP as YUV 4:2:0 in full-range BT.601 (sYCC), which is what JPEG stores, so they go
  into the encoder without any colour conversion. Encoding a 720p frame takes about 14 ms, so the node uses
  about half of one core at 30 fps.
- A **photo** stops the video, switches the sensor to its full 4608×2592 mode, lets exposure and autofocus
  settle for a few frames, takes one picture and switches back. The video pauses for about 0.8 s.
- **Autofocus** runs continuously. If the camera is mounted upside down, set `rotate_180`.
- If the camera is missing or stops sending frames for 5 s, the node reopens it, retrying every second.

Parameters (`config/camera_driver.yaml`): `frame_id`, `width`, `height`, `fps`, `jpeg_quality` (80),
`sensor_width`/`sensor_height` (2304×1296), `still_jpeg_quality` (92), `still_skip_frames`, `autofocus`,
`rotate_180`, `libcamera_log_level` (`ERROR`) and `stats_period`.

Only one program can use the camera at a time: stop the node before using `rpicam-hello` or `rpicam-still` on
the Pi, and the other way round. `./prowl dev cam -l` lists the cameras libcamera can see from the container.

## GPS (`prowl_gps_driver`)

Executable `gps_node`, node name `gps_driver`. It reads the u-blox 7's NMEA output on `/dev/ttyAMA4` and only
listens: it never sends the receiver anything, so the receiver keeps its own settings (9600 baud, one fix a
second). Written from the u-blox 7
[Receiver Description and Protocol Specification](https://content.u-blox.com/sites/default/files/products/documents/u-blox7-V14_ReceiverDescriptionProtocolSpec_%28GPS.G7-SW-12001%29_Public.pdf)
(GPS.G7-SW-12001, sections 18-24: NMEA 0183 2.3) and the
[MAX-7/NEO-7 Hardware Integration Manual](https://content.u-blox.com/sites/default/files/products/documents/MAX7-NEO7_HardwareIntegrationManual_(UBX-13003704).pdf).

| Topic | Type | Notes |
|---|---|---|
| `/gps/fix` | `sensor_msgs/NavSatFix` | Once a second, for each GGA sentence, with or without a fix; `frame_id` `gps_link`, stamped when it arrived. Without a fix, `status` is `NO_FIX` and the position is NaN. `altitude` is above the WGS84 ellipsoid, as NavSatFix asks (mean sea level plus the geoid separation). The covariance is approximated from the DOPs: (HDOP × `uere`)² east and north, (VDOP × `uere`)² up |
| `/gps/vel` | `geometry_msgs/TwistStamped` | For each RMC sentence while there's a fix: velocity over ground, `linear.x` east and `linear.y` north, m/s |
| `/gps/status` | `diagnostic_msgs/DiagnosticStatus` | With each fix. Level OK with a 3D fix, WARN otherwise, and a message like `3D fix from 8 satellites`. Its values, all strings: `fix` (`none`, `2d`, `3d`), `quality` (GGA's: 0 none, 1 GPS, 2 differential), `utc` (the receiver's time, s since 1970, empty until it knows the date), `altitude_msl` (m), `satellites_used`, `satellites_in_view`, `hdop`, `vdop`, `pdop` (99.99 without a fix), and `satellites`, JSON: `[[id, elevation, azimuth, cno, used], ...]`, degrees and dBHz, `null` for what the receiver didn't say |

How it works:

- The NMEA sentences are checked against their checksums and split into fields. GGA gives the fix, RMC the date,
  speed and course, GSA the fix type (2D or 3D), the satellites used and the DOPs, and GSV the satellites in
  view, four per sentence. The receiver stops sending GSV when it sees no satellites, so satellite lists older
  than 3 s are dropped.
- GGA quality 6 (an estimate, which the u-blox 7 also reports for a fix outside its limits) counts as no fix.
- A cold start (no backup battery, or a long time off) takes a while, and indoors there's usually no fix at all:
  the node then publishes `NO_FIX` once a second, and the dashboard says how many satellites it hears.

Parameters (`config/gps_driver.yaml`): `port` (`/dev/ttyAMA4`), `baud` (9600), `frame_id` (`gps_link`), `uere`
(2.5 m: the error per unit of DOP, for the covariance) and `stats_period`. Every `stats_period` seconds it logs
the sentences a second (5 or more), checksum errors, the fix and the satellites in view; with no bytes at all it
says to check the wiring, and with bytes but no sentences, the baud rate.

## Dashboard bridge (`prowl_dashboard_bridge`)

Executable `dashboard_bridge`, node name `dashboard_bridge`. It's written in Node.js, with
[rclnodejs](https://github.com/RobotWebTools/rclnodejs) for ROS2 and `ws` for the WebSocket, and it's the
dashboard's only way to the robot. What it sends and accepts is in [protocol.md](protocol.md).

- **To the dashboard:** `/scan` (cut to 180 points around the robot), `/motor_status` and `/imu/data_raw`
  (averaged), each slowed down to a few updates a second, the GPS (`/gps/fix` with the newest `/gps/vel` and
  `/gps/status`, as one message), a `camera_still` message for each new photo, and a
  once-a-second `status` with how long ago each sensor last sent something and whether the motor driver is
  live, in dry run or not answering (it asks the driver every 2 s).
- **From the dashboard:** drive requests, which go out on `/cmd_vel`. The robot only moves while they keep
  coming: 0.3 s after the last one, the bridge sends a stop. One dashboard drives at a time, and anyone can
  send stop.
- **The camera, over HTTP:** the newest video frame, the newest photo, and taking a photo through
  `/camera/capture`.
- **Photos:** every photo is also saved in `photo_dir` (`edge/data/photos/`, which git ignores) for the
  dashboard's Photos page, which can delete them.

`robot.launch.py`, in this package, starts the whole robot: the LD19, IMU, camera, GPS and motor drivers (in dry
run unless `live:=true`) and the bridge. Its npm packages live in its `node_modules/`, which isn't in git:
`./prowl` installs them when they're missing or the lockfile changed.

Parameters (`config/dashboard_bridge.yaml`):

| Parameter | Value | Meaning |
|---|---|---|
| `host` / `port` | `0.0.0.0` / 8000 | Where it listens |
| `drive_token` | `""` | Needed to drive and to delete photos; empty means anyone who can reach the port can |
| `linear_speed` / `angular_speed` | 1.0 / 3.34 | Top speeds the dashboard can ask for, m/s and rad/s. Set so a full request is full motor duty (driving slowly is harder on Prowl's wheels); keep them in step with `max_linear_speed` and the wheel geometry in `motor_driver.yaml` |
| `drive_timeout` | 0.3 | Seconds without a drive request before it sends a stop |
| `mission_name` | `Prowl live` | Shown as the mission in the dashboard's header |
| `laser_x` / `laser_y` / `laser_yaw` | 0 | Where the LiDAR sits on the robot (TODO: measure) |
| `max_scan_points`, `scan_rate`, `motor_status_rate`, `imu_rate`, `gps_rate` | 180, 5, 10, 5, 2 Hz | How much goes to the dashboard. Each `imu` message is the average of the readings since the last one |
| `stale_after` | 2.0 | Seconds: the robot counts as connected while the LiDAR, motors, IMU, camera or GPS sent something this recently |
| `photo_dir` / `max_photos` | `data/photos` / 200 | Where photos are saved (relative to where the bridge starts, `edge/` in the container), and how many are kept |

The bridge publishes `/cmd_vel`, so with a live motor driver running, a dashboard can move the robot. Test
with the motor driver in dry run.
