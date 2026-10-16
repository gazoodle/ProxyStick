#pragma once

// The read-ahead block cache between USB reads and HTTP range reads. Two buffers of the read-block
// size: one being read from, and one a background worker fills with the next block, so a sequential
// read by the host overlaps the network. Without the worker or the spare buffer it falls back to
// synchronous reads. Results from an earlier image are rejected by epoch.

#include <cstdint>
#include <string>

namespace esphome::proxy_stick {

constexpr uint32_t DEFAULT_READ_BLOCK_SIZE = 64 * 1024;

// Allocates the buffers and starts the worker; false only if not even the first buffer fits.
bool setup_cache(uint32_t read_block_size);
// Points the cache at a new image, discarding everything cached from the last.
void configure_cache(const std::string &url, uint64_t disk_size);
void invalidate_cache();
bool read_image_range(uint64_t start, void *buffer, uint32_t size);
bool resize_read_buffers(uint32_t size);
uint32_t read_block_size();
bool prefetch_enabled();

}  // namespace esphome::proxy_stick
