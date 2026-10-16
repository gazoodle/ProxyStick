#pragma once

// The server's event stream (docs/WIRE.md, "Events"), on a task of its own. It is how the stick learns
// of a new image, and its lease on the server: `connected()` holds only while the stream has been heard
// from within LEASE_MS, so a server that vanishes without closing the connection lapses by itself.
//
// Discovery (fetching `current` and checking the image's boot sector) runs here too, never in
// ESPHome's main loop: the loop only takes the result.

#include <atomic>
#include <cstdint>
#include <string>

#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "common.h"

namespace esphome::proxy_stick {

class EventStream {
 public:
  enum class Found : uint8_t { NOTHING, MANIFEST, FAILED };

  bool start(const std::string &endpoint);
  bool connected() const;
  // The latest discovery's result, once: a validated manifest, or a failure.
  Found take(Manifest &manifest);
  // Discover again at the next chance, whether or not the server has said anything.
  void rediscover() { this->rediscover_.store(true); }

 protected:
  static void task_(void *stream) { static_cast<EventStream *>(stream)->run_(); }
  void run_();
  void read_(esp_http_client_handle_t client);
  void line_();
  void discover_();

  std::string endpoint_;
  SemaphoreHandle_t lock_{nullptr};
  std::atomic_bool open_{false};
  std::atomic<uint32_t> heard_at_{0};
  std::atomic_bool rediscover_{false};

  // Guarded by lock_: the result waiting for the loop.
  Found found_{Found::NOTHING};
  Manifest manifest_;

  // The task's own.
  uint32_t revision_{0};
  uint32_t retry_at_{0};
  std::string line_buffer_;
  std::string event_;
  std::string id_;
};

}  // namespace esphome::proxy_stick
