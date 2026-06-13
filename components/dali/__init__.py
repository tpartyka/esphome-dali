from typing import OrderedDict
from esphome import automation, pins
from esphome.const import CONF_ID, CONF_RX_PIN, CONF_TX_PIN, CONF_DISCOVERY, CONF_TRIGGER_ID
from esphome.core import CORE

import esphome.codegen as cg
import esphome.config_validation as cv

AUTO_LOAD = ["light", "output", "button"]

CONF_DALI_BUS = 'dali_bus'
CONF_INITIALIZE_ADDRESSES = 'initialize_addresses'
CONF_INPUT_DEVICES = 'input_devices'
CONF_ON_INPUT_FRAME = 'on_input_frame'
CONF_MAX_LIGHTS = 'max_lights'

# A DALI bus addresses up to 64 control gears (short addresses 0-63), so that is
# the natural upper bound for lights discovered at runtime.
DALI_MAX_SHORT_ADDRESSES = 64
# We auto-create exactly two buttons (discovery + reboot) with no YAML.
DALI_DYNAMIC_BUTTON_COUNT = 2

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
    cv.Optional(CONF_INPUT_DEVICES, default=False): cv.boolean,
    cv.Optional(CONF_ON_INPUT_FRAME): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(DaliInputFrameTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config: OrderedDict):
    var = cg.new_Pvariable(config[CONF_ID])
    bus = await cg.register_component(var, config)

    rx_pin = await cg.gpio_pin_expression(config[CONF_RX_PIN])
    cg.add(var.set_rx_pin(rx_pin))
    
    tx_pin = await cg.gpio_pin_expression(config[CONF_TX_PIN])
    cg.add(var.set_tx_pin(tx_pin))

    # The component auto-creates the "Run DALI Discovery" and "Reboot" buttons with
    # no YAML (see create_discovery_button / create_reboot_button). Each
    # register_platform_component call both (a) makes the core define USE_BUTTON so
    # the code compiles in, and (b) reserves one slot in the fixed-capacity button
    # StaticVector. We create two buttons, so we must reserve two slots — otherwise
    # the second (Reboot) is silently dropped and never appears in Home Assistant.
    for _ in range(DALI_DYNAMIC_BUTTON_COUNT):
        CORE.register_platform_component("button", var)

    if config.get(CONF_DISCOVERY, False):
        cg.add(var.do_device_discovery())

        # Discovery creates light entities at runtime. ESPHome 2026.x sizes the
        # light StaticVector by this codegen count, so we must reserve a slot for
        # every lamp discovery might create (registering once only ever showed the
        # first lamp in Home Assistant). Reserving these slots also defines
        # USE_LIGHT so the light code compiles in even with no YAML `light:` block.
        for _ in range(config[CONF_MAX_LIGHTS]):
            CORE.register_platform_component("light", bus)

    init_mode = config[CONF_INITIALIZE_ADDRESSES]
    if init_mode != 'none':
        cg.add(var.do_initialize_addresses(INIT_MODES[init_mode]))

    # Input-device listener: enabled by `input_devices: true` or implicitly when an
    # on_input_frame automation is defined.
    if config[CONF_INPUT_DEVICES] or CONF_ON_INPUT_FRAME in config:
        cg.add(var.do_input_devices())

    for conf in config.get(CONF_ON_INPUT_FRAME, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(DaliInputFrame, 'x')], conf)
