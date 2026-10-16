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

## Release

- First commit, then a `dev` branch; publish to GitHub; tag `v0.1.0`.
- PyPI only once the protocol settles (protocol 1).
- Supported-board list, first-flash and OTA-recovery notes; the AtomS3U.
- Security notes: the token, and the read-only stance.

## Ideas parked deliberately

- HID: the stick also a keyboard or remote control for its host.
- Proxying a real SD card or another block device.
- Air-gapped or obsolete-host data delivery.
