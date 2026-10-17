#pragma once

// State shared by the stick's parts: the USB callbacks (TinyUSB's task), the block cache and its
// prefetch worker, the event stream's task, and the component's loop. Everything here is atomic or
// guarded where it is defined, because each part runs on a different FreeRTOS task.

#include <atomic>
#include <cstdint>
#include <string>

namespace esphome::proxy_stick {

constexpr uint16_t SECTOR_SIZE = 512;
constexpr uint32_t HTTP_TIMEOUT_MS = 3000;

struct Manifest {
  uint32_t revision{0};
  uint64_t bytes{0};
  std::string generation;
  std::string image_url;
};

// The counters behind the diagnostic sensors. "Session" ones start again at each mount.
struct Statistics {
  std::atomic<uint64_t> session_served_bytes{0};
  std::atomic<uint64_t> total_served_bytes{0};
  std::atomic<uint64_t> network_fetched_bytes{0};
  std::atomic<uint64_t> cache_hit_bytes{0};
  std::atomic<uint64_t> http_request_count{0};
  std::atomic<uint64_t> http_request_failures{0};
  std::atomic<uint64_t> http_request_time_us{0};
  std::atomic<uint64_t> http_success_time_us{0};
  std::atomic<uint64_t> http_first_byte_time_us{0};
  std::atomic<uint64_t> http_data_event_count{0};
  std::atomic<uint64_t> http_data_event_bytes{0};
  std::atomic<uint32_t> last_http_request_us{0};
  std::atomic<uint64_t> prefetch_hits{0};
  std::atomic<uint64_t> prefetch_waits{0};
  std::atomic<uint64_t> prefetch_wait_time_us{0};
  std::atomic<uint64_t> prefetch_discards{0};
  std::atomic<uint64_t> prefetch_failures{0};
  std::atomic<uint64_t> usb_scsi_commands{0};
  std::atomic<uint64_t> read_throughput{0};
  std::atomic<uint64_t> max_read_throughput{0};
  std::atomic<uint64_t> session_read_throughput{0};
  std::atomic<uint64_t> source_read_throughput{0};
  std::atomic<uint64_t> usb_transport_throughput{0};
  std::atomic<uint32_t> epoch{0};

  void reset_session();
};

extern Statistics stats;

// What the USB host is offered: written by the component's loop, read by the USB callbacks.
extern std::atomic_bool media_present;
extern std::atomic<uint32_t> sector_count;
// Set by a failed read on any task; the component's loop takes the medium away and rediscovers.
extern std::atomic_bool read_failed;

}  // namespace esphome::proxy_stick
