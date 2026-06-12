#pragma once

// DALI input-device listener (DALI-2 / IEC 62386-103 control devices: push
// buttons, occupancy/motion sensors, light sensors).
//
// These devices put their own *forward frames* on the bus asynchronously. We
// capture them passively on the same RX pin the master already uses.
//
// Capture is interrupt-driven: a GPIO edge ISR timestamps every transition into
// a lock-free ring buffer, and loop() Manchester-decodes the buffered edges.
// Polling in loop() is far too slow for 416us half-bits once WiFi/API are
// active, which is why the master's RX pin must be interrupt-capable.
//
// Phase 1: capture + raw logging only. DALI-2 frames are 24 bits; we log the
// raw value so the event format can be decoded once we see real devices.

#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace dali {

// DALI is 1200 baud, Manchester encoded: bit period 833us, half-bit 416us.
static const uint32_t DALI_IN_HALF_BIT_US = 416;
static const uint32_t DALI_IN_BIT_US      = 833;
static const uint32_t DALI_IN_TOLERANCE   = 180;   // +/-180us edge classification window
// Idle for > 2 bit periods marks the end of a frame.
static const uint32_t DALI_IN_EOF_US      = DALI_IN_BIT_US * 2 + DALI_IN_TOLERANCE;

/// A raw decoded forward frame seen on the bus.
/// Phase 2 will add decoded DALI-2 fields (address, instance type/number, event).
struct DaliInputFrame {
  uint32_t raw{0};    ///< raw data bits (LSB-aligned), up to 24 for DALI-2
  uint8_t  bits{0};   ///< number of data bits captured (expect 16 or 24)
};

class DaliInputListener {
 public:
  /// Attach the edge interrupt. Call once from setup() after the pin is configured.
  void input_listener_setup(InternalGPIOPin *rx_pin);

  /// Drain the ISR ring buffer and decode. Call every loop().
  void input_listener_loop();

  /// Suppress capture while the master is transmitting/receiving on the bus, so
  /// we don't decode our own frames. The ISR drops edges while suppressed; when
  /// released we discard any edges captured at the boundary and reset the decoder.
  void set_suppressed(bool suppressed) {
    this->suppress_ = suppressed;
    if (!suppressed) {
      this->edge_tail_ = this->edge_head_;  // drop boundary edges from master TX/RX
      this->rx_reset_();
    }
  }

  void add_on_input_frame_callback(std::function<void(DaliInputFrame)> cb) {
    this->frame_callback_.add(std::move(cb));
  }

 protected:
  CallbackManager<void(DaliInputFrame)> frame_callback_;

  // ── ISR edge ring buffer (single-producer ISR, single-consumer loop) ──
  static const uint16_t EDGE_BUF_SIZE = 128;  // a 24-bit frame is ~50 edges
  volatile uint32_t edge_buf_[EDGE_BUF_SIZE];
  volatile uint16_t edge_head_{0};            // written by ISR
  volatile uint16_t edge_tail_{0};            // read by loop
  volatile bool     suppress_{false};

  static void gpio_intr(DaliInputListener *self);

  // ── Manchester decoder state (loop context only) ──
  InternalGPIOPin *rx_pin_{nullptr};
  bool     rx_level_{true};        ///< reconstructed bus level (idle = HIGH); toggles per edge
  uint32_t rx_last_edge_us_{0};
  bool     rx_receiving_{false};
  uint32_t rx_raw_{0};
  uint8_t  rx_bits_{0};
  bool     rx_half_phase_{false};

  void rx_reset_();
  void process_edge_(uint32_t now_us);
  void finalize_frame_();
};

}  // namespace dali
}  // namespace esphome
