#pragma once

#include <cstdint>

namespace esphome::proxy_stick::diagnostics {

// Independent of ESPHome so aggregation can be checked on the host without a firmware build.
struct CommandSummary {
  uint64_t started_us{0}, offset{0}, bytes{0};
  uint32_t callbacks{0}, max_source_us{0}, delayed{0}, flags{0};
  bool active{false}, reported_idle{false};

  void start(uint64_t now, uint64_t position) {
    *this = {};
    started_us = now;
    offset = position;
    active = true;
  }

  void buffer(uint64_t position, uint32_t size, uint32_t source_us, bool success, bool delay) {
    callbacks++;
    if (position != offset + bytes)
      flags |= 2;  // Non-contiguous callbacks within this inferred command.
    if (source_us > max_source_us)
      max_source_us = source_us;
    if (success)
      bytes += size;
    else
      flags |= 1;
    delayed += delay;
  }
};

}  // namespace esphome::proxy_stick::diagnostics
