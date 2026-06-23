#pragma once

#include <esphome.h>
#include "esphome/components/light/light_output.h"
#include "esphome_dali.h"

namespace esphome {
namespace dali {

enum class DaliColorMode {
    AUTO,
    ON_OFF,
    BRIGHTNESS,
    COLOR_TEMPERATURE,
};

/// @brief Optimistic state update applied to a light's HA-published values
/// without driving the bus, used to keep group member lamps (and a group
/// light itself) in sync with a command sent to a group/broadcast address.
/// Unset fields are left at their previously published value.
struct GroupStateUpdate {
    optional<bool> on;
    optional<float> brightness;
    optional<uint16_t> color_temp_mired;
};

/// @brief Request for DaliLight::perform_call(): only fields with their `has_*`
/// flag set are applied. Used by the web dashboard's direct-control endpoint
/// (/api/lamp) to drive any lamp or group light through the normal
/// light::LightState::make_call() path.
struct ControlRequest {
    bool has_state{false};
    bool state{false};
    bool has_brightness{false};
    float brightness{0.0f};  // 0.0-1.0
    bool has_color_temp{false};
    uint16_t color_temp_mireds{0};

    /// @brief Set by update_circadian_() so perform_call()/write_state() can
    /// distinguish its own color-temperature updates from a manual change (web
    /// dashboard or Home Assistant) and skip disabling circadian on itself.
    bool from_circadian{false};
};

/// @brief A bus command computed by write_state(), either sent immediately or
/// held by the debounce logic in loop() until DALI_LIGHT_COMMAND_DEBOUNCE_MS
/// has elapsed since the last command sent to this light's address.
struct PendingBusWrite {
    bool valid{false};
    bool on{false};
    uint8_t brightness{0};
    float brightness_f{0.0f};
    bool has_color{false};
    uint16_t color_temp_mired{0};
    uint8_t fade_code{0};
};

class DaliLight : public light::LightOutput, public Component {
 public:
    DaliLight(DaliBusComponent* parent)
        : bus(parent)
        , address_(ADDR_BROADCAST)
        , fade_time_()
        , fade_rate_()
        , cold_white_temperature_(100.0f) // 10000K
        , warm_white_temperature_(370.0f) // 2700K
        , tc_supported_(false)
        , dali_tc_coolest_(COLOR_MIREK_COOLEST)
        , dali_tc_warmest_(400.0f)
        , dali_level_min_(1)
        , dali_level_max_(254)
        , dali_level_range_(254.0f)
        , color_mode_()
        , brightness_curve_()
    { }

    light::LightTraits get_traits() override;

    void setup_state(light::LightState *state) override;
    void write_state(light::LightState *state) override;

    /// @brief Flushes a debounced bus command once DALI_LIGHT_COMMAND_DEBOUNCE_MS
    /// has elapsed since the last one (see write_state()/schedule_or_send_()).
    void loop() override;

    /// @brief Poll the lamp's real availability + on/off + brightness and publish to
    /// Home Assistant. Reflects external changes and marks the lamp unavailable when
    /// it stops responding (and re-applies recovery config when it comes back). Uses
    /// publish_state(), so it never re-drives the bus. Driven by the bus loop.
    /// @return true if the device responded (or is not individually pollable, so it
    /// must not count against bus health); false if it was probed and did not answer.
    bool poll_and_publish();

    /// @brief Apply an optimistic state update (from a group/broadcast command
    /// sent to a different light) to this light's published state, without
    /// driving the bus. Unset fields in `update` are left unchanged. No-op if
    /// this light has no LightState yet.
    void publish_optimistic_state(const GroupStateUpdate& update);

    /// @brief Perform a LightCall on this light's bound LightState (on/off,
    /// brightness 0-1, color temperature in mireds -- only fields with their
    /// `has_*` flag set are applied). Goes through the normal
    /// light::LightState::make_call()...perform() path, so write_state()'s
    /// existing debounce/coalescing and HA-sync logic apply unchanged. No-op if
    /// this light has no LightState yet (state_ == nullptr) or, for color temp,
    /// if this light doesn't support it.
    void perform_call(const ControlRequest& req);

    /// @brief This light's last-known color temperature in mireds, derived from
    /// its published color_temperature (0..1) via this light's own coolest/warmest
    /// mired range. Returns 0 if color temperature is not supported.
    uint16_t current_color_temp_mired() const;

    uint8_t address() const { return address_; }

    /// @brief Cached state accessors for the web dashboard (esphome_dali_web.*).
    /// These are updated by poll_and_publish() / setup_state() and are safe to
    /// read from the main loop task (do not call from another task/thread).
    bool is_available() const { return available_; }
    bool has_problem() const { return problem_; }
    bool supports_color_temp() const { return tc_supported_; }
    const light::LightColorValues& remote_values() const { return state_->remote_values; }

    /// @brief This light's color temperature range in the same units as
    /// current_color_temp_mired() / perform_call()'s color_temp_mireds (valid
    /// only if supports_color_temp() is true): the DALI-reported Tc
    /// coolest/warmest range (dali_tc_coolest_/dali_tc_warmest_), not the
    /// HA-facing cold_white_temperature_/warm_white_temperature_ trait range,
    /// which is a separate scale.
    uint16_t min_mireds() const { return (uint16_t) lroundf(dali_tc_coolest_); }
    uint16_t max_mireds() const { return (uint16_t) lroundf(dali_tc_warmest_); }

    void set_address(uint8_t address) { 
        address_ = address; 

        // Prevent dynamic creation of lights with this address
        // (when device discovery is enabled)
        if (this->address_ <= ADDR_SHORT_MAX) {
            bus->register_static_addr(address_);
        }
    }

    void set_cold_white_temperature(float cold_white_temperature) { cold_white_temperature_ = cold_white_temperature; }
    void set_warm_white_temperature(float warm_white_temperature) { warm_white_temperature_ = warm_white_temperature; }

    void set_color_mode(DaliColorMode color_mode) { color_mode_ = color_mode; }
    void set_brightness_curve(DaliLedDimmingCurve brightness_curve) { brightness_curve_ = brightness_curve; }

    void set_fade_time(uint16_t fade_time) { fade_time_ = fade_time; }
    void set_fade_rate(uint16_t fade_rate) { fade_rate_ = fade_rate; }

    // NOTE: Must have a lower priority number than the DALI bus component
    float get_setup_priority() const override { return setup_priority::DATA; }

    // Static trampoline for set_initial_state() callback (ESPHome 2026.4+).
    // Uses s_setup_instance file-static pointer set just before the call.
    static void initial_state_trampoline(light::LightStateRTCState &s);

 protected:
    DaliBusComponent *bus;

    // The LightState this output is bound to (captured in setup_state). Needed so
    // polling can publish updated values back to Home Assistant.
    light::LightState *state_{nullptr};

    // Availability tracking + recovery (set up in setup_state, driven by polling).
    // Combined "Status" sensor: ON = unavailable or DALI STATUS lampFailure bit set.
    binary_sensor::BinarySensor *status_sensor_{nullptr};
    bool last_status_published_{false};
    uint8_t miss_count_{0};
    bool available_{true};
    bool problem_{false};
    bool recovery_config_done_{false};
    void apply_recovery_config_();
    /// @brief Publish problem_ || !available_ to status_sensor_, if changed.
    void publish_status_();

    uint8_t address_;
    optional<uint16_t> fade_time_;
    optional<uint16_t> fade_rate_;

    float cold_white_temperature_;
    float warm_white_temperature_;
    float dali_tc_coolest_;
    float dali_tc_warmest_;
    uint8_t dali_level_min_;
    uint8_t dali_level_max_;
    float dali_level_range_;
    optional<DaliColorMode> color_mode_;
    optional<DaliLedDimmingCurve> brightness_curve_;

    bool tc_supported_;

    // Last color temperature (mireds) sent via setColorTemperature(), so
    // write_state() only re-sends it on change. Per-instance: each DaliLight
    // (lamp or group) tracks its own last-sent value.
    uint16_t last_temperature_{0};

    // Cached initial state from DALI device queries, used by the static
    // trampoline passed to set_initial_state() (ESPHome 2026.4+ callback API).
    float initial_brightness_{1.0f};
    float initial_color_temp_{1.0f};
    bool initial_has_brightness_{false};
    bool initial_has_color_temp_{false};

    // Command debounce/coalescing: rapid successive write_state() calls (e.g. a
    // dragged brightness slider) are coalesced into a single bus command, sent
    // at most every DALI_LIGHT_COMMAND_DEBOUNCE_MS. See loop().
    uint32_t last_bus_write_ms_{0};
    bool has_sent_once_{false};
    PendingBusWrite pending_write_;
    void schedule_or_send_(const PendingBusWrite &pw);
    void send_to_bus_(const PendingBusWrite &pw);

    // Set for the duration of perform_call() when called with
    // ControlRequest::from_circadian, so write_state() (invoked synchronously by
    // call.perform() below) knows not to treat the resulting color-temperature
    // change as a manual override. See disable_circadian_for_manual_override().
    bool circadian_call_in_progress_{false};
};

}  // namespace dali
}  // namespace esphome