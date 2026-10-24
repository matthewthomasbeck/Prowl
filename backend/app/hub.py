"""Live routing: robot connections in, dashboard subscribers out.

A dashboard either follows the newest live session (the default, so one open
browser tab keeps working across robot restarts) or pins itself to one session id.
"""
import asyncio
import base64
import binascii
import json
import logging
import time
from typing import Optional

from fastapi import WebSocket

from .protocol import LOGGED_TYPES, Hello
from .state import SessionState
from .storage import Storage, new_session_id, valid_session_id

log = logging.getLogger("prowl.hub")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
QUEUE_SIZE = 200


def dumps(obj) -> str:
    return json.dumps(obj, separators=(",", ":"))


class Subscriber:
    """One dashboard connection. Messages go through a queue so a slow browser
    never holds up the robot."""

    def __init__(self, ws: WebSocket, can_command: bool, pin: Optional[str]):
        self.ws = ws
        self.can_command = can_command
        self.pin = pin  # None = follow the newest live session
        self.session_id: Optional[str] = pin
        self.queue: asyncio.Queue = asyncio.Queue(maxsize=QUEUE_SIZE)
        self.task = asyncio.create_task(self._sender())

    async def _sender(self):
        try:
            while True:
                await self.ws.send_text(await self.queue.get())
        except Exception:
            pass  # socket closed; the endpoint cleans up

    def close(self):
        self.task.cancel()


class LiveSession:
    def __init__(self, meta: dict, state: Optional[SessionState] = None):
        self.id: str = meta["id"]
        self.meta = meta
        self.state = state or SessionState()
        self.msg_count: int = meta.get("msg_count") or 0
        self.robot_ws: Optional[WebSocket] = None
        self.connected_at = time.time()
        self.grace_task: Optional[asyncio.Task] = None

    def describe(self) -> dict:
        return {
            **self.meta,
            "ended_at": None,
            "msg_count": self.msg_count,
            "live": True,
            "robot_connected": self.robot_ws is not None,
        }


class Hub:
    def __init__(self, storage: Storage, grace_s: float):
        self.storage = storage
        self.grace_s = grace_s
        self.live: dict[str, LiveSession] = {}
        self.subs: set[Subscriber] = set()
        self.newest_id: Optional[str] = None
        self.archive: dict[str, SessionState] = {}  # finished runs loaded from disk, so reviews can continue

    # ------------------------------------------------------------------ robot side

    async def robot_connected(self, ws: WebSocket, hello: Hello) -> tuple[LiveSession, bool]:
        live, resumed = None, False
        sid = hello.resume_session_id
        if sid and valid_session_id(sid):
            live = self.live.get(sid)
            if live is None and self.storage.get_session(sid):
                # Reopen a session that had already ended: rebuild its state from the log.
                self.storage.update_session(sid, ended_at=None)
                self.archive.pop(sid, None)
                live = LiveSession(self.storage.get_session(sid), self.storage.load_state(sid))
                self.live[sid] = live
            resumed = live is not None

        if live is None:
            sid = new_session_id()
            name = hello.session_name or f"{hello.robot_id} {time.strftime('%b %d %H:%M')}"
            live = LiveSession(self.storage.create_session(sid, name, hello.robot_id))
            self.live[sid] = live

        if live.grace_task:
            live.grace_task.cancel()
            live.grace_task = None

        old_ws, live.robot_ws = live.robot_ws, ws
        live.connected_at = time.time()
        if old_ws is not None and old_ws is not ws:
            try:  # a stale connection from the same robot (e.g. Wi-Fi flapped)
                await old_ws.close(code=4409, reason="replaced by a newer connection")
            except Exception:
                pass

        log.info("robot %s %s session %s", hello.robot_id, "resumed" if resumed else "started", live.id)
        self._broadcast(live.id, {"type": "robot_connected", "session_id": live.id, "resumed": resumed})
        self._set_newest(live.id)
        return live, resumed

    def ingest(self, live: LiveSession, msg: dict) -> Optional[str]:
        """Store and fan out one validated robot message. Returns an error string or None."""
        mtype = msg["type"]
        now = round(time.time(), 3)
        if mtype == "map":
            try:
                png = base64.b64decode(msg["data"], validate=True)
            except (binascii.Error, ValueError):
                return "map.data is not valid base64"
            if not png.startswith(PNG_MAGIC):
                return "map.data is not a PNG image"
            self.storage.write_map_png(live.id, png)
        elif mtype == "detections":
            live.state.assign_codes(msg, now)

        msg["ts"] = now
        msg["session_id"] = live.id
        live.state.apply(msg)
        live.msg_count += 1
        text = dumps(msg)
        if mtype in LOGGED_TYPES:
            self.storage.append_log(live.id, text)
        self._broadcast_text(live.id, text)
        return None

    async def robot_disconnected(self, live: LiveSession, ws: WebSocket) -> None:
        if live.robot_ws is not ws:
            return  # this connection was already replaced
        live.robot_ws = None
        if self.live.get(live.id) is not live:
            return  # session already ended (robot sent "end")
        self.storage.update_session(live.id, msg_count=live.msg_count)
        self._broadcast(live.id, {"type": "robot_disconnected", "session_id": live.id,
                                  "resume_window_s": self.grace_s})
        live.grace_task = asyncio.create_task(self._end_after_grace(live))

    async def _end_after_grace(self, live: LiveSession) -> None:
        try:
            await asyncio.sleep(self.grace_s)
        except asyncio.CancelledError:
            return
        live.grace_task = None
        if live.robot_ws is None and self.live.get(live.id) is live:
            self.end_session(live, "robot did not reconnect")

    def end_session(self, live: LiveSession, reason: str) -> None:
        if live.grace_task:
            live.grace_task.cancel()
            live.grace_task = None
        self.live.pop(live.id, None)
        self.storage.close_log(live.id)
        self.storage.update_session(live.id, ended_at=time.time(), msg_count=live.msg_count)
        log.info("session %s ended (%s)", live.id, reason)
        self._broadcast(live.id, {"type": "session_ended", "session_id": live.id, "reason": reason})
        # Followers stay on the finished run (showing its final state) until another robot connects.
        if self.newest_id == live.id:
            remaining = sorted(self.live.values(), key=lambda s: s.connected_at)
            self.newest_id = None
            if remaining:
                self._set_newest(remaining[-1].id)

    async def shutdown(self) -> None:
        for live in list(self.live.values()):
            self.end_session(live, "server shutting down")
        for sub in list(self.subs):
            sub.close()

    # -------------------------------------------------------------- dashboard side

    def subscribe(self, ws: WebSocket, can_command: bool, pin: Optional[str]) -> Subscriber:
        sub = Subscriber(ws, can_command, pin)
        if pin is None:
            # Follow the newest live run; if nothing is live, show the most recent finished run.
            sub.session_id = self.newest_id or next((s["id"] for s in self.storage.list_sessions(1)), None)
        self.subs.add(sub)
        self._deliver(sub, dumps(self.snapshot(sub)))
        return sub

    def reply(self, sub: Subscriber, obj: dict) -> None:
        """Send something to one dashboard (goes through its queue like everything else)."""
        self._deliver(sub, dumps(obj))

    def unsubscribe(self, sub: Subscriber) -> None:
        self.subs.discard(sub)
        sub.close()

    def snapshot(self, sub: Subscriber) -> dict:
        sid = sub.session_id
        live = self.live.get(sid) if sid else None
        session, state = None, SessionState()
        if live:
            session, state = live.describe(), live.state
        elif sid:
            meta = self.storage.get_session(sid)
            if meta:  # a finished run: show its final state
                state = self.archived_state(sid)
                session = {**meta, "live": False, "robot_connected": False}
        return {
            "type": "snapshot",
            "session": session,
            **state.to_dict(),
            "following": sub.pin is None,
            "can_command": sub.can_command,
        }

    async def send_command(self, sub: Subscriber, cmd: dict) -> Optional[str]:
        if not sub.can_command:
            return "sending commands needs the operator token (?token=...)"
        live = self._live_for(sub)
        if live is None or live.robot_ws is None:
            return "robot is not connected"
        cmd["ts"] = round(time.time(), 3)
        cmd["session_id"] = live.id
        text = dumps(cmd)
        try:
            await live.robot_ws.send_text(text)
        except Exception:
            return "could not reach the robot"
        self.storage.append_log(live.id, text)  # commands show up in replays too
        self._broadcast_text(live.id, text)
        return None

    # Reviews and reports work during a run and after it ends, since reviewing often happens afterwards.

    def review(self, sub: Subscriber, data: dict) -> Optional[str]:
        """A reviewer changed a finding's status or note. Saved and shown on every dashboard."""
        sid, state = self._review_target(sub)
        if state is None:
            return "no session to review yet"
        if not state.has_code(data["finding_id"]):
            return f"no finding {data['finding_id']} in this session"
        if data.get("status") is None and data.get("note") is None:
            return "send a status, a note, or both"
        msg = {"type": "review", "finding_id": data["finding_id"], "ts": round(time.time(), 3), "session_id": sid}
        for key in ("status", "note"):
            if data.get(key) is not None:
                msg[key] = data[key]
        self._record(sid, state, msg)
        return None

    def report(self, sub: Subscriber, finding_id: str) -> Optional[str]:
        """Queue a report: the finding's current status and note, frozen into the mission log."""
        sid, state = self._review_target(sub)
        if state is None:
            return "no session to report on yet"
        item = state.finding(finding_id)
        if item is None and not state.has_code(finding_id):
            return f"no finding {finding_id} in this session"
        review = state.reviews.get(finding_id, {})
        default = "New" if (item or {}).get("kind") == "obstacle" else "Needs analysis"
        msg = {"type": "report", "finding_id": finding_id, "status": review.get("status", default),
               "note": review.get("note", ""), "ts": round(time.time(), 3), "session_id": sid}
        self._record(sid, state, msg)
        return None

    def forget_archived(self, sid: str) -> None:
        self.archive.pop(sid, None)

    # ------------------------------------------------------------------ internals

    def _live_for(self, sub: Subscriber) -> Optional[LiveSession]:
        return self.live.get(sub.session_id) if sub.session_id else None

    def archived_state(self, sid: str) -> SessionState:
        state = self.archive.get(sid)
        if state is None:
            state = self.archive[sid] = self.storage.load_state(sid)
        return state

    def _review_target(self, sub: Subscriber) -> tuple[Optional[str], Optional[SessionState]]:
        sid = sub.session_id
        if not sid:
            return None, None
        live = self.live.get(sid)
        if live:
            return sid, live.state
        if self.storage.get_session(sid) is None:
            return None, None
        return sid, self.archived_state(sid)

    def _record(self, sid: str, state: SessionState, msg: dict) -> None:
        state.apply(msg)
        text = dumps(msg)
        self.storage.append_log(sid, text)
        if sid not in self.live:
            self.storage.close_log(sid)  # finished run: don't keep its file open
        self._broadcast_text(sid, text)

    def _set_newest(self, sid: str) -> None:
        self.newest_id = sid
        for sub in self.subs:
            if sub.pin is None and sub.session_id != sid:
                sub.session_id = sid
                self._deliver(sub, dumps(self.snapshot(sub)), clear=True)

    def _broadcast(self, sid: str, event: dict) -> None:
        self._broadcast_text(sid, dumps(event))

    def _broadcast_text(self, sid: str, text: str) -> None:
        for sub in self.subs:
            if sub.session_id == sid:
                self._deliver(sub, text)

    def _deliver(self, sub: Subscriber, text: str, clear: bool = False) -> None:
        if clear:
            _drain(sub.queue)
        try:
            sub.queue.put_nowait(text)
        except asyncio.QueueFull:
            # The browser fell behind. Skip the backlog and resync it with a fresh snapshot.
            _drain(sub.queue)
            sub.queue.put_nowait(dumps(self.snapshot(sub)))


def _drain(queue: asyncio.Queue) -> None:
    while not queue.empty():
        queue.get_nowait()
