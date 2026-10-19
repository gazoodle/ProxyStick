#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "driver/uart.h"
#include "freertos/semphr.h"
#include "../components/proxy_stick/diagnostics.h"

using namespace esphome::proxy_stick::diagnostics;
size_t space = 4096;
bool fail_setup = true;
bool writer_busy = false, fail_space = false, fail_output = false;
bool in_isr = false;
bool xPortInIsrContext() { return in_isr; }
extern "C" int proxy_stick_tusb_printf(const char *, ...);
std::vector<std::string> lines;
uint32_t esp_random() { return 0x12345678; }
int64_t esp_timer_get_time() { return 12345; }
int uart_param_config(int port, const uart_config_t *config) {
  assert(port == 1 && config->baud_rate == 1500000);
  return fail_setup ? 1 : 0;
}
int uart_set_pin(int port, int tx, int rx, int rts, int cts) {
  assert(port == 1 && tx == 2 && rx == -1 && rts == -1 && cts == -1);
  return 0;
}
const char *esp_err_to_name(int) { return "test setup error"; }
SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void *>(1); }
int xSemaphoreTake(SemaphoreHandle_t, unsigned timeout) { assert(timeout == 0); return !writer_busy; }
int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
int uart_driver_install(int port, int rx, int tx, int events, void *, int) {
  assert(port == 1 && rx == 256 && tx == 4096 && events == 0);
  return 0;
}
int uart_driver_delete(int) { return 0; }
int uart_disable_rx_intr(int port) { assert(port == 1); return 0; }
int uart_get_tx_buffer_free_size(int port, size_t *free) {
  assert(port == 1);
  *free = space;
  return fail_space ? 1 : 0;
}
int uart_write_bytes(int port, const void *bytes, size_t size) {
  assert(port == 1 && size + 64 <= space && size <= 320);
  lines.emplace_back(static_cast<const char *>(bytes), size);
  space -= size + 32;  // Simulate queued payload and transaction overhead, with no draining yet.
  return fail_output ? -1 : static_cast<int>(size);
}

int main() {
  begin("unused");
  record(BOOT);
  assert(lines.empty());  // Failed setup cannot write an unconfigured UART.
  fail_setup = false;
  begin("unused");
  assert(lines.size() == 1 && lines.back().find("BOOT") != std::string::npos);
  space = 0;
  record(MSC_RESET);
  assert(lines.size() == 1);  // Full native TX buffer: never enter the blocking write API.
  space = 4096;
  record(MSC_CLEAR_HALT);
  assert(lines.back().find("P 12345678 00000003 00000001 ") == 0);
  record(COMMAND_INCOMPLETE, ~uint64_t(0), ~0u, ~0u, ~0u, ~0u, ~0u);
  assert(lines.back().find("COMMAND_INCOMPLETE") != std::string::npos);
  assert(lines.back().size() <= 128 && lines.back().back() == '\n');
  command_begin(512);
  command_buffer(512, 4096, 50, true, false);
  command_complete();
  assert(lines.back().find("COMMAND_SUMMARY") != std::string::npos);
  const auto count = lines.size();
  command_complete();
  assert(lines.size() == count);
  assert(fetch_begin(0, 1) == 1 && correlation(1) == "12345678:1");
  fetch_end(1, 0, 1, false, 100, false);
  assert(lines.back().find("FETCH_SUMMARY") != std::string::npos);
  const auto before = lines.size();
  space = 4096;
  for (unsigned i = 0; i < 16; i++) record(MSC_CBW);
  assert(lines.size() == before + 16);  // Bursts no longer depend on one hardware FIFO's space.
  writer_busy = true;
  record(MSC_RESET);
  writer_busy = false;
  fail_space = true;
  record(MSC_RESET);
  fail_space = false;
  assert(lines.size() == before + 16);
  space = 4096;
  fail_output = true;
  record(MSC_RESET);
  fail_output = false;
  record(BOOT);
  assert(lines.back().find("00000004 ") != std::string::npos);  // Full, busy, query and write failures.
  space = 4096;
  const auto native_start = lines.size();
  proxy_stick_tusb_printf("USBD %s ", "Setup");
  assert(lines.size() == native_start);  // Fragmented TinyUSB printf calls form one framed line.
  proxy_stick_tusb_printf("%d bytes\r\n", 8);
  assert(lines.size() == native_start + 1 && lines.back().front() == 'T');
  assert(lines.back().find("USBD Setup 8 bytes\n") != std::string::npos);
  in_isr = true;
  proxy_stick_tusb_printf("ISR\n");
  assert(lines.size() == native_start + 1);
  in_isr = false;
  const uint8_t bytes[] = {0x70,0,5,0,0,0,0,10,0,0,0,0,0x20,0};
  response(bytes, sizeof(bytes), 123, 3);
  assert(lines.back().find("MSC_RESPONSE") != std::string::npos);
  record_at(54321, MSC_DATA);
  assert(lines.back().find("000000000000d431") != std::string::npos);
}
