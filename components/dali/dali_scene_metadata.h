#pragma once

#include <cstdint>

namespace dali_scene_metadata {

constexpr uint8_t LAMP_TARGET_COUNT = 64;
constexpr uint8_t GROUP_TARGET_COUNT = 16;
constexpr uint8_t TARGET_COUNT = LAMP_TARGET_COUNT + GROUP_TARGET_COUNT;
constexpr uint8_t SCENE_COUNT = 16;
constexpr uint16_t NO_COLOR_TEMPERATURE = 0;

/// Software extension for DALI Part 102 scenes: each lamp/group has its own
/// optional color-temperature value for a scene. Zero is reserved as "unset".
/// The fixed layout is deliberately persistence-friendly and has no padding.
struct SceneColorTemperatureBlob {
    uint16_t values[TARGET_COUNT][SCENE_COUNT]{};
};

static_assert(sizeof(SceneColorTemperatureBlob) == TARGET_COUNT * SCENE_COUNT * sizeof(uint16_t),
              "SceneColorTemperatureBlob must be a flat, padding-free table");

inline uint8_t lamp_target(uint8_t address) { return address; }
inline uint8_t group_target(uint8_t group) { return static_cast<uint8_t>(LAMP_TARGET_COUNT + group); }

inline bool is_valid_target(uint8_t target) { return target < TARGET_COUNT; }
inline bool is_valid_scene(uint8_t scene) { return scene < SCENE_COUNT; }

inline bool set_color_temperature(SceneColorTemperatureBlob &blob, uint8_t target, uint8_t scene, uint16_t mireds) {
    if (!is_valid_target(target) || !is_valid_scene(scene) || mireds == NO_COLOR_TEMPERATURE) return false;
    blob.values[target][scene] = mireds;
    return true;
}

inline uint16_t color_temperature(const SceneColorTemperatureBlob &blob, uint8_t target, uint8_t scene) {
    if (!is_valid_target(target) || !is_valid_scene(scene)) return NO_COLOR_TEMPERATURE;
    return blob.values[target][scene];
}

inline void remove_scene(SceneColorTemperatureBlob &blob, uint8_t target, uint8_t scene) {
    if (!is_valid_target(target) || !is_valid_scene(scene)) return;
    blob.values[target][scene] = NO_COLOR_TEMPERATURE;
}

}  // namespace dali_scene_metadata
