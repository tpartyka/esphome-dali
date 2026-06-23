#pragma once

#include <cstdint>
#include <cmath>

namespace dali_circadian {

/// @brief Number of DALI groups (0..15), matching dali_names::GROUP_COUNT.
constexpr size_t GROUP_COUNT = 16;

/// @brief Flash-persisted bitmask of which groups have circadian color
/// temperature control enabled (bit g set => enabled for group g).
struct DaliCircadianBlob {
    uint16_t enabled_mask;
};

/// @return true if circadian control is enabled for `group` in `mask`.
inline bool circadian_enabled(uint16_t mask, uint8_t group) {
    if (group >= GROUP_COUNT) return false;
    return (mask & (1u << group)) != 0;
}

/// @return `mask` with `group`'s enabled bit set to `enabled`.
inline uint16_t circadian_set(uint16_t mask, uint8_t group, bool enabled) {
    if (group >= GROUP_COUNT) return mask;
    if (enabled) return mask | (1u << group);
    return mask & ~(uint16_t)(1u << group);
}

/// @brief Interpolate a color temperature (in mireds) for the current sun
/// elevation, linearly between `night_mireds` (at or below
/// `night_elevation_deg`) and `day_mireds` (at or above `day_elevation_deg`).
///
/// Note `day_mireds` is typically the *lower* mireds value (cooler, e.g.
/// 3500K ~ 286 mireds) and `night_mireds` the *higher* one (warmer, e.g.
/// 2800K ~ 357 mireds) -- the interpolation itself is direction-agnostic.
///
/// If the thresholds are degenerate (`night_elevation_deg >=
/// day_elevation_deg`), falls back to a step function at the midpoint
/// elevation rather than dividing by zero.
inline uint16_t compute_color_temp_mireds(float elevation_deg, float day_elevation_deg, float night_elevation_deg,
                                           uint16_t day_mireds, uint16_t night_mireds) {
    float span = day_elevation_deg - night_elevation_deg;
    float t;
    if (span <= 0.0f) {
        float mid = (day_elevation_deg + night_elevation_deg) / 2.0f;
        t = (elevation_deg >= mid) ? 1.0f : 0.0f;
    } else {
        t = (elevation_deg - night_elevation_deg) / span;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    float mireds = night_mireds + t * ((float) day_mireds - (float) night_mireds);
    return (uint16_t) lroundf(mireds);
}

}  // namespace dali_circadian
