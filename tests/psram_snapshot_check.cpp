#include <array>
#include <cassert>
#include <vector>
#include "../components/proxy_stick/psram_layout.h"

using namespace esphome::proxy_stick;

void put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = n >> (i * 8);
}

int main() {
  std::array<uint8_t, 512> boot{}, info{};
  boot[12] = 2;
  boot[13] = 1;
  boot[14] = 32;
  boot[16] = 2;
  boot[510] = 0x55;
  boot[511] = 0xaa;
  put32(boot.data() + 32, 2048);
  put32(boot.data() + 36, 8);
  put32(info.data(), 0x41615252);
  put32(info.data() + 484, 0x61417272);
  put32(info.data() + 492, 10);
  put32(info.data() + 508, 0xaa550000);
  constexpr uint64_t disk_bytes = 1048576;
  const uint32_t prefix = psram_prefix_size(boot.data(), info.data(), disk_bytes);
  assert(prefix == 28672);
  std::vector<uint8_t> image(prefix);
  for (uint32_t i = 0; i < prefix; i++)
    image[i] = (i % 251) + 1;
  std::array<uint8_t, 512> out;
  assert(copy_psram_prefix(image.data(), prefix, disk_bytes, prefix - 100, out.data(), out.size()));
  for (unsigned i = 0; i < out.size(); i++)
    assert(out[i] == (i < 100 ? image[prefix - 100 + i] : 0));
  assert(copy_psram_prefix(image.data(), prefix, disk_bytes, disk_bytes - 512, out.data(), out.size()));
  for (auto byte : out)
    assert(byte == 0);
  assert(!copy_psram_prefix(image.data(), prefix, disk_bytes, disk_bytes - 1, out.data(), 2));
  assert(!copy_psram_prefix(image.data(), prefix, disk_bytes, UINT64_MAX, out.data(), 1));
  assert(copy_psram_prefix(image.data(), prefix, disk_bytes, disk_bytes, out.data(), 0));
  info[0] = 0;
  assert(psram_prefix_size(boot.data(), info.data(), disk_bytes) == 0);
  put32(info.data(), 0x41615252);
  put32(info.data() + 492, UINT32_MAX);
  assert(psram_prefix_size(boot.data(), info.data(), disk_bytes) == 0);
  put32(info.data() + 492, 10);
  assert(psram_prefix_size(boot.data(), info.data(), disk_bytes - 512) == 0);
}
