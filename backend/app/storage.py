"""Session storage: a small SQLite table for metadata, plus one folder per session:

    data/sessions/<id>/log.jsonl   every logged message, one JSON object per line
    data/sessions/<id>/map.png     the most recent map image
"""
import json
import re
import secrets
import shutil
import sqlite3
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from .state import SessionState, rebuild

SESSION_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


def new_session_id() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(2)


def valid_session_id(sid: str) -> bool:
    return bool(SESSION_ID_RE.match(sid or ""))


class Storage:
    def __init__(self, data_dir: Path):
        self.data_dir = Path(data_dir)
        self.sessions_dir = self.data_dir / "sessions"
        self.sessions_dir.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._db = sqlite3.connect(self.data_dir / "prowl.db", check_same_thread=False)
        self._db.row_factory = sqlite3.Row
        self._db.execute(
            """CREATE TABLE IF NOT EXISTS sessions (
                   id TEXT PRIMARY KEY,
                   name TEXT,
                   robot_id TEXT,
                   started_at REAL NOT NULL,
                   ended_at REAL,
                   msg_count INTEGER NOT NULL DEFAULT 0,
                   notes TEXT
               )"""
        )
        self._db.commit()
        self._logs: dict[str, object] = {}  # open log file handles

    # ----- metadata -----

    def create_session(self, sid: str, name: str, robot_id: str) -> dict:
        (self.sessions_dir / sid).mkdir(parents=True, exist_ok=True)
        with self._lock:
            self._db.execute(
                "INSERT INTO sessions (id, name, robot_id, started_at) VALUES (?, ?, ?, ?)",
                (sid, name, robot_id, time.time()),
            )
            self._db.commit()
        return self.get_session(sid)

    def get_session(self, sid: str) -> Optional[dict]:
        with self._lock:
            row = self._db.execute("SELECT * FROM sessions WHERE id = ?", (sid,)).fetchone()
        return dict(row) if row else None

    def list_sessions(self, limit: int = 100) -> list[dict]:
        with self._lock:
            rows = self._db.execute(
                "SELECT * FROM sessions ORDER BY started_at DESC LIMIT ?", (limit,)
            ).fetchall()
        return [dict(r) for r in rows]

    def update_session(self, sid: str, **fields) -> None:
        allowed = {"name", "notes", "ended_at", "msg_count"}
        fields = {k: v for k, v in fields.items() if k in allowed}
        if not fields:
            return
        sets = ", ".join(f"{k} = ?" for k in fields)
        with self._lock:
            self._db.execute(f"UPDATE sessions SET {sets} WHERE id = ?", (*fields.values(), sid))
            self._db.commit()

    def delete_session(self, sid: str) -> None:
        self.close_log(sid)
        with self._lock:
            self._db.execute("DELETE FROM sessions WHERE id = ?", (sid,))
            self._db.commit()
        shutil.rmtree(self.sessions_dir / sid, ignore_errors=True)

    # ----- files -----

    def log_path(self, sid: str) -> Path:
        return self.sessions_dir / sid / "log.jsonl"

    def map_path(self, sid: str) -> Path:
        return self.sessions_dir / sid / "map.png"

    def append_log(self, sid: str, line: str) -> None:
        fh = self._logs.get(sid)
        if fh is None:
            fh = open(self.log_path(sid), "a", encoding="utf-8")
            self._logs[sid] = fh
        fh.write(line + "\n")
        fh.flush()

    def close_log(self, sid: str) -> None:
        fh = self._logs.pop(sid, None)
        if fh:
            fh.close()

    def write_map_png(self, sid: str, png: bytes) -> None:
        tmp = self.map_path(sid).with_suffix(".tmp")
        tmp.write_bytes(png)
        tmp.replace(self.map_path(sid))  # atomic, so readers never see half a file

    def iter_log(self, sid: str):
        """Yield logged messages in order, skipping any corrupt line (e.g. cut off by a crash)."""
        path = self.log_path(sid)
        if not path.exists():
            return
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                try:
                    yield json.loads(line)
                except json.JSONDecodeError:
                    continue

    def load_state(self, sid: str, until_ts: Optional[float] = None) -> SessionState:
        """Rebuild a session's state from its log, optionally only up to a timestamp."""
        return rebuild(self.iter_log(sid), until_ts)
