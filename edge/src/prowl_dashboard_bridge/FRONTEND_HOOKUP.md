# Hooking the dashboard up to the robot bridge

Instructions for an agent (or person) connecting the React dashboard in `frontend/` straight to the
robot, through the Node.js bridge in this folder. Read all of it before changing code.

## Status: done (branch `dashboard-robot-link`)

The dashboard does all of the goals below. It was built on `frontend/` as it is on `main`, in its own
style, not on `merge-remote-control` or `bridge-hookup`. Where it differs from the tasks further down:

- **Which server:** the bridge's snapshot now lists `capabilities` (PROTOCOL.md), and the dashboard goes by
  `capabilities.drive === 'hold'`, not by a `motor_status` key. The backend sends no list.
- **LiDAR:** not drawn on the photo map. The map panel has a "Map / LiDAR scope" switch; the scope
  (`LidarScope.jsx`) keeps the robot in the middle with forward up, with distance rings, fading older
  scans and the clearance on each side.
- **Pages:** connected to the robot, the dashboard has three pages, switched by tabs under the header:
  Map (the dashboard), Camera (`CameraPage.jsx`, `#camera`) and Photos (`PhotosPage.jsx`, `#photos`).
- **Remote control:** a panel along the bottom of every page (`DriveBar.jsx`): hold-to-drive buttons and keys
  with strafing, always full speed, Stop, the wheel duties, the IMU (turn rate and a tilt level), sensor
  health, and a warning when `status.motors` says the motors are in dry run or the driver isn't answering.
- **Camera:** the live video is only on the Camera page, with Take photo and the newest photo. The map page's
  camera panel links to it. Every photo is kept on the robot and listed on the Photos page, with a Delete
  button.
- **Video by pulling frames, not MJPEG** (unlike task 5): the Camera page asks for `frame.jpg`, and asks for
  the next one when it arrives, at most 15 a second. The MJPEG stream keeps sending whether the browser keeps up
  or not, so with a busy browser, a slow link or the dashboard's web server in between, frames queue up on the
  way and the video falls seconds behind (about 8 s in a test with a link slower than the stream). Pulling is
  never more than a frame behind. `vite.config.js` keeps the proxy's connections to the bridge open, or every
  frame would need a new connection.
- **Performance:** the dashboard lagged where the test page didn't. Profiled in Firefox on the Pi against a mock
  bridge (software rendering), the dashboard drew about 0.5 frames a second and the test page about 10. The causes:
  the blurred glass (`backdrop-filter`) behind every panel, which the browser re-blurs for every change; looping
  CSS animations; the video in the same page; and every LiDAR, wheel and IMU message re-rendering the whole React
  app. Connected to the robot, the page now has no blur and no looping animations (`index.css`), those readings
  re-render only what shows them (`useLiveValue` in `useProwlLink.js`), and the video has its own page. The map
  page then drew about 15 frames a second in the same test.

The rest of this file is the original plan, kept for reference.

## Goal

When the dashboard is pointed at the robot (`PROWL_BACKEND=http://<pi address>:8000`), it should:

1. show the LiDAR scan live on the map,
2. drive the robot with its remote-control panel (hold to drive, let go to stop, Stop always works),
3. show the four wheel duties and flag stale sensors,
4. show the robot's camera live in the camera panel, and take full-resolution photos,

and keep working exactly as it does today with the Python backend (`backend/`) and in demo mode.

## Read first

| File | Why |
|---|---|
| `edge/src/prowl_dashboard_bridge/PROTOCOL.md` | Every message the bridge sends and accepts. **The source of truth.** |
| `backend/PROTOCOL.md` | The backend's protocol. The bridge speaks a subset of it, plus three message types of its own |
| `frontend/src/live/useProwlLink.js` | The WebSocket connection and the state it keeps |
| `frontend/src/live/useDrive.js` | Hold-to-drive, written for the bridge's rules (10×/s while held, zero on release); use it, don't rewrite it |
| `frontend/src/live/liveMission.js` | Turns metres in the map frame into the panels' local grid units (`makeProjection`) |
| `edge/src/prowl_dashboard_bridge/bridge/test_page.html` | A small working client of the bridge (scan plot, duties, driving). Useful as a reference for the protocol, not for styling |

## Two things to sort out before coding

**1. Which branch to start from.** On `main`, `useProwlLink.js` reads `?token=` from the page address,
and `useDrive.js` exists. The frontend's remote-control work (`RemoteControl.jsx`, `robot/driveCommands.js`,
a typed operator-token box) is on `origin/merge-remote-control`, which isn't on `main` yet. Its
`useProwlLink(token)` takes the token as an argument instead, and it adds `canCommand`. Ask the user
whether that branch has been merged. If not, branch off `main` and merge `origin/merge-remote-control` into
your branch. In `useProwlLink.js`'s `socketUrl`, keep the branch's `token` argument, and fall back to
`?token=` from the address when the argument is empty. Both docs tell people to use `?token=`.

**2. The two servers drive differently.** This is the main incompatibility.

| | Backend (`backend/`) | Robot bridge (this folder) |
|---|---|---|
| Drive message | `{"type":"command","name":"drive","args":{"direction":"forward"}}`, one short step per click | `{"type":"drive","x":..,"y":..,"turn":..}`, **repeated about 10×/s while held** |
| Stop | `command` with `"direction":"stop"` | `{"type":"stop"}`, which works for anyone, even without a token |
| Let go | nothing | `drive` with all zeros (`useDrive` does it) |
| Back to autopilot | `{"type":"command","name":"auto"}` | not supported (no autopilot yet) |
| Any `command` message | forwarded to the rover | answered with `error: the robot bridge doesn't handle "command"` |
| Token | backend's `OPERATOR_TOKEN`, as `?token=` on the socket | bridge's `drive_token` in `config/dashboard_bridge.yaml`, same `?token=`. Empty = anyone can drive |

With the bridge, a single `drive` message moves the robot for only 0.3 s (`drive_timeout`), then the
bridge sends a stop. So the click-per-step remote control does nothing useful on the bridge. On the bridge, it
has to hold to drive.

## How to tell which server you're connected to

The bridge's snapshot has a `capabilities` list (PROTOCOL.md); the backend's has none. `useProwlLink`
fills in the backend's from `BACKEND_CAPABILITIES`, and the robot is the server whose `capabilities.drive`
is `'hold'`. Don't detect it any other way (no session-id prefixes, no port numbers).

(The first version of this plan checked for a `motor_status` key in the snapshot's `latest`. Use
`capabilities` instead.)

## Tasks

### 1. `useProwlLink.js`: keep what the bridge sends

The `default:` branch currently drops `scan` and `motor_status`. Add to `empty` and handle:

- `robotBridge` (above), set from the snapshot.
- `scan`: `{t, points: [[x, y], ...]}` in metres, map frame, at most 180 points, about 5×/s. It's in the
  snapshot (`latest.scan`) and arrives as its own messages. The backend sends the same shape, so one
  code path covers both.
- `motorStatus`: `{wheels: ['front_left','rear_left','front_right','rear_right'], duties: [-1..1 ×4]}`,
  from `latest.motor_status` and `motor_status` messages, about 10×/s. Positive duty = pushing the robot
  forward on every wheel.
- `cameraStill`: `{t, width, height, bytes, url}`, from `latest.camera_still` and `camera_still`
  messages, each time anyone takes a photo.
- `status` already exists. From the bridge it's `{mode: 'manual', driving, dashboards, topic_age_s: {pose,
  scan, map, motor_status, camera}}`, with ages in seconds or `null` for never. It has no `battery_v`, so check that
  nothing assumes it's there.

Updates are already batched every 150 ms (`FLUSH_MS`), so 5–10 Hz messages are fine. Don't render on
every message.

### 2. Map: draw the scan points

In `liveMission.js`, project each scan point with the same `projection.toLocal(x, y)` used for the robot
and trail, and pass the result to `MissionMap` as a new layer of small dots, drawn above the map image and
below the robot marker. Label it in the existing style ("LiDAR — live" for a real robot), and add a
color-blind-safe color to the legend.

Note that nothing on the robot publishes a pose or a map. `pose` and `map` stay `null` from the bridge
(its `topic_age_s` says `never`), so the bridge places scan points as if the robot were at the map origin
facing +x. With the default zone, that puts the robot dot in the middle of the map with the room drawn
around it. That's expected, so don't "fix" it.

### 3. Remote control on the bridge

When `link.robotBridge` is true:

- Create `const drive = useDrive(link)` once, in `App.jsx`, not inside the panel.
- **Buttons**: spread `drive.hold({...})` on each D-pad button instead of `onClick`. Mapping: up `{x: 1}`,
  down `{x: -1}`, left `{turn: 1}`, right `{turn: -1}`. The Stop button calls `drive.stop()`.
- **Keyboard** (only while the panel's "Keyboard: ON" toggle is on): track the held keys, and on every
  keydown or keyup call `drive.set(sum of held keys)`, or `drive.set({})` when none are held. Ignore
  `event.repeat`. Don't drive on keydown alone: a key-up that never arrives must not leave the robot
  moving. `useDrive` already lets go on window blur and when the tab is hidden. X calls `drive.stop()`.
- **Speed**: commands are fractions of the robot's top speeds, and 1 is full motor duty. **Default to 1.**
  On Prowl's wheels, slow driving is harder on the wheels than fast, so don't scale commands down. A speed
  slider is fine, but start it at 100 %.
- **Strafing**: the robot has mecanum wheels and can move sideways with `y` (+ = left). Comments in
  `driveCommands.js` call it "a tank"; that's wrong for the real robot. Strafe isn't required, but if you
  add it (for example Q/E, or Shift with left/right), use `{y: ±1}`.
- **Hide "Back to autopilot"** (the reset button's live label): the bridge has no autopilot.
- **Readout**: show "driving" while `drive.driving` is true, and `error` messages that have `for_type:
  'drive'` (for example `another dashboard is driving` or `driving needs the operator token`).
  `useProwlLink` only logs errors to the console now, so keep the latest one in state.
- **Token**: if `link.canCommand` is false, show the existing token box, and say it's the robot's
  `drive_token` rather than "From the server's .env file".

When `link.robotBridge` is false, leave the backend path (`sendDriveCommand`) exactly as it is.

### 4. Wheels and sensor health

Add a small read-only block, in the remote-control panel or next to it, with the four duties laid out as
the robot seen from above (front pair on top, left wheels on the left). Each duty gets a bar centred on
zero and a number. Don't use color alone for direction: show the sign. Show it only when `motorStatus`
exists.

Use `status.topic_age_s` to warn when a sensor goes quiet: an age over 2 s for `scan` or `motor_status`
means that driver has stalled. `null` means it never started, and for `pose` and `map` that's normal
(nothing publishes them). Don't warn about it.

### 5. Camera panel

The camera is plain HTTP under `/api/camera/` (PROTOCOL.md, "Camera"). The dev server already forwards
`/api` to `PROWL_BACKEND`, so use relative addresses and don't touch `vite.config.js`.

When `link.robotBridge` is true, `CameraPanel` shows:

- **Live video**: `<img src="/api/camera/video.mjpg">`. The browser plays the MJPEG stream by itself, about
  15 frames a second, 16:9. Don't poll `frame.jpg` for video; the stream is cheaper. The stream ends when the
  bridge restarts: on the `<img>`'s `error` event, set `src` again after 2 s, with `?<timestamp>` added so
  the browser doesn't reuse the dead one. Mention in the alt text that it's the robot's live camera.
- **Take photo**: a button that sends `fetch('/api/camera/capture', {method: 'POST'})` and is disabled until
  the answer comes back (about 1 s). On `ok: false`, show `error`: `409` means someone else is taking one,
  `503` means the camera driver isn't running. Warn that the video pauses for a moment.
- **The newest photo**: from `cameraStill` (which arrives for everyone, whoever took it), a thumbnail with
  `src={cameraStill.url}` that opens the full 4608×2592 image in a new tab. Show its time.
- **Stale video**: if `status.topic_age_s.camera` is over 2 s, say the camera stalled; `null` means the
  camera driver never started. Keep the panel's current offline message for that.

When `link.robotBridge` is false, leave the panel as it is today: the backend has no camera.

### 6. Things the bridge doesn't do

- Reviews, notes and "Queue report" are backend features. The bridge answers `review` and `report` with
  an `error`. On the bridge, keep notes local, and don't show "Saved to mission log".
- No `detections`, `analysis`, replay or `session_ended`. Their panels should show their empty states.
- The bridge doesn't serve the built dashboard. Its `GET /` is the test page, so use `npm run dev` (or
  `npm run preview`) with `PROWL_BACKEND` pointing at the Pi.

## Testing

**Without the robot**: `npm run lint`, `npm run build`, then check that demo mode and the backend's fake
robot (`backend/README.md`) still work exactly as before.

**With a mock bridge (no ROS needed)**: `bridge/server.js` has no ROS in it. In
`edge/src/prowl_dashboard_bridge`, run `npm ci --ignore-scripts`, which installs `ws` without building the
ROS addon. Then write a short script outside the repo that calls `startServer` the way
`test/server.test.js` does. Give it a gate that echoes drive requests into fake `motor_status` duties, and
broadcast a fake `scan`, such as a ring of points. Pass a `CameraFeed` (`bridge/camera.js`) and call its
`pushFrame` with any JPEG a few times a second to fake the video. Don't commit the script.

**With the robot** (on the Pi, from `edge/`):

```bash
docker/dev.sh ros2 launch prowl_dashboard_bridge test_page.launch.py   # LiDAR + camera + motor driver in DRY RUN + bridge
```

Then put `PROWL_BACKEND=http://<pi address>:8000` in `frontend/.env.local` and run `npm run dev`. Check:

- [ ] Scan points show the same room shape as the bridge's own test page at `http://<pi address>:8000/`.
      It's turned 90°: the test page draws the robot's forward as up, while the map draws +x (forward at
      start) to the right.
- [ ] Holding ▲ shows all four duties going positive. Letting go brings them back to 0 within a fraction of a
      second, and so does switching tabs while holding.
- [ ] Stop works while another browser (the test page) is driving.
- [ ] With a `drive_token` set and no token entered, driving shows the token error and the robot doesn't move.
- [ ] The camera panel shows live video through the dev server's proxy, and a photo taken from the test page
      shows up in the dashboard too.
- [ ] Stopping the bridge (Ctrl-C on the launch) makes the dashboard show it lost the connection. Starting
      it again reconnects.

## Safety

- **Only test with the motor driver in dry run.** `test_page.launch.py` defaults to dry run, so never pass
  `live:=true` unless the user says so for that specific run. A dashboard connected to the bridge drives
  the real motors when the driver is live. `status.motors` says which it is.
- Never remove or weaken the stop paths: zero on release, stop on blur and when the tab is hidden, the Stop
  button, the bridge's 0.3 s timeout and the motor driver's 0.5 s timeout.
- Don't change the bridge's protocol to suit the frontend. If you need something from the robot side,
  write it down for the edge team instead (and see `edge/CLAUDE.md` before touching anything in `edge/`).
