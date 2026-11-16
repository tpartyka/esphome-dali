#pragma once

#include <cstdint>

namespace dali_rescan {

/// @brief Cursor for the bus rescan run after the bus recovers, spread over
/// loop() ticks so that no single tick blocks for long.
///
/// A full synchronous rescan (presence-probe 64 short addresses, then query
/// the groups of every present device) costs ~100 backward-frame reply windows.
/// On a noisy or half-recovered bus that adds up to several seconds inside one
/// loop() call, which is the ESP32 task-watchdog limit (5 s). The caller
/// performs exactly one bus query per tick, as named by next(), and reports
/// the result back.
///
/// Order: Presence 0..63 -> Groups for each present address (only when
/// groups are enabled) -> Finish (returned once) -> idle.
enum class StepKind : uint8_t { None, ProbePresence, QueryGroups, Finish };

struct Step {
    StepKind kind;
    uint8_t addr;
};

class Rescan {
public:
    static constexpr uint8_t ADDR_MAX = 63;

    /// Start (or restart) a rescan. with_groups: query group membership too.
    void start(bool with_groups) {
        phase_ = Phase::Presence;
        next_ = 0;
        present_ = 0;
        with_groups_ = with_groups;
    }

    void cancel() { phase_ = Phase::Idle; }
    bool active() const { return phase_ != Phase::Idle; }

    /// Present addresses seen so far (complete once Finish has been returned).
    uint64_t present_mask() const { return present_; }

    /// The next action to perform. Finish is returned exactly once, after
    /// which the rescan is idle and next() returns None.
    Step next() {
        if (phase_ == Phase::Presence) {
            if (next_ <= ADDR_MAX) return { StepKind::ProbePresence, next_ };
            phase_ = with_groups_ ? Phase::Groups : Phase::Finish;
            next_ = 0;
        }
        if (phase_ == Phase::Groups) {
            while (next_ <= ADDR_MAX && !(present_ & (1ULL << next_))) next_++;
            if (next_ <= ADDR_MAX) return { StepKind::QueryGroups, next_ };
            phase_ = Phase::Finish;
        }
        if (phase_ == Phase::Finish) {
            phase_ = Phase::Idle;
            return { StepKind::Finish, 0 };
        }
        return { StepKind::None, 0 };
    }

    void report_presence(uint8_t addr, bool present) {
        if (phase_ != Phase::Presence || addr != next_) return;
        if (present) present_ |= (1ULL << addr);
        next_ = addr + 1;
    }

    void report_groups(uint8_t addr) {
        if (phase_ != Phase::Groups || addr != next_) return;
        next_ = addr + 1;
    }

private:
    enum class Phase : uint8_t { Idle, Presence, Groups, Finish };
    Phase phase_ = Phase::Idle;
    uint8_t next_ = 0;
    uint64_t present_ = 0;
    bool with_groups_ = false;
};

} // namespace dali_rescan
