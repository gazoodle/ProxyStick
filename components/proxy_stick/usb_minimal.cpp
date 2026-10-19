#include "esphome/core/defines.h"

#ifdef USE_PROXY_STICK_MINIMAL_USB
#include <cstring>
#include "tusb.h"
#include "esphome/core/log.h"
#include "common.h"
#include "diagnostics.h"
#include "psram_test.h"
#include "usb.h"

namespace esphome::proxy_stick {
namespace {
char vendor[8], product[16], revision[4];
}

void set_inquiry(const char *v, const char *p, const char *r) {
  std::memcpy(vendor, v, sizeof(vendor));
  std::memcpy(product, p, sizeof(product));
  std::memcpy(revision, r, sizeof(revision));
}

// One-off immutable RAM disk: no USB-side statistics, logging, locks, notifications or delay.
void usb_event(tinyusb_event_t *event, void *) {
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
  diagnostics::record(diagnostics::BUS, 0, 0, event->id);
#else
  (void) event;
#endif
}
void notify_media_changed() {}
bool usb_read_in_progress() { return false; }
void abort_usb_read() {}
void dump_hot_blocks() {
  ESP_LOGW("proxy_stick", "Hot-block recording is disabled in the minimal USB test");
}

}  // namespace esphome::proxy_stick

extern "C" {
using namespace esphome::proxy_stick;

void tud_msc_inquiry_cb(uint8_t, uint8_t v[8], uint8_t p[16], uint8_t r[4]) {
  std::memcpy(v, vendor, sizeof(vendor));
  std::memcpy(p, product, sizeof(product));
  std::memcpy(r, revision, sizeof(revision));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  if (media_present.load() && psram_image_ready())
    return true;
  tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
  return false;
}

void tud_msc_capacity_cb(uint8_t, uint32_t *blocks, uint16_t *size) {
  *blocks = media_present.load() && psram_image_ready() ? sector_count.load() : 0;
  *size = SECTOR_SIZE;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  if (!media_present.load() || !psram_image_ready()) {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return -1;
  }
  const uint64_t start = uint64_t(lba) * SECTOR_SIZE + offset;
  if (!read_psram_image(start, buffer, size)) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);  // LBA out of range.
    return -1;
  }
  return size;
}

void tud_msc_read10_complete_cb(uint8_t) {}
void tud_msc_write10_complete_cb(uint8_t) {}
bool tud_msc_is_writable_cb(uint8_t) { return false; }
bool tud_msc_start_stop_cb(uint8_t, uint8_t, bool, bool) { return true; }
bool tud_msc_prevent_allow_medium_removal_cb(uint8_t, uint8_t, uint8_t) { return true; }

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t, uint32_t, uint8_t *, uint32_t) {
  tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0);
  return -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t[16], void *, uint16_t) {
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
  return -1;
}
}
#endif  // USE_PROXY_STICK_MINIMAL_USB
