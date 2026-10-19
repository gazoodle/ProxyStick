#include "diagnostics.h"

#ifdef USE_PROXY_STICK_DEBUG_SERIAL
#include <algorithm>
#include "tusb.h"

namespace {
using namespace esphome::proxy_stick::diagnostics;

void control(const tusb_control_request_t *r, uint16_t size, uint32_t stage) {
  const uint64_t packed = uint64_t(r->bmRequestType) | (uint64_t(r->bRequest) << 8) |
      (uint64_t(r->wValue) << 16) | (uint64_t(r->wIndex) << 32) | (uint64_t(r->wLength) << 48);
  record(USB_CONTROL, packed, size, stage);
}
}

extern "C" {
bool __real_tud_control_xfer(uint8_t, const tusb_control_request_t *, void *, uint16_t);
bool __wrap_tud_control_xfer(uint8_t port, const tusb_control_request_t *request, void *buffer, uint16_t size) {
  if (port == 0) {
    control(request, size, 0);
    if (request->bmRequestType & 128)
      response(static_cast<const uint8_t *>(buffer), std::min<uint16_t>(size, request->wLength), 0, 0x10000);
  }
  const bool result = __real_tud_control_xfer(port, request, buffer, size);
  if (port == 0 && !result)
    record(USB_CONTROL, 0, size, 2);  // Submission failed; preserve the real return value.
  return result;
}

bool __real_tud_control_status(uint8_t, const tusb_control_request_t *);
bool __wrap_tud_control_status(uint8_t port, const tusb_control_request_t *request) {
  if (port == 0) control(request, 0, 1);
  return __real_tud_control_status(port, request);
}

void __real_usbd_edpt_stall(uint8_t, uint8_t);
void __wrap_usbd_edpt_stall(uint8_t port, uint8_t endpoint) {
  if (port == 0) record(USB_STALL, endpoint);
  __real_usbd_edpt_stall(port, endpoint);
}

void __real_usbd_edpt_clear_stall(uint8_t, uint8_t);
void __wrap_usbd_edpt_clear_stall(uint8_t port, uint8_t endpoint) {
  if (port == 0) record(USB_CLEAR, endpoint);
  __real_usbd_edpt_clear_stall(port, endpoint);
}
}
#endif
