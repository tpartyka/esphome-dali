#pragma once

#include "esphome_dali.h"
#include "esphome_dali_input.h"
#include "esphome/core/automation.h"

namespace esphome {
namespace dali {

/// Automation trigger fired for every decoded DALI input-device frame.
/// Exposes `x` of type DaliInputFrame in lambdas (x.raw, x.bits).
class DaliInputFrameTrigger : public Trigger<DaliInputFrame> {
 public:
  explicit DaliInputFrameTrigger(DaliBusComponent *parent) {
    parent->add_on_input_frame_callback([this](DaliInputFrame f) { this->trigger(f); });
  }
};

}  // namespace dali
}  // namespace esphome
