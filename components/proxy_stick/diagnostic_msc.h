#pragma once

#include <cstdint>

namespace esphome::proxy_stick::diagnostics {

// Byte decoding avoids alignment/packing dependencies in USB's on-wire wrappers.
inline uint32_t msc_le32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

struct MscCommand {
  static constexpr uint32_t SHORT = 1, TRANSFER_ERROR = 2, SUBMIT_FAILED = 4,
                            LENGTH_MISMATCH = 8, TAG_MISMATCH = 16;
  uint32_t tag{0}, expected{0}, cdb_bytes{0}, transfers{0}, flags{0};
  uint64_t offset{0}, transferred{0};
  uint8_t opcode{0}, direction{0};
  bool active{false};

  bool start(const uint8_t *p, uint32_t size) {
    if (size != 31 || p == nullptr || msc_le32(p) != 0x43425355)
      return false;
    *this = {};
    tag = msc_le32(p + 4);
    expected = msc_le32(p + 8);
    direction = p[12];
    opcode = p[15];
    if (opcode == 0x28) {
      const uint32_t lba = (uint32_t(p[17]) << 24) | (uint32_t(p[18]) << 16) |
                           (uint32_t(p[19]) << 8) | p[20];
      offset = uint64_t(lba) * 512;
      cdb_bytes = ((uint32_t(p[22]) << 8) | p[23]) * 512;
    }
    active = true;
    return true;
  }

  void data(uint32_t queued, uint32_t actual, uint32_t result) {
    transfers++;
    transferred += actual;
    if (actual != queued)
      flags |= SHORT;
    if (result != 0)
      flags |= TRANSFER_ERROR;
  }

  static bool is_status(const uint8_t *p, uint32_t size) {
    return p != nullptr && size == 13 && msc_le32(p) == 0x53425355;
  }

  uint32_t status_flags(uint32_t status_tag, uint32_t residue) const {
    return flags | (status_tag != tag ? TAG_MISMATCH : 0) |
           (transferred + residue != expected ? LENGTH_MISMATCH : 0);
  }
};

}  // namespace esphome::proxy_stick::diagnostics
