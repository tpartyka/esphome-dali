
#include <esphome.h>
#include "esphome_dali_output.h"

using namespace esphome;
using namespace dali;

static const char *const TAG = "dali.output";

void DaliOutput::setup() {

}

void DaliOutput::loop() {

}

void DaliOutput::write_state(float state) {
    if (bus == nullptr) {
        return;
    }

    // Convert the state to uint8 brightness
    uint8_t level = static_cast<uint8_t>(state * 255.0f);
    if (level > 254) {
        level = 254;
    }
    if (level < 0) {
        level = 0;
    }

    // Coalesce rapid output updates and let the bus scheduler serialize them
    // with light writes and polling.
    bus->enqueue_broadcast_brightness(level);
}
