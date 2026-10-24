# Prowl Edge

The code that runs on the robot: a Raspberry Pi 4 running ROS2 Jazzy inside Docker. So far that's the
**motor driver** for the four mecanum wheels and a **terminal control panel** for driving and testing them.
The LiDAR, IMU, mapping and the dashboard link come next; see [ROADMAP.md](ROADMAP.md).

## What's in this folder

| Path | What it is |
|---|---|
| `prowl` | One command: starts the dev container, builds the code, opens the motor panel |
| `CLAUDE.md` | Rules and notes for AI coding agents working in this folder |
| `docker/Dockerfile` | The ROS2 Jazzy image, plus the pigpio client library and ncurses |
| `docker/dev.sh` | Manages the container: `image`, `up`, `down`, `shell`, or runs any command inside it |
| `src/prowl_motor_driver/` | ROS2 package with the motor driver node and the motor panel |
| `src/prowl_motor_driver/config/motor_driver.yaml` | Pins, wheel directions, speed and safety limits |
| `src/prowl_motor_driver/tools/motor_tui/` | Older standalone test tool that drives the pins without ROS; the panel replaces it |

## Hardware

- Raspberry Pi 4 (Raspberry Pi OS bookworm)
- 2× DBH-12V dual H-bridge boards (DC 5–12 V, 0–30 A), one per side
- 4× DC motors on mecanum wheels, no encoders
- Not used yet: LD19 LiDAR and MPU6050 IMU (see the roadmap)

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
- **Tested.** Each motor was run live on the robot. The mecanum mixing and every panel key were checked in
  dry run. The new wheel directions and strafing still need a live check on the robot.

## AI assistance

The edge code and these docs were written with help from an AI coding assistant (Claude Code, by
Anthropic) during the hackathon, with a team member directing the work and testing it on the robot. No code
was copied or adapted from other projects; the drivers are written from the hardware datasheets and API
documentation.

Keep this section up to date as the project changes.
