#pragma once

// A minimal multi-device DALI-2 bus simulator, used to exercise
// DaliBusManager's commissioning state machine (binary-search address
// discovery, withdraw, short-address programming) per IEC 62386-101 §9.

#include "dali.h"

#include <cstdint>
#include <vector>

struct VirtualDevice {
    uint32_t long_addr;             // 24-bit random address (fixed for test determinism)
    short_addr_t short_addr = 0xFF; // 0xFF = unassigned
    bool in_init = false;           // currently in INITIALISE mode
    bool withdrawn = false;         // WITHDRAWn from the current scan
};

class VirtualDaliBus : public DaliPort {
public:
    std::vector<VirtualDevice> devices;

    explicit VirtualDaliBus(std::vector<uint32_t> long_addrs) {
        for (auto a : long_addrs) devices.push_back({a});
    }

    void sendForwardFrame(uint8_t address, uint8_t data) override {
        has_response_ = false;

        switch (address) {
            case static_cast<uint8_t>(DaliSpecialCommand::INITIALISE):
                for (auto &d : devices) {
                    bool matches;
                    if (data == ASSIGN_ALL) {
                        matches = true;
                    } else if (data == ASSIGN_UNINITIALIZED) {
                        matches = (d.short_addr == 0xFF);
                    } else {
                        matches = d.short_addr != 0xFF &&
                                  (((d.short_addr & 0x3F) << 1) | DALI_COMMAND) == data;
                    }
                    if (matches) {
                        d.in_init = true;
                        d.withdrawn = false;
                    } else {
                        d.in_init = false;
                    }
                }
                break;

            case static_cast<uint8_t>(DaliSpecialCommand::RANDOMIZE):
                // No-op: long_addr is fixed at construction for determinism.
                break;

            case static_cast<uint8_t>(DaliSpecialCommand::SEARCH_ADDRH):
                search_addr_ = (search_addr_ & 0x00FFFFu) | (static_cast<uint32_t>(data) << 16);
                break;
            case static_cast<uint8_t>(DaliSpecialCommand::SEARCH_ADDRM):
                search_addr_ = (search_addr_ & 0xFF00FFu) | (static_cast<uint32_t>(data) << 8);
                break;
            case static_cast<uint8_t>(DaliSpecialCommand::SEARCH_ADDRL):
                search_addr_ = (search_addr_ & 0xFFFF00u) | data;
                break;

            case static_cast<uint8_t>(DaliSpecialCommand::COMPARE): {
                for (auto &d : devices) {
                    if (d.in_init && !d.withdrawn && d.long_addr <= search_addr_) {
                        response_ = 0xFF;
                        has_response_ = true;
                        break;
                    }
                }
                break;
            }

            case static_cast<uint8_t>(DaliSpecialCommand::WITHDRAW):
                for (auto &d : devices) {
                    if (d.in_init && !d.withdrawn && d.long_addr == search_addr_) {
                        d.withdrawn = true;
                    }
                }
                break;

            case static_cast<uint8_t>(DaliSpecialCommand::QUERY_SHORT_ADDRESS):
                for (auto &d : devices) {
                    if (d.in_init && !d.withdrawn && d.long_addr == search_addr_) {
                        if (d.short_addr == 0xFF) {
                            response_ = 0xFF;
                        } else {
                            response_ = ((d.short_addr & 0x3F) << 1) | DALI_COMMAND;
                        }
                        has_response_ = true;
                        break;
                    }
                }
                break;

            case static_cast<uint8_t>(DaliSpecialCommand::PROGRAM_SHORT_ADDRESS):
                for (auto &d : devices) {
                    if (d.in_init && !d.withdrawn && d.long_addr == search_addr_) {
                        if (data == 0x7F) {
                            d.short_addr = 0xFF;
                        } else {
                            d.short_addr = (data >> 1) & 0x3F;
                        }
                    }
                }
                break;

            case static_cast<uint8_t>(DaliSpecialCommand::VERIFY_SHORT_ADDRESS):
                for (auto &d : devices) {
                    if (d.in_init && !d.withdrawn && d.long_addr == search_addr_) {
                        uint8_t target = (data >> 1) & 0x3F;
                        if (d.short_addr == target) {
                            response_ = 0xFF;
                            has_response_ = true;
                        }
                    }
                }
                break;

            case static_cast<uint8_t>(DaliSpecialCommand::TERMINATE):
                for (auto &d : devices) {
                    d.in_init = false;
                    d.withdrawn = false;
                }
                search_addr_ = 0;
                break;

            default: {
                // Normal forward frame: (addr << 1) | cmd_bit. Support
                // QUERY_CONTROL_GEAR_PRESENT addressed to an assigned short
                // address, for end-to-end "device responds after assignment"
                // checks.
                if ((address & DALI_COMMAND) && data == static_cast<uint8_t>(DaliCommand::QUERY_CONTROL_GEAR_PRESENT)) {
                    short_addr_t addr = address >> 1;
                    for (auto &d : devices) {
                        if (d.short_addr == addr) {
                            response_ = 0xFF;
                            has_response_ = true;
                            break;
                        }
                    }
                }
                break;
            }
        }
    }

    uint8_t receiveBackwardFrame(unsigned long timeout_ms = DALI_BACKWARD_TIMEOUT_MS) override {
        (void)timeout_ms;
        if (has_response_) {
            has_response_ = false;
            return response_;
        }
        return 0x00;  // NACK / no reply
    }

private:
    uint32_t search_addr_ = 0;
    bool has_response_ = false;
    uint8_t response_ = 0;
};
