# Handover

Last updated: 2026-10-19. **Owner stopped the investigation. Do not resume without an explicit request.**

## Stopped: unresolved corruption and unusable latest serial capture

Codex failed to track the problem to a demonstrated cause. Too much time went into expanding
instrumentation and troubleshooting the instrumentation itself, with prolonged silent work and
inadequate progress feedback. The owner ran out of time and patience and chose to stop.
The owner's requested summary: "Codex failed to track this problem and the owner got so bored
he nearly inserted a $5 wrench into the computer."

This commit preserves an unfinished investigation, **not a fix or a verified release**.
Checkpoint validation: all 51 host tests passed in 6.53 seconds with localhost sockets allowed;
ruff and pyright are clean. The first sandboxed run failed on denied socket binds, not a
demonstrated application regression. No firmware build or hardware fix is implied by these checks.
Keep version 0.5.0 unreleased. Do not flash, rebuild or start more diagnostic experiments on
the owner's behalf. Firmware builds belong on the owner's HA box, never the agent's machine.

What is established:

- The strict frame corrupts photos; the Mac and tolerant frame do not. A conventional USB
  stick written with the same image displays cleanly on the strict frame.
- A verified whole-image PSRAM snapshot still corrupts; this removes network fetch latency
  from those reads. Buffer sizes of 4 KiB and 8 KiB both failed; minimal callbacks also failed.
- A forced 25 ms read delay worsens corruption and, with a larger library, affects photo count.
  The issue is not demonstrably confined to JPEG data or decoding.
- The last usable capture, after recovering one line split by the collector's own safe-mode
  log, contains 225 consecutive records, zero target drops and 22 successful READ10 commands
  with matching byte counts. Startup resets and MODE SENSE-related clear-halts precede reads.
  This does not prove payload correctness, reveal the frame driver's reaction, or diagnose
  why this host behaves differently.

Current blocker and exact last observation:

- Full TinyUSB level-2 logging, small SCSI/control reply dumps, per-IN-buffer timing and
  host-side capture/comparison tools are implemented and host-tested, but there is **no usable
  full trace**. The latest raw capture was entirely gibberish, not evidence about USB faults.
- Raising the Lite's USB console to 3000000 baud broke readability. Reverting its configuration
  and the watcher default to the previously working 1500000 did **not** restore readable output
  after the owner reported installing the update. Do not present that reversion as a fix.
- Grove target TX/collector RX is configured at 1500000; USB output/watcher currently 1500000.
  Actual running firmware and application baud remain unverified. No more speculative rate changes.
- Last read-only boot check at 115200 decoded the Lite's ROM/bootloader: normal SPI flash boot,
  ESP-IDF 5.5.5, 4 MB flash, selected application at offset 0x1d0000. It is not stuck in ROM
  download mode. Neither 1500000 nor 3000000 yielded a readable collector application startup.
  This localises an instrumentation problem but does not establish its cause.
- The Atom Lite has WiFi/password-protected OTA; its first WiFi firmware and OTA TCP listener
  were verified earlier. Later successful installation/version is not independently verified.
  Grove +5V is physically cut, ground shared. Only the Lite was reset during recent checks;
  no further S3R/frame firmware changes were made by the agent. Existing raw captures remain local.

If the owner elects to return: first establish the collector's running app identity and actual
UART output from evidence, without changing several variables. Only after its own console and
forwarded stream are readable should another strict-frame capture be attempted. The Mac can
be the good reference because the tolerant frame cannot conveniently reach the serial collector.
Native logs remain software-level observations, not a USB wire analyser or host-driver trace.

The shared instructions now require feedback within 30 seconds, bounded tool waits of 20 seconds
where supported, and an explicit obstacle report rather than another silent investigation loop.
No running user-owned server/configuration is shut down as part of shelving this work.

## Earlier investigation notes (historical, not instructions to continue)

The notes below retain the sequence of experiments. Any "next action", "ready" or "verified"
statement below is superseded by the stopped status and unresolved instrumentation above.

Immediate next action (2026-10-19): owner builds/OTA-installs **monitor only**. The captured
`frame-full.log` was entirely undecodable. Neither expected rate decoded the collector's
own startup; 115200 decoded its ROM boot. Restore monitor USB output and watcher default to
the previously verified 1500000 baud; retain Grove input/S3R output at 1500000. This is a
rate-reversion experiment, not a proven root-cause fix. S3R full diagnostics are unchanged.
Equal input/output baud reduces headroom, so verify readability before another frame session
and check loss counters under load. Existing capture is retained, not overwritten.

Latest diagnostic iteration (2026-10-19): full serial software trace is ready, not built or
installed. `debug_serial` enables native TinyUSB level-2 USBD/MSC logging through a generated
configuration extension (vendor sources unchanged), per-IN-buffer queue/completion timing,
up-to-64-byte SCSI/control responses with explicit truncation, control reply requests and
endpoint stall/clear calls. Native `T` frames and structured `P` frames share loss accounting;
ISR-native printf attempts are counted as omitted, never formatted in interrupt context.
Grove is now 1500000 baud, monitor USB output 3000000; its existing HA YAML was updated while
preserving WiFi/OTA secrets. Update the monitor via OTA first, then S3R. Collector forwards
whole diagnostic lines to prevent its own logs splitting records. Watcher defaults to 3000000;
`tools/serial-compare <reference.log> <problem.log>` compares integrity, commands, response
bytes, read ranges, timing and native decisions. Use Mac as reference: tolerant frame is too
far from the USB collector. 28 focused tests pass, lint clean; firmware linkage/rates and
actual good-vs-bad comparison remain unverified. See the first section of `docs/DEBUG.md`;
older notes below retain historical baud rates and firmware status.

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
  Runtime off, refresh and image replacement now keep TinyUSB attached, report no medium, then
  raise UNIT ATTENTION when the medium returns. This still needs both-frame hardware verification.
- `usb_trace: true` stays the tool for any new host: it keeps the last 16 boots in flash.

## Not verified: needs the hardware

25 ms injected delay made almost every image fail on the strict frame (owner, 2026-10-19).
Version 0.5.0 adds the temporary RAM diagnostic back-channel and cyan server logs; see
`docs/DEBUG.md` for enable/disable, event meanings and gaps. `tools/dev` enables reception.
Firmware compilation and behaviour remain for the owner's live ESPHome build and hardware test.
First-photo capture lost 733 detailed events. The next iteration uses per-command summaries,
fetch summaries and slow/failing-read details in the same 3840-byte ring budget. Temporary wire
format DBG2 requires a server restart and device rebuild together; unreleased version stays 0.5.0.
Local `tools/dev-device-s3r.yaml` adds a separate AtomS3R identity with 8 MB flash, octal 80 MHz
PSRAM and a free-PSRAM sensor. Restarting `tools/dev` generates `proxystick-dev-s3r.yaml` beside
the S3U config; first flash over USB. Device YAML remains gitignored and uses existing HA secrets.
An unstaged, one-off `debug_psram_image` experiment preloads a verified image prefix into PSRAM,
reconstructs the zero tail and serves offline (normal lease bypassed). USB now attaches through
the normal early startup path, with no medium until verification succeeds; it never disconnects
to present the snapshot. The delayed-attachment experiment left USB invisible for 13–16 s;
the suspected frame boot ended before 1 s, before network or USB attachment. Early attachment
is restored for the next hardware test (2026-10-19); frame tolerance remains unverified.
Only the local S3R YAML enables it; see `docs/DEBUG.md`. No server changes or firmware builds.
Owner confirms frame images remain corrupted with the PSRAM snapshot (2026-10-19).
Mac read-only filesystem checks against files extracted from the SHA-256-verified server image
passed: five complete passes over both files (4,057,006 bytes/pass), sequential 4/8/64 KiB and
shuffled 8/64 KiB reads, plus 256 unaligned 513-byte reads and EOF checks. Each file descriptor
used macOS F_NOCACHE; disk I/O counters increased. Full-pass rates were 214–860 kB/s.
The separate whole-device raw SHA-256 check could not run: raw-device access requires the
owner's administrator password. These results do not reproduce the frame-specific corruption
or verify what the frame receives; command lengths/status and debug interference remain open.
The next debug iteration adds real READ10 CBW tags/lengths, CSW status/residue and controller
byte totals, anomalous endpoint completions, BOT/driver resets and clear-halt requests. Four
debug-only linker wrappers observe TinyUSB without editing it; debug builds pin its internal
API to `0.21.0~2`. Transport resets also clear inferred read tracking. Server restart is needed
for the new DBG2 event IDs before the owner's next ESPHome build/install. See `docs/DEBUG.md`;
firmware linkage and frame behaviour still require that hardware run.
Verification: 12 focused host tests pass, including the real wrapper source with a simulated
USB controller and a debug-off object-symbol check; ruff and pyright are clean. No firmware
build or device update was performed.
The owner's build reached compilation but failed to link all four standalone `__wrap_*`
functions, including after a clean (2026-10-19). Debug code generation now also passes
`--require-defined` for each wrapper to force archive extraction. Added debug-on/off flag
regressions; the enum/integer warnings and PSRAM-only unused `read_range` warning are removed.
The corrected firmware link succeeded: the owner's capture contains real CBW/CSW records.
All 48 paired captured READ10s match lengths with successful status, zero residue/flags and
delay off. First 22 reads are complete; 129 dropped records leave gaps later. Both photos
still corrupt. The owner then wrote the unchanged server image to a conventional USB stick:
both photos display cleanly on the strict frame (2026-10-19). Geometry alone is insufficient
to explain the corruption, but a geometry/transport interaction remains possible.
Next controlled comparison changes only `CONFIG_TINYUSB_MSC_BUFSIZE` from 8192 to 4096;
PSRAM image, tracing and delay-off remain unchanged. Code-generation tests check 4096.
The owner must build/install and test; no server restart needed, no firmware build by agents.
The 4 KiB comparison also corrupts both photos (owner, 2026-10-19). Next is the unstaged
`debug_minimal_usb` test, enabled only in the local S3R device YAML: it replaces the normal
USB callbacks with direct verified-PSRAM reads, suppresses the USB cache-byte counter and
omits statistics, traces, back-channel, delay probe and UNIT ATTENTION notifications.
Descriptors, TinyUSB pin, 4 KiB buffer and early attachment stay the same. ESPHome and the
initial image preload remain; this is not the stock standalone example. Look for
`MINIMAL USB TEST` plus `PSRAM SNAPSHOT READY`; USB counters intentionally stay zero.
Set the flag false and rebuild to revert. The owner's rollback-disabled settings in both
board YAMLs are preserved and verified in the generated S3R YAML. See `docs/DEBUG.md`.
Verification: 20 focused host tests pass (minimal callbacks, bounds/write rejection, normal
callback exclusion and code-generation guards); ruff/pyright clean. No firmware build or flash.
Minimal mode still corrupts the photos. Next investigation uses an independent Atom Lite
serial collector, Mac-powered with Grove red +5V physically removed (owner confirmed).
`example/serial-monitor.yaml` and `components/serial_monitor/` implement UART1 RX GPIO26 at
921600 -> USB UART0 at 1500000, byte-preserving. Native UART event task,
8192-byte input buffer and explicit overrun/output-error reporting live on the collector.
`proxystick-monitor.yaml` was added to the owner's HA ESPHome share for a manual factory
download; no existing configs overwritten. YAML validates and host forwarding/failure/cleanup
test passes. Owner-built monitor factory firmware flashed to the verified ESP32-PICO-D4
Atom Lite on 2026-10-19; esptool verified the written hash. Flashing at 460800 failed before
writing, retry at 115200 succeeded. USB console at 1500000 reports `MONITOR READY` and
successful setup. The initially unconfigured Grove input produced UART break errors and zero
bytes; later captures contain valid target records. The local S3R YAML enables `debug_serial: true`, retaining minimal
direct PSRAM callbacks. `diagnostics_serial.cpp` replaces the WiFi uploader with TX-only GPIO2
UART1 at 921600, starting before USB attachment; no old diagnostic ring, worker or flash trace.
The FIFO-only capture lost 94 of 249 records. It now uses IDF's native 4096-byte TX buffer,
zero-wait producer mutex and conservative capacity check before driver writes, outside the
summary spinlock; sequence gaps and cumulative drops still expose overflow. The same four
TinyUSB wrappers report real READ10 CBW/CSW,
resets, clear-halt and faults, plus full non-READ CDBs and all CSWs. No payload CRC or full response
dump yet. `tools/serial-watch <port>` saves raw bytes and shows decoded decimal timings, faults,
gaps and quiet summaries; `--verbose` shows routine commands. It clears duration matching after
gaps. The owner's capture replays as 155 valid records with the same 94 reported drops.
The monitor example and existing HA YAML now enable WiFi and password-protected OTA, no native
API, WiFi power saving off and reboot-on-WiFi-loss disabled. Existing HA secrets resolve.
The WiFi/OTA monitor image was USB-flashed on 2026-10-19 (748544 bytes; written hash verified).
Earlier downloads containing only `401: Unauthorized` were rejected before flashing. Startup
reports `MONITOR READY`, setup success and WiFi connected; mDNS resolves and the OTA TCP listener
is reachable. An 18-second check forwarded 46 target records with zero collector capture errors
or UART warnings; this is not a sustained peak-load test. Subsequent updates can use OTA;
an actual OTA installation has not been exercised. OTA interrupts capture; the watcher treats `MONITOR READY`
as a collector start/restart boundary and clears pending timing matches. Receive worker unchanged.
24 focused host tests pass; ruff/pyright clean and the monitor WiFi/OTA YAML validates.
Owner must build/install both devices and measure capture integrity with WiFi
before drawing conclusions. No firmware builds or updates by agent in this iteration. The running
dev watcher propagated both debug flags to HA and preserved rollback-off. Equivalent local
S3R YAML validates; validating the share directly was blocked by its external-component cache
permissions, so no share cache was changed. Ruff/pyright clean; focused host checks cover
UART FIFO drops, complete-line bounds, setup failure, wrapper/CDB observation and minimal callbacks.

The **USB read delay (25 ms)** switch is a diagnostic for the strict frame's non-deterministic
image corruption. It is always off after boot. When on, each successfully filled TinyUSB read
buffer waits 25 ms before being returned, to reveal host timeout or short-read handling.

`tools/dev` now checks its two YAML inputs every 2 s and rewrites the generated HA configs
without restarting either the fake Git host or the image server. The fake Git host itself was
already current on every fetch; only the one-shot YAML generation was stale.

Nothing in the device code has run on a device yet. The prototype's test plan still applies
to this version:

1. `tools/dev <folder>`, Install from ESPHome on the box, confirm the frame mounts.
2. Stop the server: the medium goes within 20 s (the lease); restart it: it comes back.
3. `REFRESH` with no change: the USB device stays configured, reports no medium, then the medium
   returns with UNIT ATTENTION. Change a file and `REFRESH`: the same, with the new contents.
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
- **Rollback is off on the two burnt sticks** (S3U, S3R; 2026-10-19). The strict frame
  power-cycles a stick whenever an OTA reboot makes it disappear, inside the new firmware's
  one-minute trial, so ESP-IDF's rollback always reverted installs made in that frame. Their
  device files set `esp32: framework: advanced: enable_ota_rollback: false`; ESPHome's safe
  mode (10 failed boots, then WiFi and OTA only) is the safety net, and USB-OTG download (hold
  reset 2 s) the last resort. The change itself had to be flashed over USB once: OTA does not
  rewrite the bootloader, and an image installed by rollback-on firmware stays on trial for
  ever under a rollback-off build, rolling back at the next reset.
- **Sticks with rollback on (the unburnt AtomS3): after every Install, leave the stick on the
  Mac until "Boot seems successful" (60 s)**, or the next power loss rolls the install back.
- Firmware is built by the owner on the live ESPHome box; agents do not run `tools/compile`.
- macOS asks once to allow incoming connections, for Python (the server) and git (the fake git
  host), the first time `tools/dev` runs.
