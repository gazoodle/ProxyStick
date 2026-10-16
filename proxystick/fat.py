"""A FAT32 disk image of files in its root folder, byte for byte the same every time it is built.

The image is what a ProxyStick serves to its host as a USB drive. Built the same way each time (fixed
volume ID, file times as given, files stored one after another), the same files make the same image,
so a rebuild that changed nothing is not a change the host has to reload for.

Ported from the prototype's server (2026-10-16), where it was checked against macOS's own mount
and `fsck_msdos`.
"""

from __future__ import annotations

import math
import os
import re
import shutil
import struct
import time
from collections.abc import Callable
from pathlib import Path

SECTOR = 512
RESERVED = 32  # Boot sector, FSInfo, their backups and spare, as FAT32 volumes usually have.
MIN_CLUSTERS = 65525  # Fewer and every reader takes the volume to be FAT16.
MIN_BYTES = 64 * 1024 * 1024
END = 0x0FFFFFFF
VOLUME_ID = 0x50484F54
LABEL = "PROXYSTICK"
SHORT_NAME = re.compile(r"[A-Z0-9_\-]{1,8}(\.[A-Z0-9_\-]{1,3})?")


class Cancelled(Exception):
    """The build was stopped part way; the partial image must be discarded."""


def build(
    path: Path,
    files: list[tuple[str, Path, float]],
    label: str = LABEL,
    cancelled: Callable[[], bool] = lambda: False,
) -> None:
    """Write the image: files are (name, where to read it, modified time in seconds since 1970).

    `cancelled` is asked between files, so stopping a server mid-build is prompt even for gigabytes.
    """
    directory = _directory(files, label)
    sizes = [source.stat().st_size for _name, source, _time in files]
    spc, fat_sectors, clusters, total = _shape(len(directory), sizes)
    cluster_bytes = SECTOR * spc
    fat = bytearray(fat_sectors * SECTOR)
    struct.pack_into("<II", fat, 0, 0x0FFFFFF8, END)

    def chain(first: int, count: int) -> None:
        for cluster in range(first, first + count):
            struct.pack_into("<I", fat, 4 * cluster, cluster + 1 if cluster < first + count - 1 else END)

    root_count = math.ceil(len(directory) / cluster_bytes)
    chain(2, root_count)
    placed, next_cluster = [], 2 + root_count
    for size in sizes:
        count = math.ceil(size / cluster_bytes)
        if count:
            chain(next_cluster, count)
        placed.append(next_cluster if count else 0)
        next_cluster += count
    directory = _directory(files, label, placed, sizes)

    data_start = (RESERVED + 2 * fat_sectors) * SECTOR
    with open(path, "wb") as image:
        image.truncate(total * SECTOR)
        boot = _boot(spc, fat_sectors, total, label)
        info = _info(clusters - (next_cluster - 2), next_cluster)
        for at, sector in ((0, boot), (1, info), (6, boot), (7, info)):
            image.seek(at * SECTOR)
            image.write(sector)
        for copy in range(2):
            image.seek((RESERVED + copy * fat_sectors) * SECTOR)
            image.write(fat)
        image.seek(data_start)
        image.write(directory)
        for (_name, source, _time), first in zip(files, placed):
            if cancelled():
                raise Cancelled()
            if first:
                image.seek(data_start + (first - 2) * cluster_bytes)
                with open(source, "rb") as content:
                    shutil.copyfileobj(content, image, 1024 * 1024)
        image.flush()
        os.fsync(image.fileno())


def listing(path: Path) -> list[tuple[str, int]]:
    """(name, size) of each file, having checked the image is a whole FAT32 volume whose files' chains fit their sizes."""
    return [(name, size) for name, size, _data in _walk(path)]


def read_files(path: Path) -> dict[str, bytes]:
    return {name: data() for name, _size, data in _walk(path)}


def _shape(directory_bytes: int, sizes: list[int]) -> tuple[int, int, int, int]:
    """Sectors per cluster, sectors per FAT, clusters and sectors in all: the biggest clusters that still make FAT32."""
    total_bytes = max(MIN_BYTES, math.ceil((sum(sizes) + directory_bytes) * 1.05 / 2**20) * 2**20)
    while True:
        total = total_bytes // SECTOR
        for spc in (64, 32, 16, 8, 4, 2, 1):
            fat_sectors = math.ceil(((total - RESERVED) // spc + 2) * 4 / SECTOR)
            clusters = (total - RESERVED - 2 * fat_sectors) // spc
            if clusters >= MIN_CLUSTERS:
                break
        else:
            raise ValueError("The image is too small to be FAT32")
        need = math.ceil(directory_bytes / (SECTOR * spc)) + sum(
            math.ceil(size / (SECTOR * spc)) for size in sizes
        )
        if need <= clusters:
            return spc, fat_sectors, clusters, total
        total_bytes += max(2**20, total_bytes // 10)


def _boot(spc: int, fat_sectors: int, total: int, label: str) -> bytes:
    sector = bytearray(SECTOR)
    sector[0:3] = b"\xeb\x58\x90"
    sector[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", sector, 11, SECTOR, spc, RESERVED, 2, 0, 0, 0xF8, 0, 63, 255, 0, total)
    struct.pack_into("<IHHIHH", sector, 36, fat_sectors, 0, 0, 2, 1, 6)
    sector[64], sector[66] = 0x80, 0x29
    struct.pack_into("<I", sector, 67, VOLUME_ID)
    sector[71:82] = label.upper().encode("ascii", "replace")[:11].ljust(11)
    sector[82:90] = b"FAT32   "
    sector[510:512] = b"\x55\xaa"
    return bytes(sector)


def _info(free: int, next_free: int) -> bytes:
    sector = bytearray(SECTOR)
    sector[0:4], sector[484:488] = b"RRaA", b"rrAa"
    struct.pack_into("<II", sector, 488, free, next_free)
    sector[508:512] = b"\x00\x00\x55\xaa"
    return bytes(sector)


def _directory(
    files: list[tuple[str, Path, float]],
    label: str,
    firsts: list[int] | None = None,
    sizes: list[int] | None = None,
) -> bytes:
    """The root folder's entries; before the files have places, only the right length matters."""
    out = bytearray(_short_entry(label.upper().encode("ascii", "replace")[:11].ljust(11), 0x08, 0, 0, 0))
    used: set[str] = set()
    for index, (name, _source, modified) in enumerate(files):
        first, size = (firsts[index], sizes[index]) if firsts and sizes else (0, 0)
        if SHORT_NAME.fullmatch(name):
            short = _padded(name)
        else:
            short = _padded(_alias(name, used))
            out += _long_entries(name, short)
        used.add(short.decode())
        out += _short_entry(short, 0x20, modified, first, size)
    return bytes(out)


def _padded(name: str) -> bytes:
    stem, _dot, extension = name.partition(".")
    return (stem.ljust(8) + extension.ljust(3)).encode("ascii")


def _alias(name: str, used: set[str]) -> str:
    """An 8.3 name for a long one, as Windows makes: the first letters, ~, a number."""
    stem, _dot, extension = name.upper().rpartition(".")
    if not stem:
        stem, extension = extension, ""
    clean = re.sub(r"[^A-Z0-9_\-]", "", stem)
    suffix = re.sub(r"[^A-Z0-9_\-]", "", extension)[:3]
    number = 1
    while True:
        tail = f"~{number}"
        alias = (clean[: 8 - len(tail)] + tail) + ("." + suffix if suffix else "")
        if _padded(alias).decode() not in used:
            return alias
        number += 1


def _checksum(short: bytes) -> int:
    total = 0
    for byte in short:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def _long_entries(name: str, short: bytes) -> bytes:
    units = name.encode("utf-16-le")
    if len(units) // 2 % 13:  # A name filling its last entry exactly has no end mark.
        units += b"\x00\x00" + b"\xff\xff" * ((-(len(units) // 2 + 1)) % 13)
    count = len(units) // 26
    out = bytearray()
    for order in range(count, 0, -1):
        part = units[(order - 1) * 26 : order * 26]
        entry = bytearray(32)
        entry[0] = order | (0x40 if order == count else 0)
        entry[1:11], entry[11], entry[13], entry[14:26], entry[28:32] = (
            part[0:10],
            0x0F,
            _checksum(short),
            part[10:22],
            part[22:26],
        )
        out += entry
    return bytes(out)


def _short_entry(short: bytes, attributes: int, modified: float, first: int, size: int) -> bytes:
    year, month, day, hour, minute, second = time.gmtime(max(modified, 315532800))[:6]  # FAT begins in 1980.
    date = ((year - 1980) << 9) | (month << 5) | day
    clock = (hour << 11) | (minute << 5) | (second // 2)
    return struct.pack(
        "<11sBBBHHHHHHHI",
        short,
        attributes,
        0,
        0,
        clock,
        date,
        date,
        first >> 16,
        clock,
        date,
        first & 0xFFFF,
        size,
    )


def _walk(path: Path):
    """(name, size, function reading the content) for each file, validating the volume on the way."""
    with open(path, "rb") as image:
        boot = image.read(SECTOR)
        if len(boot) < SECTOR or boot[510:512] != b"\x55\xaa" or boot[82:90] != b"FAT32   ":
            raise ValueError("Not a FAT32 image")
        sector, spc, reserved, fats = struct.unpack_from("<HBHB", boot, 11)
        total, fat_sectors, _flags, _version, root = struct.unpack_from("<IIHHI", boot, 32)
        if sector != SECTOR or total * SECTOR != path.stat().st_size:
            raise ValueError("The image's length is not what its volume says")
        cluster_bytes = sector * spc
        data_start = (reserved + fats * fat_sectors) * sector
        image.seek(reserved * sector)
        fat = struct.unpack(f"<{fat_sectors * sector // 4}I", image.read(fat_sectors * sector))

        def clusters(first: int) -> list[int]:
            found = []
            while 2 <= first < 0x0FFFFFF8:
                found.append(first)
                if len(found) > len(fat):
                    raise ValueError("A cluster chain loops")
                first = fat[first] & 0x0FFFFFFF
            return found

        def content(first: int, size: int) -> bytes:
            with open(path, "rb") as again:
                out = bytearray()
                for cluster in clusters(first):
                    again.seek(data_start + (cluster - 2) * cluster_bytes)
                    out += again.read(cluster_bytes)
                return bytes(out[:size])

        raw = bytearray()
        for cluster in clusters(root):
            image.seek(data_start + (cluster - 2) * cluster_bytes)
            raw += image.read(cluster_bytes)
    parts: dict[int, str] = {}
    for at in range(0, len(raw), 32):
        entry = raw[at : at + 32]
        if entry[0] == 0:
            break
        if entry[0] == 0xE5:
            continue
        if entry[11] == 0x0F:
            parts[entry[0] & 0x3F] = (entry[1:11] + entry[14:26] + entry[28:32]).decode("utf-16-le")
            continue
        if entry[11] & 0x08:
            parts.clear()
            continue
        short = bytes(entry[0:11])
        name = (
            "".join(parts[order] for order in sorted(parts)).split("\x00")[0]
            if parts
            else (
                short[:8].decode().rstrip() + ("." + short[8:].decode().rstrip() if short[8:].strip() else "")
            )
        )
        parts.clear()
        first = (struct.unpack_from("<H", entry, 20)[0] << 16) | struct.unpack_from("<H", entry, 26)[0]
        (size,) = struct.unpack_from("<I", entry, 28)
        if len(clusters(first)) != math.ceil(size / cluster_bytes):
            raise ValueError(f"{name}: its chain does not fit its size")
        yield name, size, lambda first=first, size=size: content(first, size)
