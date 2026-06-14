import asyncio
import time

import pytest
from aioesphomeapi import APIClient

import config


def pytest_addoption(parser):
    parser.addoption(
        "--runslow",
        action="store_true",
        default=False,
        help="run slow/stability tests that require a USB serial connection",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "functional: fast checks against a running device")
    config.addinivalue_line("markers", "performance: timing/coalescing checks")
    config.addinivalue_line(
        "markers", "stability: flood + serial crash watch, requires --runslow and USB serial"
    )


def pytest_collection_modifyitems(config, items):
    if config.getoption("--runslow"):
        return
    skip_slow = pytest.mark.skip(reason="needs --runslow")
    for item in items:
        if "stability" in item.keywords:
            item.add_marker(skip_slow)


@pytest.fixture(scope="session")
def event_loop():
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="session")
def api_client(event_loop):
    async def _connect():
        client = APIClient(config.HOST, config.API_PORT, config.API_PASSWORD)
        await client.connect(login=True)
        return client

    client = event_loop.run_until_complete(_connect())
    yield client
    event_loop.run_until_complete(client.disconnect())


@pytest.fixture(scope="session")
def entities_by_name(api_client, event_loop):
    entities, _ = event_loop.run_until_complete(api_client.list_entities_services())
    return {e.name: e for e in entities}


@pytest.fixture(scope="session")
def dashboard_url():
    return config.DASHBOARD_URL


@pytest.fixture(scope="session", autouse=True)
def turn_off_lamps_after_session(api_client, entities_by_name):
    yield
    for addr in config.LAMP_ADDRS:
        entity = entities_by_name.get(f"DALI Light {addr}")
        if entity is not None:
            api_client.light_command(key=entity.key, state=False)
    for group in config.GROUPS:
        entity = entities_by_name.get(f"DALI Group {group}")
        if entity is not None:
            api_client.light_command(key=entity.key, state=False)
    # Give the device's debounce window time to flush the off commands to
    # the bus before the api_client fixture disconnects.
    time.sleep(1.0)


def get_light(entities_by_name: dict, name: str):
    return entities_by_name[name]


def get_binary_sensor(entities_by_name: dict, name: str):
    return entities_by_name[name]


def get_button(entities_by_name: dict, name: str):
    return entities_by_name[name]
