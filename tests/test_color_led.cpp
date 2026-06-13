// DaliColorClass / DaliLedClass: device-type-244 (LED) and -209 (colour control)
// extended commands (IEC 62386-209/-249).

#include "dali.h"
#include "framework.h"
#include "mock_port.h"

namespace {

// --- LED extended commands ---------------------------------------------------

TEST(set_dimming_curve_writes_dtr0_sets_curve_then_verifies) {
    MockDaliPort port;
    DaliLedClass led(port);

    // QUERY_DIMMING_CURVE readback must match the curve we set.
    port.responses.push_back(static_cast<uint8_t>(DaliLedDimmingCurve::LINEAR));

    led.setDimmingCurve(1, DaliLedDimmingCurve::LINEAR);

    // setDtr0 (1) + SELECT_DIMMING_CURVE extended command (ENABLE + x2 = 3)
    // + QUERY_DIMMING_CURVE extended query (ENABLE + 1 = 2) = 6 frames
    CHECK_EQ(port.sent.size(), 6u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliLedDimmingCurve::LINEAR));

    CHECK_HEX_EQ(port.sent[1].address, static_cast<uint8_t>(DaliSpecialCommand::ENABLE_DEVICE_TYPE));
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliDeviceType::LED));
    CHECK(port.sent[2] == port.sent[3]);
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliLedCommand::SELECT_DIMMING_CURVE));

    CHECK_HEX_EQ(port.sent[4].address, static_cast<uint8_t>(DaliSpecialCommand::ENABLE_DEVICE_TYPE));
    CHECK_HEX_EQ(port.sent[5].data, static_cast<uint8_t>(DaliLedCommand::QUERY_DIMMING_CURVE));
}

TEST(get_dimming_curve_is_a_single_extended_query) {
    MockDaliPort port;
    DaliLedClass led(port);

    port.responses.push_back(static_cast<uint8_t>(DaliLedDimmingCurve::LOGARITHMIC));

    auto curve = led.getDimmingCurve(1);

    CHECK_EQ(port.sent.size(), 2u);  // ENABLE_DEVICE_TYPE + query
    CHECK(curve == DaliLedDimmingCurve::LOGARITHMIC);
}

TEST(set_fast_fade_time_writes_dtr0_then_extended_command) {
    MockDaliPort port;
    DaliLedClass led(port);

    led.setFastFadeTime(1, 0x07);

    // setDtr0 (1) + STORE_DTR_AS_FAST_FADE_TIME extended command (ENABLE + x2 = 3)
    CHECK_EQ(port.sent.size(), 4u);
    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, 0x07);
    CHECK(port.sent[2] == port.sent[3]);
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliLedCommand::STORE_DTR_AS_FAST_FADE_TIME));
}

// --- Colour control (device type 8) -------------------------------------------

TEST(is_tc_capable_checks_feature_bit) {
    MockDaliPort port;
    DaliColorClass color(port);

    port.responses.push_back(static_cast<uint8_t>(DaliColorFeature::TC_CAPABLE));

    CHECK(color.isTcCapable(1));

    CHECK_EQ(port.sent.size(), 2u);  // ENABLE_DEVICE_TYPE(COLOR) + QUERY_COLOR_FEATURES
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliDeviceType::COLOR));
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliColorCommand::QUERY_COLOR_FEATURES));
}

TEST(is_tc_capable_false_when_bit_clear) {
    MockDaliPort port;
    DaliColorClass color(port);

    port.responses.push_back(static_cast<uint8_t>(DaliColorFeature::XY_CAPABLE));  // only XY

    CHECK(!color.isTcCapable(1));
}

TEST(is_xy_capable_checks_feature_bit) {
    MockDaliPort port;
    DaliColorClass color(port);

    port.responses.push_back(static_cast<uint8_t>(DaliColorFeature::XY_CAPABLE));

    CHECK(color.isXYCapable(1));
}

TEST(set_color_temperature_writes_tc_to_dtr0_dtr1_and_activates) {
    MockDaliPort port;
    DaliColorClass color(port);

    uint16_t tc = 0x1234;
    color.setColorTemperature(1, tc, /*start_fade=*/true);

    // setDtr0 + setDtr1 (2) + SET_TEMPERATURE ext cmd (ENABLE+x2=3)
    // + ACTIVATE ext cmd (ENABLE+x2=3) = 8 frames
    CHECK_EQ(port.sent.size(), 8u);

    CHECK_HEX_EQ(port.sent[0].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[0].data, tc & 0xFF);
    CHECK_HEX_EQ(port.sent[1].address, static_cast<uint8_t>(DaliSpecialCommand::DTR1_DATA));
    CHECK_HEX_EQ(port.sent[1].data, (tc >> 8) & 0xFF);

    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliDeviceType::COLOR));
    CHECK(port.sent[3] == port.sent[4]);
    CHECK_HEX_EQ(port.sent[3].data, static_cast<uint8_t>(DaliColorCommand::SET_TEMPERATURE));

    CHECK_HEX_EQ(port.sent[5].data, static_cast<uint8_t>(DaliDeviceType::COLOR));
    CHECK(port.sent[6] == port.sent[7]);
    CHECK_HEX_EQ(port.sent[6].data, static_cast<uint8_t>(DaliColorCommand::ACTIVATE));
}

TEST(set_color_temperature_without_fade_skips_activate) {
    MockDaliPort port;
    DaliColorClass color(port);

    color.setColorTemperature(1, 0x1234, /*start_fade=*/false);

    // setDtr0 + setDtr1 (2) + SET_TEMPERATURE ext cmd (ENABLE+x2=3) = 5 frames
    CHECK_EQ(port.sent.size(), 5u);
    CHECK_HEX_EQ(port.sent[4].data, static_cast<uint8_t>(DaliColorCommand::SET_TEMPERATURE));
}

TEST(step_warmer_and_cooler_send_extended_query) {
    MockDaliPort port;
    DaliColorClass color(port);

    color.stepWarmer(1);
    color.stepCooler(1);

    CHECK_EQ(port.sent.size(), 4u);
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliColorCommand::TEMPERATURE_WARMER));
    CHECK_HEX_EQ(port.sent[3].data, static_cast<uint8_t>(DaliColorCommand::TEMPERATURE_COOLER));
}

TEST(query_parameter_reads_actual_level_then_color_value_msb_lsb) {
    MockDaliPort port;
    DaliColorClass color(port);

    port.responses.push_back(0x00);  // QUERY_ACTUAL_LEVEL (ignored)
    port.responses.push_back(0x12);  // QUERY_COLOR_VALUE -> MSB
    port.responses.push_back(0x34);  // getDtr0 -> LSB

    uint16_t v = color.queryParameter(1, DaliColorParam::ReportColourTemperatureTc);

    CHECK_EQ(port.sent.size(), 5u);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_ACTUAL_LEVEL));
    CHECK_HEX_EQ(port.sent[1].address, static_cast<uint8_t>(DaliSpecialCommand::DTR0_DATA));
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliColorParam::ReportColourTemperatureTc));
    CHECK_EQ(v, 0x1234u);
}

TEST(get_color_temperature_uses_report_param) {
    MockDaliPort port;
    DaliColorClass color(port);

    port.responses = {0x00, 0x02, 0x71};  // -> 0x0271

    CHECK_EQ(color.getColorTemperature(1), 0x0271u);
}

TEST(supports_extended_color_true_when_version_nonzero) {
    MockDaliPort port;
    DaliColorClass color(port);

    port.responses.push_back(0x01);

    CHECK(color.supportsExtendedColor(1));
}

TEST(supports_extended_color_false_when_version_zero) {
    MockDaliPort port;
    DaliColorClass color(port);

    port.responses.push_back(0x00);

    CHECK(!color.supportsExtendedColor(1));
}

// --- Colour temperature range constants --------------------------------------

TEST(mirek_range_constants_are_sane) {
    CHECK(COLOR_MIREK_COOLEST < COLOR_MIREK_WARMEST);
    CHECK_EQ(COLOR_MIREK_COOLEST, 10);
    CHECK_EQ(COLOR_MIREK_WARMEST, 1000);
}

}  // namespace
