#pragma once

#include <cstdint>

namespace dali_rx_echo {

/// Expected normalized RX state for a forward-frame half-bit. The bit-banged
/// output asserts the DALI line in the first half of a logical 1 and in the
/// second half of a logical 0.
inline bool expected_asserted(bool bit, uint8_t half) {
    return half == 0 ? bit : !bit;
}

/// Per-forward-frame observations of whether RX echoes the DALI line driven by
/// this master. Counters are reset by the ESPHome transport before each frame.
struct Counter {
    uint16_t samples{0};
    uint16_t matches{0};
};

inline void record(Counter &counter, bool bit, uint8_t half, bool rx_asserted) {
    ++counter.samples;
    if (rx_asserted == expected_asserted(bit, half)) {
        ++counter.matches;
    }
}

}  // namespace dali_rx_echo
