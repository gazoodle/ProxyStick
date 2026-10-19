"""Compile and exercise the real debug wrappers with a fake USB controller, not firmware."""

import runpy
import subprocess
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock

import pytest


@pytest.mark.asyncio
@pytest.mark.parametrize("debug", [False, True])
@pytest.mark.parametrize("minimal", [False, True])
@pytest.mark.parametrize("serial", [False, True])
async def test_codegen_roots_each_enabled_transport_wrapper(monkeypatch, debug, minimal, serial):
    pytest.importorskip("esphome")
    root = Path(__file__).resolve().parents[1]
    component = runpy.run_path(str(root / "components/proxy_stick/__init__.py"))
    generate = component["to_code"]
    cg = MagicMock()
    cg.register_component = AsyncMock()
    monkeypatch.setitem(generate.__globals__, "cg", cg)
    for helper in (
        "add_idf_component",
        "include_builtin_idf_component",
        "add_idf_sdkconfig_option",
        "add_extra_build_file",
    ):
        monkeypatch.setitem(generate.__globals__, helper, MagicMock())
    monkeypatch.setitem(
        generate.__globals__, "CORE", SimpleNamespace(relative_build_path=lambda name: f"/tmp/build/{name}")
    )
    await generate(
        {
            "id": "invented_component",
            "endpoint": "http://192.0.2.10:8767",
            "read_block_size": 65536,
            "manufacturer": "ProxyStick",
            "product": "ProxyStick",
            "usb_trace": True,
            "debug_diagnostics": debug,
            "debug_psram_image": minimal,
            "debug_minimal_usb": minimal,
            "debug_serial": serial,
            "usb_handover_gap": SimpleNamespace(total_milliseconds=500),
        }
    )
    flags = {call.args[0] for call in cg.add_build_flag.call_args_list}
    generate.__globals__["add_idf_sdkconfig_option"].assert_any_call("CONFIG_TINYUSB_MSC_BUFSIZE", 4096)
    for symbol in ("usbd_edpt_xfer", "mscd_xfer_cb", "mscd_reset", "mscd_control_xfer_cb"):
        assert (f"-Wl,--wrap={symbol}" in flags) == ((debug and not minimal) or serial)
        assert (f"-Wl,--require-defined=__wrap_{symbol}" in flags) == ((debug and not minimal) or serial)
    defines = {call.args[0] for call in cg.add_define.call_args_list}
    assert ("USE_PROXY_STICK_DEBUG" in defines) == ((debug and not minimal) or serial)
    assert ("USE_PROXY_STICK_DEBUG_SERIAL" in defines) == serial
    assert ("USE_PROXY_STICK_TRACE" in defines) == (not minimal)
    assert ("USE_PROXY_STICK_MINIMAL_USB" in defines) == minimal
    includes = {call.args[0] for call in generate.__globals__["include_builtin_idf_component"].call_args_list}
    assert ("esp_driver_uart" in includes) == serial
    for symbol in ("tud_control_xfer", "tud_control_status", "usbd_edpt_stall", "usbd_edpt_clear_stall"):
        assert (f"-Wl,--wrap={symbol}" in flags) == serial
        assert (f"-Wl,--require-defined=__wrap_{symbol}" in flags) == serial
    assert any("CFG_TUSB_CONFIG_FILE" in flag for flag in flags) == serial
    if serial:
        generate.__globals__["add_idf_sdkconfig_option"].assert_any_call("CONFIG_TINYUSB_DEBUG_LEVEL", 2)
    if debug or minimal or serial:
        generate.__globals__["add_idf_component"].assert_any_call(name="espressif/tinyusb", ref="0.21.0~2")


@pytest.mark.parametrize("serial", [False, True])
def test_msc_transport_observation_on_host(tmp_path, serial):
    root = Path(__file__).resolve().parents[1]
    (tmp_path / "esphome/core").mkdir(parents=True)
    (tmp_path / "device").mkdir()
    (tmp_path / "esphome/core/defines.h").write_text("")
    (tmp_path / "tinyusb.h").write_text("#pragma once\nstruct tinyusb_event_t {};\n")
    (tmp_path / "device/usbd_pvt.h").write_text("")
    (tmp_path / "esp_timer.h").write_text("#include <cstdint>\nint64_t esp_timer_get_time();\n")
    (tmp_path / "tusb.h").write_text(
        "#pragma once\n#include <cstdint>\n"
        "enum xfer_result_t { XFER_RESULT_SUCCESS, XFER_RESULT_FAILED };\n"
        "constexpr uint8_t CONTROL_STAGE_SETUP=0, TUSB_REQ_CLEAR_FEATURE=1;\n"
        "struct tusb_control_request_t { uint8_t bmRequestType,bRequest; uint16_t wValue,wIndex,wLength; };\n"
    )
    wrapper = root / "components/proxy_stick/diagnostic_msc.cpp"
    executable = tmp_path / "msc-check"
    result = subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DUSE_PROXY_STICK_DEBUG",
            *(["-DUSE_PROXY_STICK_DEBUG_SERIAL"] if serial else []),
            f"-I{tmp_path}",
            str(Path(__file__).with_name("diagnostic_msc_check.cpp")),
            str(wrapper),
            *([str(root / "components/proxy_stick/diagnostic_control.cpp")] if serial else []),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(executable)], check=False, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    # Debug-off code does not require TinyUSB or define any wrappers.
    subprocess.run(
        ["c++", "-std=c++17", f"-I{tmp_path}", "-c", str(wrapper), "-o", str(tmp_path / "disabled.o")],
        check=True,
        capture_output=True,
    )
    symbols = subprocess.run(
        ["nm", str(tmp_path / "disabled.o")], check=True, capture_output=True, text=True
    ).stdout
    assert "__wrap_" not in symbols
