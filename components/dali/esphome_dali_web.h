#pragma once

#include "esphome/core/defines.h"

// The dashboard is implemented against the ESP-IDF AsyncWebServer-compatible API
// exposed by web_server_base (esp_http_server under the hood). It is only built
// on ESP32; on other platforms expose_dashboard is a no-op (logged at setup).
#if defined(USE_ESP32) && defined(USE_NETWORK)
#define DALI_WEB_DASHBOARD_ENABLED 1

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/lock_free_queue.h"
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
    } kind;
    uint8_t target_addr;  // short address, ADDR_BROADCAST, or ADDR_GROUP|group
    uint8_t value;        // group number (0-15) or scene number (0-15); unused for Identify
};

/// @brief Small built-in web dashboard: lists lamps with live status and lets the
/// user assign group membership and run scene actions. Serves a single embedded
/// HTML page plus a couple of JSON endpoints, all backed by cached bus state.
class DaliWebDashboard : public AsyncWebHandler {
public:
    /// @brief Start the HTTP server on `port` and begin serving the dashboard for `bus`.
    void begin(uint16_t port, DaliBusComponent* bus);

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

    DaliBusComponent* bus_{nullptr};
    AsyncWebServer* server_{nullptr};
    LockFreeQueue<DaliPendingAction, 8> queue_;
};

}  // namespace dali
}  // namespace esphome

#endif  // USE_ESP32 && USE_NETWORK
