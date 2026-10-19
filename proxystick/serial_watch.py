"""Readable serial diagnostics; always retain the untouched byte stream for investigation."""

import argparse
import logging
import re
from collections import Counter
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from .diagnostics import EVENTS

logger = logging.getLogger(__name__)
LINE = re.compile(
    r"P ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{16}) ([A-Z_]+) "
    r"([0-9a-f]{16}) ((?:[0-9a-f]{8} ){4}[0-9a-f]{8})"
)
NATIVE = re.compile(r"T ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{16}) (.*)")
SERIAL_EVENTS = ("MSC_CDB", "MSC_DATA", "MSC_RESPONSE", "USB_CONTROL", "USB_STALL", "USB_CLEAR")
OPCODES = {
    0x00: "TEST_UNIT_READY",
    0x03: "REQUEST_SENSE",
    0x12: "INQUIRY",
    0x1A: "MODE_SENSE6",
    0x1B: "START_STOP",
    0x1E: "PREVENT_ALLOW",
    0x23: "READ_FORMAT_CAPACITIES",
    0x25: "READ_CAPACITY10",
    0x28: "READ10",
    0x2A: "WRITE10",
    0x5A: "MODE_SENSE10",
    0x9E: "SERVICE_ACTION_IN16",
}


@dataclass(frozen=True)
class Record:
    boot: int
    sequence: int
    dropped: int
    us: int
    event: str
    offset: int
    size: int
    value: int
    tag: int
    callbacks: int
    flags: int
    text: str = ""


def parse(line: str) -> Record | None:
    native = NATIVE.fullmatch(line.rstrip("\r\n"))
    if native:
        boot, seq, dropped, us, text = native.groups()
        return Record(
            int(boot, 16), int(seq, 16), int(dropped, 16), int(us, 16), "NATIVE", 0, 0, 0, 0, 0, 0, text
        )
    # The first record may follow boot-ROM garbage at a different baud rate.
    match = LINE.search(line.rstrip("\r\n"))
    if match is None or match.end() != len(line.rstrip("\r\n")):
        return None
    boot, seq, dropped, us, event, offset, rest = match.groups()
    if event not in (*EVENTS, *SERIAL_EVENTS):
        return None
    size, value, tag, callbacks, flags = (int(x, 16) for x in rest.split())
    return Record(
        int(boot, 16),
        int(seq, 16),
        int(dropped, 16),
        int(us, 16),
        event,
        int(offset, 16),
        size,
        value,
        tag,
        callbacks,
        flags,
    )


def cdb_bytes(record: Record) -> bytes:
    return (
        record.offset.to_bytes(8, "little")
        + record.size.to_bytes(4, "little")
        + record.value.to_bytes(4, "little")
    )[: (record.flags >> 16) & 255]


class Watch:
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.previous: Record | None = None
        self.pending: tuple[int, int] | None = None
        self.counts: Counter[str] = Counter()
        self.last_summary_us = 0
        self.last_data: tuple[int, int] | None = None

    def feed(self, line: str) -> list[str]:
        record = parse(line)
        if record is None:
            if line.startswith(("P ", "T ")):
                self.pending = None
                self.last_data = None
                return ["CAPTURE INCOMPLETE: malformed diagnostic line (raw bytes retained)"]
            if "MONITOR" in line:
                self.pending = None
                self.last_data = None
                if "MONITOR READY" in line:
                    self.previous = None
                    return ["COLLECTOR START/RESTART: capture boundary; target may have continued running"]
                return [re.sub(r"\x1b\[[0-9;]*m", "", line).strip()]
            return []
        r = record
        output = []
        old = self.previous
        if old is None or r.boot != old.boot:
            self.pending = None
            self.last_data = None
            self.last_summary_us = r.us
            output.append(f"SESSION boot={r.boot:08x} first sequence={r.sequence}, dropped={r.dropped}")
            if r.sequence != 1 or r.dropped:
                output.append("CAPTURE INCOMPLETE: joined after the start or records were already dropped")
        elif r.sequence != (old.sequence + 1) & 0xFFFFFFFF or r.dropped != old.dropped:
            self.pending = None
            self.last_data = None
            output.append(
                f"CAPTURE INCOMPLETE: sequence {old.sequence} -> {r.sequence}; target dropped={r.dropped}"
            )
        self.previous = r
        self.counts[r.event] += 1
        prefix = f"{r.us / 1_000_000:9.3f}s #{r.sequence}"
        detail = f"{r.event} offset={r.offset} bytes={r.size} value={r.value} "
        detail += f"tag={r.tag:08x} callbacks={r.callbacks} flags=0x{r.flags:x}"
        important = r.event in (
            "BOOT",
            "BUS",
            "MSC_RESET",
            "MSC_CLEAR_HALT",
            "MSC_XFER_FAULT",
            "READ_FAILED",
            "USB_STALL",
            "USB_CLEAR",
            "USB_CONTROL",
            "MSC_RESPONSE",
        )
        if r.event == "MSC_CBW":
            self.pending = (r.tag, r.us)
            self.last_data = (r.tag, r.us)
            detail = f"READ10 tag={r.tag:08x} offset={r.offset} bytes={r.size} cdb_bytes={r.value}"
        elif r.event == "MSC_CDB":
            self.pending = (r.tag, r.us)
            cdb = cdb_bytes(r)
            if not cdb:
                return output + [f"{prefix} CAPTURE INCOMPLETE: empty CDB"]
            name = OPCODES.get(cdb[0], f"opcode=0x{cdb[0]:02x}")
            detail = f"{name} tag={r.tag:08x} host_bytes={r.callbacks} cdb={cdb.hex(' ')}"
        elif r.event == "MSC_CSW":
            duration = None
            if self.pending and self.pending[0] == r.tag and r.us >= self.pending[1]:
                duration = (r.us - self.pending[1]) / 1000
            self.pending = None
            elapsed = "unpaired" if duration is None else f"{duration:.3f} ms"
            detail = (
                f"CSW tag={r.tag:08x} bytes={r.offset} duration={elapsed} status={r.value} "
                f"residue={r.size} flags=0x{r.flags:x}"
            )
            important = bool(r.value or r.flags or duration is not None and duration >= 100)
        elif r.event == "MSC_DATA":
            queued = r.us - r.value
            gap = None
            if self.last_data and self.last_data[0] == r.tag:
                gap = queued - self.last_data[1]
            self.last_data = (r.tag, r.us)
            spacing = "unknown" if gap is None else f"{gap / 1000:.3f} ms"
            detail = (
                f"DATA tag={r.tag:08x} EP=0x{(r.flags >> 8) & 255:02x} offset={r.offset} "
                f"queued={r.size} actual={r.callbacks} duration={r.value / 1000:.3f} ms "
                f"queue_gap={spacing} result={r.flags & 255}"
            )
            important = bool(
                r.flags & 255 or r.size != r.callbacks or r.value >= 50000 or gap is not None and gap >= 20000
            )
        elif r.event == "USB_CONTROL":
            b = r.offset.to_bytes(8, "little")
            detail = (
                f"CONTROL type=0x{b[0]:02x} request=0x{b[1]:02x} value=0x{int.from_bytes(b[2:4], 'little'):04x} "
                f"index=0x{int.from_bytes(b[4:6], 'little'):04x} length={int.from_bytes(b[6:8], 'little')} "
                f"reply_bytes={r.size} stage={r.value}"
            )
        elif r.event == "MSC_RESPONSE":
            payload = r.offset.to_bytes(8, "little")[: r.callbacks]
            kind = "CONTROL" if r.flags & 0x10000 else OPCODES.get(r.flags & 255, "SCSI")
            detail = (
                f"RESPONSE {kind} tag={r.tag:08x} offset={r.value} total={r.size} "
                f"bytes={payload.hex(' ')} truncated={bool(r.flags & 0x80000000)}"
            )
        elif r.event == "NATIVE":
            detail = f"TinyUSB: {r.text}"
            important = any(
                word in r.text.lower() for word in ("stall", "fail", "unsupported", "reset", "assert")
            )
        if important or self.verbose:
            output.append(f"{prefix} {detail}")
        if r.us - self.last_summary_us >= 1_000_000:
            output.append(
                f"{r.us / 1_000_000:9.3f}s records={sum(self.counts.values())} "
                f"READ10={self.counts['MSC_CBW']} CSW={self.counts['MSC_CSW']} "
                f"target dropped={r.dropped}"
            )
            self.last_summary_us = r.us
        return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Atom Lite USB serial port; close miniterm first")
    parser.add_argument("--baud", type=int, default=1500000, help="USB serial baud (default: 1500000)")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("/tmp") / datetime.now(UTC).strftime("proxystick-serial-%Y%m%d-%H%M%S-UTC.log"),
    )
    parser.add_argument(
        "--verbose", action="store_true", help="show routine commands as well as summaries/faults"
    )
    args = parser.parse_args()
    import serial

    logging.basicConfig(level=logging.INFO, format="%(message)s")
    watch = Watch(args.verbose)
    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.2, exclusive=True)
    port.port = args.port
    port.dtr = False
    port.rts = False
    try:
        with args.output.open("xb") as capture, port:
            logger.info("Watching %s; raw capture: %s; Ctrl-C stops", args.port, args.output)
            pending = bytearray()
            while True:
                data = port.read(4096)
                if not data:
                    continue
                capture.write(data)
                capture.flush()
                pending.extend(data)
                while b"\n" in pending:
                    line, _, pending = pending.partition(b"\n")
                    for message in watch.feed(line.decode("utf-8", errors="replace")):
                        logger.info("%s", message)
                if len(pending) > 8192:
                    logger.warning("CAPTURE INCOMPLETE: overlong serial line (raw bytes retained)")
                    watch.pending = None
                    pending.clear()
    except KeyboardInterrupt:
        logger.info("Stopped; decoded %d records. Raw capture: %s", sum(watch.counts.values()), args.output)
    except (OSError, serial.SerialException) as error:
        raise SystemExit(f"Serial capture failed: {error}") from error


if __name__ == "__main__":
    main()
