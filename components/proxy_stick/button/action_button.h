#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"

#include "../proxy_stick.h"

namespace esphome::proxy_stick {

// A button that calls one of the stick's methods.
class ActionButton final : public button::Button, public Parented<ProxyStick> {
 public:
  explicit ActionButton(void (ProxyStick::*action)()) : action_(action) {}

 protected:
  void press_action() override { (this->parent_->*this->action_)(); }

  void (ProxyStick::*action_)();
};

}  // namespace esphome::proxy_stick
