#include "esphome_dali_input.h"
#include "esphome/core/log.h"

namespace esphome {
namespace dali {

static const char *const TAG = "dali.input";

const char *dali_button_event_name(int8_t ev) {
  switch (static_cast<DaliButtonEvent>(ev)) {
    case DaliButtonEvent::RELEASED:          return "released";
    case DaliButtonEvent::PRESSED:           return "pressed";
    case DaliButtonEvent::SHORT_PRESS:       return "short_press";
    case DaliButtonEvent::DOUBLE_PRESS:      return "double_press";
    case DaliButtonEvent::LONG_PRESS_START:  return "long_press_start";
    case DaliButtonEvent::LONG_PRESS_REPEAT: return "long_press_repeat";
    case DaliButtonEvent::LONG_PRESS_STOP:   return "long_press_stop";
    default:                                 return "none";
  }
}

// Decode an IEC 62386-103 control-device event frame.
//
// A 24-bit event frame splits into an addressing/source part (upper 14 bits) and a
// 10-bit event-information field (lower 10 bits). For the common instance-addressing
// scheme used by push buttons:
//   bit 23      : 0
//   bits 22..17 : source device short address (6 bits)
//   bit 16      : 0
//   bits 15..11 : instance type   (5 bits; 1 = push button per IEC 62386-301)
//   bit 10      : instance-number selector (kept folded into instance_number below)
//   bits  9..0  : event information (10 bits)
//
// NOTE: DALI-2 defines several instance-addressing schemes; the field positions above
// cover the default push-button case. They are isolated here so they can be corrected
// in one place once a real frame from the user's hardware is captured. The 10-bit
// event-info split and the push-button event codes (IEC 62386-301) are stable.
void decode_part103_event(DaliInputFrame &f) {
  f.is_event = (f.bits == 24);
  if (!f.is_event)
    return;

  uint32_t raw = f.raw & 0xFFFFFF;
  f.event_info = raw & 0x03FF;                       // low 10 bits: event information
  f.short_address = (uint8_t) ((raw >> 17) & 0x3F);  // bits 22..17: device short address
  f.instance_type = (uint8_t) ((raw >> 11) & 0x1F);  // bits 15..11: instance type
  f.instance_number = (uint8_t) ((raw >> 10) & 0x01); // bit 10: instance selector (best-effort)

  // Push-button (instance type 1): the event code lives in the low bits of event_info.
  f.button_event = static_cast<int8_t>(DaliButtonEvent::NONE);
  if (f.instance_type == static_cast<uint8_t>(DaliInstanceType::PUSH_BUTTON)) {
    uint16_t code = f.event_info & 0x000F;
    if (code <= static_cast<uint16_t>(DaliButtonEvent::LONG_PRESS_STOP))
      f.button_event = (int8_t) code;
  }
}

// ── ISR: timestamp every edge into the ring buffer ───────────────────────────
// Kept minimal. NOT marked IRAM_ATTR: ESPHome installs the esp32 GPIO ISR
// service without ESP_INTR_FLAG_IRAM, so the handler runs from flash (IRAM is a
// scarce, often-full region on the esp32-s3). The only consequence is that edges
// are not captured during the brief windows the flash cache is disabled, which
// is harmless for a passive bus listener.
//
// Levels are not read here; the decoder reconstructs them by alternation (DALI
// edges are >=416us apart, so we never miss enough to desync within a frame).
void DaliInputListener::gpio_intr(DaliInputListener *self) {
  if (self->suppress_)
    return;
  uint16_t next = self->edge_head_ + 1;
  if (next >= EDGE_BUF_SIZE)
    next = 0;
  if (next == self->edge_tail_)
    return;  // buffer full -> drop edge
  self->edge_buf_[self->edge_head_] = micros();
  self->edge_head_ = next;
}

void DaliInputListener::input_listener_setup(InternalGPIOPin *rx_pin) {
  this->rx_pin_ = rx_pin;
  this->rx_level_ = true;  // idle bus is HIGH
  this->rx_last_edge_us_ = micros();
  this->rx_reset_();
  this->rx_pin_->attach_interrupt(&DaliInputListener::gpio_intr, this, gpio::INTERRUPT_ANY_EDGE);
  ESP_LOGCONFIG(TAG, "DALI input listener attached to GPIO%u", this->rx_pin_->get_pin());
}

void DaliInputListener::rx_reset_() {
  this->rx_receiving_ = false;
  this->rx_raw_ = 0;
  this->rx_bits_ = 0;
  this->rx_half_phase_ = false;
}

void DaliInputListener::input_listener_loop() {
  // Drain whatever the ISR captured since last time.
  while (this->edge_tail_ != this->edge_head_) {
    uint32_t t = this->edge_buf_[this->edge_tail_];
    uint16_t next = this->edge_tail_ + 1;
    if (next >= EDGE_BUF_SIZE)
      next = 0;
    this->edge_tail_ = next;
    this->process_edge_(t);
  }

  // End-of-frame: bus idle for > 2 bit periods after the last edge.
  if (this->rx_receiving_ && (micros() - this->rx_last_edge_us_) > DALI_IN_EOF_US) {
    this->finalize_frame_();
  }
}

void DaliInputListener::process_edge_(uint32_t now_us) {
  uint32_t dt = now_us - this->rx_last_edge_us_;
  this->rx_last_edge_us_ = now_us;
  this->rx_level_ = !this->rx_level_;  // CHANGE interrupt -> level toggles each edge

  if (!this->rx_receiving_) {
    // Start of frame = falling edge of the start bit (idle HIGH -> LOW).
    if (!this->rx_level_) {
      this->rx_receiving_ = true;
      this->rx_raw_ = 0;
      this->rx_bits_ = 0;
      this->rx_half_phase_ = false;
    }
    return;
  }

  bool is_half = (dt >= DALI_IN_HALF_BIT_US - DALI_IN_TOLERANCE && dt <= DALI_IN_HALF_BIT_US + DALI_IN_TOLERANCE);
  bool is_full = (dt >= DALI_IN_BIT_US - DALI_IN_TOLERANCE && dt <= DALI_IN_BIT_US + DALI_IN_TOLERANCE);
  if (!is_half && !is_full) {
    this->rx_reset_();  // out-of-spec timing -> abandon frame
    return;
  }

  // Data bit value from the mid-bit transition direction:
  //   HIGH->LOW (now LOW)  = logic 1
  //   LOW->HIGH (now HIGH) = logic 0
  if (is_full) {
    uint8_t bit = this->rx_level_ ? 0 : 1;
    this->rx_raw_ = (this->rx_raw_ << 1) | bit;
    this->rx_bits_++;
    this->rx_half_phase_ = false;
  } else {
    // Two half-bit edges make one bit cell; the mid-bit one carries data.
    this->rx_half_phase_ = !this->rx_half_phase_;
    if (this->rx_half_phase_) {
      uint8_t bit = this->rx_level_ ? 0 : 1;
      this->rx_raw_ = (this->rx_raw_ << 1) | bit;
      this->rx_bits_++;
    }
  }

  if (this->rx_bits_ > 26) {  // DALI-2 forward frame is 24 bits; guard runaway
    this->rx_reset_();
  }
}

void DaliInputListener::finalize_frame_() {
  if (this->rx_bits_ >= 16) {
    DaliInputFrame f;
    f.bits = this->rx_bits_;
    f.raw = this->rx_raw_;
    decode_part103_event(f);

    if (f.is_event) {
      ESP_LOGI(TAG,
               "INPUT event: raw=0x%06X addr=%u itype=%u inum=%u event_info=0x%03X button=%s",
               f.raw & 0xFFFFFF, f.short_address, f.instance_type, f.instance_number,
               f.event_info, dali_button_event_name(f.button_event));
    } else {
      // 16-bit (e.g. a control gear's backward frame echoed on the line). Log raw only.
      ESP_LOGD(TAG, "INPUT frame: %u bits, raw=0x%06X  [%02X %02X %02X]", f.bits, f.raw & 0xFFFFFF,
               (uint8_t) (f.raw >> 16), (uint8_t) (f.raw >> 8), (uint8_t) (f.raw));
    }
    this->frame_callback_.call(f);
  }
  this->rx_reset_();
}

}  // namespace dali
}  // namespace esphome
