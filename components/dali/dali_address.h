#pragma once

#include <cstdint>

namespace dali_addr {

/// @brief Classify the 7-bit address a DaliLight / bus call targets.
///
///   short address  0..63   0AAAAAA
///   group address  64..79  100GGGG  (ADDR_GROUP | group)
///   anything else  broadcast (0x7F, or 0xFF as used in YAML)
///
/// Do not test `addr & ADDR_GROUP_MASK` (0x70) for this: short addresses
/// 16..63 have bit 4 or 5 set, so that test filed lamps 16+ as group
/// addresses (no capability query, no polling, no status sensor, no colour).

/// @return true for an individual lamp (short address 0..63).
inline bool is_short(uint8_t addr) { return addr <= 63; }

/// @return true for a group address 64..79.
inline bool is_group(uint8_t addr) { return addr >= 0x40 && addr <= 0x4F; }

} // namespace dali_addr
