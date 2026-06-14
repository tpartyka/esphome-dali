#pragma once

#include <cstdint>

namespace dali_bus_health {

/// @brief Pure decision logic for DaliBusComponent::update_bus_health_().
/// Tracks consecutive all-NACK poll rounds and flips `online` once
/// `down_threshold` is reached; recovers immediately on any response.
///
/// @param down_rounds Consecutive rounds with no response (capped at 255)
/// @param any_response Whether any control gear responded this round
/// @param online Current bus-online state, updated in place
/// @param down_threshold Consecutive down-rounds before declaring the bus down
/// @return true if this call just transitioned `online` (caller should
///         publish the bus sensor + log + run recovery/down hooks,
///         distinguishing direction by the new value of `online`).
inline bool bus_health_on_round(uint8_t &down_rounds, bool any_response,
                                 bool &online, uint8_t down_threshold) {
    if (any_response) {
        down_rounds = 0;
        if (!online) {
            online = true;
            return true;
        }
        return false;
    }
    if (down_rounds < 255) down_rounds++;
    if (online && down_rounds >= down_threshold) {
        online = false;
        return true;
    }
    return false;
}

} // namespace dali_bus_health
