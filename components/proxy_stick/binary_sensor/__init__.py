import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_DISABLED_BY_DEFAULT, DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC

from .. import CONF_PROXY_STICK_ID, ProxyStick

DEPENDENCIES = ["proxy_stick"]

CONF_UPSTREAM = "upstream"
CONF_USB_HOST = "usb_host"
CONF_USB_OTG_AT_POWER_ON = "usb_otg_at_power_on"


def _schema(icon):
    return binary_sensor.binary_sensor_schema(
        icon=icon, device_class=DEVICE_CLASS_CONNECTIVITY, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_PROXY_STICK_ID): cv.use_id(ProxyStick),
        # The server's event stream is live and its image validated.
        cv.Optional(CONF_UPSTREAM): _schema("mdi:server-network"),
        # A USB host has configured the drive.
        cv.Optional(CONF_USB_HOST): _schema("mdi:usb"),
        # USB_PHY_SEL is burnt: a host meets only the drive from power-on. Disabled in Home Assistant by default.
        cv.Optional(CONF_USB_OTG_AT_POWER_ON): binary_sensor.binary_sensor_schema(
            icon="mdi:fuse", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ).extend({cv.Optional(CONF_DISABLED_BY_DEFAULT, default=True): cv.boolean}),
    }
)


async def to_code(config):
    stick = await cg.get_variable(config[CONF_PROXY_STICK_ID])
    if CONF_UPSTREAM in config:
        cg.add(stick.set_upstream_sensor(await binary_sensor.new_binary_sensor(config[CONF_UPSTREAM])))
    if CONF_USB_OTG_AT_POWER_ON in config:
        cg.add(stick.set_otg_sensor(await binary_sensor.new_binary_sensor(config[CONF_USB_OTG_AT_POWER_ON])))
    if CONF_USB_HOST in config:
        cg.add(stick.set_usb_host_sensor(await binary_sensor.new_binary_sensor(config[CONF_USB_HOST])))
