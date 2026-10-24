#!/usr/bin/env bash
# One-command setup for the EC2 instance (Amazon Linux 2023; also works on Ubuntu).
#
#   bash deploy/setup_ec2.sh               install everything, build the dashboard, run it as a service
#   bash deploy/setup_ec2.sh --no-service  set up, but don't install the systemd service
#   bash deploy/setup_ec2.sh --no-dashboard  skip building the React dashboard (../frontend)
#   PYTHON=python3.12 bash deploy/setup_ec2.sh   force a specific Python
#
# Safe to re-run after pulling new code: it reinstalls packages, rebuilds the dashboard and restarts.
set -euo pipefail

cd "$(dirname "$0")/.."
APP_DIR="$(pwd)"
RUN_USER="$(id -un)"
SERVICE=prowl
UNIT_PATH="${UNIT_PATH:-/etc/systemd/system/${SERVICE}.service}"
PORT="${PORT:-8000}"
INSTALL_SERVICE=1
BUILD_DASHBOARD=1
for arg in "$@"; do
  case "$arg" in
    --no-service) INSTALL_SERVICE=0 ;;
    --no-dashboard) BUILD_DASHBOARD=0 ;;
  esac
done

say() { printf '\n==> %s\n' "$*"; }

py_at_least() {  # py_at_least <python> <minor>
  "$1" -c "import sys; sys.exit(0 if sys.version_info >= (3, $2) else 1)" 2>/dev/null
}

pick_python() {
  if [[ -n "${PYTHON:-}" ]]; then echo "$PYTHON"; return; fi
  local v
  for v in 3.13 3.12 3.11 3.10; do
    if command -v "python$v" >/dev/null 2>&1 && py_at_least "python$v" 10; then echo "python$v"; return; fi
  done
}

# ------------------------------------------------------------------ Python
say "Choosing Python"
PY="$(pick_python || true)"
if [[ -z "$PY" ]] && command -v dnf >/dev/null 2>&1; then
  echo "Amazon Linux's built-in python3 is 3.9, so installing python3.11 alongside it..."
  sudo dnf install -y python3.11 python3.11-pip || echo "(dnf install failed; will try the built-in python3)"
  PY="$(pick_python || true)"
fi
if [[ -z "$PY" ]]; then
  if command -v python3 >/dev/null 2>&1 && py_at_least python3 9; then
    PY=python3
    echo "Falling back to $(python3 --version). That works, but 3.11+ is better."
  else
    echo "Need Python 3.9 or newer." >&2
    exit 1
  fi
fi
PY_VER="$("$PY" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
echo "Using $(command -v "$PY") (Python $PY_VER)"

# ------------------------------------------------------------------ virtualenv
say "Setting up the virtual environment"
if [[ -x .venv/bin/python ]]; then
  VENV_VER="$(.venv/bin/python -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null || echo broken)"
  if [[ "$VENV_VER" != "$PY_VER" ]]; then
    echo "Existing .venv uses Python $VENV_VER; rebuilding it with $PY_VER."
    rm -rf .venv
  fi
fi
if [[ ! -x .venv/bin/python ]]; then
  if ! "$PY" -m venv .venv 2>/dev/null; then
    rm -rf .venv
    if command -v apt-get >/dev/null 2>&1; then
      sudo apt-get install -y "python${PY_VER}-venv"
      "$PY" -m venv .venv
    else
      echo "This Python has no bundled pip; bootstrapping it."
      "$PY" -m venv --without-pip .venv
      curl -fsSL https://bootstrap.pypa.io/get-pip.py | .venv/bin/python - -q
    fi
  fi
fi

say "Installing Python packages (a minute or two on a small instance)"
.venv/bin/python -m pip install -q --disable-pip-version-check --upgrade pip
.venv/bin/python -m pip install -q --disable-pip-version-check -r requirements.txt
echo "Done."

# ------------------------------------------------------------------ dashboard
# The backend serves the built React dashboard from ../frontend/dist at http://<ip>:8000/
node_ok() { command -v node >/dev/null 2>&1 && node -e 'process.exit(+process.versions.node.split(".")[0] >= 20 ? 0 : 1)' 2>/dev/null; }

if [[ $BUILD_DASHBOARD == 1 && -f ../frontend/package.json ]]; then
  say "Building the dashboard (../frontend)"
  if ! node_ok && command -v dnf >/dev/null 2>&1; then
    echo "Installing Node.js 22..."
    sudo dnf install -y nodejs22 nodejs22-npm || true
  fi
  if node_ok && command -v npm >/dev/null 2>&1; then
    (cd ../frontend && npm ci --no-audit --no-fund --loglevel=error && npm run build --silent)
    echo "Built ../frontend/dist"
  else
    echo "Node.js 20+ isn't available, so the dashboard wasn't built. The API still works;"
    echo "build it on your laptop (npm run build in frontend/) and copy frontend/dist up instead."
  fi
fi

# ------------------------------------------------------------------ tokens
say "Tokens"
if [[ ! -f .env ]]; then
  ROBOT_TOKEN="$(.venv/bin/python -c 'import secrets; print(secrets.token_hex(12))')"
  OPERATOR_TOKEN="$(.venv/bin/python -c 'import secrets; print(secrets.token_hex(12))')"
  cat > .env <<ENV
# Made by deploy/setup_ec2.sh. The robot sends ROBOT_TOKEN; OPERATOR_TOKEN unlocks commands.
ROBOT_TOKEN=${ROBOT_TOKEN}
OPERATOR_TOKEN=${OPERATOR_TOKEN}
CORS_ORIGINS=*
RESUME_GRACE_S=60
ENV
  chmod 600 .env
  echo "Created .env with new random tokens."
else
  echo "Keeping the existing .env."
fi
ROBOT_TOKEN="$(grep -E '^ROBOT_TOKEN=' .env | tail -1 | cut -d= -f2-)"
OPERATOR_TOKEN="$(grep -E '^OPERATOR_TOKEN=' .env | tail -1 | cut -d= -f2-)"
OPERATOR_TOKEN="${OPERATOR_TOKEN:-$ROBOT_TOKEN}"

# ------------------------------------------------------------------ service
is_our_server() { curl -fs -m 2 "http://127.0.0.1:${PORT}/health" 2>/dev/null | grep -q live_sessions; }

if [[ $INSTALL_SERVICE == 1 ]]; then
  say "Installing the '${SERVICE}' service (starts on boot, restarts if it crashes)"
  sudo tee "$UNIT_PATH" >/dev/null <<EOF
[Unit]
Description=Prowl backend (FastAPI)
After=network-online.target
Wants=network-online.target

[Service]
User=${RUN_USER}
WorkingDirectory=${APP_DIR}
ExecStart=${APP_DIR}/.venv/bin/uvicorn app.main:app --host 0.0.0.0 --port ${PORT}
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
  sudo systemctl daemon-reload
  sudo systemctl enable "$SERVICE" >/dev/null 2>&1
  sudo systemctl restart "$SERVICE"

  say "Checking that it's up"
  up=0
  for _ in $(seq 1 20); do
    if is_our_server; then up=1; break; fi
    sleep 1
  done
  if [[ $up != 1 ]]; then
    echo "The new server isn't answering on port ${PORT}. Recent log lines:" >&2
    sudo journalctl -u "$SERVICE" -n 25 --no-pager >&2 || true
    echo >&2
    echo "If you see 'address already in use', the earlier test server is still running." >&2
    echo "Stop it (Ctrl+C in its terminal or tmux window), then: sudo systemctl restart ${SERVICE}" >&2
    exit 1
  fi
  echo "Running."
fi

# ------------------------------------------------------------------ summary
IMDS_TOKEN="$(curl -fs -m 2 -X PUT http://169.254.169.254/latest/api/token \
               -H 'X-aws-ec2-metadata-token-ttl-seconds: 60' 2>/dev/null || true)"
IP=""
if [[ -n "$IMDS_TOKEN" ]]; then
  IP="$(curl -fs -m 2 -H "X-aws-ec2-metadata-token: ${IMDS_TOKEN}" \
         http://169.254.169.254/latest/meta-data/public-ipv4 2>/dev/null || true)"
fi
IP="${IP:-<public-ip>}"

cat <<EOF

------------------------------------------------------------------------
 Dashboard:       http://${IP}:${PORT}/
 Debug view:      http://${IP}:${PORT}/debug
 API docs:        http://${IP}:${PORT}/docs
 Robot URL:       ws://${IP}:${PORT}/ws/robot
 ROBOT_TOKEN:     ${ROBOT_TOKEN}
 OPERATOR_TOKEN:  ${OPERATOR_TOKEN}

 Test from your laptop (in the repo folder, with backend/requirements.txt installed):
   python backend/tools/fake_robot.py --url ws://${IP}:${PORT}/ws/robot --token ${ROBOT_TOKEN}

 Logs:            journalctl -u ${SERVICE} -f
 After git pull:  bash deploy/setup_ec2.sh
 Port ${PORT} must be open in the instance's security group.
------------------------------------------------------------------------
EOF
if [[ ! -f ../frontend/dist/index.html && ! -f dashboard_dist/index.html ]]; then
  echo "(No built dashboard found, so ${IP}:${PORT}/ redirects to the debug view.)"
fi
if [[ $INSTALL_SERVICE == 0 ]]; then
  echo "Service not installed. Run in the foreground with:"
  echo "  .venv/bin/uvicorn app.main:app --host 0.0.0.0 --port ${PORT}"
fi
