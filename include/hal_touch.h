#pragma once

// ==============================================================================
// Latter First-Party Touch HAL: Protocol Decoders, Telemetry & Driver Interface
// (include/hal_touch.h)
// ==============================================================================
// Thread affinity:
//   - ISR/main-loop cross-thread atomics: `LatterTouchIrqStore` (`irq_pending`,
//     `edge_count`) is written in IRAM GPIO ISR context and consumed on the main
//     ESPHome loop(); `wake_isr_fn` is bound before attaching the interrupt.
//   - Main-loop only: `LatterTouchscreen` methods, `TouchHalTelemetry`, and
//     `touch_pump_bridge()` / `touch_control_bridge()` / `touch_wake_bridge()`.
// ------------------------------------------------------------------------------
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

namespace latter_core {

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

inline constexpr uint32_t touch_post_reset_settle_ms(TouchHalProtocol p) noexcept {
  switch (p) {
    case TouchHalProtocol::FT6336:
    case TouchHalProtocol::FT3168:
      return 250U;  // FocalTech datasheet T_rst is 200-300 ms before 0xA3 CIPHER & config regs respond
    case TouchHalProtocol::GT911:
      return 56U;
    default:
      return 38U;
  }
}

static constexpr uint16_t kDefaultIdlePollMs = 28U;
static constexpr uint32_t kPolledPostReleaseHoldMs = 450U;

inline constexpr uint16_t effective_touch_poll_ms(bool finger_down, bool has_interrupt_pin,
                                                  uint16_t active_poll_ms, uint16_t idle_poll_ms,
                                                  uint32_t now_ms, uint32_t last_release_ms) noexcept {
  if (finger_down || has_interrupt_pin) return active_poll_ms;
  if (last_release_ms != 0U && (now_ms - last_release_ms) < kPolledPostReleaseHoldMs) {
    return active_poll_ms;
  }
  return (idle_poll_ms > active_poll_ms) ? idle_poll_ms : active_poll_ms;
}

// FocalTech FT5x06 / FT6x36 / FT3x68 CIPHER (REG 0xA3) chip family IDs.
// Rejects non-FocalTech chips sharing I2C 0x38 (e.g. AHT20 status byte 0x18/0x1C, PCA9554A 0x00/0xFF).
inline constexpr bool is_known_focaltech_chip_id(uint8_t chip_id) noexcept {
  switch (chip_id) {
    case 0x06:  // FT6206 / FT6306
    case 0x08:  // FT3168 / FT3x68
    case 0x11:  // FT5316 / FT6236G
    case 0x36:  // FT6236 / FT6336U
    case 0x54:  // FT5426 / FT5446 / FT5436
    case 0x55:  // FT5x06 / FT5306 / FT5406
    case 0x58:  // FT5826
    case 0x64:  // FT6336 / FT6436
      return true;
    default:
      return false;
  }
}

// Pure bounded boot-recovery policy (max 2 retries, >= 2 s apart, after 1.5 s boot settle).
struct TouchRecoveryPolicy {
  uint8_t attempts{0};
  uint32_t last_attempt_ms{0};

  constexpr void reset() noexcept {
    attempts = 0U;
    last_attempt_ms = 0U;
  }

  constexpr bool should_attempt(bool is_failed, uint32_t now_ms) noexcept {
    if (!is_failed || now_ms <= 1500U || attempts >= 2U) return false;
    if (last_attempt_ms != 0U && (now_ms - last_attempt_ms) < 2000U) return false;
    ++attempts;
    last_attempt_ms = now_ms;
    return true;
  }
};

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

using TouchSleepModeFn = void (*)(void *ctx, bool sleep);
using TouchRecoverFn = void (*)(void *ctx, uint32_t now_ms);
using TouchPrepareRestartFn = void (*)(void *ctx);

struct TouchControlBridge {
  void *ctx{nullptr};
  TouchSleepModeFn sleep_mode_fn{nullptr};
  TouchRecoverFn recover_fn{nullptr};
  TouchPrepareRestartFn prepare_restart_fn{nullptr};

  void bind(void *c, TouchSleepModeFn sm, TouchRecoverFn rec, TouchPrepareRestartFn rst) noexcept {
    ctx = c;
    sleep_mode_fn = sm;
    recover_fn = rec;
    prepare_restart_fn = rst;
  }

  void reset() noexcept {
    ctx = nullptr;
    sleep_mode_fn = nullptr;
    recover_fn = nullptr;
    prepare_restart_fn = nullptr;
  }
};

inline TouchControlBridge &touch_control_bridge() noexcept {
  static TouchControlBridge s_bridge{};
  return s_bridge;
}

inline void hal_touch_set_sleep_mode(bool sleep) noexcept {
  const auto &b = touch_control_bridge();
  if (b.sleep_mode_fn != nullptr) b.sleep_mode_fn(b.ctx, sleep);
}

inline void hal_touch_recover_if_failed(uint32_t now_ms) noexcept {
  const auto &b = touch_control_bridge();
  if (b.recover_fn != nullptr) b.recover_fn(b.ctx, now_ms);
}

inline void hal_touch_prepare_for_restart() noexcept {
  const auto &b = touch_control_bridge();
  if (b.prepare_restart_fn != nullptr) b.prepare_restart_fn(b.ctx);
}

using TouchIsStandbyFn = bool (*)();
using TouchWakeGraceFn = void (*)(uint32_t now_ms);
using TouchIrqWakeFn = void (*)() noexcept;

struct TouchWakeBridge {
  TouchIsStandbyFn is_standby_fn{nullptr};
  TouchWakeGraceFn wake_grace_fn{nullptr};
  TouchIrqWakeFn wake_isr_fn{nullptr};

  void bind(TouchIsStandbyFn is_standby, TouchWakeGraceFn wake_grace,
            TouchIrqWakeFn wake_isr = nullptr) noexcept {
    is_standby_fn = is_standby;
    wake_grace_fn = wake_grace;
    wake_isr_fn = wake_isr;
  }

  bool is_standby() const noexcept {
    return is_standby_fn != nullptr && is_standby_fn();
  }

  void arm_wake_grace(uint32_t now_ms) const noexcept {
    if (wake_grace_fn != nullptr) wake_grace_fn(now_ms);
  }
};

inline TouchWakeBridge &touch_wake_bridge() noexcept {
  static TouchWakeBridge s_bridge{};
  return s_bridge;
}

}  // namespace latter_core

#if defined(USE_TOUCHSCREEN) && defined(USE_I2C) && \
    __has_include("esphome/components/touchscreen/touchscreen.h") && \
    __has_include("esphome/components/i2c/i2c.h")
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"

namespace latter_core {

struct LatterTouchIrqStore {
  std::atomic<bool> irq_pending{false};
  std::atomic<uint32_t> edge_count{0};
  TouchIrqWakeFn wake_isr_fn{nullptr};
#ifdef USE_ESP32
  static void IRAM_ATTR gpio_intr(LatterTouchIrqStore *store);
#else
  static void gpio_intr(LatterTouchIrqStore *store);
#endif
};

// Keep <=4 B so ESP-IDF IRAM noflash stdatomic helpers handle ISR/main-loop access under -mdisable-hardware-atomics.
static_assert(sizeof(LatterTouchIrqStore::irq_pending) <= 4U &&
                  sizeof(LatterTouchIrqStore::edge_count) <= 4U,
              "LatterTouchIrqStore atomics must stay <=4 B for IRAM ISR stdatomic access");

class LatterTouchscreen : public ::esphome::touchscreen::Touchscreen, public ::esphome::i2c::I2CDevice {
 public:
  void set_protocol(TouchHalProtocol proto);
  void set_interrupt_pin(::esphome::InternalGPIOPin *pin) { this->interrupt_pin_ = pin; }
  void set_reset_pin(::esphome::GPIOPin *pin) { this->reset_pin_ = pin; }
  void set_active_poll_ms(uint16_t ms);

  float get_setup_priority() const override;
  void setup() override;
  void update() override;
  void loop() override;
  void pump_from_host();
  void set_sleep_mode(bool sleep);
  void recover_if_failed(uint32_t now_ms);
  void prepare_for_restart();

 protected:
  void finish_setup_();
  void finish_cst9217_setup_();
  void complete_setup_ready_();
  bool init_controller_();
  void consume_edge_delta_(uint32_t now);
  uint16_t effective_poll_ms_() const noexcept;
  void service_once_(uint32_t now);
  void update_touches() override;

  TouchHalProtocol protocol_{TouchHalProtocol::FT6336};
  ::esphome::InternalGPIOPin *interrupt_pin_{nullptr};
  ::esphome::GPIOPin *reset_pin_{nullptr};
  LatterTouchIrqStore irq_store_{};
  ::esphome::HighFrequencyLoopRequester touch_hf_{};
  TouchRecoveryPolicy recovery_policy_{};
  uint32_t last_edge_snapshot_{0};
  uint32_t last_service_ms_{0};
  uint32_t last_release_ms_{0};
  uint32_t last_prof_tel_ms_{0};
  uint32_t last_prof_reads_{0};
  uint16_t active_poll_ms_{10};
  uint16_t detected_chip_id_{0};
  int8_t last_sleep_mode_{-1};
  bool focaltech_id_verified_{false};
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

}  // namespace latter_core
#endif
