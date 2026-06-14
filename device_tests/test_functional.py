"""Functional smoke tests: run on every change touching device-facing code.

Requires a running device reachable via the native API (config.HOST) and,
for identify, the web dashboard (config.DASHBOARD_URL).
"""

import time

import httpx
import pytest

import config

pytestmark = pytest.mark.functional

SETTLE_S = 1.0


def _set_light(api_client, event_loop, key, state, brightness=None):
    kwargs = {"key": key, "state": state}
    if brightness is not None:
        kwargs["brightness"] = brightness
    api_client.light_command(**kwargs)
    time.sleep(SETTLE_S)


@pytest.mark.parametrize("addr", config.LAMP_ADDRS)
def test_lamp_on_off(api_client, entities_by_name, event_loop, addr):
    light = entities_by_name[f"DALI Light {addr}"]
    _set_light(api_client, event_loop, light.key, True, 1.0)
    _set_light(api_client, event_loop, light.key, False)


@pytest.mark.parametrize("addr", config.LAMP_ADDRS)
def test_lamp_brightness(api_client, entities_by_name, event_loop, addr):
    light = entities_by_name[f"DALI Light {addr}"]
    for brightness in (0.25, 0.75):
        _set_light(api_client, event_loop, light.key, True, brightness)
    _set_light(api_client, event_loop, light.key, False)


@pytest.mark.parametrize("group", config.GROUPS)
def test_group_light_toggle(api_client, entities_by_name, event_loop, group):
    group_light = entities_by_name[f"DALI Group {group}"]
    _set_light(api_client, event_loop, group_light.key, True, 0.5)
    _set_light(api_client, event_loop, group_light.key, False)


@pytest.mark.parametrize("scene", config.SCENES)
def test_scene_recall_button(api_client, entities_by_name, scene):
    button = entities_by_name[f"DALI Scene {scene}"]
    api_client.button_command(key=button.key)
    time.sleep(SETTLE_S)


# Identify blinks BLINKS*2 times (esphome_dali.cpp's process_identify_, 3 blinks
# x 2 steps) at STEP_MS=400ms each, then restores the lamp's pre-blink level.
IDENTIFY_SEQUENCE_S = 3.0


def test_identify(dashboard_url):
    addr = config.LAMP_ADDRS[0]
    resp = httpx.post(f"{dashboard_url}/api/identify", params={"addr": addr}, timeout=5)
    # web_server_idf only maps 200/404/409 correctly; the dashboard's POST
    # handlers use 200 (queued) / 409 (validation error or busy).
    assert resp.status_code == 200
    assert resp.text == "queued"
    # Wait for the blink sequence to finish and restore the lamp's level before
    # any later test sends a real command to the same address -- otherwise the
    # identify restore can race with and overwrite that command.
    time.sleep(IDENTIFY_SEQUENCE_S)


def test_dashboard_lamps_endpoint(dashboard_url):
    resp = httpx.get(f"{dashboard_url}/api/lamps", timeout=5)
    assert resp.status_code == 200
    body = resp.json()
    addrs = {lamp["addr"] for lamp in body["lamps"]}
    for addr in config.LAMP_ADDRS:
        assert addr in addrs


def test_bus_diagnostics_baseline(entities_by_name):
    online = entities_by_name.get("DALI Bus Online")
    disconnected = entities_by_name.get("DALI Bus Disconnected")
    assert online is not None, "DALI Bus Online sensor not exposed"
    assert disconnected is not None, "DALI Bus Disconnected sensor not exposed"
    # Presence is verified here; live state values are checked via the
    # subscribe API in test_stability.py where a state stream is already open.


def test_lamp_control_endpoint(dashboard_url):
    addr = config.LAMP_ADDRS[0]
    resp = httpx.post(
        f"{dashboard_url}/api/lamp",
        params={"target": f"lamp:{addr}", "on": 1, "brightness_pct": 50},
        timeout=5,
    )
    assert resp.status_code == 200
    assert resp.text == "queued"
    time.sleep(SETTLE_S)

    body = httpx.get(f"{dashboard_url}/api/lamps", timeout=5).json()
    lamp = next(l for l in body["lamps"] if l["addr"] == addr)
    assert lamp["on"] is True
    assert abs(lamp["brightness_pct"] - 50) <= 5

    resp = httpx.post(f"{dashboard_url}/api/lamp", params={"target": f"lamp:{addr}", "on": 0}, timeout=5)
    assert resp.status_code == 200
    time.sleep(SETTLE_S)


@pytest.mark.parametrize("group", config.GROUPS)
def test_group_control_endpoint(dashboard_url, group):
    resp = httpx.post(
        f"{dashboard_url}/api/lamp",
        params={"target": f"group:{group}", "on": 1, "brightness_pct": 40},
        timeout=5,
    )
    assert resp.status_code == 200
    assert resp.text == "queued"
    time.sleep(SETTLE_S)

    body = httpx.get(f"{dashboard_url}/api/lamps", timeout=5).json()
    groups = {g["group"]: g for g in body["groups"]}
    assert group in groups
    assert groups[group]["on"] is True

    resp = httpx.post(f"{dashboard_url}/api/lamp", params={"target": f"group:{group}", "on": 0}, timeout=5)
    assert resp.status_code == 200
    time.sleep(SETTLE_S)


def test_lamp_control_validation(dashboard_url):
    # Missing target -> 409.
    resp = httpx.post(f"{dashboard_url}/api/lamp", params={"on": 1}, timeout=5)
    assert resp.status_code == 409

    # No control fields -> 409.
    addr = config.LAMP_ADDRS[0]
    resp = httpx.post(f"{dashboard_url}/api/lamp", params={"target": f"lamp:{addr}"}, timeout=5)
    assert resp.status_code == 409

    # Out-of-range brightness -> 409.
    resp = httpx.post(
        f"{dashboard_url}/api/lamp",
        params={"target": f"lamp:{addr}", "brightness_pct": 150},
        timeout=5,
    )
    assert resp.status_code == 409

    # Unknown target -> 409.
    resp = httpx.post(f"{dashboard_url}/api/lamp", params={"target": "all", "on": 1}, timeout=5)
    assert resp.status_code == 409


def test_names_roundtrip(dashboard_url):
    addr = config.LAMP_ADDRS[0]
    group = config.GROUPS[0]
    scene = config.SCENES[0]

    cases = [
        ("lamp", addr, "Kitchen Test"),
        ("group", group, "Group Test"),
        ("scene", scene, "Scene Test"),
    ]
    try:
        for kind, index, name in cases:
            resp = httpx.post(
                f"{dashboard_url}/api/names",
                params={"kind": kind, "index": index, "name": name},
                timeout=5,
            )
            assert resp.status_code == 200
            assert resp.text == "queued"
        time.sleep(SETTLE_S)

        body = httpx.get(f"{dashboard_url}/api/names", timeout=5).json()
        for kind, index, name in cases:
            assert body[f"{kind}s"][str(index)] == name
    finally:
        # Clear the names again so this test doesn't leave dashboard state behind.
        for kind, index, _ in cases:
            httpx.post(f"{dashboard_url}/api/names", params={"kind": kind, "index": index, "name": ""}, timeout=5)
        time.sleep(SETTLE_S)


def test_names_validation(dashboard_url):
    resp = httpx.post(f"{dashboard_url}/api/names", params={"kind": "bogus", "index": 0, "name": "x"}, timeout=5)
    assert resp.status_code == 409

    resp = httpx.post(f"{dashboard_url}/api/names", params={"kind": "lamp", "index": 99, "name": "x"}, timeout=5)
    assert resp.status_code == 409


def test_log_endpoint(dashboard_url):
    resp = httpx.get(f"{dashboard_url}/api/log", timeout=5)
    assert resp.status_code == 200
    body = resp.json()

    assert "now_ms" in body
    counters = body["counters"]
    for key in ("errors", "bus_down", "collisions", "disconnected", "online"):
        assert key in counters

    assert isinstance(body["events"], list)
    for event in body["events"]:
        assert "ts" in event
        assert "type" in event
        assert "addr" in event
        assert "value" in event


def test_discovery_scan(dashboard_url):
    resp = httpx.post(f"{dashboard_url}/api/discovery", params={"mode": "discover"}, timeout=5)
    assert resp.status_code == 200
    assert resp.text == "queued"

    # mode=all is destructive (reassigns every short address) and is
    # intentionally not exercised here -- see device_tests/README.md.
    resp = httpx.post(f"{dashboard_url}/api/discovery", params={"mode": "all"}, timeout=5)
    assert resp.status_code == 409

    resp = httpx.post(f"{dashboard_url}/api/discovery", params={"mode": "bogus"}, timeout=5)
    assert resp.status_code == 409
