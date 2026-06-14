// DaliLamp: brightness/level control, fade time/rate, power-on and
// system-failure level configuration, status queries (IEC 62386-102).

#include "dali.h"
#include "framework.h"
#include "mock_port.h"

namespace {

TEST(set_brightness_uses_direct_arc_power_control) {
    MockDaliPort port;
    DaliLamp lamp(port);

    lamp.setBrightness(10, 128);

    CHECK_EQ(port.sent.size(), 1u);
    // DAPC frame: (addr << 1) | DALI_DIRECT_ARC (command bit clear)
    CHECK_HEX_EQ(port.sent[0].address, (10 << 1) | DALI_DIRECT_ARC);
    CHECK_HEX_EQ(port.sent[0].address & 0x01, 0);
    CHECK_HEX_EQ(port.sent[0].data, 128);
}

TEST(turn_off_sends_off_once) {
    MockDaliPort port;
    DaliLamp lamp(port);

    lamp.turnOff(5);

    CHECK_EQ(port.sent.size(), 1u);
    CHECK_HEX_EQ(port.sent[0].address, (5 << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::OFF));
}

TEST(turn_off_defaults_to_broadcast) {
    MockDaliPort port;
    DaliLamp lamp(port);

    lamp.turnOff();

    CHECK_HEX_EQ(port.sent[0].address, (ADDR_BROADCAST << 1) | DALI_COMMAND);
}

TEST(fade_up_down_and_recall_levels) {
    MockDaliPort port;
    DaliLamp lamp(port);

    lamp.fadeUp(1);
    lamp.fadeDown(1);
    lamp.fadeToMaximum(1);
    lamp.fadeToMinimum(1);

    CHECK_EQ(port.sent.size(), 4u);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::UP));
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::DOWN));
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::RECALL_MAX_LEVEL));
    CHECK_HEX_EQ(port.sent[3].data, static_cast<uint8_t>(DaliCommand::RECALL_MIN_LEVEL));
}

// --- Fade time / rate: DTR0 then a configuration command (sent twice) -------

TEST(set_fade_time_writes_dtr0_then_sends_command_twice) {
    MockDaliPort port;
    DaliLamp lamp(port);

    lamp.setFadeTime(2, 0x0A);

    CHECK_EQ(port.sent.size(), 4u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, 0x0A);
    CHECK(port.sent[1] == port.sent[2]);
    CHECK_HEX_EQ(port.sent[1].address, (2 << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::SET_FADE_TIME_DTR0));
    // DTR0 is reset afterwards so it doesn't linger for other commands to misuse.
    CHECK_HEX_EQ(port.sent[3].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[3].data, 0x00);
}

TEST(set_fade_time_masks_to_4_bits) {
    MockDaliPort port;
    DaliLamp lamp(port);

    lamp.setFadeTime(2, 0xFF);  // only the low nibble is valid (0..15)

    CHECK_HEX_EQ(port.sent[0].data, 0x0F);
}

TEST(set_fade_rate_writes_dtr0_then_sends_command_twice) {
    MockDaliPort port;
    DaliLamp lamp(port);

    lamp.setFadeRate(2, 0x05);

    CHECK_EQ(port.sent.size(), 4u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, 0x05);
    CHECK(port.sent[1] == port.sent[2]);
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::SET_FADE_RATE_DTR0));
    // DTR0 is reset afterwards so it doesn't linger for other commands to misuse.
    CHECK_HEX_EQ(port.sent[3].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[3].data, 0x00);
}

// --- Power-on level: write DTR0, verify, configure, verify again ------------

TEST(set_power_on_level_full_sequence_when_dtr0_confirms) {
    MockDaliPort port;
    DaliLamp lamp(port);

    // 1) getDtr0() readback after setDtr0() must equal the requested value
    // 2) QUERY_POWER_ON_LEVEL after the SET command must equal the requested value
    port.responses.push_back(200);  // getDtr0 readback
    port.responses.push_back(200);  // QUERY_POWER_ON_LEVEL verification

    lamp.setPowerOnLevel(4, 200);

    // setDtr0, getDtr0(query), SET_POWER_ON_LEVEL_DTR0 x2, setDtr0(0) reset, QUERY_POWER_ON_LEVEL
    CHECK_EQ(port.sent.size(), 6u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, 200);

    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::QUERY_CONTENT_DTR0));

    CHECK(port.sent[2] == port.sent[3]);
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::SET_POWER_ON_LEVEL_DTR0));

    // DTR0 is reset afterwards so it doesn't linger for other commands to misuse.
    CHECK_HEX_EQ(port.sent[4].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[4].data, 0x00);

    CHECK_HEX_EQ(port.sent[5].data, static_cast<uint8_t>(DaliCommand::QUERY_POWER_ON_LEVEL));
}

TEST(set_power_on_level_aborts_if_dtr0_readback_mismatches) {
    MockDaliPort port;
    DaliLamp lamp(port);

    port.responses.push_back(0);  // getDtr0 readback != requested value

    lamp.setPowerOnLevel(4, 200);

    // setDtr0, the readback query, and a setDtr0(0) reset were sent; nothing further.
    CHECK_EQ(port.sent.size(), 3u);
    CHECK_HEX_EQ(port.sent[2].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[2].data, 0x00);
}

// --- System failure level: write DTR0, verify, configure (no final verify) --

TEST(set_system_failure_level_sequence_when_dtr0_confirms) {
    MockDaliPort port;
    DaliLamp lamp(port);

    port.responses.push_back(254);  // getDtr0 readback

    lamp.setSystemFailureLevel(4, 254);

    // setDtr0, getDtr0(query), SET_SYSTEM_FAILURE_LEVEL_DTR0 x2, setDtr0(0) reset
    CHECK_EQ(port.sent.size(), 5u);
    CHECK_HEX_EQ(port.sent[0].data, 254);
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::QUERY_CONTENT_DTR0));
    CHECK(port.sent[2] == port.sent[3]);
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::SET_SYSTEM_FAILURE_LEVEL_DTR0));
    // DTR0 is reset afterwards so it doesn't linger for other commands to misuse.
    CHECK_HEX_EQ(port.sent[4].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[4].data, 0x00);
}

TEST(set_system_failure_level_aborts_if_dtr0_readback_mismatches) {
    MockDaliPort port;
    DaliLamp lamp(port);

    port.responses.push_back(1);  // mismatched readback

    lamp.setSystemFailureLevel(4, 254);

    // setDtr0, the readback query, and a setDtr0(0) reset were sent; nothing further.
    CHECK_EQ(port.sent.size(), 3u);
    CHECK_HEX_EQ(port.sent[2].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[2].data, 0x00);
}

// --- Min/Max level: write DTR0, verify, configure ----------------------------

TEST(set_min_level_sequence_when_dtr0_confirms) {
    MockDaliPort port;
    DaliLamp lamp(port);

    port.responses.push_back(1);

    lamp.setMinLevel(4, 1);

    CHECK_EQ(port.sent.size(), 4u);
    CHECK(port.sent[2] == port.sent[3]);
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::SET_MIN_LEVEL_DTR0));
}

TEST(set_max_level_sequence_when_dtr0_confirms) {
    MockDaliPort port;
    DaliLamp lamp(port);

    port.responses.push_back(254);

    lamp.setMaxLevel(4, 254);

    CHECK_EQ(port.sent.size(), 4u);
    CHECK(port.sent[2] == port.sent[3]);
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::SET_MAX_LEVEL_DTR0));
}

// --- Plain queries ------------------------------------------------------------

TEST(status_and_level_queries_are_single_frame) {
    MockDaliPort port;
    DaliLamp lamp(port);

    port.responses = {0x05, 0x01, 0xFE, 0xFE, 0x80};

    uint8_t status = lamp.getStatus(1);
    uint8_t min_level = lamp.getMinLevel(1);
    uint8_t max_level = lamp.getMaxLevel(1);
    uint8_t poweron = lamp.getPowerOnLevel(1);
    uint8_t current = lamp.getCurrentLevel(1);

    CHECK_EQ(port.sent.size(), 5u);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_STATUS));
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::QUERY_MIN_LEVEL));
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::QUERY_MAX_LEVEL));
    CHECK_HEX_EQ(port.sent[3].data, static_cast<uint8_t>(DaliCommand::QUERY_POWER_ON_LEVEL));
    CHECK_HEX_EQ(port.sent[4].data, static_cast<uint8_t>(DaliCommand::QUERY_ACTUAL_LEVEL));

    CHECK_HEX_EQ(status, 0x05);
    CHECK_HEX_EQ(min_level, 0x01);
    CHECK_HEX_EQ(max_level, 0xFE);
    CHECK_HEX_EQ(poweron, 0xFE);
    CHECK_HEX_EQ(current, 0x80);
}

TEST(status_bits_decode) {
    // Sanity check the STATUS_* bit masks against the IEC 62386-102 status byte.
    uint8_t status = STATUS_LAMP_ON | STATUS_FADE_STATE;
    CHECK(status & STATUS_LAMP_ON);
    CHECK(status & STATUS_FADE_STATE);
    CHECK(!(status & STATUS_LAMP_FAILURE));
    CHECK(!(status & STATUS_MISSING_SHORT_ADDRESS));
}

}  // namespace
