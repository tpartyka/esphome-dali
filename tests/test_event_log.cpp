#include "dali_event_log.h"
#include "framework.h"

using namespace dali_event_log;

TEST(empty_buffer_has_no_entries) {
    RingBuffer<4> buf;
    int seen = 0;
    buf.for_each([&](const Event &) { seen++; });
    CHECK_EQ(seen, 0);
    CHECK_EQ(buf.count, (size_t) 0);
}

TEST(push_fewer_than_capacity_preserves_order) {
    RingBuffer<4> buf;
    buf.push({100, EventType::BusDown, NO_ADDR, 0});
    buf.push({200, EventType::BusUp, NO_ADDR, 0});

    CHECK_EQ(buf.count, (size_t) 2);
    std::vector<uint32_t> ts;
    buf.for_each([&](const Event &e) { ts.push_back(e.timestamp_ms); });
    CHECK_EQ(ts.size(), (size_t) 2);
    CHECK_EQ(ts[0], (uint32_t) 100);
    CHECK_EQ(ts[1], (uint32_t) 200);
}

TEST(push_exactly_capacity_preserves_order) {
    RingBuffer<4> buf;
    for (uint32_t i = 0; i < 4; i++) {
        buf.push({i * 10, EventType::Collision, NO_ADDR, 0});
    }
    CHECK_EQ(buf.count, (size_t) 4);
    std::vector<uint32_t> ts;
    buf.for_each([&](const Event &e) { ts.push_back(e.timestamp_ms); });
    for (size_t i = 0; i < 4; i++) {
        CHECK_EQ(ts[i], (uint32_t) (i * 10));
    }
}

TEST(push_beyond_capacity_evicts_oldest) {
    RingBuffer<4> buf;
    for (uint32_t i = 0; i < 5; i++) {
        buf.push({i * 10, EventType::Collision, NO_ADDR, 0});
    }
    // Entry 0 (timestamp 0) should have been evicted.
    CHECK_EQ(buf.count, (size_t) 4);
    std::vector<uint32_t> ts;
    buf.for_each([&](const Event &e) { ts.push_back(e.timestamp_ms); });
    CHECK_EQ(ts.size(), (size_t) 4);
    CHECK_EQ(ts[0], (uint32_t) 10);
    CHECK_EQ(ts[1], (uint32_t) 20);
    CHECK_EQ(ts[2], (uint32_t) 30);
    CHECK_EQ(ts[3], (uint32_t) 40);
}

TEST(push_many_beyond_capacity_keeps_only_last_n) {
    RingBuffer<4> buf;
    for (uint32_t i = 0; i < 10; i++) {
        buf.push({i, EventType::LampOk, (uint8_t) (i % 64), 0});
    }
    CHECK_EQ(buf.count, (size_t) 4);
    std::vector<uint32_t> ts;
    buf.for_each([&](const Event &e) { ts.push_back(e.timestamp_ms); });
    CHECK_EQ(ts[0], (uint32_t) 6);
    CHECK_EQ(ts[1], (uint32_t) 7);
    CHECK_EQ(ts[2], (uint32_t) 8);
    CHECK_EQ(ts[3], (uint32_t) 9);
}

TEST(event_fields_round_trip) {
    RingBuffer<2> buf;
    buf.push({12345, EventType::LampUnavailable, 7, 42});
    Event got{};
    buf.for_each([&](const Event &e) { got = e; });
    CHECK_EQ(got.timestamp_ms, (uint32_t) 12345);
    CHECK(got.type == EventType::LampUnavailable);
    CHECK_EQ(got.addr, (uint8_t) 7);
    CHECK_EQ(got.value, (uint16_t) 42);
}
