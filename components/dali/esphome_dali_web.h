#pragma once

#include "esphome/core/defines.h"

// The dashboard is implemented against the ESP-IDF AsyncWebServer-compatible API
// exposed by web_server_base (esp_http_server under the hood). It is only built
// on ESP32; on other platforms expose_dashboard is a no-op (logged at setup).
#if defined(USE_ESP32) && defined(USE_NETWORK)
#define DALI_WEB_DASHBOARD_ENABLED 1

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/lock_free_queue.h"
#include "dali_names.h"
#include <cstdint>

namespace esphome {
namespace dali {

class DaliBusComponent;

/// @brief One queued action from the dashboard, executed from DaliBusComponent::loop()
/// on the main task (the bit-banged DALI bus is not safe to drive from the HTTP
/// server's own task).
struct DaliPendingAction {
    enum class Kind : uint8_t {
        GroupAdd,
        GroupRemove,
        SceneRecall,
        SceneStore,
        SceneRemove,
        Identify,
        LampControl,  // set on/off/brightness/color-temp on any lamp or group light
        Rename,       // set a dashboard-only display name (lamp/group/scene)
        Discovery,    // run discovery with a given DaliInitMode
    } kind;
    uint8_t target_addr;  // short address, ADDR_BROADCAST, or ADDR_GROUP|group
    uint8_t value;        // group/scene number (0-15); for Rename, a NameKind;
                           // for Discovery, a DaliInitMode; unused for Identify

    // LampControl fields: only fields with their has_* flag set are applied.
    bool has_on{false};
    bool on{false};
    bool has_brightness{false};
    uint8_t brightness_pct{0};  // 0-100
    bool has_color_temp{false};
    uint16_t color_temp_mireds{0};

    // Rename field: fixed-size buffer avoids a heap allocation in the queued struct.
    char name[dali_names::NAME_LEN]{0};
};

/// @brief Small built-in web dashboard: lists lamps with live status and lets the
/// user assign group membership and run scene actions. Serves a single embedded
/// HTML page plus a couple of JSON endpoints, all backed by cached bus state.
class DaliWebDashboard : public AsyncWebHandler {
public:
    /// @brief Register this handler on the shared `web_server_base` instance (also
    /// used by `web_server:`) and begin serving the dashboard for `bus`. Safe to call
    /// from setup(): add_handler() just queues the handler until web_server_base's
    /// actual socket is opened later (by `web_server:`'s own setup()).
    void begin(web_server_base::WebServerBase* server_base, DaliBusComponent* bus);

    /// @brief Drain any actions queued by the HTTP handler and apply them to the bus.
    /// Called once per DaliBusComponent::loop() iteration.
    void process_pending_actions();

    bool canHandle(AsyncWebServerRequest* request) const override;
    void handleRequest(AsyncWebServerRequest* request) override;

private:
    void handle_index_(AsyncWebServerRequest* request);
    void handle_lamps_(AsyncWebServerRequest* request);
    void handle_group_action_(AsyncWebServerRequest* request);
    void handle_scene_action_(AsyncWebServerRequest* request);
    void handle_identify_action_(AsyncWebServerRequest* request);
    void handle_lamp_control_(AsyncWebServerRequest* request);
    void handle_names_get_(AsyncWebServerRequest* request);
    void handle_names_set_(AsyncWebServerRequest* request);
    void handle_discovery_(AsyncWebServerRequest* request);
    void handle_log_(AsyncWebServerRequest* request);

    DaliBusComponent* bus_{nullptr};
    LockFreeQueue<DaliPendingAction, 8> queue_;
};

}  // namespace dali
}  // namespace esphome

#endif  // USE_ESP32 && USE_NETWORK
