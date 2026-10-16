#include "common.h"

namespace esphome::proxy_stick {

Statistics stats;
std::atomic_bool media_present{false};
std::atomic<uint32_t> sector_count{0};
std::atomic_bool read_failed{false};

void Statistics::reset_session() {
  this->epoch.fetch_add(1, std::memory_order_relaxed);
  for (auto *counter : {&this->session_served_bytes, &this->network_fetched_bytes, &this->cache_hit_bytes,
                        &this->http_request_count, &this->http_request_failures, &this->http_request_time_us,
                        &this->http_first_byte_time_us, &this->http_data_event_count, &this->http_data_event_bytes,
                        &this->prefetch_hits, &this->prefetch_waits, &this->prefetch_discards,
                        &this->prefetch_failures, &this->usb_scsi_commands})
    counter->store(0, std::memory_order_relaxed);
  this->last_http_request_us.store(0, std::memory_order_relaxed);
}

}  // namespace esphome::proxy_stick
