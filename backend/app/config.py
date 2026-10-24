"""Settings, read from environment variables (or a .env file next to this project)."""
import logging
import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
log = logging.getLogger("prowl")


def _load_dotenv(path: Path) -> None:
    """Tiny .env reader (KEY=VALUE per line) so we don't need python-dotenv."""
    if not path.exists():
        return
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


_load_dotenv(BASE_DIR / ".env")

DATA_DIR = Path(os.environ.get("DATA_DIR", BASE_DIR / "data"))

# The robot must present ROBOT_TOKEN to stream data in.
# Dashboards can watch without a token; OPERATOR_TOKEN is needed to send commands
# to the robot or delete sessions.
ROBOT_TOKEN = os.environ.get("ROBOT_TOKEN", "dev-token")
OPERATOR_TOKEN = os.environ.get("OPERATOR_TOKEN", ROBOT_TOKEN)

# Comma-separated list of allowed browser origins for the REST API ("*" = any).
CORS_ORIGINS = [o.strip() for o in os.environ.get("CORS_ORIGINS", "*").split(",") if o.strip()]

# How long a session stays "live" after the robot drops, so it can reconnect and resume.
RESUME_GRACE_S = float(os.environ.get("RESUME_GRACE_S", "60"))

# The built React dashboard is served at "/" (same origin = no CORS/HTTPS issues).
# Default: backend/dashboard_dist if you copied a build there, else the repo's frontend/dist.
def _default_dashboard_dir() -> Path:
    for candidate in (BASE_DIR / "dashboard_dist", BASE_DIR.parent / "frontend" / "dist"):
        if (candidate / "index.html").exists():
            return candidate
    return BASE_DIR / "dashboard_dist"


DASHBOARD_DIR = Path(os.environ.get("DASHBOARD_DIR") or _default_dashboard_dir())

STATIC_DIR = BASE_DIR / "static"


def warn_if_default_tokens() -> None:
    if ROBOT_TOKEN == "dev-token":
        log.warning("ROBOT_TOKEN is the default 'dev-token'. Set it in .env before exposing the server.")
