#pragma once

#include <cstdint>

namespace dali_inventory {

/// @brief Bitmask helpers for the discovered short-address inventory
/// persisted by DaliBusComponent (restore_inventory_/save_inventory_), keyed
/// by short address 0..63 (1 bit per address in a uint64_t mask).

/// @return true if `addr` is set in `mask`.
inline bool inventory_has(uint64_t mask, uint8_t addr) {
    if (addr > 63) return false;
    return (mask & (1ULL << addr)) != 0;
}

/// @return `mask` with `addr` set.
inline uint64_t inventory_set(uint64_t mask, uint8_t addr) {
    if (addr > 63) return mask;
    return mask | (1ULL << addr);
}

} // namespace dali_inventory
