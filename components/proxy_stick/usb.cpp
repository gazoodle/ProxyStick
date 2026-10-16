#include "usb.h"

#include <cinttypes>
#include <cstring>

#include "esphome/core/log.h"
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
  const int32_t read = read_blocks(lba, offset, buffer, size);
  if (read < 0)
    tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);  // Unrecovered read error.
  return read;
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
