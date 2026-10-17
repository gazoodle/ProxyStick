"""The stick's diagnostics: what it served, how the network kept up, how the read-ahead fared."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_DATA_RATE,
    DEVICE_CLASS_DATA_SIZE,
    DEVICE_CLASS_DURATION,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_BYTES,
    UNIT_MILLISECOND,
)

from .. import CONF_PROXY_STICK_ID, ProxyStick, proxy_stick_ns

DEPENDENCIES = ["proxy_stick"]
Metric = proxy_stick_ns.enum("Metric")


def _schema(icon, unit=None, device_class=None, state_class=STATE_CLASS_TOTAL, decimals=0):
    # sensor_schema refuses an explicit None, so units and classes go in only when there are some.
    optional = {"unit_of_measurement": unit, "device_class": device_class}
    return sensor.sensor_schema(
        icon=icon,
        state_class=state_class,
        accuracy_decimals=decimals,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        **{key: value for key, value in optional.items() if value is not None},
    )


def _bytes(icon, state_class=STATE_CLASS_TOTAL):
    return _schema(icon, UNIT_BYTES, DEVICE_CLASS_DATA_SIZE, state_class)


def _rate():
    return _schema("mdi:speedometer", "B/s", DEVICE_CLASS_DATA_RATE, STATE_CLASS_MEASUREMENT)


def _milliseconds(icon):
    return _schema(icon, UNIT_MILLISECOND, DEVICE_CLASS_DURATION, STATE_CLASS_MEASUREMENT, 1)


# Config key -> (Metric in proxy_stick.h, schema). Session counters start again at each mount.
SENSORS = {
    "usb_scsi_commands": ("USB_SCSI_COMMANDS", _schema("mdi:usb")),
    "bytes_served": ("BYTES_SERVED", _bytes("mdi:database-arrow-up")),
    "total_bytes_served": ("TOTAL_BYTES_SERVED", _bytes("mdi:database-arrow-up", STATE_CLASS_TOTAL_INCREASING)),
    "read_throughput": ("READ_THROUGHPUT", _rate()),
    "max_read_throughput": ("MAX_READ_THROUGHPUT", _rate()),
    "session_read_throughput": ("SESSION_READ_THROUGHPUT", _rate()),
    "http_fetch_throughput": ("HTTP_FETCH_THROUGHPUT", _rate()),
    "source_read_throughput": ("SOURCE_READ_THROUGHPUT", _rate()),
    "usb_transport_throughput": ("USB_TRANSPORT_THROUGHPUT", _rate()),
    "network_bytes_fetched": ("NETWORK_BYTES_FETCHED", _bytes("mdi:download-network-outline")),
    "cache_hit_bytes": ("CACHE_HIT_BYTES", _bytes("mdi:cached")),
    "http_requests": ("HTTP_REQUESTS", _schema("mdi:counter")),
    "http_failures": ("HTTP_FAILURES", _schema("mdi:alert-circle-outline")),
    "average_http_time": ("AVERAGE_HTTP_MS", _milliseconds("mdi:timer-outline")),
    "last_http_time": ("LAST_HTTP_MS", _milliseconds("mdi:timer-marker-outline")),
    "average_first_byte_time": ("AVERAGE_FIRST_BYTE_MS", _milliseconds("mdi:timer-sand")),
    "average_http_chunk": (
        "AVERAGE_HTTP_CHUNK_BYTES",
        _bytes("mdi:package-variant", STATE_CLASS_MEASUREMENT),
    ),
    "prefetch_hits": ("PREFETCH_HITS", _schema("mdi:check-network-outline")),
    "prefetch_waits": ("PREFETCH_WAITS", _schema("mdi:timer-sand")),
    "average_prefetch_wait_time": ("AVERAGE_PREFETCH_WAIT_MS", _milliseconds("mdi:timer-sand")),
    "prefetch_discards": ("PREFETCH_DISCARDS", _schema("mdi:delete-clock-outline")),
    "prefetch_failures": ("PREFETCH_FAILURES", _schema("mdi:alert-circle-outline")),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_PROXY_STICK_ID): cv.use_id(ProxyStick),
        **{cv.Optional(key): schema for key, (_metric, schema) in SENSORS.items()},
    }
)


async def to_code(config):
    stick = await cg.get_variable(config[CONF_PROXY_STICK_ID])
    for key, (metric, _schema_) in SENSORS.items():
        if key in config:
            cg.add(stick.set_sensor(getattr(Metric, metric), await sensor.new_sensor(config[key])))
