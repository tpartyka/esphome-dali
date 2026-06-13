// DaliScene: group membership and scene recall/store/remove (IEC 62386-102 §10.2).

#include "dali.h"
#include "framework.h"
#include "mock_port.h"

namespace {

TEST(add_to_group_encodes_group_in_low_nibble_and_sends_twice) {
    MockDaliPort port;
    DaliScene scene(port);

    scene.addToGroup(1, 5);

    CHECK_EQ(port.sent.size(), 2u);
    CHECK(port.sent[0] == port.sent[1]);
    CHECK_HEX_EQ(port.sent[0].address, (1 << 1) | DALI_COMMAND);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::ADD_TO_GROUP) | 5);
}

TEST(remove_from_group_encodes_group_and_sends_twice) {
    MockDaliPort port;
    DaliScene scene(port);

    scene.removeFromGroup(1, 5);

    CHECK_EQ(port.sent.size(), 2u);
    CHECK(port.sent[0] == port.sent[1]);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::REMOVE_FROM_GROUP) | 5);
}

TEST(group_number_is_masked_to_4_bits) {
    MockDaliPort port;
    DaliScene scene(port);

    scene.addToGroup(1, 0xFF);  // group out of range -> masked to 0x0F

    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::ADD_TO_GROUP) | 0x0F);
}

TEST(go_to_scene_is_sent_once_and_encodes_scene_in_low_nibble) {
    MockDaliPort port;
    DaliScene scene(port);

    scene.goToScene(1, 7);

    CHECK_EQ(port.sent.size(), 1u);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::GO_TO_SCENE) | 7);
}

TEST(go_to_scene_can_address_a_group) {
    MockDaliPort port;
    DaliScene scene(port);

    short_addr_t group_addr = ADDR_GROUP | 2;
    scene.goToScene(group_addr, 3);

    CHECK_HEX_EQ(port.sent[0].address, (group_addr << 1) | DALI_COMMAND);
}

TEST(scene_number_is_masked_to_4_bits) {
    MockDaliPort port;
    DaliScene scene(port);

    scene.goToScene(1, 0x1F);  // scene out of range -> masked to 0x0F

    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::GO_TO_SCENE) | 0x0F);
}

TEST(store_scene_stores_actual_level_then_sets_scene) {
    MockDaliPort port;
    DaliScene scene(port);

    scene.storeScene(1, 4);

    // STORE_ACTUAL_LEVEL_IN_DTR0 (x2) then SET_SCENE|4 (x2)
    CHECK_EQ(port.sent.size(), 4u);
    CHECK(port.sent[0] == port.sent[1]);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::STORE_ACTUAL_LEVEL_IN_DTR0));

    CHECK(port.sent[2] == port.sent[3]);
    CHECK_HEX_EQ(port.sent[2].data, static_cast<uint8_t>(DaliCommand::SET_SCENE) | 4);
}

TEST(remove_scene_encodes_scene_and_sends_twice) {
    MockDaliPort port;
    DaliScene scene(port);

    scene.removeScene(1, 4);

    CHECK_EQ(port.sent.size(), 2u);
    CHECK(port.sent[0] == port.sent[1]);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::REMOVE_FROM_SCENE) | 4);
}

TEST(query_groups_combines_two_bytes_into_16_bit_mask) {
    MockDaliPort port;
    DaliScene scene(port);

    port.responses.push_back(0b10100000);  // groups 0-7: bits 5 and 7 set
    port.responses.push_back(0b00000011);  // groups 8-15: bits 8 and 9 set

    uint16_t groups = scene.queryGroups(1);

    CHECK_EQ(port.sent.size(), 2u);
    CHECK_HEX_EQ(port.sent[0].data, static_cast<uint8_t>(DaliCommand::QUERY_GROUPS_0_7));
    CHECK_HEX_EQ(port.sent[1].data, static_cast<uint8_t>(DaliCommand::QUERY_GROUPS_8_15));

    CHECK(groups & (1u << 5));
    CHECK(groups & (1u << 7));
    CHECK(groups & (1u << 8));
    CHECK(groups & (1u << 9));
    CHECK(!(groups & (1u << 0)));
    CHECK(!(groups & (1u << 15)));
}

TEST(query_groups_all_zero_when_no_membership) {
    MockDaliPort port;
    DaliScene scene(port);

    port.responses.push_back(0x00);
    port.responses.push_back(0x00);

    CHECK_EQ(scene.queryGroups(1), 0u);
}

}  // namespace
