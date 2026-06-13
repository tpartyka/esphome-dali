#pragma once

// Include the specific esphome headers we depend on *before* <esphome.h>.
// The generated esphome.h includes component headers alphabetically, so this
// header can be pulled in before esphome/core/component.h is defined; relying on
// <esphome.h> alone then fails with "expected class-name" on `public Component`.
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/light/light_state.h"
#include <esphome.h>
#include <vector>
#include "dali.h"
#include "esphome_dali_input.h"

namespace esphome {
namespace dali {

class DaliLight;  // forward decl: the bus keeps a list of lights to poll

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

    /// @brief Register a light to be state-polled by the bus loop. Called by each
    /// DaliLight once it confirms a real (non-broadcast/group) device is present.
    void register_pollable_light(DaliLight* light) {
        m_pollable_lights.push_back(light);
        if (m_state_poll_interval_ms > 0) this->enable_loop();  // ensure loop() runs
    }

    DaliMaster dali;

public: // DaliPort
    void resetBus() override;
    void sendForwardFrame(uint8_t address, uint8_t data) override;
    uint8_t receiveBackwardFrame(unsigned long timeout_ms = 100) override;

private:
    void writeBit(bool bit);
    void writeByte(uint8_t b);
    uint8_t readByte();

    void create_light_component(short_addr_t short_addr, uint32_t long_addr);
    void create_discovery_button();
    void create_reboot_button();
    void create_fade_numbers();

    /// @brief One-shot RX diagnostic: logs the idle level, sends a single broadcast
    /// QUERY CONTROL GEAR PRESENT, then captures the raw RX line transitions so we can
    /// see whether (and when) devices actually answer, independent of polarity guesses.
    void diagnose_rx();

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
    uint32_t m_state_poll_interval_ms = 5000;
    uint32_t m_last_poll_ms = 0;
    size_t m_poll_index = 0;

    // Fade in/out times in milliseconds (runtime-adjustable via HA number entities).
    uint32_t m_fade_in_ms = 1000;
    uint32_t m_fade_out_ms = 1000;
};

}  // namespace dali
}  // namespace esphome
