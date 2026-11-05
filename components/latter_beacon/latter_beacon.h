#pragma once

// Latter presence beacon: an iBeacon advertised straight through the Bluetooth controller's HCI (VHCI),
// with no Bluetooth host stack. See __init__.py for why.

#include "esphome/core/component.h"

#include <array>
#include <atomic>
#include <cstdint>

#include <esp_bt.h>

namespace esphome::latter_beacon {

class LatterBeacon : public Component {
 public:
  explicit LatterBeacon(const std::array<uint8_t, 16> &uuid) : uuid_(uuid) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_major(uint16_t major) { this->major_ = major; }
  void set_minor(uint16_t minor) { this->minor_ = minor; }
  void set_measured_power(int8_t power) { this->measured_power_ = power; }
  void set_min_interval(uint32_t ms) { this->min_interval_ms_ = ms; }
  void set_max_interval(uint32_t ms) { this->max_interval_ms_ = ms; }
  void set_tx_power(esp_power_level_t level) { this->tx_power_ = level; }

  /// Start the controller and advertising (asynchronous: the HCI commands finish in loop()).
  void enable();
  /// Stop advertising and shut the controller down, returning its memory.
  void disable();
  /// True from enable() until disable(), including while the HCI commands are still running.
  bool is_active() const { return this->state_ != State::OFF; }
  /// True for a minute after a failed start, so the power policy does not retry every second.
  bool is_failed() const;

 protected:
  enum class State : uint8_t { OFF, CONFIGURING, ADVERTISING };

  bool send_step_(uint8_t step);
  void fail_(const char *what, int err);
  void shutdown_controller_();

  std::array<uint8_t, 16> uuid_;
  uint16_t major_{1};
  uint16_t minor_{0};
  int8_t measured_power_{-59};
  uint32_t min_interval_ms_{500};
  uint32_t max_interval_ms_{1000};
  esp_power_level_t tx_power_{ESP_PWR_LVL_P3};

  State state_{State::OFF};
  uint8_t step_{0};
  bool step_sent_{false};
  uint32_t step_sent_ms_{0};
  uint32_t retry_after_ms_{0};
  bool failed_{false};
};

}  // namespace esphome::latter_beacon
