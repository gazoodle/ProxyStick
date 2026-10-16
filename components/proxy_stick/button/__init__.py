import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    DEVICE_CLASS_RESTART,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from .. import CONF_PROXY_STICK_ID, ProxyStick, proxy_stick_ns

DEPENDENCIES = ["proxy_stick"]

ActionButton = proxy_stick_ns.class_("ActionButton", button.Button, cg.Parented.template(ProxyStick))

# Config key -> (the ProxyStick method it calls, schema).
BUTTONS = {
    # Unplug and replug the drive, so the host reads it afresh.
    "refresh": ("refresh", button.button_schema(ActionButton, icon="mdi:usb-flash-drive")),
    # Log the stored USB traces (usb_trace: true).
    "dump_usb_trace": (
        "dump_trace",
        button.button_schema(ActionButton, icon="mdi:text-box-search-outline", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    ),
    # Schedules the irreversible USB_PHY_SEL burn for the next boot; needs the arming switch on. Disabled in
    # Home Assistant until someone enables it.
    "burn_usb_phy_fuse": (
        "schedule_usb_phy_fuse",
        button.button_schema(ActionButton, icon="mdi:fuse", entity_category=ENTITY_CATEGORY_CONFIG).extend(
            {cv.Optional(CONF_DISABLED_BY_DEFAULT, default=True): cv.boolean}
        ),
    ),
    "clear_statistics": (
        "clear_statistics",
        button.button_schema(
            ActionButton, icon="mdi:counter", device_class=DEVICE_CLASS_RESTART, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
        ),
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_PROXY_STICK_ID): cv.use_id(ProxyStick),
        **{cv.Optional(key): schema for key, (_method, schema) in BUTTONS.items()},
    }
)


async def to_code(config):
    for key, (method, _schema) in BUTTONS.items():
        if key in config:
            action = cg.RawExpression(f"&{ProxyStick}::{method}")
            pressed = await button.new_button(config[key], action)
            await cg.register_parented(pressed, config[CONF_PROXY_STICK_ID])
