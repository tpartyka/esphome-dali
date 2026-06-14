"""Stability/flood tests: opt-in, requires --runslow and a USB serial console.

Replicates the manual flood QA pass (rapid on/off/brightness/group/scene
commands across all lamps) while watching the serial console for crashes,
and checks the bus-error/online diagnostics stayed sane afterwards.
"""

import asyncio
import random
import time

import pytest

import config
from serial_monitor import SerialLogWatcher

pytestmark = pytest.mark.stability

FLOOD_DURATION_S = 30
COMMAND_INTERVAL_S = 0.1


def test_flood_no_crash(api_client, entities_by_name, event_loop):
    targets = [entities_by_name[f"DALI Light {a}"] for a in config.LAMP_ADDRS]
    targets += [entities_by_name[f"DALI Group {g}"] for g in config.GROUPS]

    bus_errors_before = _read_sensor_state(api_client, entities_by_name, event_loop, "DALI Bus Errors")

    with SerialLogWatcher(config.SERIAL_PORT, config.SERIAL_BAUD) as watcher:
        end = time.monotonic() + FLOOD_DURATION_S
        sent = 0
        while time.monotonic() < end:
            target = random.choice(targets)
            state = random.choice([True, False])
            brightness = random.uniform(0.05, 1.0)
            api_client.light_command(key=target.key, state=state, brightness=brightness)
            sent += 1
            time.sleep(COMMAND_INTERVAL_S)

        time.sleep(2)  # let queued/debounced writes flush before checking the log
        watcher.assert_no_crash()

    assert sent > 0

    # Turn everything off afterwards.
    for target in targets:
        api_client.light_command(key=target.key, state=False)

    bus_online = _read_sensor_state(api_client, entities_by_name, event_loop, "DALI Bus Online")
    bus_errors_after = _read_sensor_state(api_client, entities_by_name, event_loop, "DALI Bus Errors")

    assert bus_online is True, "DALI Bus Online is not ON after the flood"
    if bus_errors_before is not None and bus_errors_after is not None:
        # Some bus errors under heavy load are expected; a runaway counter
        # (e.g. >2x growth) suggests a regression.
        assert bus_errors_after <= max(bus_errors_before * 2, bus_errors_before + 50)


def _read_sensor_state(api_client, entities_by_name, event_loop, name, timeout=5.0):
    entity = entities_by_name.get(name)
    if entity is None:
        return None

    result = {}

    def on_state(state):
        if getattr(state, "key", None) == entity.key:
            result["value"] = getattr(state, "state", None)

    api_client.subscribe_states(on_state)
    deadline = time.monotonic() + timeout
    while "value" not in result and time.monotonic() < deadline:
        event_loop.run_until_complete(asyncio.sleep(0.1))
    return result.get("value")
