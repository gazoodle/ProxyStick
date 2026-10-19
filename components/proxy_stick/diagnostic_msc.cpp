#include "diagnostics.h"

#ifdef USE_PROXY_STICK_DEBUG
#include <array>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "diagnostic_msc.h"
#include "usb.h"
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
#include "esp_timer.h"
#endif

// Linker observation only: never replace TinyUSB's decision or alter a buffer/result.
// MSC queues and completions run on TinyUSB's task; the S3 uses device controller 0.
namespace {
using namespace esphome::proxy_stick;
using namespace esphome::proxy_stick::diagnostics;
struct Pending {
  const uint8_t *buffer{nullptr};
  uint32_t size{0}, tag{0}, residue{0};
  uint8_t status{0};
  bool csw{false}, valid{false};
  uint64_t started_us{0}, offset{0};
  uint8_t opcode{0};
};
std::array<Pending, 32> pending;
MscCommand current;

size_t slot(uint8_t endpoint) { return (endpoint & 15) + ((endpoint & 128) ? 16 : 0); }

void fault(const Pending &p, uint32_t actual, uint32_t result, uint32_t kind) {
  record(MSC_XFER_FAULT, current.transferred, p.size, actual, current.tag, result, kind);
}

void reset(uint32_t reason) {
  record(MSC_RESET, current.transferred, current.expected, reason, current.tag);
  abort_usb_read();
  current = {};
  pending.fill({});
}
}  // namespace

extern "C" {
bool __real_usbd_edpt_xfer(uint8_t, uint8_t, uint8_t *, uint16_t, bool);
bool __wrap_usbd_edpt_xfer(uint8_t port, uint8_t endpoint, uint8_t *buffer, uint16_t size, bool is_isr) {
  if (port != 0 || is_isr)
    return __real_usbd_edpt_xfer(port, endpoint, buffer, size, is_isr);
  // Save before submission, since TinyUSB reuses the same buffer for data, CBW and CSW.
  Pending p{buffer, size, 0, 0, 0, false, true};
  if ((endpoint & 128) && current.active && MscCommand::is_status(buffer, size)) {
    p.csw = true;
    p.tag = msc_le32(buffer + 4);
    p.residue = msc_le32(buffer + 8);
    p.status = buffer[12];
  }
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
  if (current.active && !p.csw && (endpoint & 128)) {
    p.tag = current.tag;
    p.offset = current.offset + current.transferred;
    p.opcode = current.opcode;
    if (current.opcode != 0x28)
      response(buffer, size, current.tag, current.opcode);
    p.started_us = esp_timer_get_time();
  }
#endif
  const bool accepted = __real_usbd_edpt_xfer(port, endpoint, buffer, size, is_isr);
  pending[slot(endpoint)] = accepted ? p : Pending{};
  if (!accepted && current.active) {
    current.flags |= MscCommand::SUBMIT_FAILED;
    fault(p, 0, 0, 3);
  }
  return accepted;
}

bool __real_mscd_xfer_cb(uint8_t, uint8_t, xfer_result_t, uint32_t);
bool __wrap_mscd_xfer_cb(uint8_t port, uint8_t endpoint, xfer_result_t result, uint32_t actual) {
  if (port == 0) {
    const Pending p = pending[slot(endpoint)];
    pending[slot(endpoint)] = {};
    if (!(endpoint & 128)) {
      if (p.valid && p.size == 31) {
        // Also retain failed/short CBWs; do not parse unreceived bytes.
        if (result != XFER_RESULT_SUCCESS || actual != 31 || !current.start(p.buffer, actual)) {
          fault(p, actual, result, 2);
        } else if (current.opcode == 0x28) {
          record(MSC_CBW, current.offset, current.expected, current.cdb_bytes, current.tag,
                 current.opcode, current.direction);
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
        } else {
          // Preserve all 16 CDB bytes without another buffer or extra serial record.
          record(MSC_CDB, uint64_t(msc_le32(p.buffer + 15)) | (uint64_t(msc_le32(p.buffer + 19)) << 32),
                 msc_le32(p.buffer + 23), msc_le32(p.buffer + 27), current.tag, current.expected,
                 current.direction | (uint32_t(p.buffer[13]) << 8) | (uint32_t(p.buffer[14]) << 16));
#endif
        }
      }
    } else if (p.valid && p.csw) {
      const uint32_t flags = current.status_flags(p.tag, p.residue) |
                            (actual != p.size ? MscCommand::SHORT : 0) |
                            (result != XFER_RESULT_SUCCESS ? MscCommand::TRANSFER_ERROR : 0);
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
      const bool report_status = true;  // Include normal responses to non-READ commands.
#else
      const bool report_status = current.opcode == 0x28 || p.status != 0 || flags != 0;
#endif
      if (report_status)
        record(MSC_CSW, current.transferred, p.residue, p.status, p.tag, current.transfers, flags);
      if (result != XFER_RESULT_SUCCESS || actual != p.size)
        fault(p, actual, result, 1);
      if (result == XFER_RESULT_SUCCESS && actual == p.size)
        current.active = false;
    } else if (p.valid && current.active) {
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
      if (p.started_us != 0) {
        const uint64_t completed = esp_timer_get_time();
        record_at(completed, MSC_DATA, p.offset, p.size, static_cast<uint32_t>(completed - p.started_us),
                  p.tag, actual, uint32_t(result) | (uint32_t(endpoint) << 8) | (uint32_t(p.opcode) << 16));
      }
#endif
      current.data(p.size, actual, result);
      if (result != XFER_RESULT_SUCCESS || actual != p.size)
        fault(p, actual, result, 0);
    }
  }
  return __real_mscd_xfer_cb(port, endpoint, result, actual);
}

void __real_mscd_reset(uint8_t);
void __wrap_mscd_reset(uint8_t port) {
  if (port == 0)
    reset(0);
  __real_mscd_reset(port);
}

bool __real_mscd_control_xfer_cb(uint8_t, uint8_t, const tusb_control_request_t *);
bool __wrap_mscd_control_xfer_cb(uint8_t port, uint8_t stage, const tusb_control_request_t *request) {
  if (port == 0 && stage == CONTROL_STAGE_SETUP) {
    if (request->bmRequestType == 0x21 && request->bRequest == 0xff &&
        request->wValue == 0 && request->wLength == 0)
      reset(1);  // Valid BOT reset, before TinyUSB resets its command state.
    else if (request->bmRequestType == 0x02 && request->bRequest == TUSB_REQ_CLEAR_FEATURE &&
             request->wValue == 0)
      record(MSC_CLEAR_HALT, request->wIndex, 0, 0, current.tag);
  }
  return __real_mscd_control_xfer_cb(port, stage, request);
}
}
#endif  // USE_PROXY_STICK_DEBUG
