#pragma once

#include <array>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/defines.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include "common.h"
#include "events.h"

namespace esphome::proxy_stick {

// The diagnostic sensors, in the order the sensor platform's table names them.
enum Metric : uint8_t {
  USB_SCSI_COMMANDS,
  BYTES_SERVED,
  TOTAL_BYTES_SERVED,
  READ_THROUGHPUT,
  MAX_READ_THROUGHPUT,
  SESSION_READ_THROUGHPUT,
  HTTP_FETCH_THROUGHPUT,
  SOURCE_READ_THROUGHPUT,
  USB_TRANSPORT_THROUGHPUT,
  NETWORK_BYTES_FETCHED,
  CACHE_HIT_BYTES,
  HTTP_REQUESTS,
  HTTP_FAILURES,
  AVERAGE_HTTP_MS,
  LAST_HTTP_MS,
  AVERAGE_FIRST_BYTE_MS,
  AVERAGE_HTTP_CHUNK_BYTES,
  PREFETCH_HITS,
  PREFETCH_WAITS,
  AVERAGE_PREFETCH_WAIT_MS,
  PREFETCH_DISCARDS,
  PREFETCH_FAILURES,
  METRIC_COUNT,
};

class ProxyStick : public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_endpoint(const std::string &endpoint);
  void set_token(const std::string &token);
  void set_read_block_size(uint32_t size) { this->read_block_size_ = size; }
  void set_version(const char *version) { this->version_ = version; }
  void set_handover_gap(uint32_t milliseconds) { this->handover_gap_ms_ = milliseconds; }
  // Whether USB_PHY_SEL is burnt: USB-OTG, so the drive, owns the USB pins from power-on.
  bool otg_at_power_on() const;

  void set_enabled(bool enabled);
  bool enabled() const { return this->enabled_; }
  bool upstream_healthy() const { return this->healthy_; }
  bool usb_host_configured() const;
  // Withdraw and reinsert the medium, so the host reads it afresh without losing the USB device.
  void refresh();
  void clear_statistics();
  // Logs the stored USB traces (usb_trace: true), a few lines per loop.
  void dump_trace();
  void dump_hot_blocks();
  // The irreversible USB_PHY_SEL burn (proxy_stick.cpp): arm, schedule, restart.
  void arm_usb_phy_fuse(bool armed);
  void schedule_usb_phy_fuse();
  // 9..16: 512 B to 64 KiB. Only while the USB drive is disabled; for measuring, from a lambda.
  void set_read_block_power(uint8_t power);

#ifdef USE_SENSOR
  void set_sensor(Metric metric, sensor::Sensor *sensor) { this->sensors_[metric] = sensor; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_upstream_sensor(binary_sensor::BinarySensor *sensor) { this->upstream_sensor_ = sensor; }
  void set_usb_host_sensor(binary_sensor::BinarySensor *sensor) { this->usb_host_sensor_ = sensor; }
  void set_otg_sensor(binary_sensor::BinarySensor *sensor) { this->otg_sensor_ = sensor; }
#endif

 protected:
  void offer_(const Manifest &manifest, uint32_t now);
  void apply_(const Manifest &manifest);
  void begin_medium_change_(uint32_t now);
  void finish_medium_change_();
  void remove_media_(const char *why);
  void set_healthy_(bool healthy);
  void attach_usb_();
  void burn_scheduled_usb_phy_fuse_();

  std::string endpoint_;
  std::string token_;
  const char *version_{""};
  uint32_t read_block_size_{0};
  EventStream events_;
  bool events_started_{false};
  bool looped_{false};
  bool driver_installed_{false};
  bool enabled_{true};
  bool healthy_{false};
  bool health_known_{false};  // So the first loop publishes, healthy or not.
  bool discovery_failed_{false};
  uint32_t medium_ready_at_{0};
  uint32_t attach_at_{0};  // When the handover gap ends and the drive attaches; 0 once attached.
  uint32_t handover_gap_ms_{500};
  bool usb_phy_fuse_armed_{false};
  bool usb_phy_fuse_pending_{false};
  ESPPreferenceObject usb_phy_fuse_pref_;
  // What is mounted, and the latest the server announced (applied when a remount pause ends).
  uint32_t revision_{0};
  std::string generation_;
  Manifest latest_;
  bool has_latest_{false};

#ifdef USE_SENSOR
  std::array<sensor::Sensor *, METRIC_COUNT> sensors_{};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *upstream_sensor_{nullptr};
  binary_sensor::BinarySensor *usb_host_sensor_{nullptr};
  binary_sensor::BinarySensor *otg_sensor_{nullptr};
#endif
};

}  // namespace esphome::proxy_stick
