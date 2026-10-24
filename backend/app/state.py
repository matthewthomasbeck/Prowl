"""Session state: everything a dashboard needs to draw one run.

The same class is filled live (as messages arrive) and rebuilt from a session log
(for replays, resumed sessions and finished runs), so both always agree.
"""
from typing import Optional

from .protocol import STATE_TYPES

TRAIL_MIN_STEP_M = 0.05
TRAIL_MAX_POINTS = 5000
CODE_PREFIX = {"sample": "P", "obstacle": "O"}


def add_to_trail(trail: list, x: float, y: float) -> bool:
    """Append (x, y) if the robot moved far enough. Returns True if a point was added."""
    if trail:
        lx, ly = trail[-1]
        if (x - lx) ** 2 + (y - ly) ** 2 < TRAIL_MIN_STEP_M ** 2:
            return False
    trail.append([round(x, 3), round(y, 3)])
    if len(trail) > TRAIL_MAX_POINTS:
        del trail[1::2]  # thin out instead of dropping the start of the run
    return True


class SessionState:
    def __init__(self):
        self.latest: dict = {}  # newest message of each STATE_TYPES type
        self.trail: list = []  # where the robot has been, thinned to 5 cm steps
        self.codes: dict[str, dict] = {}  # robot detection id -> {"code": "P-001", "first_seen": ts}
        self.counters = {"sample": 0, "obstacle": 0}
        self.reviews: dict[str, dict] = {}  # code -> {"status", "note", "ts"} set by reviewers
        self.reports: list[dict] = []  # queued reports, oldest first

    # ----- live only: decide display codes before the message is stored and forwarded -----

    def assign_codes(self, msg: dict, now: float) -> None:
        """Give each detection a short display code (P-001 for samples, O-001 for obstacles)
        the first time its robot id is seen, and stamp when that was. Codes never change."""
        for item in msg.get("items", []):
            entry = self.codes.get(item["id"])
            if entry is None:
                kind = item.get("kind", "sample")
                self.counters[kind] = self.counters.get(kind, 0) + 1
                entry = {"code": f"{CODE_PREFIX.get(kind, 'X')}-{self.counters[kind]:03d}", "first_seen": now}
                self.codes[item["id"]] = entry
            item["code"] = entry["code"]
            item["first_seen"] = entry["first_seen"]

    # ----- live and replayed: fold one stamped message into the state -----

    def apply(self, msg: dict) -> None:
        mtype = msg.get("type")
        if mtype == "pose":
            add_to_trail(self.trail, msg["x"], msg["y"])
        elif mtype == "detections":
            self._learn_codes(msg.get("items", []))
        elif mtype == "review":
            entry = self.reviews.setdefault(msg["finding_id"], {})
            for key in ("status", "note"):
                if msg.get(key) is not None:
                    entry[key] = msg[key]
            entry["ts"] = msg.get("ts")
        elif mtype == "report":
            self.reports.append({k: msg.get(k) for k in ("finding_id", "status", "note", "ts")})
        if mtype in STATE_TYPES:
            self.latest[mtype] = msg

    def _learn_codes(self, items: list) -> None:
        """When rebuilding from a log, pick the codes back up from the stored messages."""
        for item in items:
            code = item.get("code")
            if not code or item.get("id") in self.codes:
                continue
            self.codes[item["id"]] = {"code": code, "first_seen": item.get("first_seen")}
            kind = item.get("kind", "sample")
            try:
                self.counters[kind] = max(self.counters.get(kind, 0), int(code.split("-")[1]))
            except (IndexError, ValueError):
                pass

    def has_code(self, code: str) -> bool:
        return any(entry["code"] == code for entry in self.codes.values())

    def finding(self, code: str) -> Optional[dict]:
        for item in (self.latest.get("detections") or {}).get("items", []):
            if item.get("code") == code:
                return item
        return None

    def to_dict(self) -> dict:
        return {"latest": self.latest, "trail": self.trail, "reviews": self.reviews, "reports": self.reports}


def rebuild(messages, until_ts: Optional[float] = None) -> SessionState:
    state = SessionState()
    for msg in messages:
        if until_ts is not None and msg.get("ts", 0) > until_ts:
            break
        state.apply(msg)
    return state
