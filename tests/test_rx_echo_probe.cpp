#include "framework.h"
#include "dali_rx_echo_probe.h"

namespace {

TEST(tx_echo_expected_levels_follow_manchester_halves) {
    CHECK(dali_rx_echo::expected_asserted(true, 0));
    CHECK(!dali_rx_echo::expected_asserted(true, 1));
    CHECK(!dali_rx_echo::expected_asserted(false, 0));
    CHECK(dali_rx_echo::expected_asserted(false, 1));
}

TEST(tx_echo_counter_counts_only_matching_rx_samples) {
    dali_rx_echo::Counter counter;
    dali_rx_echo::record(counter, true, 0, true);
    dali_rx_echo::record(counter, true, 1, false);
    dali_rx_echo::record(counter, false, 0, true);  // mismatch

    CHECK_EQ(counter.samples, 3u);
    CHECK_EQ(counter.matches, 2u);
}

}  // namespace
