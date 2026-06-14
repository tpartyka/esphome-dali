#pragma once

#include <cstdint>

namespace dali_availability {

/// @brief Per-lamp availability tracking state, mirrored from
/// DaliLight's miss_count_/available_/recovery_config_done_ members.
struct AvailabilityState {
    uint8_t miss_count{0};
    bool available{true};
    bool recovery_config_done{false};
};

/// @brief Update state after a successful poll response. Resets the miss
/// count and, if the lamp was previously marked unavailable, flips it back
/// to available (leaving recovery_config_done false so the caller knows to
/// re-apply recovery config).
///
/// @return true if this call just transitioned the lamp from unavailable to
///         available (caller should publish status + log).
inline bool availability_on_present(AvailabilityState &s) {
    s.miss_count = 0;
    if (!s.available) {
        s.available = true;
        return true;
    }
    return false;
}

/// @brief Update state after a poll with no response. Increments the miss
/// count (capped at 255 to avoid wraparound) and, once it reaches
/// threshold, marks the lamp unavailable and clears recovery_config_done so
/// recovery config is re-applied when it returns.
///
/// @return true if this call just transitioned the lamp from available to
///         unavailable (caller should publish status + log).
inline bool availability_on_missing(AvailabilityState &s, uint8_t threshold) {
    if (s.miss_count < 255) s.miss_count++;
    if (s.available && s.miss_count >= threshold) {
        s.available = false;
        s.recovery_config_done = false;
        return true;
    }
    return false;
}

} // namespace dali_availability
