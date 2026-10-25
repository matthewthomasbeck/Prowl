# What's still open

The drivers, the bridge and the dashboard all work on the robot. What's left:

## Measurements

- **Wheel geometry and top speed:** `wheel_separation`, `wheel_base` and `max_linear_speed` in
  `motor_driver.yaml` are guesses (0.3 m, 0.3 m, 1.0 m/s). When they change, change the bridge's
  `linear_speed` and `angular_speed` to match, so a full request from the dashboard is still full duty.
- **Where the LiDAR and the camera sit on the robot:** `laser_x`, `laser_y` and `laser_yaw` in
  `dashboard_bridge.yaml` are 0, so the LiDAR scope draws the scan as if the LiDAR were in the middle of the
  robot. The camera's position would give a `camera_link`.

## Sensors

- **Accelerometer calibration:** standing still, it reads 9.11 m/s² instead of 9.81 (about 70 mg low, within
  the MPU-6050's zero-g offset spec). Per-axis offsets need a six-position calibration: resting the robot on
  each side in turn.
- **Gyro drift:** the bias rises about 0.1 °/s per °C as the chip warms, and it's only measured when the IMU
  driver starts. Start `./prowl` once the robot has warmed up, or restart it before a long run.
- **GPS backup power:** without a battery on the module's V_BCKP, every power-up is a cold start, which is the
  slowest first fix (the u-blox hardware integration manual, section 2.5.3).
- **Camera calibration:** there's no `camera_info` yet. Anything that needs angles or distances from the
  camera needs the lens calibrated first.

## Checks still to do on the robot

- Driving live from the dashboard, the wheel directions and strafing (all checked in dry run so far).
- Turning left gives a positive `angular_velocity.z` on `/imu/data_raw`.
- The LiDAR scope against the real room.
- A GPS fix outdoors, and the GPS page with real satellites (so far it has only heard none, indoors; the page
  was checked with test messages).

## Ideas

- **Position tracking:** wheel encoders for odometry, or mapping from the LiDAR, so the robot can be drawn
  where it really is. The bridge and dashboard used to have unused hooks for a pose and a map (`/robot_pose`,
  `/map`); they were removed in the cleanup, and the git history of `edge/src/prowl_dashboard_bridge/bridge/`
  still has them.
- **A faster GPS:** the u-blox 7 can give more than one fix a second (UBX-CFG-RATE), but the driver only
  listens and the receiver's default is one a second. At 9600 baud a faster rate also needs a faster UART.
- **A street-level map** on the GPS page. The Earth map in the repo is about 10 km a pixel; anything finer
  means map tiles from a map service, which counts as an external API (at most two, see CLAUDE.md).
- **Hardware H.264 video** (the Pi 4's encoder) if JPEG frames use too much Wi-Fi.
- **Real logins** for driving instead of one shared drive token.

## Known issues

- On a phone, the map panel's title bar (the title, the Map / LiDAR scope switch and the tag) is wider than the
  screen, so the page scrolls sideways a little.
