#include "dali_availability.h"
#include "framework.h"

using namespace dali_availability;

static const uint8_t THRESHOLD = 3;

// --- availability_on_missing -------------------------------------------------

TEST(missing_below_threshold_stays_available) {
    AvailabilityState s;
    CHECK(!availability_on_missing(s, THRESHOLD));
    CHECK(!availability_on_missing(s, THRESHOLD));
    CHECK(s.available);
    CHECK_EQ(s.miss_count, 2);
}

TEST(missing_reaching_threshold_transitions_to_unavailable) {
    AvailabilityState s;
    CHECK(!availability_on_missing(s, THRESHOLD));
    CHECK(!availability_on_missing(s, THRESHOLD));
    CHECK(availability_on_missing(s, THRESHOLD));  // 3rd miss crosses threshold
    CHECK(!s.available);
    CHECK(!s.recovery_config_done);
    CHECK_EQ(s.miss_count, THRESHOLD);
}

TEST(missing_once_already_unavailable_does_not_re_signal) {
    AvailabilityState s;
    s.available = false;
    s.recovery_config_done = false;
    s.miss_count = 10;
    CHECK(!availability_on_missing(s, THRESHOLD));
    CHECK_EQ(s.miss_count, 11);
}

TEST(miss_count_caps_at_255) {
    AvailabilityState s;
    s.miss_count = 255;
    s.available = false;
    CHECK(!availability_on_missing(s, THRESHOLD));
    CHECK_EQ(s.miss_count, 255);
}

// --- availability_on_present -------------------------------------------------

TEST(present_resets_miss_count) {
    AvailabilityState s;
    s.miss_count = 2;
    CHECK(!availability_on_present(s));
    CHECK_EQ(s.miss_count, 0);
    CHECK(s.available);
}

TEST(present_after_unavailable_transitions_to_available) {
    AvailabilityState s;
    s.available = false;
    s.recovery_config_done = false;
    s.miss_count = THRESHOLD;
    CHECK(availability_on_present(s));
    CHECK(s.available);
    CHECK_EQ(s.miss_count, 0);
    // recovery_config_done is left for the caller to apply + set.
    CHECK(!s.recovery_config_done);
}

TEST(present_while_already_available_does_not_re_signal) {
    AvailabilityState s;
    CHECK(!availability_on_present(s));
    CHECK(s.available);
}
