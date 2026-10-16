#include "events.h"

#include <cinttypes>
#include <cstdlib>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "freertos/task.h"

#include "http.h"
#include "trace.h"

namespace esphome::proxy_stick {

static const char *const TAG = "proxy_stick.events";
// The server sends at least a comment every 15 s; a little slack for a busy network.
static constexpr uint32_t LEASE_MS = 20000;
static constexpr uint32_t RETRY_MS = 5000;
// How long one read waits, so a rediscovery request or a lapsed lease is noticed within this.
static constexpr uint32_t READ_TICK_MS = 1000;
static constexpr size_t MAX_LINE = 256;
static constexpr uint32_t TASK_STACK_SIZE = 8192;
static constexpr UBaseType_t TASK_PRIORITY = 4;

bool EventStream::start(const std::string &endpoint) {
  this->endpoint_ = endpoint;
  this->lock_ = xSemaphoreCreateMutex();
  return this->lock_ != nullptr &&
         xTaskCreate(task_, "proxy_stick_events", TASK_STACK_SIZE, this, TASK_PRIORITY, nullptr) == pdPASS;
}

bool EventStream::connected() const {
  return this->open_.load() && millis() - this->heard_at_.load() < LEASE_MS;
}

EventStream::Found EventStream::take(Manifest &manifest) {
  if (this->lock_ == nullptr)
    return Found::NOTHING;  // Not started: the loop asks before the network is up.
  xSemaphoreTake(this->lock_, portMAX_DELAY);
  const Found found = this->found_;
  if (found == Found::MANIFEST)
    manifest = this->manifest_;
  this->found_ = Found::NOTHING;
  xSemaphoreGive(this->lock_);
  return found;
}

void EventStream::run_() {
  const std::string url = this->endpoint_ + "/proxystick/events";
  while (true) {
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.buffer_size = 512;
    config.buffer_size_tx = 512;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
      ESP_LOGE(TAG, "Unable to create the event stream's HTTP client");
    } else {
      add_common_headers(client);
      esp_http_client_set_header(client, "Accept", "text/event-stream");
      const std::string last = std::to_string(this->revision_);
      if (this->revision_ != 0)
        esp_http_client_set_header(client, "Last-Event-ID", last.c_str());

      const esp_err_t error = esp_http_client_open(client, 0);
      if (error != ESP_OK) {
        ESP_LOGW(TAG, "Cannot reach %s: %s", url.c_str(), esp_err_to_name(error));
      } else if (esp_http_client_fetch_headers(client) < 0 || esp_http_client_get_status_code(client) != 200) {
        ESP_LOGW(TAG, "%s answered %d", url.c_str(), esp_http_client_get_status_code(client));
      } else {
        ESP_LOGI(TAG, "Listening to %s", url.c_str());
        esp_http_client_set_timeout_ms(client, READ_TICK_MS);
        this->heard_at_.store(millis());
        this->open_.store(true);
        trace::milestone(trace::STREAM_OPEN);
        this->read_(client);
        this->open_.store(false);
        trace::milestone(trace::STREAM_LOST);
      }
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
    vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
  }
}

void EventStream::read_(esp_http_client_handle_t client) {
  char buffer[128];
  this->line_buffer_.clear();
  this->event_.clear();
  this->id_.clear();
  while (true) {
    const bool retry_due = this->retry_at_ != 0 && static_cast<int32_t>(millis() - this->retry_at_) >= 0;
    if (this->rediscover_.exchange(false) || retry_due)
      this->discover_();

    const int read = esp_http_client_read(client, buffer, sizeof(buffer));
    if (read > 0) {
      this->heard_at_.store(millis());
      for (int index = 0; index < read; index++) {
        const char character = buffer[index];
        if (character == '\n') {
          this->line_();
          this->line_buffer_.clear();
        } else if (character != '\r' && this->line_buffer_.size() < MAX_LINE) {
          this->line_buffer_ += character;
        }
      }
    } else if (read == -ESP_ERR_HTTP_EAGAIN) {
      if (millis() - this->heard_at_.load() >= LEASE_MS) {
        ESP_LOGW(TAG, "Nothing heard for %" PRIu32 " s; taking the server as gone", LEASE_MS / 1000);
        return;
      }
    } else {
      ESP_LOGW(TAG, "The server closed the event stream");
      return;
    }
  }
}

// One line of the stream: a field, a comment (the heartbeat), or the blank line ending an event.
void EventStream::line_() {
  const std::string &line = this->line_buffer_;
  if (line.empty()) {
    if (this->event_ == "refresh") {
      const uint32_t revision = std::strtoul(this->id_.c_str(), nullptr, 10);
      if (revision >= this->revision_)
        this->discover_();
      else
        ESP_LOGD(TAG, "Ignoring revision %" PRIu32 ", older than %" PRIu32, revision, this->revision_);
    }
    this->event_.clear();
    this->id_.clear();
    return;
  }
  if (line.front() == ':')
    return;
  const size_t colon = line.find(':');
  const std::string field = line.substr(0, colon);
  std::string value = colon == std::string::npos ? "" : line.substr(colon + 1);
  if (!value.empty() && value.front() == ' ')
    value.erase(0, 1);
  if (field == "event")
    this->event_ = value;
  else if (field == "id")
    this->id_ = value;
}

void EventStream::discover_() {
  Manifest manifest;
  const bool found = fetch_manifest(this->endpoint_, manifest) && validate_image(manifest);
  xSemaphoreTake(this->lock_, portMAX_DELAY);
  this->found_ = found ? Found::MANIFEST : Found::FAILED;
  if (found)
    this->manifest_ = manifest;
  xSemaphoreGive(this->lock_);

  if (found) {
    this->revision_ = manifest.revision;
    this->retry_at_ = 0;
    // A successful discovery is fresh proof the server is there, and may have taken a while.
    this->heard_at_.store(millis());
    ESP_LOGI(TAG, "Discovered revision %" PRIu32 " (%" PRIu64 " bytes)", manifest.revision, manifest.bytes);
  } else {
    this->retry_at_ = (millis() + RETRY_MS) | 1;
    ESP_LOGW(TAG, "Discovery failed; trying again in %" PRIu32 " s", RETRY_MS / 1000);
  }
}

}  // namespace esphome::proxy_stick
