#pragma once

#include <cstdint>

namespace dali_rx {

/// ESPHome applies a YAML pin's `inverted` setting in digital_read().  The
/// Pico-DALI2 receiver is physically active-low, so its YAML pin is inverted
/// and an asserted DALI line is exposed here as logical HIGH.
inline bool logical_rx_is_asserted(bool logical_level) {
    return logical_level;
}

/// Decode the logical half-bit levels of a DALI backward frame.
///
/// The caller provides a start symbol, eight Manchester-encoded data bits, and
/// two idle stop bits: [start-hi, start-lo, data0-hi, data0-lo, ..., stop-lo,
/// stop-lo, stop-lo, stop-lo]. Logical HIGH means the DALI line is asserted;
/// the GPIO transport must normalize its GPIO samples so HIGH means the DALI
/// line is asserted.
inline bool decode_backward_frame_halves(const bool (&halves)[22], uint8_t &value) {
    // A backward frame starts with the mandatory logical-1 start symbol.
    if (!halves[0] || halves[1]) return false;

    uint8_t decoded = 0;
    for (int bit = 0; bit < 8; bit++) {
        const bool first_half = halves[2 + bit * 2];
        const bool second_half = halves[3 + bit * 2];
        if (first_half == second_half) return false;
        decoded = static_cast<uint8_t>((decoded << 1) | (first_half ? 1u : 0u));
    }

    // Both complete stop bits must be released/idle. Accepting only the first
    // one would treat a truncated or malformed backward frame as valid.
    if (halves[18] || halves[19] || halves[20] || halves[21]) return false;

    value = decoded;
    return true;
}

}  // namespace dali_rx
