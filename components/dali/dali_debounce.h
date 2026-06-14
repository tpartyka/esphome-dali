#pragma once

#include <cstdint>

namespace dali_debounce {

/// @brief Pure decision logic for the command debounce/coalescing layer in
/// DaliLight::schedule_or_send_()/loop(). Rapid successive write_state() calls
/// (e.g. a dragged brightness slider, or an automation looping quickly) are
/// coalesced to the latest value and sent at most every debounce_ms.
///
/// @param now Current time (millis())
/// @param last_bus_write_ms Time of the last command actually sent to the bus
/// @param debounce_ms Minimum spacing between bus commands
/// @param has_sent_once Whether any command has been sent yet (the very first
///        command is always sent immediately, regardless of timing)
/// @return true if a new command should be sent to the bus immediately;
///         false if it should be held as pending and flushed later by
///         debounce_should_flush().
inline bool debounce_should_send_now(uint32_t now, uint32_t last_bus_write_ms,
                                      uint32_t debounce_ms, bool has_sent_once) {
    if (!has_sent_once) return true;
    return (now - last_bus_write_ms) >= debounce_ms;
}

/// @brief Pure decision logic for flushing a pending debounced command from
/// DaliLight::loop(). Returns true once the debounce window has elapsed since
/// the last command actually sent to the bus.
inline bool debounce_should_flush(uint32_t now, uint32_t last_bus_write_ms,
                                   uint32_t debounce_ms) {
    return (now - last_bus_write_ms) >= debounce_ms;
}

} // namespace dali_debounce
