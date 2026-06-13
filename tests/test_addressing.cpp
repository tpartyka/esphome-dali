// End-to-end commissioning tests against the VirtualDaliBus simulator: binary
// search address discovery, sequential short-address assignment, and
// re-discovery of devices that already have short addresses
// (IEC 62386-101 §9.3 commissioning sequence).

#include "dali.h"
#include "framework.h"
#include "virtual_bus.h"

namespace {

TEST(auto_assign_no_devices_returns_zero) {
    VirtualDaliBus bus({});
    DaliBusManager mgr(bus);

    CHECK_EQ(mgr.autoAssignShortAddresses(ASSIGN_ALL, /*reset=*/true), 0u);
}

TEST(auto_assign_single_device_gets_address_zero) {
    VirtualDaliBus bus({0x123456});
    DaliBusManager mgr(bus);

    uint8_t count = mgr.autoAssignShortAddresses(ASSIGN_ALL, /*reset=*/true);

    CHECK_EQ(count, 1u);
    CHECK_EQ(bus.devices[0].short_addr, 0u);
}

TEST(auto_assign_multiple_devices_get_sequential_addresses_in_long_addr_order) {
    // Long addresses deliberately out of order; binary search must always
    // find the *lowest remaining* long address each round, so short
    // addresses 0,1,2 are handed out in ascending long-address order
    // regardless of the devices vector's order.
    VirtualDaliBus bus({0x800000, 0x000001, 0x400000});
    DaliBusManager mgr(bus);

    uint8_t count = mgr.autoAssignShortAddresses(ASSIGN_ALL, /*reset=*/true);

    CHECK_EQ(count, 3u);
    // device with long_addr 0x000001 -> short 0
    CHECK_EQ(bus.devices[1].short_addr, 0u);
    // device with long_addr 0x400000 -> short 1
    CHECK_EQ(bus.devices[2].short_addr, 1u);
    // device with long_addr 0x800000 -> short 2
    CHECK_EQ(bus.devices[0].short_addr, 2u);
}

TEST(auto_assign_leaves_bus_in_normal_mode_afterwards) {
    VirtualDaliBus bus({0x010203});
    DaliBusManager mgr(bus);

    mgr.autoAssignShortAddresses(ASSIGN_ALL, /*reset=*/true);

    for (auto &d : bus.devices) {
        CHECK(!d.in_init);
        CHECK(!d.withdrawn);
    }

    // The assigned device must now answer QUERY_CONTROL_GEAR_PRESENT at its
    // new short address.
    CHECK(mgr.isControlGearPresent(bus.devices[0].short_addr));
}

TEST(rerunning_discovery_reassigns_from_zero) {
    // Re-running with reset=true (re-RANDOMIZE) re-numbers everything from 0,
    // even if devices previously held different short addresses.
    VirtualDaliBus bus({0x300000, 0x100000});
    DaliBusManager mgr(bus);

    mgr.autoAssignShortAddresses(ASSIGN_ALL, true);
    CHECK_EQ(bus.devices[1].short_addr, 0u);  // 0x100000
    CHECK_EQ(bus.devices[0].short_addr, 1u);  // 0x300000

    uint8_t count = mgr.autoAssignShortAddresses(ASSIGN_ALL, true);

    CHECK_EQ(count, 2u);
    CHECK_EQ(bus.devices[1].short_addr, 0u);
    CHECK_EQ(bus.devices[0].short_addr, 1u);
}

// --- Discover only uninitialized (no short address) devices -----------------

TEST(discover_uninitialized_only_skips_already_addressed_devices) {
    // Device 0 already has a short address; device 1 is new/unassigned.
    VirtualDaliBus bus({0x010203, 0x040506});
    DaliBusManager mgr(bus);
    bus.devices[0].short_addr = 5;

    // ASSIGN_UNINITIALIZED + reset=false: only scan devices without a short
    // address, and don't reassign anything.
    uint8_t count = mgr.autoAssignShortAddresses(ASSIGN_UNINITIALIZED, /*reset=*/false);

    CHECK_EQ(count, 1u);
    // Existing address must be untouched.
    CHECK_EQ(bus.devices[0].short_addr, 5u);
    // The new device is still unassigned (reset=false doesn't program it).
    CHECK_EQ(bus.devices[1].short_addr, 0xFFu);
}

// --- Manual scan via startAddressScan / findNextAddress ----------------------

TEST(find_next_address_walks_devices_in_ascending_long_addr_order) {
    VirtualDaliBus bus({0x0000FF, 0x00FF00, 0xFF0000});
    DaliBusManager mgr(bus);

    mgr.initialize(ASSIGN_ALL);
    mgr.startAddressScan(/*already_initialized=*/true);

    short_addr_t short_addr;
    uint32_t long_addr;

    CHECK(mgr.findNextAddress(short_addr, long_addr));
    CHECK_HEX_EQ(long_addr, 0x0000FFu);
    mgr.withdrawCurrentDevice();

    CHECK(mgr.findNextAddress(short_addr, long_addr));
    CHECK_HEX_EQ(long_addr, 0x00FF00u);
    mgr.withdrawCurrentDevice();

    CHECK(mgr.findNextAddress(short_addr, long_addr));
    CHECK_HEX_EQ(long_addr, 0xFF0000u);
    mgr.withdrawCurrentDevice();

    // All devices withdrawn -> no more.
    CHECK(!mgr.findNextAddress(short_addr, long_addr));

    mgr.endAddressScan();
}

// --- Regression: long addresses at the extremes of the 24-bit range -------
//
// The binary search must not mistake a real device whose random long address
// is exactly 0x000000 or 0x000001 for "no device found" (both are valid
// 24-bit addresses, just astronomically unlikely after RANDOMIZE).

TEST(find_next_address_handles_long_addr_zero) {
    VirtualDaliBus bus({0x000000});
    DaliBusManager mgr(bus);

    mgr.initialize(ASSIGN_ALL);
    mgr.startAddressScan(true);

    short_addr_t s;
    uint32_t l;
    CHECK(mgr.findNextAddress(s, l));
    CHECK_HEX_EQ(l, 0x000000u);
}

TEST(find_next_address_handles_long_addr_one) {
    VirtualDaliBus bus({0x000001});
    DaliBusManager mgr(bus);

    mgr.initialize(ASSIGN_ALL);
    mgr.startAddressScan(true);

    short_addr_t s;
    uint32_t l;
    CHECK(mgr.findNextAddress(s, l));
    CHECK_HEX_EQ(l, 0x000001u);
}

TEST(auto_assign_handles_device_with_long_addr_one_among_others) {
    VirtualDaliBus bus({0x800000, 0x000001, 0x400000});
    DaliBusManager mgr(bus);

    uint8_t count = mgr.autoAssignShortAddresses(ASSIGN_ALL, /*reset=*/true);

    CHECK_EQ(count, 3u);
    CHECK_EQ(bus.devices[1].short_addr, 0u);  // 0x000001 -> first
    CHECK_EQ(bus.devices[2].short_addr, 1u);  // 0x400000 -> second
    CHECK_EQ(bus.devices[0].short_addr, 2u);  // 0x800000 -> third
}

TEST(find_next_address_without_withdraw_loops_on_same_device) {
    // If the caller forgets to withdraw, the same (lowest) device keeps
    // matching the binary search.
    VirtualDaliBus bus({0x000010, 0x000020});
    DaliBusManager mgr(bus);

    mgr.initialize(ASSIGN_ALL);
    mgr.startAddressScan(true);

    short_addr_t s1, s2;
    uint32_t l1, l2;
    CHECK(mgr.findNextAddress(s1, l1));
    CHECK(mgr.findNextAddress(s2, l2));

    CHECK_HEX_EQ(l1, 0x000010u);
    CHECK_HEX_EQ(l2, 0x000010u);
}

}  // namespace
