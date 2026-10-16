#include "cache.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "common.h"
#include "http.h"

namespace esphome::proxy_stick {
namespace {

static const char *const TAG = "proxy_stick.cache";
constexpr uint32_t FETCH_WORKER_STACK_SIZE = 6144;
constexpr UBaseType_t FETCH_WORKER_PRIORITY = 5;

std::unique_ptr<uint8_t[]> image_cache;
std::unique_ptr<uint8_t[]> prefetch_cache;
std::string image_cache_url;
uint64_t image_cache_start{0};
uint32_t image_cache_size{0};
uint32_t image_cache_epoch{0};
uint32_t block_size{DEFAULT_READ_BLOCK_SIZE};
SemaphoreHandle_t cache_mutex{nullptr};
SemaphoreHandle_t fetch_signal{nullptr};
SemaphoreHandle_t fetch_complete{nullptr};
TaskHandle_t fetch_worker_task{nullptr};
std::string cache_image_url;
uint64_t cache_disk_size{0};
uint32_t cache_epoch{1};
bool fetch_worker_busy{false};
bool fetch_job_pending{false};
std::atomic_bool prefetch_on{false};

enum class PrefetchState : uint8_t { EMPTY, LOADING, READY, FAILED };

struct FetchJob {
  uint32_t epoch{0};
  uint64_t start{0};
  uint32_t size{0};
  bool speculative{false};
};

FetchJob fetch_job;
PrefetchState prefetch_state{PrefetchState::EMPTY};
uint32_t prefetch_epoch{0};
uint64_t prefetch_start{0};
uint32_t prefetch_size{0};
bool prefetch_speculative{false};

bool range_contains(uint64_t range_start, uint32_t range_size, uint64_t start, uint32_t size) {
  if (start < range_start)
    return false;
  const uint64_t offset = start - range_start;
  return offset <= range_size && size <= range_size - offset;
}

bool read_image_range_sync(uint64_t start, void *buffer, uint32_t size) {
  const std::string &url = cache_image_url;
  const uint64_t end = start + size;
  const uint64_t cached_end = image_cache_start + image_cache_size;
  if (image_cache_url == url && start >= image_cache_start && end <= cached_end) {
    std::memcpy(buffer, image_cache.get() + start - image_cache_start, size);
    stats.cache_hit_bytes.fetch_add(size, std::memory_order_relaxed);
    return true;
  }

  const uint64_t disk_size = static_cast<uint64_t>(sector_count.load()) * SECTOR_SIZE;
  const uint64_t aligned_start = start & ~(static_cast<uint64_t>(block_size) - 1);
  const uint32_t fetch_size = std::min<uint64_t>(block_size, disk_size - aligned_start);
  if (end > aligned_start + fetch_size)
    return fetch_image_range(url, start, buffer, size);
  if (!fetch_image_range(url, aligned_start, image_cache.get(), fetch_size)) {
    image_cache_size = 0;
    return false;
  }
  image_cache_url = url;
  image_cache_start = aligned_start;
  image_cache_size = fetch_size;
  std::memcpy(buffer, image_cache.get() + start - aligned_start, size);
  return true;
}

// Forgets every cached and prefetched block, under the lock; `url` and `disk_size` say what comes next.
void reset_locked(const std::string &url, uint64_t disk_size) {
  cache_epoch++;
  image_cache_size = 0;
  image_cache_epoch = 0;
  image_cache_url.clear();
  prefetch_state = PrefetchState::EMPTY;
  fetch_job_pending = false;
  cache_image_url = url;
  cache_disk_size = disk_size;
}

void reset(const std::string &url, uint64_t disk_size) {
  if (cache_mutex == nullptr) {
    image_cache_size = 0;
    image_cache_url.clear();
    cache_image_url = url;
    return;
  }
  xSemaphoreTake(cache_mutex, portMAX_DELAY);
  reset_locked(url, disk_size);
  xSemaphoreGive(cache_mutex);
  // Wakes a reader waiting on the old image, so it sees the new epoch and gives up.
  if (fetch_complete != nullptr)
    xSemaphoreGive(fetch_complete);
}

bool schedule_fetch_locked(uint64_t start, bool speculative) {
  if (!prefetch_on.load() || fetch_worker_busy || fetch_job_pending || prefetch_state == PrefetchState::LOADING ||
      start >= cache_disk_size)
    return false;
  const uint32_t size = std::min<uint64_t>(block_size, cache_disk_size - start);
  prefetch_epoch = cache_epoch;
  prefetch_start = start;
  prefetch_size = size;
  prefetch_speculative = speculative;
  prefetch_state = PrefetchState::LOADING;
  fetch_job = FetchJob{cache_epoch, start, size, speculative};
  fetch_job_pending = true;
  xSemaphoreGive(fetch_signal);
  return true;
}

void fetch_worker(void *) {
  while (true) {
    xSemaphoreTake(fetch_signal, portMAX_DELAY);
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    if (!fetch_job_pending) {
      xSemaphoreGive(cache_mutex);
      continue;
    }
    const FetchJob job = fetch_job;
    fetch_job_pending = false;
    fetch_worker_busy = true;
    const bool current = job.epoch == cache_epoch && prefetch_state == PrefetchState::LOADING &&
                         prefetch_epoch == job.epoch && prefetch_start == job.start;
    const std::string url = current ? cache_image_url : std::string{};
    xSemaphoreGive(cache_mutex);

    const bool ok = current && fetch_image_range(url, job.start, prefetch_cache.get(), job.size);

    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    fetch_worker_busy = false;
    const bool still_current = job.epoch == cache_epoch && prefetch_state == PrefetchState::LOADING &&
                               prefetch_epoch == job.epoch && prefetch_start == job.start;
    if (still_current) {
      prefetch_state = ok ? PrefetchState::READY : PrefetchState::FAILED;
      if (!ok) {
        stats.prefetch_failures.fetch_add(1, std::memory_order_relaxed);
        read_failed.store(true);
      }
    }
    xSemaphoreGive(cache_mutex);
    xSemaphoreGive(fetch_complete);
  }
}

bool read_prefetched_chunk(uint64_t start, void *buffer, uint32_t size) {
  bool counted_wait = false;
  while (media_present.load()) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    if (image_cache_epoch == cache_epoch && range_contains(image_cache_start, image_cache_size, start, size)) {
      std::memcpy(buffer, image_cache.get() + start - image_cache_start, size);
      stats.cache_hit_bytes.fetch_add(size, std::memory_order_relaxed);
      if (prefetch_state == PrefetchState::EMPTY)
        schedule_fetch_locked(image_cache_start + image_cache_size, true);
      xSemaphoreGive(cache_mutex);
      return true;
    }

    if (prefetch_state == PrefetchState::READY) {
      if (prefetch_epoch == cache_epoch && range_contains(prefetch_start, prefetch_size, start, size)) {
        std::swap(image_cache, prefetch_cache);
        image_cache_start = prefetch_start;
        image_cache_size = prefetch_size;
        image_cache_epoch = prefetch_epoch;
        image_cache_url = cache_image_url;
        const bool was_speculative = prefetch_speculative;
        prefetch_state = PrefetchState::EMPTY;
        std::memcpy(buffer, image_cache.get() + start - image_cache_start, size);
        if (was_speculative) {
          stats.prefetch_hits.fetch_add(1, std::memory_order_relaxed);
          stats.cache_hit_bytes.fetch_add(size, std::memory_order_relaxed);
        }
        schedule_fetch_locked(image_cache_start + image_cache_size, true);
        xSemaphoreGive(cache_mutex);
        return true;
      }
      prefetch_state = PrefetchState::EMPTY;
      stats.prefetch_discards.fetch_add(1, std::memory_order_relaxed);
    }

    if (prefetch_state == PrefetchState::FAILED && prefetch_epoch == cache_epoch &&
        range_contains(prefetch_start, prefetch_size, start, size)) {
      xSemaphoreGive(cache_mutex);
      return false;
    }
    if (prefetch_state == PrefetchState::FAILED)
      prefetch_state = PrefetchState::EMPTY;

    const uint64_t aligned_start = start & ~(static_cast<uint64_t>(block_size) - 1);
    if (prefetch_state == PrefetchState::EMPTY && !fetch_worker_busy)
      schedule_fetch_locked(aligned_start, false);
    if (!counted_wait) {
      stats.prefetch_waits.fetch_add(1, std::memory_order_relaxed);
      counted_wait = true;
    }
    xSemaphoreGive(cache_mutex);

    if (xSemaphoreTake(fetch_complete, pdMS_TO_TICKS(HTTP_TIMEOUT_MS * 2 + 1000)) != pdTRUE)
      return false;
  }
  return false;
}

}  // namespace

bool setup_cache(uint32_t read_block_size) {
  block_size = read_block_size;
  image_cache.reset(new (std::nothrow) uint8_t[block_size]);
  prefetch_cache.reset(new (std::nothrow) uint8_t[block_size]);
  if (image_cache == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate the %" PRIu32 " byte read buffer", block_size);
    return false;
  }
  cache_mutex = xSemaphoreCreateMutex();
  fetch_signal = xSemaphoreCreateBinary();
  fetch_complete = xSemaphoreCreateBinary();
  if (prefetch_cache != nullptr && cache_mutex != nullptr && fetch_signal != nullptr && fetch_complete != nullptr &&
      xTaskCreate(fetch_worker, "proxy_stick_fetch", FETCH_WORKER_STACK_SIZE, nullptr, FETCH_WORKER_PRIORITY,
                  &fetch_worker_task) == pdPASS) {
    prefetch_on.store(true);
    ESP_LOGI(TAG, "Background image prefetch enabled");
  } else {
    prefetch_on.store(false);
    ESP_LOGW(TAG, "Background prefetch unavailable; using synchronous reads");
  }
  return true;
}

void configure_cache(const std::string &url, uint64_t disk_size) { reset(url, disk_size); }

void invalidate_cache() { reset("", 0); }

bool read_image_range(uint64_t start, void *buffer, uint32_t size) {
  if (!prefetch_on.load())
    return read_image_range_sync(start, buffer, size);
  auto *output = static_cast<uint8_t *>(buffer);
  uint32_t remaining = size;
  while (remaining > 0) {
    const uint32_t block_remaining = block_size - (start & (block_size - 1));
    const uint32_t chunk = std::min(remaining, block_remaining);
    if (!read_prefetched_chunk(start, output, chunk))
      return false;
    start += chunk;
    output += chunk;
    remaining -= chunk;
  }
  return true;
}

bool resize_read_buffers(uint32_t size) {
  std::unique_ptr<uint8_t[]> primary(new (std::nothrow) uint8_t[size]);
  if (primary == nullptr)
    return false;
  std::unique_ptr<uint8_t[]> spare(new (std::nothrow) uint8_t[size]);

  invalidate_cache();
  const uint32_t started = millis();
  while (cache_mutex != nullptr) {
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
    const bool busy = fetch_worker_busy;
    xSemaphoreGive(cache_mutex);
    if (!busy)
      break;
    if (millis() - started > HTTP_TIMEOUT_MS * 2)
      return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (cache_mutex != nullptr)
    xSemaphoreTake(cache_mutex, portMAX_DELAY);
  image_cache = std::move(primary);
  prefetch_cache = std::move(spare);
  image_cache_size = 0;
  image_cache_epoch = 0;
  image_cache_url.clear();
  prefetch_state = PrefetchState::EMPTY;
  block_size = size;
  prefetch_on.store(fetch_worker_task != nullptr && prefetch_cache != nullptr);
  if (cache_mutex != nullptr)
    xSemaphoreGive(cache_mutex);
  return true;
}

uint32_t read_block_size() { return block_size; }

bool prefetch_enabled() { return prefetch_on.load(); }

}  // namespace esphome::proxy_stick
