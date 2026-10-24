"""Prowl backend: the robot streams in over /ws/robot, dashboards watch over
/ws/dashboard, finished runs are kept on disk and can be replayed over /ws/replay/<id>.

Run:  uvicorn app.main:app --host 0.0.0.0 --port 8000
"""
import asyncio
import hmac
import json
import logging
import time
from contextlib import asynccontextmanager
from typing import Optional

from fastapi import Depends, FastAPI, Header, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, ValidationError

from . import config
from .hub import Hub, dumps
from .protocol import Command, Hello, Report, Review, robot_message, short_validation_error
from .state import SessionState
from .storage import Storage, valid_session_id

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("prowl")

storage = Storage(config.DATA_DIR)
hub = Hub(storage, config.RESUME_GRACE_S)


@asynccontextmanager
async def lifespan(_app: FastAPI):
    config.warn_if_default_tokens()
    log.info("data directory: %s", config.DATA_DIR)
    yield
    await hub.shutdown()


app = FastAPI(title="Prowl Backend", version="0.2.0", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=config.CORS_ORIGINS,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ---------------------------------------------------------------- helpers

def token_ok(given: Optional[str], expected: str) -> bool:
    if not given:
        return False
    return hmac.compare_digest(given.encode(), expected.encode())


def token_from(ws_or_header: Optional[str], query_token: Optional[str]) -> Optional[str]:
    if query_token:
        return query_token
    if ws_or_header and ws_or_header.lower().startswith("bearer "):
        return ws_or_header[7:].strip()
    return None


def require_operator(
    token: Optional[str] = Query(default=None),
    authorization: Optional[str] = Header(default=None),
) -> None:
    if not token_ok(token_from(authorization, token), config.OPERATOR_TOKEN):
        raise HTTPException(status_code=401, detail="operator token required")


def session_or_404(sid: str) -> dict:
    meta = storage.get_session(sid) if valid_session_id(sid) else None
    if meta is None:
        raise HTTPException(status_code=404, detail="no such session")
    return meta


def describe(meta: dict) -> dict:
    live = hub.live.get(meta["id"])
    return live.describe() if live else {**meta, "live": False, "robot_connected": False}


def strip_map_data(latest: dict) -> dict:
    out = dict(latest)
    if "map" in out:
        out["map"] = {k: v for k, v in out["map"].items() if k != "data"}
    return out


# ---------------------------------------------------------------- REST

@app.get("/health")
def health():
    return {"status": "ok", "time": time.time(), "live_sessions": len(hub.live), "dashboards": len(hub.subs)}


@app.get("/api/live")
def live_sessions():
    return [s.describe() for s in hub.live.values()]


@app.get("/api/sessions")
def list_sessions(limit: int = Query(default=100, ge=1, le=1000)):
    return [describe(m) for m in storage.list_sessions(limit)]


@app.get("/api/sessions/{sid}")
def get_session(sid: str):
    meta = session_or_404(sid)
    live = hub.live.get(sid)
    state = live.state if live else hub.archived_state(sid)
    return {
        "session": describe(meta),
        "latest": strip_map_data(state.latest),  # fetch the image itself from map_url
        "trail": state.trail,
        "reviews": state.reviews,
        "reports": state.reports,
        "map_url": f"/api/sessions/{sid}/map.png",
        "log_url": f"/api/sessions/{sid}/log.jsonl",
    }


class SessionPatch(BaseModel):
    name: Optional[str] = Field(default=None, max_length=120)
    notes: Optional[str] = Field(default=None, max_length=5000)


@app.patch("/api/sessions/{sid}", dependencies=[Depends(require_operator)])
def patch_session(sid: str, body: SessionPatch):
    session_or_404(sid)
    storage.update_session(sid, **body.model_dump(exclude_none=True))
    if sid in hub.live:
        hub.live[sid].meta.update(body.model_dump(exclude_none=True))
    return describe(storage.get_session(sid))


@app.delete("/api/sessions/{sid}", dependencies=[Depends(require_operator)])
def delete_session(sid: str):
    session_or_404(sid)
    if sid in hub.live:
        raise HTTPException(status_code=409, detail="session is live; end it first")
    storage.delete_session(sid)
    hub.forget_archived(sid)
    return {"deleted": sid}


@app.get("/api/sessions/{sid}/map.png")
def session_map(sid: str):
    session_or_404(sid)
    path = storage.map_path(sid)
    if not path.exists():
        raise HTTPException(status_code=404, detail="no map received yet")
    return FileResponse(path, media_type="image/png", headers={"Cache-Control": "no-store"})


@app.get("/api/sessions/{sid}/log.jsonl")
def session_log(sid: str):
    session_or_404(sid)
    path = storage.log_path(sid)
    if not path.exists():
        raise HTTPException(status_code=404, detail="log is empty")
    return FileResponse(path, media_type="application/x-ndjson", filename=f"{sid}.jsonl")


# ---------------------------------------------------------------- robot websocket

@app.websocket("/ws/robot")
async def robot_ws(ws: WebSocket, token: Optional[str] = None):
    await ws.accept()
    if not token_ok(token_from(ws.headers.get("authorization"), token), config.ROBOT_TOKEN):
        await ws.close(code=4401, reason="bad robot token")
        return

    live = None
    try:
        while True:
            raw = await ws.receive_text()
            try:
                data = json.loads(raw)
                msg = robot_message.validate_python(data)
            except json.JSONDecodeError:
                await ws.send_text(dumps({"type": "error", "error": "not valid JSON"}))
                continue
            except ValidationError as exc:
                await ws.send_text(dumps({"type": "error", "error": short_validation_error(exc),
                                          "for_type": data.get("type") if isinstance(data, dict) else None}))
                continue

            if msg.type == "hello" or live is None:
                if live is not None:
                    await ws.send_text(dumps({"type": "error", "error": "already in a session; send 'end' first"}))
                    continue
                # Be lenient: a robot that skips "hello" just gets a default session.
                hello = msg if msg.type == "hello" else Hello(type="hello")
                live, resumed = await hub.robot_connected(ws, hello)
                await ws.send_text(dumps({"type": "session", "session_id": live.id,
                                          "name": live.meta.get("name"), "resumed": resumed}))
                if msg.type == "hello":
                    continue

            if msg.type == "end":
                hub.end_session(live, "robot ended the session")
                await ws.send_text(dumps({"type": "session_ended", "session_id": live.id}))
                live = None
                continue

            error = hub.ingest(live, msg.model_dump(exclude_none=True))
            if error:
                await ws.send_text(dumps({"type": "error", "error": error, "for_type": msg.type}))
    except WebSocketDisconnect:
        pass
    except Exception:
        log.exception("robot connection error")
    finally:
        if live is not None:
            await hub.robot_disconnected(live, ws)


# ---------------------------------------------------------------- dashboard websocket

@app.websocket("/ws/dashboard")
async def dashboard_ws(ws: WebSocket, session: Optional[str] = None, token: Optional[str] = None):
    await ws.accept()
    if session is not None and not valid_session_id(session):
        await ws.close(code=4400, reason="bad session id")
        return
    can_command = token_ok(token, config.OPERATOR_TOKEN)
    sub = hub.subscribe(ws, can_command, session)
    if token and not can_command:
        hub.reply(sub, {"type": "error", "error": "wrong operator token; connected read-only"})
    try:
        while True:
            try:
                data = json.loads(await ws.receive_text())
            except json.JSONDecodeError:
                hub.reply(sub, {"type": "error", "error": "not valid JSON"})
                continue
            mtype = data.get("type") if isinstance(data, dict) else None
            if mtype == "ping":
                hub.reply(sub, {"type": "pong", "ts": time.time()})
            elif mtype in ("review", "report"):
                try:
                    if mtype == "review":
                        error = hub.review(sub, Review.model_validate(data).model_dump())
                    else:
                        error = hub.report(sub, Report.model_validate(data).finding_id)
                except ValidationError as exc:
                    error = short_validation_error(exc)
                if error:
                    hub.reply(sub, {"type": "error", "error": error, "for_type": mtype})
            elif mtype == "command":
                try:
                    cmd = Command.model_validate(data)
                except ValidationError as exc:
                    hub.reply(sub, {"type": "error", "error": short_validation_error(exc)})
                    continue
                error = await hub.send_command(sub, cmd.model_dump())
                if error:
                    hub.reply(sub, {"type": "error", "error": error, "for_type": "command"})
            else:
                hub.reply(sub, {"type": "error", "error": f"unknown message type {mtype!r}"})
    except WebSocketDisconnect:
        pass
    except Exception:
        log.exception("dashboard connection error")
    finally:
        hub.unsubscribe(sub)


# ---------------------------------------------------------------- replay websocket

MAX_REPLAY_GAP_S = 3.0  # idle stretches longer than this are skipped


@app.websocket("/ws/replay/{sid}")
async def replay_ws(ws: WebSocket, sid: str, speed: float = 1.0, from_s: float = 0.0):
    """Streams a recorded session with its original timing (scaled by speed).
    The client may send {"type":"control","action":"pause"|"resume"|"speed","value":N}."""
    await ws.accept()
    meta = storage.get_session(sid) if valid_session_id(sid) else None
    if meta is None:
        await ws.close(code=4404, reason="no such session")
        return

    msgs = await asyncio.to_thread(lambda: list(storage.iter_log(sid)))
    state = {"speed": min(max(speed, 0.1), 50.0), "paused": False}

    async def read_controls():
        while True:
            try:
                data = json.loads(await ws.receive_text())
            except json.JSONDecodeError:
                continue
            if not isinstance(data, dict) or data.get("type") != "control":
                continue
            action = data.get("action")
            if action == "pause":
                state["paused"] = True
            elif action == "resume":
                state["paused"] = False
            elif action == "speed":
                try:
                    state["speed"] = min(max(float(data.get("value")), 0.1), 50.0)
                except (TypeError, ValueError):
                    pass

    controls = asyncio.create_task(read_controls())
    try:
        t0 = msgs[0]["ts"] if msgs else 0.0
        duration = (msgs[-1]["ts"] - t0) if msgs else 0.0
        upto, i = SessionState(), 0
        while i < len(msgs) and msgs[i]["ts"] - t0 < from_s:  # fast-forward
            upto.apply(msgs[i])
            i += 1

        await ws.send_text(dumps({"type": "replay_start", "session_id": sid, "duration_s": round(duration, 3),
                                  "from_s": from_s, "speed": state["speed"], "messages": len(msgs)}))
        await ws.send_text(dumps({"type": "snapshot", "session": {**describe(meta), "live": False},
                                  **upto.to_dict(), "following": False,
                                  "can_command": False, "replay": True}))
        prev_ts = msgs[i]["ts"] if i < len(msgs) else t0
        for msg in msgs[i:]:
            remaining = min(msg["ts"] - prev_ts, MAX_REPLAY_GAP_S)  # in recorded seconds
            prev_ts = msg["ts"]
            while remaining > 0 or state["paused"]:
                if controls.done():
                    return  # client went away
                if state["paused"]:
                    await asyncio.sleep(0.05)
                    continue
                step = min(remaining, 0.05 * state["speed"])
                await asyncio.sleep(step / state["speed"])
                remaining -= step
            await ws.send_text(dumps({**msg, "replay_t": round(msg["ts"] - t0, 3)}))
        await ws.send_text(dumps({"type": "replay_end", "session_id": sid}))
        await controls  # keep the socket open until the client closes it
    except (WebSocketDisconnect, RuntimeError, OSError):
        pass  # client closed the replay
    finally:
        controls.cancel()
        if controls.done() and not controls.cancelled():
            controls.exception()  # mark the disconnect as handled


# ---------------------------------------------------------------- pages

@app.get("/debug", include_in_schema=False)
def debug_page():
    return FileResponse(config.STATIC_DIR / "debug.html")


if (config.DASHBOARD_DIR / "index.html").exists():
    # A built React app copied into dashboard_dist/ is served at "/" (same origin = no CORS/HTTPS issues).
    app.mount("/", StaticFiles(directory=config.DASHBOARD_DIR, html=True), name="dashboard")
else:
    @app.get("/", include_in_schema=False)
    def root():
        return RedirectResponse("/debug")
