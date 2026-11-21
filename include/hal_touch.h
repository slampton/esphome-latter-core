#pragma once

// ==============================================================================
// Latter First-Party Touch HAL: Protocol Decoders, Telemetry & Driver Interface
// (include/hal_touch.h)
// ==============================================================================
// Unifies FocalTech (FT6336 / FT3168), Goodix (GT911), and Hynitron (CST9217 /
// CST816S) capacitive touch controllers behind one deterministic state machine:
//   - Trigger-pulse init (FT REG 0xA4=0x01, 0x88=0x06) + post-sleep restore
//   - Zero idle I2C traffic on interrupt-wired boards
//   - High-rate 8-10 ms active-finger tracking while pressed (even on level-hold INT)
//   - Hardware-verified finger lift (0 ms touch_timeout -> zero +120 ms lag)
//   - Race-free GT911 0x814E ack (never clears 0x814E when status & 0x80 == 0)
//   - Strictly IRAM-safe atomic GPIO ISR + wake_pulse latch for <40 ms taps
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#ifdef USE_ESP32
#include <esp_attr.h>
#endif

namespace latter_ui {

enum class TouchHalProtocol : uint8_t {
  FT6336 = 0,
  FT3168 = 1,
  GT911 = 2,
  CST9217 = 3,
  CST816S = 4,
};

enum class TouchSampleKind : uint8_t {
  NotReady = 0,   // No new scan frame ready yet (keep current finger state; do NOT ack)
  Contact = 1,    // Active finger contact at (raw_x, raw_y)
  Released = 2,   // Hardware-verified finger lift (count == 0 or lift flag)
  Invalid = 3,    // Bus NACK or corrupt frame trailer (skip update safely)
};

struct TouchDecodedSample {
  TouchSampleKind kind{TouchSampleKind::NotReady};
  int16_t raw_x{0};
  int16_t raw_y{0};
  bool ack_buffer{false};
};

// FocalTech FT5x06 / FT6336 / FT3168 5-byte frame starting at REG 0x02:
//   buf[0] = TD_STATUS (bits [3:0] = touch point count 0..5)
//   buf[1] = P1_XH (bits [7:6] = Event Flag: 00=Press, 01=Lift, 10=Contact, 11=Reserved; bits [3:0] = X[11:8])
//   buf[2] = P1_XL (X[7:0])
//   buf[3] = P1_YH (bits [7:4] = Touch ID; bits [3:0] = Y[11:8])
//   buf[4] = P1_YL (Y[7:0])
inline constexpr TouchDecodedSample decode_focaltech_frame(const uint8_t buf[5]) noexcept {
  const uint8_t count = static_cast<uint8_t>(buf[0] & 0x0FU);
  if (count > 5U) {
    return TouchDecodedSample{TouchSampleKind::Invalid, 0, 0, false};
  }
  if (count == 0U) {
    return TouchDecodedSample{TouchSampleKind::Released, 0, 0, false};
  }
  const uint8_t event_flag = static_cast<uint8_t>((buf[1] >> 6U) & 0x03U);
  if (event_flag != 0x00U && event_flag != 0x02U) {
    return TouchDecodedSample{TouchSampleKind::Released, 0, 0, false};
  }
  const int16_t x = static_cast<int16_t>(((static_cast<uint16_t>(buf[1] & 0x0FU)) << 8U) | buf[2]);
  const int16_t y = static_cast<int16_t>(((static_cast<uint16_t>(buf[3] & 0x0FU)) << 8U) | buf[4]);
  return TouchDecodedSample{TouchSampleKind::Contact, x, y, false};
}

// Goodix GT911 status byte (0x814E) + optional first-point 4-byte coordinates (0x8150..0x8153).
// CRITICAL: When bit 7 (buffer_status) is 0, ack_buffer MUST be false so 0x814E is never cleared mid-scan.
inline constexpr TouchDecodedSample decode_gt911_frame(uint8_t status_814e,
                                                       const uint8_t *pt_8150_4bytes) noexcept {
  if ((status_814e & 0x80U) == 0U) {
    return TouchDecodedSample{TouchSampleKind::NotReady, 0, 0, false};
  }
  const uint8_t count = static_cast<uint8_t>(status_814e & 0x07U);
  if (count > 5U) {
    return TouchDecodedSample{TouchSampleKind::Invalid, 0, 0, true};
  }
  if (count == 0U || pt_8150_4bytes == nullptr) {
    return TouchDecodedSample{TouchSampleKind::Released, 0, 0, true};
  }
  const int16_t x = static_cast<int16_t>(static_cast<uint16_t>(pt_8150_4bytes[0]) |
                                         (static_cast<uint16_t>(pt_8150_4bytes[1]) << 8U));
  const int16_t y = static_cast<int16_t>(static_cast<uint16_t>(pt_8150_4bytes[2]) |
                                         (static_cast<uint16_t>(pt_8150_4bytes[3]) << 8U));
  return TouchDecodedSample{TouchSampleKind::Contact, x, y, true};
}

// Hynitron CST9217 / CST9220 7-byte single-point report starting at 0xD000 (cstxxx::report_size(1) == 7):
//   buf[0] = (id << 4) | event (event == 0x06 -> TOUCH_EVENT_DOWN)
//   buf[1] = X[11:4], buf[2] = Y[11:4], buf[3] = (X[3:0] << 4) | Y[3:0], buf[4] = Z (pressure)
//   buf[5] = FINGER_COUNT (0xD005, bits [6:0]; 0 -> all fingers lifted)
//   buf[6] = SYNC_BYTE (0xD006, must equal 0xAB; writing an ack back corrupts long presses, so ack_buffer = false)
inline constexpr TouchDecodedSample decode_cst9217_frame(const uint8_t buf[7]) noexcept {
  if (buf[6] != 0xABU) {
    return TouchDecodedSample{TouchSampleKind::NotReady, 0, 0, false};
  }
  const uint8_t num_touches = static_cast<uint8_t>(buf[5] & 0x7FU);
  if (num_touches == 0U || (buf[0] & 0x0FU) != 0x06U) {
    return TouchDecodedSample{TouchSampleKind::Released, 0, 0, false};
  }
  const int16_t x = static_cast<int16_t>((static_cast<uint16_t>(buf[1]) << 4U) |
                                         ((static_cast<uint16_t>(buf[3]) >> 4U) & 0x0FU));
  const int16_t y = static_cast<int16_t>((static_cast<uint16_t>(buf[2]) << 4U) |
                                         (static_cast<uint16_t>(buf[3]) & 0x0FU));
  return TouchDecodedSample{TouchSampleKind::Contact, x, y, false};
}

// Hynitron CST816S / CST816D / CST820 / CST826 6-byte report starting at REG 0x01:
//   buf[0] = GESTURE_ID (0x01)
//   buf[1] = FINGER_NUM (0x02, low nibble 0..2; some CST8xx report all-0xFF when idle)
//   buf[2] = XPOS_H (0x03, bits [7:6] = 00 Down, 01 Lift, 10 Contact, 11 Reserved; bits [3:0] = X[11:8])
//   buf[3] = XPOS_L (0x04, X[7:0])
//   buf[4] = YPOS_H (0x05, bits [3:0] = Y[11:8])
//   buf[5] = YPOS_L (0x06, Y[7:0])
inline constexpr TouchDecodedSample decode_cst816s_frame(const uint8_t buf[6]) noexcept {
  const int16_t x = static_cast<int16_t>(((static_cast<uint16_t>(buf[2] & 0x0FU)) << 8U) | buf[3]);
  const int16_t y = static_cast<int16_t>(((static_cast<uint16_t>(buf[4] & 0x0FU)) << 8U) | buf[5]);
  if (buf[1] == 0xFFU && x == 0x0FFF && y == 0x0FFF) {
    return TouchDecodedSample{TouchSampleKind::Released, 0, 0, false};
  }
  const uint8_t count = static_cast<uint8_t>(buf[1] & 0x0FU);
  if (count > 2U) {
    return TouchDecodedSample{TouchSampleKind::Invalid, 0, 0, false};
  }
  const uint8_t event_flag = static_cast<uint8_t>((buf[2] >> 6U) & 0x03U);
  if (count == 0U || event_flag == 0x01U) {
    return TouchDecodedSample{TouchSampleKind::Released, 0, 0, false};
  }
  if (event_flag == 0x03U || x == 0x0FFF || y == 0x0FFF) {
    return TouchDecodedSample{TouchSampleKind::Invalid, 0, 0, false};
  }
  return TouchDecodedSample{TouchSampleKind::Contact, x, y, false};
}

inline constexpr const char *touch_hal_protocol_name(TouchHalProtocol p) noexcept {
  switch (p) {
    case TouchHalProtocol::FT6336: return "ft6336";
    case TouchHalProtocol::FT3168: return "ft3168";
    case TouchHalProtocol::GT911: return "gt911";
    case TouchHalProtocol::CST9217: return "cst9217";
    case TouchHalProtocol::CST816S: return "cst816s";
  }
  return "unknown";
}

inline constexpr uint16_t default_active_poll_ms(TouchHalProtocol p) noexcept {
  switch (p) {
    case TouchHalProtocol::GT911: return 8U;
    case TouchHalProtocol::FT6336:
    case TouchHalProtocol::FT3168:
    case TouchHalProtocol::CST816S: return 10U;
    case TouchHalProtocol::CST9217: return 16U;
  }
  return 10U;
}

struct TouchHalTelemetry {
  uint32_t irq_edges{0};
  uint32_t i2c_reads{0};
  uint32_t i2c_errors{0};
  uint32_t contacts{0};
  uint32_t releases{0};
  uint32_t wake_pulses{0};
  uint32_t max_active_gap_ms{0};
  uint32_t last_sample_ms{0};
};

inline TouchHalTelemetry &touch_hal_telemetry() noexcept {
  static TouchHalTelemetry s_tel{};
  return s_tel;
}

using TouchPumpFn = void (*)(void *ctx);

struct TouchPumpBridge {
  void *ctx{nullptr};
  TouchPumpFn fn{nullptr};

  void bind(void *c, TouchPumpFn f) noexcept {
    ctx = c;
    fn = f;
  }
};

inline TouchPumpBridge &touch_pump_bridge() noexcept {
  static TouchPumpBridge s_bridge{};
  return s_bridge;
}

inline void hal_touch_pump() noexcept {
  const auto &b = touch_pump_bridge();
  if (b.fn != nullptr) b.fn(b.ctx);
}

}  // namespace latter_ui

#if defined(USE_TOUCHSCREEN) && defined(USE_I2C) && \
    __has_include("esphome/components/touchscreen/touchscreen.h") && \
    __has_include("esphome/components/i2c/i2c.h")
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"

namespace esphome::latter_core {

struct LatterTouchIrqStore {
  std::atomic<bool> irq_pending{false};
  std::atomic<uint32_t> edge_count{0};
#ifdef USE_ESP32
  static void IRAM_ATTR gpio_intr(LatterTouchIrqStore *store);
#else
  static void gpio_intr(LatterTouchIrqStore *store);
#endif
};

class LatterTouchscreen : public touchscreen::Touchscreen, public i2c::I2CDevice {
 public:
  void set_protocol(::latter_ui::TouchHalProtocol proto);
  void set_interrupt_pin(InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_active_poll_ms(uint16_t ms);

  float get_setup_priority() const override;
  void setup() override;
  void update() override;
  void loop() override;
  void pump_from_host();

 protected:
  void finish_setup_();
  bool init_controller_();
  void service_once_(uint32_t now);
  void update_touches() override;

  ::latter_ui::TouchHalProtocol protocol_{::latter_ui::TouchHalProtocol::FT6336};
  InternalGPIOPin *interrupt_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  LatterTouchIrqStore irq_store_{};
  HighFrequencyLoopRequester touch_hf_{};
  uint32_t last_edge_snapshot_{0};
  uint32_t last_service_ms_{0};
  uint16_t active_poll_ms_{10};
  uint16_t detected_chip_id_{0};
  bool active_poll_explicit_{false};
  bool setup_complete_{false};
  bool irq_active_high_{false};
  bool finger_down_{false};
  bool hf_held_{false};
  bool poll_due_{false};
  bool in_service_{false};
  bool had_irq_this_pass_{false};
  bool wake_pulse_active_{false};
};

}  // namespace esphome::latter_core
#endif
