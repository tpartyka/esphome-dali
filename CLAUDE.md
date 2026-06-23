# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

```bash
# Compile the example ESPHome config (ESP32-S3, ESP-IDF framework)
esphome compile esphome-web-ec5578.yaml

# Flash + stream logs over USB
esphome run esphome-web-ec5578.yaml --device /dev/cu.usbmodem1101
esphome logs esphome-web-ec5578.yaml --device /dev/cu.usbmodem1101

# Host test suite for the core DALI protocol logic (no ESPHome/Arduino needed)
cd tests && make        # build + run all tests
cd tests && make run    # same
cd tests && make clean  # remove build artifacts

# Standalone PlatformIO build (AVR example app in src/, uses components/dali
# as a library)
pio run

# Device regression suite (pytest + aioesphomeapi + web dashboard REST API),
# run against real hardware before merging device-facing changes
pip install -r device_tests/requirements.txt
pytest device_tests -m functional                    # every device-facing change
pytest device_tests -m "functional or performance"   # before merging feature work
pytest device_tests -m stability --runslow           # periodic/pre-release, needs USB serial
```

`esphome-web-ec5578.yaml` references `!secret wifi_ssid`/`wifi_password` from
a gitignored `secrets.yaml` — never commit real credentials.

### When to run which test tier

- **Always** (no hardware): `cd tests && make run` — the host suite covers
  the dual-arch core plus the framework-agnostic logic extracted from the
  ESPHome side (debounce, availability, bus health, inventory — see below).
  Run this on every change.
- **Device-facing changes** (entities, web dashboard, light driver, recovery):
  `pytest device_tests -m functional` against the running device.
- **Before merging feature work** that touches timing/coalescing or polling:
  add `-m "functional or performance"` (the performance tier needs
  `DALI_SERIAL_PORT` set to count TX frames on the wire).
- **Periodically / pre-release**: `pytest device_tests -m stability --runslow`
  — a flood test with a serial crash/watchdog watch (see
  `device_tests/README.md`).

## Architecture

### Dual-arch constraint

`components/dali/` is dual-purpose:
- An **ESPHome custom component** (loaded via `external_components` in YAML).
- A **standalone PlatformIO Arduino library** (see `platformio.ini`,
  `components/dali/library.json`, and the AVR example app in `src/`).

`library.json`'s `srcFilter` (`+<*> -<esphome_*> -<output/> -<light/>`) means
the PlatformIO build only sees `dali.h`, `dali_bus_manager.cpp`,
`dali_tx_collision.h`, and `port.h`. These **core files must stay
framework-agnostic** (no ESPHome includes/types) — they're tested in
isolation by the host suite in `tests/`. All ESPHome-specific code (entity
creation, config schema, web dashboard, input-device decoding) lives in
`esphome_dali*.h/.cpp` and `__init__.py`, which the PlatformIO build never
compiles.

### Dynamic entity creation & StaticVector capacity

Lights, group lights, scene buttons, group buttons, fade-time numbers, and
availability/problem binary_sensors are all created at **runtime** in C++
(from DALI bus discovery), not declared in YAML. However, ESPHome 2026.x
stores each entity domain in a fixed-capacity `StaticVector` sized at codegen
time — anything pushed past that capacity is silently dropped (it just never
appears in Home Assistant, with no error).

`components/dali/__init__.py`'s `to_code()` therefore calls
`CORE.register_platform_component(<domain>, var)` once per entity slot that
the C++ code might create at runtime, e.g.:
- `max_lights` slots for discovered lamps (`light`), plus `max_groups` more if
  `expose_groups` is set
- `DALI_DYNAMIC_BUTTON_COUNT` / `DALI_SCENE_BUTTON_COUNT` /
  `DALI_GROUP_BUTTON_COUNT` for auto-created buttons
- `DALI_DYNAMIC_NUMBER_COUNT` / `DALI_SCENE_NUMBER_COUNT` /
  `DALI_GROUP_NUMBER_COUNT` for auto-created numbers
- one slot per lamp for the "Status" diagnostic binary_sensor
  (`expose_problem`), plus one for `expose_bus_status`
- `DALI_DIAG_BINARY_SENSOR_COUNT` (binary_sensor) plus `DALI_DIAG_SENSOR_COUNT`
  / `DALI_DIAG_COLLISION_SENSOR_COUNT` (sensor) for `expose_bus_diagnostics`'s
  "DALI Bus Disconnected"/"DALI Bus Errors"/"DALI Bus Down Events"/"DALI
  Collisions" entities
- one `switch` slot per `circadian_groups` entry, for that group's "DALI Group
  N Circadian" enable/disable switch — unlike the other entries above, this
  count is genuinely variable (one per YAML list entry, not a fixed `_COUNT`
  constant). `switch` was added to `AUTO_LOAD` for this.

If you add a new kind of runtime-created entity, add a matching `_COUNT`
constant and `register_platform_component` call here, or it will be silently
capped.

### Web dashboard

`esphome_dali_web.h/.cpp` implements `DaliWebDashboard`, a small
`AsyncWebHandler` (via `web_server_base`) serving a single embedded HTML/JS
"cockpit" page plus a REST API. GET handlers read cached state only. POST
handlers enqueue a `DaliPendingAction` that is drained on the next
`DaliBusComponent::loop()` iteration, because the bit-banged DALI bus is not
thread-safe and must only be touched from the main loop. POST responses are
`200 "queued"` or `409 "<reason>"` (only these two status codes map correctly
through web_server_idf).

`expose_dashboard: true` requires an explicit `web_server:` block in the same
YAML, with its `port` matching `dashboard_port` (`__init__.py`'s
`_final_validate_dashboard` enforces this at compile time with a clear error
otherwise). This is because ESPHome's API only reports a `webserver_port` in
`DeviceInfo` (which Home Assistant uses to point a device's "Visit Device"
link at it) when the official `web_server` component is compiled in — `dali`
can't inject that component with a custom port on the user's behalf (AUTO_LOAD
only auto-creates components with default config). Given that, the dashboard
registers itself (`DaliWebDashboard::begin()`) onto the *shared*
`web_server_base` instance instead of opening its own `AsyncWebServer`, at
`DaliBusComponent`'s `HARDWARE` setup priority — earlier than `web_server`'s
own (`WIFI - 1`) — so it's first in the handler list and `canHandle()`
(always `true`) shadows the stock web_server UI entirely at every path; the
stock UI is compiled in (extra flash) but never actually reachable. Registering
early is safe because `web_server_base::add_handler()` just queues the handler
until the underlying socket is actually opened later by `web_server`'s own
`setup()`, well after the network stack is up.

Endpoints:
- `GET /api/lamps` — per-lamp status/control state (`addr`, `name`, `online`,
  `problem`, `on`, `brightness_pct`, `color_temp_mireds`, `groups`), plus a
  top-level `groups` array (one entry per group with an active "DALI Group N"
  light: `group`, `name`, `on`, `brightness_pct`, `color_temp_mireds`). For
  lamps/groups that support color temperature, `color_temp_min_mireds` and
  `color_temp_max_mireds` give that light's configured CT range (from
  `cold_white_temperature`/`warm_white_temperature`), used by the dashboard to
  size the CT slider per device instead of a fixed range.
- `POST /api/group` — add/remove a lamp's group membership (unchanged).
- `POST /api/scene` — recall/store/remove a scene for a target
  (`all`/`group:N`/`lamp:N`, unchanged).
- `POST /api/identify` — blink a lamp to identify it (unchanged).
- `POST /api/lamp?target=lamp:N|group:N&on=0|1&brightness_pct=0-100&color_temp_mireds=N` —
  direct control of any lamp or group light. At least one of
  `on`/`brightness_pct`/`color_temp_mireds` is required. Goes through
  `DaliLight::perform_call()` -> `light::LightState::make_call()`, so the
  normal debounce/coalescing and HA sync apply.
- `GET /api/names` / `POST /api/names?kind=lamp|group|scene&index=N&name=...` —
  dashboard-only display names (independent of HA entity names), persisted to
  flash via `m_names_pref_` (key `0xDA111102u`, distinct from the inventory
  key `0xDA111101u`). An empty `name` clears the entry. `GET` returns a sparse
  `{"lamps": {"N": "..."}, "groups": {...}, "scenes": {...}}` object, omitting
  unset entries.
- `POST /api/discovery?mode=discover|unassigned|all[&confirm=yes]` — runs
  `run_discovery()` with `DaliInitMode::{DiscoverOnly,InitializeUnassigned,InitializeAll}`.
  `mode=all` re-initializes every short address and requires `&confirm=yes` as
  a server-side safety net (the dashboard UI also shows a confirm dialog).
- `GET /api/log` — `{"now_ms", "counters": {"errors","bus_down","collisions","disconnected","online"}, "events": [...]}`.
  `events` is the in-RAM bus event log (`m_event_log_`, a 32-entry ring
  buffer, `dali_event_log::RingBuffer`), oldest-first, lost on reboot like the
  diagnostic counters. Each event has `ts` (ms since boot), `type` (e.g.
  `"bus_down"`, `"lamp_unavailable"`), `addr` (`null` if not lamp-specific),
  and `value`.

### Extracting pure logic for host testing

ESPHome-side decision logic that doesn't need ESPHome types is pulled out into
small `dali_*.h` headers under `components/dali/` (e.g. `dali_debounce.h`,
`dali_availability.h`, `dali_bus_health.h`, `dali_inventory.h`,
`dali_names.h`, `dali_event_log.h`), each in its own namespace
(`dali_debounce`, `dali_availability`, etc. — never a top-level `dali`
namespace, which collides with `esphome::dali`). The ESPHome `.cpp`
calls into these helpers and keeps only the ESPHome-specific glue (state
members, logging, sensor publishing); each header has a matching
`tests/test_*.cpp` with table-driven cases against `framework.h`, registered
in `tests/Makefile`'s `SOURCES`. This mirrors the existing
`dali_tx_collision.h`/`dali_tx` pattern below — follow it for any new pure
logic so it's covered by the host suite.

### Collision avoidance

`dali_tx_collision.h` contains pure, framework-agnostic helpers, wired into
`writeBit()` in `esphome_dali.cpp` via `check_collision_()` (gated on
`input_devices: true`) that detect when another master is driving the
wired-AND bus during a half-bit where this device is releasing it — per the
IEC 62386-101 multi-master collision rules. Each half-bit's delay is split
around a single passive `digital_read()` so the total Manchester bit period is
unchanged. Detected collisions increment the "DALI Collisions" diagnostic
sensor (`expose_bus_diagnostics`). These helpers are unit-tested on the host
in `tests/test_collision.cpp`.

### Circadian color temperature

For groups listed in `circadian_groups`, color temperature is gradually
shifted between a `day_color_temperature` and `night_color_temperature`
(Kelvin or mireds, via `cv.color_temperature`) based on the current sun
elevation, read from an ESPHome `sun:` component referenced by `sun_id`. If
`sun_id` is not set, `circadian_groups` must be empty and the feature is
entirely inert.

`dali_circadian.h` contains the pure logic: `compute_color_temp_mireds()`
linearly interpolates between `night_color_temperature` (at/below
`night_elevation`, default `-4`) and `day_color_temperature` (at/above
`day_elevation`, default `6`), clamping at both ends; and
`circadian_enabled()`/`circadian_set()` manage a `uint16_t` per-group enabled
bitmask (`DaliCircadianBlob`, persisted to flash at key `0xDA111103`,
distinct from the inventory `0xDA111101` and dashboard-names `0xDA111102`
keys).

Each `circadian_groups` entry gets an auto-created "DALI Group N Circadian"
switch (`ENTITY_CATEGORY_CONFIG`) for runtime enable/disable, restored from
flash on boot. `update_circadian_()` runs every 60s (`DALI_CIRCADIAN_UPDATE_MS`)
from `loop()`: for each enabled group whose group light is currently on, it
recomputes the target mireds from `m_sun_->elevation()` and re-applies via
`DaliLight::perform_call()` (so normal debounce/coalescing and HA sync apply)
if it differs from the last-applied value. Whenever a group is off, its
"last applied" sentinel (`m_circadian_last_applied_mireds_`) is reset to none,
so the next time it's switched on, the first `update_circadian_()` tick
re-applies the circadian value promptly rather than waiting for it to drift.

### Reliability / error recovery

`esphome_dali.cpp`/`.h` track per-lamp availability and problem state, exposed
as a combined "Status" binary_sensor (`expose_problem`), a bus-online sensor
(`expose_bus_status`), and persist the discovered address inventory to flash
(`persist_inventory`) so entities survive reboots and brief bus outages.
`power_on_level`/`system_failure_level` configure DALI-side recovery levels
sent to control gear. `receiveBackwardFrame()` treats an RX line that's
already high at the start of the wait as an immediate NACK (disconnected/
floating bus) rather than misreading it as a phantom reply — this avoids a
discovery runaway loop and task-watchdog reboot when the bus is disconnected.

`expose_bus_diagnostics` (default `true`) adds software-only line/PSU
diagnostics built from these existing signals: lifetime "DALI Bus Errors" /
"DALI Bus Down Events" counters (incremented in `loop()`'s poll block and
`update_bus_health_()`), a "DALI Bus Disconnected" binary_sensor mirroring the
RX-stuck-high case above, and (with `input_devices: true`) the "DALI
Collisions" counter from the collision detector described above.
