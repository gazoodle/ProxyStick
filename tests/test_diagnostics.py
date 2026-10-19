"""Temporary diagnostic wire bounds, authentication, correlation and console presentation."""

import logging
import struct
import subprocess
from pathlib import Path

import pytest
from aiohttp.test_utils import TestClient, TestServer

from proxystick import ProxyStick
from proxystick.diagnostics import ConsoleFormatter


def batch(event=13):
    return struct.pack(
        "<4sIIIQQIIIIIIII", b"DBG2", 0x1234, 7, 1, 123456, 8192, 42, 3, event, 8192, 25, 9, 8, 0
    )


@pytest.mark.asyncio
async def test_debug_requires_opt_in_and_authentication_then_logs_exact_fields(tmp_path, caplog):
    disabled = ProxyStick(tmp_path, tmp_path / "state")
    async with TestClient(TestServer(disabled.application())) as client:
        assert (await client.post("/proxystick/debug", data=batch())).status == 404

    enabled = ProxyStick(tmp_path, tmp_path / "state", debug_diagnostics=True, token="invented-debug-token")
    async with TestClient(TestServer(enabled.application())) as client:
        assert (await client.post("/proxystick/debug", data=batch())).status == 401
        with caplog.at_level(logging.INFO, logger="proxystick.diagnostics"):
            response = await client.post(
                "/proxystick/debug", data=batch(), headers={"Authorization": "Bearer invented-debug-token"}
            )
        assert response.status == 204
    assert "boot=00001234 records=1 overwritten=7" in caplog.text
    assert "seq=42 t=123456us cmd=3 DELAY_BEGIN" in caplog.text
    assert "offset=8192 size=8192 value=25 fetch=9" in caplog.text


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "body,status", [(b"", 400), (batch()[:-1], 400), (batch(99), 400), (b"x" * 1265, 413)]
)
async def test_bad_batches_are_rejected_without_logging_payload(tmp_path, caplog, body, status):
    stick = ProxyStick(tmp_path, tmp_path / "state", debug_diagnostics=True)
    async with TestClient(TestServer(stick.application())) as client:
        assert (await client.post("/proxystick/debug", data=body)).status == status
    assert "STICK DEBUG" not in caplog.text


def test_only_debug_records_are_coloured_and_plain_output_has_no_escapes():
    record = logging.LogRecord("proxystick.diagnostics", logging.INFO, "", 0, "DELAY_BEGIN", (), None)
    assert "\033[36m" in ConsoleFormatter(True).format(record)
    assert "\033" not in ConsoleFormatter(False).format(record)
    record.name = "proxystick.server"
    assert "\033" not in ConsoleFormatter(True).format(record)


def test_command_aggregation_on_host(tmp_path):
    source = Path(__file__).with_name("diagnostic_command_check.cpp")
    executable = tmp_path / "command-check"
    subprocess.run(["c++", "-std=c++17", str(source), "-o", str(executable)], check=True, capture_output=True)
    subprocess.run([str(executable)], check=True, capture_output=True)


@pytest.mark.asyncio
async def test_command_summaries_report_waits_delay_count_and_failures(tmp_path, caplog):
    stick = ProxyStick(tmp_path, tmp_path / "state", debug_diagnostics=True)
    record = struct.pack("<QQIIIIIIII", 123456, 512, 42, 19, 17, 65536, 194894, 138406, 8, 8 << 16 | 3)
    body = struct.pack("<4sIII", b"DBG2", 0x1234, 0, 1) + record
    async with TestClient(TestServer(stick.application())) as client:
        with caplog.at_level(logging.INFO, logger="proxystick.diagnostics"):
            assert (await client.post("/proxystick/debug", data=body)).status == 204
    assert "COMMAND_SUMMARY offset=512 bytes=65536 callbacks=8" in caplog.text
    assert "duration_us=194894 max_source_us=138406 delayed=8 failed=True discontinuous=True" in caplog.text


@pytest.mark.asyncio
async def test_fetch_header_correlates_with_device_batch_and_bad_headers_are_not_logged(tmp_path, caplog):
    stick = ProxyStick(tmp_path, tmp_path / "state", debug_diagnostics=True)
    stick.state_dir.mkdir()
    name = "sha256-" + "0" * 64 + ".img"
    (stick.state_dir / name).write_bytes(bytes(512))
    async with TestClient(TestServer(stick.application())) as client:
        with caplog.at_level(logging.INFO, logger="proxystick.diagnostics"):
            response = await client.get(
                f"/proxystick/images/{name}",
                headers={"X-ProxyStick-Debug": "00001234:9", "Range": "bytes=0-511"},
            )
            assert response.status == 206 and len(await response.read()) == 512
            assert "fetch=00001234:9 range=bytes=0-511" in caplog.text
            caplog.clear()
            response = await client.get(
                f"/proxystick/images/{name}", headers={"X-ProxyStick-Debug": "bad-untrusted-text"}
            )
            await response.read()
            assert "bad-untrusted-text" not in caplog.text


@pytest.mark.asyncio
async def test_msc_wire_records_show_actual_tags_lengths_status_and_recovery(tmp_path, caplog):
    stick = ProxyStick(tmp_path, tmp_path / "state", debug_diagnostics=True)
    records = [
        (22, 3584, 8192, 8192, 0x28, 0x80),
        (23, 4096, 4096, 1, 2, 3),
        (24, 4096, 8192, 1, 0, 0),
        (25, 4096, 8192, 4096, 0, 0),
        (26, 0x81, 0, 0, 0, 0),
    ]
    payload = struct.pack("<4sIII", b"DBG2", 0x1234, 0, len(records))
    for seq, (event, offset, size, value, callbacks, flags) in enumerate(records, 1):
        payload += struct.pack(
            "<QQIIIIIIII", 123456, offset, seq, 3, event, size, value, 0xABC, callbacks, flags
        )
    async with TestClient(TestServer(stick.application())) as client:
        with caplog.at_level(logging.INFO, logger="proxystick.diagnostics"):
            assert (await client.post("/proxystick/debug", data=payload)).status == 204
    assert (
        "tag=00000abc MSC_CBW offset=3584 host_bytes=8192 cdb_bytes=8192 opcode=0x28 direction=0x80"
        in caplog.text
    )
    assert "MSC_CSW transferred=4096 residue=4096 status=1 transfers=2 flags=0x3" in caplog.text
    assert "MSC_RESET transferred=4096 host_bytes=8192 reason=BOT reset" in caplog.text
    assert "MSC_XFER_FAULT transferred=4096 queued=8192 actual=4096 result=0 kind=0" in caplog.text
    assert "MSC_CLEAR_HALT endpoint=0x81" in caplog.text
