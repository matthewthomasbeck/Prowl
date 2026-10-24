#!/usr/bin/env bash
# Build (when sources changed) and open the MPU6050 terminal monitor. From the
# project root:
#   docker/dev.sh src/prowl_mpu6050_driver/tui.sh [bus] [address]   (defaults: 1 0x68)
# On Prowl the MPU6050 is on I2C bus 1 (/dev/i2c-1): SDA on header pin 3 (GPIO2),
# SCL on pin 5 (GPIO3); address 0x68 with AD0 low.
# The unit test runs with: docker/dev.sh colcon test --packages-select prowl_mpu6050_driver
set -euo pipefail

PKG="$(cd "$(dirname "$0")" && pwd)"
WS="$(cd "$PKG/../.." && pwd)"

# Two monitors would steal each other's data-ready flags, and a monitor from a
# dropped SSH session keeps running in the container.
if pgrep -x mpu6050_tui >/dev/null; then
  read -rp "An MPU6050 monitor is already running (maybe from a dropped connection). Stop it and open a new one? [y/N] " answer
  [[ "$answer" == [yY]* ]] || exit 1
  pkill -x mpu6050_tui
  for _ in $(seq 30); do pgrep -x mpu6050_tui >/dev/null || break; sleep 0.1; done
  pkill -KILL -x mpu6050_tui || true
fi

# The ROS node reads the same sensor, and the two would steal each other's data-ready flags.
if pgrep -f "lib/prowl_mpu6050_driver/mpu6050_node" >/dev/null; then
  echo "The mpu6050_node ROS driver is running and reading this sensor. Stop it first (Ctrl-C, or pkill -INT -f mpu6050_node)."
  exit 1
fi

if ! out="$(cd "$WS" && colcon build --packages-select prowl_mpu6050_driver 2>&1)"; then
  echo "$out"
  exit 1
fi
exec "$WS/install/prowl_mpu6050_driver/lib/prowl_mpu6050_driver/mpu6050_tui" "$@"
