# Backlog

Planned, not started. Newest first within each section. Don't start an item unless asked.

## Next

- **Hardware tests** of this version (HANDOVER.md, "Not verified").
- **The prototype's photo app uses `proxystick`** (agreed 2026-10-16): it imports the FAT
  builder and the server core, and deletes its own copies and its wire document. Done in that repo.
- **Casa Mia hosts the server** as a module: in Casa Mia's backlog, with the open question of
  how the package reaches the box.

## Server

- **Image sources** (owner idea, 2026-10-17): besides a folder (built into an image), serve an
  existing image file as it is, or a real USB stick plugged into the server's host, read-only
  and verbatim. No build and no copy of the photos; the frame sees exactly what that stick
  would show it, whatever its filesystem. A device needs: a generation per insertion (serial
  plus the plug-in event, which is the push; hashing a large stick takes minutes), the host
  never mounting it while served (HA OS automounting under `/media` must be checked), a range
  handler of our own (a block device reports size 0 to aiohttp), and raw device rights.
  Writable sticks build on this (see "Writable sticks" under Device).
- **DCIM layout option** (asked 2026-10-16): photos under `DCIM/`, so desktops treat the stick
  as camera storage and offer their own photo prompt (Windows AutoPlay "View pictures", macOS
  Photos and Image Capture, GNOME). The OS asks; nothing runs by itself. Needs subfolder
  support in `fat.py`, which writes the root folder only today.
- **Notice folder changes by itself.** Today a change needs `REFRESH` or the POST. File
  system events (FSEvents, inotify) would be push, not poll, but need a dependency or
  per-platform code: ask first.
- Copies of one file could share their clusters in the image, if frames don't mind.
- Several images for one server (preview, live), each with its own sticks.

## Device

- **Performance: measure and raise throughput** (asked 2026-10-17). History and figures are in
  `docs/PERFORMANCE.md`; the experiment order and 45-cell hardware/host/library matrix are in
  `docs/PERFORMANCE_PLAN.md`. The prototype went from ~110 KB/s to **646 KB/s** (8 MiB sequential,
  double-buffer prefetch); a frame session peaked at 237,331 B/s and fetched 2.09× what it read.
  This version is not yet benchmarked. To do: rerun the controlled 8 MiB test (with
  `update_interval: 1s` when comparing peaks), and repeat it after every transport change.
  Next experiments: a PSRAM cache of hot ranges (partition table, FATs, directories) on the
  AtomS3R beside the sequential double buffer; read-ahead suited to frames' metadata-heavy
  reads; more than one request in flight; and serving from an SD card (below).
- **Writable sticks: copy-on-write sessions** (owner design, 2026-10-17; for the air-gapped
  laptop, not frames). Images stay read-only by default, and generation URLs stay immutable.
  - The server can mark an image (its own, or a real USB stick source) as writable. One client
    at a time opens a write session and holds its write lock; every other client keeps reading
    the unchanged base, so their caches stay valid.
  - The server keeps that client's block changes (512-byte sectors) separately and never
    touches the base during the session. The writer's reads come through the session: base
    first, then its changes on top. The stick updates its own read-ahead cache as it writes.
  - Eject (START STOP UNIT) commits: the server applies the changes and publishes the result
    as a new generation; readers get the usual refresh. Store a committed session as a layer
    over the base and merge in the background, rather than copying a large image. Only a real
    USB stick source needs every reader stopped first, since it is written in place.
  - A writer that vanishes without ejecting commits nothing; its session stays pending (the
    laptop may come back) and is never committed automatically.
  - Acknowledge a write to the host only once the server has stored it durably (or the SD card
    has, when there is one); answer SYNCHRONIZE CACHE properly; batch where possible.
  - Wire: a session API beside today's endpoints: open a session on a generation (lock or
    refusal), write ranges, read ranges through it, commit or discard.
  - Commit is where the server can inspect what the laptop wrote before anyone else sees it:
    the gatekeeping step for the air gap.
  - Identity for the lock: the chip's MAC (unique per chip), or a per-stick token. **Not the
    USB serial number:** every stick currently reports TinyUSB's default `123456` (see the
    serial-number item below).
  - **Acceptance test** (owner, 2026-10-17): two sticks on one writable image, one in the
    strict frame (burnt stick, so the update arrives as a medium change), one in the Mac.
    Drag a new photo onto the Mac's drive: the frame carries on undisturbed. Eject on the Mac:
    the frame updates. Then pull the Mac's stick without ejecting: the frame stays as it was
    and the session stays pending. Watch for macOS's `._*`, `.fseventsd`, `.Spotlight-V100`
    and `.Trashes` litter, which a frame may show as corrupt pictures (`cp -X` or
    `dot_clean`, or filter at commit).
- **WebUSB landing page** (asked 2026-10-16): "open a page when plugged in, with confirmation".
  The stick declares a URL in its USB descriptors; Chrome and Edge (Windows, macOS, Linux,
  ChromeOS) show "Go to <url> to connect" on plug-in, one click to open (its status page, or
  the server's). A composite device, drive plus WebUSB, which TinyUSB supports; Safari and
  Firefox ignore it. Frames never open browsers, so this is for computers only.
- **No keystroke injection without explicit consent.** HID (roadmap) could type a shortcut to
  open anything, with no OS confirmation: exactly how malicious USB sticks work. HID for frames
  means remote control (next, previous, pause); typing into a computer only as an explicit,
  per-use opt-in, if ever.
- **Serve from an SD card on boards with a TF slot** (owner idea, 2026-10-16; T-Dongle-S3,
  ESP32-S3-GEEK). On a refresh event, download the new image to the card's spare slot in the
  background, verify it against its `generation` (the SHA-256), then switch over with an in-band
  medium change, never a disconnect. The card is two raw slots (current, next), no filesystem.
  Reads become local: faster than WiFi even in SPI mode, and immune to network stalls. Open
  decision: with a verified image on the card, keep serving it while the server is away
  (instead of "no server, no medium").
- **Bug: corrupt pictures on the strict frame** (seen 2026-10-16, with the burnt AtomS3U, the
  first time it mounted there). The same burnt stick shows the same image correctly on the
  second frame, so it is how this frame reads that differs. Not investigated. To capture
  first: whether the same pictures are corrupt each time, the same files on the Mac, the
  drive's HTTP failure and prefetch counters, and a USB trace (`READ 10 FAILED`, read
  ranges). Suspects: the read-ahead cache serving a stale or wrong block, a short HTTP read
  accepted, or this frame reading in a pattern the Mac and the second frame never did.
- **Change images without disconnecting USB.** Refresh, the switch and the front button use
  `tud_disconnect`/`tud_connect`, which the strict frame punishes as a disappearing device.
  Stay attached: report "no medium", then "medium changed" (UNIT ATTENTION), as a card reader
  does. Test on both frames.
- **After the USB PHY fuse is burnt, the arm switch and burn button still show** (asked
  2026-10-16, after the S3U burn). ESPHome cannot remove entities at runtime. Proposal: the arm
  switch refuses to turn on once burnt (log why), and the README says to disable both again.
- **Split the USB PHY fuse code out of `proxy_stick.cpp`** (440 lines, in the warning band).
- **USB serial number from the chip's MAC**, not TinyUSB's default `123456`, so two sticks on
  one host (or a frame that remembers devices) are told apart.
- **READ CAPACITY with no medium** should answer "not ready" (report block size 0), not
  "0 blocks"; seen on the second frame's trace.
- **Protocol tests for the device code** against a mock server: short reads, wrong status
  codes, generation changes, timeouts, dropped connections, a silent server (lease). Needs a
  host build of `http`, `cache` and `events`, or a test firmware.
- Test frames with FAT16, FAT32 and exFAT; keep FAT32 the default, consider a FAT16 profile
  for old frames. Record each frame's filesystem, capacity, name and folder limits.
- If hostname connections fail, test a fixed server address before changing protocol code.
- Write support stays refused. Never proxy writes without exclusive ownership and
  cache-coherency rules.

## Hardware

- **Try the candidate boards** in the README's hardware table (T-Dongle-S3, ESP32-S3-GEEK):
  board config, both frames, then mark them tested.
- **ESP32-S2 support** (LOLIN S2 Mini: https://www.wemos.cc/en/latest/s2/s2_mini.html; Adafruit
  QT Py ESP32-S2: https://learn.adafruit.com/adafruit-qt-py-esp32-s2). The S2 has USB-OTG but no
  USB-Serial/JTAG device, so it should be drive-only at power-on with no fuse; test on the
  strict frame. Needs the JTAG handover, trace USB-state sampling and fuse code compiled out.
- **Raspberry Pi Pico 2 W** (https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html):
  USB device and WiFi, but a second firmware for the same wire protocol (Pico SDK, not ESPHome).
  Only if a non-ESP stick is worth maintaining.

## Release

- First commit, then a `dev` branch; publish to GitHub; tag `v0.1.0`.
- PyPI only once the protocol settles (protocol 1).
- Supported-board list, first-flash and OTA-recovery notes; the AtomS3U.
- Security notes: the token, and the read-only stance.

## Ideas parked deliberately

- HID: the stick also a keyboard or remote control for its host.
- Proxying a real SD card or another block device.
- Air-gapped or obsolete-host data delivery.
