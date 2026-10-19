"""Receive-only Grove UART collector for the separate, Mac-powered Atom Lite."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import include_builtin_idf_component

DEPENDENCIES = ["esp32", "logger"]
namespace = cg.esphome_ns.namespace("serial_monitor")
SerialMonitor = namespace.class_("SerialMonitor", cg.Component)
CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(SerialMonitor)}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_driver_uart")
    var = cg.new_Pvariable(config["id"])
    await cg.register_component(var, config)
