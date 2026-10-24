#!/usr/bin/env bash
# One command to run the LD19 lidar terminal tool: builds it (colcon) if needed, then
# opens it. Run from the project root, inside the dev container:
#
#   docker/dev.sh src/prowl_ld19_driver/tui.sh [port]
#
# port defaults to /dev/ttyS0, the Pi's mini UART: the LD19's Tx is wired to
# header pin 10 (GPIO15, UART RXD).
set -euo pipefail

PKG="$(cd "$(dirname "$0")" && pwd)"
WS="$(cd "$PKG/../.." && pwd)"

# Processes in the container outlive a dropped SSH connection, so an old tool
# may still be holding the port (it is opened for exclusive use).
if pgrep -x ld19_tui >/dev/null; then
  read -rp "An ld19_tui is already running (maybe from a dropped connection). Stop it and open a new one? [y/N] " answer
  [[ "$answer" == [yY]* ]] || exit 1
  pkill -x ld19_tui
  for _ in $(seq 30); do pgrep -x ld19_tui >/dev/null || break; sleep 0.1; done
  pkill -KILL -x ld19_tui 2>/dev/null || true
fi

# The ROS node reads the same sensor, and it opens the port for exclusive use.
if pgrep -f "lib/prowl_ld19_driver/ld19_node" >/dev/null; then
  echo "The ld19_node ROS driver is running and reading this sensor. Stop it first (Ctrl-C, or pkill -INT -f ld19_node)."
  exit 1
fi

if ! out="$(cd "$WS" && colcon build --packages-select prowl_ld19_driver 2>&1)"; then
  echo "$out"
  exit 1
fi
exec "$WS/install/prowl_ld19_driver/lib/prowl_ld19_driver/ld19_tui" "$@"
