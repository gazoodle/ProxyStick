# ProxyStick performance work

Measurements from the prototype this repository grew from (October 2026), then this version's.
`KB/s` follows the decimal-style figures reported by `dd`; transferred byte counts and elapsed
times are exact.

## Performance history (prototype, AtomS3, no PSRAM)

| Stage | Action taken | Test/result | Change from original | What we learned |
|---|---|---:|---:|---|
| Original prototype | USB reads were serviced through the network with little useful read coalescing | Approximately **110 KB/s** | Baseline | The concept worked, but network round-trip cost dominated. |
| Server benchmark | Benchmarked the prototype server's real range-serving route and cached image | 512 B–1 MB ranges: **0.24–0.45 ms** median locally. Sequential 64 KiB ranges: about **230 MB/s** | — | The Python server and FAT image are not the bottleneck. Request size barely affects server cost. |
| Configurable block cache | Added a non-persistent 2^9–2^16 read-block control. USB still exposes 512-byte sectors, while the ESP fetches larger aligned ranges | At 2^16: 8 MiB in **29.11 s = 288,160 B/s** | **2.6×** | Fetching 64 KiB for small USB reads substantially amortises HTTP latency. |
| Initial diagnostics | Added range timings, request counts, fetched bytes, cache-hit bytes, Wi-Fi signal and instantaneous USB throughput | Average HTTP request approximately **110–169 ms**, despite the server taking under 1 ms locally | — | Most delay exists inside the ESP/network receive path, not in server processing. |
| HTTP receive-path tuning | Reused HTTP connections and enlarged/tuned the receive path so each callback delivered more data | 8 MiB in **18.99 s = 441,813 B/s** | **4.0×** | Average incoming callback chunk increased from **4,948 B** to **18,859 B**; average range time fell from **143.2 ms** to **73.8 ms**. |
| Background double buffering | Added two 64 KiB buffers and a FreeRTOS prefetch worker, allowing the next aligned range to download while TinyUSB consumes the current one | 8 MiB in **12.98 s = 646,289 B/s** | **5.9×** | This produced the best controlled sequential result. Cache hits reached **8,975,872 B of 9,009,152 B served**. |
| Prefetch diagnostics | Added prefetch hits, waits, discards and failures | Sequential test: **135 hits**, **38 waits**, **21 discards**, zero failures | — | Prefetch helps strongly, but occasional non-sequential host reads still waste requests and stall USB. |
| Memory measurement | Added free-heap and largest-block sensors | With two 64 KiB buffers: approximately **79.7 KB free**, largest block approximately **38.9 KB** | — | The original AtomS3 is close to the sensible internal-RAM limit. Larger general caching needs PSRAM or a more selective cache. |
| Real photo-frame workload | Tested frame enumeration and image loading rather than sequential `dd` | Maximum USB delivery: **237,331 B/s** | **2.2×** original | The frame workload is short, bursty and metadata-heavy rather than sequential. |
| Frame cache behaviour | Captured a complete successful frame session | **1,600,512 B served**, **1,568,256 B cache-hit**, **3,342,336 B fetched**, 51 HTTP requests | — | Nearly every USB byte came from cache, but the ESP fetched **2.09×** as much data as the frame consumed. |
| Frame HTTP behaviour | Measured the same successful session | First byte **22.6 ms**, complete range **343 ms**, average callback chunk **1,432 B** | — | The small callback chunks are ESP-IDF receive events, not server range sizes. Receive processing remains the dominant cost. |
| Verified network range size | Compared frame network bytes with request count | 3,342,336 ÷ 51 = exactly **65,536 B/request** | — | The frame test was using the intended 64 KiB cache blocks; the front-button refresh had not reverted it to 512 bytes. |
| Peak-rate diagnostics | Added `Maximum USB read throughput` alongside instantaneous throughput | Captured the frame's **237,331 B/s** peak | — | The instantaneous sensor normally returns to zero before HA is inspected; the maximum sensor preserves the useful session result. |

## Controlled sequential progression

| Implementation | Throughput | 8 MiB time | Relative speed |
|---|---:|---:|---:|
| Original | ~110 KB/s | ~76 s equivalent | 1.0× |
| 64 KiB block cache | 288 KB/s | 29.11 s | 2.6× |
| Tuned HTTP receive path | 442 KB/s | 18.99 s | 4.0× |
| Double-buffer prefetch | **646 KB/s** | **12.98 s** | **5.9×** |

## Position at the end of the prototype

The best verified sequential throughput on the non-PSRAM AtomS3 is **646 KB/s**, nearly six
times the original implementation. This is enough for the photo-frame use case, but remains far
below the physical capabilities of USB, Wi-Fi and the server.

The remaining performance problem is principally the ESP HTTP receive path:

- Server processing takes under 1 ms per request.
- ESP range requests commonly take 70–140 ms and sometimes exceed 300 ms.
- Wi-Fi strength varied, but good signal did not eliminate the latency.
- Sequential prefetch performs well.
- Photo-frame metadata access causes prefetch amplification, waits and discarded blocks.
- Internal RAM prevents simply adding a large general-purpose cache.

The most promising next experiment is using the AtomS3R's PSRAM to cache identified hot image
ranges (partition tables, FATs and directory sectors) while retaining the two-buffer sequential
path for photo data. This should improve frame behaviour without changing the server protocol.

The immediate-USB-enumeration work had not been performance-tested, and should be treated
separately from these throughput results.

## This version (2026-10-16)

The transport (HTTP client, lwIP settings, block cache, double-buffer prefetch) was carried over
from the prototype unchanged; discovery and the event stream moved off the main loop. Not yet
benchmarked. Readings from ordinary frame and Mac use, not a controlled test:

- Maximum USB read throughput **124–129 KB/s**. **Not comparable with the prototype's peaks:**
  this version's throughput sensors average over `update_interval` (5 s by default), the
  prototype's over 1 s, so short bursts read lower. Rerun the controlled 8 MiB test, and set
  `update_interval: 1s` when comparing peaks.
- HTTP range time 62–77 ms average for 64 KiB; first byte 26–35 ms; data chunks 11.6–16 KB.
- Prefetch: 8 hits against 21–22 discards and 24–26 waits on those (non-sequential) reads.
- Free heap ~54–58 KB, against the prototype's ~79.7 KB: the event-stream task (8 KiB stack)
  and the USB trace account for some of it; worth confirming before adding buffers.
