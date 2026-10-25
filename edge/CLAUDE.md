# Agent Rules — Prowl Edge Drivers

These rules apply to every agent working in this directory. They override default behavior.

## Rule 1 — At most 2 external APIs
Use no more than **2** external APIs — remote services we don't own or control that the code
fetches data from (e.g. a Google Earth / maps API, a weather API, a cloud service).
Libraries and frameworks (ROS2, GPIO libraries, etc.) are **not** external APIs and don't
count toward this limit. Before adding an external API, name it and confirm it fits.

## Rule 2 — Stay inside this directory
Do not create, modify, or delete any file outside of the `Prowl/` folder.

## Rule 3 — Approve before pushing
Before any push to GitHub, show the user the full list of changes (files and a diff summary)
and wait for explicit approval. No push happens without it.

## Rule 4 — Original code only
Do not base any code on existing code: no copying or adapting existing drivers, examples,
tutorials, or open-source implementations. Write everything from scratch, working from
hardware datasheets and API documentation.

---

## Project context
- **Repo:** https://github.com/matthewthomasbeck/Prowl (we own the `edge/` side)
- **Platform:** Raspberry Pi running ROS2
- **Hardware:**
  - 2× DC motor controllers (DC 5–12V, 0–30A)
  - 4× DC motors on mecanum wheels (the robot can strafe). The sides are mirror images: a positive duty turns a motor clockwise, so the left motors are `inverted: true` in the config to make positive mean "push the robot forward" on every wheel.
  - LIDAR: LD19
  - IMU: MPU6050
  - Camera: Raspberry Pi Camera Module 3 Wide (IMX708) on the CSI port, used through libcamera
- **Approach:** each hardware driver is wrapped in ROS2 nodes (publishers/subscribers).
- **Current focus:** motor controller driver.

## Dev environment (Docker)
ROS2 Jazzy isn't installed on the Pi (Raspberry Pi 4, Raspberry Pi OS bookworm), so drivers are built and tested in a container. `./prowl` starts it, builds the workspace, and opens the motor panel; `./dashboard` builds everything and runs the dashboard with `test_page.launch.py` (it asks about live motors); `docker/dev.sh` does the rest (`up`, `down`, `shell`, or any command, e.g. `docker/dev.sh colcon build`).
- The user isn't in the `docker` group, so `dev.sh` falls back to `sudo docker`.
- The driver talks to the host's `pigpiod`, which listens only on `[::1]:8888`; the container uses host networking to reach it.
- Ubuntu's `libpigpiod-if-dev` is missing `pigpio.h`, so `dev.sh` mounts the host's copy.
- Safe end-to-end check: run the node with `-p dry_run:=true`, publish `/cmd_vel`, echo `/motor_status`.
- Never run the motors live (`dry_run: false`) without the user's OK for that specific run. In scripted panel tests, confirm the node is in dry run before sending any drive key.
- Stop a live driver with SIGINT/SIGTERM sent to the node process itself (or `q` in the panel), never SIGKILL or `docker/dev.sh down`: pigpiod keeps driving the pins after the driver dies, so the motors can keep running. `ros2 run` doesn't pass SIGTERM on, so signalling its wrapper leaves the node running.
- The dashboard bridge (`prowl_dashboard_bridge`, Node.js) publishes `/cmd_vel`, so a connected dashboard can drive: the dry-run rule applies to it too. Its npm packages come from `npm ci` in its folder (`./prowl` runs it), and `npm test` there runs its unit tests. To override one of its parameters on top of the config file, name the node: `-p dashboard_bridge:port:=8042`.
- Processes started with `docker exec` keep running after their terminal disconnects; `./prowl` offers to stop a leftover panel. Test on another ROS domain (`docker/dev.sh env ROS_DOMAIN_ID=42 ...`) so tests can't reach a panel or driver the user has open.
- The camera driver (`prowl_camera_driver`) needs the camera's device nodes in the container, which `dev.sh up` only passes in when it creates the container: if `docker/dev.sh cam -l` lists no camera, `docker/dev.sh down && docker/dev.sh up` (with no live driver running). Only one process can hold the camera: stop `camera_node` before running `rpicam-*` on the host. Test pictures can show people in the room; delete them when done.
- Rule 2 covers temp files too: keep test scripts, logs, and tmux sockets (`tmux -S`) in `.tmp/` inside the project, never `/tmp` or the scratchpad, and delete them afterwards.
