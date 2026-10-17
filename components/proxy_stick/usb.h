#pragma once

// The USB side: TinyUSB's mass-storage callbacks, serving the image read-only. These replace the
// storage-only callbacks esp_tinyusb defines, which is why the component links with
// --allow-multiple-definition. When HID arrives, the USB descriptors and callbacks stay in this one file.

#include "tinyusb.h"

namespace esphome::proxy_stick {

// The SCSI inquiry answer, space-padded to 8, 16 and 4 characters by the code generator.
void set_inquiry(const char *vendor, const char *product, const char *revision);
// esp_tinyusb's event hook: bus attach, detach, suspend and resume, logged (and traced).
void usb_event(tinyusb_event_t *event, void *arg);
// Logs the most frequently requested 4 KiB image pages since statistics were cleared.
void dump_hot_blocks();

}  // namespace esphome::proxy_stick
