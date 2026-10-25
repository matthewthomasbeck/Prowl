# ROS2 Jazzy dev/test image for the Prowl edge drivers (Raspberry Pi 4, arm64).
# The workspace is bind-mounted at /ws at runtime, not baked in; see dev.sh.
FROM ros:jazzy-ros-base

# pigpiod client library for the motor driver, plus `pigs` for poking the daemon;
# ncurses for the motor panel.
# Ubuntu's libpigpiod-if-dev ships pigpiod_if2.h without the pigpio.h it includes,
# so dev.sh mounts the host's copy (from Raspberry Pi OS's pigpio package).
# libcamera (with the Raspberry Pi pipeline and the IMX708 tuning files in libcamera-ipa) and
# TurboJPEG for the camera driver; libcamera-tools gives `cam -l` for checking the camera.
RUN apt-get update && apt-get install -y --no-install-recommends \
      libpigpiod-if-dev \
      pigpio-tools \
      libncurses-dev \
      i2c-tools \
      libcamera-dev \
      libcamera-ipa \
      libcamera-tools \
      libturbojpeg0-dev \
      curl \
      xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Node.js for the dashboard bridge (prowl_dashboard_bridge). Ubuntu 24.04's nodejs is 18, and rclnodejs
# needs >= 20.20.2, so install the official arm64 build, checked against nodejs.org's SHASUMS256.txt.
ARG NODE_VERSION=24.22.1
ARG NODE_SHA256=dbabf7a1cbf947e6059ffad2c27b80041db97dea1e553b62afdee7f13fca336e
RUN curl -fsSLo /tmp/node.tar.xz \
      "https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-linux-arm64.tar.xz" \
    && echo "${NODE_SHA256}  /tmp/node.tar.xz" | sha256sum -c - \
    && tar -xJf /tmp/node.tar.xz -C /usr/local --strip-components=1 --no-same-owner \
         --exclude CHANGELOG.md --exclude LICENSE --exclude README.md \
    && rm /tmp/node.tar.xz

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
