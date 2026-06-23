#include "dali_circadian.h"
#include "framework.h"

using namespace dali_circadian;

// Day = 3500K ~= 286 mireds, Night = 2800K ~= 357 mireds.
constexpr uint16_t DAY_MIREDS = 286;
constexpr uint16_t NIGHT_MIREDS = 357;
constexpr float DAY_ELEV = 6.0f;
constexpr float NIGHT_ELEV = -4.0f;

TEST(elevation_at_day_threshold_returns_day_mireds) {
    CHECK_EQ(compute_color_temp_mireds(DAY_ELEV, DAY_ELEV, NIGHT_ELEV, DAY_MIREDS, NIGHT_MIREDS), DAY_MIREDS);
}

TEST(elevation_above_day_threshold_clamped_to_day_mireds) {
    CHECK_EQ(compute_color_temp_mireds(45.0f, DAY_ELEV, NIGHT_ELEV, DAY_MIREDS, NIGHT_MIREDS), DAY_MIREDS);
}

TEST(elevation_at_night_threshold_returns_night_mireds) {
    CHECK_EQ(compute_color_temp_mireds(NIGHT_ELEV, DAY_ELEV, NIGHT_ELEV, DAY_MIREDS, NIGHT_MIREDS), NIGHT_MIREDS);
}

TEST(elevation_below_night_threshold_clamped_to_night_mireds) {
    CHECK_EQ(compute_color_temp_mireds(-45.0f, DAY_ELEV, NIGHT_ELEV, DAY_MIREDS, NIGHT_MIREDS), NIGHT_MIREDS);
}

TEST(elevation_at_midpoint_returns_midpoint_mireds) {
    float mid_elev = (DAY_ELEV + NIGHT_ELEV) / 2.0f;
    uint16_t expected = (uint16_t) lroundf((DAY_MIREDS + NIGHT_MIREDS) / 2.0f);
    CHECK_EQ(compute_color_temp_mireds(mid_elev, DAY_ELEV, NIGHT_ELEV, DAY_MIREDS, NIGHT_MIREDS), expected);
}

TEST(degenerate_thresholds_step_at_midpoint_above) {
    // night_elevation >= day_elevation: degenerate, falls back to a step at
    // the midpoint of the two thresholds rather than dividing by zero.
    float day_elev = 0.0f, night_elev = 0.0f;
    CHECK_EQ(compute_color_temp_mireds(10.0f, day_elev, night_elev, DAY_MIREDS, NIGHT_MIREDS), DAY_MIREDS);
}

TEST(degenerate_thresholds_step_at_midpoint_below) {
    float day_elev = 0.0f, night_elev = 0.0f;
    CHECK_EQ(compute_color_temp_mireds(-10.0f, day_elev, night_elev, DAY_MIREDS, NIGHT_MIREDS), NIGHT_MIREDS);
}

TEST(circadian_enabled_set_round_trip) {
    uint16_t mask = 0;
    CHECK(!circadian_enabled(mask, 3));
    mask = circadian_set(mask, 3, true);
    CHECK(circadian_enabled(mask, 3));
    CHECK(!circadian_enabled(mask, 4));
    mask = circadian_set(mask, 3, false);
    CHECK(!circadian_enabled(mask, 3));
}

TEST(circadian_enabled_out_of_range_group_is_false) {
    uint16_t mask = 0xFFFF;
    CHECK(!circadian_enabled(mask, 16));
    CHECK(!circadian_enabled(mask, 255));
}

TEST(circadian_set_out_of_range_group_is_noop) {
    uint16_t mask = 0;
    CHECK_EQ(circadian_set(mask, 16, true), mask);
}

TEST(blob_size_is_uint16) {
    CHECK_EQ(sizeof(DaliCircadianBlob), sizeof(uint16_t));
}
