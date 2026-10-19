#pragma once

#include <cstdint>
#include <string>
#include "esphome/core/defines.h"

namespace esphome::proxy_stick::diagnostics {

enum Event : uint32_t {
  BOOT = 1, BUS, COMMAND_BEGIN, READ_BEGIN, BUFFER_READY, READ_FAILED, COMMAND_COMPLETE,
  CACHE_HIT, CACHE_WAIT, FETCH_BEGIN, FETCH_END, DELAY_STATE, DELAY_BEGIN, DELAY_END,
  PREFETCH_JOB, IDLE_INCOMPLETE,
  COMMAND_SUMMARY, COMMAND_INCOMPLETE, COMMAND_ABORTED, FETCH_SUMMARY, SLOW_READ,
  MSC_CBW, MSC_CSW, MSC_RESET, MSC_XFER_FAULT, MSC_CLEAR_HALT,
  MSC_CDB,  // Serial-only full non-READ command block; never sent to the DBG2 HTTP receiver.
  MSC_DATA, MSC_RESPONSE, USB_CONTROL, USB_STALL, USB_CLEAR,
};

#ifdef USE_PROXY_STICK_DEBUG
void begin(const std::string &endpoint);
void record(Event event, uint64_t offset = 0, uint32_t size = 0, uint32_t value = 0, uint32_t fetch = 0,
            uint32_t callbacks = 0, uint32_t flags = 0);
void activity();
void command_begin(uint64_t offset);
void command_buffer(uint64_t offset, uint32_t size, uint32_t source_us, bool success, bool delayed);
void command_complete(bool aborted = false);
uint32_t fetch_begin(uint64_t offset, uint32_t size);
void fetch_end(uint32_t id, uint64_t offset, uint32_t size, bool success, uint32_t elapsed, bool speculative);
std::string correlation(uint32_t fetch);
#ifdef USE_PROXY_STICK_DEBUG_SERIAL
void record_at(uint64_t us, Event event, uint64_t offset = 0, uint32_t size = 0, uint32_t value = 0,
               uint32_t fetch = 0, uint32_t callbacks = 0, uint32_t flags = 0);
// Packed little-endian response bytes; at most 64 per reply, with truncation flagged explicitly.
void response(const uint8_t *bytes, uint32_t size, uint32_t tag, uint32_t flags);
#endif
#else
inline void begin(const std::string &) {}
inline void record(Event, uint64_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0) {}
inline void activity() {}
inline void command_begin(uint64_t) {}
inline void command_buffer(uint64_t, uint32_t, uint32_t, bool, bool) {}
inline void command_complete(bool = false) {}
inline uint32_t fetch_begin(uint64_t, uint32_t) { return 0; }
inline void fetch_end(uint32_t, uint64_t, uint32_t, bool, uint32_t, bool) {}
#endif

}  // namespace esphome::proxy_stick::diagnostics
