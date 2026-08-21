#include "framework.h"
#include "dali_backward_frame.h"

#include <cstdint>

namespace {

void encode_valid_backward_frame(uint8_t value, bool (&halves)[20]) {
    // Start bit is logical 1. Each data bit is Manchester encoded as
    // [bit, !bit]. The final two half-bits must be idle/released.
    halves[0] = true;
    halves[1] = false;
    for (int bit = 0; bit < 8; bit++) {
        const bool value_bit = (value & (0x80u >> bit)) != 0;
        halves[2 + bit * 2] = value_bit;
        halves[3 + bit * 2] = !value_bit;
    }
    halves[18] = false;
    halves[19] = false;
}

TEST(backward_frame_decodes_valid_manchester_byte) {
    bool halves[20]{};
    encode_valid_backward_frame(0xA5, halves);

    uint8_t decoded = 0;
    CHECK(dali_rx::decode_backward_frame_halves(halves, decoded));
    CHECK_HEX_EQ(decoded, 0xA5);
}

TEST(backward_frame_rejects_invalid_start_symbol) {
    bool halves[20]{};
    encode_valid_backward_frame(0x5A, halves);
    halves[0] = false;

    uint8_t decoded = 0;
    CHECK(!dali_rx::decode_backward_frame_halves(halves, decoded));
}

TEST(backward_frame_rejects_non_manchester_data_symbol) {
    bool halves[20]{};
    encode_valid_backward_frame(0x5A, halves);
    halves[7] = halves[6];

    uint8_t decoded = 0;
    CHECK(!dali_rx::decode_backward_frame_halves(halves, decoded));
}

TEST(backward_frame_rejects_asserted_stop_period) {
    bool halves[20]{};
    encode_valid_backward_frame(0x5A, halves);
    halves[18] = true;

    uint8_t decoded = 0;
    CHECK(!dali_rx::decode_backward_frame_halves(halves, decoded));
}

}  // namespace
