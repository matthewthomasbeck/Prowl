# Message protocol

The contract between the **rover** (Pi), the **backend** (EC2) and the **dashboard** (React).
Every WebSocket message is one JSON object with a `"type"` field. Extra fields are allowed
and passed through, so you can add things without changing the backend.

## Conventions

- **Units:** metres and radians. `theta` is 0 along +x, counter-clockwise positive (ROS REP 103).
- **Frame:** everything is in the SLAM `map` frame, including scan points and findings.
- **Time:** `t` is the robot's clock (optional). The server stamps every message with `ts`
  (server Unix time) and `session_id` before forwarding it.
- **"Latest wins":** `detections` always carries the *full current list*, not a delta. That's
  what lets a dashboard that joins mid-run catch up from one snapshot.

## Rover → server: `ws://<host>:8000/ws/robot?token=<ROBOT_TOKEN>`

A wrong token closes the socket with code **4401**.

| type | suggested rate | logged for replay |
|---|---|---|
| `hello` | once per connection | – |
| `mission` | once, right after `hello` | yes |
| `pose` | 10 Hz | yes |
| `scan` | 2–5 Hz | no (live only) |
| `map` | 1–2 Hz | yes |
| `detections` | 1–2 Hz | yes |
| `analysis` | 1 Hz | yes |
| `status` | 1 Hz | yes |
| `end` | once, at the end of a run | – |

**hello**: first message. Omit `resume_session_id` to start a new run. After a dropped
connection, send the id you got back so the run continues instead of starting over.
Resume works for `RESUME_GRACE_S` (60 s by default) and also for ended runs.
```json
{"type": "hello", "robot_id": "prowl-1", "session_name": "Lab demo, run 3", "resume_session_id": null}
```
Server replies:
```json
{"type": "session", "session_id": "20261024-163512-ab12", "name": "Lab demo, run 3", "resumed": false}
```

**mission**: what the dashboard should call this run, and which part of the map frame is the
survey zone (bottom-left corner plus size, in metres). The dashboard's map box is 3:2, so a 3:2
zone fills it exactly. Set `simulated` only from the simulator: the dashboard then labels
everything as simulator data.
```json
{"type": "mission", "name": "Survey Zone A", "zone": {"x": -1.5, "y": -1.0, "width": 18, "height": 12}, "simulated": false}
```
Without a zone, the dashboard shows 15 m x 10 m centered on where the robot started.

**pose**
```json
{"type": "pose", "t": 1729790000.12, "x": 3.21, "y": 1.05, "theta": 1.571}
```

**map**: the occupancy grid as a grayscale PNG, base64-encoded.
Pixel values: **0 = occupied, 205 = unknown, 254 = free** (same as ROS `map_saver`).
Row 0 of the image is the **top** of the map (max y), so flip the ROS grid vertically.
`origin` is the world position of the grid's bottom-left corner.
```json
{"type": "map", "resolution": 0.05, "width": 344, "height": 244, "origin": [-1.1, -1.1],
 "encoding": "png_base64", "data": "iVBORw0KGgo..."}
```

**scan**: lidar hits in the map frame, downsampled to ~180 points. Live only.
```json
{"type": "scan", "points": [[2.31, 4.02], [2.35, 4.10]]}
```

**detections**: everything the rover has found, as one list.
- `kind: "sample"`: something the **camera** flagged. Position from the camera's bearing plus
  the LiDAR's range at that bearing.
- `kind: "obstacle"`: something solid the **LiDAR** outlined, with a rough `size_m`.

`id` must stay the same for the same thing across messages, because the server gives each new
`id` a display code (`P-001` for samples, `O-001` for obstacles) and remembers when it was first
seen. `label` is whatever your detector says; the dashboard has friendly text for
`light_patch`, `loose_material` and `dark_fragments` (in `frontend/src/live/liveMission.js`)
and a careful generic description for anything else.
```json
{"type": "detections", "items": [
  {"id": "cam-7", "kind": "sample", "label": "light_patch", "confidence": 0.83, "x": 2.8, "y": 3.6},
  {"id": "obs-2", "kind": "obstacle", "label": "lidar_object", "x": 7.2, "y": 3.4, "size_m": 1.0}]}
```
The server adds `code` and `first_seen` to each item before forwarding it.

**analysis**: optional extras computed on the rover.
```json
{"type": "analysis", "explored_m2": 135.1, "warnings": ["Obstacle about 0.6 m ahead"]}
```

**status**: any extra fields are fine.
```json
{"type": "status", "mode": "auto", "battery_v": 12.1, "cpu_temp_c": 61.0}
```

**end**: closes the run. You can send another `hello` on the same socket to start a new one.

**What the rover gets back:** `{"type":"error","error":"...","for_type":"pose"}` when a message
is rejected (the connection stays open), and `command` messages from operators:
`{"type":"command","name":"pause"}`, `{"type":"command","name":"resume"}`. Command names are up
to the rover; the backend just forwards them.

## Dashboard: `ws://<host>:8000/ws/dashboard[?token=<OPERATOR_TOKEN>][&session=<id>]`

Without `session` the dashboard **follows the newest live run**. If nothing is live, it shows the
most recent finished run. With `session=<id>` it stays on that run. Watching and reviewing need
no token; sending commands to the rover needs `token=<OPERATOR_TOKEN>`.

The first message is always a snapshot. After that you get every rover message as-is, plus events.
```json
{"type": "snapshot",
 "session": {"id": "...", "name": "...", "started_at": 1729790000.0, "live": true, "robot_connected": true},
 "latest": {"mission": {}, "pose": {}, "map": {}, "scan": {}, "detections": {}, "analysis": {}, "status": {}},
 "trail": [[1.2, 1.2], [1.3, 1.2]],
 "reviews": {"P-001": {"status": "Prioritize sampling", "note": "Worth a sample", "ts": 1729790100.0}},
 "reports": [{"finding_id": "P-001", "status": "Prioritize sampling", "note": "Worth a sample", "ts": 1729790101.0}],
 "following": true, "can_command": false}
```
A new snapshot also arrives when you switch to a new run, or when your connection fell behind
(the server skips the backlog). **Treat every snapshot as "replace all state".**

Events: `robot_connected`, `robot_disconnected` (with `resume_window_s`), `session_ended`
(with `reason`), `review`, `report`, `command`, `error`.

**Reviewing** (shared with every dashboard, saved in the run's log, works after the run ends too):
```json
{"type": "review", "finding_id": "P-001", "status": "Prioritize sampling", "note": "Worth a sample"}
{"type": "report", "finding_id": "P-001"}
```
`review` may carry just a status or just a note. `report` freezes the finding's current status
and note into the run's list of queued reports. Nothing is sent anywhere else.

**Commands** (operator token only): `{"type": "command", "name": "pause"}`. `{"type":"ping"}` gets
`{"type":"pong"}` back.

## Replay: `ws://<host>:8000/ws/replay/<session_id>?speed=1&from_s=0`

You get `replay_start` (`duration_s`, `messages`), then a `snapshot` (the state at `from_s`),
then the recorded messages with their original timing, each with `replay_t` (seconds from
the start), then `replay_end`. The messages are the same as live ones, so the same rendering
code works. Gaps over 3 s are shortened. Controls:
```json
{"type": "control", "action": "pause"}
{"type": "control", "action": "resume"}
{"type": "control", "action": "speed", "value": 4}
```
To seek, reconnect with a different `from_s`.

## REST

| method | path | notes |
|---|---|---|
| GET | `/health` | |
| GET | `/api/live` | live runs |
| GET | `/api/sessions?limit=100` | newest first |
| GET | `/api/sessions/{id}` | metadata, latest state (map without pixels), trail, reviews, reports |
| GET | `/api/sessions/{id}/map.png` | latest map image |
| GET | `/api/sessions/{id}/log.jsonl` | full recorded log |
| PATCH | `/api/sessions/{id}` | `{"name": "...", "notes": "..."}`, operator token |
| DELETE | `/api/sessions/{id}` | operator token, ended runs only |
| GET | `/docs` | interactive API docs |

Operator token: `?token=...` or the header `Authorization: Bearer ...`.

## On the Pi (Python bridge node)

```python
import base64, io, math
import numpy as np
from PIL import Image

def occupancy_grid_to_msg(grid):  # nav_msgs/msg/OccupancyGrid
    a = np.array(grid.data, dtype=np.int8).reshape(grid.info.height, grid.info.width)
    img = np.full(a.shape, 205, dtype=np.uint8)   # unknown (-1)
    img[(a >= 0) & (a < 25)] = 254                 # free
    img[a >= 65] = 0                               # occupied
    buf = io.BytesIO()
    Image.fromarray(np.flipud(img), "L").save(buf, format="PNG")
    return {"type": "map", "resolution": grid.info.resolution,
            "width": grid.info.width, "height": grid.info.height,
            "origin": [grid.info.origin.position.x, grid.info.origin.position.y],
            "encoding": "png_base64", "data": base64.b64encode(buf.getvalue()).decode()}

def yaw_from_quaternion(q):
    return math.atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z))
```
- **Pose:** look up the `map` → `base_link` transform with tf2 at 10 Hz.
- **Obstacles:** `find_obstacles()` in `tools/fake_robot.py` is a working starting point. It
  clusters occupied map cells, ignores anything touching the edge of the area, and keeps ids
  stable by matching each cluster to the nearest known obstacle.
- **Samples:** for each camera detection, turn the box's horizontal center into a bearing, read
  the LiDAR range at that bearing, and place the point in the map frame.
- **Connection:** copy the connect, resume and reconnect loop from `main()` in
  `tools/fake_robot.py`; it already handles dropped Wi-Fi.
