#pragma once

#include <cstdint>

namespace dali_input {

/// DALI control gear forwards use 16 data bits; DALI-2 control-device event
/// forwards use 24. Any other reconstructed length is a malformed capture and
/// must not be emitted to automations as a real bus frame.
constexpr bool is_supported_forward_frame_length(uint8_t bits) {
  return bits == 16 || bits == 24;
}

}  // namespace dali_input
