# Prowl backend

FastAPI server between the rover and the dashboard:

- **`/ws/robot`**: the Pi streams pose, the LiDAR map, scans, findings and status in.
  Findings are camera samples (`P-001`, …) and LiDAR obstacles (`O-001`, …); the server gives
  each one a short code the first time it's seen.
- **`/ws/dashboard`**: dashboards get a snapshot, then every update live. Reviewer statuses,
  notes and queued reports are saved on the server and shown on every open dashboard. That
  works during a run and after it ends.
- **Sessions**: every run is saved (SQLite + a JSONL log + the latest map image) and can be
  replayed with its original timing over **`/ws/replay/<id>`**. That's your backup if the rover
  dies on stage.
- **The dashboard**: the server serves the built React app from `../frontend/dist` at `/`.
- **`tools/fake_robot.py`**: a simulated rover in a mock Moon area, with simulated LiDAR and
  camera, so the dashboard works without hardware. Its data is always labeled as simulator data.
- **`/debug`**: a plain-JS view of the raw data, for testing.

The message formats are in **[PROTOCOL.md](PROTOCOL.md)**. That's the contract for whoever
writes the Pi bridge node.

```
app/
  main.py       routes and WebSocket endpoints
  hub.py        live sessions, fan-out to dashboards, commands, reviews
  state.py      session state, built the same way live and from logs
  storage.py    SQLite metadata + per-session files in data/
  protocol.py   message schemas (pydantic)
  config.py     settings from env / .env
static/debug.html
tools/fake_robot.py
deploy/setup_ec2.sh   one-command install on the EC2 instance
```

## Run everything on your laptop

Terminal 1, the backend (from `backend/`):
```bash
python -m venv .venv
.venv\Scripts\activate            # Windows. On Mac/Linux: source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --reload --port 8000
```
Terminal 2, the fake rover (from `backend/`, same venv):
```bash
python tools/fake_robot.py --speed 2
```
Terminal 3, the dashboard (from `frontend/`):
```bash
npm install
npm run dev
```
Open http://localhost:5173. The header should say "Simulator connected — live". Without a
backend, the dashboard shows its built-in demo data instead.

API docs: http://localhost:8000/docs. Raw data view: http://localhost:8000/debug (paste
`dev-token` into "operator token" to enable Pause/Resume).

## Put it on the EC2 instance (Amazon Linux 2023)

**1. Stop the old echo test** if it's still running (Ctrl+C in its terminal or tmux window).
It's holding port 8000.

**2. Get the repo and run the setup script.** On the instance:
```bash
sudo dnf install -y git
git clone https://github.com/matthewthomasbeck/Prowl.git ~/prowl
cd ~/prowl/backend
bash deploy/setup_ec2.sh
```
The script:
- installs Python 3.11 next to Amazon Linux's built-in 3.9 (it leaves `/usr/bin/python3` alone,
  because the OS depends on it), makes `.venv` and installs the packages
- installs Node.js 22 and builds the dashboard
- creates `.env` with random tokens and prints them
- installs a `prowl` systemd service that runs as `ec2-user`, starts on boot and restarts if it
  crashes
- checks the server is answering, then prints your URLs

**3. Test it.** From the repo folder on your laptop, run the command the script printed:
```bash
python backend/tools/fake_robot.py --url ws://<public-ip>:8000/ws/robot --token <ROBOT_TOKEN>
```
Then open `http://<public-ip>:8000/`.

**Day-to-day:**
```bash
journalctl -u prowl -f              # live logs
sudo systemctl restart prowl        # restart
cat ~/prowl/backend/.env            # see the tokens again
```
**Updating:** `cd ~/prowl && git pull && cd backend && bash deploy/setup_ec2.sh`. It keeps
`.env` and the recorded runs in `data/`.

To point `npm run dev` on your laptop at the EC2 server instead of a local backend, create
`frontend/.env.local` containing `PROWL_BACKEND=http://<public-ip>:8000`.

## Things that will bite you

- **The public IP changes when you stop/start the instance.** Attach an Elastic IP before the
  event, or the Pi's config breaks.
- **Serve the dashboard from the backend for the demo** (`http://<ip>:8000/`), not from an HTTPS
  host like Vercel. Browsers block an HTTPS page from opening a plain `ws://` connection.
- **Venue Wi-Fi.** The Pi should reconnect and resume its session (copy the loop in
  `tools/fake_robot.py`). Record a good run before the demo so you have a replay.
- **Reviews are open to anyone who can load the dashboard.** Driving commands to the rover need
  the operator token. That's fine for a hackathon; don't leave it running afterwards.

## Settings (`.env`)

| name | default | meaning |
|---|---|---|
| `ROBOT_TOKEN` | `dev-token` | the robot must send this to stream data |
| `OPERATOR_TOKEN` | same as `ROBOT_TOKEN` | needed to send commands, rename or delete sessions |
| `CORS_ORIGINS` | `*` | comma-separated browser origins allowed to call the REST API |
| `RESUME_GRACE_S` | `60` | how long a session waits for the robot to reconnect |
| `DATA_DIR` | `./data` | where sessions are stored |
| `DASHBOARD_DIR` | `../frontend/dist` | built dashboard to serve at `/` |
