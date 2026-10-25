# Safety

The motors are strong enough to hurt fingers and knock things over, and anyone who can open the dashboard can
drive (unless a drive token is set). So:

- **Only drive live with the wheels off the ground or the area clear.**
- **Dry run is the default.** `./prowl` asks before the motors go live, and `./prowl --dry-run` never asks.
  The motor panel starts in dry run every time, and LIVE lasts one run. In dry run the motor driver works out
  the wheel duties (the dashboard and the panel show them) but never touches a pin. The one exception: the
  motor driver's own launch file and config (`motor_driver.launch.py`, `motor_driver.yaml`) run it live, so
  running the motor driver on its own is live.
- **Hold to move.** The robot only moves while a drive button or key is held: the dashboard repeats the
  command 10 times a second and the bridge stops the robot 0.3 s after the last one. Letting go, switching
  windows or tabs, closing the page and losing the connection all stop it. As a last safety net, the motor
  driver stops by itself 0.5 s after `/cmd_vel` goes quiet. **Stop** on the dashboard (or Space with the
  keyboard on) stops the robot whoever is driving.

## Stopping the motors

- **`./prowl`:** Ctrl-C. Every node stops cleanly, and the motor driver stops the motors on the way out.
- **The motor panel:** Space stops the robot, and `Q` quits, which stops its driver.
- **A motor driver started some other way:** send SIGINT or SIGTERM to the `motor_driver_node` process
  itself: `./prowl dev pkill -INT -f motor_driver_node`. Two things don't work:
  - Signalling `ros2 run`, which doesn't pass SIGTERM on to the node.
  - SIGKILL, or `./prowl dev down` (which removes the container). The driver dies without stopping the
    motors, and pigpiod keeps the last PWM running.
- **Last resort:** cut the motor power.

Closing the terminal doesn't stop anything: programs started in the container keep running. The motors stop
anyway (hold-to-drive and the driver's timeout), but the drivers stay up. The next `./prowl` notices and
offers to stop them, or run `./prowl dev pkill -x motor_panel`.

## The network and the camera

- Without a `drive_token` ([setup.md](setup.md#the-drive-token)), anyone on the same network can drive the
  robot. Set one, and use a private hotspot for demos rather than shared Wi-Fi.
- Every photo is kept on the Pi in `edge/data/photos/`, which git ignores. Photos can show people near the
  robot: delete the ones you don't need from the dashboard's Photos page.
