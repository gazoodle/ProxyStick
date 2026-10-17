import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_DISABLED_BY_DEFAULT, ENTITY_CATEGORY_CONFIG

from .. import CONF_PROXY_STICK_ID, ProxyStick, proxy_stick_ns

DEPENDENCIES = ["proxy_stick"]

UsbSwitch = proxy_stick_ns.class_("UsbSwitch", switch.Switch, cg.Component, cg.Parented.template(ProxyStick))
FuseArmSwitch = proxy_stick_ns.class_("FuseArmSwitch", switch.Switch, cg.Parented.template(ProxyStick))
CONF_USB = "usb"
CONF_USB_PHY_FUSE_ARMED = "usb_phy_fuse_armed"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_PROXY_STICK_ID): cv.use_id(ProxyStick),
        # The removable medium on or off; the USB device stays attached. Remembered over restarts.
        cv.Optional(CONF_USB): switch.switch_schema(
            UsbSwitch,
            icon="mdi:usb-flash-drive",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="RESTORE_DEFAULT_ON",
        ).extend(cv.COMPONENT_SCHEMA),
        # Arms the USB PHY fuse button; always off after a restart, and turning it off cancels a scheduled burn.
        # Disabled in Home Assistant until someone enables it.
        cv.Optional(CONF_USB_PHY_FUSE_ARMED): switch.switch_schema(
            FuseArmSwitch,
            icon="mdi:fuse-alert",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="ALWAYS_OFF",
        ).extend({cv.Optional(CONF_DISABLED_BY_DEFAULT, default=True): cv.boolean}),
    }
)


async def to_code(config):
    if CONF_USB in config:
        usb = await switch.new_switch(config[CONF_USB])
        await cg.register_component(usb, config[CONF_USB])
        await cg.register_parented(usb, config[CONF_PROXY_STICK_ID])
    if CONF_USB_PHY_FUSE_ARMED in config:
        armed = await switch.new_switch(config[CONF_USB_PHY_FUSE_ARMED])
        await cg.register_parented(armed, config[CONF_PROXY_STICK_ID])
