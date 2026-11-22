#include "dali_address.h"
#include "framework.h"

using namespace dali_addr;

TEST(address_every_short_address_is_short) {
    for (int a = 0; a <= 63; a++) {
        CHECK(is_short((uint8_t) a));
        CHECK(!is_group((uint8_t) a));
    }
}

TEST(address_lamps_16_and_up_are_not_groups) {
    // Regression: `addr & 0x70` was nonzero for 16..63, filing them as groups.
    CHECK(is_short(16));
    CHECK(is_short(19));
    CHECK(is_short(32));
    CHECK(is_short(63));
    CHECK(!is_group(16));
    CHECK(!is_group(63));
}

TEST(address_group_addresses) {
    for (int g = 0; g < 16; g++) {
        CHECK(is_group((uint8_t) (0x40 | g)));
        CHECK(!is_short((uint8_t) (0x40 | g)));
    }
    CHECK(!is_group(0x50));
}

TEST(address_broadcast_is_neither) {
    CHECK(!is_short(0x7F));
    CHECK(!is_group(0x7F));
    CHECK(!is_short(0xFF));
    CHECK(!is_group(0xFF));
}
