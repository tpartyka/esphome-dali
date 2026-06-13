#pragma once

// Host-build stand-in for <esphome.h>.
//
// components/dali/port.h unconditionally does `#include <esphome.h>` and then,
// when ARDUINO is defined, only relies on it for delay()/LOW/HIGH (the
// non-ARDUINO branch additionally needs ESP-IDF headers, which is why tests
// are built with -DARDUINO=1). This stub provides just enough for
// dali_bus_manager.cpp's delayMilliseconds() to compile and do nothing.
//
// Found via -I host_shims, which is listed before components/dali, so this
// shadows the real <esphome.h> for the host build only.

#include <cstdint>

inline void delay(uint32_t) {}

#define LOW (0)
#define HIGH (1)
