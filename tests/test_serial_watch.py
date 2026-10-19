"""Serial capture decoding, loss detection and quiet/verbose display."""

import sys

import pytest
import serial

from proxystick.serial_watch import Watch, main, parse


def line(
    seq=1,
    event="MSC_CBW",
    *,
    us=1_000_000,
    drop=0,
    offset=512,
    size=8192,
    value=8192,
    tag=123,
    callbacks=0x28,
    flags=0x80,
    boot=1,
):
    return (
        f"P {boot:08x} {seq:08x} {drop:08x} {us:016x} {event} {offset:016x} "
        f"{size:08x} {value:08x} {tag:08x} {callbacks:08x} {flags:08x}"
    )


def test_parse_and_command_timing():
    record = parse("garbled boot bytes" + line())
    assert record is not None and record.size == 8192 and record.us == 1_000_000
    assert parse("P bad") is None
    assert parse(line(event="UNKNOWN")) is None
    watch = Watch(verbose=True)
    assert any("READ10" in message and "bytes=8192" in message for message in watch.feed(line()))
    result = watch.feed(line(2, "MSC_CSW", us=1_012_500, offset=8192, size=0, value=0, flags=0))
    assert any("duration=12.500 ms" in message for message in result)


def test_gap_clears_pair_and_boot_starts_new_session():
    watch = Watch(verbose=True)
    watch.feed(line())
    result = watch.feed(line(3, "MSC_CSW", drop=1, value=0, flags=0))
    assert any("CAPTURE INCOMPLETE" in message for message in result)
    assert any("unpaired" in message for message in result)
    result = watch.feed(line(1, "BOOT", boot=2))
    assert any("SESSION boot=00000002" in message for message in result)
    assert watch.pending is None


def test_quiet_mode_retains_faults_and_reports_summaries():
    watch = Watch()
    watch.feed(line())
    assert watch.feed(line(2, "MSC_CSW", us=1_010_000, value=0, flags=0)) == []
    assert any(
        "status=1" in message for message in watch.feed(line(3, "MSC_CSW", us=2_010_000, value=1, flags=0))
    )
    assert "MONITOR" in watch.feed("MONITOR CAPTURE INCOMPLETE: RX overrun")[0]
    assert "malformed" in watch.feed("P broken")[0]


def test_full_cdb_decode():
    watch = Watch(True)
    result = watch.feed(
        line(event="MSC_CDB", offset=0x2400000012, size=0, value=0, callbacks=36, flags=0x60080)
    )
    assert any("INQUIRY" in message and "cdb=12 00 00 00 24 00" in message for message in result)


def test_capture_retains_raw_bytes_and_closes_on_interrupt(tmp_path, monkeypatch):
    raw = b"\xffboot garbage\n" + line().encode() + b"\npartial tail"
    chunks = iter([raw[:40], raw[40:]])

    class Port:
        closed = False
        dtr = True
        rts = True

        def __init__(self, **kwargs):
            assert kwargs["baudrate"] == 1500000 and kwargs["exclusive"]

        def __enter__(self):
            assert self.dtr is False and self.rts is False
            return self

        def __exit__(self, *_):
            self.closed = True

        def read(self, size):
            assert size == 4096
            try:
                return next(chunks)
            except StopIteration:
                raise KeyboardInterrupt from None

    port = Port(baudrate=1500000, exclusive=True)
    monkeypatch.setattr(serial, "Serial", lambda **_: port)
    output = tmp_path / "capture.log"
    monkeypatch.setattr(sys, "argv", ["serial-watch", "invented-port", "--output", str(output)])
    main()
    assert output.read_bytes() == raw and port.closed
    with pytest.raises(SystemExit, match="Serial capture failed"):
        main()  # Never overwrite the capture from the first invocation.
    assert output.read_bytes() == raw


def test_collector_restart_invalidates_duration_pair():
    watch = Watch(True)
    watch.feed(line())
    assert "RESTART" in watch.feed("[I] MONITOR READY: GPIO26 RX")[0]
    result = watch.feed(line(2, "MSC_CSW", value=0, flags=0))
    assert any("unpaired" in message for message in result)
    assert any("CAPTURE INCOMPLETE" in message for message in result)


def test_native_and_buffer_timing_share_loss_detection():
    watch = Watch(True)
    watch.feed(line(us=1000000))
    result = watch.feed("T 00000001 00000002 00000000 00000000000f424a SCSI Data")
    assert any("TinyUSB: SCSI Data" in message for message in result)
    result = watch.feed(line(3, "MSC_DATA", us=1004000, value=3000, callbacks=8192, flags=0x288100))
    assert any("duration=3.000 ms" in message and "queue_gap=1.000 ms" in message for message in result)
    result = watch.feed(line(4, "USB_CONTROL", offset=0x0012000001000680, size=18, value=0, flags=0))
    assert any("request=0x06" in message and "length=18" in message for message in result)
    result = watch.feed(line(5, "MSC_RESPONSE", offset=0x050070, size=18, value=0, callbacks=3, flags=3))
    assert any("bytes=70 00 05" in message and "REQUEST_SENSE" in message for message in result)
