#include "serial_monitor.h"

#include <algorithm>
#include "esphome/core/log.h"

namespace esphome::serial_monitor {
static const char *const TAG = "serial_monitor";
static constexpr uart_port_t INPUT = UART_NUM_1;
static constexpr uart_port_t OUTPUT = UART_NUM_0;

void SerialMonitor::setup() {
  uart_config_t config{};
  config.baud_rate = 1500000;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  esp_err_t error = uart_param_config(INPUT, &config);
  if (error == ESP_OK)
    error = uart_set_pin(INPUT, UART_PIN_NO_CHANGE, 26, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (error == ESP_OK) {
    error = uart_driver_install(INPUT, 8192, 0, 32, &this->events_, 0);
    this->installed_ = error == ESP_OK;
  }
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Receiver setup failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
  this->stopped_ = xSemaphoreCreateBinary();
  if (this->stopped_ == nullptr ||
      xTaskCreate(receive_, "serial_monitor", 4096, this, 1, &this->task_) != pdPASS) {
    ESP_LOGE(TAG, "Unable to start the receiver task");
    uart_driver_delete(INPUT);
    this->installed_ = false;
    this->task_ = nullptr;
    if (this->stopped_ != nullptr) {
      vSemaphoreDelete(this->stopped_);
      this->stopped_ = nullptr;
    }
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "MONITOR READY: Grove yellow GPIO26 RX, 1500000 8N1; USB UART0 output 1500000 baud");
}

void SerialMonitor::flush_() {
  if (this->line_used_ == 0) return;
  const size_t size = this->line_used_;
  this->line_used_ = 0;
  if (uart_write_bytes(OUTPUT, this->line_, size) != static_cast<int>(size))
    ESP_LOGE(TAG, "MONITOR CAPTURE INCOMPLETE: USB serial output failed");
}

void SerialMonitor::forward_(const uint8_t *bytes, size_t size) {
  // One driver write per complete diagnostic line prevents the logger inserting text inside it.
  // Overlong/non-line input remains byte-preserving but cannot have that atomic-line guarantee.
  for (size_t i = 0; i < size; i++) {
    this->line_[this->line_used_++] = bytes[i];
    if (bytes[i] == '\n' || this->line_used_ == sizeof(this->line_)) this->flush_();
  }
}

void SerialMonitor::receive_(void *arg) {
  auto *self = static_cast<SerialMonitor *>(arg);
  uart_event_t event;
  uint8_t bytes[512];
  while (!self->stopping_.load()) {
    if (xQueueReceive(self->events_, &event, portMAX_DELAY) != pdTRUE)
      continue;
    if (self->stopping_.load())
      break;
    if (event.type == UART_DATA) {
      size_t left = event.size;
      while (left != 0 && !self->stopping_.load()) {
        const int count = uart_read_bytes(INPUT, bytes, std::min(left, sizeof(bytes)), 0);
        if (count <= 0) {
          ESP_LOGE(TAG, "MONITOR CAPTURE INCOMPLETE: queued UART data unavailable (%d)", count);
          break;
        }
        left -= count;
        self->forward_(bytes, count);
      }
    } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
      ESP_LOGE(TAG, "MONITOR CAPTURE INCOMPLETE: RX overrun (%d); discarding pending input", event.type);
      uart_flush_input(INPUT);
      xQueueReset(self->events_);
    } else if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR || event.type == UART_BREAK) {
      ESP_LOGW(TAG, "MONITOR UART error %d: check baud rate, ground and signal", event.type);
    }
  }
  self->flush_();
  xSemaphoreGive(self->stopped_);
  vTaskDelete(nullptr);
}

void SerialMonitor::on_shutdown() {
  if (this->task_ != nullptr) {
    this->stopping_.store(true);
    const uart_event_t wake{};
    xQueueSend(this->events_, &wake, 0);
    if (xSemaphoreTake(this->stopped_, pdMS_TO_TICKS(1000)) != pdTRUE) {
      ESP_LOGE(TAG, "Receiver did not stop; leaving its UART intact until reset");
      return;
    }
    this->task_ = nullptr;
    vSemaphoreDelete(this->stopped_);
    this->stopped_ = nullptr;
  }
  if (this->installed_) {
    uart_driver_delete(INPUT);
    this->installed_ = false;
  }
}

void SerialMonitor::dump_config() {
  ESP_LOGCONFIG(TAG, "UART monitor: RX GPIO26 @ 1500000 -> USB serial UART0 @ 1500000; OTA interrupts capture");
}
}  // namespace esphome::serial_monitor
