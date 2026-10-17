"""ProxyStick: an ESP32-S3 presenting a server's FAT image as a read-only USB drive (docs/WIRE.md)."""

import tomllib
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import (
    add_idf_component,
    add_idf_sdkconfig_option,
    include_builtin_idf_component,
)
from esphome.const import CONF_ID

CODEOWNERS = ["@gazoodle"]
DEPENDENCIES = ["esp32", "network"]
AUTO_LOAD = ["json"]
MULTI_CONF = False

CONF_PROXY_STICK_ID = "proxy_stick_id"
CONF_ENDPOINT = "endpoint"
CONF_TOKEN = "token"
CONF_READ_BLOCK_SIZE = "read_block_size"
CONF_MANUFACTURER = "manufacturer"
CONF_PRODUCT = "product"
CONF_USB_TRACE = "usb_trace"
CONF_USB_HANDOVER_GAP = "usb_handover_gap"

# The one version, kept in the repository's pyproject.toml (AGENTS.md, Versioning); ESPHome fetches the
# whole repository, so it is always there beside the component.
VERSION = tomllib.loads((Path(__file__).parents[2] / "pyproject.toml").read_text())["project"]["version"]

proxy_stick_ns = cg.esphome_ns.namespace("proxy_stick")
ProxyStick = proxy_stick_ns.class_("ProxyStick", cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ProxyStick),
        cv.Required(CONF_ENDPOINT): cv.url,
        cv.Optional(CONF_TOKEN): cv.string_strict,
        # Bytes fetched per range read, and the size of each of the two read-ahead buffers.
        cv.Optional(CONF_READ_BLOCK_SIZE, default=65536): cv.one_of(*(2**power for power in range(9, 17)), int=True),
        cv.Optional(CONF_MANUFACTURER, default="ProxyStick"): cv.All(cv.string_strict, cv.Length(max=32)),
        cv.Optional(CONF_PRODUCT, default="ProxyStick"): cv.All(cv.string_strict, cv.Length(max=32)),
        # Record every USB event and SCSI command in flash, for the last 16 boots (trace.h).
        cv.Optional(CONF_USB_TRACE, default=False): cv.boolean,
        # How long the bus stays idle between the chip's own USB-Serial/JTAG device letting go and the drive
        # attaching, so a host that already enumerated the former sees an unplug (proxy_stick.cpp, setup).
        cv.Optional(CONF_USB_HANDOVER_GAP, default="500ms"): cv.positive_time_period_milliseconds,
    }
).extend(cv.polling_component_schema("5s"))


def _padded(text: str, length: int) -> str:
    return text.encode("ascii", "replace").decode()[:length].ljust(length)


async def to_code(config):
    add_idf_component(name="espressif/esp_tinyusb", ref="2.2.1")
    include_builtin_idf_component("esp_http_client")
    add_idf_sdkconfig_option("CONFIG_TINYUSB_MSC_ENABLED", True)
    # A transport buffer, not a filesystem sector size: sectors stay 512 B for every host.
    add_idf_sdkconfig_option("CONFIG_TINYUSB_MSC_BUFSIZE", 8192)
    # Measured on the prototype: range reads of 64 KiB were bound by lwIP's ~5.7 KiB default window.
    add_idf_sdkconfig_option("CONFIG_LWIP_TCP_WND_DEFAULT", 32768)
    add_idf_sdkconfig_option("CONFIG_LWIP_TCP_RECVMBOX_SIZE", 32)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_DESC_MANUFACTURER_STRING", config[CONF_MANUFACTURER])
    add_idf_sdkconfig_option("CONFIG_TINYUSB_DESC_PRODUCT_STRING", config[CONF_PRODUCT])
    # ponytail: our mass-storage callbacks replace esp_tinyusb's storage-only ones by overriding them at
    # link time; drop this when ESPHome or esp_tinyusb offers a generic block-device hook.
    cg.add_build_flag("-Wl,--allow-multiple-definition")

    if config[CONF_USB_TRACE]:
        cg.add_define("USE_PROXY_STICK_TRACE")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_endpoint(config[CONF_ENDPOINT]))
    if CONF_TOKEN in config:
        cg.add(var.set_token(config[CONF_TOKEN]))
    cg.add(var.set_read_block_size(config[CONF_READ_BLOCK_SIZE]))
    cg.add(var.set_version(VERSION))
    cg.add(var.set_handover_gap(config[CONF_USB_HANDOVER_GAP].total_milliseconds))

    major_minor = ".".join(VERSION.split(".")[:2])
    cg.add(
        proxy_stick_ns.set_inquiry(
            _padded(config[CONF_MANUFACTURER], 8), _padded(config[CONF_PRODUCT], 16), _padded(major_minor, 4)
        )
    )
