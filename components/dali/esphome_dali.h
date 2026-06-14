#pragma once

// Include the specific esphome headers we depend on *before* <esphome.h>.
// The generated esphome.h includes component headers alphabetically, so this
// header can be pulled in before esphome/core/component.h is defined; relying on
// <esphome.h> alone then fails with "expected class-name" on `public Component`.
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/preferences.h"
#include "esphome/components/light/light_state.h"
#include <esphome.h>
#include <vector>
#include "dali.h"
#include "esphome_dali_input.h"
#include "esphome_dali_web.h"

namespace esphome {

namespace binary_sensor { class BinarySensor; }  // fwd: per-lamp availability sensors
namespace sensor { class Sensor; }  // fwd: bus diagnostic counters

namespace dali {

class DaliLight;  // forward decl: the bus keeps a list of lights to poll
struct GroupStateUpdate;  // forward decl: optimistic state update for group member sync

enum class DaliInitMode {
    DiscoverOnly,
    InitializeUnassigned,
    InitializeAll
};

class DaliBusComponent : public Component, public DaliPort {
public:
    DaliBusComponent()
        : Component { }
        , dali { *this }
    { }

    void setup() override;
    void loop() override;
    void dump_config() override;

    void set_tx_pin(GPIOPin* tx_pin) { m_txPin = tx_pin; }
    // RX must be interrupt-capable (InternalGPIOPin) so the input-device listener
    // can attach an edge interrupt; digital_read still works for the bit-bang RX.
    void set_rx_pin(InternalGPIOPin* rx_pin) { m_rxPin = rx_pin; }

    /// @brief Perform automatic device discovery on setup.
    /// Light components will automatically be created and appear in HomeAssistant
    void do_device_discovery() { m_discovery = true; }

    /// @brief Initialize long and short addresses for devices on the bus.
    /// @param mode 
    //          InitializeUnassigned - only devices that do not yet have an assigned short address
    ///         InitializeAll - all devices on the bus
    /// @note
    void do_initialize_addresses(DaliInitMode mode = DaliInitMode::InitializeUnassigned) { m_initialize_addresses = mode; }

    // NOTE: Must have a higher priority number than the components that depend on this.
    // ie, this must be initialized first.
    float get_setup_priority() const override { return setup_priority::HARDWARE; }

    /// @brief Run device discovery / address assignment.
    /// The no-arg form uses the YAML-configured mode and is called once from setup().
    /// The mode form lets callers force a specific behavior (e.g. the button uses
    /// InitializeUnassigned). Lights found here only appear in Home Assistant if
    /// registered before the API connects (i.e. during setup()).
    void run_discovery();
    void run_discovery(DaliInitMode mode);

    /// @brief Button action: log the short addresses of already-assigned devices,
    /// then assign addresses to any unassigned (new) lamps. Driven by the
    /// auto-created "Run DALI Discovery" button.
    void scan_and_assign();

    /// @brief Enable passive listening for DALI input-device (Part 103) frames.
    void do_input_devices() { m_input_devices = true; }

    /// @brief Register a callback for every decoded input-device frame.
    void add_on_input_frame_callback(std::function<void(DaliInputFrame)> cb) {
        m_input_listener.add_on_input_frame_callback(std::move(cb));
    }

    void register_static_addr(short_addr_t short_addr) {
        if (short_addr < ADDR_SHORT_MAX) {
            m_addresses[short_addr] = 0xFFFFFF;
        }
    }

    /// @brief How often (ms) to poll each lamp's real state so Home Assistant
    /// reflects external changes. 0 disables polling.
    void set_state_poll_interval(uint32_t ms) { m_state_poll_interval_ms = ms; }

    /// @brief Default fade times (milliseconds). Also settable at runtime via the HA
    /// "DALI Fade In/Out Time" number entities. Applied to the device's hardware
    /// fade on each on/off/dim command (fade-in when turning on/dimming, fade-out
    /// when turning off). DALI also fades any loaded color temperature with it.
    void set_fade_in_ms(uint32_t ms)  { m_fade_in_ms = ms; }
    void set_fade_out_ms(uint32_t ms) { m_fade_out_ms = ms; }
    uint32_t fade_in_ms() const  { return m_fade_in_ms; }
    uint32_t fade_out_ms() const { return m_fade_out_ms; }

    /// @brief DALI power-on / system-failure levels (0..254, or 255 = keep last
    /// level). Written to each device at discovery and re-applied when a device
    /// recovers (so a power cut doesn't snap lamps to full brightness).
    void set_power_on_level(uint8_t lvl) { m_power_on_level = lvl; }
    void set_system_failure_level(uint8_t lvl) { m_system_failure_level = lvl; }
    uint8_t power_on_level() const { return m_power_on_level; }
    uint8_t system_failure_level() const { return m_system_failure_level; }

    /// @brief Whether to create a per-lamp "Status" binary_sensor (problem device
    /// class: ON = lamp unavailable or reporting the DALI STATUS lampFailure bit,
    /// IEC 62386-102; OFF = OK).
    void set_expose_problem(bool v) { m_expose_problem = v; }
    bool expose_problem() const { return m_expose_problem; }

    /// @brief Whether to create a single "DALI Bus Online" connectivity sensor that
    /// goes off when no lamp answers for a few poll rounds (bus power lost / wiring).
    void set_expose_bus_status(bool v) { m_expose_bus_status = v; }
    bool expose_bus_status() const { return m_expose_bus_status; }

    /// @brief Whether to create software-only line/PSU diagnostic entities: lifetime
    /// "DALI Bus Errors"/"DALI Bus Down Events" counters, a "DALI Bus Disconnected"
    /// binary_sensor (RX stuck high = no PSU/physically disconnected), and (when
    /// `input_devices` is enabled) a "DALI Collisions" counter from the multi-master
    /// collision detector.
    void set_expose_bus_diagnostics(bool v) { m_expose_bus_diagnostics = v; }
    bool expose_bus_diagnostics() const { return m_expose_bus_diagnostics; }

    /// @brief Persist the discovered short-address inventory to flash, so on the next
    /// boot the light entities exist immediately (even if the bus is momentarily down
    /// at startup) and a device that was known but is now absent is tracked as a
    /// "missing" (unavailable) lamp rather than silently vanishing.
    void set_persist_inventory(bool v) { m_persist_inventory = v; }
    bool persist_inventory() const { return m_persist_inventory; }

    /// @brief Whether to auto-discover DALI group membership (QUERY GROUPS) and expose
    /// one optimistic "DALI Group N" light per active group.
    void set_expose_groups(bool v) { m_expose_groups = v; }
    bool expose_groups() const { return m_expose_groups; }

    /// @brief Whether to expose 16 "DALI Scene N" recall buttons + a scene number and
    /// store/clear buttons.
    void set_expose_scenes(bool v) { m_expose_scenes = v; }
    bool expose_scenes() const { return m_expose_scenes; }

    /// @brief Scene index (0..15) the Store/Clear scene buttons act on, set by the
    /// "DALI Scene Number" entity.
    void set_selected_scene(uint8_t s) { m_selected_scene_ = (s > 15) ? 15 : s; }
    uint8_t selected_scene() const { return m_selected_scene_; }

    /// @brief Short address (0..63) the Add/Remove Group buttons act on, set by the
    /// "DALI Group Target Address" entity.
    void set_group_target_address(uint8_t a) { m_group_target_addr_ = (a > ADDR_SHORT_MAX) ? ADDR_SHORT_MAX : a; }
    uint8_t group_target_address() const { return m_group_target_addr_; }

    /// @brief Group index (0..15) the Add/Remove Group buttons act on, set by the
    /// "DALI Group Number" entity.
    void set_group_number(uint8_t g) { m_group_number_ = (g > 15) ? 15 : g; }
    uint8_t group_number() const { return m_group_number_; }

    /// @brief Add/remove `addr` (a short address) to/from `group` (0..15). On add,
    /// also creates the "DALI Group N" light if it doesn't exist yet, so the new
    /// membership is immediately controllable. Updates the cached group-membership
    /// table read by the web dashboard.
    void add_to_group(uint8_t addr, uint8_t group);
    void remove_from_group(uint8_t addr, uint8_t group);

    /// @brief Add/remove the device at group_target_address() to/from group
    /// group_number(). Thin wrappers around add_to_group/remove_from_group used by
    /// the "DALI Add/Remove To Group" HA buttons.
    void add_target_to_group() { add_to_group(m_group_target_addr_, m_group_number_); }
    void remove_target_from_group() { remove_from_group(m_group_target_addr_, m_group_number_); }

    /// @brief Scene actions usable with any target address: ADDR_BROADCAST, a short
    /// address, or ADDR_GROUP|group. Used by the web dashboard's scene panel.
    enum class SceneAction { Recall, Store, Remove };
    void do_scene_action(uint8_t target_addr, uint8_t scene, SceneAction action);

    /// @brief Briefly blink the lamp at `addr` (alternating min/max brightness a
    /// few times, non-blocking via loop()) so the user can identify which physical
    /// lamp it is, then restore its original brightness. Used by the web dashboard.
    void identify_lamp(uint8_t addr);

    /// @brief Cached per-lamp info exposed to the web dashboard. All fields come from
    /// state already maintained by polling/discovery -- reading this never touches
    /// the bus, so it's safe to call from any context that runs on the main task.
    struct LampInfo {
        uint8_t addr;
        bool online;
        bool problem;
        bool on;
        uint8_t brightness_pct;     // 0-100
        bool has_color_temp;
        uint16_t color_temp_mireds; // valid only if has_color_temp
        uint16_t groups;            // bit N set => member of group N (0-15)
    };
    size_t lamp_count() const { return m_pollable_lights.size(); }
    LampInfo lamp_info(size_t index) const;

    /// @brief Whether the web dashboard is enabled, and which TCP port it listens on.
    void set_dashboard_enabled(bool v) { m_dashboard_enabled = v; }
    void set_dashboard_port(uint16_t port) { m_dashboard_port = port; }

    /// @brief Device-class string-table indices (registered in codegen). 0 = unset.
    /// Passed into configure_entity_() so Home Assistant renders the right
    /// semantics ("Connected"/"Disconnected", "Problem"/"OK").
    void set_dc_index_connectivity(uint8_t idx) { m_dc_idx_connectivity = idx; }
    void set_dc_index_problem(uint8_t idx) { m_dc_idx_problem = idx; }

    /// @brief Create + register a diagnostic "Status" binary_sensor for a lamp
    /// (ON = unavailable or lamp/gear failure, OFF = OK). Returns nullptr if
    /// status sensors are disabled.
    binary_sensor::BinarySensor* create_status_sensor(short_addr_t short_addr);

    /// @brief Create + register the single "DALI Bus Online" connectivity sensor.
    void create_bus_sensor();

    /// @brief Create + register the software-only line/PSU diagnostic entities
    /// (error/bus-down/collision counters, bus-disconnected sensor). No-op if
    /// `expose_bus_diagnostics` is false.
    void create_diag_sensors();

    /// @brief Register a light to be state-polled by the bus loop. Called by each
    /// DaliLight once it confirms a real (non-broadcast/group) device is present.
    void register_pollable_light(DaliLight* light) {
        m_pollable_lights.push_back(light);
        if (m_state_poll_interval_ms > 0) this->enable_loop();  // ensure loop() runs
    }

    /// @brief Optimistically apply `update` to the HA-published state of every
    /// individually-pollable lamp that is a member of `group` (0..15), per
    /// m_group_membership_. Called when a "DALI Group N" light's write_state()
    /// sends a command to the group address, so member lamps' entities reflect
    /// the change immediately instead of waiting for their next poll.
    void sync_group_member_states(uint8_t group, const GroupStateUpdate& update);

    DaliMaster dali;

public: // DaliPort
    void resetBus() override;
    void sendForwardFrame(uint8_t address, uint8_t data) override;
    uint8_t receiveBackwardFrame(unsigned long timeout_ms = DALI_BACKWARD_TIMEOUT_MS) override;

private:
    void writeBit(bool bit);
    /// @brief If `input_devices` is enabled, sample the RX pin during a half-bit
    /// where `writeBit()` is releasing the bus (per dali_tx::master_releasing) and
    /// bump the collision counter if another master is driving it low. half: 0 =
    /// first half-bit, 1 = second. No-op (and no extra delay) if input_devices is off.
    void check_collision_(bool bit, int half);
    void writeByte(uint8_t b);
    uint8_t readByte();

    /// @brief Return the lowest short address (0..63) not marked used, or 0xFF if
    /// none are free. Used to assign new devices into address gaps.
    short_addr_t lowest_free_address_(const bool* used) const {
        for (uint8_t a = 0; a <= ADDR_SHORT_MAX; a++) {
            if (!used[a]) return (short_addr_t) a;
        }
        return 0xFF;
    }

    void create_light_component(short_addr_t short_addr, uint32_t long_addr);
    /// @brief Create an optimistic "DALI Group N" light addressing (ADDR_GROUP | group).
    void create_group_light_component(uint8_t group);
    /// @brief Query group membership of every present device and create a group light
    /// for each active group. present_mask: bit a set => short address a is present.
    void create_group_lights_(uint64_t present_mask);
    /// @brief Non-destructively presence-scan addresses 0-63 and create a light
    /// entity for any device that doesn't already have one. Reads only; never
    /// reassigns addresses. Lets entities be rebuilt every boot without re-running
    /// the disruptive INITIALISE/RANDOMIZE address assignment.
    void create_entities_for_present_devices();
    /// @brief Advance the non-blocking identify blink sequence, if one is active.
    /// Called once per loop().
    void process_identify_();
    void create_discovery_button();
    void create_reboot_button();
    void create_fade_numbers();
    /// @brief Create the 16 scene-recall buttons + scene-number + store/clear buttons.
    void create_scene_controls();
    /// @brief Create the group-membership controls: target-address + group-number
    /// selectors, and Add/Remove buttons.
    void create_group_controls();

    /// @brief Fold one completed poll round's result into bus-health state. Marks the
    /// bus down after DALI_BUS_DOWN_ROUNDS all-NACK rounds; recovers on any response.
    void update_bus_health_(bool any_response);
    /// @brief Re-sync after the bus comes back: rebuild any missing entities. Each
    /// lamp re-applies its own recovery config on its unavailable->available edge.
    void on_bus_recovered_();

    /// @brief Increment + publish the "DALI Bus Errors" counter (a polled lamp did
    /// not respond).
    void note_poll_error_();
    /// @brief Increment + publish the "DALI Bus Down Events" counter (bus transitioned
    /// online -> offline).
    void note_bus_down_();
    /// @brief Publish the "DALI Bus Disconnected" binary_sensor if `disconnected`
    /// differs from the last published value (RX stuck high at the start of
    /// receiveBackwardFrame's wait = no PSU/physically disconnected bus).
    void note_disconnected_(bool disconnected);
    /// @brief Increment + publish the "DALI Collisions" counter (another master drove
    /// the bus while we were releasing it during writeBit()).
    void note_collision_();

    /// @brief Load the persisted inventory bitmask and create a light entity for each
    /// known short address (before discovery), so entities exist at API connect even
    /// if the bus is momentarily unreachable at boot.
    void restore_inventory_();
    /// @brief Persist the set of short addresses currently present on the bus.
    void save_inventory_(uint64_t present_mask);

    InternalGPIOPin* m_rxPin;
    GPIOPin* m_txPin;

    DaliInputListener m_input_listener;
    bool m_input_devices = false;

    bool m_discovery = false;
    uint32_t discovery_start_ms_ = 0;  // millis() when the deferred-discovery wait began
    DaliInitMode m_initialize_addresses = DaliInitMode::DiscoverOnly;
    uint32_t m_addresses[ADDR_SHORT_MAX+1] = {0};

    // Dynamic lights created during discovery are not in ESPHome's looping_components_
    // (that list is fixed at compile time). We drive their loop() manually.
    std::vector<light::LightState*> m_dynamic_lights;

    // Lights (static + dynamic) we periodically poll so HA reflects external state.
    std::vector<DaliLight*> m_pollable_lights;
    uint32_t m_state_poll_interval_ms = 15000;
    uint32_t m_last_poll_ms = 0;
    size_t m_poll_index = 0;

    // Bus-health tracking (phase 4). A poll "round" is one full pass over
    // m_pollable_lights; if no lamp answers for a few consecutive rounds the bus is
    // treated as down (power/wiring fault) and a single connectivity sensor reports it.
    binary_sensor::BinarySensor* m_bus_sensor_ = nullptr;
    bool m_bus_online_ = true;
    bool m_round_any_response_ = false;
    uint8_t m_bus_down_rounds_ = 0;

    // Inventory persistence (phase 5): a 64-bit mask of known short addresses saved
    // to flash so entities survive a boot where the bus is briefly unreachable, and
    // removed lamps are tracked as "missing" rather than silently disappearing.
    bool m_persist_inventory = true;
    ESPPreferenceObject m_inventory_pref_;

    // Group lights (auto-discovered). m_group_created_[g] guards against creating a
    // "DALI Group g" light twice across discovery passes.
    bool m_expose_groups = true;
    bool m_group_created_[16] = { false };
    // DaliLight* for each created "DALI Group N" light, used to read/publish a
    // group's own color temperature (scene CT capture/recall). nullptr if not created.
    DaliLight* m_group_lights_[16] = { nullptr };

    // Scene controls.
    bool m_expose_scenes = true;
    uint8_t m_selected_scene_ = 0;

    // Software per-scene color-temperature table (DALI Part 102 scenes only store
    // brightness). Indexed by scene number; only captured/recalled for group or
    // individual-lamp targets (not ADDR_BROADCAST, which the HA "DALI Scene N"
    // buttons use). RAM-only -- resets on reboot.
    optional<uint16_t> m_scene_color_temp_[16] = {};

    // Group-membership controls (Add/Remove the target address to/from a group).
    uint8_t m_group_target_addr_ = 0;
    uint8_t m_group_number_ = 0;

    // Per-device group membership cache (bit N set => device is in group N),
    // populated during create_group_lights_() and kept up to date by
    // add_to_group()/remove_from_group(). Read by the web dashboard.
    uint16_t m_group_membership_[ADDR_SHORT_MAX+1] = {0};

    // Non-blocking blink sequence for identify_lamp(), advanced from loop().
    struct IdentifyState {
        bool active = false;
        uint8_t addr = 0;
        uint8_t step = 0;
        uint32_t next_at_ms = 0;
        uint8_t original_level = 0;
    } m_identify_;

    // Web dashboard (lamps/groups/scenes). Only constructed if enabled. Starting
    // AsyncWebServer::begin() before the network stack is up aborts (httpd_start
    // hits an unmet FreeRTOS assertion), so the actual `begin()` call is deferred
    // from setup() to the first loop() iteration where network::is_connected().
    bool m_dashboard_enabled = false;
    uint16_t m_dashboard_port = 8080;
#ifdef DALI_WEB_DASHBOARD_ENABLED
    DaliWebDashboard* m_dashboard_ = nullptr;
    bool m_dashboard_started_ = false;
#endif

    // Fade in/out times in milliseconds (runtime-adjustable via HA number entities).
    uint32_t m_fade_in_ms = 1000;
    uint32_t m_fade_out_ms = 1000;

    // Recovery config: 255 = "keep last level" (sensible default that avoids a
    // power cut blasting lamps to full brightness).
    uint8_t m_power_on_level = 255;
    uint8_t m_system_failure_level = 255;
    bool m_expose_problem = true;
    bool m_expose_bus_status = true;

    // Software-only line/PSU diagnostics (phase 7).
    bool m_expose_bus_diagnostics = true;
    sensor::Sensor* m_error_sensor_ = nullptr;
    sensor::Sensor* m_bus_down_sensor_ = nullptr;
    sensor::Sensor* m_collision_sensor_ = nullptr;
    binary_sensor::BinarySensor* m_disconnected_sensor_ = nullptr;
    uint32_t m_error_count_ = 0;
    uint32_t m_bus_down_count_ = 0;
    uint32_t m_collision_count_ = 0;
    bool m_disconnected_ = false;

    // Device-class string-table indices supplied by codegen (0 = unset).
    uint8_t m_dc_idx_connectivity = 0;
    uint8_t m_dc_idx_problem = 0;
};

}  // namespace dali
}  // namespace esphome
