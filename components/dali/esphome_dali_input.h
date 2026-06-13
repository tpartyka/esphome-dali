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
// Phase 2: capture + IEC 62386-103 event decode. The 24-bit forward frame is
// decoded into source address / instance / event-info fields (and push-button
// events per IEC 62386-301), surfaced on DaliInputFrame for the on_input_frame
// automation. The exact addressing bit positions should be confirmed against a
// real captured frame — see decode_part103_event() (single, isolated function).

#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace dali {

// IEC 62386 instance types (control-device parts 3xx). Type 1 = push buttons (301).
enum class DaliInstanceType : uint8_t {
  PUSH_BUTTON      = 1,   ///< IEC 62386-301
  ABSOLUTE_INPUT   = 2,   ///< IEC 62386-302 (sliders/dials)
  OCCUPANCY        = 3,   ///< IEC 62386-303 (presence/motion)
  LIGHT_SENSOR     = 4,   ///< IEC 62386-304
};

// Push-button events (IEC 62386-301), carried in the low bits of the event info.
enum class DaliButtonEvent : int8_t {
  NONE             = -1,
  RELEASED         = 0,
  PRESSED          = 1,
  SHORT_PRESS      = 2,
  DOUBLE_PRESS     = 3,
  LONG_PRESS_START = 4,
  LONG_PRESS_REPEAT= 5,
  LONG_PRESS_STOP  = 6,
};

// DALI is 1200 baud, Manchester encoded: bit period 833us, half-bit 416us.
static const uint32_t DALI_IN_HALF_BIT_US = 416;
static const uint32_t DALI_IN_BIT_US      = 833;
static const uint32_t DALI_IN_TOLERANCE   = 180;   // +/-180us edge classification window
// Idle for > 2 bit periods marks the end of a frame.
static const uint32_t DALI_IN_EOF_US      = DALI_IN_BIT_US * 2 + DALI_IN_TOLERANCE;

/// A forward frame seen on the bus, with the IEC 62386-103 event fields decoded
/// (valid only when is_event is true, i.e. a 24-bit control-device frame).
struct DaliInputFrame {
  uint32_t raw{0};            ///< raw data bits (LSB-aligned), up to 24 for DALI-2
  uint8_t  bits{0};           ///< number of data bits captured (16 = gear reply, 24 = event)

  bool     is_event{false};   ///< true for a 24-bit control-device event frame
  uint8_t  short_address{0xFF};   ///< source device short address 0..63, 0xFF = not short-addressed
  uint8_t  instance_type{0xFF};   ///< DaliInstanceType (1 = push button), 0xFF = unknown
  uint8_t  instance_number{0xFF}; ///< instance index within the device, 0xFF = unknown
  uint16_t event_info{0};     ///< 10-bit event information field
  int8_t   button_event{static_cast<int8_t>(DaliButtonEvent::NONE)};  ///< DaliButtonEvent or NONE
};

/// Decode a captured frame's IEC 62386-103 event fields in place (no-op unless 24-bit).
void decode_part103_event(DaliInputFrame &f);
/// Human-readable push-button event name (for logs / templates).
const char *dali_button_event_name(int8_t ev);

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
