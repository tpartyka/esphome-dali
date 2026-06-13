// IEC 62386-101/-102 forward-frame encoding tests: addressing, command framing,
// and the "send twice for configuration commands" rule.

#include "dali.h"
#include "framework.h"
#include "mock_port.h"

namespace {

// --- Addressing -------------------------------------------------------------

TEST(query_command_sets_command_bit_and_addr_shift) {
    MockDaliPort port;
    port.responses.push_back(0x42);

    uint8_t result = port.sendQueryCommand(5, DaliCommand::QUERY_ACTUAL_LEVEL);

    CHECK_EQ(port.sent.size(), 1u);
    // (5 << 1) | DALI_COMMAND
    CHECK_HEX_EQ(port.sent[0].address, (5 << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_ACTUAL_LEVEL));
    CHECK_EQ(result, 0x42);
}

TEST(query_command_broadcast_address) {
    MockDaliPort port;
    port.sendQueryCommand(ADDR_BROADCAST, DaliCommand::QUERY_CONTROL_GEAR_PRESENT);

    CHECK_HEX_EQ(port.sent[0].address, (ADDR_BROADCAST << 1) | DALI_COMMAND);
}

TEST(query_command_group_address) {
    MockDaliPort port;
    short_addr_t group_addr = ADDR_GROUP | 3;  // group 3
    port.sendQueryCommand(group_addr, DaliCommand::QUERY_GROUPS_0_7);

    CHECK_HEX_EQ(port.sent[0].address, (group_addr << 1) | DALI_COMMAND);
}

// --- "Send twice" rule for configuration commands (opcodes 0x20..0x81) ------

TEST(level_commands_are_sent_once) {
    struct Case { DaliCommand cmd; };
    const Case cases[] = {
        {DaliCommand::OFF},
        {DaliCommand::UP},
        {DaliCommand::DOWN},
        {DaliCommand::STEP_UP},
        {DaliCommand::STEP_DOWN},
        {DaliCommand::RECALL_MAX_LEVEL},
        {DaliCommand::RECALL_MIN_LEVEL},
        {DaliCommand::STEP_DOWN_AND_OFF},
        {DaliCommand::ON_AND_STEP_UP},
        {DaliCommand::ENABLE_DAPC_SEQUENCE},
        {DaliCommand::GO_TO_LAST_ACTIVE_LEVEL},
        {DaliCommand::CONTINUOUS_UP},
        {DaliCommand::CONTINUOUS_DOWN},
        {DaliCommand::GO_TO_SCENE},  // 0x10
    };

    for (const auto &c : cases) {
        MockDaliPort port;
        port.sendControlCommand(1, c.cmd);
        CHECK_EQ(port.sent.size(), 1u);
    }
}

TEST(configuration_commands_are_sent_twice) {
    struct Case { DaliCommand cmd; };
    const Case cases[] = {
        {DaliCommand::DALI_RESET},                  // 0x20, lower boundary
        {DaliCommand::STORE_ACTUAL_LEVEL_IN_DTR0},
        {DaliCommand::SAVE_PERSISTENT_VARIABLES},
        {DaliCommand::SET_OPERATING_MODE_DTR0},
        {DaliCommand::RESET_MEMORY_BANK_DTR0},
        {DaliCommand::IDENTIFY_DEVICE},
        {DaliCommand::SET_MAX_LEVEL_DTR0},
        {DaliCommand::SET_MIN_LEVEL_DTR0},
        {DaliCommand::SET_SYSTEM_FAILURE_LEVEL_DTR0},
        {DaliCommand::SET_POWER_ON_LEVEL_DTR0},
        {DaliCommand::SET_FADE_TIME_DTR0},
        {DaliCommand::SET_FADE_RATE_DTR0},
        {DaliCommand::SET_SCENE},
        {DaliCommand::REMOVE_FROM_SCENE},
        {DaliCommand::ADD_TO_GROUP},
        {DaliCommand::REMOVE_FROM_GROUP},
        {DaliCommand::SET_SHORT_ADDRESS_DTR0},
        {DaliCommand::ENABLE_WRITE_MEMORY},  // 0x81, upper boundary
    };

    for (const auto &c : cases) {
        MockDaliPort port;
        port.sendControlCommand(7, c.cmd);

        CHECK_EQ(port.sent.size(), 2u);
        if (port.sent.size() == 2) {
            CHECK(port.sent[0] == port.sent[1]);
            CHECK_HEX_EQ(port.sent[0].address, (7 << 1) | DALI_COMMAND);
            CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(c.cmd));
        }
    }
}

TEST(configuration_command_repeats_are_identical_frames_within_one_call) {
    // Spec requires two *identical* frames within 100ms; verify we don't
    // accidentally vary address/data between the two sends.
    MockDaliPort port;
    port.sendControlCommand(42, DaliCommand::IDENTIFY_DEVICE);

    CHECK_EQ(port.sent.size(), 2u);
    CHECK_EQ(port.sent[0].address, port.sent[1].address);
    CHECK_EQ(port.sent[0].data, port.sent[1].data);
}

// --- Control commands return no value (no backward frame consumed) ---------

TEST(control_command_does_not_consume_backward_frame) {
    MockDaliPort port;
    port.responses.push_back(0xAA);  // would be misread as a query response if consumed
    port.sendControlCommand(1, DaliCommand::OFF);

    // sendControlCommand never calls receiveBackwardFrame, so the queued
    // response must remain untouched.
    CHECK_EQ(port.responses.size(), 1u);
}

// --- Special commands --------------------------------------------------------

TEST(special_command_encodes_command_byte_as_address) {
    MockDaliPort port;
    port.sendSpecialCommand(DaliSpecialCommand::DTR0_DATA, 0x55);

    CHECK_EQ(port.sent.size(), 1u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, 0x55);
}

// --- DTR registers ------------------------------------------------------------

TEST(set_dtr0_dtr1_dtr2) {
    MockDaliPort port;
    port.setDtr0(0x11);
    port.setDtr1(0x22);
    port.setDtr2(0x33);

    CHECK_EQ(port.sent.size(), 3u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, 0x11);
    CHECK_HEX_EQ(port.sent[1].address, static_cast<uint8_t>(DaliSpecialCommand::DTR1_DATA));
    CHECK_HEX_EQ(port.sent[1].data, 0x22);
    CHECK_HEX_EQ(port.sent[2].address, static_cast<uint8_t>(DaliSpecialCommand::DTR2_DATA));
    CHECK_HEX_EQ(port.sent[2].data, 0x33);
}

TEST(get_dtr0_queries_content_dtr0) {
    MockDaliPort port;
    port.responses.push_back(0x99);

    uint8_t v = port.getDtr0(12);

    CHECK_HEX_EQ(port.sent[0].address, (12 << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_CONTENT_DTR0));
    CHECK_EQ(v, 0x99);
}

// --- Extended (device-type) commands -----------------------------------------

TEST(extended_query_enables_device_type_then_queries) {
    MockDaliPort port;
    port.responses.push_back(0x07);  // QUERY_FEATURES response

    uint8_t v = port.sendExtendedQuery(3, DaliLedCommand::QUERY_FEATURES);

    CHECK_EQ(port.sent.size(), 2u);
    // Frame 1: ENABLE_DEVICE_TYPE special command, data = LED device type
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::ENABLE_DEVICE_TYPE));
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliDeviceType::LED));
    // Frame 2: the actual extended query, addressed normally, with response
    CHECK_HEX_EQ(port.sent[1].address, (3 << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliLedCommand::QUERY_FEATURES));
    CHECK_EQ(v, 0x07);
}

TEST(extended_command_enables_device_type_and_sends_twice_with_no_response) {
    MockDaliPort port;
    port.responses.push_back(0xAA);  // must not be consumed

    port.sendExtendedCommand(3, DaliLedCommand::STORE_DTR_AS_FAST_FADE_TIME);

    CHECK_EQ(port.sent.size(), 3u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::ENABLE_DEVICE_TYPE));
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliDeviceType::LED));
    CHECK(port.sent[1] == port.sent[2]);
    CHECK_HEX_EQ(port.sent[1].address, (3 << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliLedCommand::STORE_DTR_AS_FAST_FADE_TIME));
    // No backward frame consumed by sendExtendedCommand.
    CHECK_EQ(port.responses.size(), 1u);
}

TEST(extended_query_color_uses_color_device_type) {
    MockDaliPort port;
    port.responses.push_back(0x00);

    port.sendExtendedQuery(9, DaliColorCommand::QUERY_COLOR_FEATURES);

    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliDeviceType::COLOR));
}

}  // namespace
