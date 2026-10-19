import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_DISABLED_BY_DEFAULT, ENTITY_CATEGORY_CONFIG
from esphome.core import CORE

from .. import CONF_DEBUG_DIAGNOSTICS, CONF_DEBUG_MINIMAL_USB, CONF_PROXY_STICK_ID, ProxyStick, proxy_stick_ns

DEPENDENCIES = ["proxy_stick"]

UsbSwitch = proxy_stick_ns.class_("UsbSwitch", switch.Switch, cg.Component, cg.Parented.template(ProxyStick))
UsbReadDelaySwitch = proxy_stick_ns.class_("UsbReadDelaySwitch", switch.Switch, cg.Component)
FuseArmSwitch = proxy_stick_ns.class_("FuseArmSwitch", switch.Switch, cg.Parented.template(ProxyStick))
CONF_USB = "usb"
CONF_USB_READ_DELAY = "usb_read_delay"
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
        # Diagnostic only: holds each successful TinyUSB read callback for 25 ms. Always off after boot.
        cv.Optional(CONF_USB_READ_DELAY): switch.switch_schema(
            UsbReadDelaySwitch,
            icon="mdi:timer-alert-outline",
            entity_category=ENTITY_CATEGORY_CONFIG,
            default_restore_mode="ALWAYS_OFF",
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
    component = CORE.config["proxy_stick"]
    if (
        CONF_USB_READ_DELAY in config
        and component[CONF_DEBUG_DIAGNOSTICS]
        and not component[CONF_DEBUG_MINIMAL_USB]
    ):
        delay = await switch.new_switch(config[CONF_USB_READ_DELAY])
        await cg.register_component(delay, config[CONF_USB_READ_DELAY])
    if CONF_USB_PHY_FUSE_ARMED in config:
        armed = await switch.new_switch(config[CONF_USB_PHY_FUSE_ARMED])
        await cg.register_parented(armed, config[CONF_PROXY_STICK_ID])
