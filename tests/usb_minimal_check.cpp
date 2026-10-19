#include <array>
#include <cassert>
#include <cstring>
#include "tusb.h"
#include "../components/proxy_stick/common.h"
#include "../components/proxy_stick/usb.h"
#include "../components/proxy_stick/diagnostics.h"

#ifdef USE_PROXY_STICK_DEBUG_SERIAL
unsigned bus_events = 0;
namespace esphome::proxy_stick::diagnostics {
void record(Event event, uint64_t, uint32_t, uint32_t value, uint32_t, uint32_t, uint32_t) {
  assert(event == BUS && value == 7);
  bus_events++;
}
}
#endif

namespace esphome::proxy_stick {
Statistics stats;
std::atomic_bool media_present{false}, read_failed{false};
std::atomic<uint32_t> sector_count{0};
bool snapshot_ready = false;
std::array<uint8_t, 1024> image;
unsigned reads = 0;
bool psram_image_ready() { return snapshot_ready; }
bool read_psram_image(uint64_t start, void *buffer, uint32_t size) {
  reads++;
  if (start > image.size() || size > image.size() - start)
    return false;
  std::memcpy(buffer, image.data() + start, size);
  return true;
}
}
using namespace esphome::proxy_stick;
uint8_t sense_key = 0, asc = 0;
extern "C" {
bool tud_msc_set_sense(uint8_t, uint8_t key, uint8_t code, uint8_t) {
  sense_key = key;
  asc = code;
  return true;
}
void tud_msc_inquiry_cb(uint8_t, uint8_t[8], uint8_t[16], uint8_t[4]);
bool tud_msc_test_unit_ready_cb(uint8_t);
void tud_msc_capacity_cb(uint8_t, uint32_t *, uint16_t *);
int32_t tud_msc_read10_cb(uint8_t, uint32_t, uint32_t, void *, uint32_t);
int32_t tud_msc_write10_cb(uint8_t, uint32_t, uint32_t, uint8_t *, uint32_t);
void tud_msc_read10_complete_cb(uint8_t);
void tud_msc_write10_complete_cb(uint8_t);
bool tud_msc_is_writable_cb(uint8_t);
bool tud_msc_start_stop_cb(uint8_t, uint8_t, bool, bool);
bool tud_msc_prevent_allow_medium_removal_cb(uint8_t, uint8_t, uint8_t);
int32_t tud_msc_scsi_cb(uint8_t, const uint8_t[16], void *, uint16_t);
}

int main() {
  set_inquiry("Vendor  ", "Product         ", "0.5 ");
  uint8_t v[8], p[16], r[4];
  tud_msc_inquiry_cb(0, v, p, r);
  assert(std::memcmp(v, "Vendor  ", 8) == 0 && std::memcmp(p, "Product         ", 16) == 0);
  assert(std::memcmp(r, "0.5 ", 4) == 0);
  for (size_t i = 0; i < image.size(); i++)
    image[i] = static_cast<uint8_t>(i * 17);
  std::array<uint8_t, 513> buffer{};
  uint32_t blocks = 9;
  uint16_t size = 9;
  assert(!tud_msc_test_unit_ready_cb(0) && sense_key == SCSI_SENSE_NOT_READY && asc == 0x3a);
  assert(tud_msc_read10_cb(0, 0, 0, buffer.data(), buffer.size()) == -1 && reads == 0);
  tud_msc_capacity_cb(0, &blocks, &size);
  assert(blocks == 0 && size == 512);
  media_present.store(true);
  assert(!tud_msc_test_unit_ready_cb(0));  // Do not expose an unverified snapshot.
  snapshot_ready = true;
  sector_count.store(2);
  notify_media_changed();  // Minimal mode deliberately has no UNIT ATTENTION state machine.
  assert(tud_msc_test_unit_ready_cb(0));
  tud_msc_capacity_cb(0, &blocks, &size);
  assert(blocks == 2 && size == 512);
  assert(tud_msc_read10_cb(0, 0, 511, buffer.data(), buffer.size()) == 513);
  assert(std::memcmp(buffer.data(), image.data() + 511, 513) == 0);
  assert(tud_msc_read10_cb(0, 1, 511, buffer.data(), 1) == 1 && buffer[0] == image[1023]);
  assert(tud_msc_read10_cb(0, 1, 511, buffer.data(), 2) == -1 && asc == 0x21);
  assert(tud_msc_read10_cb(0, UINT32_MAX, UINT32_MAX, buffer.data(), 1) == -1);
  const auto original = image;
  assert(!tud_msc_is_writable_cb(0));
  assert(tud_msc_write10_cb(0, 0, 0, buffer.data(), 1) == -1 && sense_key == SCSI_SENSE_DATA_PROTECT);
  assert(image == original);
  assert(tud_msc_start_stop_cb(0, 0, false, false));
  assert(tud_msc_prevent_allow_medium_removal_cb(0, 1, 0));
  const uint8_t command[16]{};
  assert(tud_msc_scsi_cb(0, command, buffer.data(), buffer.size()) == -1 && asc == 0x20);
  tud_msc_read10_complete_cb(0);
  tud_msc_write10_complete_cb(0);
  tinyusb_event_t event{7};
  usb_event(&event, nullptr);
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
  assert(bus_events == 1);
#endif
  abort_usb_read();
  assert(!usb_read_in_progress());
  assert(stats.usb_scsi_commands == 0 && stats.total_served_bytes == 0 && stats.cache_hit_bytes == 0);
}
