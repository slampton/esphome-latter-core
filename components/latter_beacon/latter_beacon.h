#pragma once

// Latter presence beacon: an iBeacon advertised with four raw HCI commands and no Bluetooth host stack.
// Two transports, chosen at build time:
//   - on-chip controller (ESP32-S3 and other chips with Bluetooth): ESP-IDF VHCI;
//   - esp_hosted co-processor (ESP32-P4 + C6): esp_hosted's custom-stack HCI byte pipe.
// Everything else (state machine, commands, payload, logs, control surface) is shared. See __init__.py.

#include "esphome/core/defines.h"
#include "esphome/core/component.h"

#include <array>
#include <cstdint>
#include <string_view>

#ifndef USE_LATTER_BEACON_HOSTED
#include <esp_bt.h>
#endif

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
#ifndef USE_LATTER_BEACON_HOSTED
  void set_tx_power(esp_power_level_t level) { this->tx_power_ = level; }
#endif

  /// Start the controller (if down) and enable advertising; resumes in <1 ms via LE_Set_Adv_Enable(1) when paused.
  void enable();
  /// Pause advertising in <1 ms via raw HCI LE_Set_Adv_Enable(0) while keeping the controller/HCI pipe warm.
  void pause();
  /// Stop advertising and shut the controller down, returning its memory.
  void disable();
  /// True while logically active (from enable() until pause(), disable(), or start failure).
  bool is_active() const { return this->active_; }
  /// True while advertising is paused with the BT controller / esp_hosted HCI pipe kept initialized.
  bool is_paused() const { return this->paused_; }
  /// True whenever the BT controller / esp_hosted HCI pipe is initialized (active or paused).
  bool is_transport_up() const { return this->transport_up_; }
  /// True for a minute after a failed start, so the power policy does not retry every second.
  bool is_failed() const;
  /// [PROF:BLE] heap lines on/off (follows the Telemetry Profiler switch).
  void set_profile_logging(bool on) { this->profile_logging_ = on; }
  /// Update the 128-bit iBeacon UUID from a canonical hex/UUID string, persist in NVS if changed, and refresh if up.
  bool set_uuid_hex(std::string_view uuid_str);

 protected:
  // Transport (one implementation per build).
  int transport_up_();
  void transport_down_();
  bool transport_can_send_();
  bool transport_send_(const uint8_t *frame, uint16_t len);

  bool send_step_(uint8_t step);
  bool send_adv_enable_(bool enable, uint32_t now_ms);
  bool harvest_cmd_complete_();
  void fail_(const char *what, int err);
  void stop_(const char *log_what);
  void log_internal_heap_(const char *phase);

  std::array<uint8_t, 16> uuid_;
  uint16_t major_{1};
  uint16_t minor_{0};
  int8_t measured_power_{-59};
  uint32_t min_interval_ms_{500};
  uint32_t max_interval_ms_{1000};
#ifndef USE_LATTER_BEACON_HOSTED
  esp_power_level_t tx_power_{ESP_PWR_LVL_P3};
#endif

  bool transport_up_{false};
  bool active_{false};
  bool paused_{false};
  bool configured_{false};
  bool adv_enabled_{false};
  uint8_t step_{0};
  bool cmd_in_flight_{false};
  uint16_t cmd_opcode_{0};
  uint32_t cmd_sent_ms_{0};
  uint32_t retry_after_ms_{0};
  bool failed_{false};
  bool profile_logging_{false};
};

}  // namespace esphome::latter_beacon
