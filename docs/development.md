# Development

## The dev container

ROS2 Jazzy has no packages for Raspberry Pi OS, so the robot's code builds and runs in the official
`ros:jazzy-ros-base` image (Ubuntu 24.04), with the pigpio client library, ncurses, libcamera, TurboJPEG and
Node.js added (`edge/docker/Dockerfile`). `edge/docker/dev.sh` manages it, and `./prowl dev` is a shortcut to
it from the repo root:

```bash
./prowl dev image    # build the image (after changing the Dockerfile)
./prowl dev up       # start the container (builds the image first if it's missing)
./prowl dev down     # remove the container
./prowl dev shell    # a shell inside it, with ROS sourced
./prowl dev <cmd>    # run any command inside it, e.g. ./prowl dev colcon build
```

- The container uses the Pi's network (`--network host`). That's how it reaches pigpiod, how the dashboard
  reaches the bridge, and how ROS2 nodes elsewhere on the network can see the robot's topics.
- `edge/` is mounted at `/ws`, the container's working directory, and the container runs as your user, so
  `build/`, `install/` and `log/` stay owned by you.
- It gets the sensors' device nodes (`/dev/ttyS0`, `/dev/ttyAMA4`, `/dev/i2c-1`) and the camera's (`/dev/media*`,
  `/dev/video*`, `/dev/v4l-subdev*`, `/dev/dma_heap/*`), and reads the Pi's udev database, which libcamera
  needs to find the camera. Devices are only passed in when the container is created, so after plugging
  something in, run `./prowl dev down && ./prowl dev up`.
- After changing the Dockerfile: `./prowl dev down && ./prowl dev image && ./prowl dev up`. Don't do either of
  these while a live motor driver is running ([safety.md](safety.md)).
- Programs started in the container keep running after their terminal closes; `./prowl` offers to stop
  leftovers.

## Building

`./prowl` builds everything it needs each time it starts. By hand:

```bash
./prowl dev colcon build                                         # the whole workspace
./prowl dev colcon build --packages-select prowl_camera_driver   # one package
```

The C++ packages build with `-Wall -Wextra -Wpedantic`. The bridge has nothing to compile, but its npm packages
have to be installed first (`./prowl` does it):
`./prowl dev bash -c 'cd src/prowl_dashboard_bridge && npm ci'`. The dashboard is built with `npm run build`
in `frontend/` ([dashboard.md](dashboard.md#working-on-it-from-a-laptop)).

## Tests

None of them need the hardware.

```bash
./prowl dev colcon test && ./prowl dev colcon test-result --verbose   # the C++ drivers' unit tests
./prowl dev bash -c 'cd src/prowl_dashboard_bridge && npm test'      # the bridge's unit tests
```

| Package | What's tested |
|---|---|
| `prowl_ld19_driver` | The CRC and packet parser against the manual's example packet, packets that cross 0°, corrupted bytes and resyncing, scan assembly, and the conversion to `LaserScan` |
| `prowl_mpu6050_driver` | Register settings and sample rates, unit conversions, tilt, and the gyro bias check with and without the LiDAR's vibration |
| `prowl_camera_driver` | JPEG encoding from padded YUV planes (grey levels and colour), bad input, and the timing helpers |
| `prowl_gps_driver` | NMEA checksums and sentence splitting (starting mid-stream, corrupted and binary bytes), the field formats, GGA, RMC, GSA and GSV against the protocol specification's examples and sentences captured from the receiver, combining them into a fix, and the conversion to `NavSatFix`, `TwistStamped` and `DiagnosticStatus` |
| `prowl_dashboard_bridge` | The message conversions, the drive gate, the camera feed, the photo store and the HTTP and WebSocket server |

For the dashboard, `npm run lint` and `npm run build` in `frontend/`.

## Testing on the robot

- Keep the motors in **dry run** (`./prowl --dry-run`) unless the run is meant to move the robot, and say so
  first ([safety.md](safety.md)). In dry run the wheel duties still change, so driving can be checked
  without anything moving.
- To test nodes without disturbing a panel or a `./prowl` someone has open, use another ROS domain:
  `./prowl dev env ROS_DOMAIN_ID=42 ros2 launch ...`.
- Keep test scripts, logs and tmux sockets in `.tmp/` inside the repo (git ignores it), and delete them when
  done.
- Test photos can show people in the room: delete them when done.

## Ground rules

These come from [CLAUDE.md](../CLAUDE.md), which also has the rules for AI coding agents:

- **Original code only.** Drivers are written from the hardware datasheets and API documentation, not
  copied or adapted from existing drivers or examples (for instance LDRobot's `ldlidar_stl_ros2` or
  Adafruit's MPU6050 libraries). Libraries used as they are, such as rclnodejs, React and Vite, are fine.
- **At most two external APIs** (remote services the code fetches data from). There are none at the moment:
  the dashboard only talks to the robot, and the NASA images, including the GPS page's map of the Earth, are
  plain files in the repo. A street-level map (map tiles from a map service) would be one.
- **Nothing is pushed to GitHub without the owner's OK**, after seeing the full list of changes.
- ROS conventions (REP 103): x forward, y left, z up, angles counter-clockwise, metres and radians.
