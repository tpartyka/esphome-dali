#include "dali_bus_health.h"
#include "framework.h"

using namespace dali_bus_health;

static const uint8_t THRESHOLD = 2;

TEST(any_response_resets_down_rounds_and_stays_online) {
    uint8_t down_rounds = 0;
    bool online = true;
    CHECK(!bus_health_on_round(down_rounds, true, online, THRESHOLD));
    CHECK(online);
    CHECK_EQ(down_rounds, 0);
}

TEST(no_response_below_threshold_stays_online) {
    uint8_t down_rounds = 0;
    bool online = true;
    CHECK(!bus_health_on_round(down_rounds, false, online, THRESHOLD));
    CHECK(online);
    CHECK_EQ(down_rounds, 1);
}

TEST(no_response_reaching_threshold_goes_offline) {
    uint8_t down_rounds = 0;
    bool online = true;
    CHECK(!bus_health_on_round(down_rounds, false, online, THRESHOLD));
    CHECK(bus_health_on_round(down_rounds, false, online, THRESHOLD));
    CHECK(!online);
    CHECK_EQ(down_rounds, THRESHOLD);
}

TEST(no_response_while_already_offline_does_not_re_signal) {
    uint8_t down_rounds = THRESHOLD;
    bool online = false;
    CHECK(!bus_health_on_round(down_rounds, false, online, THRESHOLD));
    CHECK(!online);
    CHECK_EQ(down_rounds, THRESHOLD + 1);
}

TEST(down_rounds_caps_at_255) {
    uint8_t down_rounds = 255;
    bool online = false;
    CHECK(!bus_health_on_round(down_rounds, false, online, THRESHOLD));
    CHECK_EQ(down_rounds, 255);
}

TEST(response_after_offline_recovers) {
    uint8_t down_rounds = THRESHOLD;
    bool online = false;
    CHECK(bus_health_on_round(down_rounds, true, online, THRESHOLD));
    CHECK(online);
    CHECK_EQ(down_rounds, 0);
}
