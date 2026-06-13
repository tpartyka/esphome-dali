#pragma once

// Pure, framework-agnostic helpers for DALI multi-master collision detection on the
// bit-banged master (IEC 62386-101). Kept out of the ESPHome-coupled TX code so the core
// rule can be unit-tested on the host. Used by writeBit() in esphome_dali.cpp.
//
// DALI is Manchester-encoded: every bit cell has two half-bits. In this driver's TX
// convention (see writeBit): the first half drives (bit ? assert : release) and the
// second half the opposite. "assert" pulls the wired-AND bus to 0V (dominant);
// "release" lets it idle high. Because the bus is wired-AND, another transmitter is only
// observable during a half-bit where WE release the line — while we assert, our own low
// dominates and hides everyone else.
namespace dali_tx {

/// @return true if the master is releasing the bus in this half-bit.
/// @param half 0 = first half, 1 = second half.
inline bool master_releasing(bool bit, int half) {
    return half == 0 ? !bit : bit;
}

/// @return true if a collision is observed: we are releasing the bus but read it as
/// asserted (someone else is driving it low).
/// @param rx_asserted the RX pin reads the bus asserted (logic level set by another device).
inline bool is_collision(bool bit, int half, bool rx_asserted) {
    return master_releasing(bit, half) && rx_asserted;
}

}  // namespace dali_tx
