"""Exercise the minimal MSC callback path without compiling firmware."""

import runpy
import subprocess
from pathlib import Path
from types import SimpleNamespace

import pytest


@pytest.mark.parametrize(
    "snapshot,minimal,valid", [(False, False, True), (False, True, False), (True, True, True)]
)
def test_minimal_requires_verified_psram_mode(monkeypatch, snapshot, minimal, valid):
    pytest.importorskip("esphome")
    root = Path(__file__).resolve().parents[1]
    component = runpy.run_path(str(root / "components/proxy_stick/__init__.py"))
    validate = component["validate_psram_test"]
    monkeypatch.setitem(
        validate.__globals__, "fv", SimpleNamespace(full_config=SimpleNamespace(get=lambda: {"psram": {}}))
    )
    config = {"debug_psram_image": snapshot, "debug_minimal_usb": minimal, "debug_diagnostics": True}
    if valid:
        assert validate(config) == config
    else:
        with pytest.raises(component["cv"].Invalid, match="debug_minimal_usb requires"):
            validate(config)


@pytest.mark.parametrize("serial", [False, True])
def test_minimal_usb_callbacks_on_host(tmp_path, serial):
    root = Path(__file__).resolve().parents[1]
    (tmp_path / "esphome/core").mkdir(parents=True)
    (tmp_path / "esphome/core/defines.h").write_text("")
    (tmp_path / "esphome/core/log.h").write_text("#define ESP_LOGW(...) ((void)0)\n")
    (tmp_path / "tinyusb.h").write_text("#pragma once\nstruct tinyusb_event_t { int id; };\n")
    (tmp_path / "tusb.h").write_text(
        "#pragma once\n#include <cstdint>\n"
        "constexpr uint8_t SCSI_SENSE_NOT_READY=2, SCSI_SENSE_ILLEGAL_REQUEST=5, SCSI_SENSE_DATA_PROTECT=7;\n"
        'extern "C" bool tud_msc_set_sense(uint8_t,uint8_t,uint8_t,uint8_t);\n'
    )
    executable = tmp_path / "minimal-check"
    result = subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DUSE_PROXY_STICK_MINIMAL_USB",
            "-DUSE_PROXY_STICK_PSRAM_TEST",
            *(["-DUSE_PROXY_STICK_DEBUG", "-DUSE_PROXY_STICK_DEBUG_SERIAL"] if serial else []),
            f"-I{tmp_path}",
            str(Path(__file__).with_name("usb_minimal_check.cpp")),
            str(root / "components/proxy_stick/usb_minimal.cpp"),
            "-o",
            str(executable),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(executable)], check=False, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    # The normal callback implementation must disappear, rather than competing at link time.
    normal_object = tmp_path / "normal-disabled.o"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-DUSE_PROXY_STICK_MINIMAL_USB",
            f"-I{tmp_path}",
            "-c",
            str(root / "components/proxy_stick/usb.cpp"),
            "-o",
            str(normal_object),
        ],
        check=True,
        capture_output=True,
    )
    symbols = subprocess.run(["nm", str(normal_object)], check=True, capture_output=True, text=True).stdout
    assert "tud_msc_" not in symbols
