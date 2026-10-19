#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#include "../components/serial_monitor/serial_monitor.h"

std::deque<uart_event_t> events;
std::vector<uint8_t> input, output;
std::vector<std::string> messages;
size_t position = 0;
bool fail_install = false, fail_task = false, fail_output = false;
unsigned deleted = 0, stopped = 0, flushed = 0;
void (*receiver)(void *) = nullptr;
void *receiver_arg = nullptr;
struct Finished {};
bool finished = false;
SemaphoreHandle_t xSemaphoreCreateBinary() { return reinterpret_cast<void *>(3); }
int xSemaphoreGive(SemaphoreHandle_t) { finished = true; return 1; }
int xSemaphoreTake(SemaphoreHandle_t, unsigned wait) {
  assert(wait == 1000);
  if (!finished) receiver(receiver_arg);
  assert(finished);
  return 1;
}
void vSemaphoreDelete(SemaphoreHandle_t) {}

void log_message(const char *, const char *message) { messages.emplace_back(message); }
const char *esp_err_to_name(int) { return "test error"; }
int uart_param_config(int port, const uart_config_t *config) {
  assert(port == 1 && config->baud_rate == 1500000);
  return 0;
}
int uart_set_pin(int port, int tx, int rx, int rts, int cts) {
  assert(port == 1 && tx == -1 && rx == 26 && rts == -1 && cts == -1);
  return 0;
}
int uart_driver_install(int port, int rx, int tx, int count, QueueHandle_t *queue, int) {
  assert(port == 1 && rx == 8192 && tx == 0 && count == 32);
  *queue = reinterpret_cast<void *>(1);
  return fail_install ? 1 : 0;
}
int uart_driver_delete(int port) { assert(port == 1); deleted++; return 0; }
int uart_read_bytes(int port, void *buffer, uint32_t count, unsigned wait) {
  assert(port == 1 && wait == 0);
  const size_t length = std::min<size_t>(count, input.size() - position);
  std::memcpy(buffer, input.data() + position, length);
  position += length;
  return length;
}
int uart_write_bytes(int port, const void *buffer, size_t length) {
  assert(port == 0);
  const auto *bytes = static_cast<const uint8_t *>(buffer);
  output.insert(output.end(), bytes, bytes + length);
  return fail_output ? -1 : length;
}
int uart_flush_input(int port) { assert(port == 1); flushed++; return 0; }
int xQueueReset(QueueHandle_t) { events.clear(); return 1; }
int xQueueSend(QueueHandle_t, const void *event, unsigned) {
  events.push_back(*static_cast<const uart_event_t *>(event));
  return 1;
}
int xQueueReceive(QueueHandle_t, void *event, unsigned wait) {
  assert(wait == portMAX_DELAY);
  if (events.empty()) throw Finished{};
  *static_cast<uart_event_t *>(event) = events.front();
  events.pop_front();
  return 1;
}
int xTaskCreate(void (*fn)(void *), const char *, unsigned stack, void *arg, unsigned, TaskHandle_t *task) {
  assert(stack == 4096);
  receiver = fn;
  receiver_arg = arg;
  *task = reinterpret_cast<void *>(2);
  return fail_task ? 0 : 1;
}
void vTaskDelete(TaskHandle_t) { stopped++; }
void run() { try { receiver(receiver_arg); } catch (const Finished &) {} }

int main() {
  esphome::serial_monitor::SerialMonitor monitor;
  monitor.setup();
  assert(!monitor.failed);
  for (unsigned i = 0; i < 1300; i++) input.push_back(i % 256);
  events.push_back({UART_DATA, input.size()});
  run();
  assert(std::equal(output.begin(), output.end(), input.begin()));
  assert(output.size() < input.size());  // A partial line waits for a delimiter, not a UART event.
  events.push_back({UART_FRAME_ERR, 0});
  run();
  assert(messages.back().find("UART error") != std::string::npos);
  events.push_back({UART_BUFFER_FULL, 0});
  run();
  assert(flushed == 1 && messages.back().find("CAPTURE INCOMPLETE") != std::string::npos);
  events.push_back({UART_DATA, 11});  // Includes a newline, forcing an atomic line write.
  run();
  assert(messages.back().find("queued UART data unavailable") != std::string::npos);
  position = 0;
  fail_output = true;
  events.push_back({UART_DATA, 11});
  run();
  assert(messages.back().find("USB serial output failed") != std::string::npos);
  fail_output = false;
  monitor.on_shutdown();
  assert(stopped == 1 && deleted == 1);
  fail_install = true;
  esphome::serial_monitor::SerialMonitor unavailable;
  unavailable.setup();
  assert(unavailable.failed);
  fail_install = false;
  fail_task = true;
  esphome::serial_monitor::SerialMonitor no_task;
  no_task.setup();
  assert(no_task.failed && deleted == 2);
}
