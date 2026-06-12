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

    # The component auto-creates a "Run DALI Discovery" button with no YAML (see
    # create_discovery_button). Registering the platform here makes the core define
    # USE_BUTTON so that code is compiled in, the same way the discovery branch below
    # does for USE_LIGHT.
    CORE.register_platform_component("button", var)

    if config.get(CONF_DISCOVERY, False):
        cg.add(var.do_device_discovery())

        # When discovery is enabled but no light components are defined
        # in the YAML, we need to make it look like we have a light 
        # defined so it will compile in support. Without this, USE_LIGHT
        # will not be defined.
        #
        # This can be done by registering this bus component as a light,
        # making the core think there is at least one light defined.
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
