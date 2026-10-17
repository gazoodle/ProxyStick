#include "usb.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "tusb.h"

#include "cache.h"
#include "common.h"
#include "trace.h"

namespace esphome::proxy_stick {
namespace {

static const char *const TAG = "proxy_stick.usb";

char inquiry_vendor[8];
char inquiry_product[16];
char inquiry_revision[4];

struct ReadCommand {
  uint32_t started_us{0};
  uint32_t epoch{0};
  uint64_t bytes{0};
  uint64_t source_us{0};
  bool active{false};
  bool failed{false};
} read_command;
uint32_t read_session_epoch{UINT32_MAX};
uint64_t read_session_started_ms{0};
uint64_t read_session_bytes{0};
uint64_t read_session_source_us{0};
uint64_t read_session_usb_us{0};

constexpr uint32_t HOT_PAGE_BYTES = 4096;
constexpr size_t HOT_PAGE_COUNT = 64;
struct HotPage {
  uint32_t page{0};
  uint32_t touches{0};
  uint32_t error{0};
};
static_assert(sizeof(HotPage) * HOT_PAGE_COUNT == 768);
std::array<HotPage, HOT_PAGE_COUNT> hot_pages;
uint32_t hot_pages_epoch{UINT32_MAX};
portMUX_TYPE hot_pages_lock = portMUX_INITIALIZER_UNLOCKED;

void reset_hot_pages_locked(uint32_t epoch) {
  if (hot_pages_epoch == epoch)
    return;
  hot_pages.fill({});
  hot_pages_epoch = epoch;
}

void record_hot_pages(uint64_t start, uint32_t size) {
  if (size == 0)
    return;
  const uint32_t epoch = stats.epoch.load(std::memory_order_relaxed);
  const uint32_t first = start / HOT_PAGE_BYTES;
  const uint32_t last = (start + size - 1) / HOT_PAGE_BYTES;
  portENTER_CRITICAL(&hot_pages_lock);
  reset_hot_pages_locked(epoch);
  for (uint32_t page = first;; page++) {
    HotPage *candidate = &hot_pages[0];
    for (auto &entry : hot_pages) {
      if (entry.touches != 0 && entry.page == page) {
        candidate = &entry;
        break;
      }
      if (entry.touches == 0) {
        candidate = &entry;
        break;
      }
      if (entry.touches < candidate->touches)
        candidate = &entry;
    }
    if (candidate->touches != 0 && candidate->page != page) {
      candidate->error = candidate->touches;
      candidate->touches++;
      candidate->page = page;
    } else {
      candidate->page = page;
      candidate->touches++;
    }
    if (page == last)
      break;
  }
  portEXIT_CRITICAL(&hot_pages_lock);
}

constexpr uint64_t bytes_per_second(uint64_t bytes, uint64_t elapsed_us) {
  return elapsed_us == 0 ? 0 : bytes * 1000000ULL / elapsed_us;
}
static_assert(bytes_per_second(512, 1000) == 512000);

constexpr uint64_t bytes_per_second_ms(uint64_t bytes, uint64_t elapsed_ms) {
  return elapsed_ms == 0 ? 0 : bytes * 1000ULL / elapsed_ms;
}
static_assert(bytes_per_second_ms(1000, 2) == 500000);

void finish_read_command() {
  if (!read_command.active || read_command.failed)
    return;
  const uint32_t epoch = stats.epoch.load(std::memory_order_relaxed);
  if (epoch != read_command.epoch)
    return;

  const uint32_t elapsed_us = micros() - read_command.started_us;
  const uint64_t throughput = bytes_per_second(read_command.bytes, elapsed_us);
  stats.read_throughput.store(throughput, std::memory_order_relaxed);
  uint64_t peak = stats.max_read_throughput.load(std::memory_order_relaxed);
  while (throughput > peak &&
         !stats.max_read_throughput.compare_exchange_weak(peak, throughput, std::memory_order_relaxed)) {
  }
  read_session_bytes += read_command.bytes;
  const uint64_t source_us = read_command.source_us < elapsed_us ? read_command.source_us : elapsed_us;
  read_session_source_us += source_us;
  read_session_usb_us += elapsed_us - source_us;
  stats.session_read_throughput.store(
      bytes_per_second_ms(read_session_bytes, millis_64() - read_session_started_ms), std::memory_order_relaxed);
  stats.source_read_throughput.store(bytes_per_second(read_session_bytes, read_session_source_us),
                                     std::memory_order_relaxed);
  stats.usb_transport_throughput.store(bytes_per_second(read_session_bytes, read_session_usb_us),
                                       std::memory_order_relaxed);
  if (stats.epoch.load(std::memory_order_relaxed) != epoch) {
    stats.read_throughput.store(0, std::memory_order_relaxed);
    stats.max_read_throughput.store(0, std::memory_order_relaxed);
    stats.session_read_throughput.store(0, std::memory_order_relaxed);
    stats.source_read_throughput.store(0, std::memory_order_relaxed);
    stats.usb_transport_throughput.store(0, std::memory_order_relaxed);
  }
}

int32_t read_blocks(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  const uint64_t start = static_cast<uint64_t>(lba) * SECTOR_SIZE + offset;
  const uint64_t disk_size = static_cast<uint64_t>(sector_count.load()) * SECTOR_SIZE;
  if (!media_present.load() || start > disk_size || size > disk_size - start)
    return -1;
  if (!read_image_range(start, buffer, size)) {
    trace::record(trace::READ_FAILED, 0, &lba, sizeof(lba), true);
    read_failed.store(true);
    ESP_LOGW(TAG, "Lost the image service while reading offset %" PRIu64, start);
    return -1;
  }
  record_hot_pages(start, size);
  trace::record(trace::READ, 0, &lba, sizeof(lba));
  stats.session_served_bytes.fetch_add(size, std::memory_order_relaxed);
  stats.total_served_bytes.fetch_add(size, std::memory_order_relaxed);
  ESP_LOGV(TAG, "Read %" PRIu32 " bytes at image offset %" PRIu64, size, start);
  return size;
}

void count_command() { stats.usb_scsi_commands.fetch_add(1, std::memory_order_relaxed); }

}  // namespace

void set_inquiry(const char *vendor, const char *product, const char *revision) {
  std::memcpy(inquiry_vendor, vendor, sizeof(inquiry_vendor));
  std::memcpy(inquiry_product, product, sizeof(inquiry_product));
  std::memcpy(inquiry_revision, revision, sizeof(inquiry_revision));
}

void usb_event(tinyusb_event_t *event, void *) {
  static const char *const NAMES[] = {"attached (configured)", "detached", "suspended", "resumed"};
  const unsigned id = event->id;
  ESP_LOGI(TAG, "USB bus %s", id < 4 ? NAMES[id] : "event");
  trace::record(trace::BUS, id, nullptr, 0, true);
  if (event->id == TINYUSB_EVENT_DETACHED)
    read_command = {};
}

void dump_hot_blocks() {
  std::array<HotPage, HOT_PAGE_COUNT> pages;
  portENTER_CRITICAL(&hot_pages_lock);
  reset_hot_pages_locked(stats.epoch.load(std::memory_order_relaxed));
  pages = hot_pages;
  portEXIT_CRITICAL(&hot_pages_lock);

  std::sort(pages.begin(), pages.end(), [](const HotPage &left, const HotPage &right) {
    return left.touches - left.error > right.touches - right.error;
  });
  ESP_LOGI(TAG, "Most requested 4 KiB image pages (LBA range, minimum..estimated touches):");
  for (const auto &page : pages) {
    const uint32_t minimum = page.touches - page.error;
    if (minimum == 0)
      break;
    const uint64_t first_lba = static_cast<uint64_t>(page.page) * HOT_PAGE_BYTES / SECTOR_SIZE;
    ESP_LOGI(TAG, "  LBA %" PRIu64 "..%" PRIu64 ": %" PRIu32 "..%" PRIu32, first_lba,
             first_lba + HOT_PAGE_BYTES / SECTOR_SIZE - 1, minimum, page.touches);
  }
}

}  // namespace esphome::proxy_stick

using namespace esphome::proxy_stick;

extern "C" {

void tud_msc_inquiry_cb(uint8_t, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
  count_command();
  trace::record(trace::INQUIRY);
  std::memcpy(vendor_id, inquiry_vendor, 8);
  std::memcpy(product_id, inquiry_product, 16);
  std::memcpy(product_rev, inquiry_revision, 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  count_command();
  const bool ready = media_present.load();
  trace::record(trace::TEST_UNIT_READY, ready);
  if (ready)
    return true;
  tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);  // Medium not present.
  return false;
}

void tud_msc_capacity_cb(uint8_t, uint32_t *block_count, uint16_t *block_size) {
  count_command();
  *block_count = sector_count.load();
  *block_size = SECTOR_SIZE;
  trace::record(trace::CAPACITY, 0, block_count, sizeof(*block_count));
}

bool tud_msc_start_stop_cb(uint8_t, uint8_t power_condition, bool start, bool load_eject) {
  count_command();
  trace::record(trace::START_STOP, start | (load_eject << 1), &power_condition, 1, true);
  return true;
}

bool tud_msc_prevent_allow_medium_removal_cb(uint8_t, uint8_t prohibit_removal, uint8_t) {
  count_command();
  trace::record(trace::PREVENT_REMOVAL, prohibit_removal);
  return true;
}

bool tud_msc_is_writable_cb(uint8_t) {
  trace::record(trace::WRITE_PROTECT);
  return false;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  if (!read_command.active) {
    const uint32_t epoch = stats.epoch.load(std::memory_order_relaxed);
    if (read_session_epoch != epoch) {
      read_session_epoch = epoch;
      read_session_started_ms = esphome::millis_64();
      read_session_bytes = 0;
      read_session_source_us = 0;
      read_session_usb_us = 0;
    }
    read_command = {esphome::micros(), epoch, 0, 0, true, false};
    count_command();
  }
  const uint32_t source_started_us = esphome::micros();
  const int32_t read = read_blocks(lba, offset, buffer, size);
  read_command.source_us += esphome::micros() - source_started_us;
  if (read < 0) {
    read_command.failed = true;
    tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);  // Unrecovered read error.
  } else {
    read_command.bytes += read;
  }
  return read;
}

void tud_msc_read10_complete_cb(uint8_t) {
  finish_read_command();
  read_command = {};
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t, uint32_t, uint8_t *, uint32_t) {
  trace::record(trace::WRITE, 0, nullptr, 0, true);
  tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);  // Write protected.
  return -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *, uint16_t) {
  count_command();
  ESP_LOGI(TAG, "Rejected SCSI command 0x%02X", scsi_cmd[0]);
  trace::record(trace::REJECTED, scsi_cmd[0], scsi_cmd + 1, 8, true);
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);  // Invalid command.
  return -1;
}

}  // extern "C"
