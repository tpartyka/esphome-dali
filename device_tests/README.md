# Device regression tests

End-to-end tests against a running DALI bus-master device, using the Home
Assistant native API (`aioesphomeapi`) and the built-in web dashboard REST
API. These complement the host unit tests in `../tests/` (pure protocol/logic,
no hardware needed) by exercising the full ESPHome component against real
control gear.

## Hardware assumptions

- An ESP32-S3 running `esphome-web-ec5578.yaml`, reachable on the network with
  the native API enabled and the web dashboard on port 8080.
- DALI lamps at short addresses 2, 3, 4 (override with `DALI_LAMP_ADDRS`).
- Group lights for DALI groups 3 and 7 (override with `DALI_GROUPS`).
- A USB serial connection for the stability tier (`DALI_SERIAL_PORT`,
  default `/dev/cu.usbmodem1101`).

Override any of these via environment variables — see `config.py` for the
full list (`DALI_HOST`, `DALI_API_PORT`, `DALI_DASHBOARD_PORT`,
`DALI_LAMP_ADDRS`, `DALI_GROUPS`, `DALI_SCENES`, `DALI_SERIAL_PORT`,
`DALI_SERIAL_BAUD`).

## Setup

```bash
python3 -m venv .venv-devtests
source .venv-devtests/bin/activate
pip install -r device_tests/requirements.txt
```

## Running

```bash
# Every change touching device-facing code (entities, web dashboard, light driver)
DALI_HOST=192.168.0.127 pytest device_tests -m functional

# Before merging feature work
DALI_HOST=192.168.0.127 DALI_SERIAL_PORT=/dev/cu.usbmodem1101 \
    pytest device_tests -m "functional or performance"

# Periodic/pre-release: 30s flood test + crash/watchdog watch over serial
DALI_HOST=192.168.0.127 DALI_SERIAL_PORT=/dev/cu.usbmodem1101 \
    pytest device_tests -m stability --runslow
```

Stability tests are skipped by default (they hold the USB serial port open
and run for tens of seconds); pass `--runslow` to enable them.
