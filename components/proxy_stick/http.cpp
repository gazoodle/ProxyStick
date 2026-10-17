#include "http.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::proxy_stick {
namespace {

static const char *const TAG = "proxy_stick.http";
constexpr uint32_t IMAGE_HTTP_BUFFER_SIZE = 32 * 1024;
constexpr size_t MAX_JSON_BYTES = 2048;

std::string authorization;
esp_http_client_handle_t image_client{nullptr};
std::string image_client_url;
esp_http_client_handle_t control_client{nullptr};

struct Response {
  uint8_t *buffer{nullptr};
  size_t capacity{0};
  size_t received{0};
  std::string *text{nullptr};
  bool track_data{false};
  bool saw_data{false};
  uint32_t started_us{0};
  uint32_t first_data_us{0};
  uint32_t data_events{0};
  uint32_t data_bytes{0};
};

esp_err_t http_event(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len == 0)
    return ESP_OK;
  auto *response = static_cast<Response *>(event->user_data);
  if (response->track_data) {
    if (!response->saw_data) {
      response->saw_data = true;
      response->first_data_us = micros() - response->started_us;
    }
    response->data_events++;
    response->data_bytes += event->data_len;
  }
  if (response->text != nullptr) {
    if (response->text->size() + event->data_len > MAX_JSON_BYTES)
      return ESP_FAIL;
    response->text->append(static_cast<const char *>(event->data), event->data_len);
    return ESP_OK;
  }
  if (response->received + event->data_len > response->capacity)
    return ESP_FAIL;
  std::memcpy(response->buffer + response->received, event->data, event->data_len);
  response->received += event->data_len;
  return ESP_OK;
}

void reset_response(Response &response) {
  response.received = 0;
  if (response.text != nullptr)
    response.text->clear();
}

void close_client(esp_http_client_handle_t &client) {
  if (client != nullptr)
    esp_http_client_cleanup(client);
  client = nullptr;
}

esp_http_client_handle_t open_client(const std::string &url, int buffer_size) {
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.event_handler = http_event;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.buffer_size = buffer_size;
  config.buffer_size_tx = SECTOR_SIZE;
  config.keep_alive_enable = true;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client != nullptr)
    add_common_headers(client);
  return client;
}

void format_range(char (&range)[48], uint64_t start, uint32_t size) {
  std::snprintf(range, sizeof(range), "bytes=%" PRIu64 "-%" PRIu64, start, start + size - 1);
}

int perform(const std::string &url, const char *range, Response &response) {
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    if (control_client == nullptr) {
      control_client = open_client(url, SECTOR_SIZE);
      if (control_client == nullptr)
        continue;
    } else if (esp_http_client_set_url(control_client, url.c_str()) != ESP_OK) {
      close_client(control_client);
      continue;
    }

    reset_response(response);
    esp_http_client_set_user_data(control_client, &response);
    if (range != nullptr)
      esp_http_client_set_header(control_client, "Range", range);
    else
      esp_http_client_delete_header(control_client, "Range");
    const esp_err_t error = esp_http_client_perform(control_client);
    if (error == ESP_OK)
      return esp_http_client_get_status_code(control_client);
    close_client(control_client);
  }
  return 0;
}

bool read_range(const std::string &url, uint64_t start, void *buffer, uint32_t size) {
  char range[48];
  format_range(range, start, size);
  Response response{static_cast<uint8_t *>(buffer), size};
  return perform(url, range, response) == 206 && response.received == size;
}

std::string absolute_url(const std::string &endpoint, const std::string &url) {
  if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
    return url;
  // A path is relative to the endpoint's origin (scheme://host:port), not to the endpoint's own path.
  const size_t scheme = endpoint.find("://");
  const size_t path = scheme == std::string::npos ? std::string::npos : endpoint.find('/', scheme + 3);
  const std::string origin = path == std::string::npos ? endpoint : endpoint.substr(0, path);
  if (!url.empty() && url.front() == '/')
    return origin + url;
  return endpoint + "/" + url;
}

}  // namespace

void set_authorization(const std::string &token) { authorization = token.empty() ? "" : "Bearer " + token; }

void add_common_headers(esp_http_client_handle_t client) {
  esp_http_client_set_header(client, "Accept-Encoding", "identity");
  if (!authorization.empty())
    esp_http_client_set_header(client, "Authorization", authorization.c_str());
}

bool fetch_image_range(const std::string &url, uint64_t start, void *buffer, uint32_t size) {
  char range[48];
  format_range(range, start, size);
  for (uint8_t attempt = 0; attempt < 2; attempt++) {
    if (image_client == nullptr || image_client_url != url) {
      close_client(image_client);
      image_client = open_client(url, IMAGE_HTTP_BUFFER_SIZE);
      if (image_client == nullptr)
        continue;
      image_client_url = url;
    }

    Response response{static_cast<uint8_t *>(buffer), size};
    response.track_data = true;
    esp_http_client_set_user_data(image_client, &response);
    esp_http_client_set_header(image_client, "Range", range);
    const uint32_t statistics_epoch = stats.epoch.load(std::memory_order_relaxed);
    response.started_us = micros();
    const esp_err_t error = esp_http_client_perform(image_client);
    const uint32_t elapsed = micros() - response.started_us;
    const bool success = error == ESP_OK && esp_http_client_get_status_code(image_client) == 206 &&
                         response.received == size;
    if (stats.epoch.load(std::memory_order_relaxed) == statistics_epoch) {
      stats.http_request_count.fetch_add(1, std::memory_order_relaxed);
      stats.http_request_time_us.fetch_add(elapsed, std::memory_order_relaxed);
      stats.http_first_byte_time_us.fetch_add(response.first_data_us, std::memory_order_relaxed);
      stats.http_data_event_count.fetch_add(response.data_events, std::memory_order_relaxed);
      stats.http_data_event_bytes.fetch_add(response.data_bytes, std::memory_order_relaxed);
      stats.last_http_request_us.store(elapsed, std::memory_order_relaxed);
      if (success) {
        stats.network_fetched_bytes.fetch_add(size, std::memory_order_relaxed);
        stats.http_success_time_us.fetch_add(elapsed, std::memory_order_relaxed);
      } else {
        stats.http_request_failures.fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (success)
      return true;
    ESP_LOGW(TAG, "Range read at %" PRIu64 " failed: %s, status %d, %u of %" PRIu32 " bytes", start,
             esp_err_to_name(error), esp_http_client_get_status_code(image_client),
             static_cast<unsigned>(response.received), size);
    close_client(image_client);
    image_client_url.clear();
  }
  return false;
}

bool fetch_manifest(const std::string &endpoint, Manifest &manifest) {
  std::string body;
  Response response;
  response.text = &body;
  const int status = perform(endpoint + "/proxystick/current", nullptr, response);
  if (status != 200) {
    ESP_LOGW(TAG, "Discovery failed: %s/proxystick/current answered %d", endpoint.c_str(), status);
    return false;
  }
  const bool valid = json::parse_json(body, [&](JsonObject root) -> bool {
    if (root["protocol"].as<int>() != 0 || root["sector_size"].as<int>() != SECTOR_SIZE ||
        !root["image_url"].is<const char *>() || !root["generation"].is<const char *>())
      return false;
    manifest.revision = root["revision"].as<uint32_t>();
    manifest.bytes = root["bytes"].as<uint64_t>();
    manifest.generation = root["generation"].as<std::string>();
    manifest.image_url = absolute_url(endpoint, root["image_url"].as<std::string>());
    return manifest.revision > 0 && manifest.bytes > 0 && manifest.bytes % SECTOR_SIZE == 0 &&
           manifest.bytes / SECTOR_SIZE <= UINT32_MAX && !manifest.generation.empty();
  });
  if (!valid)
    ESP_LOGW(TAG, "Discovery failed: the server's current image is not one this stick can serve");
  return valid;
}

bool validate_image(const Manifest &manifest) {
  std::array<uint8_t, SECTOR_SIZE> boot{};
  if (read_range(manifest.image_url, 0, boot.data(), boot.size()) && boot[510] == 0x55 && boot[511] == 0xAA)
    return true;
  ESP_LOGW(TAG, "Revision %" PRIu32 ": its boot sector could not be read or is not a disk's", manifest.revision);
  return false;
}

}  // namespace esphome::proxy_stick
