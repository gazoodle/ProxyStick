#include "diagnostics.h"

#if defined(USE_PROXY_STICK_DEBUG) && !defined(USE_PROXY_STICK_DEBUG_SERIAL)
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include "esp_random.h"
#include "esp_timer.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http.h"
#include "diagnostic_command.h"

namespace esphome::proxy_stick::diagnostics {
namespace {
// Debug wire v2: preserve RAM budget while adding command summary fields.
struct Record {
  uint64_t us;
  uint64_t offset;
  uint32_t sequence, command, event, size, value, fetch;
  uint32_t callbacks, flags;
};
static_assert(sizeof(Record) == 48);
struct Batch {
  uint32_t magic, boot, dropped, count;
  Record records[26];
} batch;
static_assert(sizeof(Batch) == 1264);
std::array<Record, 80> ring;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t head = 0, count = 0, dropped = 0, sequence = 0;
uint32_t boot;
std::atomic<uint32_t> command{0}, fetch_id{0}, fetching{0};
std::atomic<uint64_t> last_activity_us{0};
CommandSummary summary;
TaskHandle_t worker{nullptr};
std::string url;

void append_locked(Event event, uint64_t offset, uint32_t size, uint32_t value, uint32_t fetch,
                   uint32_t callbacks = 0, uint32_t flags = 0) {
  if (count == ring.size()) {
    head = (head + 1) % ring.size();
    count--;
    dropped++;
  }
  ring[(head + count) % ring.size()] = {static_cast<uint64_t>(esp_timer_get_time()), offset, ++sequence,
      command.load(), event, size, value, fetch, callbacks, flags};
  count++;
}

void summarise_locked(Event event) {
  // Low 16 bits: error/discontinuity flags; high 16: number of injected delays (saturated).
  const uint32_t flags = summary.flags | (std::min<uint32_t>(summary.delayed, 65535) << 16);
  append_locked(event, summary.offset, static_cast<uint32_t>(summary.bytes),
                static_cast<uint32_t>(esp_timer_get_time() - summary.started_us), summary.max_source_us,
                summary.callbacks, flags);
}

void upload_worker(void *) {
  while (true) {
    // Events wake the worker; a quiet 250 ms window permits a small upload.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250)) != 0) {}
    if (fetching.load() != 0 || esp_timer_get_time() - last_activity_us.load() < 250000) {
      xTaskNotifyGive(worker);
      continue;
    }
    portENTER_CRITICAL(&lock);
    if (summary.active && !summary.reported_idle) {
      summarise_locked(COMMAND_INCOMPLETE);
      summary.reported_idle = true;
    }
    batch.magic = 0x32474244;  // DBG2
    batch.boot = boot;
    batch.dropped = dropped;
    batch.count = std::min<uint32_t>(count, 26);
    for (uint32_t i = 0; i < batch.count; i++)
      batch.records[i] = ring[(head + i) % ring.size()];
    portEXIT_CRITICAL(&lock);
    if (batch.count == 0)
      continue;

    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.timeout_ms = 250;
    config.buffer_size = 256;
    config.buffer_size_tx = 512;
    config.disable_auto_redirect = true;
    auto client = esp_http_client_init(&config);
    bool ok = false;
    if (client != nullptr) {
      add_common_headers(client);
      esp_http_client_set_method(client, HTTP_METHOD_POST);
      esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
      esp_http_client_set_post_field(client, reinterpret_cast<const char *>(&batch), 16 + batch.count * 48);
      ok = esp_http_client_perform(client) == ESP_OK && esp_http_client_get_status_code(client) == 204;
      esp_http_client_cleanup(client);
    }
    if (ok) {
      const uint32_t last = batch.records[batch.count - 1].sequence;
      portENTER_CRITICAL(&lock);
      while (count != 0 && static_cast<int32_t>(ring[head].sequence - last) <= 0) {
        head = (head + 1) % ring.size();
        count--;
      }
      portEXIT_CRITICAL(&lock);
    } else {
      ESP_LOGW("proxy_stick.debug", "Diagnostic upload failed; retaining records for retry");
    }
    // Limit debug traffic and retries; image requests may resume during an upload.
    vTaskDelay(pdMS_TO_TICKS(2000));
    portENTER_CRITICAL(&lock);
    const bool pending = count != 0;
    portEXIT_CRITICAL(&lock);
    if (pending)
      xTaskNotifyGive(worker);
  }
}
}  // namespace

void begin(const std::string &endpoint) {
  boot = esp_random();
  url = endpoint + "/proxystick/debug";
  record(BOOT);
  if (xTaskCreate(upload_worker, "proxy_debug", 4096, nullptr, 1, &worker) != pdPASS) {
    worker = nullptr;
    ESP_LOGE("proxy_stick.debug", "Could not start diagnostic uploader");
  } else {
    xTaskNotifyGive(worker);
  }
}

void record(Event event, uint64_t offset, uint32_t size, uint32_t value, uint32_t fetch,
            uint32_t callbacks, uint32_t flags) {
  activity();
  portENTER_CRITICAL(&lock);
  append_locked(event, offset, size, value, fetch, callbacks, flags);
  portEXIT_CRITICAL(&lock);
  if (worker != nullptr)
    xTaskNotifyGive(worker);
}

void activity() { last_activity_us.store(esp_timer_get_time()); }

void command_begin(uint64_t offset) {
  activity();
  portENTER_CRITICAL(&lock);
  command.fetch_add(1);
  summary.start(esp_timer_get_time(), offset);
  portEXIT_CRITICAL(&lock);
  if (worker != nullptr)
    xTaskNotifyGive(worker);
}

void command_buffer(uint64_t offset, uint32_t size, uint32_t source_us, bool success, bool delayed) {
  activity();
  portENTER_CRITICAL(&lock);
  summary.buffer(offset, size, source_us, success, delayed);
  portEXIT_CRITICAL(&lock);
  if (!success || source_us >= 50000)
    record(success ? SLOW_READ : READ_FAILED, offset, size, source_us);
}

void command_complete(bool aborted) {
  activity();
  portENTER_CRITICAL(&lock);
  if (summary.active) {
    summarise_locked(aborted ? COMMAND_ABORTED : COMMAND_SUMMARY);
    summary.active = false;
  }
  portEXIT_CRITICAL(&lock);
  if (worker != nullptr)
    xTaskNotifyGive(worker);
}

uint32_t fetch_begin(uint64_t, uint32_t) {
  fetching.fetch_add(1);
  const uint32_t id = fetch_id.fetch_add(1) + 1;
  activity();
  return id;
}

void fetch_end(uint32_t id, uint64_t offset, uint32_t size, bool success, uint32_t elapsed, bool speculative) {
  activity();
  portENTER_CRITICAL(&lock);
  append_locked(FETCH_SUMMARY, offset, size, elapsed, id, 0, (success ? 0 : 1) | (speculative ? 2 : 0));
  portEXIT_CRITICAL(&lock);
  fetching.fetch_sub(1);
  if (worker != nullptr)
    xTaskNotifyGive(worker);
}

std::string correlation(uint32_t fetch) {
  char text[48];
  std::snprintf(text, sizeof(text), "%08lx:%lu", static_cast<unsigned long>(boot), static_cast<unsigned long>(fetch));
  return text;
}
}  // namespace esphome::proxy_stick::diagnostics
#endif  // USE_PROXY_STICK_DEBUG
