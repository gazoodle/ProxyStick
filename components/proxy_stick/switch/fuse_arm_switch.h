#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"

#include "../proxy_stick.h"

namespace esphome::proxy_stick {

// Arms the USB PHY fuse button (proxy_stick.cpp, schedule_usb_phy_fuse); off cancels a scheduled burn.
class FuseArmSwitch final : public switch_::Switch, public Parented<ProxyStick> {
 protected:
  void write_state(bool state) override {
    this->parent_->arm_usb_phy_fuse(state);
    this->publish_state(state);
  }
};

}  // namespace esphome::proxy_stick
