#include <esphome.h>
#include <esp_task_wdt.h>
#include <cstring>
#include "esphome_dali.h"
#include "esphome_dali_light.h"
#include "port.h"
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif

//static const char *const TAG = "dali";
static const bool DEBUG_LOG_RXTX = true; // NOTE: Will probably trigger WDT

using namespace esphome;
using namespace dali;

namespace {

class AppRegistrationAccessor : public esphome::Application {
public:
    using Application::register_component_;
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
#endif  // USE_NUMBER

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

    // Passive input-device listener (Part 103 push buttons / sensors).
    if (m_input_devices) {
        m_input_listener.input_listener_setup(m_rxPin);
    }

    // Discovery must also run in setup(): dynamically created light entities are only
    // advertised to Home Assistant in the entity list it requests once at connect time.
    // (Running discovery later, e.g. via the button, won't surface NEW lights in HA
    // until the next reboot.)
    if (m_discovery) {
        run_discovery();
    } else if (!m_input_devices) {
        // No discovery and no listener -> nothing for loop() to drive.
        this->disable_loop();
    }

    // The input listener needs loop() running even if discovery disabled it.
    if (m_input_devices) {
        this->enable_loop();
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
        if (!m_input_devices) this->disable_loop();
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
    for (uint8_t addr = 0; addr <= ADDR_SHORT_MAX; addr++) {
        delay(1);  // yield to ESP stack
        esp_task_wdt_reset();
        if (m_addresses[addr]) continue;  // already has an entity (discovery or static YAML)
        if (dali.isDevicePresent(addr)) {
            m_addresses[addr] = 0xFFFFFF;  // mark created (long address unknown here)
            create_light_component(addr, 0xFFFFFF);
            created++;
        }
    }
    DALI_LOGI("Created %u light entit%s for already-addressed device(s)", created, created == 1 ? "y" : "ies");
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

void DaliBusComponent::loop() {
    if (m_input_devices) {
        m_input_listener.input_listener_loop();
    }
    for (auto* light : m_dynamic_lights) {
        light->loop();
    }

    // Reflect external lamp changes (broadcast, other controllers) back into HA by
    // polling real device state. One device per tick, round-robin, so each is polled
    // roughly every m_state_poll_interval_ms regardless of how many there are.
    if (m_state_poll_interval_ms > 0 && !m_pollable_lights.empty()) {
        uint32_t per_tick = m_state_poll_interval_ms / m_pollable_lights.size();
        if (per_tick < 50) per_tick = 50;  // floor: don't hammer the bus
        uint32_t now = millis();
        if (now - m_last_poll_ms >= per_tick) {
            m_last_poll_ms = now;
            if (m_poll_index >= m_pollable_lights.size()) m_poll_index = 0;
            m_pollable_lights[m_poll_index]->poll_and_publish();
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

void DaliBusComponent::writeBit(bool bit) {
    // Output is inverted: HIGH pulls the bus to 0V.
    m_txPin->digital_write(bit ? HIGH : LOW);
    delayMicroseconds(HALF_BIT_PERIOD - 6);
    m_txPin->digital_write(bit ? LOW : HIGH);
    delayMicroseconds(HALF_BIT_PERIOD - 6);
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
    // Don't let the input listener decode our own transmission.
    if (m_input_devices) m_input_listener.set_suppressed(true);

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
        m_txPin->digital_write(LOW);
    }

    // Non critical delay
    delayMicroseconds(HALF_BIT_PERIOD*2);
    delayMicroseconds(BIT_PERIOD*4); // Optional, for clarity in scope trace

    if (m_input_devices) m_input_listener.set_suppressed(false);
}

uint8_t DaliBusComponent::receiveBackwardFrame(unsigned long timeout_ms) {
    uint8_t data;

    // The backward frame is the device's response to us — not an input event.
    if (m_input_devices) m_input_listener.set_suppressed(true);

    unsigned long startTime = millis();

    // Wait for START bit (timing critical)
    // TODO: Need a better way to wait for this that doesn't block the CPU
    while (m_rxPin->digital_read() == LOW) {
        if (millis() - startTime >= timeout_ms) {
            if (DEBUG_LOG_RXTX) {
                DALI_LOGD("RX: 00 (NACK)");
            }
            if (m_input_devices) m_input_listener.set_suppressed(false);
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
    if (m_input_devices) m_input_listener.set_suppressed(false);
    return data;
}
