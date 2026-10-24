"""Fake Prowl rover: drives a simulated mock-Moon survey zone with a simulated 360° lidar
and camera, and streams everything to the backend exactly like the real Pi should.

What it simulates:
  - LiDAR: raycasts against boulders and the arena edge, builds an occupancy map, and finds
    obstacles by clustering occupied cells (the same idea the real robot can use).
  - Camera: "detects" flat material patches the lidar can't see, when they're in view and close.

Use it to develop the dashboard without hardware, and read it as the reference client for the
Pi-side bridge (hello / mission / resume / reconnect / commands).

    python tools/fake_robot.py                                  # local backend
    python tools/fake_robot.py --url ws://<ec2-ip>:8000/ws/robot --token <ROBOT_TOKEN> --speed 2

Commands it understands from the dashboard: {"type":"command","name":"pause"} and "resume".
"""
import argparse
import asyncio
import base64
import io
import json
import math
import random
import time
from collections import deque
from urllib.parse import urlencode

import numpy as np
import websockets
from PIL import Image

RES = 0.05                    # map resolution, metres per cell
WORLD_ORIGIN = (-2.0, -1.5)   # world (x, y) of grid cell (0, 0)
WORLD_SIZE = (19.0, 13.0)     # metres
LIDAR_RANGE = 8.0
LIDAR_BEAMS = 360
CAMERA_FOV = math.radians(70)
CAMERA_RANGE = 3.5

# The survey zone shown on the dashboard (map-frame metres, 3:2 like the dashboard's map box).
ZONE = {"x": -1.5, "y": -1.0, "width": 18.0, "height": 12.0}

# ---------------------------------------------------------------- the mock Moon area
ARENA = (0.0, 0.0, 15.0, 10.0)  # x0, y0, x1, y1: a low wall around the mock area
WALL = 0.15
BOULDERS = [  # (x, y, radius) in metres; the lidar sees these
    (4.0, 7.2, 0.55), (7.2, 3.4, 0.45), (10.6, 6.8, 0.75),
    (12.6, 2.6, 0.35), (6.4, 8.6, 0.30), (13.4, 8.3, 0.50),
]
RIDGES = [(9.3, 4.6, 0.9, 0.3)]  # (x, y, half-length x, half-width y): a long low rock
MATERIALS = [  # flat patches only the camera can notice: (id, label, x, y)
    ("m1", "light_patch", 2.8, 3.6),
    ("m2", "loose_material", 8.4, 5.9),
    ("m3", "dark_fragments", 11.8, 4.6),
    ("m4", "light_patch", 13.6, 6.0),
    ("m5", "loose_material", 4.8, 1.6),
]
WAYPOINTS = [
    (1.2, 1.2), (5.6, 1.0), (9.0, 1.4), (13.8, 1.2), (13.9, 4.8), (12.0, 5.2),
    (8.6, 6.0), (7.4, 7.6), (5.2, 8.0), (2.4, 8.6), (1.4, 5.0),
]


def to_cell(x, y):
    return int((x - WORLD_ORIGIN[0]) / RES), int((y - WORLD_ORIGIN[1]) / RES)


def build_ground_truth() -> np.ndarray:
    w, h = int(WORLD_SIZE[0] / RES), int(WORLD_SIZE[1] / RES)
    grid = np.zeros((h, w), dtype=bool)  # grid[iy, ix]; iy = 0 is the bottom (min y)
    ys, xs = np.mgrid[0:h, 0:w]
    wx = WORLD_ORIGIN[0] + (xs + 0.5) * RES
    wy = WORLD_ORIGIN[1] + (ys + 0.5) * RES
    x0, y0, x1, y1 = ARENA
    outer = (wx > x0 - WALL) & (wx < x1 + WALL) & (wy > y0 - WALL) & (wy < y1 + WALL)
    inner = (wx > x0) & (wx < x1) & (wy > y0) & (wy < y1)
    grid |= outer & ~inner
    for bx, by, r in BOULDERS:
        # slightly lumpy, so they don't look like perfect circles on the map
        ang = np.arctan2(wy - by, wx - bx)
        lumpy = r * (1 + 0.12 * np.sin(3 * ang + bx) + 0.06 * np.cos(5 * ang + by))
        grid |= (wx - bx) ** 2 + (wy - by) ** 2 < lumpy ** 2
    for rx, ry, hx, hy in RIDGES:
        grid |= ((wx - rx) / hx) ** 2 + ((wy - ry) / hy) ** 2 < 1
    return grid


# ---------------------------------------------------------------- simulation

class Sim:
    def __init__(self):
        self.gt = build_ground_truth()
        self.h, self.w = self.gt.shape
        self.logodds = np.zeros((self.h, self.w), dtype=np.float32)
        self.x, self.y = WAYPOINTS[0]
        self.theta = 0.0
        self.wp = 1
        self.paused = False
        self.t = 0.0  # simulated seconds
        self.samples: dict[str, dict] = {}
        self.obstacles: dict[str, dict] = {}  # visible now
        self.known_obstacles: dict[str, dict] = {}  # every id ever given, for stable matching
        self.next_obstacle = 1
        self.battery = 12.4
        self.steps = np.arange(0, LIDAR_RANGE, RES / 2, dtype=np.float32)
        self.beam_angles = np.linspace(-math.pi, math.pi, LIDAR_BEAMS, endpoint=False, dtype=np.float32)
        self.last_hits = np.zeros((0, 2))

    # --- motion: skid-steer, turn in place then drive
    def step(self, dt: float) -> None:
        self.t += dt
        self.battery = max(10.5, self.battery - 0.0006 * dt)
        if self.paused:
            return
        tx, ty = WAYPOINTS[self.wp]
        dx, dy = tx - self.x, ty - self.y
        if math.hypot(dx, dy) < 0.08:
            self.wp = (self.wp + 1) % len(WAYPOINTS)
            return
        err = (math.atan2(dy, dx) - self.theta + math.pi) % (2 * math.pi) - math.pi
        if abs(err) > 0.08:
            self.theta += max(-1.2 * dt, min(1.2 * dt, err))
        else:
            self.theta += err * 0.5
            d = min(0.35 * dt, math.hypot(dx, dy))
            self.x += d * math.cos(self.theta)
            self.y += d * math.sin(self.theta)
        self.theta = (self.theta + math.pi) % (2 * math.pi) - math.pi

    # --- lidar: raycast against the ground truth, then update the occupancy map
    def raycast(self, angles: np.ndarray):
        cos, sin = np.cos(angles)[:, None], np.sin(angles)[:, None]
        ix = ((self.x + cos * self.steps - WORLD_ORIGIN[0]) / RES).astype(np.int32)
        iy = ((self.y + sin * self.steps - WORLD_ORIGIN[1]) / RES).astype(np.int32)
        inside = (ix >= 0) & (ix < self.w) & (iy >= 0) & (iy < self.h)
        occ = np.zeros(ix.shape, dtype=bool)
        occ[inside] = self.gt[iy[inside], ix[inside]]
        hit = occ.any(axis=1)
        dist = np.where(hit, self.steps[occ.argmax(axis=1)], LIDAR_RANGE)
        return dist, hit, ix, iy, inside

    def scan(self) -> None:
        angles = self.theta + self.beam_angles
        dist, hit, ix, iy, inside = self.raycast(angles)
        dist = dist + np.random.normal(0, 0.01, dist.shape).astype(np.float32)

        free = inside & (self.steps[None, :] < (dist[:, None] - RES))
        self.logodds.flat[np.unique(iy[free] * self.w + ix[free])] -= 0.4

        hx = self.x + np.cos(angles[hit]) * dist[hit]
        hy = self.y + np.sin(angles[hit]) * dist[hit]
        hcx = ((hx - WORLD_ORIGIN[0]) / RES).astype(np.int32).clip(0, self.w - 1)
        hcy = ((hy - WORLD_ORIGIN[1]) / RES).astype(np.int32).clip(0, self.h - 1)
        self.logodds.flat[np.unique(hcy * self.w + hcx)] += 0.9
        np.clip(self.logodds, -4, 4, out=self.logodds)
        self.last_hits = np.stack([hx, hy], axis=1)

    # --- camera: flat material patches in view, close enough, and not hidden behind a rock
    def look(self) -> None:
        for mid, label, mx, my in MATERIALS:
            dx, dy = mx - self.x, my - self.y
            dist = math.hypot(dx, dy)
            bearing = math.atan2(dy, dx)
            off = (bearing - self.theta + math.pi) % (2 * math.pi) - math.pi
            if dist > CAMERA_RANGE or abs(off) > CAMERA_FOV / 2:
                continue
            seen_dist, hit, *_ = self.raycast(np.array([bearing], dtype=np.float32))
            if hit[0] and seen_dist[0] < dist - 0.2:
                continue  # a rock is in the way
            conf = round(min(0.97, 0.55 + 0.35 * (1 - dist / CAMERA_RANGE) + random.uniform(0, 0.08)), 2)
            prev = self.samples.get(mid)
            if prev is None or conf > prev["confidence"]:
                self.samples[mid] = {"id": f"cam-{mid}", "kind": "sample", "label": label, "confidence": conf,
                                     "x": round(mx + random.gauss(0, 0.06), 2),
                                     "y": round(my + random.gauss(0, 0.06), 2)}

    # --- lidar obstacles: clusters of occupied cells that aren't part of the arena edge
    def find_obstacles(self) -> None:
        c = 2  # cluster on a 0.1 m grid
        h, w = self.h // c, self.w // c
        occ = (self.logodds[: h * c, : w * c].reshape(h, c, w, c) > 0.5).any(axis=(1, 3))
        seen = np.zeros_like(occ)
        clusters = []
        for start in zip(*np.nonzero(occ)):
            if seen[start]:
                continue
            seen[start] = True
            cells, queue = [], deque([start])
            while queue:
                cy, cx = queue.popleft()
                cells.append((cy, cx))
                for ny in (cy - 1, cy, cy + 1):
                    for nx in (cx - 1, cx, cx + 1):
                        if 0 <= ny < h and 0 <= nx < w and occ[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            queue.append((ny, nx))
            clusters.append(np.array(cells))

        # Anything touching the edge of the mock area is the boundary wall, not an obstacle.
        # (The real robot needs a rule like this too, e.g. "ignore within 35 cm of the zone edge".)
        cres = RES * c
        x0, y0, x1, y1 = ARENA
        inset = 0.35
        visible, used = {}, set()
        for cells in clusters:
            if len(cells) < 4:
                continue  # a few stray cells: noise
            wx = WORLD_ORIGIN[0] + (cells[:, 1] + 0.5) * cres
            wy = WORLD_ORIGIN[1] + (cells[:, 0] + 0.5) * cres
            if (wx < x0 + inset).any() or (wx > x1 - inset).any() or (wy < y0 + inset).any() or (wy > y1 - inset).any():
                continue
            ox, oy = float(wx.mean()), float(wy.mean())
            size = float(max(wx.max() - wx.min(), wy.max() - wy.min()) + cres)
            # Keep ids stable: reuse the nearest obstacle we already know, if it's close.
            candidates = [o for o in self.known_obstacles.values() if o["id"] not in used]
            match = min(candidates, key=lambda o: math.dist((o["x"], o["y"]), (ox, oy)), default=None)
            if match is None or math.dist((match["x"], match["y"]), (ox, oy)) > 0.9:
                match = {"id": f"obs-{self.next_obstacle}", "kind": "obstacle", "label": "lidar_object",
                         "confidence": 1.0, "size_m": 0.0}
                self.next_obstacle += 1
                self.known_obstacles[match["id"]] = match
            match.update(x=round(ox, 2), y=round(oy, 2), size_m=round(size, 2))
            used.add(match["id"])
            visible[match["id"]] = match
        self.obstacles = visible  # only what the current map supports

    # --- message builders
    def mission_msg(self):
        return {"type": "mission", "name": "Survey Zone A", "zone": ZONE, "simulated": True}

    def pose_msg(self):
        return {"type": "pose", "t": time.time(), "x": round(self.x, 3), "y": round(self.y, 3),
                "theta": round(self.theta, 4)}

    def scan_msg(self):
        pts = self.last_hits[::2]
        return {"type": "scan", "t": time.time(),
                "points": [[round(float(a), 2), round(float(b), 2)] for a, b in pts]}

    def map_msg(self):
        """Encode the known part of the map (like slam_toolbox, the map grows as you explore)."""
        known = np.abs(self.logodds) > 0.5
        ys, xs = np.nonzero(known)
        if len(xs) == 0:
            return None
        pad = int(1.0 / RES)
        x0, x1 = max(xs.min() - pad, 0), min(xs.max() + pad + 1, self.w)
        y0, y1 = max(ys.min() - pad, 0), min(ys.max() + pad + 1, self.h)
        crop = self.logodds[y0:y1, x0:x1]
        img = np.full(crop.shape, 205, dtype=np.uint8)
        img[crop < -0.5] = 254
        img[crop > 0.5] = 0
        buf = io.BytesIO()
        Image.fromarray(np.flipud(img), "L").save(buf, format="PNG", optimize=True)
        return {"type": "map", "t": time.time(), "resolution": RES, "width": int(x1 - x0), "height": int(y1 - y0),
                "origin": [round(float(WORLD_ORIGIN[0] + x0 * RES), 3), round(float(WORLD_ORIGIN[1] + y0 * RES), 3)],
                "encoding": "png_base64", "data": base64.b64encode(buf.getvalue()).decode()}

    def detections_msg(self):
        return {"type": "detections", "t": time.time(),
                "items": list(self.samples.values()) + list(self.obstacles.values())}

    def analysis_msg(self):
        warnings = []
        for o in self.obstacles.values():
            dx, dy = o["x"] - self.x, o["y"] - self.y
            gap = math.hypot(dx, dy) - o["size_m"] / 2
            off = (math.atan2(dy, dx) - self.theta + math.pi) % (2 * math.pi) - math.pi
            if gap < 1.0 and abs(off) < math.radians(30):
                warnings.append(f"Obstacle about {max(gap, 0):.1f} m ahead")
        explored = float((self.logodds < -0.5).sum()) * RES * RES
        return {"type": "analysis", "t": time.time(), "explored_m2": round(explored, 1), "warnings": warnings}

    def status_msg(self):
        return {"type": "status", "t": time.time(), "mode": "paused" if self.paused else "auto",
                "battery_v": round(self.battery, 2), "cpu_temp_c": round(random.uniform(55, 64), 1)}

    def apply_command(self, cmd: dict) -> str:
        name, args = cmd.get("name"), cmd.get("args") or {}
        if name == "pause":
            self.paused = True
        elif name == "resume":
            self.paused = False
        elif name == "set_mode":
            self.paused = args.get("mode") == "paused"
        else:
            return f"ignored unknown command {name!r}"
        return f"ok: {name} {args}"


# ---------------------------------------------------------------- networking

async def stream(ws, sim: Sim, speed: float, stop_at: float):
    tick, dt = 0, 0.05  # 20 Hz sim
    loop = asyncio.get_running_loop()
    next_t = loop.time()
    while sim.t < stop_at:
        sim.step(dt)
        if tick % 4 == 0:
            sim.scan()
            await ws.send(json.dumps(sim.scan_msg()))
        if tick % 2 == 0:
            await ws.send(json.dumps(sim.pose_msg()))
        if tick % 5 == 0:
            sim.look()
        if tick % 20 == 0:
            m = sim.map_msg()
            if m:
                await ws.send(json.dumps(m))
            await ws.send(json.dumps(sim.detections_msg()))
            await ws.send(json.dumps(sim.status_msg()))
            await ws.send(json.dumps(sim.analysis_msg()))
        if tick % 20 == 10:
            sim.find_obstacles()  # ~10-20 ms; fine once a second
        tick += 1
        next_t += dt / speed
        await asyncio.sleep(max(0.0, next_t - loop.time()))


async def listen(ws, sim: Sim):
    async for raw in ws:
        msg = json.loads(raw)
        if msg.get("type") == "command":
            print("command:", sim.apply_command(msg))
        elif msg.get("type") == "error":
            print("server error:", msg.get("error"))


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="ws://localhost:8000/ws/robot")
    ap.add_argument("--token", default="dev-token")
    ap.add_argument("--name", default="Survey Zone A (simulator)")
    ap.add_argument("--robot-id", default="prowl-sim")
    ap.add_argument("--speed", type=float, default=1.0, help="simulation speed multiplier")
    ap.add_argument("--duration", type=float, default=0, help="sim seconds before sending 'end' (0 = forever)")
    args = ap.parse_args()

    sim = Sim()
    session_id = None
    stop_at = args.duration or float("inf")
    url = f"{args.url}?{urlencode({'token': args.token})}"
    while True:
        try:
            async with websockets.connect(url, max_size=None, open_timeout=10) as ws:
                await ws.send(json.dumps({"type": "hello", "robot_id": args.robot_id,
                                          "session_name": args.name, "resume_session_id": session_id}))
                reply = json.loads(await ws.recv())
                if reply.get("type") != "session":
                    print("unexpected reply:", reply)
                    return
                session_id = reply["session_id"]
                print(f"{'resumed' if reply['resumed'] else 'started'} session {session_id}")
                await ws.send(json.dumps(sim.mission_msg()))
                listener = asyncio.create_task(listen(ws, sim))
                try:
                    await stream(ws, sim, args.speed, stop_at)
                finally:
                    listener.cancel()
                await ws.send(json.dumps({"type": "end"}))
                print(f"done, sent end for session {session_id}")
                return
        except websockets.ConnectionClosed as exc:
            code = exc.rcvd.code if exc.rcvd else None
            if code == 4401:
                print("server rejected the token")
                return
            print(f"connection closed ({code}); reconnecting in 2 s")
        except OSError as exc:
            print(f"cannot connect ({exc}); retrying in 2 s")
        await asyncio.sleep(2)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
