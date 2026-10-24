"""Message formats exchanged between robot, server and dashboard.

Every message is a JSON object with a "type" field. Extra fields are allowed and
passed through untouched, so the robot team can add things without a backend change.
See PROTOCOL.md for the human-readable version.
"""
from typing import Annotated, Literal, Optional, Union

from pydantic import BaseModel, ConfigDict, Field, TypeAdapter

Point = tuple[float, float]


class _Msg(BaseModel):
    model_config = ConfigDict(extra="allow")
    t: Optional[float] = None  # robot clock in seconds (optional; server adds its own "ts")


# ---------- robot -> server ----------

class Hello(_Msg):
    type: Literal["hello"]
    robot_id: str = Field(default="prowl-1", max_length=64)
    session_name: Optional[str] = Field(default=None, max_length=120)
    resume_session_id: Optional[str] = None


class Zone(BaseModel):
    """The survey zone in map-frame metres: bottom-left corner plus size."""
    x: float
    y: float
    width: float = Field(gt=0)
    height: float = Field(gt=0)


class Mission(_Msg):
    type: Literal["mission"]
    name: Optional[str] = Field(default=None, max_length=120)
    zone: Optional[Zone] = None
    simulated: bool = False  # true when the data comes from tools/fake_robot.py


class Pose(_Msg):
    type: Literal["pose"]
    x: float
    y: float
    theta: float  # radians, 0 = +x, counter-clockwise positive


class MapMsg(_Msg):
    type: Literal["map"]
    resolution: float = Field(gt=0)  # metres per cell
    width: int = Field(gt=0)
    height: int = Field(gt=0)
    origin: Point  # world (x, y) of the bottom-left corner of the grid
    encoding: Literal["png_base64"] = "png_base64"
    data: str  # base64 PNG, grayscale: 0 = occupied, 254 = free, 205 = unknown; row 0 = top (max y)


class Scan(_Msg):
    type: Literal["scan"]
    points: list[Point]  # lidar hits in the map frame, downsampled (~180 points is plenty)


class Detection(BaseModel):
    model_config = ConfigDict(extra="allow")
    id: str = Field(min_length=1, max_length=64)  # stable id from the robot, e.g. "cam-7" or "obs-2"
    kind: Literal["sample", "obstacle"] = "sample"  # camera finding vs. lidar obstacle
    label: str = Field(max_length=64)
    confidence: float = Field(default=1.0, ge=0, le=1)
    x: float
    y: float
    size_m: Optional[float] = None  # obstacles: rough width from the lidar outline


class Detections(_Msg):
    type: Literal["detections"]
    items: list[Detection]  # full current list (latest message wins)


class Analysis(_Msg):
    type: Literal["analysis"]
    explored_m2: Optional[float] = None
    warnings: list[str] = []


class Status(_Msg):
    type: Literal["status"]
    mode: Optional[str] = None  # e.g. "auto", "manual", "paused"
    battery_v: Optional[float] = None
    cpu_temp_c: Optional[float] = None


class End(_Msg):
    type: Literal["end"]


RobotMessage = Annotated[
    Union[Hello, Mission, Pose, MapMsg, Scan, Detections, Analysis, Status, End],
    Field(discriminator="type"),
]
robot_message = TypeAdapter(RobotMessage)

# Message types kept as "latest state" for dashboards that join mid-run.
STATE_TYPES = ("mission", "pose", "map", "scan", "detections", "analysis", "status")
# Message types written to the session log (scan is live-only: high rate, low replay value).
LOGGED_TYPES = ("mission", "pose", "map", "detections", "analysis", "status")


# ---------- dashboard -> server ----------

class Command(BaseModel):
    """Forwarded to the robot as-is. Names are up to the robot, e.g. "pause"."""
    model_config = ConfigDict(extra="allow")
    type: Literal["command"]
    name: str = Field(min_length=1, max_length=64)
    args: dict = {}


class Review(BaseModel):
    """A reviewer changes a finding's status and/or note. Shared with every dashboard."""
    type: Literal["review"]
    finding_id: str = Field(min_length=1, max_length=16)  # the display code, e.g. "P-001"
    status: Optional[str] = Field(default=None, max_length=40)
    note: Optional[str] = Field(default=None, max_length=2000)


class Report(BaseModel):
    """Queue a report: freezes the finding's current status and note into the mission log."""
    type: Literal["report"]
    finding_id: str = Field(min_length=1, max_length=16)


def short_validation_error(exc) -> str:
    """One-line version of a pydantic ValidationError for sending back over the socket."""
    parts = []
    for err in exc.errors()[:3]:
        loc = ".".join(str(p) for p in err.get("loc", ()))
        parts.append(f"{loc}: {err.get('msg')}")
    return "; ".join(parts)
