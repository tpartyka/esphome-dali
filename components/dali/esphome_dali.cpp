#include <esphome.h>
#include <esp_task_wdt.h>
#include <cmath>
#include <cstring>
#ifdef DALI_WEB_DASHBOARD_ENABLED
#include "esphome/components/network/util.h"
#endif
#include "esphome_dali.h"
#include "esphome_dali_light.h"
#include "port.h"
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#include "dali_tx_collision.h"

//static const char *const TAG = "dali";
static const bool DEBUG_LOG_RXTX = true; // NOTE: Will probably trigger WDT

using namespace esphome;
using namespace dali;

namespace {

class AppRegistrationAccessor : public esphome::Application {
public:
    using Application::register_component_;
};

// RAII guard that suppresses the passive input listener while the master drives the
// bus (TX) or reads a reply (RX), and *always* un-suppresses on scope exit — even on
// an early return — so a future code path can never leave the listener wedged off.
struct ListenerSuppressGuard {
    DaliInputListener* listener;
    bool active;
    ListenerSuppressGuard(DaliInputListener* l, bool active) : listener(l), active(active) {
        if (active) listener->set_suppressed(true);
    }
    ~ListenerSuppressGuard() {
        if (active) listener->set_suppressed(false);
    }
};

class DynamicDaliLightState : public esphome::light::LightState {
public:
    using esphome::light::LightState::LightState;

    void configure_dynamic_entity(const char* name, const char* object_id, bool disabled_by_default) {
        uint32_t entity_fields = (static_cast<uint32_t>(disabled_by_default) << esphome::ENTITY_FIELD_DISABLED_BY_DEFAULT_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }
};

#ifdef USE_BUTTON
// Auto-created (no YAML needed) button in the Home Assistant "Configuration"
// section. Pressing it logs the short addresses of already-assigned devices and
// then assigns addresses to any unassigned (new) lamps on the bus.
class DaliDiscoveryButton : public esphome::button::Button, public esphome::Component {
public:
    explicit DaliDiscoveryButton(DaliBusComponent* parent) : parent_(parent) {}

    void configure_dynamic_entity(const char* name, const char* object_id, esphome::EntityCategory category) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(category) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }

protected:
    void press_action() override {
        this->parent_->scan_and_assign();
    }

    DaliBusComponent* parent_;
};

// Auto-created (no YAML needed) "Reboot" button next to the discovery button.
// Useful because newly discovered lights only appear in Home Assistant after a reboot.
class DaliRebootButton : public esphome::button::Button, public esphome::Component {
public:
    void configure_dynamic_entity(const char* name, const char* object_id, esphome::EntityCategory category) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(category) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }

protected:
    void press_action() override {
        ESP_LOGI("dali", "Rebooting device");
        esphome::delay(100);  // let pending traffic settle
        esphome::App.safe_reboot();
    }
};

// Auto-created (no YAML) "DALI Scene N" button. Pressing it broadcasts GO TO SCENE N,
// so every control gear fades to the level it has stored for that scene. GO TO SCENE
// is a level command (sent once per IEC 62386-102), handled by sendControlCommand().
class DaliSceneRecallButton : public esphome::button::Button, public esphome::Component {
public:
    DaliSceneRecallButton(DaliBusComponent* parent, uint8_t scene) : parent_(parent), scene_(scene) {}

    void configure_dynamic_entity(const char* name, const char* object_id) {
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), 0);
    }

protected:
    void press_action() override {
        this->parent_->dali.scene.goToScene(ADDR_BROADCAST, scene_);
    }

    DaliBusComponent* parent_;
    uint8_t scene_;
};

// Auto-created (no YAML) "Store Current As Scene" / "Clear Scene" config buttons. They
// act on the scene index selected by the "DALI Scene Number" entity, broadcast to all
// gears: store snapshots each gear's current level into that scene; clear removes it.
class DaliSceneEditButton : public esphome::button::Button, public esphome::Component {
public:
    DaliSceneEditButton(DaliBusComponent* parent, bool clear) : parent_(parent), clear_(clear) {}

    void configure_dynamic_entity(const char* name, const char* object_id) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(esphome::ENTITY_CATEGORY_CONFIG) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }

protected:
    void press_action() override {
        uint8_t scene = this->parent_->selected_scene();
        if (this->clear_) this->parent_->dali.scene.removeScene(ADDR_BROADCAST, scene);
        else              this->parent_->dali.scene.storeScene(ADDR_BROADCAST, scene);
    }

    DaliBusComponent* parent_;
    bool clear_;
};

// Auto-created (no YAML) "Add to Group" / "Remove from Group" config buttons. They
// act on the device selected by "DALI Group Target Address" and the group selected
// by "DALI Group Number".
class DaliGroupMembershipButton : public esphome::button::Button, public esphome::Component {
public:
    DaliGroupMembershipButton(DaliBusComponent* parent, bool add) : parent_(parent), add_(add) {}

    void configure_dynamic_entity(const char* name, const char* object_id) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(esphome::ENTITY_CATEGORY_CONFIG) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }

protected:
    void press_action() override {
        if (this->add_) this->parent_->add_target_to_group();
        else             this->parent_->remove_target_from_group();
    }

    DaliBusComponent* parent_;
    bool add_;
};
#endif  // USE_BUTTON

#ifdef USE_NUMBER
// Auto-created (no YAML) "Fade In/Out Time" numbers in the HA "Configuration"
// section. Changing one updates the bus's fade time (in seconds); the value is
// applied to the device's hardware fade on the next on/off/dim command.
class DaliFadeNumber : public esphome::number::Number, public esphome::Component {
public:
    DaliFadeNumber(DaliBusComponent* parent, bool is_out) : parent_(parent), is_out_(is_out) {}

    void configure_dynamic_entity(const char* name, const char* object_id) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(esphome::ENTITY_CATEGORY_CONFIG) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }

protected:
    void control(float value) override {
        if (this->is_out_) this->parent_->set_fade_out_ms((uint32_t) value);
        else               this->parent_->set_fade_in_ms((uint32_t) value);
        this->publish_state(value);
    }

    DaliBusComponent* parent_;
    bool is_out_;
};

// Auto-created (no YAML) "DALI Scene Number" (0..15) selecting which scene the Store /
// Clear buttons act on. Just records the selection on the bus component.
class DaliSceneNumber : public esphome::number::Number, public esphome::Component {
public:
    explicit DaliSceneNumber(DaliBusComponent* parent) : parent_(parent) {}

    void configure_dynamic_entity(const char* name, const char* object_id) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(esphome::ENTITY_CATEGORY_CONFIG) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }

protected:
    void control(float value) override {
        this->parent_->set_selected_scene((uint8_t) value);
        this->publish_state(value);
    }

    DaliBusComponent* parent_;
};

// Auto-created (no YAML) "DALI Group Target Address" (0..63) and "DALI Group Number"
// (0..15) numbers, selecting which device and group the Add/Remove Group buttons
// act on.
class DaliGroupSelectNumber : public esphome::number::Number, public esphome::Component {
public:
    DaliGroupSelectNumber(DaliBusComponent* parent, bool is_group) : parent_(parent), is_group_(is_group) {}

    void configure_dynamic_entity(const char* name, const char* object_id) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(esphome::ENTITY_CATEGORY_CONFIG) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }

protected:
    void control(float value) override {
        if (this->is_group_) this->parent_->set_group_number((uint8_t) value);
        else                 this->parent_->set_group_target_address((uint8_t) value);
        this->publish_state(value);
    }

    DaliBusComponent* parent_;
    bool is_group_;
};
#endif  // USE_NUMBER

#ifdef USE_BINARY_SENSOR
// Auto-created (no YAML) diagnostic binary_sensor per lamp ("online", "problem").
// The bus publishes to it from the state poller. The optional device-class index
// (registered in codegen, see __init__.py) lets Home Assistant render the proper
// semantics, e.g. connectivity -> "Connected"/"Disconnected", problem -> "Problem"/"OK".
class DaliDiagBinarySensor : public esphome::binary_sensor::BinarySensor, public esphome::Component {
public:
    void configure_dynamic_entity(const char* name, const char* object_id, uint8_t device_class_index = 0) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(esphome::ENTITY_CATEGORY_DIAGNOSTIC) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
#ifdef USE_ENTITY_DEVICE_CLASS
        entity_fields |= (static_cast<uint32_t>(device_class_index) << esphome::ENTITY_FIELD_DC_SHIFT);
#else
        (void) device_class_index;
#endif
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
    }
};
#endif  // USE_BINARY_SENSOR

#ifdef USE_SENSOR
// Auto-created (no YAML) diagnostic sensor for lifetime bus-health counters
// ("DALI Bus Errors", "DALI Bus Down Events", "DALI Collisions").
class DaliDiagSensor : public esphome::sensor::Sensor, public esphome::Component {
public:
    void configure_dynamic_entity(const char* name, const char* object_id) {
        uint32_t entity_fields =
            (static_cast<uint32_t>(esphome::ENTITY_CATEGORY_DIAGNOSTIC) << esphome::ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
        this->configure_entity_(name, esphome::fnv1_hash_object_id(object_id, std::strlen(object_id)), entity_fields);
        this->set_state_class(esphome::sensor::STATE_CLASS_TOTAL_INCREASING);
        this->set_accuracy_decimals(0);
    }
};
#endif  // USE_SENSOR

}  // namespace

void DaliBusComponent::setup() {
    DALI_LOGD("DALI bus setup start...");
    m_txPin->pin_mode(gpio::Flags::FLAG_OUTPUT);
    m_rxPin->pin_mode(gpio::Flags::FLAG_INPUT);

    // Allow bus to stabilize before sending any commands
    delay(500);

    DALI_LOGI("DALI bus ready");

    // Always expose the "Run DALI Discovery" and "Reboot" buttons (no YAML required).
    // They must be registered here in setup(), before the API client connects,
    // otherwise Home Assistant won't list them.
    create_discovery_button();
    create_reboot_button();
    create_fade_numbers();
    create_scene_controls();
    create_group_controls();
    create_bus_sensor();
    create_diag_sensors();

    // Passive input-device listener (Part 103 push buttons / sensors).
    if (m_input_devices) {
        m_input_listener.input_listener_setup(m_rxPin);
    }

    // Discovery must also run in setup(): dynamically created light entities are only
    // advertised to Home Assistant in the entity list it requests once at connect time.
    // (Running discovery later, e.g. via the button, won't surface NEW lights in HA
    // until the next reboot.)
    if (m_discovery) {
        // Recreate entities for previously-known lamps first, so Home Assistant sees
        // them at connect even if the bus is momentarily unreachable right now.
        restore_inventory_();
        run_discovery();
    } else if (!m_input_devices) {
        // No discovery and no listener -> nothing for loop() to drive.
        this->disable_loop();
    }

    // The input listener needs loop() running even if discovery disabled it.
    if (m_input_devices) {
        this->enable_loop();
    }

    // Allocate the web dashboard, but defer AsyncWebServer::begin() to loop() --
    // calling it this early (before the network stack is up) aborts inside
    // httpd_start(). Force loop() on regardless of the decisions above, so the
    // dashboard gets started and queued actions get drained even if discovery
    // found nothing to poll.
    if (m_dashboard_enabled) {
#ifdef DALI_WEB_DASHBOARD_ENABLED
        m_dashboard_ = new DaliWebDashboard {};
        this->enable_loop();
#else
        DALI_LOGE("expose_dashboard is set but the web dashboard is only supported on ESP32");
#endif
    }
}

void DaliBusComponent::run_discovery() {
    this->run_discovery(this->m_initialize_addresses);
}

void DaliBusComponent::scan_and_assign() {
    // 1) Report the short addresses of devices already on the bus (non-destructive).
    DALI_LOGI("Scanning bus for assigned short addresses (0-%d)...", ADDR_SHORT_MAX);
    uint8_t found = 0;
    for (uint8_t addr = 0; addr <= ADDR_SHORT_MAX; addr++) {
        delay(1); // yield to ESP stack
        esp_task_wdt_reset();
        if (dali.isDevicePresent(addr)) {
            found++;
            DALI_LOGI("  Address %u: present", addr);
        }
    }
    DALI_LOGI("Found %u assigned device(s) on the bus.", found);

    // 2) Assign addresses to any unassigned (new) lamps. Already-addressed devices
    // are left untouched (they do not enter initialization mode under ASSIGN_UNINITIALIZED).
    run_discovery(DaliInitMode::InitializeUnassigned);
}

void DaliBusComponent::run_discovery(DaliInitMode mode) {
    this->discovery_start_ms_ = millis();

    // Optional: reset devices on the bus so we are in a known-good state.
    // Can help if devices are not responding to anything.
    if (false) {
        this->resetBus();
        esp_task_wdt_reset();
    }

    if (dali.bus_manager.isControlGearPresent()) {
        DALI_LOGD("Detected control gear on bus");
    } else {
        DALI_LOGE("No DALI control gear detected on bus!");
        // Keep loop() running if we have restored/static lights to poll (so the bus
        // can be detected as recovered) or an input listener.
        if (!m_input_devices && m_pollable_lights.empty()) this->disable_loop();
        return; // Unlikely to get anything from discovery if no one responds to this
    }

    // Map which short addresses are already in use *before* initialization, by
    // probing the bus directly (addressed devices still answer normal queries).
    // New devices are then assigned the lowest free address, so adding a lamp to
    // an existing set never collides with an in-use address.
    bool addr_used[ADDR_SHORT_MAX + 1] = { false };
    if (mode != DaliInitMode::DiscoverOnly) {
        DALI_LOGD("Mapping in-use short addresses...");
        for (uint8_t a = 0; a <= ADDR_SHORT_MAX; a++) {
            delay(1);
            esp_task_wdt_reset();
            if (dali.isDevicePresent(a)) addr_used[a] = true;
        }
    }

    if (mode != DaliInitMode::DiscoverOnly) {
        if (mode == DaliInitMode::InitializeAll) {
            DALI_LOGI("Randomizing addresses for *all* DALI devices");
            dali.bus_manager.initialize(ASSIGN_ALL);
        }
        else if (mode == DaliInitMode::InitializeUnassigned) {
            // Only randomize devices without an assigned short address
            DALI_LOGI("Randomizing addresses for unassigned DALI devices");
            dali.bus_manager.initialize(ASSIGN_UNINITIALIZED);
        }

        dali.bus_manager.randomize();

        // DALI spec requires minimum 100ms after RANDOMIZE for devices to generate random address.
        // Use 500ms to be safe with slower devices.
        // NOTE: Do NOT call terminate() here - devices must stay in initialization mode
        // for the binary search (startAddressScan) to work correctly.
        delay(500);
    }

    DALI_LOGI("Begin device discovery...");
    // Pass true = devices already in initialization mode from INITIALIZE command above.
    // Do NOT re-send initialize(ASSIGN_ALL) which would override ASSIGN_UNINITIALIZED.
    dali.bus_manager.startAddressScan(true);

    // Keep track of short addresses to detect duplicates
    bool duplicate_detected = false;
    bool is_discovered[ADDR_SHORT_MAX+1];
    for (int i = 0; i <= ADDR_SHORT_MAX; i++) {
        is_discovered[i] = false;
    }

    short_addr_t short_addr = 0xFF;
    uint32_t long_addr = 0;
    while (dali.bus_manager.findNextAddress(short_addr, long_addr)) {
        delay(1); // yield to ESP stack
        esp_task_wdt_reset();

        if (short_addr <= ADDR_SHORT_MAX) {
            DALI_LOGI("  Device %.6x @ %.2x", long_addr, short_addr);

            // Duplicate detection
            if (is_discovered[short_addr]) {
                if (mode == DaliInitMode::DiscoverOnly) {
                    DALI_LOGW("  WARNING: Duplicate short address detected!");
                    duplicate_detected = true;
                }
                else {
                    // Duplicate: reassign this device to the lowest free address.
                    short_addr_t free_addr = lowest_free_address_(addr_used);
                    if (free_addr == 0xFF) {
                        DALI_LOGE("  No free short address available (bus full)");
                        dali.bus_manager.withdrawCurrentDevice();
                        short_addr = 0xFF;
                        continue;
                    }
                    short_addr = free_addr;
                    addr_used[short_addr] = true;
                    DALI_LOGD("  Duplicate short address detected, reassigning to: %.2x", short_addr);

                    if (!dali.bus_manager.programShortAddress(short_addr)) {
                        DALI_LOGE("  Could not program short address");
                        dali.bus_manager.withdrawCurrentDevice();
                        short_addr = 0xFF;
                        continue;
                    }
                }
            }
            else {
                is_discovered[short_addr] = true;
            }

            // Withdraw after address is confirmed (spec order: find → program → withdraw)
            dali.bus_manager.withdrawCurrentDevice();

            // Dynamic component creation (if not defined in YAML)
            if (m_addresses[short_addr]) {
                DALI_LOGD("  Ignoring, already defined");
            }
            else {
                m_addresses[short_addr] = long_addr;
                create_light_component(short_addr, long_addr);
            }
        }
        else if (short_addr == 0xFF) {
            if (mode == DaliInitMode::DiscoverOnly) {
                DALI_LOGI("  Device %.6x @ --", long_addr);
                DALI_LOGW("  No short address assigned!");
                dali.bus_manager.withdrawCurrentDevice();
                continue;
            }
            else {
                // Assign the lowest free short address, so new lamps slot into the
                // gaps after any already-addressed devices instead of colliding.
                short_addr_t free_addr = lowest_free_address_(addr_used);
                if (free_addr == 0xFF) {
                    DALI_LOGE("  No free short address available (bus full)");
                    dali.bus_manager.withdrawCurrentDevice();
                    short_addr = 0xFF;
                    continue;
                }
                short_addr = free_addr;
                addr_used[short_addr] = true;
                DALI_LOGI("  Assigning short address: %.2x", short_addr);

                if (!dali.bus_manager.programShortAddress(short_addr)) {
                    DALI_LOGE("  Could not program short address");
                    dali.bus_manager.withdrawCurrentDevice();
                    short_addr = 0xFF;
                    continue;
                }

                // Withdraw after successful address programming
                // (spec order: find → program → withdraw)
                dali.bus_manager.withdrawCurrentDevice();

                DALI_LOGI("  Device %.6x @ %.2x", long_addr, short_addr);

                is_discovered[short_addr] = true;

                // Dynamic component creation, same as the already-addressed branch.
                // Without this, brand-new devices get an address programmed but no
                // light entity is ever created.
                if (m_addresses[short_addr]) {
                    DALI_LOGD("  Ignoring, already defined");
                }
                else {
                    m_addresses[short_addr] = long_addr;
                    create_light_component(short_addr, long_addr);
                }
            }
        }
    }

    DALI_LOGD("No more devices found!");
    dali.bus_manager.endAddressScan();

    if (duplicate_detected) {
        DALI_LOGW("Duplicate short addresses detected on the bus!");
        DALI_LOGW("  Devices may report inconsistent capabilities.");
        DALI_LOGW("  You should fix your address assignments.");
    }

    // Create entities for every device already on the bus, not just the ones the
    // init-mode binary search returned. Already-addressed lamps don't enter
    // initialization mode (so findNextAddress never returns them) — without this,
    // initialize_addresses: none/unassigned would create no entities on reboot and
    // the lamps would vanish from Home Assistant. This is non-destructive: it only
    // reads, it never reassigns addresses.
    create_entities_for_present_devices();

    DALI_LOGI("DALI discovery finished in %u ms", (unsigned) (millis() - this->discovery_start_ms_));

    // If no dynamic lights were discovered, disable loop() to avoid unnecessary CPU cycles.
    // (Keep it running if the input-device listener needs it.)
    if (m_dynamic_lights.empty() && !m_input_devices) {
        this->disable_loop();
    }
}

void DaliBusComponent::create_entities_for_present_devices() {
    DALI_LOGI("Scanning bus for addressed devices to create entities...");
    uint8_t created = 0;
    uint64_t present_mask = 0;
    for (uint8_t addr = 0; addr <= ADDR_SHORT_MAX; addr++) {
        delay(1);  // yield to ESP stack
        esp_task_wdt_reset();
        bool present = dali.isDevicePresent(addr);
        if (present) present_mask |= (1ULL << addr);
        if (m_addresses[addr]) continue;  // already has an entity (discovery, static YAML, or restored)
        if (present) {
            m_addresses[addr] = 0xFFFFFF;  // mark created (long address unknown here)
            create_light_component(addr, 0xFFFFFF);
            created++;
        }
    }
    DALI_LOGI("Created %u light entit%s for already-addressed device(s)", created, created == 1 ? "y" : "ies");

    // Persist the truly-present set: a device that's gone will not be pre-created on
    // the next boot (it drops out of NVS), while one present now is restored instantly.
    save_inventory_(present_mask);

    // Auto-discover group membership and expose a light per active group.
    create_group_lights_(present_mask);
}

void DaliBusComponent::restore_inventory_() {
    if (!m_persist_inventory) return;
    m_inventory_pref_ = global_preferences->make_preference<uint64_t>(0xDA111101u);
    uint64_t mask = 0;
    if (!m_inventory_pref_.load(&mask) || mask == 0) {
        DALI_LOGD("No saved DALI inventory to restore");
        return;
    }
    uint8_t restored = 0;
    for (uint8_t addr = 0; addr <= ADDR_SHORT_MAX; addr++) {
        if (!(mask & (1ULL << addr))) continue;
        if (m_addresses[addr]) continue;  // already created (static YAML)
        m_addresses[addr] = 0xFFFFFF;
        create_light_component(addr, 0xFFFFFF);  // setup_state() tracks it as missing if absent now
        restored++;
    }
    DALI_LOGI("Restored %u light entit%s from saved inventory", restored, restored == 1 ? "y" : "ies");
}

void DaliBusComponent::save_inventory_(uint64_t present_mask) {
    if (!m_persist_inventory) return;
    // make_preference may not have run yet if restore was skipped on first boot.
    if (m_inventory_pref_.save(&present_mask)) {
        DALI_LOGD("Saved DALI inventory mask 0x%08x%08x",
                  (unsigned) (present_mask >> 32), (unsigned) (present_mask & 0xFFFFFFFF));
    }
}

void DaliBusComponent::create_light_component(short_addr_t short_addr, uint32_t long_addr) {
#ifdef USE_LIGHT
    DaliLight* dali_light = new DaliLight { this };
    dali_light->set_address(short_addr);

    const int MAX_STR_LEN = 20;
    char* name = new char[MAX_STR_LEN];
    char* id = new char[MAX_STR_LEN];
    snprintf(name, MAX_STR_LEN, "DALI Light %d", short_addr);
    // Identify the entity by the STABLE short address, not the long address. With
    // initialize_addresses: all the long (random) address is regenerated on every
    // discovery, so a long-address-based object id would change every boot and Home
    // Assistant would lose track of the entity. Short addresses persist.
    snprintf(id, MAX_STR_LEN, "dali_light_%d", short_addr);
    // NOTE: Not freeing these strings, they will be owned by LightState.

    auto* light_state = new DynamicDaliLightState { dali_light };
    // set_component_source is codegen-only since ESPHome 2026.4 (uint8_t index into PROGMEM table);
    // dynamic components cannot participate, and it was cosmetic (log source tagging) only.
    light_state->configure_dynamic_entity(name, id, false);
    App.register_light(light_state);
    static_cast<AppRegistrationAccessor&>(App).register_component_(light_state);

    light_state->set_restore_mode(light::LIGHT_RESTORE_DEFAULT_ON);
    light_state->add_effects({});

    // Initialize the DaliLight with the LightState:
    // queries device capabilities (min/max level, color temperature support)
    dali_light->setup_state(light_state);

    // Track for manual loop() driving — dynamic lights are not in ESPHome's
    // compile-time looping_components_ list so their loop() won't be called automatically.
    if (m_dynamic_lights.empty()) {
        this->enable_loop();  // only enable loop when we have lights to drive
    }
    m_dynamic_lights.push_back(light_state);

    DALI_LOGI("Created light component '%s' (%s)", name, id);
#else
    DALI_LOGE("Not compiled with light component. Add `light:` to YAML.");
#endif
}

void DaliBusComponent::create_group_light_component(uint8_t group) {
#ifdef USE_LIGHT
    // Reuse DaliLight with a group address. setup_state() excludes group addresses from
    // capability queries and polling, so the entity is optimistic (publishes what we
    // command) — a DALI group has no single queryable state. write_state() sends the
    // fade + DAPC level to (ADDR_GROUP | group), reaching every member.
    DaliLight* gl = new DaliLight { this };
    gl->set_address((short_addr_t) (ADDR_GROUP | (group & 0x0F)));

    // Group addresses skip per-device capability detection (no single queryable
    // state), so force color-temperature support using the default mired range.
    // This is spec-legal: setColorTemperature() addresses the group via
    // ENABLE_DEVICE_TYPE(COLOR) + an extended command, which only COLOR-type
    // members of the group act on (IEC 62386-209).
    gl->set_color_mode(DaliColorMode::COLOR_TEMPERATURE);

    const int MAX_STR_LEN = 20;
    char* name = new char[MAX_STR_LEN];
    char* id = new char[MAX_STR_LEN];
    snprintf(name, MAX_STR_LEN, "DALI Group %d", group);
    snprintf(id, MAX_STR_LEN, "dali_group_%d", group);

    auto* light_state = new DynamicDaliLightState { gl };
    light_state->configure_dynamic_entity(name, id, false);
    App.register_light(light_state);
    static_cast<AppRegistrationAccessor&>(App).register_component_(light_state);

    light_state->set_restore_mode(light::LIGHT_RESTORE_DEFAULT_ON);
    light_state->add_effects({});

    gl->setup_state(light_state);

    if (m_dynamic_lights.empty()) {
        this->enable_loop();
    }
    m_dynamic_lights.push_back(light_state);
    m_group_lights_[group & 0x0F] = gl;

    DALI_LOGI("Created group light component '%s' (%s)", name, id);
#else
    DALI_LOGE("Not compiled with light component.");
#endif
}

void DaliBusComponent::create_group_lights_(uint64_t present_mask) {
    if (!m_expose_groups) return;

    // Union the group membership of every present device, caching each device's
    // membership for the web dashboard.
    uint16_t active_groups = 0;
    for (uint8_t addr = 0; addr <= ADDR_SHORT_MAX; addr++) {
        if (!(present_mask & (1ULL << addr))) continue;
        delay(1);  // yield to ESP stack
        esp_task_wdt_reset();
        uint16_t groups = dali.scene.queryGroups(addr);
        m_group_membership_[addr] = groups;
        active_groups |= groups;
    }

    uint8_t created = 0;
    for (uint8_t g = 0; g < 16; g++) {
        if (!(active_groups & (1u << g))) continue;
        if (m_group_created_[g]) continue;  // already have a light for this group
        m_group_created_[g] = true;
        create_group_light_component(g);
        created++;
    }
    DALI_LOGI("Created %u group light entit%s", created, created == 1 ? "y" : "ies");
}

void DaliBusComponent::create_discovery_button() {
#ifdef USE_BUTTON
    auto* btn = new DaliDiscoveryButton { this };
    btn->configure_dynamic_entity("Run DALI Discovery", "dali_run_discovery", ENTITY_CATEGORY_CONFIG);
    App.register_button(btn);
    static_cast<AppRegistrationAccessor&>(App).register_component_(btn);

    DALI_LOGI("Created DALI discovery button");
#endif
}

void DaliBusComponent::create_reboot_button() {
#ifdef USE_BUTTON
    auto* btn = new DaliRebootButton {};
    btn->configure_dynamic_entity("Reboot", "dali_reboot", ENTITY_CATEGORY_CONFIG);
    App.register_button(btn);
    static_cast<AppRegistrationAccessor&>(App).register_component_(btn);

    DALI_LOGI("Created DALI reboot button");
#endif
}

void DaliBusComponent::create_fade_numbers() {
#ifdef USE_NUMBER
    auto make = [this](const char* name, const char* id, bool is_out, uint32_t initial) {
        auto* n = new DaliFadeNumber { this, is_out };
        n->traits.set_min_value(0.0f);
        n->traits.set_max_value(16000.0f);  // ms; snapped to the nearest DALI step internally
        n->traits.set_step(100.0f);
        n->configure_dynamic_entity(name, id);
        App.register_number(n);
        static_cast<AppRegistrationAccessor&>(App).register_component_(n);
        n->publish_state((float) initial);
    };
    make("DALI Fade In Time", "dali_fade_in_time", false, m_fade_in_ms);
    make("DALI Fade Out Time", "dali_fade_out_time", true, m_fade_out_ms);

    DALI_LOGI("Created DALI fade in/out time numbers");
#endif
}

void DaliBusComponent::create_scene_controls() {
#if defined(USE_BUTTON) && defined(USE_NUMBER)
    if (!m_expose_scenes) return;

    // 16 broadcast scene-recall buttons.
    const int MAX_STR_LEN = 18;
    for (uint8_t s = 0; s < 16; s++) {
        char* name = new char[MAX_STR_LEN];
        char* id = new char[MAX_STR_LEN];
        snprintf(name, MAX_STR_LEN, "DALI Scene %d", s);
        snprintf(id, MAX_STR_LEN, "dali_scene_%d", s);
        auto* btn = new DaliSceneRecallButton { this, s };
        btn->configure_dynamic_entity(name, id);
        App.register_button(btn);
        static_cast<AppRegistrationAccessor&>(App).register_component_(btn);
    }

    // Scene index selector for the store/clear buttons.
    auto* num = new DaliSceneNumber { this };
    num->traits.set_min_value(0.0f);
    num->traits.set_max_value(15.0f);
    num->traits.set_step(1.0f);
    num->configure_dynamic_entity("DALI Scene Number", "dali_scene_number");
    App.register_number(num);
    static_cast<AppRegistrationAccessor&>(App).register_component_(num);
    num->publish_state(0.0f);

    // Store-current / clear buttons (act on the selected scene index, broadcast).
    auto* store = new DaliSceneEditButton { this, false };
    store->configure_dynamic_entity("DALI Store Current As Scene", "dali_scene_store");
    App.register_button(store);
    static_cast<AppRegistrationAccessor&>(App).register_component_(store);

    auto* clear = new DaliSceneEditButton { this, true };
    clear->configure_dynamic_entity("DALI Clear Scene", "dali_scene_clear");
    App.register_button(clear);
    static_cast<AppRegistrationAccessor&>(App).register_component_(clear);

    DALI_LOGI("Created DALI scene controls (16 recall + number + store/clear)");
#endif
}

void DaliBusComponent::create_group_controls() {
#if defined(USE_BUTTON) && defined(USE_NUMBER)
    if (!m_expose_groups) return;

    // Target device address selector (0..63).
    auto* addr_num = new DaliGroupSelectNumber { this, /*is_group=*/false };
    addr_num->traits.set_min_value(0.0f);
    addr_num->traits.set_max_value((float) ADDR_SHORT_MAX);
    addr_num->traits.set_step(1.0f);
    addr_num->configure_dynamic_entity("DALI Group Target Address", "dali_group_target_address");
    App.register_number(addr_num);
    static_cast<AppRegistrationAccessor&>(App).register_component_(addr_num);
    addr_num->publish_state(0.0f);

    // Target group selector (0..15).
    auto* group_num = new DaliGroupSelectNumber { this, /*is_group=*/true };
    group_num->traits.set_min_value(0.0f);
    group_num->traits.set_max_value(15.0f);
    group_num->traits.set_step(1.0f);
    group_num->configure_dynamic_entity("DALI Group Number", "dali_group_number");
    App.register_number(group_num);
    static_cast<AppRegistrationAccessor&>(App).register_component_(group_num);
    group_num->publish_state(0.0f);

    // Add/Remove buttons (act on the selected address + group).
    auto* add_btn = new DaliGroupMembershipButton { this, /*add=*/true };
    add_btn->configure_dynamic_entity("DALI Add To Group", "dali_group_add");
    App.register_button(add_btn);
    static_cast<AppRegistrationAccessor&>(App).register_component_(add_btn);

    auto* remove_btn = new DaliGroupMembershipButton { this, /*add=*/false };
    remove_btn->configure_dynamic_entity("DALI Remove From Group", "dali_group_remove");
    App.register_button(remove_btn);
    static_cast<AppRegistrationAccessor&>(App).register_component_(remove_btn);

    DALI_LOGI("Created DALI group controls (target address + group number + add/remove)");
#endif
}

void DaliBusComponent::add_to_group(uint8_t addr, uint8_t group) {
    dali.scene.addToGroup(addr, group);
    if (addr <= ADDR_SHORT_MAX) m_group_membership_[addr] |= (uint16_t) (1u << group);

    // Make the group immediately controllable if this is its first member.
    if (m_expose_groups && !m_group_created_[group]) {
        m_group_created_[group] = true;
        create_group_light_component(group);
    }
}

void DaliBusComponent::remove_from_group(uint8_t addr, uint8_t group) {
    dali.scene.removeFromGroup(addr, group);
    if (addr <= ADDR_SHORT_MAX) m_group_membership_[addr] &= (uint16_t) ~(1u << group);
}

void DaliBusComponent::sync_group_member_states(uint8_t group, const GroupStateUpdate& update) {
    uint16_t mask = (uint16_t) (1u << group);
    for (DaliLight* light : m_pollable_lights) {
        if ((m_group_membership_[light->address()] & mask) == 0) continue;
        light->publish_optimistic_state(update);
    }
}

void DaliBusComponent::do_scene_action(uint8_t target_addr, uint8_t scene, SceneAction action) {
    scene &= 0x0F;
    // DALI Part 102 scenes only store brightness; m_scene_color_temp_ is a software
    // side-table for group/individual-lamp targets only (not ADDR_BROADCAST, which
    // the HA "DALI Scene N" buttons use and which has no single CT to capture).
    bool is_group = (target_addr != ADDR_BROADCAST) && ((target_addr & ADDR_GROUP_MASK) != 0);
    bool is_lamp = target_addr <= ADDR_SHORT_MAX;

    switch (action) {
        case SceneAction::Recall: {
            dali.scene.goToScene(target_addr, scene);
            if (!m_scene_color_temp_[scene].has_value()) break;
            uint16_t ct = m_scene_color_temp_[scene].value();
            if (is_group) {
                uint8_t group = target_addr & 0x0F;
                DaliLight* gl = m_group_lights_[group];
                if (gl != nullptr && gl->supports_color_temp()) {
                    dali.color.setColorTemperature(target_addr, ct, false);
                    GroupStateUpdate update;
                    update.color_temp_mired = ct;
                    gl->publish_optimistic_state(update);
                    sync_group_member_states(group, update);
                }
            } else if (is_lamp) {
                for (DaliLight* light : m_pollable_lights) {
                    if (light->address() != target_addr) continue;
                    if (light->supports_color_temp()) {
                        dali.color.setColorTemperature(target_addr, ct, false);
                        GroupStateUpdate update;
                        update.color_temp_mired = ct;
                        light->publish_optimistic_state(update);
                    }
                    break;
                }
            }
            break;
        }
        case SceneAction::Store: {
            dali.scene.storeScene(target_addr, scene);
            m_scene_color_temp_[scene] = {};
            if (is_group) {
                DaliLight* gl = m_group_lights_[target_addr & 0x0F];
                if (gl != nullptr && gl->supports_color_temp()) {
                    uint16_t ct = gl->current_color_temp_mired();
                    if (ct != 0) m_scene_color_temp_[scene] = ct;
                }
            } else if (is_lamp) {
                for (DaliLight* light : m_pollable_lights) {
                    if (light->address() != target_addr) continue;
                    if (light->supports_color_temp()) {
                        uint16_t ct = dali.color.getColorTemperature(target_addr);
                        if (ct != 0) m_scene_color_temp_[scene] = ct;
                    }
                    break;
                }
            }
            break;
        }
        case SceneAction::Remove:
            dali.scene.removeScene(target_addr, scene);
            m_scene_color_temp_[scene] = {};
            break;
    }
}

void DaliBusComponent::identify_lamp(uint8_t addr) {
    if (addr > ADDR_SHORT_MAX) return;

    // If a blink is already running (possibly for a different lamp), restore that
    // lamp's brightness immediately before starting the new one.
    if (m_identify_.active) {
        dali.lamp.setBrightness(m_identify_.addr, m_identify_.original_level);
    }

    m_identify_.active = true;
    m_identify_.addr = addr;
    m_identify_.step = 0;
    m_identify_.next_at_ms = millis();
    m_identify_.original_level = dali.lamp.getCurrentLevel(addr);
}

void DaliBusComponent::process_identify_() {
    if (!m_identify_.active) return;
    if ((int32_t)(millis() - m_identify_.next_at_ms) < 0) return;

    // Blink a few times by alternating min/max brightness, then restore the
    // lamp's original level.
    constexpr uint8_t BLINKS = 3;
    constexpr uint32_t STEP_MS = 400;
    if (m_identify_.step < BLINKS * 2) {
        uint8_t level = (m_identify_.step % 2 == 0) ? 254 : 0;
        dali.lamp.setBrightness(m_identify_.addr, level);
        m_identify_.step++;
        m_identify_.next_at_ms = millis() + STEP_MS;
    } else {
        dali.lamp.setBrightness(m_identify_.addr, m_identify_.original_level);
        m_identify_.active = false;
    }
}

DaliBusComponent::LampInfo DaliBusComponent::lamp_info(size_t index) const {
    const DaliLight* light = m_pollable_lights[index];
    LampInfo info{};
    info.addr = light->address();
    info.online = light->is_available();
    info.problem = light->has_problem();
    const auto& rv = light->remote_values();
    info.on = rv.is_on();
    info.brightness_pct = (uint8_t) lroundf(rv.get_brightness() * 100.0f);
    info.has_color_temp = light->supports_color_temp();
    info.color_temp_mireds = info.has_color_temp ? (uint16_t) lroundf(rv.get_color_temperature()) : 0;
    info.groups = (info.addr <= ADDR_SHORT_MAX) ? m_group_membership_[info.addr] : 0;
    return info;
}

binary_sensor::BinarySensor* DaliBusComponent::create_status_sensor(short_addr_t short_addr) {
#ifdef USE_BINARY_SENSOR
    if (!m_expose_problem) return nullptr;

    const int MAX_STR_LEN = 25;
    char* name = new char[MAX_STR_LEN];
    char* id = new char[MAX_STR_LEN];
    snprintf(name, MAX_STR_LEN, "DALI Light %d Status", short_addr);
    snprintf(id, MAX_STR_LEN, "dali_light_%d_status", short_addr);

    auto* bs = new DaliDiagBinarySensor {};
    bs->configure_dynamic_entity(name, id, m_dc_idx_problem);
    App.register_binary_sensor(bs);
    static_cast<AppRegistrationAccessor&>(App).register_component_(bs);
    bs->publish_initial_state(false);  // assume healthy/available until a poll says otherwise
    return bs;
#else
    return nullptr;
#endif
}

void DaliBusComponent::create_bus_sensor() {
#ifdef USE_BINARY_SENSOR
    if (!m_expose_bus_status) return;
    auto* bs = new DaliDiagBinarySensor {};
    bs->configure_dynamic_entity("DALI Bus Online", "dali_bus_online", m_dc_idx_connectivity);
    App.register_binary_sensor(bs);
    static_cast<AppRegistrationAccessor&>(App).register_component_(bs);
    bs->publish_initial_state(true);  // assume up until a few poll rounds say otherwise
    m_bus_sensor_ = bs;
#endif
}

// Bus-down detection thresholds.
static const uint8_t  DALI_BUS_DOWN_ROUNDS = 2;        // consecutive all-NACK rounds -> down
static const uint32_t DALI_BUS_DOWN_PROBE_MS = 5000;   // slow recovery probing while down

void DaliBusComponent::create_diag_sensors() {
    if (!m_expose_bus_diagnostics) return;

#ifdef USE_BINARY_SENSOR
    auto* ds = new DaliDiagBinarySensor {};
    ds->configure_dynamic_entity("DALI Bus Disconnected", "dali_bus_disconnected");
    App.register_binary_sensor(ds);
    static_cast<AppRegistrationAccessor&>(App).register_component_(ds);
    ds->publish_initial_state(false);
    m_disconnected_sensor_ = ds;
#endif

#ifdef USE_SENSOR
    auto* errs = new DaliDiagSensor {};
    errs->configure_dynamic_entity("DALI Bus Errors", "dali_bus_errors");
    App.register_sensor(errs);
    static_cast<AppRegistrationAccessor&>(App).register_component_(errs);
    errs->publish_state(0);
    m_error_sensor_ = errs;

    auto* downs = new DaliDiagSensor {};
    downs->configure_dynamic_entity("DALI Bus Down Events", "dali_bus_down_events");
    App.register_sensor(downs);
    static_cast<AppRegistrationAccessor&>(App).register_component_(downs);
    downs->publish_state(0);
    m_bus_down_sensor_ = downs;

    if (m_input_devices) {
        auto* colls = new DaliDiagSensor {};
        colls->configure_dynamic_entity("DALI Collisions", "dali_collisions");
        App.register_sensor(colls);
        static_cast<AppRegistrationAccessor&>(App).register_component_(colls);
        colls->publish_state(0);
        m_collision_sensor_ = colls;
    }
#endif
}

void DaliBusComponent::note_poll_error_() {
    m_error_count_++;
#ifdef USE_SENSOR
    if (m_error_sensor_ != nullptr) m_error_sensor_->publish_state(m_error_count_);
#endif
}

void DaliBusComponent::note_bus_down_() {
    m_bus_down_count_++;
#ifdef USE_SENSOR
    if (m_bus_down_sensor_ != nullptr) m_bus_down_sensor_->publish_state(m_bus_down_count_);
#endif
}

void DaliBusComponent::note_disconnected_(bool disconnected) {
    if (disconnected == m_disconnected_) return;
    m_disconnected_ = disconnected;
#ifdef USE_BINARY_SENSOR
    if (m_disconnected_sensor_ != nullptr) m_disconnected_sensor_->publish_state(disconnected);
#endif
}

void DaliBusComponent::note_collision_() {
    m_collision_count_++;
#ifdef USE_SENSOR
    if (m_collision_sensor_ != nullptr) m_collision_sensor_->publish_state(m_collision_count_);
#endif
}

void DaliBusComponent::update_bus_health_(bool any_response) {
    if (any_response) {
        m_bus_down_rounds_ = 0;
        if (!m_bus_online_) {
            m_bus_online_ = true;
            if (m_bus_sensor_ != nullptr) m_bus_sensor_->publish_state(true);
            DALI_LOGI("DALI bus recovered");
            on_bus_recovered_();
        }
        return;
    }
    if (m_bus_down_rounds_ < 255) m_bus_down_rounds_++;
    if (m_bus_online_ && m_bus_down_rounds_ >= DALI_BUS_DOWN_ROUNDS) {
        m_bus_online_ = false;
        if (m_bus_sensor_ != nullptr) m_bus_sensor_->publish_state(false);
        note_bus_down_();
        DALI_LOGW("DALI bus down: no control gear answered for %u poll rounds", m_bus_down_rounds_);
    }
}

void DaliBusComponent::on_bus_recovered_() {
    // The bus came back (e.g. DALI PSU restored). Rebuild any entities for devices
    // present but not yet created; each existing lamp re-applies its own recovery
    // config on its individual unavailable->available transition during polling.
    create_entities_for_present_devices();
}

void DaliBusComponent::loop() {
    process_identify_();
#ifdef DALI_WEB_DASHBOARD_ENABLED
    if (m_dashboard_ != nullptr) {
        if (!m_dashboard_started_) {
            if (network::is_connected()) {
                m_dashboard_->begin(m_dashboard_port, this);
                m_dashboard_started_ = true;
                DALI_LOGI("DALI web dashboard listening on port %u", (unsigned) m_dashboard_port);
            }
        } else {
            m_dashboard_->process_pending_actions();
        }
    }
#endif
    if (m_input_devices) {
        m_input_listener.input_listener_loop();
    }
    for (auto* light : m_dynamic_lights) {
        light->loop();
    }
    for (auto* light : m_command_lights) {
        light->loop();
    }

    // Reflect external lamp changes (broadcast, other controllers) back into HA by
    // polling real device state. One device per tick, round-robin, so each is polled
    // roughly every m_state_poll_interval_ms regardless of how many there are. While
    // the bus is marked down we probe slowly (DALI_BUS_DOWN_PROBE_MS) instead of
    // hammering a dead bus, and recover as soon as any lamp answers again.
    if (m_state_poll_interval_ms > 0 && !m_pollable_lights.empty()) {
        uint32_t per_tick;
        if (!m_bus_online_) {
            per_tick = DALI_BUS_DOWN_PROBE_MS;
        } else {
            per_tick = m_state_poll_interval_ms / m_pollable_lights.size();
            if (per_tick < 50) per_tick = 50;  // floor: don't hammer the bus
        }
        uint32_t now = millis();
        if (now - m_last_poll_ms >= per_tick) {
            m_last_poll_ms = now;
            if (m_poll_index >= m_pollable_lights.size()) {
                // A full round just completed — fold its result into bus health.
                update_bus_health_(m_round_any_response_);
                m_round_any_response_ = false;
                m_poll_index = 0;
            }
            bool resp = m_pollable_lights[m_poll_index]->poll_and_publish();
            if (resp) {
                m_round_any_response_ = true;
                // Recover immediately on the first answer rather than waiting for the
                // round to finish (important during the slow bus-down probe cadence).
                if (!m_bus_online_) update_bus_health_(true);
            } else {
                note_poll_error_();
            }
            m_poll_index++;
        }
    }
}

void DaliBusComponent::dump_config() {
    static const char *const TAG = "dali";
    ESP_LOGCONFIG(TAG, "DALI Bus:");
    LOG_PIN("  TX Pin: ", m_txPin);
    LOG_PIN("  RX Pin: ", m_rxPin);
    ESP_LOGCONFIG(TAG, "  m_discovery: %d", m_discovery);
    ESP_LOGCONFIG(TAG, "  m_init_mode: %d", (int)m_initialize_addresses);
    ESP_LOGCONFIG(TAG, "  Discovery: %s", m_discovery ? "enabled" : "disabled");
    ESP_LOGCONFIG(TAG, "  Control Gear: %s", dali.bus_manager.isControlGearPresent() ? "present" : "not present");
    bool any = false;
    for (int i = 0; i <= ADDR_SHORT_MAX; i++) {
        if (m_addresses[i] > 0) {
            if (!any) {
                ESP_LOGCONFIG(TAG, "  Addresses:");
                any = true;
            }
            ESP_LOGCONFIG(TAG, "    %.2u = %.6x", i, m_addresses[i]);
        }
    }
}

#define QUARTER_BIT_PERIOD 208
#define HALF_BIT_PERIOD 416
#define BIT_PERIOD 833

// Collision sampling splits each half-bit's delay around a single digital_read(),
// so the total delay (and thus the Manchester bit period) is unchanged.
#define HALF_BIT_PERIOD_PRE  (HALF_BIT_PERIOD - 6) / 2
#define HALF_BIT_PERIOD_POST (HALF_BIT_PERIOD - 6) - HALF_BIT_PERIOD_PRE

void DaliBusComponent::check_collision_(bool bit, int half) {
    if (!m_input_devices) return;
    // rx_pin is configured `inverted: true`, matching the TX convention where
    // HIGH = assert (drive bus to 0V): digital_read() == HIGH means the bus
    // reads as asserted.
    bool rx_asserted = (m_rxPin->digital_read() == HIGH);
    if (dali_tx::is_collision(bit, half, rx_asserted)) {
        note_collision_();
    }
}

void DaliBusComponent::writeBit(bool bit) {
    // Output is inverted: HIGH pulls the bus to 0V.
    m_txPin->digital_write(bit ? HIGH : LOW);
    delayMicroseconds(HALF_BIT_PERIOD_PRE);
    check_collision_(bit, 0);
    delayMicroseconds(HALF_BIT_PERIOD_POST);
    m_txPin->digital_write(bit ? LOW : HIGH);
    delayMicroseconds(HALF_BIT_PERIOD_PRE);
    check_collision_(bit, 1);
    delayMicroseconds(HALF_BIT_PERIOD_POST);
}

void DaliBusComponent::writeByte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
        writeBit(b & 0x80);
        b <<= 1;
    }
}

uint8_t DaliBusComponent::readByte() {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte <<= 1;
        byte |= m_rxPin->digital_read();
        delayMicroseconds(BIT_PERIOD); // 1/1200 seconds
    }
    return byte;
}

void DaliBusComponent::resetBus() {
    DALI_LOGD("Resetting bus");
    m_txPin->digital_write(HIGH);
    delay(1000);
    m_txPin->digital_write(LOW);
}

void DaliBusComponent::sendForwardFrame(uint8_t address, uint8_t data) {
    // Don't let the input listener decode our own transmission (un-suppressed on exit).
    ListenerSuppressGuard suppress(&m_input_listener, m_input_devices);

    if (DEBUG_LOG_RXTX) {
        DALI_LOGD("TX: %02x %02x", address, data);
        delayMicroseconds(BIT_PERIOD*8);
        //Serial.print("TX: "); Serial.print(address, HEX); Serial.print(" "); Serial.println(data, HEX);
    }

    {
        // This is timing critical
        InterruptLock lock;

        writeBit(1); // START bit
        writeByte(address);
        writeByte(data);
        // Always leave the bus driven to idle (no half-finished frame on the wire).
        m_txPin->digital_write(LOW);
    }

    // Non critical delay
    delayMicroseconds(HALF_BIT_PERIOD*2);
    delayMicroseconds(BIT_PERIOD*4); // Optional, for clarity in scope trace
}

uint8_t DaliBusComponent::receiveBackwardFrame(unsigned long timeout_ms) {
    uint8_t data;

    // The backward frame is the device's response to us — not an input event.
    // (Un-suppressed automatically on every return path by the guard.)
    ListenerSuppressGuard suppress(&m_input_listener, m_input_devices);

    unsigned long startTime = millis();

    // A real backward frame can't start the instant we begin listening: per
    // IEC 62386-101 a control gear's reply starts at least ~7 Te after the
    // forward frame's stop condition, which is later than this point. If RX is
    // already HIGH right now, the line is stuck/floating (e.g. DALI bus
    // disconnected) rather than carrying a genuine start bit -- treat it as a
    // NACK immediately instead of "reading" a phantom 0xFF reply, which would
    // otherwise make isControlGearPresent()/compareSearchAddress() see a bus
    // full of devices and send run_discovery() into a runaway loop.
    if (m_rxPin->digital_read() == HIGH) {
        if (DEBUG_LOG_RXTX) {
            DALI_LOGD("RX: 00 (NACK, line stuck high)");
        }
        note_disconnected_(true);
        return 0;
    }
    note_disconnected_(false);

    // Wait for the reply's START bit. Bounded by timeout_ms so a silent/stuck-idle bus
    // returns a NACK instead of spinning forever (see DALI_BACKWARD_TIMEOUT_MS).
    while (m_rxPin->digital_read() == LOW) {
        if (millis() - startTime >= timeout_ms) {
            if (DEBUG_LOG_RXTX) {
                DALI_LOGD("RX: 00 (NACK)");
            }
            return 0;
        }
    }

    {
        // This is timing critical
        InterruptLock lock;

        delayMicroseconds(BIT_PERIOD); // Wait for first data bit
        delayMicroseconds(QUARTER_BIT_PERIOD); // Wait a quater bit period to sample middle of first half bit
        data = readByte();
        delayMicroseconds(BIT_PERIOD*2); // Wait for STOP bits
    }

    if (DEBUG_LOG_RXTX) {
        DALI_LOGD("RX: %02x", data);
    }

    // Minimum time before we can send another forward frame
    delayMicroseconds(BIT_PERIOD*8);
    return data;
}
