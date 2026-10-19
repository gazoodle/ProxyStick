#include "psram_test.h"

#ifdef USE_PROXY_STICK_PSRAM_TEST
#include <array>
#include <atomic>
#include <cinttypes>
#include <memory>
#include "esp_heap_caps.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "http.h"
#include "psram_layout.h"

namespace esphome::proxy_stick {
namespace {
const char *const TAG = "proxy_stick.psram_test";
std::atomic_bool ready{false};
std::unique_ptr<uint8_t, decltype(&heap_caps_free)> image{nullptr, heap_caps_free};
uint32_t prefix_bytes{0};
uint64_t volume_bytes{0};
std::string pinned_generation, pinned_url;
}  // namespace

bool psram_image_ready() { return ready.load(std::memory_order_acquire); }

bool prepare_psram_image(const Manifest &manifest) {
  if (psram_image_ready()) {
    const bool same = manifest.generation == pinned_generation && manifest.image_url == pinned_url &&
                      manifest.bytes == volume_bytes;
    if (!same)
      ESP_LOGW(TAG, "One-off RAM test is pinned to its first image; reboot to load a different image");
    return same;
  }
  if (manifest.bytes > 64 * 1024 * 1024) {
    ESP_LOGE(TAG, "One-off RAM test only supports logical volumes up to 64 MiB");
    return false;
  }
  std::array<uint8_t, 512> boot{}, info{};
  if (!fetch_image_range(manifest.image_url, 0, boot.data(), boot.size()))
    return false;
  const uint32_t info_sector = boot[48] | (uint32_t(boot[49]) << 8);
  const uint32_t reserved = boot[14] | (uint32_t(boot[15]) << 8);
  if (info_sector == 0 || info_sector >= reserved ||
      !fetch_image_range(manifest.image_url, uint64_t(info_sector) * 512, info.data(), info.size()))
    return false;
  const uint64_t prefix = psram_prefix_size(boot.data(), info.data(), manifest.bytes);
  if (prefix == 0 || prefix > UINT32_MAX || manifest.generation.size() != 71 ||
      manifest.generation.compare(0, 7, "sha256-") != 0) {
    ESP_LOGE(TAG, "Image is not a supported contiguous FAT snapshot");
    return false;
  }
  std::unique_ptr<uint8_t, decltype(&heap_caps_free)> pending{
      static_cast<uint8_t *>(heap_caps_malloc(prefix, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), heap_caps_free};
  if (!pending) {
    ESP_LOGE(TAG, "Cannot fit %" PRIu64 " image bytes in PSRAM; refusing streaming fallback", prefix);
    return false;
  }
  ESP_LOGI(TAG, "Preloading %" PRIu64 " bytes into PSRAM before making the medium ready", prefix);
  for (uint32_t at = 0; at < prefix;) {
    const uint32_t size = std::min<uint64_t>(65536, prefix - at);
    if (!fetch_image_range(manifest.image_url, at, pending.get() + at, size)) {
      ESP_LOGE(TAG, "Preload failed at byte %" PRIu32, at);
      return false;
    }
    at += size;
  }

  // Check the exact logical image, including the zero tail, against the server's generation.
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  int error = mbedtls_sha256_starts(&hash, 0);
  error |= mbedtls_sha256_update(&hash, pending.get(), prefix);
  static const uint8_t zeros[4096]{};
  uint32_t chunks = 0;
  for (uint64_t at = prefix; at < manifest.bytes && error == 0;) {
    const size_t size = std::min<uint64_t>(sizeof(zeros), manifest.bytes - at);
    error |= mbedtls_sha256_update(&hash, zeros, size);
    at += size;
    if ((++chunks & 15) == 0)
      vTaskDelay(1);
  }
  uint8_t digest[32]{};
  error |= mbedtls_sha256_finish(&hash, digest);
  mbedtls_sha256_free(&hash);
  std::string actual = "sha256-";
  for (uint8_t byte : digest) {
    actual += "0123456789abcdef"[byte >> 4];
    actual += "0123456789abcdef"[byte & 15];
  }
  if (error != 0 || actual != manifest.generation) {
    ESP_LOGE(TAG, "RAM snapshot SHA-256 mismatch; keeping the medium unavailable");
    return false;
  }
  image = std::move(pending);
  prefix_bytes = prefix;
  volume_bytes = manifest.bytes;
  pinned_generation = manifest.generation;
  pinned_url = manifest.image_url;
  ready.store(true, std::memory_order_release);
  ESP_LOGI(TAG, "PSRAM SNAPSHOT READY: %" PRIu32 " resident bytes; full image SHA-256 verified; USB reads are offline",
           prefix_bytes);
  return true;
}

bool read_psram_image(uint64_t start, void *buffer, uint32_t size) {
  if (!psram_image_ready() || !copy_psram_prefix(image.get(), prefix_bytes, volume_bytes, start, buffer, size))
    return false;
#ifndef USE_PROXY_STICK_MINIMAL_USB
  stats.cache_hit_bytes.fetch_add(size, std::memory_order_relaxed);
#endif
  return true;
}
}  // namespace esphome::proxy_stick
#endif  // USE_PROXY_STICK_PSRAM_TEST
