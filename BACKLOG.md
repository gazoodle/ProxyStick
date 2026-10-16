# Backlog

Planned, not started. Newest first within each section. Don't start an item unless asked.

## Next

- **Hardware tests** of this version (HANDOVER.md, "Not verified").
- **The prototype's photo app uses `proxystick`** (agreed 2026-10-16): it imports the FAT
  builder and the server core, and deletes its own copies and its wire document. Done in that repo.
- **Casa Mia hosts the server** as a module: in Casa Mia's backlog, with the open question of
  how the package reaches the box.

## Server

- **Notice folder changes by itself.** Today a change needs `REFRESH` or the POST. File
  system events (FSEvents, inotify) would be push, not poll, but need a dependency or
  per-platform code: ask first.
- Copies of one file could share their clusters in the image, if frames don't mind.
- Several images for one server (preview, live), each with its own sticks.

## Device

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
