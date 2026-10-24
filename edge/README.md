# Prowl Edge

The code that runs on the robot: a Raspberry Pi 4 running ROS2 Jazzy inside Docker. So far that's the
**motor driver** for the four mecanum wheels, a **terminal control panel** for driving and testing them, and
the **LD19 LiDAR** and **MPU6050 IMU** drivers, and the **dashboard bridge** that serves the robot's data to
the dashboard and lets it drive. Mapping comes next; see [ROADMAP.md](ROADMAP.md).

## What's in this folder

| Path | What it is |
|---|---|
| `prowl` | One command: starts the dev container, builds the code, opens the motor panel |
| `CLAUDE.md` | Rules and notes for AI coding agents working in this folder |
| `docker/Dockerfile` | The ROS2 Jazzy image, plus the pigpio client library, ncurses and Node.js |
| `docker/dev.sh` | Manages the container: `image`, `up`, `down`, `shell`, or runs any command inside it |
| `src/prowl_motor_driver/` | ROS2 package with the motor driver node and the motor panel |
| `src/prowl_motor_driver/config/motor_driver.yaml` | Pins, wheel directions, speed and safety limits |
| `src/prowl_motor_driver/tools/motor_tui/` | Older standalone test tool that drives the pins without ROS; the panel replaces it |
| `src/prowl_ld19_driver/` | ROS2 package with the LD19 LiDAR driver node (`/scan`) and a terminal LiDAR viewer |
| `src/prowl_mpu6050_driver/` | ROS2 package with the MPU6050 IMU driver node (`/imu/data_raw`) and a terminal IMU monitor |
| `src/prowl_dashboard_bridge/` | ROS2 node in Node.js: a WebSocket server for the dashboard (map, pose, LiDAR, motors, driving) |
| `src/prowl_dashboard_bridge/PROTOCOL.md` | The messages the bridge and the dashboard exchange |

## Hardware

- Raspberry Pi 4 (Raspberry Pi OS bookworm)
- 2× DBH-12V dual H-bridge boards (DC 5–12 V, 0–30 A), one per side
- 4× DC motors on mecanum wheels, no encoders
- LD19 LiDAR on the mini UART (`/dev/ttyS0`) and MPU6050 IMU on I2C bus 1 (`/dev/i2c-1`); wiring is in
  [ROADMAP.md](ROADMAP.md), Phase 0

Motor wiring, as BCM GPIO number with the physical header pin in brackets. Each H-bridge channel's EN pin
is held HIGH in hardware rather than wired to the Pi.

| Wheel | H-bridge | IN1 | IN2 | Inverted |
|---|---|---|---|---|
| front_left | left board, channel B | GPIO20 (38) | GPIO21 (40) | yes |
| rear_left | left board, channel A | GPIO5 (29) | GPIO6 (31) | yes |
| front_right | right board, channel A | GPIO17 (11) | GPIO27 (13) | no |
| rear_right | right board, channel B | GPIO22 (15) | GPIO23 (16) | no |

A positive duty turns a motor clockwise. The two sides are mirror images, so the left motors are inverted
in the config; after that, positive means "push the robot forward" on every wheel.

## One-time setup on the Pi

1. Install Docker: `sudo apt install docker.io`. You don't need to be in the `docker` group, because
   `docker/dev.sh` falls back to `sudo docker`.
2. Install and start the pigpio daemon, which gives the driver hardware-timed PWM:
   ```bash
   sudo apt install pigpio
   sudo systemctl enable --now pigpiod
   ```
   It only listens on localhost. The container shares the Pi's network, so it can still reach it. The same
   package provides `/usr/include/pigpio.h`, which the container borrows because Ubuntu's pigpio headers
   leave it out.

## Run it

```bash
./prowl
```

The first run builds the Docker image, which takes a few minutes. After that, `./prowl` starts the
container, rebuilds whatever changed, and opens the panel. It works over SSH.

The panel always starts in **dry run**: the driver runs and the bars move, but no GPIO pin is touched. To
drive the real motors, press `L` (the panel shows `next start: LIVE`), then `N`. LIVE lasts for one run:
once the driver stops, the next start is a dry run again.

| Key | Action |
|---|---|
| Hold `W` / `S`, or ↑ / ↓ | Forward / backward |
| Hold `A` / `D` | Strafe left / right |
| Hold `Z` / `C`, or ← / → | Turn left / right |
| `Space` | Stop |
| `1`–`4` | Run one wheel for 3 s (front_left, rear_left, front_right, rear_right) |
| `T` | Run all four wheels for 3 s |
| `+` / `-` | Speed (duty 0.05–1.00, starts at 0.50), used for driving and the wheel tests |
| `N` | Start / stop the driver |
| `L` | Switch the next start between dry run and LIVE (only while the driver is stopped) |
| `Q` or Ctrl-C | Quit; this stops the driver and the motors coast |

How driving works:

- **Hold to move.** The wheels only turn while the key is repeating. A single tap moves for 0.7 s; after you
  let go of a held key, the stop goes out within about 0.3 s.
- If the connection drops, keys stop arriving and the robot stops the same way. As a second safety net, the
  driver stops by itself 0.5 s after the last command.
- The header says RUNNING about half a second before the driver is ready, so a tap right after `N` may do
  almost nothing.

## The motor driver node

Package `prowl_motor_driver`, executable `motor_driver_node`, node name `motor_driver`.

| Topic | Type | Direction | Meaning |
|---|---|---|---|
| `/cmd_vel` | `geometry_msgs/Twist` | in | Whole-robot motion: `linear.x` forward (m/s), `linear.y` left, i.e. strafing (m/s), `angular.z` counter-clockwise turn (rad/s). Mixed into mecanum wheel duties |
| `/wheel_cmd` | `std_msgs/Float64MultiArray` | in | One duty per wheel in [-1, 1], in the order front_left, rear_left, front_right, rear_right. Positive pushes the robot forward |
| `/motor_status` | `std_msgs/Float64MultiArray` | out | The four duties actually applied (after ramping), same order, published at `loop_rate` |

The most recent message on either input topic wins. The driver is open loop: with no encoders, a speed is
just a fraction of `max_linear_speed` mapped straight to duty cycle.

Parameters (`config/motor_driver.yaml`):

| Parameter | Value | Meaning |
|---|---|---|
| `dry_run` | `false` | `true` runs without touching GPIO |
| `pwm_frequency` | 2000 | Hz; pigpio picks its nearest supported rate |
| `max_duty` | 0.98 | The H-bridge manual recommends staying at or below ~98 % |
| `loop_rate` | 50.0 | Hz |
| `cmd_timeout` | 0.5 | Seconds without a command before the motors stop |
| `max_accel` | 2.0 | Duty per second (0 to full in 0.5 s) |
| `wheel_separation` | 0.3 | m, left–right distance between wheel centers (TODO: measure) |
| `wheel_base` | 0.3 | m, front–rear distance between axles (TODO: measure) |
| `max_linear_speed` | 1.0 | m/s at full duty (TODO: measure) |
| `<wheel>.in1` / `.in2` / `.en` / `.inverted` | see wiring | BCM pins (`en: -1` = not wired) and direction flip |

Running the driver without the panel:

```bash
# Dry run, no GPIO
docker/dev.sh ros2 run prowl_motor_driver motor_driver_node --ros-args \
  --params-file install/prowl_motor_driver/share/prowl_motor_driver/config/motor_driver.yaml -p dry_run:=true

# Live: the launch file uses the config as is, which has dry_run: false
docker/dev.sh ros2 launch prowl_motor_driver motor_driver.launch.py
```

A quick check, from a second terminal while a dry-run driver is running:

```bash
docker/dev.sh ros2 topic pub -r 20 /cmd_vel geometry_msgs/msg/Twist "{linear: {y: 0.5}}"
docker/dev.sh ros2 topic echo /motor_status    # strafe left: [-0.5, 0.5, 0.5, -0.5]
```

## The sensor drivers

Neither one touches the motors, so they're safe to run any time. Each one reads its sensor on a background
thread. If the sensor is missing or stops answering, the node keeps retrying every second, so it can start
before the sensor is plugged in.

```bash
docker/dev.sh ros2 launch prowl_ld19_driver ld19_driver.launch.py
docker/dev.sh ros2 launch prowl_mpu6050_driver mpu6050_driver.launch.py
docker/dev.sh ros2 topic hz /scan            # ~10 Hz
docker/dev.sh ros2 topic hz /imu/data_raw    # ~100 Hz
```

| Node | Topic | Type | Notes |
|---|---|---|---|
| `ld19_driver` (`ld19_node`) | `/scan` | `sensor_msgs/LaserScan` | One message per rotation, `frame_id` `laser_link`. 720 bins of 0.5°; `angle_min` 0 and `angle_increment` negative, so bin i is i × 0.5° clockwise from the LiDAR's front, in the order it was measured. Empty bins are `+inf`. Stamped at the start of the rotation |
| `mpu6050_driver` (`mpu6050_node`) | `/imu/data_raw` | `sensor_msgs/Imu` | 100 Hz, `frame_id` `imu_link`. Angular velocity (rad/s, gyro bias removed) and acceleration (m/s², gravity included). No orientation: `orientation_covariance[0]` is −1 |

Parameters are in each package's `config/` folder:

- **LD19** (`ld19_driver.yaml`): `port`, `frame_id`, `range_min`/`range_max` (0.02–12 m), `bins`,
  `min_intensity`, `stats_period`.
- **MPU6050** (`mpu6050_driver.yaml`): `bus`, `address`, `frame_id`, `rate`, `accel_range`, `gyro_range`,
  `dlpf`, `calibration_time`, `calibration_max_stddev`, `vibration_period`, `stats_period`.

Every `stats_period` seconds, each node logs its rates and errors. The LD19 should show about
10 rotations/s and 416 packets/s.

**Gyro calibration:** for the first 2 s after it starts, the IMU node averages the gyro to find its bias, so
keep the robot still. Nothing is published until that's done. To tell whether the robot moved, it averages
the gyro over 0.1 s blocks (`vibration_period`, one LiDAR rotation) and checks that the block averages agree
to within `calibration_max_stddev` (0.3 °/s). The spinning LiDAR shakes the gyro at 10 Hz, and averaging over
one rotation cancels that, while turning or bumping the robot doesn't cancel. If the robot moved, it tries
again, up to 3 times, then uses the last average and logs a warning. The log line after calibration shows
the bias, the raw noise, and the motion figure that was checked.

**Terminal tools**, which read the sensor directly without ROS: `docker/dev.sh src/prowl_ld19_driver/tui.sh`
and `docker/dev.sh src/prowl_mpu6050_driver/tui.sh`. Stop the matching node first: the LiDAR port can only
be opened once, and two IMU readers would steal each other's samples.

Tests (no hardware needed): `docker/dev.sh colcon test && docker/dev.sh colcon test-result --verbose`.

## The dashboard bridge

Package `prowl_dashboard_bridge`, executable `dashboard_bridge`, node name `dashboard_bridge`. It's written in
Node.js with [rclnodejs](https://github.com/RobotWebTools/rclnodejs) and `ws`, so the frontend team can read
it. It listens on port 8000 and speaks the same messages as the backend's `/ws/dashboard`, so the dashboard
works with either. The messages are in [PROTOCOL.md](src/prowl_dashboard_bridge/PROTOCOL.md).

- **To the dashboard:** `/robot_pose`, `/scan` (cut to 180 points, in the map frame), `/map` (as a PNG) and
  `/motor_status`, each slowed down to a few updates a second.
- **From the dashboard:** drive requests, which go out on `/cmd_vel`. The robot only moves while they keep
  coming: 0.3 s after the last one, the bridge sends a stop. One dashboard drives at a time, and anyone can
  send stop.

Its npm packages live in `src/prowl_dashboard_bridge/node_modules`, which isn't in git. `./prowl` installs
them on the first run; to do it by hand, `docker/dev.sh bash -c 'cd src/prowl_dashboard_bridge && npm ci'`.

```bash
docker/dev.sh ros2 launch prowl_dashboard_bridge dashboard_bridge.launch.py
docker/dev.sh bash -c 'cd src/prowl_dashboard_bridge && npm test'   # unit tests, no ROS needed
```

### Test page

To check the robot without the dashboard, open `http://<pi address>:8000/` in a browser. The bridge
serves a small page (`bridge/test_page.html`, no libraries) with the LiDAR scan around the robot, the
four wheel duties, how long ago each topic last arrived, and hold-to-drive buttons (W/S forward/back,
A/D strafe, Q/E turn, space stops). It uses the same `/ws/dashboard` messages as the dashboard. Add
`?token=<drive_token>` to the address if a token is set.

`test_page.launch.py` starts what it needs: the LD19 driver, the motor driver (dry run unless
`live:=true`) and the bridge. Ctrl-C stops all three.

```bash
docker/dev.sh ros2 launch prowl_dashboard_bridge test_page.launch.py             # motors in dry run
docker/dev.sh ros2 launch prowl_dashboard_bridge test_page.launch.py live:=true  # motors really move
```

To show the robot on the dashboard, put `PROWL_BACKEND=http://<pi address>:8000` in `frontend/.env.local`
and run `npm run dev` in `frontend/`. Add `?token=<drive_token>` to the page address if a token is set.
`frontend/src/live/useDrive.js` provides hold-to-drive buttons.

Parameters (`config/dashboard_bridge.yaml`):

| Parameter | Value | Meaning |
|---|---|---|
| `host` / `port` | `0.0.0.0` / 8000 | Where it listens |
| `drive_token` | `""` | Needed to drive; empty means anyone who can reach the port can drive |
| `linear_speed` / `angular_speed` | 0.3 / 1.0 | Top speeds the dashboard can ask for, m/s and rad/s |
| `drive_timeout` | 0.3 | Seconds without a drive request before it sends a stop |
| `mission_name`, `zone` | `Prowl live`, unset | What the dashboard calls the run, and the survey zone `[x, y, width, height]` |
| `laser_x` / `laser_y` / `laser_yaw` | 0 | Where the LiDAR sits on the robot (TODO: measure) |
| `max_scan_points`, `pose_rate`, `scan_rate`, `motor_status_rate`, `map_period` | 180, 10, 5, 10 Hz, 1 s | How much goes to the dashboard |

To override one on the command line together with the config file, name the node, as in
`-p dashboard_bridge:port:=8042`. A plain `-p port:=8042` loses to the file's value.

The bridge publishes `/cmd_vel`, so treat it like the motor panel: with a live motor driver running, a
dashboard can move the robot. Test with the motor driver in dry run.

## docker/dev.sh

```bash
docker/dev.sh image    # build the image (after changing the Dockerfile)
docker/dev.sh up       # start the container (builds the image first if it's missing)
docker/dev.sh down     # remove the container
docker/dev.sh shell    # a shell inside it, with ROS sourced
docker/dev.sh <cmd>    # run any command inside it, e.g. docker/dev.sh colcon build
```

- The container uses the Pi's network (`--network host`). That's how it reaches pigpiod, and how ROS2 nodes
  elsewhere on the network can see the robot's topics.
- This folder is mounted at `/ws`, and the container runs as your user, so `build/`, `install/` and `log/`
  stay owned by you.
- After changing the Dockerfile, run `docker/dev.sh down && docker/dev.sh image && docker/dev.sh up`. Don't
  do this while a live driver is running (see Safety).

## Safety

- Only drive live with the wheels off the ground or the area clear. Dry run is the default everywhere except
  the launch file.
- To stop a live driver, press `Space` or `Q` in the panel. For a driver started some other way, send
  SIGINT or SIGTERM to the `motor_driver_node` process itself: `docker/dev.sh pkill -INT -f motor_driver_node`.
  Two things don't work:
  - Signalling `ros2 run`, which doesn't pass SIGTERM on to the node.
  - SIGKILL or `docker/dev.sh down`. The driver dies without stopping the motors, and pigpiod keeps the
    last PWM running.
- Closing the terminal doesn't stop the panel: programs started inside the container keep running. The
  motors stop anyway (hold-to-drive plus the driver's timeout), but the driver stays up. `./prowl` notices a
  leftover panel and offers to stop it, or you can run `docker/dev.sh pkill -x motor_panel`.
- Last resort: cut the motor power.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `could not connect to pigpiod` | `sudo systemctl start pigpiod` |
| `permission denied ... docker.sock` | Already handled: `dev.sh` uses `sudo docker`. To skip sudo, run `sudo usermod -aG docker $USER` and log in again |
| Build fails because `pigpio.h` is missing | Install `pigpio` on the Pi; it provides `/usr/include/pigpio.h` |
| `./prowl` asks to stop a running panel | A panel from an earlier session is still running (for example, its window was closed). Answer `y` |
| The panel says `RUNNING (started outside the panel)` | Another driver is running, e.g. from the launch file. Stop that one first |
| A wheel turns the wrong way | Flip `inverted` for that wheel in `config/motor_driver.yaml`; `./prowl` rebuilds |
| Strafing goes the wrong way | The mecanum wheels may be on the wrong corners: seen from above, the top rollers should form an X |

## What's been done so far

- **Docker dev environment.** ROS2 Jazzy has no packages for Raspberry Pi OS, so everything runs in the
  official `ros:jazzy-ros-base` image (Ubuntu 24.04), defined in `docker/Dockerfile` and managed by
  `docker/dev.sh`.
- **`./prowl`.** One command to start the container, build, and open the panel. It waits for the build to
  finish and offers to clean up a leftover panel.
- **Motor driver.** Mecanum mixing for `/cmd_vel` (including strafing), per-wheel control on `/wheel_cmd`, an
  acceleration ramp, a command timeout, and a dry-run mode. The left motors are inverted so positive is
  forward on every wheel.
- **Motor panel.** Starts and stops the driver; hold-to-drive with strafing; wheel tests; speed control;
  LIVE for one run only. If the panel dies, its driver is stopped automatically.
- **LD19 and MPU6050 drivers.** Both libraries were written from the LD19 manual and the MPU-6000/6050
  register map, then wrapped in ROS2 nodes. Checked on the robot: `/scan` at 9.95 Hz with no CRC errors,
  `/imu/data_raw` at 100 Hz. The gyro calibration now ignores the LiDAR's 10 Hz vibration and passes first
  time on the robot. The accelerometer reads gravity about 7 % low and needs a six-position calibration
  (see the roadmap, Phase 2).
- **Tested.** Each motor was run live on the robot. The mecanum mixing and every panel key were checked in
  dry run. The new wheel directions and strafing still need a live check on the robot.

## AI assistance

The edge code and these docs were written with help from an AI coding assistant (Claude Code, by
Anthropic) during the hackathon, with a team member directing the work and testing it on the robot. No code
was copied or adapted from other projects; the drivers are written from the hardware datasheets and API
documentation.

Keep this section up to date as the project changes.
