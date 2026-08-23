# ESPHome DALI Master Component

This component implements a DALI master that can talk to devices on a DALI bus.




## Supported Features:

- Automatic device discovery & address assignment
  - New devices are assigned the lowest free short address, avoiding collisions
- DALI dimmer support
  - brightness control
  - colour temperature control
- Broadcast, group, or short address
- Query device capabilities 
  - `dali.light` component automatically enables colour temperature if device reports the capability
- DALI parameter configuration
  - Fade rate/time
  - Brightness curve (log/linear)
- Groups & scenes
  - Auto-discovered group membership, exposed as "DALI Group N" lights
  - Scene recall/store/remove buttons (scenes 0-15)
- Reliability & error recovery
  - Per-lamp "online"/"problem" diagnostic sensors and a bus-connectivity sensor
  - Persisted device inventory, so entities survive reboots and brief bus outages
  - Configurable power-on and system-failure recovery levels
- Multi-master collision avoidance/detection on the bit-banged bus (when `input_devices` is enabled)
- Built-in web dashboard for lamp/group/scene control and lamp identification

## Usage:

At first, load the [external component](https://esphome.io/components/external_components/#external-components) from github:
```yaml
external_components:
  - source: github://jorticus/esphome-dali@master
    components: [dali]
```

Lights can be automatically discovered on the bus by simply defining a dali bus component:

```yaml
# bit-banged 1200 baud DALI bus
dali:
  id: dali_bus
  tx_pin: 4
  rx_pin: 12

  # Lights will be created for each detected DALI light device
  discovery: true

  # Devices will be automatically assigned a short address if they do not have one
  initialize_addresses: unassigned
```

![HomeAssistant Discovery](doc/ha-discovery.png)

### `dali:` configuration reference

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `id` | ID | auto-generated | Optional, but recommended if you reference this bus from `light:`/`output:` platform entries or multiple buses are defined. |
| `tx_pin` | pin (output) | **required** | GPIO driving the DALI bus TX line. |
| `rx_pin` | pin (input) | **required** | GPIO reading the DALI bus RX line. The component expects the conventional polarity: HIGH while the bus is idle and LOW while it is asserted. Must be an interrupt-capable GPIO — required for the input-device listener (`input_devices`/`on_input_frame`), even if you don't enable it. For an interface with the opposite polarity, use `inverted: true`. |
| `debug_frames` | bool | `false` | Log every transmitted forward frame (`TX: aa dd`) and received backward frame (`RX: dd`) at DEBUG level. Also logs NACK and malformed replies. Enable `logger: logs: dali: DEBUG`. |
| `discovery` | bool | `false` | Scan the bus and create a `light` entity for each detected DALI device (and group/scene entities, if enabled). Requires the `light` component. |
| `initialize_addresses` | `none` \| `unassigned` \| `all` | `none` | Auto-assign short addresses. `unassigned` only assigns devices with no address yet; `all` re-initializes every device (disruptive — only use once when (re)commissioning a bus). `true`/`false` are accepted as legacy aliases for `unassigned`/`none`. |
| `max_lights` | int, 1-64 | `64` | Reserved entity slots for lamps `discovery` may create. ESPHome sizes the light `StaticVector` at codegen time from this value — if discovery finds more lamps than this, the extras are silently dropped. Lower it only if you know your bus has fewer than 64 possible short addresses and want to save RAM. |
| `state_poll_interval` | time | `15s` | How often to poll each lamp's actual on/off/brightness, so Home Assistant reflects changes made outside ESPHome (broadcast commands, another controller, scene recalls). The interval is divided across all pollable lamps. `0s` disables polling. |
| `default_fade_in_time` | time | `1s` | Initial fade time used when turning a light on or increasing brightness. Adjustable live via the "DALI Fade In Time" number entity. Snapped to the nearest DALI fade-time step. |
| `default_fade_out_time` | time | `1s` | Initial fade time used when turning a light off or decreasing brightness. Adjustable live via the "DALI Fade Out Time" number entity. |
| `power_on_level` | `last` \| `off` \| `0`-`254` | `last` | Level a lamp returns to when mains power returns after an outage (DALI `POWER ON LEVEL`). `last` keeps the level from before the outage. |
| `system_failure_level` | `last` \| `off` \| `0`-`254` | `last` | Level a lamp goes to if it loses DALI bus communication (DALI `SYSTEM FAILURE LEVEL`). |
| `expose_problem` | bool | `true` | Create a diagnostic "Status" `binary_sensor` per discovered lamp: ON when the lamp is unavailable or reports the DALI `STATUS` lamp-failure bit, OFF when OK. |
| `expose_bus_status` | bool | `true` | Create a single "DALI Bus Online" connectivity `binary_sensor` reflecting whether the bus itself is responding. |
| `expose_bus_diagnostics` | bool | `true` | Create software-only diagnostic entities: "DALI Poll Misses" and "DALI Bus Down Events" lifetime counters, a "DALI RX Stuck Low" `binary_sensor`, and (when `input_devices` is enabled) a "DALI Collisions" counter. The legacy object IDs are retained for existing Home Assistant entity registrations. |
| `persist_inventory` | bool | `true` | Persist the discovered short-address inventory to flash, so lamp entities exist at boot (as "unavailable" if missing) even before the bus has been re-polled. Only used when `discovery` is enabled. |
| `expose_groups` | bool | `true` | Auto-discover DALI group membership and expose one optimistic "DALI Group N" light per active group. Only used when `discovery` is enabled. |
| `max_groups` | int, 1-16 | `16` | Maximum number of group lights (`0`..`max_groups - 1`) `expose_groups` may create. Reserves the matching number of entity slots. |
| `expose_scenes` | bool | `true` | Expose 16 "DALI Scene N" recall buttons (scenes 0-15), plus a scene-number selector and store/clear buttons. These always target the broadcast address — use the web dashboard's scene panel for group/lamp-targeted scenes. |
| `expose_dashboard` | bool | `false` | Serve the built-in web dashboard (see "Web Dashboard" below) on `dashboard_port`. ESP32 only. |
| `dashboard_port` | port | `8080` | TCP port for the web dashboard, when `expose_dashboard: true`. |
| `input_devices` | bool | `false` | Enable the push-button/input-device listener and multi-master collision avoidance/detection on the bit-banged TX. Automatically enabled if `on_input_frame` is set. |
| `on_input_frame` | automation trigger | — | Automation triggered for each frame captured from another DALI device (e.g. a push-button input device), with a `DaliInputFrame` struct (`short_address`, `instance_type`, `instance_number`, `button_event`, `event_info`, raw bits) as the trigger variable `x`. Implies `input_devices: true`. |

Example using `on_input_frame`:

```yaml
dali:
  id: dali_bus
  tx_pin: 4
  rx_pin: 12
  discovery: true
  on_input_frame:
    then:
      - logger.log:
          format: "DALI input: short_addr=%d instance=%d button_event=%d"
          args: [x.short_address, x.instance_number, x.button_event]
```

If you do not want to use automatic discovery, or want to customize a specific light,
you can specify the light component with an address like so:

```yaml
light:
- platform: dali
  id: dali_light
  name: "DALI Light"
  address: 0 # Short address, group address, or omit for broadcast
  restore_mode: RESTORE_DEFAULT_ON 

  # Set the brightness curve on the device
  brightness_curve: LOGARITHMIC # (default)

  # Force a specific color mode, irrespective of what the device claims.
  color_mode: COLOR_TEMPERATURE # (default: auto detect)

  # Update the fade time/rate on the device
  fade_time: 1s
  fade_rate: 44724  # steps/second
```

## Web Dashboard

Setting `expose_dashboard: true` serves a small built-in dashboard on
`dashboard_port` (default `8080`), e.g. `http://<device-ip>:8080/`. It shows
all discovered lamps with their online/problem status, on/off and brightness,
and group membership, and lets you:

- Add/remove a lamp's group membership
- Recall, store, or remove a scene, targeting all lamps, a group, or a single lamp
- "Identify" a lamp (briefly blink it) to find its physical location

The dashboard reads from cached state and enqueues any changes, which are
applied on the next main-loop iteration — it does not block or interfere with
bus polling/discovery.

## Reliability & Recovery

- **Status sensor** (`expose_problem`): each discovered lamp gets a diagnostic
  "Status" binary_sensor that turns on when the lamp is unavailable or reports
  the DALI STATUS `lampFailure` bit, and off when OK.
- **Bus status** (`expose_bus_status`): a single "DALI Bus Online" connectivity
  sensor reflects whether the DALI bus itself is responding.
- **Persisted inventory** (`persist_inventory`): the discovered short-address
  inventory is saved to flash, so lamp entities exist at boot (as
  "unavailable" if missing) even before the bus has been re-polled.
- **Power-failure recovery** (`power_on_level`, `system_failure_level`):
  controls what level a lamp returns to after mains power returns or after a
  DALI bus failure — `last` (keep previous level), `off`, or a raw `0..254`
  level.
- **Line/PSU diagnostics** (`expose_bus_diagnostics`): software-only
  diagnostics derived from existing bus-health signals (no extra hardware):
  - "DALI Poll Misses": lifetime counter of scheduled lamp polls that did
    not receive a presence response. It is not a general protocol-error count.
  - "DALI Bus Down Events": lifetime counter of bus online→offline
    transitions.
  - "DALI RX Stuck Low": ON when RX is low at the start of a reply window.
    This is an electrical-line heuristic, not a definitive disconnected-bus
    diagnosis.
  - "DALI Collisions" (only with `input_devices: true`): lifetime counter of
    multi-master collisions detected on the bit-banged TX (IEC 62386-101),
    sampled passively while this device releases the bus.

## Future Work:

- [X] Support scenes & groups
- [X] Allow configuration of DALI device parameters
- [X] Automatic device discovery
- [X] Automatic address assignment
- [ ] Support for RGB(W) devices
- [ ] Hardware protocol support (no bit banging)

## Components

### dali

The main dali copmonent implements the low level bus interface via bit-bang protocol.

The dali bus implements a simple bit-banged protocol, but you can extend it with a better implementation
for your platform (eg, DMA, interrupt based).

```yaml
dali:
  tx_pin: <transmit pin>
  rx_pin: <receive pin>
```

### dali.light

The dali light component supports both specific device addressing (short address), addressing groups, and broadcast.

NOTE: Querying capabilities is only supported if a specific device address is provided.

If an address is provided, it will query the device for its capabilities, and enable the appropriate
features such as colour temperature control.

```yaml
light:
- platform: dali
  address: <short address, group address, or broadcast(0xFF)>

  # For UI display purposes only:
  cold_white_color_temperature: 6000K
  warm_white_color_temperature: 2700K
```

### dali.output

The dali output component implements a float output that broadcasts the value as a brightness level

```yaml
output:
  - platform: dali
```

It has no configurable parameters.

## Wiring

The minimum viable schematic for interfacing with the bus looks like this:

![alt text](doc/schematic.png)

This is NOT compliant with the spec, but will work...

The spec requires 16V with a fast-response ~200mA current limiter implemented with a BJT current source,
and dual opto-isolators. However since the bus is connected to a non-isolated power supply, we can get away without opto-isolation, except as convenience for converting the DALI voltage levels to a safe 3.3 logic level.

There are other ways to implement the current limiter too, as long as they have a quick response time.
I succsessfully used an opamp current limiting circuit as I didn't have an LM317 or BJT on hand. 
It has been noted elsewhere that if using a LM317, it MUST be an on-brand chip, and preferably the `LM317DCYR` from Texas Instruments.

## Alternate Wiring

If you have a low-voltage DALI adapter and just want to wire 1:1 (no multi-bus architecture), you can actually bypass
all of the wiring requirements and connect RX->TX & TX->RX, using 3.3V logic levels, and the protocol will still work.

## Hardware Adapters

WaveShare sells a cheap DALI board for use with their ESP32-S3-Pico devboard, and is probably the easiest way to get up and going with this component:

https://www.waveshare.com/pico-dali2.htm

https://www.waveshare.com/wiki/Pico-DALI2

Let me know if you have success with this! I am using a custom design that is more or less the same as this, but also injects power onto the bus.

## Device Support

The following devices have been tested with this library:

- EOKE BK-DWL060-1500AD (63W CCT LED Driver, min brightness 86)
- LTECH LM-75-24-G2D2 (75W CCT LED Driver, 1000:1 dimming range)
- LTECH MT-100-650-D2D1-A1 (48VDC CCT LED Driver PCB Module)
