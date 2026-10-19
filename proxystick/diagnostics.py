"""Temporary debug back-channel v2, enabled explicitly by the development console."""

import logging
import struct

from aiohttp import web

logger = logging.getLogger(__name__)
HEADER = struct.Struct("<4sIII")
RECORD = struct.Struct("<QQIIIIIIII")
MAX_RECORDS = 26
MAX_BYTES = HEADER.size + MAX_RECORDS * RECORD.size
EVENTS = (
    "BOOT",
    "BUS",
    "COMMAND_BEGIN",
    "READ_BEGIN",
    "BUFFER_READY",
    "READ_FAILED",
    "COMMAND_COMPLETE",
    "CACHE_HIT",
    "CACHE_WAIT",
    "FETCH_BEGIN",
    "FETCH_END",
    "DELAY_STATE",
    "DELAY_BEGIN",
    "DELAY_END",
    "PREFETCH_JOB",
    "IDLE_INCOMPLETE",
    "COMMAND_SUMMARY",
    "COMMAND_INCOMPLETE",
    "COMMAND_ABORTED",
    "FETCH_SUMMARY",
    "SLOW_READ",
    "MSC_CBW",
    "MSC_CSW",
    "MSC_RESET",
    "MSC_XFER_FAULT",
    "MSC_CLEAR_HALT",
)


async def receive(request: web.Request) -> web.Response:
    # Stream with an explicit bound, including for chunked requests on an embedded app.
    body = bytearray()
    async for chunk in request.content.iter_chunked(MAX_BYTES + 1):
        body.extend(chunk)
        if len(body) > MAX_BYTES:
            raise web.HTTPRequestEntityTooLarge(max_size=MAX_BYTES, actual_size=len(body))
    if len(body) < HEADER.size:
        raise web.HTTPBadRequest(text="Truncated debug header")
    magic, boot, dropped, count = HEADER.unpack_from(body)
    if magic != b"DBG2" or not 1 <= count <= MAX_RECORDS or len(body) != HEADER.size + count * RECORD.size:
        raise web.HTTPBadRequest(text="Invalid debug batch")
    records = list(RECORD.iter_unpack(body[HEADER.size :]))
    if any(not 1 <= record[4] <= len(EVENTS) for record in records):
        raise web.HTTPBadRequest(text="Unknown debug event")
    logger.info("STICK DEBUG boot=%08x records=%d overwritten=%d", boot, count, dropped)
    for us, offset, sequence, command, event, size, value, fetch, callbacks, flags in records:
        if event >= 22:
            fields = {
                22: f"offset={offset} host_bytes={size} cdb_bytes={value} opcode=0x{callbacks:02x} direction=0x{flags:02x}",
                23: f"transferred={offset} residue={size} status={value} transfers={callbacks} flags=0x{flags:x}",
                24: f"transferred={offset} host_bytes={size} reason={'bus/reset-config' if value == 0 else 'BOT reset'}",
                25: f"transferred={offset} queued={size} actual={value} result={callbacks} kind={flags}",
                26: f"endpoint=0x{offset:02x}",
            }
            logger.info(
                "  boot=%08x seq=%d t=%dus tag=%08x %s %s",
                boot,
                sequence,
                us,
                fetch,
                EVENTS[event - 1],
                fields[event],
            )
            continue
        if event in (17, 18, 19):
            logger.info(
                "  boot=%08x seq=%d t=%dus cmd=%d %s offset=%d bytes=%d callbacks=%d "
                "duration_us=%d max_source_us=%d delayed=%d failed=%s discontinuous=%s",
                boot,
                sequence,
                us,
                command,
                EVENTS[event - 1],
                offset,
                size,
                callbacks,
                value,
                fetch,
                flags >> 16,
                bool(flags & 1),
                bool(flags & 2),
            )
            continue
        if event == 20:
            logger.info(
                "  boot=%08x seq=%d t=%dus FETCH_SUMMARY fetch=%d offset=%d size=%d "
                "duration_us=%d failed=%s speculative=%s",
                boot,
                sequence,
                us,
                fetch,
                offset,
                size,
                value,
                bool(flags & 1),
                bool(flags & 2),
            )
            continue
        logger.info(
            "  boot=%08x seq=%d t=%dus cmd=%d %-17s offset=%d size=%d value=%d fetch=%d",
            boot,
            sequence,
            us,
            command,
            EVENTS[event - 1],
            offset,
            size,
            value,
            fetch,
        )
    return web.Response(status=204)


class ConsoleFormatter(logging.Formatter):
    """Colour only diagnostics; other handlers and redirected output keep plain logs."""

    def __init__(self, colour: bool):
        super().__init__("%(asctime)s %(name)s: %(message)s", datefmt="%H:%M:%S")
        self.colour = colour

    def format(self, record: logging.LogRecord) -> str:
        line = super().format(record)
        if self.colour and record.name == __name__:
            return f"\033[36m{line}\033[0m"
        return line
