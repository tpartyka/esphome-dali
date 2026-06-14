#include "dali_inventory.h"
#include "framework.h"

using namespace dali_inventory;

TEST(empty_mask_has_no_addresses) {
    uint64_t mask = 0;
    CHECK(!inventory_has(mask, 0));
    CHECK(!inventory_has(mask, 63));
}

TEST(set_then_has_round_trips) {
    uint64_t mask = 0;
    mask = inventory_set(mask, 5);
    CHECK(inventory_has(mask, 5));
    CHECK(!inventory_has(mask, 4));
    CHECK(!inventory_has(mask, 6));
}

TEST(set_is_additive_across_addresses) {
    uint64_t mask = 0;
    mask = inventory_set(mask, 0);
    mask = inventory_set(mask, 63);
    CHECK(inventory_has(mask, 0));
    CHECK(inventory_has(mask, 63));
    CHECK(!inventory_has(mask, 1));
}

TEST(out_of_range_address_is_ignored) {
    uint64_t mask = 0;
    uint64_t result = inventory_set(mask, 64);
    CHECK_EQ(result, mask);
    CHECK(!inventory_has(mask, 64));
}

TEST(setting_same_address_twice_is_idempotent) {
    uint64_t mask = inventory_set(0, 10);
    mask = inventory_set(mask, 10);
    CHECK(inventory_has(mask, 10));
    CHECK_EQ(mask, (uint64_t) (1ULL << 10));
}
