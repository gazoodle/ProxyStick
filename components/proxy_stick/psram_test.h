#pragma once

#include "esphome/core/defines.h"
#include "common.h"

namespace esphome::proxy_stick {

#ifdef USE_PROXY_STICK_PSRAM_TEST
bool prepare_psram_image(const Manifest &manifest);
bool psram_image_ready();
bool read_psram_image(uint64_t start, void *buffer, uint32_t size);
#endif

}  // namespace esphome::proxy_stick
