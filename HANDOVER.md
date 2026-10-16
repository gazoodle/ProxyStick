# Handover

Last updated: 2026-10-16. Nothing is committed yet: the owner reviews the first commit.

## State

The prototype's device component and server are moved here and reworked:

- `proxystick/`: the server. A headless core (`server.py`: build, publish, the wire protocol,
  `start/stop/health`), the FAT32 builder (`fat.py`, ported as it was, plus cancellation),
  and the console (`console.py`, `proxystick serve <folder>`).
- `components/proxy_stick/`: the ESPHome component, split from one 848-line file into
  `common`, `http`, `cache` (read-ahead), `usb` (TinyUSB callbacks), `events` (the event
  stream task) and `proxy_stick` (the component), plus `sensor/`, `binary_sensor/`,
  `switch/` and `button/` platforms in place of the prototype's template lambdas.
- `docs/WIRE.md`: protocol v0, paths now `/proxystick/...`, the event stream as the lease,
  and the optional bearer token.
- `tools/dev`, `tools/compile`, `tools/setup`; `example/proxystick.yaml`.
- `fake-git-host` is its own repo beside this one: snapshot of the working tree at each
  fetch, by `git daemon --access-hook`.

## Verified

- Server: 13 tests (protocol, ranges, ETag, 416, publish/force/restart, failure reporting,
  SSE, token, prompt stop with a stick connected, port in use), 1.3 s. Ruff and pyright clean.
- Component: `tools/compile` builds the example config with every platform, with no
  warnings. The verbatim port built too, before the split.
- Dev loop, locally: `tools/dev` wrote the config, ESPHome cloned the component from the fake
  git host and built it, and SIGTERM left no daemon behind.

## Frames that power-cycle the stick: solved (2026-10-16)

**Root cause, from `usb_trace` dumps:** the strict frame power-cycles the USB port whenever a
device it has seen disappears (about 1.1 s after power-on in every failing boot; it never sent our
drive a request). An ESP32-S3's own USB-Serial/JTAG device holds the USB pins from power-on and
always disappears when our firmware hands the pins to TinyUSB, so every boot triggered the next
power cycle. The factory AtomS3U, a steady JTAG device, was left alone; WiFi current (experiment
B) and the JTAG release timing (50–100 ms after power-on) were ruled out.

**Fix:** burn the `USB_PHY_SEL` eFuse, which gives the pins to USB-OTG from power-on, so a host
only ever meets our drive. Done from Home Assistant: enable the hidden controls, turn on **USB
PHY fuse armed**, press **Burn USB PHY fuse on next boot**, then **Restart**. The firmware
refuses if USB-OTG download is fused off (early-batch chips), since the board could then not be
reflashed over USB. Irreversible, per board.

**Verified:** the burnt AtomS3U shows photos on the strict frame at once, and after the burn
the ROM's USB-OTG download mode (hold reset 2 s) still talks to esptool (stub flasher ran), so it
can be reflashed over USB. It enumerates with the MAC in lower case as its serial. The second
frame works with the burnt stick too, so the fix suits both. (`espefuse` fails in ESPHome's venv on a `bitstring`
version clash; the firmware's config dump reports the fuses instead.)

**Keep in mind:**
- The second frame tolerated the JTAG handover all along; never fix one frame at the expense
  of the other. Test every change on both.
- Never `tud_disconnect` to refresh: the strict frame punishes any disappearance, at any time.
  Report medium changes in-band instead (backlog).
- `usb_trace: true` stays the tool for any new host: it keeps the last 16 boots in flash.

## Not verified: needs the hardware

Nothing in the device code has run on a device yet. The prototype's test plan still applies
to this version:

1. `tools/dev <folder>`, Install from ESPHome on the box, confirm the frame mounts.
2. Stop the server: the medium goes within 20 s (the lease); restart it: it comes back.
3. `REFRESH` with no change: exactly one unplug, pause, replug. Change a file and `REFRESH`:
   the same, with the new contents.
4. Watch for resets, heap pressure, watchdog warnings and short reads in the logs.
5. Compare throughput with the prototype's 442 KB/s single-buffer baseline.

## Calls made while the owner was away (review)

- **The event stream replaced the 2 s poll** (core "Push, never poll"; already in the
  protocol). Discovery runs on the stream's task, so ESPHome's loop no longer blocks on HTTP.
- **A failed discovery keeps what is mounted** and retries in 5 s, as the protocol already
  said. The prototype took the medium away instead. A failed range read still removes it.
- **Relative image URLs resolve against the endpoint's origin**, as the protocol says. The
  prototype joined them to the whole endpoint, which only differs if the endpoint has a path.
- **Heap and WiFi sensors are left to ESPHome's own `debug` and `wifi_signal`** platforms,
  not duplicated in the component.
- **The read-block size is a config option** (default 64 KiB). `set_read_block_power()`
  stays for measuring from a lambda while the drive is off.
- **The USB switch is remembered over restarts** (`RESTORE_DEFAULT_ON`); the prototype
  always started enabled.
- **`REFRESH` forces a reload when nothing changed**; the start-up build never does, so
  restarting the server does not make every stick reload.

## This machine

- If the home folder is a git repository with no commits, ESP-IDF's CMake walks up
  into it and fails. The workaround: ESPHome's cached IDF copy
  (`~/Library/Caches/esphome/idf/frameworks/5.5.5`) was given a git repository with one empty
  commit. If ESPHome downloads a new IDF, do the same there, or commit something in `~`.
- `tools/dev-device.yaml` (gitignored) is the device half of the dev config, made from the
  prototype's: same device name, so Install updates the same stick. `tools/dev` writes the
  whole config to the box as one file each time it starts.
- **After every Install, leave the stick powered on the Mac until the log says "Boot seems
  successful" (60 s).** ESPHome marks new firmware good only then; lose power sooner and the
  bootloader rolls back to the previous firmware. A frame that power-cycles the stick never
  lets new firmware pass, so a test run silently uses the old one (2026-10-16, cost a run).
- Firmware is only ever built by the ESPHome builder on the box; `tools/compile` is just a
  local check that the component still builds.
- macOS asks once to allow incoming connections, for Python (the server) and git (the fake git
  host), the first time `tools/dev` runs.
