#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dali_event_log {

/// @brief addr value meaning "not lamp-specific" (a bus-wide event).
constexpr uint8_t NO_ADDR = 0xFF;

enum class EventType : uint8_t {
    BusUp,
    BusDown,
    Collision,
    Disconnected,
    Reconnected,
    LampAvailable,
    LampUnavailable,
    LampProblem,
    LampOk,
};

struct Event {
    uint32_t timestamp_ms{0};
    EventType type{EventType::BusUp};
    uint8_t addr{NO_ADDR};
    uint16_t value{0};
};

/// @brief Fixed-capacity ring buffer of the most recent `N` events, oldest
/// evicted first. Used for the dashboard's in-RAM bus event log (lost on
/// reboot, same lifecycle as the existing diagnostic counters).
template <size_t N>
struct RingBuffer {
    std::array<Event, N> entries{};
    size_t count{0};  // number of valid entries (<= N)
    size_t head{0};   // index of the oldest entry

    void push(const Event &e) {
        size_t write_idx = (head + count) % N;
        entries[write_idx] = e;
        if (count < N) {
            count++;
        } else {
            head = (head + 1) % N;
        }
    }

    /// @brief Visit every valid entry, oldest first.
    template <typename F>
    void for_each(F &&f) const {
        for (size_t i = 0; i < count; i++) {
            f(entries[(head + i) % N]);
        }
    }
};

}  // namespace dali_event_log
