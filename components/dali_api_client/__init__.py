from esphome import automation
from esphome.components import http_request
from esphome.const import CONF_ID

import esphome.codegen as cg
import esphome.config_validation as cv

DEPENDENCIES = ["http_request"]
AUTO_LOAD = ["light"]

CONF_BASE_URL = "base_url"
CONF_HTTP_REQUEST_ID = "http_request_id"


dali_api_client_ns = cg.esphome_ns.namespace("dali_api_client")
DaliApiClient = dali_api_client_ns.class_("DaliApiClient", cg.Component)
DaliApiRecallSceneAction = dali_api_client_ns.class_(
    "DaliApiRecallSceneAction", automation.Action
)


def validate_base_url(value):
    value = cv.url(value)
    if not value.startswith(("http://", "https://")):
        raise cv.Invalid("base_url must start with http:// or https://")
    return value.rstrip("/")


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DaliApiClient),
        cv.Required(CONF_HTTP_REQUEST_ID): cv.use_id(http_request.HttpRequestComponent),
        cv.Required(CONF_BASE_URL): validate_base_url,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    http = await cg.get_variable(config[CONF_HTTP_REQUEST_ID])
    cg.add(var.set_http_request(http))
    cg.add(var.set_base_url(config[CONF_BASE_URL]))


CONF_SCENE = "scene"
CONF_TARGET = "target"
CONF_ADDRESS = "address"
CONF_DALI_API_CLIENT_ID = "dali_api_client_id"

def validate_scene_target_address(config):
    address = config[CONF_ADDRESS]
    if config[CONF_TARGET] == "group" and isinstance(address, int) and address > 15:
        raise cv.Invalid("group target address must be in the range 0..15")
    return config


SCENE_ACTION_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_DALI_API_CLIENT_ID): cv.use_id(DaliApiClient),
            cv.Required(CONF_SCENE): cv.templatable(cv.int_range(min=0, max=15)),
            cv.Optional(CONF_TARGET, default="all"): cv.one_of("all", "lamp", "group", lower=True),
            cv.Optional(CONF_ADDRESS, default=0): cv.templatable(cv.int_range(min=0, max=63)),
        }
    ),
    validate_scene_target_address,
)


@automation.register_action(
    "dali_api_client.recall_scene", DaliApiRecallSceneAction, SCENE_ACTION_SCHEMA, synchronous=True
)
async def dali_api_client_recall_scene_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_DALI_API_CLIENT_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    scene = await cg.templatable(config[CONF_SCENE], args, cg.uint8)
    address = await cg.templatable(config[CONF_ADDRESS], args, cg.uint8)
    cg.add(var.set_scene(scene))
    cg.add(var.set_address(address))
    cg.add(var.set_target_all(config[CONF_TARGET] == "all"))
    cg.add(var.set_target_group(config[CONF_TARGET] == "group"))
    return var
