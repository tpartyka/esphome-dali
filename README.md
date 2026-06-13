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
  initialize_addresses: true

  # --- Optional settings (all shown with their defaults) ---

  # How often to poll each lamp's actual state, so Home Assistant reflects
  # changes made outside ESPHome (broadcast commands, another controller).
  # 0s disables polling.
  state_poll_interval: 15s

  # Initial fade in/out times (also adjustable live via the "DALI Fade In/Out
  # Time" number entities), in milliseconds.
  default_fade_in_time: 1s
  default_fade_out_time: 1s

  # Where lamps go when mains power returns, or when the DALI bus itself
  # fails. 'last' keeps the previous level, 'off', or a raw 0..254 level.
  power_on_level: last
  system_failure_level: last

  # Diagnostic "online"/"problem" binary_sensors per lamp, and a "DALI Bus
  # Online" connectivity sensor.
  expose_availability: true
  expose_problem: true
  expose_bus_status: true

  # Persist the discovered address inventory to flash, so lamp entities exist
  # at boot even if the bus is briefly unreachable.
  persist_inventory: true

  # Auto-discover group membership and expose one "DALI Group N" light per
  # active group (0-15).
  expose_groups: true
  max_groups: 16

  # Expose 16 "DALI Scene N" recall buttons plus a scene-number selector and
  # store/clear buttons (scenes 0-15).
  expose_scenes: true

  # Serve a small built-in web dashboard (see "Web Dashboard" below). ESP32 only.
  expose_dashboard: false
  dashboard_port: 8080

  # Enable the push-button/input-device listener and multi-master collision
  # avoidance on the bit-banged TX (see on_input_frame below).
  input_devices: false
```

![HomeAssistant Discovery](doc/ha-discovery.png)

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

- **Availability & problem sensors** (`expose_availability`, `expose_problem`):
  each discovered lamp gets diagnostic "online" and "problem" binary_sensors,
  the latter driven by the DALI STATUS `lampFailure` bit.
- **Bus status** (`expose_bus_status`): a single "DALI Bus Online" connectivity
  sensor reflects whether the DALI bus itself is responding.
- **Persisted inventory** (`persist_inventory`): the discovered short-address
  inventory is saved to flash, so lamp entities exist at boot (as
  "unavailable" if missing) even before the bus has been re-polled.
- **Power-failure recovery** (`power_on_level`, `system_failure_level`):
  controls what level a lamp returns to after mains power returns or after a
  DALI bus failure — `last` (keep previous level), `off`, or a raw `0..254`
  level.

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
