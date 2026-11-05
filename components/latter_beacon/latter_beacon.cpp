#include "latter_beacon.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome::latter_beacon {

static const char *const TAG = "latter_beacon";

static constexpr uint32_t STEP_TIMEOUT_MS = 1000;
static constexpr uint32_t RETRY_AFTER_FAILURE_MS = 60000;
static constexpr uint8_t STEP_COUNT = 4;  // reset, advertising parameters, advertising data, advertising enable

// HCI events arrive on the controller's task: only flags cross over.
static std::atomic<bool> s_cmd_complete{false};
static std::atomic<uint8_t> s_cmd_status{0};

static void vhci_send_available() {}

static int vhci_recv(uint8_t *data, uint16_t len) {
  // H4 event packet: [0x04][event code][param len][...]. Command Complete (0x0E) params:
  // [num packets][opcode lo][opcode hi][status ...]
  if (len >= 7 && data[0] == 0x04 && data[1] == 0x0E) {
    s_cmd_status.store(data[6]);
    s_cmd_complete.store(true);
  }
  return 0;
}

static esp_vhci_host_callback_t s_vhci_callbacks = {vhci_send_available, vhci_recv};

// Advertising interval in 0.625 ms units.
static uint16_t adv_units(uint32_t ms) {
  return static_cast<uint16_t>(std::clamp<uint32_t>(ms * 8U / 5U, 0x00A0U, 0x4000U));
}

void LatterBeacon::setup() { this->disable_loop(); }

bool LatterBeacon::is_failed() const { return this->failed_ && millis() < this->retry_after_ms_; }

void LatterBeacon::enable() {
  if (this->state_ != State::OFF || this->is_failed())
    return;
  this->failed_ = false;
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_bt_controller_init(&cfg);
  if (err != ESP_OK) {
    this->fail_("controller init", err);
    return;
  }
  err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (err != ESP_OK) {
    esp_bt_controller_deinit();
    this->fail_("controller enable", err);
    return;
  }
  err = esp_vhci_host_register_callback(&s_vhci_callbacks);
  if (err != ESP_OK) {
    this->shutdown_controller_();
    this->fail_("VHCI callback", err);
    return;
  }
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, this->tx_power_);
  this->state_ = State::CONFIGURING;
  this->step_ = 0;
  this->step_sent_ = false;
  this->enable_loop();
}

void LatterBeacon::disable() {
  if (this->state_ == State::OFF)
    return;
  this->shutdown_controller_();
  this->state_ = State::OFF;
  this->disable_loop();
  ESP_LOGD(TAG, "Advertising stopped, controller released");
}

void LatterBeacon::loop() {
  if (this->state_ != State::CONFIGURING) {
    this->disable_loop();
    return;
  }
  const uint32_t now = millis();
  if (this->step_sent_) {
    if (!s_cmd_complete.load()) {
      if (now - this->step_sent_ms_ > STEP_TIMEOUT_MS) {
        this->shutdown_controller_();
        this->state_ = State::OFF;
        this->fail_("HCI command timeout, step", this->step_);
      }
      return;
    }
    const uint8_t status = s_cmd_status.load();
    if (status != 0) {
      this->shutdown_controller_();
      this->state_ = State::OFF;
      this->fail_("HCI command rejected, status", status);
      return;
    }
    this->step_++;
    this->step_sent_ = false;
    if (this->step_ >= STEP_COUNT) {
      this->state_ = State::ADVERTISING;
      this->disable_loop();
      ESP_LOGD(TAG, "Advertising (major %u, minor %u)", this->major_, this->minor_);
      return;
    }
  }
  if (!esp_vhci_host_check_send_available())
    return;
  s_cmd_complete.store(false);
  if (this->send_step_(this->step_)) {
    this->step_sent_ = true;
    this->step_sent_ms_ = now;
  }
}

bool LatterBeacon::send_step_(uint8_t step) {
  switch (step) {
    case 0: {  // HCI_Reset
      uint8_t pkt[] = {0x01, 0x03, 0x0C, 0x00};
      esp_vhci_host_send_packet(pkt, sizeof(pkt));
      return true;
    }
    case 1: {  // LE Set Advertising Parameters: non-connectable undirected, public address, all channels
      const uint16_t min_u = adv_units(this->min_interval_ms_);
      const uint16_t max_u = adv_units(this->max_interval_ms_);
      uint8_t pkt[] = {0x01, 0x06, 0x20, 15,
                       static_cast<uint8_t>(min_u & 0xFF), static_cast<uint8_t>(min_u >> 8),
                       static_cast<uint8_t>(max_u & 0xFF), static_cast<uint8_t>(max_u >> 8),
                       0x03,  // ADV_NONCONN_IND
                       0x00,  // own address: public
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // peer address type + address (unused)
                       0x07,  // channels 37, 38, 39
                       0x00};  // no filter
      esp_vhci_host_send_packet(pkt, sizeof(pkt));
      return true;
    }
    case 2: {  // LE Set Advertising Data: Flags + Apple iBeacon manufacturer data (30 of 31 bytes)
      uint8_t pkt[4 + 32] = {0x01, 0x08, 0x20, 32, 30,
                             0x02, 0x01, 0x06,                     // Flags: LE General Discoverable, BR/EDR unsupported
                             0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15};  // Manufacturer data: Apple, iBeacon, 21 bytes
      size_t pos = 14;
      std::memcpy(pkt + pos, this->uuid_.data(), this->uuid_.size());
      pos += this->uuid_.size();
      pkt[pos++] = static_cast<uint8_t>(this->major_ >> 8);
      pkt[pos++] = static_cast<uint8_t>(this->major_ & 0xFF);
      pkt[pos++] = static_cast<uint8_t>(this->minor_ >> 8);
      pkt[pos++] = static_cast<uint8_t>(this->minor_ & 0xFF);
      pkt[pos++] = static_cast<uint8_t>(this->measured_power_);
      esp_vhci_host_send_packet(pkt, sizeof(pkt));
      return true;
    }
    case 3: {  // LE Set Advertising Enable
      uint8_t pkt[] = {0x01, 0x0A, 0x20, 0x01, 0x01};
      esp_vhci_host_send_packet(pkt, sizeof(pkt));
      return true;
    }
    default:
      return false;
  }
}

void LatterBeacon::shutdown_controller_() {
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
    esp_bt_controller_disable();
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
    esp_bt_controller_deinit();
}

void LatterBeacon::fail_(const char *what, int err) {
  ESP_LOGE(TAG, "Beacon start failed: %s (%d); retrying in %u s", what, err,
           static_cast<unsigned>(RETRY_AFTER_FAILURE_MS / 1000U));
  this->failed_ = true;
  this->retry_after_ms_ = millis() + RETRY_AFTER_FAILURE_MS;
  this->disable_loop();
}

void LatterBeacon::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Latter Beacon (iBeacon, controller only):\n"
                "  Major: %u, Minor: %u, Measured power: %d dBm\n"
                "  Interval: %u-%u ms",
                this->major_, this->minor_, this->measured_power_,
                static_cast<unsigned>(this->min_interval_ms_), static_cast<unsigned>(this->max_interval_ms_));
}

}  // namespace esphome::latter_beacon
