#include "hal_touch.h"

#include "esphome/core/defines.h"

#if defined(USE_TOUCHSCREEN) && defined(USE_I2C) && \
    __has_include("esphome/components/touchscreen/touchscreen.h") && \
    __has_include("esphome/components/i2c/i2c.h")

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <utility>

#include "latter_prof.h"

namespace latter_core {
using namespace ::esphome;

namespace {
constexpr const char *TAG = "latter";
constexpr uint32_t kResetAssertTimeoutId = 0x4C540000U;
constexpr uint32_t kResetReleaseTimeoutId = 0x4C540001U;
constexpr uint32_t kPostResetSettleTimeoutId = 0x4C540002U;
constexpr uint32_t kCstCmdSettleTimeoutId = 0x4C540003U;
}  // namespace

static_assert(std::atomic<bool>::is_always_lock_free,
              "Touch ISR atomic<bool> must be lock-free");
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "Touch ISR atomic<uint32_t> must be lock-free");

#ifdef USE_ESP32
void IRAM_ATTR LatterTouchIrqStore::gpio_intr(LatterTouchIrqStore *store) {
#else
void LatterTouchIrqStore::gpio_intr(LatterTouchIrqStore *store) {
#endif
  store->irq_pending.store(true, std::memory_order_relaxed);
  store->edge_count.fetch_add(1U, std::memory_order_relaxed);
  if (store->wake_isr_fn != nullptr) {
    store->wake_isr_fn();
  }
}

void LatterTouchscreen::set_protocol(TouchHalProtocol proto) {
  this->protocol_ = proto;
  if (!this->active_poll_explicit_) {
    this->active_poll_ms_ = default_active_poll_ms(proto);
  }
}

void LatterTouchscreen::set_active_poll_ms(uint16_t ms) {
  this->active_poll_ms_ = (ms < 4U) ? 4U : ms;
  this->active_poll_explicit_ = true;
}

float LatterTouchscreen::get_setup_priority() const {
  // Run after display setup (PROCESSOR) and on_boot:-100 PMU rail initialization (hal_init_axp2101).
  return setup_priority::LATE - 10.0f;
}

void LatterTouchscreen::setup() {
  using P = TouchHalProtocol;
  this->setup_complete_ = false;
  this->irq_active_high_ = false;
  this->detected_chip_id_ = 0U;
  this->last_sleep_mode_ = -1;
  this->focaltech_id_verified_ = false;

  touch_control_bridge().bind(
      this,
      +[](void *ctx, bool sleep) { static_cast<LatterTouchscreen *>(ctx)->set_sleep_mode(sleep); },
      +[](void *ctx, uint32_t now_ms) { static_cast<LatterTouchscreen *>(ctx)->recover_if_failed(now_ms); },
      +[](void *ctx) { static_cast<LatterTouchscreen *>(ctx)->prepare_for_restart(); });

  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    if (this->protocol_ == P::GT911) {
      // Goodix GT911 datasheet: hold INT low while RST rises to select 0x5D, then release INT to input after T3+T4.
      this->reset_pin_->digital_write(false);
      if (this->interrupt_pin_ != nullptr) {
        this->interrupt_pin_->setup();
        this->interrupt_pin_->pin_mode(gpio::FLAG_OUTPUT);
        this->interrupt_pin_->digital_write(false);
      }
      this->set_timeout(kResetReleaseTimeoutId, 12, [this]() {
        this->reset_pin_->digital_write(true);
        this->set_timeout(kPostResetSettleTimeoutId, 56, [this]() {
          if (this->interrupt_pin_ != nullptr) {
            this->interrupt_pin_->pin_mode(gpio::FLAG_INPUT);
          }
          this->finish_setup_();
        });
      });
      return;
    }

    // Hynitron CST9217 & FocalTech: strictly non-blocking reset sequence (5 ms high -> 12 ms low -> 38 ms settle).
    this->reset_pin_->digital_write(true);
    this->set_timeout(kResetAssertTimeoutId, 5, [this]() {
      this->reset_pin_->digital_write(false);
      this->set_timeout(kResetReleaseTimeoutId, 12, [this]() {
        this->reset_pin_->digital_write(true);
        this->set_timeout(kPostResetSettleTimeoutId, 38, [this]() { this->finish_setup_(); });
      });
    });
    return;
  }

  // Boards without a dedicated reset_pin: allow 40 ms post-rail / bootloader settle non-blockingly.
  this->set_timeout(kPostResetSettleTimeoutId, 40, [this]() { this->finish_setup_(); });
}

void LatterTouchscreen::finish_setup_() {
  using P = TouchHalProtocol;

  if (this->protocol_ == P::CST9217) {
    if (this->write_register16(0xD101, nullptr, 0) != i2c::ERROR_OK) {
      ESP_LOGE(TAG, "Touch HAL (cst9217 @ 0x%02X) failed cmd-mode enter; scheduled for HAL recovery",
               this->address_);
      this->mark_failed();
      return;
    }
    this->set_timeout(kCstCmdSettleTimeoutId, 10, [this]() { this->finish_cst9217_setup_(); });
    return;
  }

  if (!this->init_controller_()) {
    ESP_LOGE(TAG, "Touch HAL (%s @ 0x%02X) failed I2C init; scheduled for HAL recovery",
             touch_hal_protocol_name(this->protocol_), this->address_);
    this->mark_failed();
    return;
  }

  this->complete_setup_ready_();
}

void LatterTouchscreen::finish_cst9217_setup_() {
  // Hynitron CST9217 / CST9220 post-cmd-mode verify:
  //   2. Verify firmware check code at 0xD1FC == 0xCACA
  //   3. Read native X/Y resolution at 0xD1F8 and chip ID at 0xD204 (0x9220 or 0x9217)
  //   4. Do NOT write 0xD109 or ack 0xD000 (controller serves 7-byte reports at 0xD000 directly)
  uint8_t chk[4]{};
  if (this->read_register16(0xD1FC, chk, 4) != i2c::ERROR_OK) {
    this->mark_failed();
    return;
  }
  const uint16_t check_code =
      static_cast<uint16_t>(chk[2]) | (static_cast<uint16_t>(chk[3]) << 8U);
  if (check_code != 0xCACAU) {
    ESP_LOGE(TAG, "CST92xx invalid firmware check code: 0x%04X", static_cast<unsigned>(check_code));
    this->mark_failed();
    return;
  }

  uint8_t res[4]{};
  if (this->read_register16(0xD1F8, res, 4) == i2c::ERROR_OK) {
    const int16_t rx = static_cast<int16_t>(static_cast<uint16_t>(res[0]) | (static_cast<uint16_t>(res[1]) << 8U));
    const int16_t ry = static_cast<int16_t>(static_cast<uint16_t>(res[2]) | (static_cast<uint16_t>(res[3]) << 8U));
    if (rx > 10 && ry > 10) {
      if (this->x_raw_max_ == this->x_raw_min_) this->x_raw_max_ = rx;
      if (this->y_raw_max_ == this->y_raw_min_) this->y_raw_max_ = ry;
    }
  }

  uint8_t info[4]{};
  if (this->read_register16(0xD204, info, 4) != i2c::ERROR_OK) {
    this->mark_failed();
    return;
  }
  const uint16_t chip_id =
      static_cast<uint16_t>(info[2]) | (static_cast<uint16_t>(info[3]) << 8U);
  if (chip_id != 0x9220U && chip_id != 0x9217U) {
    ESP_LOGE(TAG, "CST92xx unexpected chip ID: 0x%04X", static_cast<unsigned>(chip_id));
    this->mark_failed();
    return;
  }
  this->detected_chip_id_ = chip_id;
  this->complete_setup_ready_();
}

void LatterTouchscreen::complete_setup_ready_() {
  using P = TouchHalProtocol;

  if (this->x_raw_max_ == this->x_raw_min_) {
    this->x_raw_max_ = static_cast<int16_t>(
        (this->display_ != nullptr) ? this->display_->get_native_width() : this->display_width_);
  }
  if (this->y_raw_max_ == this->y_raw_min_) {
    this->y_raw_max_ = static_cast<int16_t>(
        (this->display_ != nullptr) ? this->display_->get_native_height() : this->display_height_);
  }

  this->irq_store_.wake_isr_fn = touch_wake_bridge().wake_isr_fn;
  if (this->interrupt_pin_ != nullptr) {
    if (!this->store_.init) {
      this->interrupt_pin_->setup();
      if (this->protocol_ == P::FT6336 || this->protocol_ == P::FT3168 || this->protocol_ == P::CST816S) {
        this->interrupt_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
      }
      this->interrupt_pin_->attach_interrupt(
          LatterTouchIrqStore::gpio_intr, &this->irq_store_,
          this->irq_active_high_ ? gpio::INTERRUPT_RISING_EDGE : gpio::INTERRUPT_FALLING_EDGE);
      this->store_.init = true;
    }
    this->stop_poller();
  }

  touch_pump_bridge().bind(
      this, +[](void *ctx) { static_cast<LatterTouchscreen *>(ctx)->pump_from_host(); });
  this->setup_complete_ = true;

  ESP_LOGD(TAG, "Touch HAL ready: proto=%s chip=0x%04X addr=0x%02X mode=%s poll=%ums raw=%dx%d",
           touch_hal_protocol_name(this->protocol_),
           static_cast<unsigned>(this->detected_chip_id_), this->address_,
           (this->interrupt_pin_ != nullptr)
               ? (this->irq_active_high_ ? "irq_high+poll" : "irq_low+poll")
               : "polled",
           static_cast<unsigned>(this->active_poll_ms_),
           static_cast<int>(this->x_raw_max_), static_cast<int>(this->y_raw_max_));
}

void LatterTouchscreen::update() {
  if (this->interrupt_pin_ == nullptr) {
    this->poll_due_ = true;
  }
}

void LatterTouchscreen::consume_edge_delta_(uint32_t now) {
  const uint32_t edges = this->irq_store_.edge_count.load(std::memory_order_relaxed);
  if (edges != this->last_edge_snapshot_) {
    touch_hal_telemetry().irq_edges += (edges - this->last_edge_snapshot_);
    this->last_edge_snapshot_ = edges;
    if (touch_wake_bridge().is_standby()) {
      touch_wake_bridge().arm_wake_grace(now);
    }
  }
}

void LatterTouchscreen::pump_from_host() {
  if (!this->setup_complete_ || this->is_failed() || this->in_service_) return;
  const uint32_t now = esphome::millis();
  this->consume_edge_delta_(now);
  const bool has_irq = this->irq_store_.irq_pending.load(std::memory_order_relaxed);
  const bool poll_ready =
      (this->finger_down_ || this->interrupt_pin_ == nullptr) &&
      (this->last_service_ms_ == 0U || (now - this->last_service_ms_) >= this->active_poll_ms_);
  if (has_irq || poll_ready) {
    this->service_once_(now);
  }
}

void LatterTouchscreen::loop() {
  if (!this->setup_complete_ || this->is_failed() || this->in_service_) return;
  const uint32_t now = esphome::millis();
  if (this->irq_store_.wake_isr_fn == nullptr && touch_wake_bridge().wake_isr_fn != nullptr) {
    this->irq_store_.wake_isr_fn = touch_wake_bridge().wake_isr_fn;
  }
  this->consume_edge_delta_(now);

  const bool has_irq = this->irq_store_.irq_pending.load(std::memory_order_relaxed);
  const bool int_asserted =
      (this->interrupt_pin_ != nullptr) &&
      (this->interrupt_pin_->digital_read() == this->irq_active_high_);
  const bool active_due =
      (this->finger_down_ || int_asserted || this->interrupt_pin_ == nullptr || this->poll_due_) &&
      (this->last_service_ms_ == 0U || (now - this->last_service_ms_) >= this->active_poll_ms_);

  if (has_irq || active_due) {
    this->service_once_(now);
  }

  if (profiler_enabled() && (now - this->last_prof_tel_ms_) >= 10000U) {
    const auto &tel = touch_hal_telemetry();
    if (tel.i2c_reads != this->last_prof_reads_) {
      this->last_prof_reads_ = tel.i2c_reads;
      this->last_prof_tel_ms_ = now;
      LATTER_PROF_LOGI("[PROF:TOUCH_HAL] proto=%s edges=%lu reads=%lu err=%lu contacts=%lu wake=%lu max_gap=%lums",
                       touch_hal_protocol_name(this->protocol_),
                       static_cast<unsigned long>(tel.irq_edges),
                       static_cast<unsigned long>(tel.i2c_reads),
                       static_cast<unsigned long>(tel.i2c_errors),
                       static_cast<unsigned long>(tel.contacts),
                       static_cast<unsigned long>(tel.wake_pulses),
                       static_cast<unsigned long>(tel.max_active_gap_ms));
    }
  }
}

void LatterTouchscreen::set_sleep_mode(bool sleep) {
  using P = TouchHalProtocol;
  if (!this->setup_complete_ || this->is_failed() || this->in_service_) return;
  if (this->protocol_ != P::FT6336 && this->protocol_ != P::FT3168) return;
  if (!this->focaltech_id_verified_) return;

  const int8_t want = sleep ? 1 : 0;
  // On wake (!sleep), always re-apply active scan registers so a prior touch hardware reset
  // or transient NACK cannot leave FocalTech stuck in low-power monitor scan.
  if (sleep && this->last_sleep_mode_ == want) return;

  if (this->write_byte(0x86, sleep ? 0x01 : 0x00)) {
    if (!sleep) {
      (void) this->write_byte(0xA4, 0x01);
      (void) this->write_byte(0x88, 0x06);
    }
    if (this->last_sleep_mode_ != want) {
      LATTER_PROF_LOGI("[PROF:TOUCH] monitor_mode=%d", static_cast<int>(want));
    }
    this->last_sleep_mode_ = want;
  } else {
    this->last_sleep_mode_ = -1;
  }
}

void LatterTouchscreen::recover_if_failed(uint32_t now_ms) {
  if (!this->recovery_policy_.should_attempt(this->is_failed(), now_ms)) return;
  ESP_LOGW(TAG, "Touch HAL (%s @ 0x%02X) failed at boot; re-running setup (attempt %u)",
           touch_hal_protocol_name(this->protocol_), this->address_,
           static_cast<unsigned>(this->recovery_policy_.attempts));
  this->status_clear_error();
  this->setup();
}

void LatterTouchscreen::prepare_for_restart() {
  this->cancel_timeout(kResetAssertTimeoutId);
  this->cancel_timeout(kResetReleaseTimeoutId);
  this->cancel_timeout(kPostResetSettleTimeoutId);
  this->cancel_timeout(kCstCmdSettleTimeoutId);
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(20);
    this->reset_pin_->digital_write(true);
  }
}

bool LatterTouchscreen::init_controller_() {
  using P = TouchHalProtocol;
  switch (this->protocol_) {
    case P::FT6336:
    case P::FT3168: {
      (void) this->write_byte(0x00, 0x00);  // Normal operating mode (FT5X06_OP_MODE)
      uint8_t cipher_id = 0;
      uint8_t vendor_id = 0;
      const bool got_cipher = this->read_byte(0xA3, &cipher_id);
      const bool got_vendor = this->read_byte(0xA8, &vendor_id);
      if (!got_cipher && !got_vendor) {
        return false;
      }
      this->focaltech_id_verified_ = got_cipher && is_known_focaltech_chip_id(cipher_id);
      if (got_cipher && !this->focaltech_id_verified_) {
        ESP_LOGW(TAG, "Touch HAL (%s @ 0x%02X) unexpected CIPHER 0x%02X; disabling monitor-mode writes",
                 touch_hal_protocol_name(this->protocol_), this->address_,
                 static_cast<unsigned>(cipher_id));
      }
      this->detected_chip_id_ = got_vendor ? vendor_id : cipher_id;
      // Program Trigger Pulse mode (0xA4 = 0x01), active scan mode (0x86 = 0x00), and ~100 Hz active period (0x88 = 0x06).
      (void) this->write_byte(0xA4, 0x01);
      (void) this->write_byte(0x86, 0x00);
      (void) this->write_byte(0x88, 0x06);
      this->last_sleep_mode_ = 0;
      return true;
    }

    case P::GT911: {
      uint8_t sw = 0x01U;
      if (this->read_register16(0x804D, &sw, 1) != i2c::ERROR_OK) {
        const uint8_t alt_addr = (this->address_ == 0x5D) ? 0x14 : 0x5D;
        this->set_i2c_address(alt_addr);
        if (this->read_register16(0x804D, &sw, 1) != i2c::ERROR_OK) {
          return false;
        }
      }
      // sw & 1 == 1 => falling edge (active-low); sw & 1 == 0 => rising edge (active-high)
      this->irq_active_high_ = ((sw & 0x01U) == 0U);
      this->detected_chip_id_ = 0x0911U;

      uint8_t prod[4]{};
      (void) this->read_register16(0x8140, prod, 4);

      uint8_t res[4]{};
      if (this->read_register16(0x8048, res, 4) == i2c::ERROR_OK) {
        const int16_t rx = static_cast<int16_t>(static_cast<uint16_t>(res[0]) | (static_cast<uint16_t>(res[1]) << 8U));
        const int16_t ry = static_cast<int16_t>(static_cast<uint16_t>(res[2]) | (static_cast<uint16_t>(res[3]) << 8U));
        if (rx > 10 && ry > 10 && this->x_raw_max_ == this->x_raw_min_ && this->y_raw_max_ == this->y_raw_min_) {
          this->x_raw_max_ = rx;
          this->y_raw_max_ = ry;
          if (this->swap_x_y_) {
            std::swap(this->x_raw_max_, this->y_raw_max_);
          }
        }
      }
      return true;
    }

    case P::CST9217:
      // Handled asynchronously in finish_setup_() -> finish_cst9217_setup_().
      return true;

    case P::CST816S: {
      uint8_t chip_id = 0;
      if (!this->read_byte(0xA7, &chip_id)) {
        return false;
      }
      this->detected_chip_id_ = chip_id;
      (void) this->write_byte(0xFA, 0x60);
      return true;
    }
  }
  return false;
}

void LatterTouchscreen::service_once_(uint32_t now) {
  this->in_service_ = true;
  const bool had_irq = this->irq_store_.irq_pending.exchange(false, std::memory_order_relaxed);
  this->poll_due_ = false;
  this->had_irq_this_pass_ = had_irq;

  auto &tel = touch_hal_telemetry();
  if (this->finger_down_ && this->last_service_ms_ != 0U) {
    const uint32_t gap = now - this->last_service_ms_;
    if (gap > tel.max_active_gap_ms) tel.max_active_gap_ms = gap;
  }
  this->last_service_ms_ = now;
  tel.last_sample_ms = now;

  this->first_touch_ = this->touches_.empty();
  this->need_update_ = false;
  this->is_touched_ = false;
  this->skip_update_ = false;

  for (auto &tp : this->touches_) {
    if (tp.second.state == touchscreen::STATE_PRESSED || tp.second.state == touchscreen::STATE_UPDATED) {
      tp.second.state |= touchscreen::STATE_RELEASING;
    } else {
      tp.second.state = touchscreen::STATE_RELEASED;
    }
    tp.second.x_prev = tp.second.x;
    tp.second.y_prev = tp.second.y;
  }

  this->update_touches();

  if (this->skip_update_) {
    for (auto &tp : this->touches_) {
      tp.second.state &= ~touchscreen::STATE_RELEASING;
    }
    this->in_service_ = false;
    return;
  }

  const bool was_down = this->finger_down_;
  this->finger_down_ = this->is_touched_;
  if (this->finger_down_ && !this->hf_held_) {
    this->touch_hf_.start();
    this->hf_held_ = true;
  } else if (!this->finger_down_ && this->hf_held_) {
    this->touch_hf_.stop();
    this->hf_held_ = false;
  }

  if (this->finger_down_) {
    tel.contacts++;
  } else if (was_down) {
    tel.releases++;
  }

  this->send_touches_();

  if (this->wake_pulse_active_) {
    this->wake_pulse_active_ = false;
    this->is_touched_ = false;
    this->finger_down_ = false;
    if (this->hf_held_) {
      this->touch_hf_.stop();
      this->hf_held_ = false;
    }
    for (auto &tp : this->touches_) {
      if (tp.second.state == touchscreen::STATE_PRESSED || tp.second.state == touchscreen::STATE_UPDATED) {
        tp.second.state |= touchscreen::STATE_RELEASING;
      } else {
        tp.second.state = touchscreen::STATE_RELEASED;
      }
    }
    this->send_touches_();
    for (auto &tp : this->touches_) {
      tp.second.state = touchscreen::STATE_RELEASED;
    }
  }

  this->in_service_ = false;
}

void LatterTouchscreen::update_touches() {
  using K = TouchSampleKind;
  using P = TouchHalProtocol;
  auto &tel = touch_hal_telemetry();
  tel.i2c_reads++;

  TouchDecodedSample dec{};

  switch (this->protocol_) {
    case P::FT6336:
    case P::FT3168: {
      uint8_t buf[5]{};
      if (!this->read_bytes(0x02, buf, 5)) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      dec = decode_focaltech_frame(buf);
      break;
    }

    case P::GT911: {
      uint8_t status = 0;
      if (this->read_register16(0x814E, &status, 1) != i2c::ERROR_OK) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      if ((status & 0x80U) == 0U) {
        this->skip_update_ = true;
        return;
      }
      const uint8_t ack_814e[3] = {0x81, 0x4E, 0x00};
      uint8_t pt[4]{};
      const uint8_t cnt = static_cast<uint8_t>(status & 0x07U);
      if (cnt >= 1U && cnt <= 5U) {
        if (this->read_register16(0x8150, pt, 4) != i2c::ERROR_OK) {
          tel.i2c_errors++;
          (void) this->write(ack_814e, 3);
          this->skip_update_ = true;
          return;
        }
      }
      dec = decode_gt911_frame(status, (cnt >= 1U && cnt <= 5U) ? pt : nullptr);
      if (dec.ack_buffer) {
        (void) this->write(ack_814e, 3);
      }
      break;
    }

    case P::CST9217: {
      // 7-byte report at 0xD000: 5 bytes point 0 + finger_count (0xD005) + 0xAB sync marker (0xD006).
      uint8_t buf[7]{};
      if (this->read_register16(0xD000, buf, 7) != i2c::ERROR_OK) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      dec = decode_cst9217_frame(buf);
      break;
    }

    case P::CST816S: {
      uint8_t buf[6]{};
      if (!this->read_bytes(0x01, buf, 6)) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      dec = decode_cst816s_frame(buf);
      break;
    }
  }

  if (dec.kind == K::Invalid || dec.kind == K::NotReady) {
    this->skip_update_ = true;
    return;
  }

  if (dec.kind == K::Contact) {
    this->add_raw_touch_position_(0, dec.raw_x, dec.raw_y);
    return;
  }

  if (!this->finger_down_ && this->had_irq_this_pass_ &&
      touch_wake_bridge().is_standby()) {
    const int16_t cx = static_cast<int16_t>(this->x_raw_max_ / 2);
    const int16_t cy = static_cast<int16_t>(this->y_raw_max_ / 2);
    this->wake_pulse_active_ = true;
    tel.wake_pulses++;
    LATTER_PROF_LOGI("[PROF:TOUCH] wake_pulse=1 proto=%s",
                     touch_hal_protocol_name(this->protocol_));
    this->add_raw_touch_position_(0, cx, cy);
  }
}

}  // namespace latter_core

#endif
