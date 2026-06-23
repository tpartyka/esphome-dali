
#include <esphome.h>
#include <cmath>
#include "dali_availability.h"
#include "dali_debounce.h"
#include "esphome_dali_light.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

using namespace esphome;
using namespace dali;

using namespace esphome::light;

static const char *const TAG = "dali.light";

#define DALI_MAX_BRIGHTNESS_F (254.0f)

// File-static instance pointer for the set_initial_state trampoline.
// Safe because ESPHome setup is single-threaded: each LightState::setup()
// calls setup_state() then invokes the callback in the same frame.
static DaliLight* s_setup_instance = nullptr;

// Consecutive non-responses before a lamp is marked unavailable. >1 avoids
// flapping on a single transient bus collision.
static const uint8_t DALI_AVAIL_MISS_THRESHOLD = 3;

// Minimum spacing between bus commands sent to the same light's address.
// Rapid successive write_state() calls (e.g. a dragged brightness slider, or
// an automation looping quickly) are coalesced to the latest value and sent
// at most this often, to avoid overwhelming control gear with back-to-back
// DAPC commands.
static const uint32_t DALI_LIGHT_COMMAND_DEBOUNCE_MS = 150;

// Convert a fade time in milliseconds to the DALI fade-time code (0..15).
// DALI fade time: t(X) = 0.5 * 2^(X/2) seconds, for X = 1..15 (X = 0 -> no fade).
//   1 -> 0.7s, 2 -> 1.0s, 3 -> 1.4s, 4 -> 2.0s, ... 15 -> 90.5s
static uint8_t dali_fade_code(uint32_t ms) {
    if (ms < 50) return 0;  // effectively immediate
    float seconds = ms / 1000.0f;
    int code = (int) lroundf(2.0f * log2f(2.0f * seconds));
    if (code < 1) code = 1;
    if (code > 15) code = 15;
    return (uint8_t) code;
}

void DaliLight::initial_state_trampoline(LightStateRTCState &s) {
    if (s_setup_instance) {
        if (s_setup_instance->initial_has_brightness_) {
            s.brightness = s_setup_instance->initial_brightness_;
        }
        if (s_setup_instance->initial_has_color_temp_) {
            s.color_temp = s_setup_instance->initial_color_temp_;
        }
    }
}

void dali::DaliLight::setup_state(light::LightState *state) {
    // Initialization code for DaliLight

    // Exclude broadcast and group addresses
    if ((this->address_ != ADDR_BROADCAST) && ((this->address_ & ADDR_GROUP_MASK) == 0)) {
        ESP_LOGD(TAG, "Querying DALI device capabilities...");
        if (bus->dali.isDevicePresent(address_)) {
            ESP_LOGD(TAG, "DALI[%.2x] Is Present", address_);

            this->dali_level_min_ = bus->dali.lamp.getMinLevel(address_);
            this->dali_level_max_ = bus->dali.lamp.getMaxLevel(address_);
            this->dali_level_range_ = (float)(dali_level_max_ - this->dali_level_min_ + 1);
            ESP_LOGD(TAG, "Reported min:%d max:%d", this->dali_level_min_, this->dali_level_max_);

            // NOTE: Some DALI controllers report their device type is LED(6) even though they do also support color temperature,
            // so let's explicitly check if they respond to this:
            this->tc_supported_ = bus->dali.color.isTcCapable(address_);
            if (tc_supported_) {
                ESP_LOGD(TAG, "DALI[%.2x] Supports color temperature", address_);

                // TODO: Don't seem to be getting the full range here?
                // Tc(cool)=153, Tc(warm)=370
                uint16_t coolest = bus->dali.color.queryParameter(address_, DaliColorParam::ColourTemperatureTcCoolest);
                uint16_t warmest = bus->dali.color.queryParameter(address_, DaliColorParam::ColourTemperatureTcWarmest);

                ESP_LOGD(TAG, "Tc(cool)=%d, Tc(warm)=%d", coolest, warmest);
                if (coolest > COLOR_MIREK_WARMEST || warmest > COLOR_MIREK_WARMEST) {
                    ESP_LOGW(TAG, "Tc min/max is out of range!");
                } else {
                    // Store reported coolest/warmest mired values for mapping.
                    // NOTE: Not updating the configuration-provided warm/cool values, those are for UI only.
                    // Ultimately we don't really want to trust the mired range reported by the dimmer
                    // as it depends on the LED strip attached. So we map the UI range into the reported range.
                    this->dali_tc_coolest_ = (float)coolest;
                    //this->dali_tc_warmest_ = (float)warmest;
                }
            }
            else {
                ESP_LOGD(TAG, "Does not support color temperature");
            }

            ESP_LOGD(TAG, "Sending configuration to device...");

            if (this->brightness_curve_.has_value()) {
                switch (this->brightness_curve_.value()) {
                    case DaliLedDimmingCurve::LOGARITHMIC: ESP_LOGD(TAG, "Setting brightness curve to LOGARITHMIC"); break;
                    case DaliLedDimmingCurve::LINEAR:      ESP_LOGD(TAG, "Setting brightness curve to LINEAR"); break;
                }
                bus->dali.led.setDimmingCurve(address_, this->brightness_curve_.value());
            }

            if (this->fade_rate_.has_value()) {
                ESP_LOGD(TAG, "Setting fade rate: %d", this->fade_rate_.value());
                bus->dali.lamp.setFadeRate(address_, this->fade_rate_.value());
            }
            if (this->fade_time_.has_value()) {
                ESP_LOGD(TAG, "Setting fade time: %d", this->fade_time_.value());
                bus->dali.lamp.setFadeTime(address_, this->fade_time_.value());
            }

            // bus->dali.lamp.setMinLevel(address_, 1);
            // bus->dali.lamp.setMaxLevel(address_, 254);

            // Query the actual brightness level of the device and 
            // ensure this is reflected in the ESPHome component itself...
            uint8_t current_level = bus->dali.lamp.getCurrentLevel(address_);
            if (current_level != 0) {
                // TODO: Do we need to take into account reported min/max brightness?
                initial_brightness_ = (current_level * (1.0f/255.0f));
                initial_has_brightness_ = true;
                ESP_LOGD(TAG, "Restore brightness level: %.2f", initial_brightness_);
            }

            if (tc_supported_) {
                uint16_t current_temperature = bus->dali.color.getColorTemperature(address_);
                if (current_temperature != 0) {
                    float mired = (float)current_temperature;
                    // Need to convert mireds to 0..1 range
                    initial_color_temp_ = (current_temperature - dali_tc_coolest_) / (dali_tc_warmest_ - dali_tc_coolest_);
                    initial_has_color_temp_ = true;
                    ESP_LOGD(TAG, "Restore colour temperature: %.2f", initial_color_temp_);
                }
            }

            s_setup_instance = this;
            state->set_initial_state(initial_state_trampoline);

            this->available_ = true;
            this->miss_count_ = 0;
        }
        else {
            // Address known (e.g. restored from saved inventory) but not answering
            // right now. Create the entity anyway so Home Assistant keeps it, using
            // safe default level limits, and let polling flip it available + restore
            // its recovery config when it reappears. This is the "missing" state.
            ESP_LOGW(TAG, "DALI[%.2x] not present at setup; tracking as unavailable (missing)", address_);
            this->dali_level_min_ = 1;
            this->dali_level_max_ = 254;
            this->dali_level_range_ = 254.0f;
            this->available_ = false;
            this->miss_count_ = DALI_AVAIL_MISS_THRESHOLD;
            this->recovery_config_done_ = false;
        }

        // Always register for polling + diagnostics, present or missing, so the bus
        // can track availability and restore configuration if the device appears.
        this->state_ = state;
        bus->register_pollable_light(this);
        bus->register_command_light(this);
        this->status_sensor_ = bus->create_status_sensor(this->address_);

        if (this->available_) {
            // Apply power-failure recovery config now; polling re-applies it on each
            // unavailable->available transition thereafter.
            this->apply_recovery_config_();
            this->recovery_config_done_ = true;
        } else {
            this->publish_status_();
        }
    }
    else {
        // Broadcast/group address: no per-device polling or capability detection
        // (no single queryable state). Group lights get color-temperature support
        // forced on by create_group_light_component()'s set_color_mode() call, since
        // CT commands to a group are spec-legal no-ops for non-CT members (IEC
        // 62386-209). No broadcast light entity is created, so this is moot for it.
        this->state_ = state;
        bus->register_command_light(this);
    }


    // if (this->color_mode_.has_value()) {
    //     if (this->color_mode_.value() == DaliColorMode::COLOR_TEMPERATURE) {
    //         tc_supported_ = true;
    //         ESP_LOGD(TAG, "Override: enable color temperature support");
    //     } else {
    //         tc_supported_ = false;
    //         ESP_LOGD(TAG, "Override: disable color temperature support");
    //     }
    // }
}

light::LightTraits dali::DaliLight::get_traits() {
    light::LightTraits traits;

    // NOTE: This is called repeatedly, do not perform any bus queries here...

    // Force a color mode irrespective of what the device itself says it supports
    // eg. you can convert a CT capable device to a plain brighness device,
    // or force colour temperature support and hope the device recognizes the command...
    if (this->color_mode_.has_value()) {
        switch (this->color_mode_.value()) {
            case DaliColorMode::COLOR_TEMPERATURE: 
                this->tc_supported_ = true;
                traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
                traits.set_min_mireds(this->cold_white_temperature_);
                traits.set_max_mireds(this->warm_white_temperature_);
                break;
            case DaliColorMode::BRIGHTNESS:
                this->tc_supported_ = false;
                traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
                break;
            case DaliColorMode::ON_OFF:
                this->tc_supported_ = false;
                traits.set_supported_color_modes({light::ColorMode::ON_OFF});
                break;
        }
    }
    else {
        // Device reports color temperature support
        if (this->tc_supported_) {
            traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
            traits.set_min_mireds(this->cold_white_temperature_);
            traits.set_max_mireds(this->warm_white_temperature_);
        }
        else {
            traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
        }
    }

    return traits;
}

uint16_t dali::DaliLight::current_color_temp_mired() const {
    if (!tc_supported_ || this->state_ == nullptr) return 0;
    float color_temperature = this->state_->remote_values.get_color_temperature();
    float mired = (color_temperature * (dali_tc_warmest_ - dali_tc_coolest_)) + dali_tc_coolest_;
    return static_cast<uint16_t>(mired);
}

void dali::DaliLight::publish_optimistic_state(const GroupStateUpdate& update) {
    if (this->state_ == nullptr) return;

    LightColorValues v = this->state_->remote_values;
    if (update.on.has_value()) v.set_state(update.on.value());
    if (update.brightness.has_value()) v.set_brightness(update.brightness.value());
    if (update.color_temp_mired.has_value() && tc_supported_) {
        float color_temperature = (update.color_temp_mired.value() - dali_tc_coolest_) / (dali_tc_warmest_ - dali_tc_coolest_);
        if (color_temperature < 0.0f) color_temperature = 0.0f;
        if (color_temperature > 1.0f) color_temperature = 1.0f;
        v.set_color_temperature(color_temperature);
        // Keep our own change-suppression in write_state() in sync with the value
        // we're now reporting, so a subsequent identical command isn't skipped.
        this->last_temperature_ = update.color_temp_mired.value();
    }

    this->state_->current_values = v;
    this->state_->remote_values = v;
    this->state_->publish_state();
}

void dali::DaliLight::perform_call(const ControlRequest& req) {
    if (this->state_ == nullptr) return;

    auto call = this->state_->make_call();
    if (req.has_state) call.set_state(req.state);
    if (req.has_brightness) call.set_brightness(req.brightness);
    if (req.has_color_temp && tc_supported_) {
        // Inverse of current_color_temp_mired()'s mapping.
        float color_temperature = (req.color_temp_mireds - dali_tc_coolest_) / (dali_tc_warmest_ - dali_tc_coolest_);
        if (color_temperature < 0.0f) color_temperature = 0.0f;
        if (color_temperature > 1.0f) color_temperature = 1.0f;
        call.set_color_temperature(color_temperature);
    }

    // call.perform() invokes write_state() synchronously; circadian_call_in_progress_
    // tells it whether this color-temperature change is circadian's own update.
    this->circadian_call_in_progress_ = req.from_circadian;
    call.perform();
    this->circadian_call_in_progress_ = false;
}

void dali::DaliLight::write_state(light::LightState *state) {
    bool on;
    float brightness;
    float color_temperature;

    state->current_values_as_binary(&on);

    PendingBusWrite pw;
    pw.valid = true;
    pw.on = on;

    // Apply the configured fade before the level command: fade-out when turning off,
    // fade-in when turning on or dimming. DALI fades the level (and any color
    // temperature loaded just below) in hardware over this time.
    pw.fade_code = dali_fade_code(on ? bus->fade_in_ms() : bus->fade_out_ms());

    if (!on) {
        pw.brightness = 0;

        // A group/broadcast command reaches its members on the bus instantly, but
        // their individual HA entities only learn of it on the next poll cycle.
        // Optimistically reflect the new state now so HA stays in sync.
        if ((address_ & ADDR_GROUP_MASK) != 0) {
            GroupStateUpdate update;
            update.on = false;
            bus->sync_group_member_states(address_ & 0x0F, update);
        }
        schedule_or_send_(pw);
        return;
    }

    if (tc_supported_) {
        state->current_values_as_ct(&color_temperature, &brightness);

        // Map temperature 0..1 to reported TC coolest/warmest mireds
        // NOTE: Not using the configuration warm/cool colours - these may not match the reported range of the DALI device.
        float color_temperature_mired = (color_temperature * (dali_tc_warmest_ - dali_tc_coolest_)) + dali_tc_coolest_;

        uint16_t dali_color_temperature = static_cast<uint16_t>(color_temperature_mired);

        // Only update if temperature has changed, to allow faster brightness changes
        if (dali_color_temperature != last_temperature_) {
            // A real color-temperature change on a circadian-enabled group that
            // didn't come from update_circadian_() itself is a manual override
            // (web dashboard or Home Assistant) -- disable circadian for this
            // group so the next 60s tick doesn't silently revert it.
            if (!circadian_call_in_progress_ && (address_ & ADDR_GROUP_MASK) != 0) {
                bus->disable_circadian_for_manual_override(address_ & 0x0F);
            }
            last_temperature_ = dali_color_temperature;
            pw.has_color = true;
            pw.color_temp_mired = dali_color_temperature;
        }
    } else {
        state->current_values_as_brightness(&brightness);
    }

    int dali_brightness = static_cast<uint8_t>(brightness * this->dali_level_range_) + this->dali_level_min_ - 1;
    if (dali_brightness < 1) dali_brightness = 1;
    if (dali_brightness > 254) dali_brightness = 254;

    pw.brightness = (uint8_t) dali_brightness;
    pw.brightness_f = brightness;

    // See comment above: optimistically sync group commands to member lamps.
    if ((address_ & ADDR_GROUP_MASK) != 0) {
        GroupStateUpdate update;
        update.on = true;
        update.brightness = brightness;
        if (tc_supported_) update.color_temp_mired = static_cast<uint16_t>(color_temperature * (dali_tc_warmest_ - dali_tc_coolest_) + dali_tc_coolest_);
        bus->sync_group_member_states(address_ & 0x0F, update);
    }

    schedule_or_send_(pw);
}

void dali::DaliLight::schedule_or_send_(const PendingBusWrite &pw) {
    uint32_t now = millis();
    if (dali_debounce::debounce_should_send_now(now, last_bus_write_ms_, DALI_LIGHT_COMMAND_DEBOUNCE_MS, has_sent_once_)) {
        send_to_bus_(pw);
        last_bus_write_ms_ = now;
        has_sent_once_ = true;
        pending_write_.valid = false;
    } else {
        // Too soon since the last command to this address: hold this one and let
        // loop() flush it once the debounce window elapses. A newer pending write
        // simply overwrites an older one, so only the latest value is ever sent.
        pending_write_ = pw;
    }
}

void dali::DaliLight::send_to_bus_(const PendingBusWrite &pw) {
    bus->dali.lamp.setFadeTime(address_, pw.fade_code);

    if (!pw.on) {
        // Short cut: send power off command
        //bus->dali.lamp.turnOff(address_); // no fade
        bus->dali.lamp.setBrightness(address_, 0); // fade
        return;
    }

    if (pw.has_color) {
        ESP_LOGD(TAG, "DALI[%d] Tc=%d", address_, pw.color_temp_mired);

        // IMPORTANT: Do not set start_fade (activate), or the color temperature fade will
        // be cancelled when we next call setBrightness, and no color change will occur.
        bus->dali.color.setColorTemperature(address_, pw.color_temp_mired, false);
    }

    ESP_LOGD(TAG, "DALI[%d] B=%.2f (%d)", address_, pw.brightness_f, pw.brightness);
    bus->dali.lamp.setBrightness(address_, pw.brightness);
}

void dali::DaliLight::loop() {
    if (!pending_write_.valid) return;
    uint32_t now = millis();
    if (!dali_debounce::debounce_should_flush(now, last_bus_write_ms_, DALI_LIGHT_COMMAND_DEBOUNCE_MS)) return;

    PendingBusWrite pw = pending_write_;
    pending_write_.valid = false;
    send_to_bus_(pw);
    last_bus_write_ms_ = now;
}

void dali::DaliLight::publish_status_() {
    if (this->status_sensor_ == nullptr) return;
    bool status = this->problem_ || !this->available_;
    if (status == this->last_status_published_) return;
    this->last_status_published_ = status;
    this->status_sensor_->publish_state(status);
}

void dali::DaliLight::apply_recovery_config_() {
    if ((this->address_ == ADDR_BROADCAST) || ((this->address_ & ADDR_GROUP_MASK) != 0)) return;
    uint8_t pol = bus->power_on_level();
    uint8_t sfl = bus->system_failure_level();
    bus->dali.lamp.setPowerOnLevel(this->address_, pol);
    bus->dali.lamp.setSystemFailureLevel(this->address_, sfl);
    ESP_LOGD(TAG, "DALI[%d] applied recovery config (power-on=%u, sys-fail=%u)", this->address_, pol, sfl);
}

bool dali::DaliLight::poll_and_publish() {
    // Not individually pollable (no state object, or broadcast/group address): return
    // true so it isn't counted as a NACK against bus health.
    if (this->state_ == nullptr) return true;
    if ((this->address_ == ADDR_BROADCAST) || ((this->address_ & ADDR_GROUP_MASK) != 0)) return true;

    // --- Availability -------------------------------------------------------
    bool present = bus->dali.isDevicePresent(this->address_);
    dali_availability::AvailabilityState avail{this->miss_count_, this->available_, this->recovery_config_done_};
    if (!present) {
        if (dali_availability::availability_on_missing(avail, DALI_AVAIL_MISS_THRESHOLD)) {
            this->publish_status_();
            bus->log_event_(dali_event_log::EventType::LampUnavailable, this->address_);
            ESP_LOGW(TAG, "DALI[%d] not responding -> unavailable", this->address_);
        }
        this->miss_count_ = avail.miss_count;
        this->available_ = avail.available;
        this->recovery_config_done_ = avail.recovery_config_done;
        return false;  // probed, no answer — hold last state, count against bus health
    }
    if (dali_availability::availability_on_present(avail)) {
        this->publish_status_();
        bus->log_event_(dali_event_log::EventType::LampAvailable, this->address_);
        ESP_LOGI(TAG, "DALI[%d] responding again -> available", this->address_);
    }
    this->miss_count_ = avail.miss_count;
    this->available_ = avail.available;
    this->recovery_config_done_ = avail.recovery_config_done;
    if (!this->recovery_config_done_) {
        // Device recovered (e.g. after a power cut) — restore our configuration.
        this->apply_recovery_config_();
        this->recovery_config_done_ = true;
    }

    // --- State --------------------------------------------------------------
    uint8_t status = bus->dali.lamp.getStatus(this->address_);

    // Fault reporting (IEC 62386-102 STATUS bit 1 = lampFailure). Independent of
    // fade state, so publish it before the mid-fade early return below.
    bool problem = (status & STATUS_LAMP_FAILURE) != 0;
    if (problem != this->problem_) {
        this->problem_ = problem;
        this->publish_status_();
        bus->log_event_(problem ? dali_event_log::EventType::LampProblem : dali_event_log::EventType::LampOk,
                         this->address_);
        ESP_LOGW(TAG, "DALI[%d] lamp failure -> %s", this->address_, problem ? "PROBLEM" : "OK");
    }

    if (status & STATUS_FADE_STATE) return true;  // mid-fade; don't publish an intermediate value
    bool on = (status & STATUS_LAMP_ON) != 0;

    // Start from the current published values so we preserve color temperature, then
    // update on/off and brightness from what the device actually reports.
    LightColorValues v = this->state_->remote_values;
    v.set_state(on);
    if (on) {
        uint8_t level = bus->dali.lamp.getCurrentLevel(this->address_);
        if (level != 0xFF && level != 0) {
            // Inverse of write_state()'s mapping, for a clean command/report round-trip.
            float brightness = ((float) level - this->dali_level_min_ + 1) / this->dali_level_range_;
            if (brightness < 0.0f) brightness = 0.0f;
            if (brightness > 1.0f) brightness = 1.0f;
            v.set_brightness(brightness);
        }
    }

    // Skip if nothing meaningfully changed (avoid log/API spam every poll).
    const LightColorValues &cur = this->state_->remote_values;
    float db = cur.get_brightness() - v.get_brightness();
    if (db < 0) db = -db;
    bool changed = (cur.is_on() != v.is_on()) || (on && db > 0.02f);
    if (!changed) return true;

    this->state_->current_values = v;
    this->state_->remote_values = v;
    this->state_->publish_state();
    ESP_LOGD(TAG, "DALI[%d] external state -> %s", this->address_, on ? "ON" : "OFF");
    return true;
}
