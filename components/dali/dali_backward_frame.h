#pragma once

#include <cstdint>

namespace dali_rx {

/// Decode the logical half-bit levels of a DALI backward frame.
///
/// The caller provides a start symbol, eight Manchester-encoded data bits, and
/// one idle stop bit: [start-hi, start-lo, data0-hi, data0-lo, ..., stop-lo,
/// stop-lo]. Logical HIGH means the DALI line is asserted; the GPIO transport
/// must apply its configured inversion before collecting the samples.
inline bool decode_backward_frame_halves(const bool (&halves)[20], uint8_t &value) {
    // A backward frame starts with the mandatory logical-1 start symbol.
    if (!halves[0] || halves[1]) return false;

    uint8_t decoded = 0;
    for (int bit = 0; bit < 8; bit++) {
        const bool first_half = halves[2 + bit * 2];
        const bool second_half = halves[3 + bit * 2];
        if (first_half == second_half) return false;
        decoded = static_cast<uint8_t>((decoded << 1) | (first_half ? 1u : 0u));
    }

    // The first complete stop bit must be released/idle.
    if (halves[18] || halves[19]) return false;

    value = decoded;
    return true;
}

}  // namespace dali_rx
