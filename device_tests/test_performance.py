"""Performance checks: debounce coalescing and poll latency.

The debounce test requires a USB serial connection to count TX frames in the
device log (--runslow not required, but DALI_SERIAL_PORT must be set and
reachable).
"""

import asyncio
import re
import time

import pytest

import config
from serial_monitor import SerialLogWatcher

pytestmark = pytest.mark.performance

DALI_LIGHT_COMMAND_DEBOUNCE_S = 0.150


def test_debounce_coalesces_rapid_brightness_changes(api_client, entities_by_name):
    addr = config.LAMP_ADDRS[0]
    light = entities_by_name[f"DALI Light {addr}"]

    with SerialLogWatcher(config.SERIAL_PORT, config.SERIAL_BAUD) as watcher:
        time.sleep(0.2)  # let the watcher attach before the burst
        burst = [0.1, 0.3, 0.5, 0.7, 0.9]
        for brightness in burst:
            api_client.light_command(key=light.key, state=True, brightness=brightness)
            time.sleep(0.03)  # faster than the 150ms debounce window
        time.sleep(DALI_LIGHT_COMMAND_DEBOUNCE_S + 0.3)  # let the pending write flush

    api_client.light_command(key=light.key, state=False)

    # DAPC frames address byte is (addr << 1), printed as lowercase hex by
    # the "TX: %02x %02x" log (esphome_dali.cpp).
    addr_byte = f"{(addr << 1):02x}"
    tx_pattern = re.compile(rf"TX: {addr_byte} ")
    tx_count = sum(1 for line in watcher.lines() if tx_pattern.search(line))
    # The whole burst (sent within ~120ms) plus the trailing flush should
    # coalesce to roughly 1-2 frames to this lamp, not one per command.
    assert tx_count <= 2, f"expected coalesced TX frames, got {tx_count}: {watcher.lines()}"


def test_poll_latency_reflects_state_change(api_client, entities_by_name, event_loop):
    addr = config.LAMP_ADDRS[0]
    light = entities_by_name[f"DALI Light {addr}"]

    seen_states = []

    def on_state(state):
        seen_states.append((time.monotonic(), state))

    api_client.subscribe_states(on_state)

    start = time.monotonic()
    api_client.light_command(key=light.key, state=True, brightness=0.6)

    # state_poll_interval default is 10s; debounce window is 150ms.
    max_wait = 12.0
    deadline = start + max_wait
    matched_at = None
    while time.monotonic() < deadline:
        for ts, state in seen_states:
            if getattr(state, "key", None) == light.key and getattr(state, "state", False):
                matched_at = ts
                break
        if matched_at is not None:
            break
        # Run the loop briefly so aioesphomeapi can process incoming socket
        # data and invoke the subscribe_states callback.
        event_loop.run_until_complete(asyncio.sleep(0.2))

    api_client.light_command(key=light.key, state=False)

    assert matched_at is not None, "no state update observed for the lamp"
    latency = matched_at - start
    assert latency <= max_wait, f"poll latency {latency:.2f}s exceeded {max_wait}s"
