#include "framework.h"
#include "dali_scene_metadata.h"

#include <cstdint>

namespace {

using namespace dali_scene_metadata;

TEST(scene_metadata_keeps_same_scene_independent_per_target) {
    SceneColorTemperatureBlob blob{};
    const uint8_t lamp = lamp_target(3);
    const uint8_t group = group_target(3);

    CHECK(set_color_temperature(blob, lamp, 4, 153));
    CHECK(set_color_temperature(blob, group, 4, 370));

    CHECK_EQ(color_temperature(blob, lamp, 4), 153);
    CHECK_EQ(color_temperature(blob, group, 4), 370);
}

TEST(scene_metadata_removes_only_requested_target_scene) {
    SceneColorTemperatureBlob blob{};
    const uint8_t lamp = lamp_target(3);
    const uint8_t group = group_target(3);
    set_color_temperature(blob, lamp, 4, 153);
    set_color_temperature(blob, group, 4, 370);

    remove_scene(blob, lamp, 4);

    CHECK_EQ(color_temperature(blob, lamp, 4), NO_COLOR_TEMPERATURE);
    CHECK_EQ(color_temperature(blob, group, 4), 370);
}

TEST(scene_metadata_rejects_invalid_target_scene_and_temperature) {
    SceneColorTemperatureBlob blob{};
    CHECK(!set_color_temperature(blob, LAMP_TARGET_COUNT + GROUP_TARGET_COUNT, 0, 153));
    CHECK(!set_color_temperature(blob, lamp_target(0), SCENE_COUNT, 153));
    CHECK(!set_color_temperature(blob, lamp_target(0), 0, NO_COLOR_TEMPERATURE));
    CHECK_EQ(color_temperature(blob, LAMP_TARGET_COUNT + GROUP_TARGET_COUNT, 0), NO_COLOR_TEMPERATURE);
}

}  // namespace
