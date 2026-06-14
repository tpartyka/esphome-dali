"""Device test configuration, read from environment variables.

Defaults match the bench setup: ESP32-S3 "LightCoordinator" with DALI lamps
at short addresses 2/3/4, group lights 3 and 7, and DALI scenes 0-15.
"""

import os


def _env_list(name: str, default: str) -> list[int]:
    return [int(x) for x in os.environ.get(name, default).split(",") if x]


# Native API connection
HOST = os.environ.get("DALI_HOST", "192.168.0.127")
API_PORT = int(os.environ.get("DALI_API_PORT", "6053"))
API_PASSWORD = os.environ.get("DALI_API_PASSWORD", "")

# Web dashboard REST API
DASHBOARD_PORT = int(os.environ.get("DALI_DASHBOARD_PORT", "8080"))
DASHBOARD_URL = f"http://{HOST}:{DASHBOARD_PORT}"

# Bus topology
LAMP_ADDRS = _env_list("DALI_LAMP_ADDRS", "2,3,4")
GROUPS = _env_list("DALI_GROUPS", "3,7")
SCENES = _env_list("DALI_SCENES", "0,1")

# Serial console (stability tier only)
SERIAL_PORT = os.environ.get("DALI_SERIAL_PORT", "/dev/cu.usbmodem1101")
SERIAL_BAUD = int(os.environ.get("DALI_SERIAL_BAUD", "115200"))
