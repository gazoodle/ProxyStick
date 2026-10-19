#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace esphome::proxy_stick {

// shortcut: this occupied-prefix hint is specific to our contiguous FAT builder; the full-image
// SHA-256 check must succeed before assuming the rest of the volume is zero.
inline uint32_t ram_le32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline uint64_t psram_prefix_size(const uint8_t *boot, const uint8_t *info, uint64_t disk_bytes) {
  const uint32_t reserved = boot[14] | (uint32_t(boot[15]) << 8);
  const uint32_t spc = boot[13];
  const uint32_t next = ram_le32(info + 492);
  if (boot[11] != 0 || boot[12] != 2 || boot[510] != 0x55 || boot[511] != 0xaa ||
      spc == 0 || spc > 128 || (spc & (spc - 1)) != 0 || boot[16] != 2 || reserved < 8 ||
      ram_le32(boot + 36) == 0 || uint64_t(ram_le32(boot + 32)) * 512 != disk_bytes ||
      ram_le32(info) != 0x41615252 || ram_le32(info + 484) != 0x61417272 ||
      ram_le32(info + 508) != 0xaa550000 || next < 2 || next == UINT32_MAX)
    return 0;
  const uint64_t prefix = (reserved + uint64_t(boot[16]) * ram_le32(boot + 36) + uint64_t(next - 2) * spc) * 512;
  return prefix <= disk_bytes ? prefix : 0;
}

inline bool copy_psram_prefix(const uint8_t *image, uint32_t prefix, uint64_t disk_bytes,
                              uint64_t start, void *buffer, uint32_t size) {
  if (start > disk_bytes || size > disk_bytes - start)
    return false;
  const uint32_t copied = start < prefix ? std::min<uint64_t>(size, prefix - start) : 0;
  if (copied != 0)
    std::memcpy(buffer, image + start, copied);
  if (copied < size)
    std::memset(static_cast<uint8_t *>(buffer) + copied, 0, size - copied);
  return true;
}

}  // namespace esphome::proxy_stick
