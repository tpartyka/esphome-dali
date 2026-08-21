from typing import OrderedDict
from esphome import automation, pins
from esphome.components import sun, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import (
    CONF_ID, CONF_RX_PIN, CONF_TX_PIN, CONF_DISCOVERY, CONF_TRIGGER_ID, CONF_GROUP, CONF_PORT,
)
from esphome.core import CORE
from esphome.core.entity_helpers import register_device_class

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv

AUTO_LOAD = ["light", "output", "button", "number", "binary_sensor", "sensor", "switch", "web_server_base", "json"]

CONF_DALI_BUS = 'dali_bus'
CONF_INITIALIZE_ADDRESSES = 'initialize_addresses'
CONF_INPUT_DEVICES = 'input_devices'
CONF_ON_INPUT_FRAME = 'on_input_frame'
CONF_MAX_LIGHTS = 'max_lights'
CONF_STATE_POLL_INTERVAL = 'state_poll_interval'
CONF_DEFAULT_FADE_IN_TIME = 'default_fade_in_time'
CONF_DEFAULT_FADE_OUT_TIME = 'default_fade_out_time'
CONF_POWER_ON_LEVEL = 'power_on_level'
CONF_SYSTEM_FAILURE_LEVEL = 'system_failure_level'
CONF_EXPOSE_PROBLEM = 'expose_problem'
CONF_EXPOSE_BUS_STATUS = 'expose_bus_status'
CONF_EXPOSE_BUS_DIAGNOSTICS = 'expose_bus_diagnostics'
CONF_PERSIST_INVENTORY = 'persist_inventory'
CONF_EXPOSE_GROUPS = 'expose_groups'
CONF_MAX_GROUPS = 'max_groups'
CONF_EXPOSE_SCENES = 'expose_scenes'
CONF_EXPOSE_DASHBOARD = 'expose_dashboard'
CONF_DASHBOARD_PORT = 'dashboard_port'
CONF_SUN_ID = 'sun_id'
CONF_CIRCADIAN_DAY_ELEVATION = 'day_elevation'
CONF_CIRCADIAN_NIGHT_ELEVATION = 'night_elevation'
CONF_CIRCADIAN_GROUPS = 'circadian_groups'
CONF_DAY_COLOR_TEMPERATURE = 'day_color_temperature'
CONF_NIGHT_COLOR_TEMPERATURE = 'night_color_temperature'


def _validate_dali_level(value):
    # Accept 'last' (255 = keep last level), 'off' (0), or a raw 0..255 level.
    if isinstance(value, str):
        v = value.strip().lower()
        if v in ('last', 'mask'):
            return 255
        if v == 'off':
            return 0
    return cv.int_range(min=0, max=255)(value)

# A DALI bus addresses up to 64 control gears (short addresses 0-63), so that is
# the natural upper bound for lights discovered at runtime.
DALI_MAX_SHORT_ADDRESSES = 64
# We auto-create exactly two buttons (discovery + reboot) with no YAML.
DALI_DYNAMIC_BUTTON_COUNT = 2
# ...and two number entities (fade in + fade out time).
DALI_DYNAMIC_NUMBER_COUNT = 2
# A DALI bus has 16 groups (0-15), the upper bound for auto-discovered group lights.
DALI_MAX_GROUPS = 16
# Scene controls: 16 recall buttons + store + clear = 18 buttons, and 1 scene-number.
DALI_SCENE_BUTTON_COUNT = 18
DALI_SCENE_NUMBER_COUNT = 1
# Group-membership controls: Add + Remove buttons, and target-address + group-number.
DALI_GROUP_BUTTON_COUNT = 2
DALI_GROUP_NUMBER_COUNT = 2
# Bus diagnostics: "DALI Bus Errors" + "DALI Bus Down Events" counters, plus
# "DALI Collisions" (only when input_devices is enabled).
DALI_DIAG_SENSOR_COUNT = 2
DALI_DIAG_COLLISION_SENSOR_COUNT = 1
# Bus diagnostics: "DALI Bus Disconnected" binary_sensor.
DALI_DIAG_BINARY_SENSOR_COUNT = 1

dali_ns = cg.esphome_ns.namespace('dali')
dali_lib_ns = cg.global_ns
DaliBusComponent = dali_ns.class_('DaliBusComponent', cg.Component)
DaliInputFrame = dali_ns.struct('DaliInputFrame')
DaliInputFrameTrigger = dali_ns.class_('DaliInputFrameTrigger', automation.Trigger.template(DaliInputFrame))

DaliInitMode = dali_ns.enum('DaliInitMode', is_class=True)
INIT_MODES = {
    'none': DaliInitMode.DiscoverOnly,
    'unassigned': DaliInitMode.InitializeUnassigned,
    'all': DaliInitMode.InitializeAll,
}

def _validate_init_mode(value):
    # Back-compat: `true` -> only unassigned devices, `false` -> discover only.
    if isinstance(value, bool):
        return 'unassigned' if value else 'none'
    return cv.one_of(*INIT_MODES, lower=True)(value)

def _final_validate_dashboard(config):
    # expose_dashboard's "Visit Device" link in Home Assistant only works if the
    # device reports a webserver_port over the API, which ESPHome's API only does
    # when the official `web_server:` component is compiled in (USE_WEBSERVER).
    # dali can't inject a `web_server:` block with a custom port on the user's
    # behalf (ESPHome's AUTO_LOAD only auto-creates components with default
    # config), so require the user to configure it explicitly, sharing dali's
    # dashboard_port -- the dali dashboard then registers itself onto that same
    # shared web_server_base instance instead of opening a second server.
    if not config[CONF_EXPOSE_DASHBOARD] or not CORE.is_esp32:
        return config
    web_server_conf = fv.full_config.get().get('web_server')
    if web_server_conf is None:
        raise cv.Invalid(
            f"'{CONF_EXPOSE_DASHBOARD}' requires a 'web_server:' component (with "
            f"'port: {config[CONF_DASHBOARD_PORT]}') so Home Assistant's 'Visit Device' "
            f"link can reach the DALI cockpit -- see CLAUDE.md's web dashboard section."
        )
    web_server_port = web_server_conf.get(CONF_PORT)
    if web_server_port != config[CONF_DASHBOARD_PORT]:
        raise cv.Invalid(
            f"'web_server:'s port ({web_server_port}) must match '{CONF_DASHBOARD_PORT}' "
            f"({config[CONF_DASHBOARD_PORT]}) so both serve the DALI cockpit on the same port."
        )
    return config

FINAL_VALIDATE_SCHEMA = _final_validate_dashboard

def _validate_circadian(config):
    groups = config[CONF_CIRCADIAN_GROUPS]
    if groups and CONF_SUN_ID not in config:
        raise cv.Invalid(f"'{CONF_CIRCADIAN_GROUPS}' requires '{CONF_SUN_ID}' to be set")
    seen = set()
    for entry in groups:
        g = entry[CONF_GROUP]
        if g in seen:
            raise cv.Invalid(f"Duplicate {CONF_GROUP} {g} in '{CONF_CIRCADIAN_GROUPS}'")
        seen.add(g)
    return config

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(DaliBusComponent),
    # RX must be interrupt-capable for the input-device listener.
    cv.Required(CONF_RX_PIN): pins.internal_gpio_input_pin_schema,
    cv.Required(CONF_TX_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_DISCOVERY): cv.All(cv.requires_component("light"), cv.boolean),
    cv.Optional(CONF_INITIALIZE_ADDRESSES, default='none'): _validate_init_mode,
    # Capacity reserved for lights created at runtime by discovery. ESPHome 2026.x
    # stores entities in fixed-capacity StaticVectors sized by this codegen count;
    # any push_back past capacity is silently dropped, so this must be >= the number
    # of lamps discovery will create or only that many appear in Home Assistant.
    cv.Optional(CONF_MAX_LIGHTS, default=DALI_MAX_SHORT_ADDRESSES):
        cv.int_range(min=1, max=DALI_MAX_SHORT_ADDRESSES),
    # How often to poll each lamp's real state so Home Assistant reflects changes
    # made outside ESPHome (broadcast commands, another controller). 0s disables it.
    cv.Optional(CONF_STATE_POLL_INTERVAL, default='15s'): cv.positive_time_period_milliseconds,
    # Initial fade in/out times (also adjustable live via the HA "DALI Fade In/Out
    # Time" number entities, in milliseconds). Snapped to the nearest DALI step.
    cv.Optional(CONF_DEFAULT_FADE_IN_TIME, default='1s'): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_DEFAULT_FADE_OUT_TIME, default='1s'): cv.positive_time_period_milliseconds,
    # Power-failure recovery: where lamps go when their mains returns
    # (power_on_level) or when the DALI bus fails (system_failure_level).
    # 'last' keeps the level from before the outage; 'off' = 0; or a raw 0..254.
    cv.Optional(CONF_POWER_ON_LEVEL, default='last'): _validate_dali_level,
    cv.Optional(CONF_SYSTEM_FAILURE_LEVEL, default='last'): _validate_dali_level,
    # Create a diagnostic "Status" binary_sensor per discovered lamp: ON when the
    # lamp is unavailable or reports the DALI STATUS lampFailure bit (IEC 62386-102).
    cv.Optional(CONF_EXPOSE_PROBLEM, default=True): cv.boolean,
    # Create a single "DALI Bus Online" connectivity sensor (bus-down detection).
    cv.Optional(CONF_EXPOSE_BUS_STATUS, default=True): cv.boolean,
    # Create software-only line diagnostics: "DALI Bus Errors" and "DALI Bus Down
    # Events" counters, a "DALI Bus Disconnected" binary_sensor (RX stuck high, i.e.
    # physically disconnected/no PSU, vs. devices present but not answering), and
    # (when input_devices is enabled) a "DALI Collisions" counter from the
    # multi-master collision detector.
    cv.Optional(CONF_EXPOSE_BUS_DIAGNOSTICS, default=True): cv.boolean,
    # Persist the discovered short-address inventory to flash, so lamp entities exist
    # at boot even if the bus is briefly unreachable, and removed lamps are tracked as
    # "missing" (unavailable) instead of vanishing. Only used when discovery is on.
    cv.Optional(CONF_PERSIST_INVENTORY, default=True): cv.boolean,
    # Auto-discover DALI group membership (QUERY GROUPS) and expose one optimistic
    # "DALI Group N" light per active group. Only used when discovery is on.
    cv.Optional(CONF_EXPOSE_GROUPS, default=True): cv.boolean,
    cv.Optional(CONF_MAX_GROUPS, default=DALI_MAX_GROUPS):
        cv.int_range(min=1, max=DALI_MAX_GROUPS),
    # Expose 16 "DALI Scene N" recall buttons plus a scene-number selector and
    # store/clear buttons (broadcast scene recall/store, scenes 0-15).
    cv.Optional(CONF_EXPOSE_SCENES, default=True): cv.boolean,
    # Serve a small built-in web dashboard (lamp list/status, group assignment,
    # scene recall/store/remove) on dashboard_port. ESP32 only. Registers itself
    # as a handler on the shared web_server_base instance (taking priority over
    # the stock web_server UI at '/') instead of opening its own server, so the
    # device can report a webserver_port over the API -- see _final_validate_dashboard.
    cv.Optional(CONF_EXPOSE_DASHBOARD, default=False): cv.boolean,
    cv.Optional(CONF_DASHBOARD_PORT, default=8080): cv.port,
    cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    cv.Optional(CONF_INPUT_DEVICES, default=False): cv.boolean,
    cv.Optional(CONF_ON_INPUT_FRAME): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DaliInputFrameTrigger),
    }),
    # Circadian color temperature: requires a `sun:` component (astronomical
    # elevation) to determine time of day. If circadian_groups is empty (the
    # default), the feature is entirely inert.
    cv.Optional(CONF_SUN_ID): cv.use_id(sun.Sun),
    # Elevation (degrees) at/above which a circadian group uses its
    # day_color_temperature, and at/below which it uses its
    # night_color_temperature, linearly interpolated in between.
    cv.Optional(CONF_CIRCADIAN_DAY_ELEVATION, default=6.0): cv.float_,
    cv.Optional(CONF_CIRCADIAN_NIGHT_ELEVATION, default=-4.0): cv.float_,
    cv.Optional(CONF_CIRCADIAN_GROUPS, default=[]): cv.ensure_list(cv.Schema({
        cv.Required(CONF_GROUP): cv.int_range(min=0, max=DALI_MAX_GROUPS - 1),
        cv.Required(CONF_DAY_COLOR_TEMPERATURE): cv.color_temperature,
        cv.Required(CONF_NIGHT_COLOR_TEMPERATURE): cv.color_temperature,
    })),
}).extend(cv.COMPONENT_SCHEMA).add_extra(_validate_circadian)

async def to_code(config: OrderedDict):
    var = cg.new_Pvariable(config[CONF_ID])
    bus = await cg.register_component(var, config)

    # Input-device listener: enabled by `input_devices: true` or implicitly when an
    # on_input_frame automation is defined. Computed up front since it also affects
    # the bus-diagnostics sensor count below.
    input_devices_enabled = config[CONF_INPUT_DEVICES] or CONF_ON_INPUT_FRAME in config

    rx_pin = await cg.gpio_pin_expression(config[CONF_RX_PIN])
    cg.add(var.set_rx_pin(rx_pin))
    
    tx_pin = await cg.gpio_pin_expression(config[CONF_TX_PIN])
    cg.add(var.set_tx_pin(tx_pin))

    cg.add(var.set_state_poll_interval(config[CONF_STATE_POLL_INTERVAL]))
    cg.add(var.set_fade_in_ms(config[CONF_DEFAULT_FADE_IN_TIME]))
    cg.add(var.set_fade_out_ms(config[CONF_DEFAULT_FADE_OUT_TIME]))
    cg.add(var.set_power_on_level(config[CONF_POWER_ON_LEVEL]))
    cg.add(var.set_system_failure_level(config[CONF_SYSTEM_FAILURE_LEVEL]))
    cg.add(var.set_expose_problem(config[CONF_EXPOSE_PROBLEM]))
    cg.add(var.set_expose_bus_status(config[CONF_EXPOSE_BUS_STATUS]))
    cg.add(var.set_expose_bus_diagnostics(config[CONF_EXPOSE_BUS_DIAGNOSTICS]))
    cg.add(var.set_persist_inventory(config[CONF_PERSIST_INVENTORY]))
    cg.add(var.set_expose_groups(config[CONF_EXPOSE_GROUPS]))
    cg.add(var.set_max_groups(config[CONF_MAX_GROUPS]))
    cg.add(var.set_expose_scenes(config[CONF_EXPOSE_SCENES]))
    cg.add(var.set_dashboard_enabled(config[CONF_EXPOSE_DASHBOARD]))
    cg.add(var.set_dashboard_port(config[CONF_DASHBOARD_PORT]))
    if config[CONF_EXPOSE_DASHBOARD] and CORE.is_esp32:
        web_server_base_var = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
        cg.add(var.set_web_server_base(web_server_base_var))

    cg.add(var.set_circadian_day_elevation(config[CONF_CIRCADIAN_DAY_ELEVATION]))
    cg.add(var.set_circadian_night_elevation(config[CONF_CIRCADIAN_NIGHT_ELEVATION]))
    if CONF_SUN_ID in config:
        # Gates esphome_dali.{h,cpp}'s sun.h include + update_circadian_() body --
        # sun: isn't in AUTO_LOAD, so its files aren't in the build tree unless the
        # user configures it (only possible when sun_id is set), and the dali: side
        # must not reference the real esphome::sun::Sun type unconditionally.
        cg.add_define("DALI_CIRCADIAN_ENABLED")
        sun_comp = await cg.get_variable(config[CONF_SUN_ID])
        cg.add(var.set_sun(sun_comp))
    for entry in config[CONF_CIRCADIAN_GROUPS]:
        cg.add(var.add_circadian_group(
            entry[CONF_GROUP],
            round(entry[CONF_DAY_COLOR_TEMPERATURE]),
            round(entry[CONF_NIGHT_COLOR_TEMPERATURE]),
        ))

    # Register the device-class strings our runtime-created diagnostic binary_sensors
    # use, so Home Assistant renders the proper semantics. configure_entity_() only
    # honors the device-class index when USE_ENTITY_DEVICE_CLASS is defined; since we
    # create these entities in C++ (not from a YAML platform), define it here.
    uses_connectivity = config[CONF_EXPOSE_BUS_STATUS]
    if uses_connectivity or config[CONF_EXPOSE_PROBLEM]:
        cg.add_define("USE_ENTITY_DEVICE_CLASS")
    if uses_connectivity:
        cg.add(var.set_dc_index_connectivity(register_device_class("connectivity")))
    if config[CONF_EXPOSE_PROBLEM]:
        cg.add(var.set_dc_index_problem(register_device_class("problem")))

    # Runtime-created binary_sensors need a reserved slot each in the fixed-capacity
    # StaticVector (same rule as lights/buttons/numbers — see max_lights): one per lamp
    # for the "Status" sensor, plus one for the bus sensor.
    if config[CONF_EXPOSE_PROBLEM]:
        for _ in range(config[CONF_MAX_LIGHTS]):
            CORE.register_platform_component("binary_sensor", var)
    if config[CONF_EXPOSE_BUS_STATUS]:
        CORE.register_platform_component("binary_sensor", var)

    # Bus diagnostics: "DALI Bus Disconnected" binary_sensor, plus "DALI Bus Errors" /
    # "DALI Bus Down Events" sensors, and "DALI Collisions" if input devices are enabled.
    if config[CONF_EXPOSE_BUS_DIAGNOSTICS]:
        for _ in range(DALI_DIAG_BINARY_SENSOR_COUNT):
            CORE.register_platform_component("binary_sensor", var)
        for _ in range(DALI_DIAG_SENSOR_COUNT):
            CORE.register_platform_component("sensor", var)
        if input_devices_enabled:
            for _ in range(DALI_DIAG_COLLISION_SENSOR_COUNT):
                CORE.register_platform_component("sensor", var)

    # The component auto-creates the "Run DALI Discovery" and "Reboot" buttons with
    # no YAML (see create_discovery_button / create_reboot_button). Each
    # register_platform_component call both (a) makes the core define USE_BUTTON so
    # the code compiles in, and (b) reserves one slot in the fixed-capacity button
    # StaticVector. We create two buttons, so we must reserve two slots — otherwise
    # the second (Reboot) is silently dropped and never appears in Home Assistant.
    for _ in range(DALI_DYNAMIC_BUTTON_COUNT):
        CORE.register_platform_component("button", var)

    # Scene controls add 16 recall buttons + store + clear. Reserve their button slots.
    if config[CONF_EXPOSE_SCENES]:
        for _ in range(DALI_SCENE_BUTTON_COUNT):
            CORE.register_platform_component("button", var)

    # Group-membership controls add Add/Remove buttons. Reserve their button slots.
    if config[CONF_EXPOSE_GROUPS]:
        for _ in range(DALI_GROUP_BUTTON_COUNT):
            CORE.register_platform_component("button", var)

    # Two auto-created number entities: "DALI Fade In Time" and "DALI Fade Out Time".
    # Reserve a slot for each in the fixed-capacity number StaticVector (see max_lights).
    for _ in range(DALI_DYNAMIC_NUMBER_COUNT):
        CORE.register_platform_component("number", var)

    # Scene controls add a "DALI Scene Number" selector. Reserve its number slot.
    if config[CONF_EXPOSE_SCENES]:
        for _ in range(DALI_SCENE_NUMBER_COUNT):
            CORE.register_platform_component("number", var)

    # Group-membership controls add target-address + group-number selectors.
    if config[CONF_EXPOSE_GROUPS]:
        for _ in range(DALI_GROUP_NUMBER_COUNT):
            CORE.register_platform_component("number", var)

    # Circadian color temperature adds one "DALI Group N Circadian" switch per
    # configured group. Unlike the other _COUNT constants (fixed regardless of
    # YAML), this count is genuinely variable -- one slot per circadian_groups
    # entry.
    for _ in config[CONF_CIRCADIAN_GROUPS]:
        CORE.register_platform_component("switch", var)

    if config.get(CONF_DISCOVERY, False):
        cg.add(var.do_device_discovery())

        # Discovery creates light entities at runtime. ESPHome 2026.x sizes the
        # light StaticVector by this codegen count, so we must reserve a slot for
        # every lamp discovery might create (registering once only ever showed the
        # first lamp in Home Assistant). Reserving these slots also defines
        # USE_LIGHT so the light code compiles in even with no YAML `light:` block.
        for _ in range(config[CONF_MAX_LIGHTS]):
            CORE.register_platform_component("light", bus)

        # Auto-discovered group lights are additional light entities — reserve a slot
        # for each possible group on top of the per-lamp lights.
        if config[CONF_EXPOSE_GROUPS]:
            for _ in range(config[CONF_MAX_GROUPS]):
                CORE.register_platform_component("light", bus)

    init_mode = config[CONF_INITIALIZE_ADDRESSES]
    if init_mode != 'none':
        cg.add(var.do_initialize_addresses(INIT_MODES[init_mode]))

    if input_devices_enabled:
        cg.add(var.do_input_devices())

    for conf in config.get(CONF_ON_INPUT_FRAME, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(DaliInputFrame, 'x')], conf)
