#pragma once

// HTTP to the server (docs/WIRE.md). Two kept-alive clients, each used by one task only: the control
// client by the event stream's task (discovery), the image client by whichever task reads the image
// (the prefetch worker, or TinyUSB's task when prefetch is unavailable).

#include <string>

#include "esp_http_client.h"

#include "common.h"

namespace esphome::proxy_stick {

// "Bearer <token>", or empty for none. Set once, before any request.
void set_authorization(const std::string &token);
// Adds the headers every request carries.
void add_common_headers(esp_http_client_handle_t client);

bool fetch_image_range(const std::string &url, uint64_t start, void *buffer, uint32_t size);
bool fetch_manifest(const std::string &endpoint, Manifest &manifest);
bool validate_image(const Manifest &manifest);

}  // namespace esphome::proxy_stick
