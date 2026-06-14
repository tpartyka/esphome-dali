#include "dali_debounce.h"
#include "framework.h"

using namespace dali_debounce;

// --- debounce_should_send_now ----------------------------------------------

TEST(first_command_is_always_sent_immediately) {
    // has_sent_once == false: send regardless of timing.
    CHECK(debounce_should_send_now(0, 0, 150, false));
    CHECK(debounce_should_send_now(1000000, 999999, 150, false));
}

TEST(command_within_debounce_window_is_held) {
    // last sent at t=1000, debounce=150ms, now=1100 -> too soon.
    CHECK(!debounce_should_send_now(1100, 1000, 150, true));
}

TEST(command_at_exactly_debounce_window_is_sent) {
    // now - last == debounce_ms exactly -> send (>=).
    CHECK(debounce_should_send_now(1150, 1000, 150, true));
}

TEST(command_after_debounce_window_is_sent) {
    CHECK(debounce_should_send_now(1200, 1000, 150, true));
}

// --- debounce_should_flush ---------------------------------------------------

TEST(flush_before_window_elapsed_is_false) {
    CHECK(!debounce_should_flush(1100, 1000, 150));
}

TEST(flush_at_exactly_window_elapsed_is_true) {
    CHECK(debounce_should_flush(1150, 1000, 150));
}

TEST(flush_after_window_elapsed_is_true) {
    CHECK(debounce_should_flush(1300, 1000, 150));
}

// --- millis() wraparound (uint32_t arithmetic wraps correctly) --------------

TEST(handles_millis_wraparound) {
    // last_bus_write_ms_ just before wraparound, now just after: the
    // subtraction wraps to a small positive delta, as intended.
    uint32_t last = 0xFFFFFFFFu - 10;  // 10ms before wraparound
    uint32_t now = 50;                 // 60ms after wraparound (50 + 10)
    CHECK(debounce_should_send_now(now, last, 150, true) == false);  // 60ms < 150ms
    CHECK(debounce_should_flush(now, last, 50));  // 60ms >= 50ms
}
