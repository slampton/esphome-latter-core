#include "latter_beacon.h"

#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include <esp_heap_caps.h>
#include <nvs.h>
#include <nvs_flash.h>

#ifdef USE_LATTER_BEACON_HOSTED
#include "esp_hosted_bt_host_stack.h"
#endif

namespace esphome::latter_beacon {

static const char *const TAG = "latter_beacon";

static constexpr uint32_t STEP_TIMEOUT_MS = 1000;
static constexpr uint32_t RETRY_AFTER_FAILURE_MS = 60000;
static constexpr uint8_t CONFIG_STEP_COUNT = 3;  // reset, advertising parameters, advertising data

static constexpr uint16_t HCI_OP_RESET = 0x0C03;
static constexpr uint16_t HCI_OP_LE_SET_ADV_PARAMS = 0x2006;
static constexpr uint16_t HCI_OP_LE_SET_ADV_DATA = 0x2008;
static constexpr uint16_t HCI_OP_LE_SET_ADV_ENABLE = 0x200A;

static constexpr uint16_t step_opcode(uint8_t step) {
  switch (step) {
    case 0:
      return HCI_OP_RESET;
    case 1:
      return HCI_OP_LE_SET_ADV_PARAMS;
    case 2:
      return HCI_OP_LE_SET_ADV_DATA;
    default:
      return HCI_OP_LE_SET_ADV_ENABLE;
  }
}

// [PROF:BLE] internal RAM (bytes) at the controller's lifecycle points: pre_enable minus advertising is
// what the running controller holds; pre_enable minus disabled is what it did not give back. Logged only
// while the Telemetry Profiler is on (set_profile_logging), like every other [PROF:*] INFO line.
void LatterBeacon::log_internal_heap_(const char *phase) {
  if (!this->profile_logging_)
    return;
  ESP_LOGI(TAG, "[PROF:BLE] phase=%s int_free=%u int_max_blk=%u", phase,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

// HCI events arrive on the controller's (or the hosted transport's) task: only atomics cross over.
static std::atomic<bool> s_cmd_complete{false};
static std::atomic<uint8_t> s_cmd_status{0};
static std::atomic<uint16_t> s_cmd_opcode{0};

// One H4 HCI frame from the controller. Command Complete: [0x04][0x0E][len][num pkts][opcode lo][opcode hi][status]
static void on_hci_frame(const uint8_t *frame, uint16_t len) {
  if (len >= 7 && frame[0] == 0x04 && frame[1] == 0x0E) {
    const uint16_t opcode = static_cast<uint16_t>(frame[4]) | (static_cast<uint16_t>(frame[5]) << 8);
    s_cmd_opcode.store(opcode, std::memory_order_relaxed);
    s_cmd_status.store(frame[6], std::memory_order_relaxed);
    s_cmd_complete.store(true, std::memory_order_release);
  }
}

// ---------------------------------------------------------------------------------------------------
// Transport: on-chip controller over VHCI
// ---------------------------------------------------------------------------------------------------
#ifndef USE_LATTER_BEACON_HOSTED

static void vhci_send_available() {}
static int vhci_recv(uint8_t *data, uint16_t len) {
  on_hci_frame(data, len);
  return 0;
}
static esp_vhci_host_callback_t s_vhci_callbacks = {vhci_send_available, vhci_recv};

int LatterBeacon::transport_up_() {
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_bt_controller_init(&cfg);
  if (err != ESP_OK)
    return err;
  err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (err == ESP_OK)
    err = esp_vhci_host_register_callback(&s_vhci_callbacks);
  if (err != ESP_OK) {
    this->transport_down_();
    return err;
  }
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, this->tx_power_);
  return ESP_OK;
}

void LatterBeacon::transport_down_() {
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
    esp_bt_controller_disable();
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED)
    esp_bt_controller_deinit();
}

bool LatterBeacon::transport_can_send_() { return esp_vhci_host_check_send_available(); }

bool LatterBeacon::transport_send_(const uint8_t *frame, uint16_t len) {
  esp_vhci_host_send_packet(const_cast<uint8_t *>(frame), len);
  return true;
}

// ---------------------------------------------------------------------------------------------------
// Transport: esp_hosted co-processor (custom-stack HCI byte pipe; no Bluedroid or NimBLE on the host)
// ---------------------------------------------------------------------------------------------------
#else

static esp_hosted_bt_hci_tx_fn_t s_hosted_tx = nullptr;
static void hosted_rx(const uint8_t *frame, uint16_t len, void * /*ctx*/) { on_hci_frame(frame, len); }

int LatterBeacon::transport_up_() {
  esp_hosted_bt_host_stack_cfg_t cfg{};
  cfg.stack = ESP_HOSTED_BT_HOST_STACK_CUSTOM;
  cfg.bt_mac = nullptr;  // the co-processor's own Bluetooth address
  cfg.bring_up_controller = true;
  cfg.controller_ready_timeout_ms = 2000;
  cfg.custom.rx = hosted_rx;
  cfg.custom.ctx = nullptr;
  const esp_err_t err = esp_hosted_bt_host_stack_setup(&cfg);
  if (err != ESP_OK || cfg.custom.tx == nullptr) {
    esp_hosted_bt_host_stack_teardown();
    return err != ESP_OK ? err : ESP_FAIL;
  }
  s_hosted_tx = cfg.custom.tx;
  return ESP_OK;
}

void LatterBeacon::transport_down_() {
  s_hosted_tx = nullptr;
  esp_hosted_bt_host_stack_teardown();
}

bool LatterBeacon::transport_can_send_() { return s_hosted_tx != nullptr; }

bool LatterBeacon::transport_send_(const uint8_t *frame, uint16_t len) {
  return s_hosted_tx != nullptr && s_hosted_tx(frame, len) == 0;
}

#endif

// ---------------------------------------------------------------------------------------------------
// Shared: state machine, HCI commands, iBeacon payload
// ---------------------------------------------------------------------------------------------------

// Advertising interval in 0.625 ms units (legacy non-connectable: 100 ms to 10.24 s).
static uint16_t adv_units(uint32_t ms) {
  return static_cast<uint16_t>(std::clamp<uint32_t>(ms * 8U / 5U, 0x00A0U, 0x4000U));
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

void LatterBeacon::setup() {
  nvs_handle_t h = 0;
  if (nvs_open("latter_cfg", NVS_READONLY, &h) == ESP_OK) {
    std::array<uint8_t, 16> stored{};
    size_t sz = stored.size();
    if (nvs_get_blob(h, "ble_uuid", stored.data(), &sz) == ESP_OK && sz == stored.size()) {
      this->uuid_ = stored;
    }
    nvs_close(h);
  }
  this->disable_loop();
}

bool LatterBeacon::set_uuid_hex(std::string_view uuid_str) {
  std::array<uint8_t, 16> parsed{};
  size_t nibbles = 0;
  int hi = -1;
  for (char ch : uuid_str) {
    if (ch == '-' || ch == ' ') continue;
    int v = hex_nibble(ch);
    if (v < 0 || nibbles >= 32) return false;
    if ((nibbles & 1U) == 0U) {
      hi = v;
    } else {
      parsed[nibbles >> 1U] = static_cast<uint8_t>((hi << 4) | v);
    }
    ++nibbles;
  }
  if (nibbles != 32) return false;
  if (parsed == this->uuid_) return true;

  this->uuid_ = parsed;
  nvs_handle_t h = 0;
  if (nvs_open("latter_cfg", NVS_READWRITE, &h) == ESP_OK) {
    if (nvs_set_blob(h, "ble_uuid", this->uuid_.data(), this->uuid_.size()) == ESP_OK) {
      nvs_commit(h);
    }
    nvs_close(h);
  }
  if (this->transport_ready_) {
    if (this->active_) {
      this->pause();
      if (this->configured_) {
        this->configured_ = false;
        this->step_ = 2;  // re-send LE_Set_Adv_Data before LE_Set_Adv_Enable(1)
      }
      this->enable();
    } else if (this->paused_ && this->configured_) {
      this->configured_ = false;
      this->step_ = 2;
    }
  }
  return true;
}

bool LatterBeacon::is_failed() const { return this->failed_ && millis() < this->retry_after_ms_; }

bool LatterBeacon::send_adv_enable_(bool enable, uint32_t now_ms) {
  const uint8_t pkt[] = {0x01, 0x0A, 0x20, 0x01, static_cast<uint8_t>(enable ? 0x01 : 0x00)};
  s_cmd_complete.store(false, std::memory_order_relaxed);
  if (!this->transport_send_(pkt, sizeof(pkt)))
    return false;
  this->adv_enabled_ = enable;
  this->cmd_in_flight_ = true;
  this->cmd_opcode_ = HCI_OP_LE_SET_ADV_ENABLE;
  this->cmd_sent_ms_ = now_ms;
  return true;
}

bool LatterBeacon::harvest_cmd_complete_() {
  const uint16_t opcode = s_cmd_opcode.load(std::memory_order_relaxed);
  const uint8_t status = s_cmd_status.load(std::memory_order_relaxed);
  s_cmd_complete.store(false, std::memory_order_relaxed);
  if (opcode != this->cmd_opcode_) {
    return false;
  }
  this->cmd_in_flight_ = false;
  this->cmd_opcode_ = 0;

  // HCI 0x0C (Command Disallowed) on LE_Set_Adv_Enable means advertising was already in that state.
  const bool benign_disallowed = (opcode == HCI_OP_LE_SET_ADV_ENABLE && status == 0x0C);
  if (status != 0 && !benign_disallowed) {
    this->stop_(nullptr);
    this->fail_("HCI command rejected, status", status);
    return false;
  }
  if (!this->configured_ && opcode != HCI_OP_LE_SET_ADV_ENABLE) {
    this->step_++;
    if (this->step_ >= CONFIG_STEP_COUNT) {
      this->configured_ = true;
    }
  }
  return true;
}

void LatterBeacon::enable() {
  if (this->active_ || this->is_failed())
    return;
  this->failed_ = false;

  // Warm resume path (<1 ms): controller / esp_hosted pipe is already up from a prior pause().
  if (this->transport_ready_) {
    const bool was_paused = this->paused_;
    this->paused_ = false;
    this->active_ = true;
    if (this->cmd_in_flight_ && s_cmd_complete.load(std::memory_order_acquire)) {
      this->harvest_cmd_complete_();
      if (!this->transport_ready_)
        return;
    }
    if (was_paused && !this->cmd_in_flight_ && this->configured_ && !this->adv_enabled_ &&
        this->transport_can_send_()) {
      // Skip transport_up_(), HCI_Reset, Set_Adv_Params, and Set_Adv_Data: send ONLY LE_Set_Adv_Enable(0x01).
      this->send_adv_enable_(true, millis());
    }
    this->enable_loop();
    return;
  }

  this->log_internal_heap_("pre_enable");
  const int err = this->transport_up_();
  if (err != 0) {
    this->fail_("controller start", err);
    return;
  }
  this->transport_ready_ = true;
  this->active_ = true;
  this->paused_ = false;
  this->configured_ = false;
  this->adv_enabled_ = false;
  this->step_ = 0;
  this->cmd_in_flight_ = false;
  this->cmd_opcode_ = 0;
  this->enable_loop();
}

void LatterBeacon::pause() {
  if (!this->transport_ready_ || this->paused_)
    return;
  this->paused_ = true;
  this->active_ = false;

  if (this->cmd_in_flight_ && s_cmd_complete.load(std::memory_order_acquire)) {
    this->harvest_cmd_complete_();
    if (!this->transport_ready_)
      return;
  }

  // Fast path (<1 ms, zero heap churn, zero P4 2000ms RPC block): send raw HCI LE_Set_Adv_Enable(0x00)
  // immediately while keeping the BT controller / esp_hosted HCI pipe initialized; loop() stays armed for
  // the single <1 ms tick to harvest the 0x200A Command Complete ack and then calls disable_loop().
  if (!this->cmd_in_flight_) {
    if (!this->adv_enabled_) {
      this->disable_loop();
      ESP_LOGD(TAG, "Advertising paused (controller kept warm)");
      return;
    }
    if (this->transport_can_send_()) {
      this->send_adv_enable_(false, millis());
    }
  }
  this->enable_loop();
}

void LatterBeacon::disable() {
  if (!this->transport_ready_ && !this->active_ && !this->paused_)
    return;
  if (this->transport_ready_ && this->adv_enabled_ && this->transport_can_send_()) {
    const uint8_t pkt[] = {0x01, 0x0A, 0x20, 0x01, 0x00};
    this->transport_send_(pkt, sizeof(pkt));
  }
  this->stop_("Advertising stopped, controller released");
}

void LatterBeacon::stop_(const char *log_what) {
  if (this->transport_ready_) {
    this->transport_down_();
    this->log_internal_heap_("disabled");
  }
  this->transport_ready_ = false;
  this->active_ = false;
  this->paused_ = false;
  this->configured_ = false;
  this->adv_enabled_ = false;
  this->step_ = 0;
  this->cmd_in_flight_ = false;
  this->cmd_opcode_ = 0;
  s_cmd_complete.store(false, std::memory_order_relaxed);
  this->disable_loop();
  if (log_what != nullptr) {
    ESP_LOGD(TAG, "%s", log_what);
  }
}

void LatterBeacon::loop() {
  if (!this->transport_ready_) {
    this->disable_loop();
    return;
  }
  const uint32_t now = millis();
  if (this->cmd_in_flight_) {
    if (!s_cmd_complete.load(std::memory_order_acquire)) {
      if (now - this->cmd_sent_ms_ > STEP_TIMEOUT_MS) {
        const uint8_t failed_step = this->step_;
        this->stop_(nullptr);
        this->fail_("HCI command timeout, step", failed_step);
      }
      return;
    }
    if (!this->harvest_cmd_complete_())
      return;
  }

  if (this->paused_) {
    if (this->adv_enabled_) {
      if (this->transport_can_send_()) {
        this->send_adv_enable_(false, now);
      }
      return;
    }
    this->disable_loop();
    ESP_LOGD(TAG, "Advertising paused (controller kept warm)");
    return;
  }

  if (!this->active_) {
    this->disable_loop();
    return;
  }

  if (!this->configured_) {
    if (!this->transport_can_send_())
      return;
    s_cmd_complete.store(false, std::memory_order_relaxed);
    if (this->send_step_(this->step_)) {
      this->cmd_in_flight_ = true;
      this->cmd_opcode_ = step_opcode(this->step_);
      this->cmd_sent_ms_ = now;
    }
    return;
  }

  if (!this->adv_enabled_) {
    if (this->transport_can_send_()) {
      this->send_adv_enable_(true, now);
    }
    return;
  }

  this->disable_loop();
  ESP_LOGD(TAG, "Advertising (major %u, minor %u)", this->major_, this->minor_);
  this->log_internal_heap_("advertising");
}

bool LatterBeacon::send_step_(uint8_t step) {
  switch (step) {
    case 0: {  // HCI_Reset
      const uint8_t pkt[] = {0x01, 0x03, 0x0C, 0x00};
      return this->transport_send_(pkt, sizeof(pkt));
    }
    case 1: {  // LE Set Advertising Parameters: non-connectable undirected, public address, all channels
      const uint16_t min_u = adv_units(this->min_interval_ms_);
      const uint16_t max_u = adv_units(this->max_interval_ms_);
      const uint8_t pkt[] = {0x01, 0x06, 0x20, 15,
                             static_cast<uint8_t>(min_u & 0xFF), static_cast<uint8_t>(min_u >> 8),
                             static_cast<uint8_t>(max_u & 0xFF), static_cast<uint8_t>(max_u >> 8),
                             0x03,  // ADV_NONCONN_IND
                             0x00,  // own address: public
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // peer address type + address (unused)
                             0x07,  // channels 37, 38, 39
                             0x00};  // no filter
      return this->transport_send_(pkt, sizeof(pkt));
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
      return this->transport_send_(pkt, sizeof(pkt));
    }
    default:
      return false;
  }
}

void LatterBeacon::fail_(const char *what, int err) {
  ESP_LOGE(TAG, "Beacon start failed: %s (%d); retrying in %u s", what, err,
           static_cast<unsigned>(RETRY_AFTER_FAILURE_MS / 1000U));
  this->failed_ = true;
  this->retry_after_ms_ = millis() + RETRY_AFTER_FAILURE_MS;
  this->disable_loop();
}

void LatterBeacon::dump_config() {
#ifdef USE_LATTER_BEACON_HOSTED
  constexpr const char *transport = "esp_hosted";
#else
  constexpr const char *transport = "VHCI";
#endif
  ESP_LOGCONFIG(TAG,
                "Latter Beacon (iBeacon, raw HCI over %s):\n"
                "  Major: %u, Minor: %u, Measured power: %d dBm\n"
                "  Interval: %u-%u ms",
                transport,
                this->major_, this->minor_, this->measured_power_,
                static_cast<unsigned>(this->min_interval_ms_), static_cast<unsigned>(this->max_interval_ms_));
}

}  // namespace esphome::latter_beacon
