"""Compare serial protocol captures, without mistaking different host workloads for proof of a bug."""

import argparse
import logging
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from .serial_watch import Record, cdb_bytes, parse

logger = logging.getLogger(__name__)


@dataclass
class Capture:
    records: int = 0
    issues: Counter[str] = field(default_factory=Counter)
    events: Counter[str] = field(default_factory=Counter)
    requests: Counter[str] = field(default_factory=Counter)
    responses: Counter[str] = field(default_factory=Counter)
    reads: Counter[str] = field(default_factory=Counter)
    decisions: Counter[str] = field(default_factory=Counter)
    read_bytes: int = 0
    buffers: int = 0
    max_buffer_us: int = 0
    max_queue_gap_us: int = 0
    max_command_us: int = 0
    dropped: int = 0


def analyse(path: Path) -> Capture:
    result = Capture()
    previous: Record | None = None
    command: Record | None = None
    data_end: tuple[int, int] | None = None
    with path.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            r = parse(line)
            if r is None:
                if (
                    line.startswith(("P ", "T "))
                    or "MONITOR UART error" in line
                    or "CAPTURE INCOMPLETE" in line
                ):
                    result.issues["malformed record or collector error"] += 1
                    command = None
                    data_end = None
                if "MONITOR READY" in line:
                    previous = command = None
                    data_end = None
                continue
            if previous is None or previous.boot != r.boot:
                if r.sequence != 1 or r.dropped:
                    result.issues["session start missing or already lossy"] += 1
                command = None
                data_end = None
            elif r.sequence != (previous.sequence + 1) & 0xFFFFFFFF or previous.dropped != r.dropped:
                result.issues["sequence gap or target drop"] += 1
                command = None
                data_end = None
            previous = r
            result.records += 1
            result.events[r.event] += 1
            result.dropped = max(result.dropped, r.dropped)
            if r.event in ("MSC_CBW", "MSC_CDB"):
                command = r
                data_end = (r.tag, r.us)
                if r.event == "MSC_CBW":
                    result.read_bytes += r.size
                    result.reads[f"LBA={r.offset // 512} offset={r.offset} bytes={r.size}"] += 1
                else:
                    result.requests[cdb_bytes(r).hex(" ")] += 1
            elif r.event == "USB_CONTROL":
                result.requests[f"EP0 {r.offset.to_bytes(8, 'little').hex(' ')} stage={r.value}"] += 1
            elif r.event == "MSC_RESPONSE":
                payload = r.offset.to_bytes(8, "little")[: r.callbacks].hex(" ")
                result.responses[f"kind=0x{r.flags:x} pos={r.value} total={r.size} bytes={payload}"] += 1
            elif r.event == "MSC_DATA":
                result.buffers += 1
                result.max_buffer_us = max(result.max_buffer_us, r.value)
                if data_end and data_end[0] == r.tag:
                    result.max_queue_gap_us = max(result.max_queue_gap_us, r.us - r.value - data_end[1])
                data_end = (r.tag, r.us)
                if r.size != r.callbacks or r.flags & 255:
                    result.issues["short/failed data completion"] += 1
            elif r.event == "MSC_CSW":
                if command and command.tag == r.tag:
                    result.max_command_us = max(result.max_command_us, r.us - command.us)
                command = None
                data_end = None
                if r.value or r.flags:
                    result.decisions[f"CSW status={r.value} flags=0x{r.flags:x} residue={r.size}"] += 1
            elif r.event == "NATIVE":
                if "USBD Setup Received" in r.text:
                    result.requests[r.text.strip()] += 1  # Includes requests that stall before a reply.
                if any(word in r.text.lower() for word in ("stall", "fail", "unsupported", "case", "reset")):
                    result.decisions[r.text.strip()] += 1
    if result.records == 0:
        result.issues["no decoded records"] += 1
    return result


def compare(reference: Capture, problem: Capture) -> list[str]:
    lines = [
        "Capture comparison: different host workloads are expected; differences are leads, not verdicts."
    ]
    for name, capture in (("REFERENCE", reference), ("PROBLEM", problem)):
        lines.append(
            f"{name}: records={capture.records}, target_dropped_max={capture.dropped}, "
            f"READ10={capture.events['MSC_CBW']}, requested_bytes={capture.read_bytes}, buffers={capture.buffers}"
        )
        lines.append(
            f"  max command={capture.max_command_us / 1000:.3f} ms; "
            f"max buffer={capture.max_buffer_us / 1000:.3f} ms; "
            f"max queue gap={capture.max_queue_gap_us / 1000:.3f} ms"
        )
        if not capture.buffers:
            lines.append("  Per-buffer trace missing: use the full serial-debug firmware.")
        lines.append(f"  Integrity: {dict(capture.issues) if capture.issues else 'no detected gaps/errors'}")
    for title, first, second in (
        ("Control/CDB request differences", reference.requests, problem.requests),
        ("Response-byte differences", reference.responses, problem.responses),
        ("TinyUSB/status decision differences", reference.decisions, problem.decisions),
        ("Read range/size differences", reference.reads, problem.reads),
    ):
        lines.append(title + " (first 12):")
        changed = sorted(key for key in first.keys() | second.keys() if first[key] != second[key])
        lines.extend(f"  reference={first[key]} problem={second[key]} {key}" for key in changed[:12])
        if not changed:
            lines.append("  none")
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path, help="known-good Mac or frame raw capture")
    parser.add_argument("problem", type=Path, help="problem-frame raw capture; same image and firmware")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        for message in compare(analyse(args.reference), analyse(args.problem)):
            logger.info("%s", message)
    except OSError as error:
        raise SystemExit(f"Could not read captures: {error}") from error


if __name__ == "__main__":
    main()
