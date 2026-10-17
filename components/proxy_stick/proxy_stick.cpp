#include "proxy_stick.h"

#include <cinttypes>

#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_heap_caps.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "hal/usb_serial_jtag_ll.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#include "cache.h"
#include "http.h"
#include "trace.h"
#include "usb.h"

namespace esphome::proxy_stick {

static const char *const TAG = "proxy_stick";
// Long enough for a host to observe no medium before the replacement appears.
static constexpr uint32_t MEDIUM_CHANGE_PAUSE_MS = 1500;

void ProxyStick::set_endpoint(const std::string &endpoint) {
  this->endpoint_ = endpoint;
  while (!this->endpoint_.empty() && this->endpoint_.back() == '/')
    this->endpoint_.pop_back();
}

void ProxyStick::set_token(const std::string &token) {
  this->token_ = token;
  set_authorization(token);
}

void ProxyStick::setup() {
  if (!setup_cache(this->read_block_size_ != 0 ? this->read_block_size_ : DEFAULT_READ_BLOCK_SIZE)) {
    this->mark_failed();
    return;
  }

  trace::begin();
  media_present.store(false);
  sector_count.store(0);

  // The fuse as the chip read it at reset decides this boot's handover; a scheduled burn changes the next.
  const bool otg_at_reset = this->otg_at_power_on();
  this->usb_phy_fuse_pref_ = global_preferences->make_preference<bool>(fnv1_hash("proxy_stick_usb_phy_fuse"));
  this->burn_scheduled_usb_phy_fuse_();
#ifdef USE_BINARY_SENSOR
  if (this->otg_sensor_ != nullptr)
    this->otg_sensor_->publish_state(this->otg_at_power_on());
#endif

  // With USB_PHY_SEL burnt the pins belong to USB-OTG from power-on: nothing to hand over.
  if (otg_at_reset) {
    ESP_LOGI(TAG, "USB-OTG owns the USB pins from power-on; attaching the drive now");
    this->attach_usb_();
    return;
  }

  // From power-on the chip's own USB-Serial/JTAG device holds the bus, and a quick host (a frame's
  // Linux) has enumerated it and given it an address before we get here. Swapping our drive in without
  // the line ever going idle looks to that host like the same device gone deaf: it keeps talking to the
  // old address, our drive at address 0 hears nothing, and after ~1 s the host power-cycles the port
  // (traced on a frame, 2026-10-16). So: release the line, let the host see it unplugged, then attach.
  const usb_serial_jtag_pull_override_vals_t released{};  // No pull-ups or pull-downs: to the host, unplugged.
  usb_serial_jtag_ll_phy_enable_pull_override(&released);
  trace::milestone(trace::JTAG_RELEASED);
  this->attach_at_ = (millis() + this->handover_gap_ms_) | 1;
  ESP_LOGI(TAG, "USB-Serial/JTAG released; attaching the drive in %" PRIu32 " ms", this->handover_gap_ms_);
}

void ProxyStick::attach_usb_() {
  tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
  config.event_cb = usb_event;
  const esp_err_t error = tinyusb_driver_install(&config);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "USB mass-storage initialization failed: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }
  // The pins are the drive's now; the JTAG override has done its job and must not hold our pull-up off.
  usb_serial_jtag_ll_phy_disable_pull_override();
  this->driver_installed_ = true;
  trace::milestone(trace::USB_CONNECT);
  ESP_LOGI(TAG, "USB drive attached; no medium until the image service is heard from");
}

void ProxyStick::loop() {
  const uint32_t now = millis();
  if (this->attach_at_ != 0 && static_cast<int32_t>(now - this->attach_at_) >= 0) {
    this->attach_at_ = 0;
    this->attach_usb_();
  }
  if (!this->looped_) {
    this->looped_ = true;
    trace::milestone(trace::SETUP_DONE);
  }

  // The event stream starts once there is a network to start it on.
  if (!this->events_started_ && network::is_connected()) {
    this->events_started_ = true;
    trace::milestone(trace::NETWORK_UP);
    if (!this->events_.start(this->endpoint_)) {
      ESP_LOGE(TAG, "Unable to start the event stream's task");
      this->mark_failed();
      return;
    }
  }

  // The lease: without a live event stream nothing may be served.
  const bool connected = network::is_connected() && this->events_.connected();
  if (!connected)
    this->remove_media_("the image service is unavailable");
  if (read_failed.exchange(false)) {
    this->remove_media_("a read from the image service failed");
    this->events_.rediscover();
  }

  Manifest manifest;
  switch (this->events_.take(manifest)) {
    case EventStream::Found::MANIFEST:
      this->discovery_failed_ = false;
      this->offer_(manifest, now);
      break;
    case EventStream::Found::FAILED:
      this->discovery_failed_ = true;
      break;
    case EventStream::Found::NOTHING:
      break;
  }

  if (this->medium_ready_at_ != 0 && static_cast<int32_t>(now - this->medium_ready_at_) >= 0)
    this->finish_medium_change_();
  trace::dump_step();

  this->set_healthy_(connected && !this->discovery_failed_ && this->has_latest_);
#ifdef USE_BINARY_SENSOR
  if (this->usb_host_sensor_ != nullptr)
    this->usb_host_sensor_->publish_state(this->usb_host_configured());
#endif
}

// A newly discovered image: mount it, or withdraw the old medium and mount it after a pause.
void ProxyStick::offer_(const Manifest &manifest, uint32_t now) {
  this->latest_ = manifest;
  this->has_latest_ = true;
  if (!this->enabled_)
    return;

  const bool changed = manifest.revision != this->revision_ || manifest.generation != this->generation_;
  const bool has_media = media_present.load();
  if (has_media && !changed)
    return;
  if (has_media) {
    ESP_LOGI(TAG, "New image revision; withdrawing the medium before switching");
    this->begin_medium_change_(now);
    return;
  }
  if (this->medium_ready_at_ != 0)
    return;  // The pause applies the latest when it ends.
  if (usb_read_in_progress()) {
    this->medium_ready_at_ = now | 1;  // Retry from loop once the old read has completed.
    return;
  }

  this->apply_(manifest);
}

void ProxyStick::apply_(const Manifest &manifest) {
  configure_cache(manifest.image_url, manifest.bytes);
  sector_count.store(manifest.bytes / SECTOR_SIZE);
  this->revision_ = manifest.revision;
  this->generation_ = manifest.generation;
  media_present.store(true);
  if (this->driver_installed_)
    notify_media_changed();
  trace::milestone(trace::MEDIUM_READY);
  ESP_LOGI(TAG, "Medium ready: revision %" PRIu32 ", %" PRIu64 " bytes", manifest.revision, manifest.bytes);
}

void ProxyStick::begin_medium_change_(uint32_t now) {
  media_present.store(false);
  if (this->driver_installed_)
    notify_media_changed();
  stats.reset_session();
  this->medium_ready_at_ = (now + MEDIUM_CHANGE_PAUSE_MS) | 1;  // Never 0, which means no pause.
}

void ProxyStick::finish_medium_change_() {
  if (usb_read_in_progress())
    return;
  this->medium_ready_at_ = 0;
  if (this->enabled_ && this->has_latest_ && this->events_.connected() && !this->discovery_failed_)
    this->apply_(this->latest_);
}

void ProxyStick::remove_media_(const char *why) {
  if (!media_present.exchange(false))
    return;
  if (this->driver_installed_)
    notify_media_changed();
  trace::milestone(trace::MEDIUM_REMOVED);
  ESP_LOGW(TAG, "USB medium removed: %s", why);
}

void ProxyStick::set_healthy_(bool healthy) {
  if (this->health_known_ && healthy == this->healthy_)
    return;
  this->health_known_ = true;
  this->healthy_ = healthy;
  if (healthy)
    this->status_clear_warning();
  else
    this->status_set_warning("Image service unavailable");
#ifdef USE_BINARY_SENSOR
  if (this->upstream_sensor_ != nullptr)
    this->upstream_sensor_->publish_state(healthy);
#endif
}

void ProxyStick::set_enabled(bool enabled) {
  if (this->enabled_ == enabled)
    return;
  this->enabled_ = enabled;
  if (!enabled) {
    stats.reset_session();
    this->medium_ready_at_ = 0;
    if (media_present.exchange(false) && this->driver_installed_)
      notify_media_changed();
    ESP_LOGI(TAG, "USB drive disabled; device remains attached with no medium");
    return;
  }
  ESP_LOGI(TAG, "USB drive enabled");
  if (this->has_latest_ && this->events_.connected())
    this->offer_(this->latest_, millis());
}

bool ProxyStick::usb_host_configured() const { return this->driver_installed_ && tud_mounted(); }

void ProxyStick::refresh() {
  if (!this->driver_installed_ || !this->enabled_)
    return;
  ESP_LOGI(TAG, "USB medium refresh requested");
  this->begin_medium_change_(millis());
}

bool ProxyStick::otg_at_power_on() const { return esp_efuse_read_field_bit(ESP_EFUSE_USB_PHY_SEL); }

// Why the USB_PHY_SEL burn is safe on this chip, or nullptr if it is not (the reason is logged).
static const char *usb_phy_fuse_refusal() {
  if (esp_efuse_read_field_bit(ESP_EFUSE_USB_PHY_SEL))
    return "it is already burnt";
  if (esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_OTG_DOWNLOAD_MODE))
    return "USB-OTG download is fused off on this chip (an early batch), so it could never be reflashed over "
           "USB again";
  return nullptr;
}

void ProxyStick::arm_usb_phy_fuse(bool armed) {
  this->usb_phy_fuse_armed_ = armed;
  if (!armed && this->usb_phy_fuse_pending_) {
    this->usb_phy_fuse_pending_ = false;
    this->usb_phy_fuse_pref_.save(&this->usb_phy_fuse_pending_);
    global_preferences->sync();
    ESP_LOGW(TAG, "USB PHY fuse burn cancelled");
  }
}

// The button: records the burn for the next boot, where it happens before USB and WiFi start.
void ProxyStick::schedule_usb_phy_fuse() {
  if (!this->usb_phy_fuse_armed_) {
    ESP_LOGW(TAG, "Not scheduling the USB PHY fuse burn: turn on the arming switch first");
    return;
  }
  if (const char *refusal = usb_phy_fuse_refusal()) {
    ESP_LOGE(TAG, "Not scheduling the USB PHY fuse burn: %s", refusal);
    return;
  }
  this->usb_phy_fuse_pending_ = true;
  this->usb_phy_fuse_pref_.save(&this->usb_phy_fuse_pending_);
  global_preferences->sync();
  ESP_LOGW(TAG, "USB PHY fuse burn scheduled: restart the stick to burn it (irreversible; turn the arming "
                "switch off to cancel)");
}

// At boot, when scheduled. Irreversible: the USB pins go to USB-OTG from power-on for good, so a host only
// ever meets the drive; for hosts that power-cycle a device which disappears (the chip's own USB-Serial/JTAG
// device does, at every boot). The request is cleared first, so a failure cannot repeat at every boot.
void ProxyStick::burn_scheduled_usb_phy_fuse_() {
  this->usb_phy_fuse_pref_.load(&this->usb_phy_fuse_pending_);
  if (!this->usb_phy_fuse_pending_)
    return;
  this->usb_phy_fuse_pending_ = false;
  this->usb_phy_fuse_pref_.save(&this->usb_phy_fuse_pending_);
  global_preferences->sync();

  if (const char *refusal = usb_phy_fuse_refusal()) {
    ESP_LOGE(TAG, "Not burning USB_PHY_SEL: %s", refusal);
    return;
  }
  ESP_LOGW(TAG, "Burning USB_PHY_SEL (irreversible)");
  const esp_err_t error = esp_efuse_write_field_bit(ESP_EFUSE_USB_PHY_SEL);
  if (error != ESP_OK || !this->otg_at_power_on()) {
    ESP_LOGE(TAG, "Burning USB_PHY_SEL failed: %s", esp_err_to_name(error));
    return;
  }
  ESP_LOGW(TAG, "USB_PHY_SEL burnt: from the next power-on a host meets only the drive. Reflash over USB "
                "through the ROM's USB-OTG download mode (hold reset for 2 s)");
}

void ProxyStick::dump_trace() {
#ifdef USE_PROXY_STICK_TRACE
  trace::start_dump();
#else
  ESP_LOGW(TAG, "No USB trace: set usb_trace: true under proxy_stick: to record one");
#endif
}

void ProxyStick::dump_hot_blocks() { proxy_stick::dump_hot_blocks(); }

void ProxyStick::clear_statistics() {
  stats.reset_session();
  this->update();
  ESP_LOGI(TAG, "Session statistics cleared");
}

void ProxyStick::set_read_block_power(uint8_t power) {
  const uint32_t size = 1UL << power;
  if (power < 9 || power > 16 || size == read_block_size())
    return;
  if (this->enabled_ || usb_read_in_progress()) {
    ESP_LOGW(TAG, "Disable the USB drive and wait for its current read before changing the read block size");
    return;
  }
  if (!resize_read_buffers(size)) {
    ESP_LOGW(TAG, "Unable to allocate %" PRIu32 " byte read buffers", size);
    return;
  }
  ESP_LOGI(TAG, "Read block size set to %" PRIu32 " bytes (%s)", size,
           prefetch_enabled() ? "prefetch enabled" : "synchronous fallback");
}

void ProxyStick::update() {
#ifdef USE_SENSOR
  const auto load = [](const std::atomic<uint64_t> &counter) {
    return static_cast<float>(counter.load(std::memory_order_relaxed));
  };
  const auto average = [](uint64_t total, uint64_t count, float scale) {
    return count == 0 ? 0.0f : static_cast<float>(total) / count / scale;
  };
  const auto rate = [](uint64_t bytes, uint64_t microseconds) {
    return microseconds == 0 ? 0.0f : static_cast<float>(bytes) * 1000000.0f / microseconds;
  };

  const uint64_t served = stats.total_served_bytes.load(std::memory_order_relaxed);
  const uint64_t requests = stats.http_request_count.load(std::memory_order_relaxed);
  std::array<float, METRIC_COUNT> values{};
  values[USB_SCSI_COMMANDS] = load(stats.usb_scsi_commands);
  values[BYTES_SERVED] = load(stats.session_served_bytes);
  values[TOTAL_BYTES_SERVED] = static_cast<float>(served);
  values[READ_THROUGHPUT] = load(stats.read_throughput);
  values[MAX_READ_THROUGHPUT] = load(stats.max_read_throughput);
  values[SESSION_READ_THROUGHPUT] = load(stats.session_read_throughput);
  const uint64_t fetched = stats.network_fetched_bytes.load(std::memory_order_relaxed);
  values[HTTP_FETCH_THROUGHPUT] = rate(fetched, stats.http_success_time_us.load(std::memory_order_relaxed));
  values[SOURCE_READ_THROUGHPUT] = load(stats.source_read_throughput);
  values[USB_TRANSPORT_THROUGHPUT] = load(stats.usb_transport_throughput);
  values[NETWORK_BYTES_FETCHED] = static_cast<float>(fetched);
  values[CACHE_HIT_BYTES] = load(stats.cache_hit_bytes);
  values[HTTP_REQUESTS] = static_cast<float>(requests);
  values[HTTP_FAILURES] = load(stats.http_request_failures);
  values[AVERAGE_HTTP_MS] = average(stats.http_request_time_us.load(std::memory_order_relaxed), requests, 1000);
  values[LAST_HTTP_MS] = stats.last_http_request_us.load(std::memory_order_relaxed) / 1000.0f;
  values[AVERAGE_FIRST_BYTE_MS] =
      average(stats.http_first_byte_time_us.load(std::memory_order_relaxed), requests, 1000);
  values[AVERAGE_HTTP_CHUNK_BYTES] = average(stats.http_data_event_bytes.load(std::memory_order_relaxed),
                                             stats.http_data_event_count.load(std::memory_order_relaxed), 1);
  values[PREFETCH_HITS] = load(stats.prefetch_hits);
  values[PREFETCH_WAITS] = load(stats.prefetch_waits);
  values[AVERAGE_PREFETCH_WAIT_MS] =
      average(stats.prefetch_wait_time_us.load(std::memory_order_relaxed),
              stats.prefetch_waits.load(std::memory_order_relaxed), 1000);
  values[PREFETCH_DISCARDS] = load(stats.prefetch_discards);
  values[PREFETCH_FAILURES] = load(stats.prefetch_failures);

  for (uint8_t metric = 0; metric < METRIC_COUNT; metric++) {
    if (this->sensors_[metric] != nullptr)
      this->sensors_[metric]->publish_state(values[metric]);
  }
#endif
}

void ProxyStick::dump_config() {
  ESP_LOGCONFIG(TAG,
                "ProxyStick %s:\n  Endpoint: %s\n  Token: %s\n  Sector size: 512\n  Read block: %" PRIu32
                " bytes\n  Background prefetch: %s\n  USB handover gap: %" PRIu32 " ms\n  Read only: yes",
                this->version_, this->endpoint_.c_str(), YESNO(!this->token_.empty()), read_block_size(),
                YESNO(prefetch_enabled()), this->handover_gap_ms_);

  // Read only: what a host meets at power-on, and whether USB could still reflash this chip if the PHY
  // were given to USB-OTG for good (USB_PHY_SEL). Early-batch chips have USB-OTG download fused off.
  ESP_LOGCONFIG(TAG,
                "  USB eFuses:\n    At power-on: %s\n    USB-OTG download: %s\n"
                "    USB-Serial/JTAG download: %s\n    USB-Serial/JTAG device: %s\n    Download mode: %s",
                this->otg_at_power_on() ? "USB-OTG (the drive is the first device a host sees)"
                                : "USB-Serial/JTAG (a host sees it first, then the drive)",
                esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_OTG_DOWNLOAD_MODE) ? "DISABLED" : "allowed",
                esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE) ? "DISABLED" : "allowed",
                esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_SERIAL_JTAG) ? "DISABLED" : "enabled",
                esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE) ? "DISABLED" : "allowed");
}

}  // namespace esphome::proxy_stick
