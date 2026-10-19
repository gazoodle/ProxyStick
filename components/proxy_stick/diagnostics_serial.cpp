#include "diagnostics.h"

#ifdef USE_PROXY_STICK_DEBUG_SERIAL
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include "driver/uart.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "diagnostic_command.h"

namespace esphome::proxy_stick::diagnostics {
namespace {
constexpr uart_port_t PORT = UART_NUM_1;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t boot = 0;
std::atomic<uint32_t> sequence{0}, dropped{0};
std::atomic<uint32_t> fetch_id{0};
bool ready = false;
SemaphoreHandle_t writer = nullptr;
CommandSummary summary;
constexpr const char *NAMES[] = {
    "BOOT", "BUS", "COMMAND_BEGIN", "READ_BEGIN", "BUFFER_READY", "READ_FAILED", "COMMAND_COMPLETE",
    "CACHE_HIT", "CACHE_WAIT", "FETCH_BEGIN", "FETCH_END", "DELAY_STATE", "DELAY_BEGIN", "DELAY_END",
    "PREFETCH_JOB", "IDLE_INCOMPLETE", "COMMAND_SUMMARY", "COMMAND_INCOMPLETE", "COMMAND_ABORTED",
    "FETCH_SUMMARY", "SLOW_READ", "MSC_CBW", "MSC_CSW", "MSC_RESET", "MSC_XFER_FAULT", "MSC_CLEAR_HALT",
    "MSC_CDB",
    "MSC_DATA", "MSC_RESPONSE", "USB_CONTROL", "USB_STALL", "USB_CLEAR",
};

void write_locked(const char *bytes, int length) {
  size_t free = 0;
  if (length <= 0 || uart_get_tx_buffer_free_size(PORT, &free) != ESP_OK ||
      free < static_cast<size_t>(length) + 64 || uart_write_bytes(PORT, bytes, length) != length)
    dropped.fetch_add(1);
}

void emit(uint64_t us, Event event, uint64_t offset, uint32_t size, uint32_t value, uint32_t fetch,
          uint32_t callbacks, uint32_t flags) {
  if (xSemaphoreTake(writer, 0) != pdTRUE) {
    sequence.fetch_add(1);
    dropped.fetch_add(1);
    return;
  }
  char line[129];
  const char *name = event >= BOOT && event <= USB_CLEAR ? NAMES[event - 1] : "UNKNOWN";
  // Preserve the capture format; native UART buffering now absorbs short command/status bursts.
  const int length = std::snprintf(line, sizeof(line),
      "P %08lx %08lx %08lx %016llx %s %016llx %08lx %08lx %08lx %08lx %08lx\n",
      static_cast<unsigned long>(boot), static_cast<unsigned long>(++sequence),
      static_cast<unsigned long>(dropped.load()), static_cast<unsigned long long>(us), name,
      static_cast<unsigned long long>(offset), static_cast<unsigned long>(size),
      static_cast<unsigned long>(value), static_cast<unsigned long>(fetch),
      static_cast<unsigned long>(callbacks), static_cast<unsigned long>(flags));
  if (length <= 0 || length >= static_cast<int>(sizeof(line))) {
    dropped.fetch_add(1);
    xSemaphoreGive(writer);
    return;
  }
  // Sole writer: IDF's conservative payload bound includes transaction metadata; ISR only frees
  // space. Extra headroom avoids alignment edges, so uart_write_bytes never waits for capacity.
  write_locked(line, length);
  xSemaphoreGive(writer);
}
}  // namespace

void begin(const std::string &) {
  uart_config_t config{};
  config.baud_rate = 1500000;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  esp_err_t error = uart_param_config(PORT, &config);
  if (error == ESP_OK)
    error = uart_set_pin(PORT, 2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (error == ESP_OK)
    error = uart_driver_install(PORT, 256, 4096, 0, nullptr, 0);
  if (error == ESP_OK) {
    error = uart_disable_rx_intr(PORT);  // TX only: an unused RX pin must not generate interrupts.
    if (error == ESP_OK)
      writer = xSemaphoreCreateMutex();
    if (error != ESP_OK || writer == nullptr) {
      uart_driver_delete(PORT);
      ESP_LOGE("proxy_stick.debug", "Could not initialise TX-only serial diagnostics (%s)", esp_err_to_name(error));
      return;
    }
  }
  if (error != ESP_OK) {
    ESP_LOGE("proxy_stick.debug", "Serial diagnostics unavailable: %s", esp_err_to_name(error));
    return;
  }
  boot = esp_random();
  ready = true;
  record(BOOT);
  ESP_LOGI("proxy_stick.debug", "SERIAL DEBUG FULL: TX GPIO2 @ 1500000; TinyUSB level 2, 4096-byte TX buffer");
}

void record(Event event, uint64_t offset, uint32_t size, uint32_t value, uint32_t fetch,
            uint32_t callbacks, uint32_t flags) {
  if (ready)
    emit(esp_timer_get_time(), event, offset, size, value, fetch, callbacks, flags);
}

void record_at(uint64_t us, Event event, uint64_t offset, uint32_t size, uint32_t value,
               uint32_t fetch, uint32_t callbacks, uint32_t flags) {
  if (ready)
    emit(us, event, offset, size, value, fetch, callbacks, flags);
}

void response(const uint8_t *bytes, uint32_t size, uint32_t tag, uint32_t flags) {
  if (bytes == nullptr)
    return;
  const uint32_t captured = std::min<uint32_t>(size, 64);
  for (uint32_t pos = 0; pos < captured; pos += 8) {
    uint64_t packed = 0;
    const uint32_t count = std::min<uint32_t>(8, captured - pos);
    for (uint32_t i = 0; i < count; i++) packed |= uint64_t(bytes[pos + i]) << (i * 8);
    record(MSC_RESPONSE, packed, size, pos, tag, count, flags | (size > captured ? 0x80000000u : 0));
  }
}

int native_print(const char *format, va_list args) {
  // Level-2 USBD/MSC logging runs on the USB task. Never call driver/semaphore/stdio from ISR.
  // If another port/version adds ISR logging, make that omission visible in the same loss counter.
  if (!ready) return 0;
  if (xPortInIsrContext() || xSemaphoreTake(writer, 0) != pdTRUE) {
    sequence.fetch_add(1);
    dropped.fetch_add(1);
    return 0;
  }
  static char partial[240];
  static size_t used = 0;
  char formatted[256];
  const int wanted = std::vsnprintf(formatted, sizeof(formatted), format, args);
  if (wanted < 0 || wanted >= static_cast<int>(sizeof(formatted))) {
    sequence.fetch_add(1);
    dropped.fetch_add(1);
  }
  const size_t count = wanted < 0 ? 0 : std::min<size_t>(wanted, sizeof(formatted) - 1);
  for (size_t i = 0; i < count; i++) {
    const char ch = formatted[i];
    if (ch != '\r' && ch != '\n') partial[used++] = ch;
    if (ch == '\n' || used == sizeof(partial) - 1 || (i + 1 == count && wanted >= 256)) {
      partial[used] = 0;
      char line[320];
      const int length = std::snprintf(line, sizeof(line), "T %08lx %08lx %08lx %016llx %s\n",
          static_cast<unsigned long>(boot), static_cast<unsigned long>(++sequence),
          static_cast<unsigned long>(dropped.load()), static_cast<unsigned long long>(esp_timer_get_time()), partial);
      write_locked(line, length);
      used = 0;
    }
  }
  xSemaphoreGive(writer);
  return wanted;
}

void activity() {}

void command_begin(uint64_t offset) {
  portENTER_CRITICAL(&lock);
  summary.start(esp_timer_get_time(), offset);
  portEXIT_CRITICAL(&lock);
}

void command_buffer(uint64_t offset, uint32_t size, uint32_t source_us, bool success, bool delayed) {
  portENTER_CRITICAL(&lock);
  summary.buffer(offset, size, source_us, success, delayed);
  portEXIT_CRITICAL(&lock);
  if (!success || source_us >= 50000)
    record(success ? SLOW_READ : READ_FAILED, offset, size, source_us);
}

void command_complete(bool aborted) {
  CommandSummary completed;
  portENTER_CRITICAL(&lock);
  if (summary.active) {
    completed = summary;
    summary.active = false;
  }
  portEXIT_CRITICAL(&lock);
  if (completed.active)
    record(aborted ? COMMAND_ABORTED : COMMAND_SUMMARY, completed.offset,
        static_cast<uint32_t>(completed.bytes), static_cast<uint32_t>(esp_timer_get_time() - completed.started_us),
        completed.max_source_us, completed.callbacks,
        completed.flags | (std::min<uint32_t>(completed.delayed, 65535) << 16));
}

uint32_t fetch_begin(uint64_t, uint32_t) { return fetch_id.fetch_add(1) + 1; }
void fetch_end(uint32_t id, uint64_t offset, uint32_t size, bool success, uint32_t elapsed, bool speculative) {
  record(FETCH_SUMMARY, offset, size, elapsed, id, 0, (success ? 0 : 1) | (speculative ? 2 : 0));
}

std::string correlation(uint32_t fetch) {
  char text[48];
  std::snprintf(text, sizeof(text), "%08lx:%lu", static_cast<unsigned long>(boot), static_cast<unsigned long>(fetch));
  return text;
}
}  // namespace esphome::proxy_stick::diagnostics
extern "C" int proxy_stick_tusb_printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int result = esphome::proxy_stick::diagnostics::native_print(format, args);
  va_end(args);
  return result;
}
#endif
