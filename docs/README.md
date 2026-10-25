# Prowl docs

Prowl is a small mecanum-wheeled robot with a LiDAR, an IMU and a camera, driven from a browser dashboard
styled like a Moon-mission screen. Everything runs on the robot's Raspberry Pi: `./prowl`, at the root of the
repo, starts the robot and serves the dashboard.

| Doc | What's in it |
|---|---|
| [setup.md](setup.md) | Setting up the Pi once, running `./prowl`, opening the dashboard, troubleshooting |
| [safety.md](safety.md) | Driving safely, and how to stop the motors |
| [hardware.md](hardware.md) | The parts, how they're wired, and how to check the wiring |
| [architecture.md](architecture.md) | How the pieces fit together: nodes, topics, the bridge and the dashboard, and the repo's layout |
| [drivers.md](drivers.md) | Each ROS2 package: what it publishes, its parameters, its terminal tool, and running it alone |
| [protocol.md](protocol.md) | The messages the dashboard and the robot's bridge exchange |
| [dashboard.md](dashboard.md) | The dashboard (`frontend/`): its pages, how its code fits together, working on it from a laptop |
| [development.md](development.md) | The dev container, building, tests, and the project's ground rules |
| [roadmap.md](roadmap.md) | What's still open |
| [credits.md](credits.md) | Where the images come from, starter files, and how AI assistance was used |
