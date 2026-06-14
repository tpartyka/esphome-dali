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


def test_identify(dashboard_url):
    addr = config.LAMP_ADDRS[0]
    resp = httpx.post(f"{dashboard_url}/api/identify", params={"addr": addr}, timeout=5)
    # web_server_idf only maps 200/404/409 correctly; the dashboard's POST
    # handlers use 200 (queued) / 409 (validation error or busy).
    assert resp.status_code == 200
    assert resp.text == "queued"


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
