// DaliBusManager: commissioning special commands (IEC 62386-101 §9).

#include "dali.h"
#include "framework.h"
#include "mock_port.h"

namespace {

TEST(query_returns_nack_and_skips_rx_after_collision) {
    MockDaliPort port;
    DaliBusManager bus(port);
    port.collision = true;

    CHECK_EQ(port.sendQueryCommand(3, DaliCommand::QUERY_STATUS), 0u);
    CHECK_EQ(port.receive_count, 0);
}

TEST(initialize_sends_initialise_twice_with_given_addr) {
    MockDaliPort port;
    DaliBusManager bus(port);

    bus.initialize(ASSIGN_ALL);

    CHECK_EQ(port.sent.size(), 2u);
    CHECK(port.sent[0] == port.sent[1]);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::INITIALISE));
    CHECK_HEX_EQ(port.sent[0].data, ASSIGN_ALL);
}

TEST(initialize_uninitialized_uses_0xff) {
    MockDaliPort port;
    DaliBusManager bus(port);

    bus.initialize(ASSIGN_UNINITIALIZED);

    CHECK_HEX_EQ(port.sent[0].data, ASSIGN_UNINITIALIZED);
}

TEST(randomize_sends_randomize_twice) {
    MockDaliPort port;
    DaliBusManager bus(port);

    bus.randomize();

    CHECK_EQ(port.sent.size(), 2u);
    CHECK(port.sent[0] == port.sent[1]);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::RANDOMIZE));
}

TEST(terminate_sends_terminate_once) {
    MockDaliPort port;
    DaliBusManager bus(port);

    bus.terminate();

    CHECK_EQ(port.sent.size(), 1u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::TERMINATE));
}

TEST(compare_search_address_sends_searchhml_then_compare) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0xFF);
    bool result = bus.compareSearchAddress(0x123456);

    CHECK_EQ(port.sent.size(), 4u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::SEARCH_ADDRH));
    CHECK_HEX_EQ(port.sent[0].data, 0x12);
    CHECK_HEX_EQ(port.sent[1].address, static_cast<uint8_t>(DaliSpecialCommand::SEARCH_ADDRM));
    CHECK_HEX_EQ(port.sent[1].data, 0x34);
    CHECK_HEX_EQ(port.sent[2].address, static_cast<uint8_t>(DaliSpecialCommand::SEARCH_ADDRL));
    CHECK_HEX_EQ(port.sent[2].data, 0x56);
    CHECK_HEX_EQ(port.sent[3].address, static_cast<uint8_t>(DaliSpecialCommand::COMPARE));
    CHECK(result);
}

TEST(compare_search_address_returns_false_on_collision) {
    MockDaliPort port;
    DaliBusManager bus(port);
    port.collision = true;

    CHECK(!bus.compareSearchAddress(0x123456));
    CHECK_EQ(port.receive_count, 0);
}

TEST(compare_search_address_false_on_non_0xff_response) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0x00);
    CHECK(!bus.compareSearchAddress(0x000000));
}

TEST(program_short_address_encodes_addr_with_command_bit) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0xFF);  // VERIFY_SHORT_ADDRESS confirms
    bool ok = bus.programShortAddress(5);

    CHECK_EQ(port.sent.size(), 2u);
    uint8_t expect = ((5 & 0x3F) << 1) | DALI_COMMAND;
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::PROGRAM_SHORT_ADDRESS));
    CHECK_HEX_EQ(port.sent[0].data, expect);
    CHECK_HEX_EQ(port.sent[1].address, static_cast<uint8_t>(DaliSpecialCommand::VERIFY_SHORT_ADDRESS));
    CHECK_HEX_EQ(port.sent[1].data, expect);
    CHECK(ok);
}

TEST(program_short_address_masks_to_6_bits) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0xFF);
    bus.programShortAddress(0xFF);  // out-of-range -> masked to 0x3F (63)

    uint8_t expect = ((0xFF & 0x3F) << 1) | DALI_COMMAND;
    CHECK_HEX_EQ(port.sent[0].data, expect);
}

TEST(program_short_address_returns_false_on_verify_failure) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0x00);  // VERIFY_SHORT_ADDRESS does not confirm
    CHECK(!bus.programShortAddress(5));
}

TEST(program_short_address_returns_false_on_collision_without_rx) {
    MockDaliPort port;
    DaliBusManager bus(port);
    port.collision = true;

    CHECK(!bus.programShortAddress(5));
    CHECK_EQ(port.receive_count, 0);
}

TEST(auto_assign_returns_failure_on_initialise_collision) {
    MockDaliPort port;
    DaliBusManager bus(port);
    port.collision = true;

    CHECK_EQ(bus.autoAssignShortAddresses(), 0xFF);
    CHECK(!port.sent.empty());
}

TEST(clear_short_address_programs_unassigned_marker) {
    MockDaliPort port;
    DaliBusManager bus(port);

    bus.clearShortAddress();

    CHECK_EQ(port.sent.size(), 1u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::PROGRAM_SHORT_ADDRESS));
    CHECK_HEX_EQ(port.sent[0].data, 0xFF);
}

TEST(is_control_gear_present_defaults_to_broadcast) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0xFF);
    CHECK(bus.isControlGearPresent());

    CHECK_HEX_EQ(port.sent[0].address, (ADDR_BROADCAST << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_CONTROL_GEAR_PRESENT));
}

TEST(is_control_gear_present_false_on_no_response) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0x00);
    CHECK(!bus.isControlGearPresent(3));
}

TEST(is_missing_short_address_query) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses.push_back(0xFF);
    CHECK(bus.isMissingShortAddress(3));

    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_MISSING_SHORT_ADDRESS));
}

TEST(query_address_assembles_24_bit_value) {
    MockDaliPort port;
    DaliBusManager bus(port);

    port.responses = {0x12, 0x34, 0x56};

    uint32_t addr = bus.queryAddress(7);

    CHECK_HEX_EQ(addr, 0x123456u);
    CHECK_EQ(port.sent.size(), 3u);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_RANDOM_ADDRESS_H));
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::QUERY_RANDOM_ADDRESS_M));
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::QUERY_RANDOM_ADDRESS_L));
}

TEST(find_next_address_fails_without_starting_scan) {
    MockDaliPort port;
    DaliBusManager bus(port);

    short_addr_t s;
    uint32_t l;
    CHECK(!bus.findNextAddress(s, l));
}

TEST(find_next_address_returns_false_when_bus_empty) {
    MockDaliPort port;
    DaliBusManager bus(port);

    // already_initialized=true -> startAddressScan skips re-sending INITIALISE
    bus.startAddressScan(/*already_initialized=*/true);
    port.clear();

    // First compareSearchAddress(0xFFFFFF) returns 0x00 -> no devices present.
    port.responses.push_back(0x00);

    short_addr_t s;
    uint32_t l;
    CHECK(!bus.findNextAddress(s, l));
    CHECK_EQ(port.sent.size(), 4u);  // one compareSearchAddress call
}

}  // namespace
