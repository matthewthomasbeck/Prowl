#!/usr/bin/env bash
# Manage the ROS2 dev container for the Prowl edge drivers.
#
#   docker/dev.sh image          build the image
#   docker/dev.sh up             start the container (host network, workspace at /ws)
#   docker/dev.sh down           remove the container
#   docker/dev.sh shell          interactive shell inside it
#   docker/dev.sh <cmd...>       run any command inside it with ROS sourced, e.g.
#                                  docker/dev.sh colcon build
#                                  docker/dev.sh ros2 launch prowl_motor_driver motor_driver.launch.py
#
# The motor driver talks to the host's pigpiod over localhost:8888, which is why
# the container shares the host network.
set -euo pipefail

IMAGE=prowl-edge-dev:jazzy
CONTAINER=prowl-edge-dev
WS="$(cd "$(dirname "$0")/.." && pwd)"

# Fall back to sudo when the user isn't in the docker group.
DOCKER=(docker)
docker info >/dev/null 2>&1 || DOCKER=(sudo docker)

# Only ask for a TTY when both ends are a terminal: with output captured, e.g.
# out="$(docker/dev.sh colcon build)", a TTY exec returns before the command
# finishes. docker exec doesn't forward TERM, which curses (the motor panel) needs.
TTY=(-i)
[ -t 0 ] && [ -t 1 ] && TTY+=(-t -e "TERM=${TERM:-xterm}")

exists() { "${DOCKER[@]}" container inspect "$CONTAINER" >/dev/null 2>&1; }
running() { [ "$("${DOCKER[@]}" container inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null)" = true ]; }

case "${1:-}" in
  image)
    "${DOCKER[@]}" build --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" \
      -t "$IMAGE" "$WS/docker"
    ;;
  up)
    "${DOCKER[@]}" image inspect "$IMAGE" >/dev/null 2>&1 || "$0" image
    if running; then
      echo "$CONTAINER is already running"
    elif exists; then
      "${DOCKER[@]}" start "$CONTAINER" >/dev/null
    else
      # --init so exited ros2 processes get reaped (sleep as PID 1 never does).
      "${DOCKER[@]}" run -d --init --name "$CONTAINER" \
        --network host --ipc host \
        -v "$WS:/ws" \
        -v /usr/include/pigpio.h:/usr/include/pigpio.h:ro \
        "$IMAGE" sleep infinity >/dev/null
    fi
    ;;
  down)
    exists && "${DOCKER[@]}" rm -f "$CONTAINER" >/dev/null
    ;;
  shell)
    "${DOCKER[@]}" exec "${TTY[@]}" "$CONTAINER" bash
    ;;
  "")
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
  *)
    # bash -c with the command as positional args keeps their quoting intact.
    "${DOCKER[@]}" exec "${TTY[@]}" "$CONTAINER" bash -c \
      'source /opt/ros/jazzy/setup.bash
       [ -f install/setup.bash ] && source install/setup.bash
       exec "$@"' bash "$@"
    ;;
esac
