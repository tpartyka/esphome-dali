#pragma once

#include "dali.h"

#include <cstdint>
#include <deque>
#include <vector>

/// One forward frame as seen on the wire: (address byte, data/command byte).
struct SentFrame {
    uint8_t address;
    uint8_t data;

    bool operator==(const SentFrame &o) const {
        return address == o.address && data == o.data;
    }
};

/// Minimal DaliPort double for protocol/encoding tests.
///
/// - Every sendForwardFrame() call is recorded in `sent`, in order.
/// - receiveBackwardFrame() returns the next queued response from
///   `responses` (FIFO), or `default_response` if the queue is empty.
class MockDaliPort : public DaliPort {
public:
    std::vector<SentFrame> sent;
    std::deque<uint8_t> responses;
    uint8_t default_response = 0xFF;
    bool collision = false;
    int reset_count = 0;
    int receive_count = 0;

    void sendForwardFrame(uint8_t address, uint8_t data) override {
        sent.push_back({address, data});
    }

    uint8_t receiveBackwardFrame(unsigned long timeout_ms = DALI_BACKWARD_TIMEOUT_MS) override {
        (void)timeout_ms;
        receive_count++;
        if (!responses.empty()) {
            uint8_t r = responses.front();
            responses.pop_front();
            return r;
        }
        return default_response;
    }

    bool lastTransmissionHadCollision() const override { return collision; }

    void resetBus() override { reset_count++; }

    void clear() {
        sent.clear();
        responses.clear();
    }
};
