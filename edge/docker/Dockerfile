# ROS2 Jazzy dev/test image for the Prowl edge drivers (Raspberry Pi 4, arm64).
# The workspace is bind-mounted at /ws at runtime, not baked in; see dev.sh.
FROM ros:jazzy-ros-base

# pigpiod client library for the motor driver, plus `pigs` for poking the daemon;
# ncurses for the motor panel.
# Ubuntu's libpigpiod-if-dev ships pigpiod_if2.h without the pigpio.h it includes,
# so dev.sh mounts the host's copy (from Raspberry Pi OS's pigpio package).
RUN apt-get update && apt-get install -y --no-install-recommends \
      libpigpiod-if-dev \
      pigpio-tools \
      libncurses-dev \
      i2c-tools \
    && rm -rf /var/lib/apt/lists/*

# Run as the host user so colcon's build/ install/ log/ stay owned by them.
# The base image already has uid 1000 (ubuntu); only add a user for other uids.
ARG UID=1000
ARG GID=1000
RUN getent group "${GID}" >/dev/null || groupadd -g "${GID}" dev; \
    getent passwd "${UID}" >/dev/null || useradd -m -u "${UID}" -g "${GID}" -s /bin/bash dev

# Interactive shells get ROS and the workspace overlay (once it has been built).
RUN printf '%s\n' \
      'source /opt/ros/jazzy/setup.bash' \
      '[ -f /ws/install/setup.bash ] && source /ws/install/setup.bash' \
      >> /etc/bash.bashrc

USER ${UID}:${GID}
WORKDIR /ws
