#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "../proxy_stick.h"
#include "../usb.h"

namespace esphome::proxy_stick {

class UsbSwitch final : public switch_::Switch, public Component, public Parented<ProxyStick> {
 public:
  // A switch restores and publishes its own state at boot; without this it shows "off" while the
  // drive is on, and the remembered setting is never applied.
  void setup() override {
    this->parent_->set_enabled(this->get_initial_state_with_restore_mode().value_or(true));
    this->publish_state(this->parent_->enabled());
  }

 protected:
  void write_state(bool state) override {
    this->parent_->set_enabled(state);
    this->publish_state(this->parent_->enabled());
  }
};

#ifdef USE_PROXY_STICK_DEBUG
class UsbReadDelaySwitch final : public switch_::Switch, public Component {
 public:
  void setup() override {
    set_usb_read_delay(false);
    this->publish_state(false);
  }

 protected:
  void write_state(bool state) override {
    set_usb_read_delay(state);
    this->publish_state(state);
  }
};
#endif

}  // namespace esphome::proxy_stick
