#pragma once

#include <atomic>
#include "esphome/core/component.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace esphome::serial_monitor {

// One-off Atom Lite collector: UART1 RX=26; UART0 belongs to the USB serial logger.
class SerialMonitor : public Component {
 public:
  void setup() override;
  void dump_config() override;
  void on_shutdown() override;

 protected:
  static void receive_(void *arg);
  void forward_(const uint8_t *bytes, size_t size);
  void flush_();
  uint8_t line_[512];
  size_t line_used_{0};
  QueueHandle_t events_{nullptr};
  TaskHandle_t task_{nullptr};
  SemaphoreHandle_t stopped_{nullptr};
  std::atomic_bool stopping_{false};
  bool installed_{false};
};

}  // namespace esphome::serial_monitor
