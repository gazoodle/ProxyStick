#pragma once

// A flight recorder for the USB conversation (`usb_trace: true`): every bus event and SCSI command, what
// the stick answered, and the stick's own milestones, on one clock from boot. It is kept in flash, one
// trace per boot for the last BOOTS boots, because a host that power-cycles the stick wipes RAM; the
// trace of a boot that lost its power is still there when the stick next comes up. `dump` logs them.
//
// Without `usb_trace` it compiles to nothing.

#include <cstddef>
#include <cstdint>

#include "esphome/core/defines.h"

namespace esphome::proxy_stick::trace {

enum Kind : uint8_t {
  BOOT,             // code: reset reason; data: the boot's number, then ms since power-on (RTC counter)
  BUS,              // code: tinyusb_event_id_t (attached, detached, suspended, resumed)
  TEST_UNIT_READY,  // code: 1 ready, 0 not ready (no medium)
  INQUIRY,
  CAPACITY,          // READ CAPACITY(10) or READ FORMAT CAPACITIES; data: the block count given
  START_STOP,        // code: bit 0 start, bit 1 load/eject; data[0]: power condition
  PREVENT_REMOVAL,   // code: prohibit
  WRITE_PROTECT,     // asked whether writable: MODE SENSE(6), or before a write
  READ,              // consecutive reads, merged; data: first and last LBA
  READ_FAILED,       // data: the LBA
  WRITE,             // refused
  REJECTED,          // an unsupported command; code: opcode; data: command bytes 1-8
  MILESTONE,         // code: Milestone
  FULL,              // this boot's trace ran out of room
  ALIVE,             // the heartbeat, early in a boot; data: the latest ms seen, then UsbState bits;
                     // consecutive ones with the same state merge
};

enum Milestone : uint8_t {
  NETWORK_UP,
  STREAM_OPEN,
  STREAM_LOST,
  MEDIUM_READY,
  MEDIUM_REMOVED,
  USB_CONNECT,
  USB_DISCONNECT,
  SETUP_DONE,  // every component set up, the WiFi radio included: the component's first loop
  JTAG_RELEASED,  // the chip's USB-Serial/JTAG device let go of the bus, starting the handover gap
};

// What TinyUSB says of the link, sampled with each heartbeat.
enum UsbState : uint8_t {
  USB_SPOKEN_TO = 1,  // the host has reset the bus and sent our device its first request
  USB_CONFIGURED = 2,
  USB_SUSPENDED = 4,
};

#ifdef USE_PROXY_STICK_TRACE
// Starts this boot's trace: before USB is installed, so the first host request is in it.
void begin();
void record(Kind kind, uint8_t code = 0, const void *data = nullptr, size_t size = 0, bool urgent = false);
inline void milestone(Milestone milestone) { record(MILESTONE, milestone, nullptr, 0, true); }
// Logs every stored boot's trace, a few lines per call so the log keeps up; call until it returns false.
void start_dump();
bool dump_step();
#else
inline void begin() {}
inline void record(Kind, uint8_t = 0, const void * = nullptr, size_t = 0, bool = false) {}
inline void milestone(Milestone) {}
inline void start_dump() {}
inline bool dump_step() { return false; }
#endif

}  // namespace esphome::proxy_stick::trace
