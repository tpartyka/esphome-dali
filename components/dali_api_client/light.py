from esphome.components import light
from esphome.const import (
    CONF_ADDRESS,
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_ID,
    CONF_OUTPUT_ID,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

import esphome.codegen as cg
import esphome.config_validation as cv

from . import DaliApiClient, dali_api_client_ns

DEPENDENCIES = ["dali_api_client"]

CONF_DALI_API_CLIENT_ID = "dali_api_client_id"
CONF_TARGET = "target"
CONF_SUPPORTS_COLOR_TEMPERATURE = "supports_color_temperature"

DaliApiLight = dali_api_client_ns.class_("DaliApiLight", light.LightOutput)


def validate_target_address(config):
    if config[CONF_TARGET] == "group" and config[CONF_ADDRESS] > 15:
        raise cv.Invalid("group target address must be in the range 0..15")
    return config


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(DaliApiLight),
            cv.GenerateID(CONF_DALI_API_CLIENT_ID): cv.use_id(DaliApiClient),
            cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=63),
            cv.Optional(CONF_TARGET, default="lamp"): cv.one_of("lamp", "group", lower=True),
            cv.Optional(CONF_SUPPORTS_COLOR_TEMPERATURE, default=False): cv.boolean,
            cv.Optional(CONF_COLD_WHITE_COLOR_TEMPERATURE, default="6500K"): cv.color_temperature,
            cv.Optional(CONF_WARM_WHITE_COLOR_TEMPERATURE, default="2700K"): cv.color_temperature,
        }
    ),
    validate_target_address,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DALI_API_CLIENT_ID])
    var = await light.new_light(config, parent)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_group(config[CONF_TARGET] == "group"))
    cg.add(var.set_supports_color_temperature(config[CONF_SUPPORTS_COLOR_TEMPERATURE]))
    if config[CONF_SUPPORTS_COLOR_TEMPERATURE]:
        cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
        cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
