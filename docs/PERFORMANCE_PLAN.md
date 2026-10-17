# ProxyStick performance plan

The history and existing measurements are in `PERFORMANCE.md`. This document is the plan for
approaching the ESP32-S3's practical USB mass-storage limit without making the normal photo-frame
workload worse.

## Target

The USB full-speed bus signals at 12 Mbit/s, but framing, tokens and handshakes make that an
unreachable payload rate. Espressif reports about 0.925 MB/s from an ESP32-S3 MSC device with an
8 KiB FIFO and SD storage. Use these targets on each host:

- at least 0.90 MB/s from a RAM-backed USB-only benchmark;
- at least 1.20 MB/s from the network-only path, leaving margin for jitter;
- at least 0.85 MB/s in the raw end-to-end block test, with 0.90 MB/s as the stretch target;
- no regression in mounting, first-photo time, image correctness or recovery.

Raw sequential block speed is a diagnostic ceiling, not the product result. The primary result is
filesystem performance: mounting, enumeration and reading real files through the host filesystem.
A frame's metadata-heavy reads must not fetch substantially more data than the host consumes or
repeatedly empty the read-ahead buffer.

The prototype's headline throughput figures—about 110, 288, 442 and 646 KB/s—were all sequential
`dd` block-device tests. They provide raw-block history only; the first filesystem matrix run in
this plan establishes the application-visible baseline.

## Candidate path

Try one change at a time, in this order, and keep it only when the matrix shows a useful gain:

1. Measure USB-only, network-only and integrated ceilings independently.
2. Raise the TinyUSB MSC FIFO from 4 KiB to the S3 maximum of 8 KiB.
3. Test the high-throughput ESP-IDF TCP receive-window, mailbox, WiFi-buffer and power settings.
4. On PSRAM boards, replace two 64 KiB blocks with a rolling sequential runway fed by one long
   HTTP range. Keep USB and DMA-sensitive staging buffers in internal RAM.
5. Keep boot, FSInfo, FAT and directory sectors in a small metadata cache rather than predicting
   that every next read is the next linear block.
6. Give the USB task its own core and suitable priority; the normal MSC read callback should only
   copy ready bytes, never wait for HTTP.
7. Try two independent HTTP producers only if one long request cannot keep the runway filled.
8. For boards with a TF slot, stage and verify each immutable generation on SD, then serve USB
   locally. This is the deterministic route when WiFi jitter remains visible.

Do not begin with a new wire protocol, UDP, compression, server rewrites or zero-copy work. JPEG
data will not compress usefully, the server is already fast, and copies below 1 MB/s are unlikely
to be the limiting cost.

## Test articles

### Libraries

Build five libraries containing **1, 2, 10, 500 and 5,000 images**. Each larger library is a
prefix superset of the smaller one, so changing library size does not also change the first files
the host encounters.

Use one versioned, deterministic corpus with invented filenames and no private photos. Record its
generator version, file-size distribution, total source bytes, resulting FAT image bytes and image
generation. Keep image dimensions and JPEG sizes representative of the intended photo library;
repeating source bytes under unique names is acceptable because transport is uncompressed.

### Device variants

| ID | Device path | Purpose |
|---|---|---|
| `S3` | ESP32-S3, internal RAM, network-backed | Minimum-hardware baseline |
| `S3+PSRAM` | ESP32-S3 with PSRAM, network-backed | Larger runway and metadata cache |
| `S3+SD` | The same PSRAM/TF board as `S3+PSRAM`, image staged on SD | Isolate local storage from board differences |

Record the exact board, S3 revision, flash/PSRAM sizes, firmware version, CPU frequency, SD card
and SD bus mode. Keep the same power supply, server, access point, channel, distance and orientation.

### Hosts

| ID | Host | User-visible result |
|---|---|---|
| `STRICT` | The frame that power-cycles disappearing USB devices | Mounts without a disconnect and shows correct photos |
| `MAC` | The Mac test host | Mounts read-only; enumeration and controlled reads succeed |
| `FRAME` | The more tolerant, slower frame | Mounts and shows correct photos without timing-specific failures |

Record the host model and OS version outside the public repository if either is private. Test both
frames after every USB scheduling or media-change alteration.

## Filesystem workload matrix

Every cell is one device/host/library combination: **3 device paths × 3 hosts × 5 libraries = 45
cells**. Record each cell as `result; usable time; filesystem rate; fetched/served`, using `—` where
a measurement does not apply. Do not put raw block-read results in these tables.

### S3 results

| Images | `STRICT` | `MAC` | `FRAME` |
|---:|---|---|---|
| 1 | | | |
| 2 | | | |
| 10 | | | |
| 500 | | | |
| 5,000 | | | |

### S3 + PSRAM results

| Images | `STRICT` | `MAC` | `FRAME` |
|---:|---|---|---|
| 1 | | | |
| 2 | | | |
| 10 | | | |
| 500 | | | |
| 5,000 | | | |

### S3 + SD results

| Images | `STRICT` | `MAC` | `FRAME` |
|---:|---|---|---|
| 1 | | | |
| 2 | | | |
| 10 | | | |
| 500 | | | |
| 5,000 | | | |

### Board comparison

Complete this only from the cell results; do not substitute isolated USB or network benchmarks.

| Result | `S3` | `S3+PSRAM` | `S3+SD` |
|---|---:|---:|---:|
| Cells passed / 15 | | | |
| Median Mac filesystem read rate | | | |
| Worst Mac filesystem read rate | | | |
| Median strict-frame usable time | | | |
| Median tolerant-frame usable time | | | |
| Worst fetched/served amplification | | | |
| Runway-empty or prefetch-wait count | | | |
| Read failures or corrupt files | | | |
| Minimum free internal heap | | | |
| Publication-to-ready time | | | |

For `S3+SD`, report both generation staging time and USB usable time under
`publication-to-ready`; hiding staging would make the local-storage result misleading.

Run one cold screening pass over all 45 cells. Repeat a cell three times when it fails, varies by
more than 10%, or is used to decide whether a candidate change survives. This avoids hundreds of
routine runs while still repeating every consequential result.

## Mac filesystem baselines

Keep two mounted-filesystem workloads beside the raw-device `dd` result:

1. **Whole tree:** time `tar -cf /dev/null -C /Volumes/PROXYSTICK .` three times. This forces
   directory traversal and file reads through the mounted filesystem without measuring destination
   writes.
2. **Frame shuffle:** enumerate the image files once, shuffle them with a recorded seed, then open
   and read each selected file to EOF in 64 KiB chunks. Record first-byte and complete-file time for
   each image as well as the whole-run rate.

Clear drive statistics immediately before each run. Compare the known file bytes with the increase
in **Session bytes served**: repeated macOS reads can be satisfied from the host page cache. A run
whose device byte count does not rise by approximately the bytes read is a host-cache result, not a
ProxyStick filesystem result. Record it separately. For comparable cold runs, unmount and remount
the volume before each repetition, while still using the device counter to prove that reads reached
the stick.

The eventual `tools/frame-bench` CLI should implement only the deterministic shuffle workload,
accepting a mount path, image count and seed. It should report host timings and bytes; the device
sensors remain the authority for how those bytes were supplied. Start with scan-once followed by
whole-file reads. Add header probes, rescans or dwell time only when a real frame trace shows that
behaviour. This makes it a repeatable pressure test, not a claim to emulate either physical frame.

## Procedure for each cell

1. Publish the named library and record its generation and FAT image size.
2. Start from an empty volatile device cache. For `S3+SD`, stage and verify the generation before
   timing USB, and measure staging separately.
3. Attach the host and start timing at USB power-on.
4. Record time to enumeration, medium ready and usable content (first correct photo on a frame;
   mounted volume and completed directory listing on the Mac).
5. Exercise the workload:
   - frames: allow the initial scan, then advance through up to ten photos or the whole library,
     whichever is smaller;
   - Mac: list the complete root directory, then read whole photo files through the mounted
     filesystem until at least 64 MiB has been read, or the whole library when it is smaller. Use
     the same deterministic filename order in every run.
6. Confirm displayed/read data against the published generation and inspect for corrupt images,
   USB resets, read failures and watchdog warnings.
7. Capture the counters below before removing power.

The throughput sensors report the latest completed successful SCSI `READ(10)`, the command maximum,
and successful session bytes from the first read through the latest completed read. ESPHome's
publishing interval does not define these measurements. Use host-observed transferred byte counts
and elapsed monotonic time for the authoritative filesystem and block results.

The layer diagnostics use the same session reset and are also independent of publishing:

- **HTTP fetch throughput** is successfully fetched range bytes divided by their HTTP request time;
  it includes server, HTTP and WiFi, not WiFi alone.
- **Block source throughput** is successful read bytes divided by time spent synchronously obtaining
  those bytes from the cache/block layer, including prefetch waits.
- **USB transport throughput** is successful read bytes divided by the remainder of completed
  `READ(10)` command time after synchronous source time is removed. It includes TinyUSB scheduling
  and status, and is an operational estimate rather than a PHY measurement.
- **Session USB read throughput** spans the first read callback through the latest completed read,
  so it includes inter-command gaps and is the device-side counterpart to a host `dd` result.

The stages overlap through background prefetch, so their times and rates must not be added together.
Keep `usb_trace: false` during performance tests; enable its NVS flight recorder only when diagnosing
USB negotiation or power loss.

The device also keeps a RAM-only Space-Saving table of the 64 most frequently touched 4 KiB image
pages (768 bytes). **Dump hot blocks** logs each candidate's LBA range and its minimum-to-estimated
touch count. Clear statistics starts a new table. Use the repeated pages to choose metadata-cache
ranges; one-pass sequential photo data has a low confidence bound and should not drive that choice.

## Measurements

Record these for every cell:

- pass/fail and failure reason;
- time to USB enumeration, medium ready and usable content;
- exact file bytes and time for the filesystem workload, plus effective B/s;
- maximum and p95 MSC callback duration, callback size and time waiting for source data;
- runway low-water mark and empty count, where applicable;
- served bytes, network-fetched bytes and fetched/served amplification;
- cache-hit bytes; prefetch hits, waits, discards and failures;
- HTTP request count, average and p95 first-byte time, full-request time and receive-event size;
- WiFi RSSI, free internal heap, largest internal block and free PSRAM;
- resets, disconnects, short reads, corrupt files and watchdog warnings;
- for SD: staging time, verified bytes, read rate and SD errors.

The aggregate averages already exposed by the component are insufficient for jitter diagnosis.
The benchmark build may collect a bounded latency histogram, but normal firmware must not retain a
large trace merely for this plan.

## Block-level and transport benchmarks

Run these on the Mac before interpreting the filesystem matrix. They isolate the ceiling and the
slow layer, but never substitute for a filesystem result.

### Raw block results by device

Record `bytes; elapsed; B/s; stalls` for each applicable cell.

| Raw block result | `S3` | `S3+PSRAM` | `S3+SD` |
|---|---:|---:|---:|
| RAM-backed MSC, 4 KiB FIFO | | | |
| RAM-backed MSC, 8 KiB FIFO | | | |
| Integrated sequential 8 MiB read | | | |
| SD-backed sequential 8 MiB read | — | — | |

### Transport isolation

| Benchmark | Question answered | Pass condition |
|---|---|---|
| Network into memory, repeated 64 KiB ranges | Present producer ceiling | Rate and latency distribution recorded |
| Network into memory, one long range | Cost of HTTP request boundaries | At least 1.20 MB/s with useful jitter margin |
| SD into memory | SD producer ceiling | Faster than the measured USB ceiling |

If RAM-backed MSC misses its target, work on TinyUSB buffers, scheduling and task affinity before
touching HTTP. If long-range networking misses its target, work on TCP/WiFi settings before adding
cache machinery. If both pass but the integrated test fails, runway starvation or task interaction
is the problem. If block tests pass but filesystem tests do not, investigate the host's metadata
read pattern, cache amplification and latency rather than pursuing a higher sequential number.

## Acceptance gates

A candidate change survives only when:

- both frames and the Mac pass all five library sizes;
- no host sees a corrupt or mixed-generation sector;
- the strict frame never observes an avoidable USB disappearance;
- Mac filesystem rate or frame time to usable content improves without a material regression on
  another host or library size;
- fetched/served amplification does not regress without a measured user-visible benefit;
- three repeated decisive runs vary by no more than 10%, or the variance is explained;
- 1 s and 5 s ESPHome publishing intervals preserve the same command-level maximum for the same
  controlled workload;
- failure still removes the medium safely rather than returning invented or stale data.

A faster raw block test alone never passes a candidate. The winning design must improve the
filesystem matrix and should approach the measured RAM-backed USB ceiling. If it cannot do so
reliably after the single-request PSRAM runway and transport tuning, prefer verified SD staging
over a bespoke network protocol.
