# Hardware

## Parts

- Raspberry Pi 4 (Raspberry Pi OS bookworm)
- 2× DBH-12V dual H-bridge boards (DC 5–12 V, 0–30 A), one per side
- 4× DC motors on mecanum wheels, so the robot can strafe. No encoders
- LD19 LiDAR, on the Pi's mini UART
- MPU6050 IMU breakout, on I2C bus 1
- Raspberry Pi Camera Module 3 Wide (IMX708 sensor, 4608×2592, autofocus, about 120° across), on the CSI
  camera port
- u-blox 7 GPS module (the MAX-7 / NEO-7 family), on the Pi's UART4
- 3D-printed parts, as STL files in `3d_parts/`: the wheel arm, the T bracket and the connector arm

## Motors

BCM GPIO number, with the physical header pin in brackets. Each H-bridge channel's EN pin is held HIGH in
hardware rather than wired to the Pi.

| Wheel | H-bridge | IN1 | IN2 | Inverted |
|---|---|---|---|---|
| front_left | left board, channel B | GPIO20 (38) | GPIO21 (40) | yes |
| rear_left | left board, channel A | GPIO5 (29) | GPIO6 (31) | yes |
| front_right | right board, channel A | GPIO17 (11) | GPIO27 (13) | no |
| rear_right | right board, channel B | GPIO22 (15) | GPIO23 (16) | no |

A positive duty turns a motor clockwise. The two sides are mirror images, so the left motors are inverted in
`motor_driver.yaml`; after that, positive means "push the robot forward" on every wheel. The pins and
directions are all in that file.

The mecanum wheels have to be on the right corners: seen from above, the top rollers form an X.

## Sensors

None of the sensors share a pin with the motors.

| Sensor pin | Pi header pin |
|---|---|
| LD19 P5V | 5V (pin 4) |
| LD19 GND | GND (pin 6) |
| LD19 Tx | GPIO15, RXD (pin 10) |
| LD19 PWM | GND (pin 25). The manual says it must be grounded when unused; grounded means its own 10 Hz speed control |
| MPU6050 VIN | 3.3V (pin 1) |
| MPU6050 GND | GND (pin 9) |
| MPU6050 SDA | GPIO2 (pin 3) |
| MPU6050 SCL | GPIO3 (pin 5) |
| GPS TX | GPIO9, RXD4 (pin 21) |
| GPS RX | GPIO8, TXD4 (pin 24) |
| GPS power and ground | TODO: record which pins |

The LD19 is on the mini UART (`/dev/ttyS0`, `enable_uart=1`) and the MPU6050 on I2C bus 1 (`/dev/i2c-1`,
`dtparam=i2c_arm=on`), at address `0x68` (`0x69` if AD0 is pulled high). The GPS is on UART4 (`/dev/ttyAMA4`,
`dtoverlay=uart4`), which takes GPIO8 and GPIO9 from SPI0, so SPI0 has to stay off. The camera is found by
`camera_auto_detect=1`. All four settings are in `/boot/firmware/config.txt`; the container gets the devices
when it's created.

The GPS talks at 9600 baud, the u-blox 7's default. Its UART's levels are 0 V to its supply (the u-blox
[hardware integration manual](https://content.u-blox.com/sites/default/files/products/documents/MAX7-NEO7_HardwareIntegrationManual_(UBX-13003704).pdf),
section 2.6.1), and the Pi's pins take 3.3 V at most: check that the module's TX is 3.3 V logic. It needs a
clear view of the sky to get a fix; indoors it usually hears no satellites at all.

Mounting:

- **IMU:** flat, with the board's X arrow pointing forward, so its axes match ROS (x forward, y left, z up).
- **LiDAR:** taken to sit in the middle of the robot with its 0° facing forward. Its real position
  (`laser_x`, `laser_y`, `laser_yaw` in `dashboard_bridge.yaml`) isn't measured yet.
- **Camera:** if it's mounted upside down, set `rotate_180` in `camera_driver.yaml`.
- **GPS:** antenna up, with nothing metal above it.

## Checking the wiring

```bash
./prowl dev i2cdetect -y 1     # the IMU: should show 68
./prowl dev bash -c 'stty -F /dev/ttyS0 230400 raw -echo; timeout 2 od -An -tx1 /dev/ttyS0 | head'
                               # the LiDAR: bytes, with 54 2c appearing regularly
./prowl dev cam -l             # the camera: should list imx708_wide
./prowl dev bash -c 'stty -F /dev/ttyAMA4 9600 raw -echo; timeout 3 cat /dev/ttyAMA4'
                               # the GPS: a burst of lines like $GPRMC,... and $GPGGA,... every second
./prowl lidar                  # the LiDAR live, with the nearest objects around it
./prowl imu                    # the IMU live, with tilt and gyro readings
```

To check the motors, use `./prowl panel` ([drivers.md](drivers.md#motor-panel)): its `1`–`4` keys run one
wheel for 3 s. Read [safety.md](safety.md) first.
