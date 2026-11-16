#include "hal_touch.h"

#include "esphome/core/defines.h"

#if defined(USE_TOUCHSCREEN) && defined(USE_I2C) && \
    __has_include("esphome/components/touchscreen/touchscreen.h") && \
    __has_include("esphome/components/i2c/i2c.h")

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "hal_panel_power.h"
#include "hal_power_pm.h"
#include "latter_prof.h"
#include "latter_state.h"

namespace esphome::latter_core {

namespace {
constexpr const char *TAG = "latter.touch";
}  // namespace

void IRAM_ATTR LatterTouchIrqStore::gpio_intr(LatterTouchIrqStore *store) {
  store->irq_pending = true;
  store->edge_count = store->edge_count + 1U;
#if defined(USE_ESP32)
  App.wake_loop_threadsafe();
#endif
}

void LatterTouchscreen::set_protocol(::latter_ui::TouchHalProtocol proto) {
  this->protocol_ = proto;
  if (!this->active_poll_explicit_) {
    this->active_poll_ms_ = ::latter_ui::default_active_poll_ms(proto);
  }
}

void LatterTouchscreen::set_active_poll_ms(uint16_t ms) {
  this->active_poll_ms_ = (ms < 4U) ? 4U : ms;
  this->active_poll_explicit_ = true;
}

float LatterTouchscreen::get_setup_priority() const { return setup_priority::DATA - 1.0f; }

void LatterTouchscreen::setup() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delay(15);
    this->reset_pin_->digital_write(true);
    delay(50);
  }

  if (this->x_raw_max_ == this->x_raw_min_) {
    this->x_raw_max_ = this->swap_x_y_ ? this->display_height_ : this->display_width_;
  }
  if (this->y_raw_max_ == this->y_raw_min_) {
    this->y_raw_max_ = this->swap_x_y_ ? this->display_width_ : this->display_height_;
  }

  if (!this->init_controller_()) {
    ESP_LOGW(TAG, "Touch controller (%s @ 0x%02X) did not ACK at setup; will retry via HAL recovery",
             ::latter_ui::touch_hal_protocol_name(this->protocol_), this->address_);
    this->mark_failed();
    return;
  }

  if (this->interrupt_pin_ != nullptr) {
    this->interrupt_pin_->setup();
    this->interrupt_pin_->attach_interrupt(LatterTouchIrqStore::gpio_intr, &this->irq_store_,
                                           gpio::INTERRUPT_FALLING_EDGE);
    this->store_.init = true;
    this->stop_poller();
  }

  ::latter_ui::touch_pump_bridge().bind(
      this, +[](void *ctx) { static_cast<LatterTouchscreen *>(ctx)->pump_from_host(); });

  ESP_LOGI(TAG, "Latter Touch HAL ready: proto=%s addr=0x%02X mode=%s active_poll=%ums raw=%dx%d",
           ::latter_ui::touch_hal_protocol_name(this->protocol_), this->address_,
           (this->interrupt_pin_ != nullptr) ? "irq+active_poll" : "polled",
           static_cast<unsigned>(this->active_poll_ms_),
           static_cast<int>(this->x_raw_max_), static_cast<int>(this->y_raw_max_));
}

void LatterTouchscreen::update() {
  if (this->interrupt_pin_ == nullptr) {
    this->poll_due_ = true;
  }
}

void LatterTouchscreen::pump_from_host() {
  if (this->is_failed() || this->in_service_) return;
  const uint32_t now = millis();
  const bool has_irq = this->irq_store_.irq_pending;
  const bool poll_ready =
      (this->finger_down_ || this->interrupt_pin_ == nullptr) &&
      (this->last_service_ms_ == 0U || (now - this->last_service_ms_) >= this->active_poll_ms_);
  if (has_irq || poll_ready) {
    this->service_once_(now);
  }
}

void LatterTouchscreen::loop() {
  if (this->is_failed() || this->in_service_) return;
  const uint32_t now = millis();

  const uint32_t edges = this->irq_store_.edge_count;
  if (edges != this->last_edge_snapshot_) {
    ::latter_ui::touch_hal_telemetry().irq_edges += (edges - this->last_edge_snapshot_);
    this->last_edge_snapshot_ = edges;
    if (::latter_ui::runtime_state().nav.display_power_state != 0) {
      ::latter_ui::pm_wake_grace_arm(now);
    }
  }

  const bool has_irq = this->irq_store_.irq_pending;
  const bool int_held_low =
      (this->interrupt_pin_ != nullptr) && (!this->interrupt_pin_->digital_read());
  const bool active_due =
      (this->finger_down_ || (this->interrupt_pin_ != nullptr && int_held_low) ||
       this->interrupt_pin_ == nullptr || this->poll_due_) &&
      (this->last_service_ms_ == 0U || (now - this->last_service_ms_) >= this->active_poll_ms_);

  if (has_irq || active_due) {
    this->service_once_(now);
  }
}

bool LatterTouchscreen::init_controller_() {
  using P = ::latter_ui::TouchHalProtocol;
  switch (this->protocol_) {
    case P::FT6336:
    case P::FT3168: {
      uint8_t chip_id = 0;
      if (!this->read_byte(0xA3, &chip_id) && !this->read_byte(0xA8, &chip_id)) {
        return false;
      }
      // Program Trigger Pulse mode (0xA4 = 0x01), active scan mode (0x86 = 0x00), and ~100 Hz active period (0x88 = 0x06).
      (void) this->write_byte(0xA4, 0x01);
      (void) this->write_byte(0x86, 0x00);
      (void) this->write_byte(0x88, 0x06);
      return true;
    }

    case P::GT911: {
      uint8_t prod[4]{};
      if (this->read_register16(0x8140, prod, 4) != i2c::ERROR_OK) {
        const uint8_t alt_addr = (this->address_ == 0x5D) ? 0x14 : 0x5D;
        this->set_i2c_address(alt_addr);
        if (this->read_register16(0x8140, prod, 4) != i2c::ERROR_OK) {
          return false;
        }
      }
      uint8_t res[4]{};
      if (this->read_register16(0x8048, res, 4) == i2c::ERROR_OK) {
        const int16_t rx = static_cast<int16_t>(static_cast<uint16_t>(res[0]) | (static_cast<uint16_t>(res[1]) << 8U));
        const int16_t ry = static_cast<int16_t>(static_cast<uint16_t>(res[2]) | (static_cast<uint16_t>(res[3]) << 8U));
        if (rx > 10 && ry > 10 && this->x_raw_min_ == 0 && this->y_raw_min_ == 0) {
          this->x_raw_max_ = this->swap_x_y_ ? ry : rx;
          this->y_raw_max_ = this->swap_x_y_ ? rx : ry;
        }
      }
      return true;
    }

    case P::CST9217: {
      const uint8_t cmd_mode[2] = {0xD1, 0x01};
      if (this->write(cmd_mode, 2) != i2c::ERROR_OK) {
        return false;
      }
      delay(10);
      uint8_t info[4]{};
      (void) this->read_register16(0xD1FC, info, 4);
      uint8_t res[4]{};
      if (this->read_register16(0xD1F8, res, 4) == i2c::ERROR_OK) {
        const int16_t rx = static_cast<int16_t>(static_cast<uint16_t>(res[0]) | (static_cast<uint16_t>(res[1]) << 8U));
        const int16_t ry = static_cast<int16_t>(static_cast<uint16_t>(res[2]) | (static_cast<uint16_t>(res[3]) << 8U));
        if (rx > 10 && ry > 10 && this->x_raw_min_ == 0 && this->y_raw_min_ == 0) {
          this->x_raw_max_ = this->swap_x_y_ ? ry : rx;
          this->y_raw_max_ = this->swap_x_y_ ? rx : ry;
        }
      }
      const uint8_t normal_mode[2] = {0xD1, 0x09};
      (void) this->write(normal_mode, 2);
      return true;
    }

    case P::CST816S: {
      uint8_t chip_id = 0;
      if (!this->read_byte(0xA7, &chip_id)) {
        return false;
      }
      (void) this->write_byte(0xFA, 0x60);
      return true;
    }
  }
  return false;
}

void LatterTouchscreen::service_once_(uint32_t now) {
  this->in_service_ = true;
  const bool had_irq = this->irq_store_.irq_pending;
  this->irq_store_.irq_pending = false;
  this->poll_due_ = false;
  this->had_irq_this_pass_ = had_irq;

  auto &tel = ::latter_ui::touch_hal_telemetry();
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
    this->send_touches_();
  }

  this->in_service_ = false;
}

void LatterTouchscreen::update_touches() {
  using K = ::latter_ui::TouchSampleKind;
  using P = ::latter_ui::TouchHalProtocol;
  auto &tel = ::latter_ui::touch_hal_telemetry();
  tel.i2c_reads++;

  ::latter_ui::TouchDecodedSample dec{};

  switch (this->protocol_) {
    case P::FT6336:
    case P::FT3168: {
      uint8_t buf[5]{};
      if (!this->read_bytes(0x02, buf, 5)) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      dec = ::latter_ui::decode_focaltech_frame(buf);
      break;
    }

    case P::GT911: {
      uint8_t status = 0;
      if (!this->read_byte_reg16(0x814E, &status)) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      if ((status & 0x80U) == 0U) {
        this->skip_update_ = true;
        return;
      }
      uint8_t pt[4]{};
      const uint8_t cnt = static_cast<uint8_t>(status & 0x0FU);
      if (cnt >= 1U && cnt <= 5U) {
        if (this->read_register16(0x8150, pt, 4) != i2c::ERROR_OK) {
          tel.i2c_errors++;
          (void) this->write_byte_reg16(0x814E, 0x00);
          this->skip_update_ = true;
          return;
        }
      }
      dec = ::latter_ui::decode_gt911_frame(status, (cnt >= 1U && cnt <= 5U) ? pt : nullptr);
      if (dec.ack_buffer) {
        (void) this->write_byte_reg16(0x814E, 0x00);
      }
      break;
    }

    case P::CST9217: {
      uint8_t buf[5]{};
      if (this->read_register16(0xD000, buf, 5) != i2c::ERROR_OK) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      const uint8_t sync_ack[3] = {0xD0, 0x00, 0xAB};
      (void) this->write(sync_ack, 3);
      dec = ::latter_ui::decode_cst9217_frame(buf);
      break;
    }

    case P::CST816S: {
      uint8_t buf[6]{};
      if (!this->read_bytes(0x01, buf, 6)) {
        tel.i2c_errors++;
        this->skip_update_ = true;
        return;
      }
      dec = ::latter_ui::decode_cst816s_frame(buf);
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
      ::latter_ui::runtime_state().nav.display_power_state != 0) {
    const int16_t cx = static_cast<int16_t>(this->x_raw_max_ / 2);
    const int16_t cy = static_cast<int16_t>(this->y_raw_max_ / 2);
    this->wake_pulse_active_ = true;
    tel.wake_pulses++;
    LATTER_PROF_LOGI("[PROF:TOUCH] wake_pulse=1 proto=%s",
                     ::latter_ui::touch_hal_protocol_name(this->protocol_));
    this->add_raw_touch_position_(0, cx, cy);
  }
}

}  // namespace esphome::latter_core

#endif
