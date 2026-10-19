# Temporary device diagnostics

## Full TinyUSB serial capture (2026-10-19)

**Current correction:** the 3000000-baud USB session produced no valid records, including
unreadable collector startup output. USB UART0 and the watcher default are reverted to the
previously verified **1500000** rate; Grove remains **1500000**. Rebuild/install only the monitor
for this correction. Both hops now have equal capacity, so sustained-load headroom is smaller;
watch for loss/overrun reports. No change to the S3R diagnostics or its baud is required.
The earlier 3000000 instructions below describe the failed comparison, not the current setting.

This stage supersedes the baud rates in the earlier investigation notes below.
Build/install the **monitor first**, using its WiFi OTA connection: Grove RX is now
1500000 baud and USB UART0 is 3000000 baud. Its HA YAML retains the existing WiFi and OTA
secrets. Then build/install the S3R. Both ends must run these matching rates. The collector
now forwards each complete diagnostic line in one UART write, preventing its own safe-mode
or WiFi logs from splitting a normal record. Partial input is flushed at shutdown; arbitrary
input longer than 512 bytes without a newline is still forwarded, but not atomically.

`debug_serial: true` now enables TinyUSB level-2 USBD/MSC logging as well as the wrappers.
A generated configuration-extension header includes Espressif's original configuration and
overrides only the debug sink/levels; no vendor source is edited. Native log fragments are
coalesced into timestamped `T boot sequence dropped us text` lines, sharing sequence/loss
accounting with `P` records. Level 3 is deliberately not enabled: it dumps image payloads.
Level-2 logging adds real CPU/UART work; compare like-for-like firmware and check loss counters.
Native logs attempted from ISR context are not formatted or written there; they increment the
shared lost-record counters instead. Overlong formatted fragments also flag loss. Consequently
this is a detailed software trace, not an unconditional lossless wire capture.

Additional serial-only records:

- `MSC_DATA`: every submitted IN data buffer's completion. `offset` is image offset for READ10;
  `size` queued bytes, `callbacks` actual bytes, `value` duration in microseconds, `fetch` host tag.
  Flags pack result in bits 0..7, endpoint in 8..15 and opcode in 16..23. Its timestamp is sampled
  at completion before formatting. Subtract duration to obtain submission time; gaps from the
  preceding completion reveal pauses hidden by whole-command totals. These are software API
  observations, not individual USB packet times or proof of host application consumption.
- `MSC_RESPONSE`: pre-submission response bytes, packed little-endian in `offset` (up to eight
  bytes per record). `size` is total reply size, `value` byte position, `callbacks` captured count,
  `fetch` tag. Flags hold opcode, or bit 16 for EP0; bit 31 explicitly marks a reply capped at
  64 bytes. Typical sense/mode/capacity/inquiry responses fit. Payloads are not READ10 image dumps.
- `USB_CONTROL`: raw eight-byte setup request packed in `offset`, offered reply length in `size`,
  stage in `value` (0 data submission, 1 status, 2 rejected data submission). Native `USBD Setup
  Received` logs also retain requests that are rejected before these reply hooks.
- `USB_STALL` / `USB_CLEAR`: device endpoint stall/clear calls, with endpoint in `offset`.

Capture with the new watcher default of 3000000 baud:

```sh
tools/serial-watch /dev/cu.usbserial-EXAMPLE --output /tmp/reference.log
tools/serial-watch /dev/cu.usbserial-EXAMPLE --output /tmp/problem.log
tools/serial-compare /tmp/reference.log /tmp/problem.log
```

Use the **Mac as the known-good reference** if the tolerant frame cannot reach the collector.
Use the same firmware and image, capture from target boot, and note which photo is corrupt.
Ctrl-C ends each capture; files are never overwritten. `--verbose` displays every native line
and buffer, while quiet mode preserves all raw data and shows important events/summaries.
`--baud 1500000` remains available for the previous monitor firmware.
Comparison reports capture integrity, read ranges/sizes, control/CDB requests, response bytes,
TinyUSB decisions and maximum command/buffer/queue-gap times. Different host workloads are
expected; differences are investigation leads, not automatic diagnoses. No hardware USB analyser
or access to the frame's internal driver is implied. Rebuild with `debug_serial: false` to remove
this instrumentation. Firmware compilation, linkage, higher-rate serial operation and the
next frame comparison still require the owner's HA builds/hardware run.

## Separate serial collector (Atom Lite)

`example/serial-monitor.yaml` builds a Mac-powered Atom Lite with WiFi and password-protected
ESPHome OTA, but no native API. The owner's HA config is `proxystick-monitor.yaml`, using the existing
fake Git host for the uncommitted `serial_monitor` component. It is not a ProxyStick device
and is not generated by `tools/dev`'s device-template watcher.

Power isolation is mandatory: Grove red +5V is physically disconnected and insulated.
Connect black ground and yellow signal only: target S3R GPIO2 TX -> Lite GPIO26 RX.
White is unused; the collector never drives the target. USB powers only the Lite.

Build on the owner's HA box, download the factory binary, then first-flash over the Mac's
USB serial connection. Open the monitor at **1500000 baud, 8N1**. `MONITOR READY` reports its
921600-baud Grove input. A native UART event task forwards bytes unchanged, including NULs,
to UART0 alongside the Lite's own logs. Input buffering (8192 bytes) lives on the collector,
not the target. RX overrun and output failures produce `MONITOR CAPTURE INCOMPLETE`; UART
framing errors are also reported. This is bounded diagnostic capture, not a guarantee against
USB-bridge/host-side losses. Avoid concurrent serial-terminal readers and keep capture running.

The collector's first firmware was USB-flashed and its `MONITOR READY` console verified
on 2026-10-19. That installed image has no WiFi; build/download and USB-flash this update once
before using OTA. The example references `wifi_ssid`, `wifi_password` and `ota_password` secrets;
the owner's HA file uses its existing secret names. WiFi loss never reboots the monitor, and
power saving is disabled. OTA necessarily interrupts capture; stop captures before installing.
WiFi adds some interrupt/logging load, so throughput and overrun counters still need hardware
verification. A healthy UART stream was received after the S3R transmitter was enabled; the
earlier unconfigured line's break errors were not USB faults. Builds remain on the HA box.

For a quiet, readable view with automatic raw-byte capture:

```sh
tools/serial-watch /dev/cu.usbserial-EXAMPLE
```

Replace the invented port with the Atom Lite port. Ctrl-C stops. The command logs its unique
UTC-named `/tmp/proxystick-serial-*.log` capture path and refuses to overwrite an existing file.
`--output <path>` chooses the capture file; `--verbose` shows every command. Default output
shows sessions, capture gaps/errors, failed status, commands taking at least 100 ms, and
event-driven activity summaries. Timings and byte counts are decimal; non-READ CDBs decode to
command names. Unpaired completions are labelled, never assigned an invented duration. Sequence
gaps discard the pending duration match. The untouched capture includes boot garbage and the
monitor's own logs, even when the console suppresses them. A USB/serial failure exits visibly;
there is no automatic reconnect that could hide a gap. This tool uses the existing pySerial
development dependency, not another terminal package.

For an unfiltered view, the installed pySerial terminal also works:

```sh
.venv/bin/python -m serial.tools.list_ports
.venv/bin/python -m serial.tools.miniterm --raw --dtr 0 --rts 0 /dev/cu.usbserial-EXAMPLE 1500000
```

Replace the invented port with the listed Atom Lite port. Ctrl-] exits. To save the session,
append `| tee /tmp/proxystick-serial.log`. Open the terminal before rebooting the target;
do not run another reader on the same port. DTR/RTS stay deasserted so opening the terminal
does not intentionally reset the collector. Its ROM boot output uses another baud rate and
may look garbled briefly; application logs and target records use 1500000 on this USB link.

### S3R serial diagnostic stage

The local S3R YAML now adds `debug_serial: true`, retaining `debug_minimal_usb: true` and
the verified PSRAM image. Build/install `proxystick-dev-s3r.yaml` on the HA box. No server
restart or collector rebuild is needed. Look for `SERIAL DEBUG: TX GPIO2 @ 921600` in the
target's normal ESPHome log, and `P ... BOOT ...` followed by USB records on the collector.

`USE_PROXY_STICK_DEBUG_SERIAL` selects a separate recorder instead of `diagnostics.cpp`'s
HTTP uploader: no old diagnostic ring, upload task or flash trace. It starts before USB attachment,
not after WiFi connects. UART1 is dedicated TX-only on GPIO2, 921600 8N1; do not configure
another component or logger on UART1 or GPIO2. No Grove RX is enabled on the target.
The same four pinned TinyUSB observation hooks are enabled even in minimal mode; the direct
PSRAM callbacks, descriptors, 4 KiB USB buffer and normal WiFi/API/OTA path are unchanged.
The 25 ms probe stays omitted in minimal mode.

Each ASCII line is at most 128 bytes and contains:

```text
P boot sequence dropped us EVENT offset size value tag_or_fetch callbacks flags
```

All numbers are hexadecimal; `us` is monotonic target microseconds. Event names and field
meanings match the back-channel below, except there is no inferred command-ID column.
READ10 `MSC_CBW` and matching `MSC_CSW` share the real host tag: their timestamp difference
is observed command duration, not filesystem or decoder time. Minimal callbacks emit no
`COMMAND_SUMMARY`; use the transport pair. Every non-READ status is also reported.
Serial-only `MSC_CDB` replaces the non-READ CBW record: `offset` packs CDB bytes 0..7,
`size` bytes 8..11, `value` bytes 12..15, each little-endian. `callbacks` is the host's byte
count, `tag_or_fetch` its tag, and `flags` packs direction in bits 0..7, LUN in 8..15 and
CDB length in 16..23. This preserves the whole request, not the complete response payload.

The original hardware-FIFO-only implementation dropped 94 of the first 249 records in the
owner's capture. The transmitter now installs ESP-IDF's native **4096-byte TX buffer**, plus
the driver's minimum 256-byte RX buffer with RX interrupts disabled. No custom queue or sender
task is added. A zero-wait writer mutex protects the sole TX producer; contention drops the
record. The IDF free-space query accounts for transaction metadata; an extra 64-byte margin
leaves alignment headroom before calling the driver's write API. Only the UART ISR can free
space concurrently, so writes with insufficient room never enter that potentially blocking API.
UART calls run outside the command-summary spinlock. Sequence numbers advance on drops and
`dropped` is cumulative per boot; there are no retries. Sustained traffic can still fill the
bounded buffer. A gap, increased count or collector UART error makes that portion incomplete;
absence of reset/fault records across a gap proves nothing. A lost final record is only
reported if a later record gets through. Formatting and a short spinlock still have CPU cost:
this is low-impact instrumentation, not zero interference. It does not checksum USB payloads
or prove what the frame's application received.

Set `debug_serial: false` and rebuild to remove this transmitter and return minimal mode to
its uninstrumented comparison. Non-minimal mode then resumes the separately configured
WiFi diagnostics; do not change the owner's rollback or early USB-attachment settings.

## Minimal USB isolation test

The local S3R YAML enables `debug_minimal_usb: true` for the next one-off comparison.
It requires the verified `debug_psram_image` experiment below; PSRAM mode still requires
`psram:` and `debug_diagnostics: true` in YAML. Minimal mode overrides instrumentation at
code generation: without the explicit `debug_serial` exception above, `USE_PROXY_STICK_DEBUG`
and `USE_PROXY_STICK_TRACE` are not defined, regardless of those YAML settings. No persistent
trace writes, diagnostic uploader, linker hooks or 25 ms switch are compiled. The S3U config
is not changed.

This selects `usb_minimal.cpp` instead of `usb.cpp`: no USB statistics, hot-block tracking,
per-read logs, command bookkeeping, media-change notification or deliberate delay. Reads call
the immutable PSRAM snapshot directly and return the requested length, or an explicit SCSI
error. The PSRAM reader also omits its cache-byte counter in this mode. USB descriptors,
inquiry strings, early attachment, 4096-byte transport buffer and TinyUSB `0.21.0~2` remain
unchanged. The device reports no medium until snapshot verification succeeds, then readiness
without UNIT ATTENTION. Writes are still rejected.

ESPHome, WiFi/API/OTA, the initial HTTP preload and component loop remain: this is not the
unmodified Espressif example and does not isolate the entire RTOS/ESPHome stack. Avoid USB
on/off and refresh controls during this test. USB performance/counter sensors intentionally
stay at zero; USB-host configured and PSRAM availability are still useful. Dump buttons report
that capture is disabled. There should be no cyan device back-channel batches.

Build/install `proxystick-dev-s3r.yaml` through ESPHome; no server restart is needed. Confirm
`MINIMAL USB TEST` and `PSRAM SNAPSHOT READY` in device logs, then display both photos on the
strict frame. Record whether each is corrupted. Set only `debug_minimal_usb: false` and rebuild
to restore the instrumented callbacks. The existing trace/debug settings take effect again.
The owner's `enable_ota_rollback: false` device settings are preserved, not adjusted by this test.

## One-off whole-image PSRAM test

The local S3R device YAML additionally enables `debug_psram_image: true`, defining
`USE_PROXY_STICK_PSRAM_TEST`. This requires both `psram:` and `debug_diagnostics: true`.
It is off by default, and the S3U keeps the existing streaming path. All experiment code
is conditional; the option is not a supported general PSRAM cache design. This test accepts
logical volumes up to 64 MiB (the builder's minimum volume size for the small test folder).

USB attaches through the normal early startup path, reporting no medium while the event task
downloads the occupied prefix of this server's
contiguous FAT image into explicitly allocated PSRAM. The builder's FSInfo next-free hint
locates the end of that prefix. The rest of the logical volume is reconstructed as zeros.
The device hashes the prefix and the entire zero tail and compares the result with the
published immutable image generation. A bad hint, insufficient PSRAM, failed download or
hash mismatch keeps the medium unavailable; there is no streaming fallback. Only successful
verification makes the medium available, using the existing UNIT ATTENTION notification
without disconnecting USB. This avoids a long invisible startup while the snapshot loads.

Wait for `PSRAM SNAPSHOT READY` in the device log. Every subsequent sector read is a memory
copy or a zero fill; no image HTTP reads or prefetch worker are used. The image stays pinned
until reboot, even if the server publishes a different generation. Turning off WiFi or stopping
the server does not remove the medium: this experiment deliberately bypasses the normal SSE
lease for its verified local snapshot. The S3R device config also disables automatic reboot
on WiFi/API loss, so going offline does not erase the RAM snapshot after 15 minutes.

Power loss or reboot erases PSRAM and requires the server again for one preload. Inserting
the stick into a frame therefore requires the server initially, until the medium is ready. Keep the
25 ms toggle off for the first frame comparison; it remains available for a controlled second
pass. Revert this experiment by setting `debug_psram_image: false` in the S3R device YAML
and rebuilding. Restore desired WiFi/API reboot timeouts separately. No server restart is
needed for this experiment; the running dev watcher propagates the existing S3R YAML edit.

## Back-channel

Enabled for the current investigation in `example/proxystick.yaml`:

```yaml
proxy_stick:
  debug_diagnostics: true
```

This defines `USE_PROXY_STICK_DEBUG`. The RAM recorder, uploader, HTTP correlation headers,
and 25 ms delay probe are guarded by that define. To compile them out, set the option to
false (the default); the delay switch is then omitted even if its YAML entry remains. The delay
switch is always off after boot, and turning it on/off emits `DELAY_STATE`. Command summaries
count how many successful buffers received the 25 ms pause, without per-buffer delay events.

`tools/dev` enables server reception. For the standalone console, use
`proxystick serve <folder> --debug-diagnostics`. Without this flag the debug endpoint is
absent. Remove the temporary `args.debug_diagnostics = True` line in `tools/dev.py` to turn
off reception there. Terminal diagnostic logs are cyan; redirected logs and `NO_COLOR`
remain plain. Log timestamps are server receipt time; `t=...us` is the ESP's monotonic time.
The usual bearer-token middleware also protects the debug endpoint.

The recorder holds 80 fixed-size records (3840 bytes) under a short critical section. It
allocates nothing and performs no network or flash writes in USB callbacks. The uploader
has a 4096-byte task stack and a 1264-byte batch buffer, plus its temporary HTTP client.
It waits for 250 ms without USB read or fetch activity and for image fetches to finish, then
posts at most 26 records. Fast callbacks still update the idle clock despite producing no
records. Batches/retries are at least two seconds apart. Unacknowledged records
remain queued, subject to ring overwrite; failures are logged. Successful batches are
acknowledged by HTTP 204. Duplicate sequences after a lost acknowledgement are possible.

An image read can start during an upload. The uploader uses a separate client, a low-priority
task and a 250 ms HTTP timeout, but this is not a hard end-to-end deadline (connection setup
and scheduling may add time). Compare debug-enabled/disabled runs to measure interference.
Continuous reads can postpone uploads and overwrite the ring. `overwritten` is cumulative
per boot, and sequence gaps expose missing events. Power loss destroys unsent records.

The temporary endpoint `POST /proxystick/debug` is outside wire protocol v0. Bodies use
little-endian integers: a 16-byte `<4sIII` header (`DBG2`, random boot ID, overwritten count,
record count), then 1..26 records, each `<QQIIIIIIII` (48 bytes): ESP microseconds, absolute image
byte offset, sequence, command ID, event ID, size, value, fetch ID, callback count, flags.
Maximum body: 1264 bytes. This unreleased format replaces DBG1; restart the server and rebuild
the device together. There is no compatibility decoder.
The server validates the entire batch before logging any record. It does not persist traces
separately; capture the server logs to retain them. No image data or credentials are included.

Events are numbered from 1 in this order:

`BOOT BUS COMMAND_BEGIN READ_BEGIN BUFFER_READY READ_FAILED COMMAND_COMPLETE CACHE_HIT
CACHE_WAIT FETCH_BEGIN FETCH_END DELAY_STATE DELAY_BEGIN DELAY_END PREFETCH_JOB IDLE_INCOMPLETE
COMMAND_SUMMARY COMMAND_INCOMPLETE COMMAND_ABORTED FETCH_SUMMARY SLOW_READ
MSC_CBW MSC_CSW MSC_RESET MSC_XFER_FAULT MSC_CLEAR_HALT`

Routine callback/cache/delay events in the original numbering are no longer emitted.
`COMMAND_SUMMARY` reports completion with first offset, bytes served, callback count, total
duration (`value`, microseconds), and maximum source wait (`fetch`, microseconds). Flag bit 0
means a read failed, bit 1 means non-contiguous callbacks; upper 16 bits count injected delays
(saturated at 65535). Bytes and duration in the wire record are 32-bit. On detach an active
command emits `COMMAND_ABORTED`. After a quiet interval an active command emits one
`COMMAND_INCOMPLETE` snapshot; later completion may still arrive, so this is not proof of a
timeout. `SLOW_READ` preserves offset/size/source wait for callbacks taking at least 50 ms.
Failures always produce `READ_FAILED`. `BUS` value is the esp_tinyusb event ID.
`FETCH_SUMMARY` has duration in `value`, fetch ID in `fetch`; flags bit 0 is failure and bit 1
is speculative prefetch. USB command IDs are inferred between read/completion callbacks, not
CBW tags. The separate transport records below now expose actual tags and endpoint completions.

### USB transport observation

Debug builds pin `espressif/tinyusb` to `0.21.0~2` and wrap four linker symbols:
`usbd_edpt_xfer`, `mscd_xfer_cb`, `mscd_reset` and `mscd_control_xfer_cb`. Normal builds
have neither the wrappers nor their linker flags or dependency pin. No vendored code is edited.
Each wrapped symbol also uses `--require-defined=__wrap_<symbol>` to pull the standalone
observer object from IDF's static archives; `--wrap` alone left all four wrappers unresolved
in the owner's build. Host code-generation tests check both flags, enabled and disabled.
These hooks rely on that version's internal API; revisit the pin and host harness together
when updating it. They preserve buffers, arguments and return values. This is for the current
single-MSC S3 device controller 0; ISR submissions are not observed. Pending endpoint metadata
uses a fixed 32-entry array in addition to the unchanged 3840-byte ring; no per-transfer allocation.

- `MSC_CBW` (22): each READ10's real host tag, byte offset, CBW `host_bytes`, CDB block
  count converted to `cdb_bytes` using our 512-byte sectors, opcode and direction.
- `MSC_CSW` (23): status-transfer completion, with real tag, controller-reported data bytes
  `transferred`, CSW `residue`, CSW status (0 passed, 1 failed, 2 phase error), data-transfer
  count and flags. Flags: 1 short/overlong completion, 2 transfer error, 4 submission rejected,
  8 transferred+residue differs from host length, 16 status tag differs from command tag.
  Every READ10 is reported; other commands are reported only on status or accounting errors.
- `MSC_RESET` (24): driver reset (also used on configuration changes) or a valid BOT reset
  request, including the abandoned command's tag and progress. Inferred read tracking is cleared
  so a new command cannot accidentally inherit an abandoned summary.
- `MSC_XFER_FAULT` (25): queued and actual lengths, raw TinyUSB completion result (0 success),
  tag and kind: 0 data, 1 status, 2 incoming CBW, 3 rejected submission. Successful data
  completions are counted, not individually logged.
- `MSC_CLEAR_HALT` (26): host endpoint-clear-halt recovery request and endpoint address.

CSW metadata is copied at submission because TinyUSB reuses its buffer. `MSC_CSW` is emitted
at completion and flags a short/failed status transfer; it is not proof the frame application
consumed the data correctly. READ10 controller byte totals are distinct from callback-supplied
bytes in `COMMAND_SUMMARY`. A failed/short data completion can occur even when all callbacks
returned successfully. Earlier MSC code ignored the transfer-result argument; these diagnostics
observe that condition without changing TinyUSB's handling. Record overwrite remains possible.

Restart the dev server before installing this debug firmware: the new event IDs use the same
DBG2 layout but an older receiver rejects them. Then build/install in ESPHome, leave the 25 ms
delay off, and capture a frame session from boot through the first corrupted image.

Image HTTP requests carry `X-ProxyStick-Debug: <boot-id-hex>:<fetch-id>` and the server logs
that ID alongside the range. Match it to `FETCH_SUMMARY`. The command field is the
most recently observed USB command, not proof that a speculative fetch belongs to it; use
offsets, timing and the speculative flag to establish that relationship. Device clocks need no wall
clock synchronisation. The same ESP boot ID appears on every batch for this boot.
