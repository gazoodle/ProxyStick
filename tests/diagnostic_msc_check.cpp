#include <array>
#include <cassert>
#include <cstring>
#include <vector>
#include "tusb.h"
#include "../components/proxy_stick/diagnostics.h"
#include "../components/proxy_stick/diagnostic_msc.h"

using namespace esphome::proxy_stick::diagnostics;
struct EventRecord {
  Event event;
  uint64_t offset;
  uint32_t size, value, tag, callbacks, flags;
};
std::vector<EventRecord> records;
bool accepted = true;
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
int64_t timer = 100;
int64_t esp_timer_get_time() { return timer += 100; }
std::vector<uint8_t> response_bytes;
#endif
unsigned real_xfers = 0, real_completions = 0, real_resets = 0, real_controls = 0, aborts = 0;

namespace esphome::proxy_stick::diagnostics {
void record(Event event, uint64_t offset, uint32_t size, uint32_t value, uint32_t tag,
            uint32_t callbacks, uint32_t flags) {
  records.push_back({event, offset, size, value, tag, callbacks, flags});
}
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
void record_at(uint64_t us, Event event, uint64_t offset, uint32_t size, uint32_t value,
               uint32_t tag, uint32_t callbacks, uint32_t flags) {
  assert(us == static_cast<uint64_t>(timer));
  record(event, offset, size, value, tag, callbacks, flags);
}
void response(const uint8_t *bytes, uint32_t size, uint32_t, uint32_t) {
  if (bytes != nullptr) response_bytes.assign(bytes, bytes + size);
}
#endif
}
namespace esphome::proxy_stick {
void abort_usb_read() { aborts++; }
}

extern "C" {
bool __real_usbd_edpt_xfer(uint8_t port, uint8_t ep, uint8_t *buffer, uint16_t size, bool is_isr) {
  assert(port == 0 && (ep == 1 || ep == 0x81));
  assert(buffer != nullptr && size != 0 && !is_isr);
  real_xfers++;
  return accepted;
}
bool __real_mscd_xfer_cb(uint8_t port, uint8_t ep, xfer_result_t, uint32_t) {
  assert(port == 0 && (ep == 1 || ep == 0x81));
  real_completions++;
  return false;  // Wrapper must preserve even a failed handler return.
}
void __real_mscd_reset(uint8_t port) { assert(port == 0); real_resets++; }
bool __real_mscd_control_xfer_cb(uint8_t port, uint8_t, const tusb_control_request_t *) {
  assert(port == 0);
  real_controls++;
  return false;
}
bool __wrap_usbd_edpt_xfer(uint8_t, uint8_t, uint8_t *, uint16_t, bool);
bool __wrap_mscd_xfer_cb(uint8_t, uint8_t, xfer_result_t, uint32_t);
void __wrap_mscd_reset(uint8_t);
bool __wrap_mscd_control_xfer_cb(uint8_t, uint8_t, const tusb_control_request_t *);
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
bool __real_tud_control_xfer(uint8_t, const tusb_control_request_t *, void *, uint16_t) { return accepted; }
bool __real_tud_control_status(uint8_t, const tusb_control_request_t *) { return accepted; }
void __real_usbd_edpt_stall(uint8_t, uint8_t) {}
void __real_usbd_edpt_clear_stall(uint8_t, uint8_t) {}
bool __wrap_tud_control_xfer(uint8_t, const tusb_control_request_t *, void *, uint16_t);
bool __wrap_tud_control_status(uint8_t, const tusb_control_request_t *);
void __wrap_usbd_edpt_stall(uint8_t, uint8_t);
void __wrap_usbd_edpt_clear_stall(uint8_t, uint8_t);
#endif
}

void le32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = value >> (i * 8);
}

std::array<uint8_t, 31> cbw() {
  std::array<uint8_t, 31> p{};
  le32(p.data(), 0x43425355);
  le32(p.data() + 4, 123);
  le32(p.data() + 8, 8192);
  p[12] = 0x80;
  p[14] = 10;
  p[15] = 0x28;
  p[20] = 7;
  p[23] = 16;
  return p;
}

void start() {
  auto p = cbw();
  const auto original = p;
  assert(__wrap_usbd_edpt_xfer(0, 1, p.data(), p.size(), false));
  assert(!__wrap_mscd_xfer_cb(0, 1, XFER_RESULT_SUCCESS, p.size()));
  assert(p == original);
  assert(records.back().event == MSC_CBW);
  assert(records.back().offset == 7 * 512);
  assert(records.back().size == 8192 && records.back().value == 8192);
  assert(records.back().tag == 123 && records.back().callbacks == 0x28);
}

void status(uint32_t tag, uint32_t residue, uint8_t code, xfer_result_t result = XFER_RESULT_SUCCESS,
            uint32_t actual = 13) {
  std::array<uint8_t, 13> p{};
  le32(p.data(), 0x53425355);
  le32(p.data() + 4, tag);
  le32(p.data() + 8, residue);
  p[12] = code;
  const auto original = p;
  assert(__wrap_usbd_edpt_xfer(0, 0x81, p.data(), p.size(), false));
  assert(!__wrap_mscd_xfer_cb(0, 0x81, result, actual));
  assert(p == original);
}

int main() {
  MscCommand parser;
  auto p = cbw();
  assert(!parser.start(p.data(), 30));
  assert(!parser.start(nullptr, 31));
  p[0] = 0;
  assert(!parser.start(p.data(), 31));

  start();
  std::array<uint8_t, 8192> data{};
  assert(__wrap_usbd_edpt_xfer(0, 0x81, data.data(), 8192, false));
  assert(!__wrap_mscd_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 4096));
  assert(records.back().event == MSC_XFER_FAULT && records.back().size == 8192);
  assert(records.back().value == 4096 && records.back().flags == 0);
  assert(__wrap_usbd_edpt_xfer(0, 0x81, data.data(), 4096, false));
  assert(!__wrap_mscd_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 4096));
  status(123, 0, 0);
  assert(records.back().event == MSC_CSW && records.back().offset == 8192);
  assert(records.back().flags == MscCommand::SHORT && records.back().callbacks == 2);

  start();
  const size_t before_data = records.size();
  assert(__wrap_usbd_edpt_xfer(0, 0x81, data.data(), 8192, false));
  assert(!__wrap_mscd_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 8192));
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
  assert(records.size() == before_data + 1 && records.back().event == MSC_DATA);
  assert(records.back().offset == 7 * 512 && records.back().callbacks == 8192);
  assert(records.back().value == 100);  // Submission/completion timestamps, not command duration.
#else
  assert(records.size() == before_data);  // Successful data transfers do not flood the WiFi ring.
#endif
  status(123, 0, 0);
  assert(records.back().event == MSC_CSW && records.back().offset == 8192);
  assert(records.back().flags == 0 && records.back().callbacks == 1);

  start();
  status(123, 8192, 1);
  assert(records.back().value == 1 && records.back().flags == 0);
  start();
  status(456, 0, 0);
  assert(records.back().flags == (MscCommand::LENGTH_MISMATCH | MscCommand::TAG_MISMATCH));
  start();
  assert(__wrap_usbd_edpt_xfer(0, 0x81, data.data(), 8192, false));
  assert(!__wrap_mscd_xfer_cb(0, 0x81, XFER_RESULT_FAILED, 0));
  assert(records.back().callbacks == XFER_RESULT_FAILED);
  accepted = false;
  assert(!__wrap_usbd_edpt_xfer(0, 0x81, data.data(), 8192, false));
  assert(records.back().flags == 3);
  accepted = true;
  status(123, 8192, 1, XFER_RESULT_FAILED, 0);
  assert(records.back().event == MSC_XFER_FAULT && records.back().flags == 1);

  tusb_control_request_t request{0x02, TUSB_REQ_CLEAR_FEATURE, 0, 0x81, 0};
  assert(!__wrap_mscd_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
  assert(records.back().event == MSC_CLEAR_HALT && records.back().offset == 0x81);
  request = {0x21, 0xff, 0, 0, 1};
  assert(!__wrap_mscd_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
  assert(aborts == 0);  // Invalid reset must not reset the observer.
  request.wLength = 0;
  assert(!__wrap_mscd_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
  assert(records.back().event == MSC_RESET && records.back().value == 1 && aborts == 1);
  __wrap_mscd_reset(0);
  assert(records.back().value == 0 && aborts == 2 && real_resets == 1 && real_controls == 3);
  assert(real_xfers == 15 && real_completions == 14);
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
  auto inquiry = cbw();
  for (unsigned i = 0; i < 16; i++) inquiry[15 + i] = i + 0x12;
  const auto original = inquiry;
  assert(__wrap_usbd_edpt_xfer(0, 1, inquiry.data(), inquiry.size(), false));
  assert(!__wrap_mscd_xfer_cb(0, 1, XFER_RESULT_SUCCESS, inquiry.size()));
  assert(inquiry == original);
  const auto &cdb = records.back();
  assert(cdb.event == MSC_CDB && cdb.tag == 123 && cdb.callbacks == 8192);
  assert(cdb.offset == 0x1918171615141312ULL);
  assert(cdb.size == 0x1d1c1b1a && cdb.value == 0x21201f1e);
  std::array<uint8_t, 36> reply{};
  reply[0] = 0x70;
  assert(__wrap_usbd_edpt_xfer(0, 0x81, reply.data(), reply.size(), false));
  assert(response_bytes.size() == reply.size() && response_bytes.front() == 0x70);
  assert(!__wrap_mscd_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, reply.size()));
  status(123, 8192 - reply.size(), 0);
  assert(records.back().event == MSC_CSW);  // Successful non-READ status must be captured.
  const tusb_control_request_t setup{0x80, 6, 0x100, 0, 18};
  assert(__wrap_tud_control_xfer(0, &setup, reply.data(), 18));
  assert(records.back().event == USB_CONTROL && records.back().offset == 0x0012000001000680ULL);
  assert(response_bytes.size() == 18 && reply[0] == 0x70);
  accepted = false;
  assert(!__wrap_tud_control_status(0, &setup));
  assert(!__wrap_tud_control_xfer(0, &setup, reply.data(), 18));
  assert(records.back().event == USB_CONTROL && records.back().value == 2);
  __wrap_usbd_edpt_stall(0, 0x81);
  assert(records.back().event == USB_STALL && records.back().offset == 0x81);
  __wrap_usbd_edpt_clear_stall(0, 0x81);
  assert(records.back().event == USB_CLEAR);
#endif
}
