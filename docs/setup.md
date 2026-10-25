# Setup and running

Prowl runs on a Raspberry Pi 4 with Raspberry Pi OS (bookworm). ROS2 Jazzy isn't packaged for Raspberry Pi
OS, so the robot's code is built and run in a Docker container (`edge/docker/`), and `./prowl` at the root of
the repo does the container work for you. It works over SSH.

## One-time setup on the Pi

1. Install Docker: `sudo apt install docker.io`. You don't need to be in the `docker` group: everything
   falls back to `sudo docker`.
2. Install and start the pigpio daemon, which gives the motor driver hardware-timed PWM:
   ```bash
   sudo apt install pigpio
   sudo systemctl enable --now pigpiod
   ```
   It only listens on localhost; the container shares the Pi's network, so it can still reach it. The same
   package provides `/usr/include/pigpio.h`, which the container borrows because Ubuntu's pigpio headers
   leave it out.
3. Turn on the LiDAR's serial port, the IMU's I2C bus and the camera in `/boot/firmware/config.txt`:
   `enable_uart=1`, `dtparam=i2c_arm=on` and `camera_auto_detect=1`, then reboot. The wiring is in
   [hardware.md](hardware.md).

The first `./prowl` builds the Docker image, which takes a few minutes, and installs the npm packages for the
bridge and the dashboard, which needs internet. After that it only rebuilds what changed.

## Running the robot

```bash
./prowl              # asks whether the motors should be live
./prowl --live       # driving really moves the robot
./prowl --dry-run    # the motors stay still; the dashboard's wheel readout still responds
```

`./prowl` starts the container, builds the robot's code and the dashboard, serves the dashboard on port 5173,
and starts `robot.launch.py`: the LiDAR, IMU, camera and motor drivers and the dashboard bridge (port 8000).
The motor driver is in dry run unless you said live. Once the bridge answers, it prints where to open the
dashboard:

```
Prowl is up. Open the dashboard in a browser on the same network (or Tailscale):

  Wi-Fi       http://192.168.1.42:5173/
  Tailscale   http://100.64.0.7:5173/
  By name     http://raspberrypi.local:5173/

Motors: dry run. Driving won't move the robot (./prowl --live to drive for real).
Anyone who can open the dashboard can drive: no drive_token is set in
edge/src/prowl_dashboard_bridge/config/dashboard_bridge.yaml.
Ctrl-C stops everything.
```

Open any of those addresses on a computer or phone. Keep the robot still for the first 2 s while the IMU
calibrates. **Ctrl-C** stops every node cleanly, which stops the motors, and removes the dashboard's web
server. If a motor panel or an earlier `./prowl` is still running (say, from a dropped SSH connection),
`./prowl` offers to stop it first, since only one motor driver can run.

`DASHBOARD_PORT=8080 ./prowl` serves the dashboard on another port.

## The other commands

| Command | What it does |
|---|---|
| `./prowl panel` | The motor panel in this terminal, for testing the motors without the dashboard ([drivers.md](drivers.md#motor-panel)) |
| `./prowl lidar [port]` | A live view of the LiDAR in this terminal, reading the sensor directly ([drivers.md](drivers.md#ld19-lidar-prowl_ld19_driver)) |
| `./prowl imu [bus address]` | A live view of the IMU in this terminal, reading the sensor directly ([drivers.md](drivers.md#mpu6050-imu-prowl_mpu6050_driver)) |
| `./prowl dev <command>` | Anything else in the container, e.g. `./prowl dev colcon build` or `./prowl dev shell` ([development.md](development.md)) |
| `./prowl help` | The list of commands |

The LiDAR and IMU tools can't run while `./prowl` is running, because only one program can read each sensor;
they say so.

## The drive token

By default anyone who can open the dashboard can drive. To require a token, set `drive_token` in
`edge/src/prowl_dashboard_bridge/config/dashboard_bridge.yaml` and restart `./prowl`. Then type the token into
the "Robot drive token" box under Remote control, or open the dashboard with `?token=<drive_token>` on the
address; the browser remembers it. Watching never needs it. For a demo, use a private hotspot rather than
shared Wi-Fi.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `could not connect to pigpiod` | `sudo systemctl start pigpiod` |
| `permission denied ... docker.sock` | Already handled: the scripts use `sudo docker`. To skip sudo, run `sudo usermod -aG docker $USER` and log in again |
| The build fails because `pigpio.h` is missing | Install `pigpio` on the Pi; it provides `/usr/include/pigpio.h` |
| `./prowl` asks to stop a motor panel or a running robot | One is left over from an earlier session (for example, its window was closed). Answer `y` |
| The dashboard says the motors are in dry run | `./prowl` was started without `--live`. Ctrl-C, then `./prowl --live` |
| The dashboard says "Robot not connected" | `./prowl` isn't running, or the address is wrong. It retries by itself once the robot is back |
| `Port 5173 is already in use` | An `npm run dev` or another web server has the port. Stop it, or use `DASHBOARD_PORT` |
| The panel says `RUNNING (started outside the panel)` | `./prowl` (or another motor driver) is running. Stop it first, or drive from the dashboard |
| A wheel turns the wrong way | Flip `inverted` for that wheel in `edge/src/prowl_motor_driver/config/motor_driver.yaml`; `./prowl` rebuilds |
| Strafing goes the wrong way | The mecanum wheels may be on the wrong corners: seen from above, the top rollers should form an X |
| `camera_driver` says `no camera found` | `./prowl dev cam -l` should list `imx708_wide`. If it doesn't, but `rpicam-hello --list-cameras` on the Pi does, recreate the container with no robot running: `./prowl dev down && ./prowl dev up`. If neither does, check the ribbon cable at both ends |
| `camera_driver` says the camera is in use | Another program has it: an `rpicam` app on the Pi, or a second `camera_node` |
| `./prowl lidar` or `./prowl imu` says the driver is reading the sensor | `./prowl` is running; stop it first |
