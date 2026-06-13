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
```

`esphome-web-ec5578.yaml` references `!secret wifi_ssid`/`wifi_password` from
a gitignored `secrets.yaml` — never commit real credentials.

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

If you add a new kind of runtime-created entity, add a matching `_COUNT`
constant and `register_platform_component` call here, or it will be silently
capped.

### Web dashboard

`esphome_dali_web.h/.cpp` implements `DaliWebDashboard`, a small
`AsyncWebHandler` (via `web_server_base`) serving a single embedded HTML/JS
page plus `/api/lamps`, `/api/group`, `/api/scene`, `/api/identify` endpoints.
GET handlers read cached state only. POST handlers (group changes, scene
actions, identify) enqueue a pending action that is drained on the next
`DaliBusComponent::loop()` iteration, because the bit-banged DALI bus is not
thread-safe and must only be touched from the main loop.

### Collision avoidance

`dali_tx_collision.h` contains pure, framework-agnostic helpers (used by
`writeBit()` in `esphome_dali.cpp`, gated on `input_devices: true`) that
detect when another master is driving the wired-AND bus during a half-bit
where this device is releasing it — per the IEC 62386-101 multi-master
collision rules. These helpers are unit-tested on the host in
`tests/test_collision.cpp`.

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
