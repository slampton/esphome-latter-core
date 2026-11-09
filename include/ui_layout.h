#pragma once

// ==============================================================================
// Latter Core UI Layout & Hardware Portability Toolkit (ui_layout.h)
// Provides resolution- & shape-adaptive layout metrics and hit-testing across
// circular (466x466), rectangular watch (410x502), and square (480x480 / 720x720)
// ESP32-S3 and ESP32-P4 displays.
// ==============================================================================

#include "ui_core.h"
#ifdef USE_ESP32
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_attr.h>
#endif
#include <atomic>
#ifdef USE_ESP32_CRASH_HANDLER
#include "esphome/components/esp32/crash_handler.h"
#endif

#ifndef LATTER_BOARD_ID
#define LATTER_BOARD_ID "generic_esp32"
#endif
#ifndef LATTER_BLE_MINOR
#define LATTER_BLE_MINOR 1
#endif
#ifndef LATTER_SLOT_COUNT
#define LATTER_SLOT_COUNT 5
#endif
#ifndef LATTER_AUDIO_OUT_RATE
#define LATTER_AUDIO_OUT_RATE 16000
#endif
#ifndef LATTER_AUDIO_OUT_CH
#define LATTER_AUDIO_OUT_CH 1
#endif
#ifndef LATTER_AUDIO_OUT_BITS
#define LATTER_AUDIO_OUT_BITS 16
#endif
#ifndef LATTER_AUDIO_IN_RATE
#define LATTER_AUDIO_IN_RATE 16000
#endif
#ifndef LATTER_BACKLIGHT_PWM_HZ
#define LATTER_BACKLIGHT_PWM_HZ 2000
#endif
#ifndef LATTER_BACKLIGHT_PWM_BITS
#define LATTER_BACKLIGHT_PWM_BITS 8
#endif
#ifndef LATTER_BACKLIGHT_DUTY_MIN_PCT
#define LATTER_BACKLIGHT_DUTY_MIN_PCT 0
#endif
#ifndef LATTER_BACKLIGHT_DUTY_MAX_PCT
#define LATTER_BACKLIGHT_DUTY_MAX_PCT 100
#endif

#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4) && defined(LATTER_DSI_IRAM_SAFE)
#include <esp_attr.h>
#include <esp_lcd_mipi_dsi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ESPHome 2026.10's mipi_dsi component registers a non-IRAM callback
// (notify_refresh_ready), which causes esp_lcd_dpi_panel_register_event_callbacks()
// to fail with ESP_ERR_INVALID_ARG when CONFIG_LCD_DSI_ISR_CACHE_SAFE=y is enabled.
// Wrapping registration to substitute an IRAM_ATTR semaphore-give trampoline allows
// CONFIG_LCD_DSI_ISR_CACHE_SAFE=y to stay enabled so the DW-GDMA DPI scanout ISR
// continues running without screen flashing during OTA flash sector erases.
extern "C" {
static DRAM_ATTR SemaphoreHandle_t s_latter_dpi_io_lock = nullptr;

static bool IRAM_ATTR __attribute__((noinline, used))
latter_iram_dpi_color_trans_done_cb(esp_lcd_panel_handle_t panel,
                                    esp_lcd_dpi_panel_event_data_t *edata,
                                    void *user_ctx) {
  (void) panel;
  (void) edata;
  (void) user_ctx;
  SemaphoreHandle_t sem = s_latter_dpi_io_lock;
  BaseType_t need_yield = pdFALSE;
  if (sem != nullptr) {
    xSemaphoreGiveFromISR(sem, &need_yield);
  }
  return need_yield == pdTRUE;
}

esp_err_t __real_esp_lcd_dpi_panel_register_event_callbacks(
    esp_lcd_panel_handle_t dpi_panel,
    const esp_lcd_dpi_panel_event_callbacks_t *cbs,
    void *user_ctx);

esp_err_t __wrap_esp_lcd_dpi_panel_register_event_callbacks(
    esp_lcd_panel_handle_t dpi_panel,
    const esp_lcd_dpi_panel_event_callbacks_t *cbs,
    void *user_ctx) {
  if (cbs == nullptr) {
    return __real_esp_lcd_dpi_panel_register_event_callbacks(dpi_panel, nullptr, user_ctx);
  }
  s_latter_dpi_io_lock = static_cast<SemaphoreHandle_t>(user_ctx);
  esp_lcd_dpi_panel_event_callbacks_t iram_cbs = *cbs;
  if (iram_cbs.on_color_trans_done != nullptr) {
    iram_cbs.on_color_trans_done = &latter_iram_dpi_color_trans_done_cb;
  }
  return __real_esp_lcd_dpi_panel_register_event_callbacks(dpi_panel, &iram_cbs, nullptr);
}
}  // extern "C"
#endif

namespace latter_ui {

struct HardwareCapabilities {
  bool is_round = (LATTER_ROUND_DISPLAY != 0);
  bool has_display = (LATTER_HAS_DISPLAY != 0);
  bool has_touch = (LATTER_HAS_TOUCH != 0);
  bool has_audio = (LATTER_HAS_AUDIO != 0);
  bool has_mic = (LATTER_HAS_MIC != 0);
  bool has_speaker = (LATTER_HAS_SPEAKER != 0);
  bool has_imu = (LATTER_HAS_IMU != 0);
  bool has_battery = (LATTER_HAS_BATTERY != 0);
  bool is_stationary = (LATTER_IS_STATIONARY != 0);
  bool is_wearable = (LATTER_IS_WEARABLE != 0);
  bool is_ips_panel = (LATTER_PANEL_IPS != 0);
  bool is_chip_p4 = (LATTER_CHIP_P4 != 0);
  int psram_mb = LATTER_PSRAM_MB;
};

inline constexpr HardwareCapabilities caps() {
  return HardwareCapabilities{};
}

inline uint32_t device_boot_id() {
  static uint32_t s_boot_id = 0;
  if (s_boot_id == 0) {
#ifdef USE_ESP32
    s_boot_id = esp_random() & 0x7FFFFFFFU;
#endif
    if (s_boot_id == 0) s_boot_id = 1U;
  }
  return s_boot_id;
}

// ------------------------------------------------------------------------------
// Main-loop stall guard
// ------------------------------------------------------------------------------
// ESPHome's task watchdog fires only when the loop task stops feeding it. A loop stuck inside one
// operation that keeps feeding it leaves the device on Wi-Fi but deaf to the API, the buttons and the
// display until a hard power-off. The guard is a separate FreeRTOS task that reads ESPHome's own
// per-operation start time twice a second. An operation that runs past kStallAbortMs (OTA, which
// blocks the loop by design, is excluded) is recorded in RTC memory and the device aborts: it reboots,
// reports reset "stall" in the manifest, and the boot summary names the operation. Blocks of
// kStallReportMs or more are kept as the longest block since boot and re-logged to every API client:
// such a block usually ends while the API is down, so ESPHome's "took a long time" line is lost.
constexpr uint32_t kStallAbortMs = 30000U;
constexpr uint32_t kStallReportMs = 2000U;
constexpr uint32_t kStallRecordMagic = 0x5354414CU;  // "STAL"

struct StallRecord {
  uint32_t magic;       // kStallRecordMagic when valid
  uint32_t blocked_ms;  // how long the operation had run
  uint32_t uptime_s;    // when it started
  char source[40];      // ESPHome execution source / component
};

#ifdef USE_ESP32
// Survives the abort's software reset (not a power-on); validated by magic + reset reason.
static RTC_NOINIT_ATTR StallRecord s_stall_rtc_record;
#endif

struct StallGuardState {
  std::atomic<bool> paused{false};
  // millis() when the guard last resumed: the operation that was running then (an OTA that failed or
  // is rebooting) is measured from this point, not from its own start.
  std::atomic<uint32_t> resumed_ms{0};
  bool started{false};
  StallRecord previous_boot{};  // the abort that ended the previous boot (magic 0 = none)
  StallRecord longest{};        // longest block this boot (magic 0 = none)
#ifdef USE_ESP32
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif
  StallGuardState() {
#ifdef USE_ESP32
    if (esp_reset_reason() == ESP_RST_PANIC && s_stall_rtc_record.magic == kStallRecordMagic) {
      previous_boot = s_stall_rtc_record;
      previous_boot.source[sizeof(previous_boot.source) - 1] = '\0';
    }
    s_stall_rtc_record.magic = 0;
#endif
  }
};

inline StallGuardState &stall_guard() {
  static StallGuardState s;
  return s;
}

inline bool previous_boot_stalled() { return stall_guard().previous_boot.magic == kStallRecordMagic; }

// OTA blocks the loop for the whole transfer by design: handle_ota_begin pauses the guard and
// handle_ota_end / handle_ota_error resume it. Those callbacks run inside the OTA component's own
// (still blocking) operation, so a resume restarts the clock rather than counting the transfer.
inline void stall_guard_pause(bool paused) {
  auto &g = stall_guard();
  if (!paused) g.resumed_ms.store(esphome::millis() | 1U, std::memory_order_relaxed);
  g.paused.store(paused, std::memory_order_relaxed);
}

inline void stall_current_source(char *dst, size_t n) {
  const esphome::LogString *src = esphome::App.get_current_source();
  esphome::Component *comp = esphome::App.get_current_component();
  const char *src_s = (src != nullptr) ? LOG_STR_ARG(src) : nullptr;
  const char *comp_s = (comp != nullptr) ? LOG_STR_ARG(comp->get_component_log_str()) : nullptr;
  if (src_s != nullptr && comp_s != nullptr && std::strcmp(src_s, comp_s) != 0) {
    std::snprintf(dst, n, "%s/%s", comp_s, src_s);
  } else {
    std::snprintf(dst, n, "%s", src_s != nullptr ? src_s : (comp_s != nullptr ? comp_s : "unknown"));
  }
}

#ifdef USE_ESP32
inline void stall_guard_task(void *) {
  auto &g = stall_guard();
  for (;;) {
    // Every 2 s: each check wakes the chip from light sleep, and stalls worth reporting last seconds.
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (g.paused.load(std::memory_order_relaxed)) continue;
    uint32_t start = esphome::App.get_loop_component_start_time();
    const uint32_t resumed = g.resumed_ms.load(std::memory_order_relaxed);
    if (resumed != 0 && static_cast<int32_t>(resumed - start) > 0) start = resumed;
    // The start time is the last dispatch, so an idle loop sleeping out its interval (1 s in light-sleep
    // quiet) looks blocked for that long: count only the time beyond one loop interval.
    const uint32_t idle_ms = esphome::App.get_loop_interval();
    const uint32_t since = esphome::millis() - start;
    if (static_cast<int32_t>(since) < 0 || since <= idle_ms) continue;
    const uint32_t blocked = since - idle_ms;
    if (blocked < kStallReportMs) continue;
    StallRecord rec{};
    rec.magic = kStallRecordMagic;
    rec.blocked_ms = blocked;
    rec.uptime_s = start / 1000U;
    stall_current_source(rec.source, sizeof(rec.source));
    portENTER_CRITICAL(&g.mux);
    if (blocked > g.longest.blocked_ms) g.longest = rec;
    portEXIT_CRITICAL(&g.mux);
    if (blocked >= kStallAbortMs) {
      // No logging here: the stalled operation may hold the logger. The record is the report.
      s_stall_rtc_record = rec;
      esp_system_abort("Latter stall guard: main loop blocked");
    }
  }
}
#endif

// Started once from on_boot. Priority sits above the loop task and below Wi-Fi / lwIP.
inline void start_stall_guard() {
#ifdef USE_ESP32
  auto &g = stall_guard();
  if (g.started) return;
  g.started = xTaskCreate(stall_guard_task, "latter_stall", 2560, nullptr, 15, nullptr) == pdPASS;
  if (!g.started) ESP_LOGW("latter", "Stall guard task could not start");
#endif
}

inline void log_stall_summary() {
  auto &g = stall_guard();
  if (g.previous_boot.magic == kStallRecordMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] previous boot aborted: main loop blocked %lums in %s (started at uptime %lus)",
             (unsigned long)g.previous_boot.blocked_ms, g.previous_boot.source,
             (unsigned long)g.previous_boot.uptime_s);
  }
  StallRecord longest{};
#ifdef USE_ESP32
  portENTER_CRITICAL(&g.mux);
  longest = g.longest;
  portEXIT_CRITICAL(&g.mux);
#endif
  if (longest.magic == kStallRecordMagic) {
    ESP_LOGW("PROF", "[PROF:STALL] longest main loop block this boot: %lums in %s (started at uptime %lus)",
             (unsigned long)longest.blocked_ms, longest.source, (unsigned long)longest.uptime_s);
  }
}

// Why the device last restarted, whether ESPHome's crash handler holds a record of that crash (its
// decoded backtrace is logged to every log client until the next restart), and whether the running
// firmware is a rollback after an update that failed to boot. Reported in the device_hello manifest
// so Home Assistant can raise a repair instead of the crash going unnoticed.
inline const char *last_reset_reason() {
#ifdef USE_ESP32
  if (previous_boot_stalled()) return "stall";  // stall guard abort (an ESP_RST_PANIC)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";  // USB Serial/JTAG reset (e.g. after a USB flash)
    default: break;
  }
#endif
  return "unknown";
}

inline bool crash_record_present() {
#ifdef USE_ESP32_CRASH_HANDLER
  return esphome::esp32::crash_handler_has_data();
#else
  return false;
#endif
}

inline bool firmware_rolled_back() {
#ifdef USE_ESP32
  return esp_ota_get_last_invalid_partition() != nullptr;
#else
  return false;
#endif
}

inline std::string build_hardware_manifest_json() {
  const char *shape = LATTER_ROUND_DISPLAY ? "circle" : "square";
  const char *chip = LATTER_CHIP_P4 ? "ESP32-P4" : "ESP32-S3";
  char audio_buf[96];
  if (LATTER_HAS_AUDIO) {
    std::snprintf(audio_buf, sizeof(audio_buf),
                  "{\"out_rate\":%d,\"out_ch\":%d,\"out_bits\":%d,\"in_rate\":%d}",
                  (int)LATTER_AUDIO_OUT_RATE, (int)LATTER_AUDIO_OUT_CH,
                  (int)LATTER_AUDIO_OUT_BITS, (int)LATTER_AUDIO_IN_RATE);
  } else {
    std::snprintf(audio_buf, sizeof(audio_buf), "0");
  }
  char buf[640];
  std::snprintf(buf, sizeof(buf),
                "{\"v\":2,\"contract\":%d,\"fw\":\"2.2.1\",\"board\":\"%s\",\"boot_id\":%lu,"
                "\"w\":%d,\"h\":%d,\"shape\":\"%s\","
                "\"display\":%d,\"touch\":%d,\"audio\":%s,\"mic\":%d,\"spk\":%d,"
                "\"imu\":%d,\"battery\":%d,\"stationary\":%d,\"wearable\":%d,"
                "\"ips\":%d,\"chip\":\"%s\",\"psram_mb\":%d,"
                "\"slot_count\":%d,\"max_payload_bytes\":2048,"
                "\"beacon\":{\"uuid\":\"4C415454-4552-434F-5245-000000000220\",\"major\":1,\"minor\":%d},"
                "\"sdk\":%ld,\"reset\":\"%s\",\"crash\":%d,\"rollback\":%d}",
                kProtocolContractVersion, LATTER_BOARD_ID, (unsigned long)device_boot_id(),
                SCREEN_W, SCREEN_H, shape,
                (int)LATTER_HAS_DISPLAY, (int)LATTER_HAS_TOUCH,
                audio_buf, (int)LATTER_HAS_MIC, (int)LATTER_HAS_SPEAKER,
                (int)LATTER_HAS_IMU, (int)LATTER_HAS_BATTERY,
                (int)LATTER_IS_STATIONARY, (int)LATTER_IS_WEARABLE,
                (int)LATTER_PANEL_IPS, chip, (int)LATTER_PSRAM_MB,
                (int)LATTER_SLOT_COUNT, (int)LATTER_BLE_MINOR,
                (long)LATTER_SDK_REV, last_reset_reason(), (int)crash_record_present(), (int)firmware_rolled_back());
  return std::string(buf);
}

// Compact <=230-byte Hardware Manifest v2 state for Home Assistant's 255-char entity state limit
inline std::string build_hardware_manifest_state_json() {
  const char *shape = LATTER_ROUND_DISPLAY ? "circle" : "square";
  const char *chip = LATTER_CHIP_P4 ? "ESP32-P4" : "ESP32-S3";
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "{\"v\":2,\"contract\":%d,\"board\":\"%s\","
                "\"w\":%d,\"h\":%d,\"shape\":\"%s\","
                "\"display\":%d,\"touch\":%d,\"audio\":%d,\"mic\":%d,\"spk\":%d,"
                "\"imu\":%d,\"battery\":%d,\"stationary\":%d,\"wearable\":%d,"
                "\"ips\":%d,\"chip\":\"%s\",\"psram_mb\":%d,\"minor\":%d}",
                kProtocolContractVersion, LATTER_BOARD_ID,
                SCREEN_W, SCREEN_H, shape,
                (int)LATTER_HAS_DISPLAY, (int)LATTER_HAS_TOUCH,
                (int)LATTER_HAS_AUDIO, (int)LATTER_HAS_MIC, (int)LATTER_HAS_SPEAKER,
                (int)LATTER_HAS_IMU, (int)LATTER_HAS_BATTERY,
                (int)LATTER_IS_STATIONARY, (int)LATTER_IS_WEARABLE,
                (int)LATTER_PANEL_IPS, chip, (int)LATTER_PSRAM_MB,
                (int)LATTER_BLE_MINOR);
  return std::string(buf);
}

struct UiLayout {
  int w = SCREEN_W;
  int h = SCREEN_H;
  int cx = SCREEN_CX;
  int cy = SCREEN_CY;
  bool is_round = (LATTER_ROUND_DISPLAY != 0);

  // Header & Footer Y coordinates (Adaptive for Round vs Wearable Watch vs Square/Rect vs 720p HD)
  int hdr_y = LATTER_ROUND_DISPLAY ? 48 : (LATTER_IS_WEARABLE ? 36 : ((SCREEN_H > 520) ? 48 : 28));
  int sub_hdr_y = LATTER_ROUND_DISPLAY ? 76 : (LATTER_IS_WEARABLE ? 64 : ((SCREEN_H > 520) ? 88 : 56));
  int back_y = kBackLabelY;
  int home_bar_y = kHomeBarY;
  int dock_y = LATTER_ROUND_DISPLAY ? 359 : (SCREEN_H - scale_y(66));
  int app_dock_y = LATTER_ROUND_DISPLAY ? 369 : (SCREEN_H - scale_y(66));
  int dock_pips_y = LATTER_ROUND_DISPLAY ? 404 : (SCREEN_H - scale_y(27));
  int app_dock_pips_y = LATTER_ROUND_DISPLAY ? 408 : (SCREEN_H - scale_y(27));
  int dock_r = (SCREEN_W < 360) ? 24 : ((SCREEN_W > 520) ? 40 : 27);

  // Consistent Upper-Left Celestial Sun/Moon Coordinates across all 5 displays
  int celestial_x = LATTER_ROUND_DISPLAY ? 106 : ((SCREEN_W < 380) ? 44 : (LATTER_IS_WEARABLE ? 68 : ((SCREEN_W > 520) ? 118 : 78)));
  int celestial_y = LATTER_ROUND_DISPLAY ? 86  : ((SCREEN_W < 380) ? 72 : ((SCREEN_H > 520) ? 118 : 78));

  // Corner-Safe Battery Indicator Coordinates for Non-Round Displays
  int battery_cx = LATTER_ROUND_DISPLAY ? SCREEN_CX : (LATTER_IS_WEARABLE ? (SCREEN_W - 68) : ((SCREEN_W < 380) ? (SCREEN_W - 36) : ((SCREEN_W > 520) ? (SCREEN_W - 86) : (SCREEN_W - 58))));
  int battery_y  = LATTER_ROUND_DISPLAY ? 435       : (LATTER_IS_WEARABLE ? 43 : ((SCREEN_W < 380) ? 24 : ((SCREEN_H > 520) ? 42 : 28)));

  // Standard Menu Card Width & Left Text Margin (232px on 320x480; scales up on >520px HD panels)
  int list_card_w = (SCREEN_W < 380) ? (SCREEN_W - 88) : ((SCREEN_W > 520) ? ((SCREEN_W * 276) / 466) : 276);
  int list_card_left = SCREEN_CX - (list_card_w / 2);

  // 1. Adaptive 5-Slot Vertical List Geometry (System, Music, Area Picker, Calendar, Intercom)
  int slot5_stride = (SCREEN_H > 520) ? ((SCREEN_H * 63) / 466) : 63;
  int slot5_h      = (SCREEN_H > 520) ? ((SCREEN_H * 44) / 466) : 44;
  int slot5_cy     = (SCREEN_H > 520) ? SCREEN_CY : 236;
  constexpr int slot5_y(int idx_0_to_4) const {
    return slot5_cy + (idx_0_to_4 - 2) * slot5_stride;
  }
  template <typename TouchT>
  constexpr int hit_slot5(const TouchT &t, int x_min, int x_max) const {
    const int half_w = (w > 520) ? ((w * 150) / 466) : 150;
    const int scaled_min = (w < 380 && x_min < 60) ? 4 : scale_x(x_min);
    const int scaled_max = (w < 380) ? (w - 10) : scale_x(x_max);
    const int eff_min = (w == 466) ? std::min(x_min, 38) : std::max(4, std::min(scaled_min, cx - half_w));
    const int eff_max = (w == 466) ? x_max : std::min(w - 10, std::max(scaled_max, cx + half_w));
    if (t.x < eff_min || t.x > eff_max) return -1;
    const int half_s = slot5_stride / 2;
    for (int i = 0; i < 5; i++) {
      const int ym = slot5_y(i);
      const int y_top = ym - ((i == 0) ? (half_s + 4) : half_s);
      const int y_bot = ym + ((i == 4) ? (half_s + 10) : half_s);
      if (t.y >= y_top && t.y <= y_bot) return i;
    }
    return -1;
  }

  // 2. Adaptive 3-Card Area Control & Whole-Home Geometry + Unified Hit-Testers
  int area_card3_stride = (SCREEN_H > 520) ? ((SCREEN_H * 76) / 466) : 76;
  int area_card3_h      = (SCREEN_H > 520) ? ((SCREEN_H * 64) / 466) : 64;
  int area_card3_w      = (SCREEN_W < 380) ? (SCREEN_W - 68) : ((SCREEN_W > 520) ? ((SCREEN_W * 316) / 466) : 316);
  int area_card3_cy     = (SCREEN_H > 520) ? ((SCREEN_H * 208) / 466) : 208;
  constexpr int area_card3_y(int idx_0_to_2) const {
    return area_card3_cy + (idx_0_to_2 - 1) * area_card3_stride;
  }
  constexpr int area_card_left() const { return cx - (area_card3_w / 2); }
  constexpr int area_card_right() const { return cx + (area_card3_w / 2); }
  constexpr int area_card_div_x() const { return area_card_left() + scale_x(64); }
  constexpr int area_card_orb_x() const { return area_card_right() - scale_x(28); }
  constexpr int hit_area_card3(int tx, int ty) const {
    int hw = (area_card3_w / 2) + 10;
    if (tx < cx - hw || tx > cx + hw) return -1;
    int hs = area_card3_stride / 2;
    for (int i = 0; i < 3; i++) {
      int ym = area_card3_y(i);
      if (ty >= ym - hs && ty <= ym + hs) return i;
    }
    return -1;
  }
  constexpr bool is_area_light_orb(int tx) const {
    int ox = area_card_orb_x();
    return (tx >= ox - scale_x(28) && tx <= area_card_right() + 8);
  }
  constexpr int area_slider_pct(int tx) const {
    int x0 = area_card_div_x() + 4;
    int x1 = area_card_right() - 8;
    if (x1 <= x0) return 100;
    int raw = ((tx - x0) * 100) / (x1 - x0);
    int stepped = ((raw + 2) / 5) * 5;
    return (stepped < 5) ? 5 : ((stepped > 100) ? 100 : stepped);
  }

  // 3. Adaptive 3-Card Timers & Alarms Landing Geometry + Unified Hit-Tester
  int tool_card3_stride = (SCREEN_H > 520) ? ((SCREEN_H * 100) / 466) : 100;
  int tool_card3_h      = (SCREEN_H > 520) ? ((SCREEN_H * 74) / 466) : 74;
  int tool_card3_w      = (SCREEN_W < 380) ? (SCREEN_W - 36) : ((SCREEN_W > 520) ? ((SCREEN_W * 316) / 466) : 316);
  int tool_card3_cy     = (SCREEN_H > 520) ? ((SCREEN_H * 222) / 466) : 222;
  constexpr int tool_card3_y(int idx_0_to_2) const {
    return tool_card3_cy + (idx_0_to_2 - 1) * tool_card3_stride;
  }
  constexpr int tool_card3_div_x() const {
    return cx + (tool_card3_w / 2) - scale_x(93);
  }
  constexpr int hit_tool_card3(int tx, int ty) const {
    int hw = (tool_card3_w / 2) + 12;
    if (tx < cx - hw || tx > cx + hw) return -1;
    int hs = tool_card3_stride / 2;
    for (int i = 0; i < 3; i++) {
      int ym = tool_card3_y(i);
      if (ty >= ym - hs && ty <= ym + hs) return i;
    }
    return -1;
  }

  // 4. Adaptive 2x3 Tools Launcher Grid Geometry + Unified Hit-Tester
  int tools_col_off    = (SCREEN_W < 380) ? 61 : ((SCREEN_W > 520) ? ((SCREEN_W * 74) / 466) : 68);
  int tools_row_stride = (SCREEN_H > 520) ? ((SCREEN_H * 105) / 466) : 105;
  int tools_grid_cy    = (SCREEN_H > 520) ? SCREEN_CY : 236;
  constexpr int tools_row_y(int row_0_to_2) const {
    return tools_grid_cy + (row_0_to_2 - 1) * tools_row_stride;
  }
  constexpr int hit_tools_row(int ty) const {
    int hs = tools_row_stride / 2;
    for (int r = 0; r < 3; r++) {
      int ym = tools_row_y(r);
      if (ty >= ym - hs && ty <= ym + hs) return r;
    }
    return -1;
  }
  constexpr int hit_tools_col(int tx) const {
    int max_dx = tools_col_off * 2 + 12;
    if (tx >= cx - max_dx && tx <= cx) return 0;
    if (tx > cx && tx <= cx + max_dx) return 1;
    return -1;
  }

  // 5. Adaptive Home Screen Vertical Zones + Unified Hit-Testers
  int home_date_y     = (SCREEN_H > 520) ? ((SCREEN_H * 52) / 466)  : 52;
  int home_clock_y    = (SCREEN_H > 520) ? ((SCREEN_H * 128) / 466) : 128;
  int home_weather_y  = (SCREEN_H > 520) ? ((SCREEN_H * 200) / 466) : 200;
  int home_activity_y = (SCREEN_H > 520) ? ((SCREEN_H * 266) / 466) : 266;
  constexpr bool hit_home_clock(int tx, int ty) const {
    int hw = (w > 520) ? 180 : 135;
    int hh = (h > 520) ? 64  : 48;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_clock_y - hh && ty <= home_clock_y + hh);
  }
  constexpr bool hit_home_weather(int tx, int ty) const {
    int hw = (w > 520) ? 210 : 165;
    int hh = (h > 520) ? 36  : 28;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_weather_y - hh && ty <= home_weather_y + hh);
  }
  constexpr bool hit_home_activity(int tx, int ty) const {
    int act_w = (w - 24 < 380) ? (w - 24) : ((w > 520) ? ((w * 380) / 466) : 380);
    int hw = act_w / 2;
    int hh = (h > 520) ? 54 : 44;
    return (tx >= cx - hw && tx <= cx + hw && ty >= home_activity_y - hh && ty <= home_activity_y + hh);
  }

  // 4-Column Stepper X centers (-5m, -1m, +1m, +5m / -1h, +1h, -5m, +5m)
  constexpr int stepper_x(int col_0_to_3) const {
    const int stride = (w < 380) ? 74 : ((w > 520) ? ((w * 90) / 466) : 90);
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }

  // 4-Column Calculator Keypad X centers
  constexpr int calc_col_x(int col_0_to_3) const {
    const int stride = (w < 380) ? 72 : ((w > 520) ? ((w * 82) / 466) : 82);
    return cx - ((stride * 3) / 2) + (col_0_to_3 * stride);
  }

  // 6. Single-Source Music Now Playing, Drilldown & Track Inspector Scrubber Geometry + Hit-Testers
  int music_art_cy = scale_y(233);
  constexpr int music_left_flank_x() const { return is_round ? 71 : ((w < 380) ? 42 : scale_x(48)); }
  constexpr int music_right_flank_x() const { return w - music_left_flank_x(); }
  constexpr int music_prev_x() const { return cx - (is_round ? 109 : ((w < 380) ? 96 : scale_x(104))); }
  constexpr int music_next_x() const { return cx + (is_round ? 109 : ((w < 380) ? 96 : scale_x(104))); }
  constexpr bool hit_music_header(int tx, int ty) const {
    return (ty <= scale_y(85) && (tx >= cx - scale_x(130) && tx <= cx + scale_x(130)));
  }
  constexpr bool hit_music_play_pause(int tx, int ty) const {
    int r = scale_s(48);
    return (tx >= cx - r && tx <= cx + r && ty >= music_art_cy - r && ty <= music_art_cy + r);
  }
  constexpr bool hit_music_prev(int tx, int ty) const {
    int r = scale_s(48);
    return (tx < cx - r && ty >= music_art_cy - r && ty < music_art_cy + r);
  }
  constexpr bool hit_music_next(int tx, int ty) const {
    int r = scale_s(48);
    return (tx > cx + r && ty >= music_art_cy - r && ty < music_art_cy + r);
  }
  constexpr bool hit_music_spk_shortcut(int tx, int ty) const {
    int r = scale_s(48);
    return (tx <= cx - r && ty >= scale_y(70) && ty < music_art_cy - r);
  }
  constexpr bool hit_music_lib_shortcut(int tx, int ty) const {
    int r = scale_s(48);
    return (tx <= cx - r && ty >= music_art_cy + r && ty < scale_y(405));
  }
  constexpr bool hit_music_vol_up(int tx, int ty) const {
    int r = scale_s(48);
    return (tx >= cx + r && ty >= scale_y(70) && ty < music_art_cy - r);
  }
  constexpr bool hit_music_vol_down(int tx, int ty) const {
    int r = scale_s(48);
    return (tx >= cx + r && ty >= music_art_cy + r && ty < scale_y(405));
  }
  constexpr bool hit_music_metadata(int tx, int ty) const {
    return (tx >= cx - scale_x(110) && tx <= cx + scale_x(110) && ty >= scale_y(338) && ty < scale_y(408));
  }
  constexpr int music_drill_play_cx() const { return (w < 380) ? 28 : scale_x(82); }
  constexpr int music_drill_split_x() const { return music_drill_play_cx() + scale_x(26); }
  constexpr bool hit_music_drill_play(int tx) const { return tx <= (music_drill_split_x() + scale_x(8)); }
  int music_scrub_y = scale_y(242);
  constexpr int music_seek_left_x() const { return is_round ? 50 : scale_x(34); }
  constexpr int music_seek_right_x() const { return w - music_seek_left_x(); }
  constexpr int music_scrub_x0() const { return is_round ? 95 : scale_x(72); }
  constexpr int music_scrub_x1() const { return w - music_scrub_x0(); }
  constexpr int music_scrub_w() const { return music_scrub_x1() - music_scrub_x0(); }
  constexpr float music_scrub_frac(int tx) const {
    int bw = music_scrub_w();
    if (bw <= 10) return 0.0f;
    float f = static_cast<float>(tx - music_scrub_x0()) / static_cast<float>(bw);
    return (f < 0.0f) ? 0.0f : ((f > 1.0f) ? 1.0f : f);
  }

  // 7. Single-Source Light Kelvin / Brightness Inspector Slider Geometry
  constexpr int light_slider_card_w() const { return (w < 380) ? (w - 24) : scale_x(316); }
  constexpr int light_kelvin_bar_w() const { return light_slider_card_w() - scale_x(32); }
  constexpr int light_kelvin_bar_x0() const { return cx - (light_kelvin_bar_w() / 2); }
  constexpr int light_bri_bar_w() const { return light_slider_card_w() - scale_x(16); }
  constexpr int light_bri_bar_x0() const { return cx - (light_bri_bar_w() / 2); }

  // 8. Single-Source System Color Swatch & Info Margin Geometry
  constexpr int sys_color_swatch_x() const { return cx + (list_card_w / 2) - scale_x(34); }
  constexpr bool hit_sys_color_swatch(int tx) const { return tx >= (sys_color_swatch_x() - scale_x(22)); }
  constexpr int sys_info_x() const { return is_round ? 88 : ((w < 380) ? 18 : scale_x(42)); }

  // 9. Single-Source Calendar Map Floating Zoom Button Geometry + Hit-Testers
  int map_zoom_y = scale_y(233);
  constexpr int map_zoom_left_x() const { return is_round ? 44 : scale_x(34); }
  constexpr int map_zoom_right_x() const { return w - map_zoom_left_x(); }
  constexpr bool hit_map_zoom_out(int tx, int ty) const {
    int r = scale_s(45);
    return (ty >= map_zoom_y - r && ty <= map_zoom_y + r && tx <= map_zoom_left_x() + r);
  }
  constexpr bool hit_map_zoom_in(int tx, int ty) const {
    int r = scale_s(45);
    return (ty >= map_zoom_y - r && ty <= map_zoom_y + r && tx >= map_zoom_right_x() - r);
  }

  // 10. Touch thresholds in millimetres (docs/device-ui-design.md D-002), converted with LATTER_PANEL_PPI.
  //     Each value is the pixel distance the 466 px reference panel (266 ppi) used before, so that panel
  //     behaves exactly as it did; every other panel gets the same physical distance. touch_handlers.yaml
  //     takes every gesture threshold from here (latter-verify [TOUCH MM]).
  constexpr int drag_start() const { return mm_to_px(0.6f); }          //  6 px: a card slider drag starts
  constexpr int flick_min() const { return mm_to_px(1.7f); }           // 18 px: music track-inspector flick
  constexpr int dock_release_swipe() const { return mm_to_px(1.9f); }  // 20 px: Home dock page swipe on release
  constexpr int swipe_short() const { return mm_to_px(2.1f); }         // 22 px: dock swipe while moving, music lists
  constexpr int swipe_min() const { return mm_to_px(2.3f); }           // 24 px: swipe-up home, pill-tap travel, mode bar, 2048
  constexpr int list_swipe() const { return mm_to_px(2.5f); }          // 26 px: vertical list and carousel paging
  constexpr int page_swipe() const { return mm_to_px(2.7f); }          // 28 px: in-app mode bars, map pan, cameras
  constexpr int tap_max() const { return mm_to_px(2.9f); }             // 30 px: a touch that moved less is a tap
  constexpr int wide_swipe() const { return mm_to_px(2.9f); }          // 30 px: Sky & Weather subpage swipe
  constexpr int canvas_swipe() const { return mm_to_px(3.25f); }       // 34 px: Area Control canvas category swipe
  constexpr int tap_max_loose() const { return mm_to_px(3.45f); }      // 36 px: music browser taps
  constexpr int fast_pan() const { return mm_to_px(10.5f); }           // 110 px: a map drag pans two tiles
  // Zones measured from the bottom edge (round panels keep their hand-placed rows).
  constexpr int edge_guard_y() const { return h - mm_to_px(1.5f); }                      // 16 px band below in-app content
  constexpr int home_zone_y() const { return is_round ? 412 : (h - mm_to_px(5.2f)); }   // 54 px: swipe-up home starts here
  constexpr int chin_guard_y() const { return is_round ? 412 : (h - mm_to_px(2.7f)); }  // 28 px: the home pill's chin
  // Area Control: drags above this line scrub card sliders; horizontal swipes below it step the mode bar.
  constexpr int mode_bar_zone_top() const { return app_dock_y - dock_r - scale_s(14); }
  // Home dock icon hit radius: the drawn icon plus 16 px, and never less than a 7 mm target.
  constexpr int dock_hit_r() const { return std::max(dock_r + 16, mm_to_px(3.6f)); }
  template <typename TouchT>
  constexpr bool is_home_pill_tap(const TouchT &t) const {
    const int pill_y = is_round ? 422 : (h - mm_to_px(2.8f));
    const int pill_hw = std::max(scale_x(64), mm_to_px(6.5f));
    return (t.y >= pill_y && std::abs(t.x - cx) <= pill_hw);
  }
};

inline constexpr UiLayout layout() {
  return UiLayout{};
}

// Page header (docs/device-ui-design.md, components): an UPPERCASE title in font_small and theme_primary,
// centered at layout().hdr_y. `shadow` adds a 1 px drop shadow for headers drawn over wallpaper or art.
template <typename DisplayT, typename FontT>
inline void render_page_header(DisplayT &it, int cx, const char *title, FontT *font, esphome::Color color,
                               bool shadow = false) {
  if (title == nullptr || title[0] == '\0' || font == nullptr) return;
  const int y = layout().hdr_y;
  if (shadow) fast_print(it, cx + 1, y + 1, font, kChromeShadow, esphome::display::TextAlign::CENTER, title);
  fast_print(it, cx, y, font, color, esphome::display::TextAlign::CENTER, title);
}

// Header text that may be long (names from Home Assistant): fitted to the header width first.
template <typename DisplayT, typename FontT>
inline void render_page_header(DisplayT &it, int cx, const std::string &title, FontT *font, esphome::Color color,
                               bool shadow = false) {
  if (title.empty() || font == nullptr) return;
  const std::string fitted = fit_text_to_width_font(title, scale_x(280), font);
  render_page_header(it, cx, fitted.c_str(), font, color, shadow);
}

// Sub-header line at layout().sub_hdr_y: action feedback in theme_secondary, otherwise a context line in
// col_slate or col_white. Always font_small.
template <typename DisplayT, typename FontT>
inline void render_sub_header(DisplayT &it, int cx, const char *text, FontT *font, esphome::Color color) {
  if (text == nullptr || text[0] == '\0' || font == nullptr) return;
  fast_print(it, cx, layout().sub_hdr_y, font, color, esphome::display::TextAlign::CENTER, text);
}

template <typename DisplayT, typename FontT>
inline void render_sub_header(DisplayT &it, int cx, const std::string &text, FontT *font, esphome::Color color) {
  render_sub_header(it, cx, text.c_str(), font, color);
}

// Empty or loading state: one sentence on a quiet card that says what's missing (or what's loading).
template <typename DisplayT, typename FontT>
inline void render_empty_state(DisplayT &it, int cx, int cy, const char *text, FontT *font, esphome::Color text_c) {
  render_modern_card(it, cx, cy, 296, 54, 14, kChromeBarTrack, kChromeCardBorder, text, text_c, font);
}


// Hit-test the 4-column stepper row (returns 0..3 or -1)
inline int hit_4col_stepper(int tx, int cx = SCREEN_CX) {
  const int stride = (SCREEN_W < 380) ? 74 : scale_x(90);
  if (tx < cx - stride * 2 || tx > cx + stride * 2) return -1;
  if (tx < cx - stride) return 0;
  if (tx < cx)          return 1;
  if (tx < cx + stride) return 2;
  return 3;
}

// Hit-test the 4-column calculator grid (returns 0..3 or -1)
inline int hit_4col_calc(int tx, int cx = SCREEN_CX) {
  const int stride = (SCREEN_W < 380) ? 72 : scale_x(82);
  if (tx < cx - stride * 2 || tx > cx + stride * 2) return -1;
  if (tx < cx - stride) return 0;
  if (tx < cx)          return 1;
  if (tx < cx + stride) return 2;
  return 3;
}

// Wi-Fi modem power save has one owner: ESPHome's wifi component (latter_core enables its runtime
// power-save control). Latter sets the base mode with set_power_save_mode(): LIGHT with the screen on,
// HIGH with it off (LIGHT on the P4, so the SDIO RPC to the C6 stays responsive). While Latter needs
// full throughput it holds an ESPHome high-performance request; ESPHome runs NONE while any component
// holds one and returns to the base mode when the last is released. Latter never calls esp_wifi_set_ps()
// for this: the old direct call cached its last mode, so when ESPHome restored its configured LIGHT after
// another component's request, a docked device stayed in power save and OTA crawled (or fell back to UDP).
struct WifiPowerState {
  bool hp_held{false};      // Latter's ESPHome high-performance request
  bool base_valid{false};   // false: (re)apply the base mode on the next policy pass
  int base{-1};             // esphome::wifi::WiFiPowerSaveMode last handed to ESPHome
  bool pulse{false};        // holding a request for one pass so ESPHome applies a new base on release
};
inline WifiPowerState &wifi_power_state() {
  static WifiPowerState s;
  return s;
}

#ifdef USE_WIFI
template <typename WifiT>
inline void apply_wifi_power_policy(WifiT *wifi, bool high_tput_needed, int display_power_state) {
#if !LATTER_IS_STATIONARY
  if (wifi == nullptr) return;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  (void)display_power_state;
  const auto base = esphome::wifi::WIFI_POWER_SAVE_LIGHT;
#elif defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
  // Automatic light sleep (hal_power_pm.h) already idles the CPU between Wi-Fi wakes. Max modem sleep
  // (listen interval 3) on top of it lost ~70% of packets with ~0.6 s round trips: the Home Assistant
  // connection dropped and could not re-handshake. Min modem sleep wakes for every DTIM beacon.
  (void)display_power_state;
  const auto base = esphome::wifi::WIFI_POWER_SAVE_LIGHT;
#else
  const auto base = (display_power_state == 2) ? esphome::wifi::WIFI_POWER_SAVE_HIGH
                                               : esphome::wifi::WIFI_POWER_SAVE_LIGHT;
#endif
  auto &st = wifi_power_state();
  bool want_hp = high_tput_needed;
  if (st.pulse) {
    st.pulse = false;  // the request held since the base change is released below unless still needed
  }
  if (!st.base_valid || st.base != static_cast<int>(base)) {
    wifi->set_power_save_mode(base);
    st.base = static_cast<int>(base);
    st.base_valid = true;
    // ESPHome applies a new base only when the last high-performance request is released: hold one for a
    // single pass so that release happens on the next one.
    st.pulse = true;
    want_hp = true;
  }
  if (want_hp && !st.hp_held) {
    st.hp_held = wifi->request_high_performance();
  } else if (!want_hp && st.hp_held) {
    wifi->release_high_performance();
    st.hp_held = false;
  }
#else
  (void)wifi;
  (void)high_tput_needed;
  (void)display_power_state;
#endif
}
#endif

// OTA blocks the main loop for the whole transfer, so ESPHome's wifi loop can't apply a request then:
// handle_ota_begin switches the modem out of power save directly, and an OTA that fails or is aborted
// marks the base for re-application, which hands the mode back to ESPHome on the next policy pass.
inline void wifi_power_ota_begin() {
#ifdef USE_ESP32
  esp_wifi_set_ps(WIFI_PS_NONE);
#endif
}
inline void wifi_power_ota_end() { wifi_power_state().base_valid = false; }

// Event-declared radio demand: a voice session that is about to start (before va.is_running() is
// true) asks for full-throughput Wi-Fi for a bounded window; apply_power_policy honours it.
inline uint32_t &radio_boost_until_ms() {
  static uint32_t until = 0;
  return until;
}
inline void request_radio_boost(uint32_t now_ms, uint32_t duration_ms) {
  radio_boost_until_ms() = (now_ms + duration_ms) | 1U;
}
inline bool radio_boost_active(uint32_t now_ms) {
  const uint32_t until = radio_boost_until_ms();
  return until != 0 && static_cast<int32_t>(until - now_ms) > 0;
}

// "Area: Presence Beacon Mode" option -> evaluate_radio_policy beacon_mode.
inline int beacon_mode_index(const std::string &opt) {
  if (opt == "Always") return 2;
  if (opt == "Handheld + Screen Off") return 1;
  return 0;  // "Handheld Only"
}

template <typename BleCtrlT, typename WifiT>
inline void evaluate_radio_policy(BleCtrlT *ble_ctrl, WifiT *wifi, bool wifi_connected, bool ble_beacon_switch_on,
                                  int beacon_mode, bool usb_powered,
                                  bool is_hibernating, bool ota_in_progress, int display_power_state,
                                  bool high_tput_needed, uint32_t now_ms) {
  if (is_hibernating || ota_in_progress) return;

  // 1. BLE Presence Beacon:
  // The beacon (ble_area_beacon_switch) exists to place a device that moves between Areas. On battery
  // devices, "Area: Presence Beacon Mode" decides when it advertises:
  //   0 Handheld Only (default): unplugged with the screen on. Docked devices stay put (their Area was
  //     settled while they were carried there), and Bluetooth holds internal RAM that the
  //     docked voice assistant (wake word, Sendspin) needs. On battery with the screen off it pauses:
  //     the BT controller blocks automatic light sleep, and Voice Follows Me re-places the device within
  //     the dwell time after the next wake.
  //   1 Handheld + Screen Off: unplugged in any screen state.
  //   2 Always: whenever the switch is on, docked included.
  // Stationary (non-battery) devices advertise whenever the switch is on.
#if LATTER_HAS_BATTERY
  const bool beacon_context_ok = (beacon_mode == 2) ||
                                 (!usb_powered && (beacon_mode == 1 || display_power_state != 2));
#else
  (void)beacon_mode;
  (void)usb_powered;
  const bool beacon_context_ok = true;
#endif
  const bool beacon_paused = ble_beacon_switch_on && !beacon_context_ok;
  // Bluetooth holds internal RAM while on (on the S3, latter_beacon starts only the controller; esp32_ble's
  // Bluedroid host held ~45 KB in total), so the beacon starts 20 s after boot, once Wi-Fi, wake word and
  // Sendspin have finished allocating.
  const bool beacon_wanted = ble_beacon_switch_on && !beacon_paused && (now_ms > 20000U);
  if (ble_ctrl != nullptr) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    // ESP32-P4 Bluetooth runs on the C6 over SDIO (esp_hosted); wait for the link.
    const bool sdio_ready = wifi_connected && (now_ms > 8000U);
    const bool want_ble = beacon_wanted && sdio_ready;
#else
    (void)wifi_connected;
    const bool want_ble = beacon_wanted;
#endif
    if (want_ble && !ble_ctrl->is_active() && !ble_ctrl->is_failed()) {
      ESP_LOGI("ble", "Enabling BLE radio (Area Presence Beacon)");
      ble_ctrl->enable();
    } else if (!want_ble && ble_ctrl->is_active()) {
      ESP_LOGI("ble", "Disabling BLE radio (beacon %s)",
               !beacon_paused ? "off" : (usb_powered ? "paused while docked" : "paused for screen-off on battery"));
      ble_ctrl->disable();
    }
  }

  // 2. Wi-Fi modem sleep (roaming / battery devices only; see apply_wifi_power_policy)
#ifdef USE_WIFI
  if (now_ms > 6000U) apply_wifi_power_policy(wifi, high_tput_needed, display_power_state);
#else
  (void)wifi;
  (void)display_power_state;
  (void)high_tput_needed;
#endif
}

template <typename DispT>
inline void hal_panel_dcs_set_sleep(DispT *disp, bool sleep);

template <typename DispT>
inline void hal_set_display_brightness(DispT *disp, uint8_t bri_byte) {
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_EN_PIN)
  gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_EN_PIN), (bri_byte > 0) ? 1 : 0);
#endif
#if defined(USE_ESP32) && defined(LATTER_BACKLIGHT_PIN)
  // Board-owned backlight PWM traits (defaults: 2 kHz, 8-bit, full 0..100% duty window).
  // Analog-dimming backlights (PWM -> RC filter -> boost FB) need a PWM period well below the
  // RC time constant, so boards declare LATTER_BACKLIGHT_PWM_HZ / _BITS / _DUTY_MIN_PCT / _DUTY_MAX_PCT.
  constexpr uint32_t kPwmBits = static_cast<uint32_t>(LATTER_BACKLIGHT_PWM_BITS);
  static_assert(kPwmBits >= 8 && kPwmBits <= 13, "LATTER_BACKLIGHT_PWM_BITS must be 8..13");
  static_assert(LATTER_BACKLIGHT_DUTY_MIN_PCT >= 0 && LATTER_BACKLIGHT_DUTY_MIN_PCT < LATTER_BACKLIGHT_DUTY_MAX_PCT &&
                    LATTER_BACKLIGHT_DUTY_MAX_PCT <= 100,
                "LATTER_BACKLIGHT_DUTY_MIN_PCT/_MAX_PCT must satisfy 0 <= MIN < MAX <= 100");
  constexpr uint32_t kMaxDuty = (1UL << kPwmBits) - 1UL;
  constexpr uint32_t kOnMin = (kMaxDuty * static_cast<uint32_t>(LATTER_BACKLIGHT_DUTY_MIN_PCT)) / 100UL;
  constexpr uint32_t kOnMax = (kMaxDuty * static_cast<uint32_t>(LATTER_BACKLIGHT_DUTY_MAX_PCT)) / 100UL;
  const uint32_t on_duty = (bri_byte == 0)
      ? 0UL
      : kOnMin + (((kOnMax - kOnMin) * static_cast<uint32_t>(bri_byte)) + 127UL) / 255UL;
#if defined(LATTER_BACKLIGHT_INVERTED) && LATTER_BACKLIGHT_INVERTED
  // Inverted (analog-dimming) backlight: higher duty = dimmer; brightness 0 parks at full duty
  // while LATTER_BACKLIGHT_EN_PIN (if present) switches the boost converter fully off.
  const uint32_t hw_duty = kMaxDuty - on_duty;
  const int gpio_on_lvl = 0;
  const int gpio_off_lvl = 1;
#else
  const uint32_t hw_duty = on_duty;
  const int gpio_on_lvl = 1;
  const int gpio_off_lvl = 0;
#endif
  static bool ledc_timer_inited = false;
  static bool ledc_channel_inited = false;
  static int64_t last_hw_duty = -1;
  if (!ledc_timer_inited) {
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_timer.duty_resolution = static_cast<ledc_timer_bit_t>(kPwmBits);
    ledc_timer.timer_num = LEDC_TIMER_0;
    ledc_timer.freq_hz = static_cast<uint32_t>(LATTER_BACKLIGHT_PWM_HZ);
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&ledc_timer) == ESP_OK) {
      ledc_timer_inited = true;
    }
  }
  if (ledc_timer_inited) {
    if (!ledc_channel_inited) {
      ledc_channel_config_t ledc_channel = {};
      ledc_channel.gpio_num = LATTER_BACKLIGHT_PIN;
      ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
      ledc_channel.channel = LEDC_CHANNEL_0;
      ledc_channel.intr_type = LEDC_INTR_DISABLE;
      ledc_channel.timer_sel = LEDC_TIMER_0;
      ledc_channel.duty = hw_duty;
      ledc_channel.hpoint = 0;
      if (ledc_channel_config(&ledc_channel) == ESP_OK) {
        ledc_channel_inited = true;
        last_hw_duty = hw_duty;
      }
    } else if (last_hw_duty != static_cast<int64_t>(hw_duty)) {
      ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, hw_duty);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      last_hw_duty = hw_duty;
    }
  } else {
    gpio_set_direction(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(LATTER_BACKLIGHT_PIN), (bri_byte > 0) ? gpio_on_lvl : gpio_off_lvl);
  }
#endif
#if !defined(LATTER_BACKLIGHT_PIN)
  static_assert(requires(DispT *d, uint8_t v) { d->set_brightness(v); },
                "Display driver lacks set_brightness() and no LATTER_BACKLIGHT_PIN is defined");
  if (disp != nullptr) {
    if (bri_byte == 0) {
      disp->set_brightness(0);
      hal_panel_dcs_set_sleep(disp, true);
    } else {
      hal_panel_dcs_set_sleep(disp, false);
      disp->set_brightness(bri_byte);
    }
  }
#else
  (void)disp;
#endif
}

// Self-emissive (AMOLED) panels: brightness 0 alone leaves the driver IC and panel power supply running.
// With LATTER_PANEL_DCS_SLEEP=1 (QSPI DCS panels such as CO5300), screen-off sends DISPOFF (0x28) +
// SLPIN (0x10) and screen-on sends SLPOUT (0x11) + DISPON (0x29), using the same QSPI command framing
// as esphome::mipi_spi (cmd 0x02, 24-bit address = DCS << 8). GRAM is retained across sleep.
template <typename DispT>
inline void hal_panel_dcs_set_sleep(DispT *disp, bool sleep) {
#if defined(USE_ESP32) && defined(LATTER_PANEL_DCS_SLEEP) && LATTER_PANEL_DCS_SLEEP
  if constexpr (requires(DispT *d, const uint8_t *b) {
                  d->enable();
                  d->disable();
                  d->write_cmd_addr_data(size_t{8}, uint32_t{0x02}, size_t{24}, uint32_t{0}, b, size_t{0});
                }) {
    static bool asleep = false;
    static uint32_t last_wake_ms = 0;
    if (disp == nullptr || sleep == asleep || !disp->is_ready()) return;
    auto dcs = [disp](uint8_t cmd) {
      uint8_t dummy = cmd;
      disp->enable();
      disp->write_cmd_addr_data(8, 0x02, 24, static_cast<uint32_t>(cmd) << 8, &dummy, 0);
      disp->disable();
    };
    if (sleep) {
      // DCS timing: at least 120 ms between SLPOUT and SLPIN.
      const uint32_t since_wake = esphome::millis() - last_wake_ms;
      if (last_wake_ms != 0 && since_wake < 120U) esphome::delay(120U - since_wake);
      dcs(0x28);
      dcs(0x10);
      asleep = true;
      if (profiler_enabled()) ESP_LOGI("PROF", "[PROF:PANEL] dcs=sleep");
    } else {
      dcs(0x11);
      // DCS timing: the panel's internal power-up runs after SLPOUT; SH8601-class AMOLEDs (S3-2.06)
      // ignore DISPON sent before ~120 ms and stay dark. Wait the full 120 ms before DISPON.
      esphome::delay(120);
      dcs(0x29);
      last_wake_ms = esphome::millis();
      asleep = false;
      if (profiler_enabled()) ESP_LOGI("PROF", "[PROF:PANEL] dcs=wake");
    }
  } else {
    static bool warned = false;
    if (!warned) {
      warned = true;
      ESP_LOGW("power", "LATTER_PANEL_DCS_SLEEP set but the display driver exposes no QSPI command path; panel sleep disabled");
    }
    (void)disp;
    (void)sleep;
  }
#else
  (void)disp;
  (void)sleep;
#endif
}

template <typename DispT>
inline void hal_set_display_brightness(DispT *const *disp_pp, uint8_t bri) {
  if (disp_pp != nullptr) hal_set_display_brightness(*disp_pp, bri);
}

// =============================================================================
// FLUID 4-ICON HOME DOCK & UNIFIED IN-APP SEGMENTED MODE BAR GEOMETRY
// =============================================================================
inline int get_home_dock_start_index(int total_count, int page) {
  if (total_count <= 4 || page <= 0) return 0;
  int total_pages = (total_count + 3) / 4;
  int eff_page = (page >= total_pages) ? (total_pages - 1) : page;
  int raw_start = eff_page * 4;
  if (raw_start + 4 > total_count) {
    raw_start = total_count - 4;
  }
  return std::max(0, raw_start);
}

inline void get_home_dock_window_xs(int total_count, int page, int cx, int out_xs[4],
                                    bool &out_has_left_arrow, bool &out_has_right_arrow) {
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  int start_idx = get_home_dock_start_index(total_count, page);
  int vis_count = std::min(4, total_count - start_idx);
  if (vis_count == 1) {
    out_xs[0] = cx;
  } else if (vis_count == 2) {
    out_xs[0] = cx - scale_x(44);
    out_xs[1] = cx + scale_x(44);
  } else if (vis_count == 3) {
    out_xs[0] = cx - scale_x(84);
    out_xs[1] = cx;
    out_xs[2] = cx + scale_x(84);
  } else {
#if LATTER_ROUND_DISPLAY
    out_xs[0] = 112;
    out_xs[1] = 193;
    out_xs[2] = 274;
    out_xs[3] = 355;
#else
    const int stride = (SCREEN_W < 380) ? 74 : scale_x(98);
    out_xs[0] = cx - (stride * 3) / 2;
    out_xs[1] = cx - (stride / 2);
    out_xs[2] = cx + (stride / 2);
    out_xs[3] = cx + (stride * 3) / 2;
#endif
  }
}

// Unified In-App Segmented Mode Bar (Option A: Stadium Pill + 1-Step Conveyor Window)
// Floating circles on Page 1 launch apps; connected stadium pill tracks inside
// apps (Sky & Weather, Area Control, Whole-Home Hub) switch the middle view.
inline int get_mode_bar_start_index(int total_count, int win_start) {
  if (total_count <= 4 || win_start <= 0) return 0;
  return std::max(0, std::min(total_count - 4, win_start));
}

inline void sync_mode_bar_window(int &win_start, int active_idx, int total_count) {
  if (total_count <= 4) {
    win_start = 0;
    return;
  }
  win_start = get_mode_bar_start_index(total_count, win_start);
  if (active_idx < win_start) {
    win_start = active_idx;
  } else if (active_idx >= win_start + 4) {
    win_start = active_idx - 3;
  }
  win_start = std::max(0, std::min(total_count - 4, win_start));
}

inline void get_mode_bar_window_xs(int total_count, int win_start, int cx, int out_xs[4],
                                   bool &out_has_left_arrow, bool &out_has_right_arrow) {
  out_has_left_arrow = false;
  out_has_right_arrow = false;
  if (total_count <= 0) return;
  int start_idx = get_mode_bar_start_index(total_count, win_start);
  int vis_count = std::min(4, total_count - start_idx);
  bool dl = false, dr = false;
  get_home_dock_window_xs(vis_count, 0, cx, out_xs, dl, dr);
  out_has_left_arrow  = (start_idx > 0);
  out_has_right_arrow = (start_idx + 4 < total_count);
}

// =============================================================================
// SMOOTH HORIZONTAL CONVEYOR SLIDE & CAPSULE GLIDE ANIMATOR (ESP32-P4 & S3)
// =============================================================================
struct DockSlideAnimator {
  float cur_start = 0.0f;
  float from_start = 0.0f;
  int target_start = -1;
  uint32_t win_start_ms = 0;
  bool win_animating = false;

  float cur_active = 0.0f;
  float from_active = 0.0f;
  int target_active = -1;
  uint32_t act_start_ms = 0;
  bool act_animating = false;

  // Duration and curve come from the one motion system in ui_core.h (kMotionMs, ease_out_cubic).

  void reset() {
    target_start = -1;
    target_active = -1;
    win_animating = false;
    act_animating = false;
  }

  float step_window(int new_start, uint32_t now_ms) {
    if (target_start < 0 || std::abs(new_start - target_start) > 4) {
      target_start = new_start;
      from_start = static_cast<float>(new_start);
      cur_start = from_start;
      win_animating = false;
      return cur_start;
    }
    if (new_start != target_start) {
      from_start = cur_start;
      target_start = new_start;
      win_start_ms = now_ms;
      win_animating = true;
    }
    if (win_animating) {
      uint32_t dt = now_ms - win_start_ms;
      if (dt >= kMotionMs) {
        cur_start = static_cast<float>(target_start);
        win_animating = false;
      } else {
        float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kMotionMs));
        cur_start = from_start + (static_cast<float>(target_start) - from_start) * e;
      }
    } else {
      cur_start = static_cast<float>(target_start);
    }
    return cur_start;
  }

  float step_active(int new_active, uint32_t now_ms) {
    if (target_active < 0 || std::abs(new_active - target_active) > 6) {
      target_active = new_active;
      from_active = static_cast<float>(new_active);
      cur_active = from_active;
      act_animating = false;
      return cur_active;
    }
    if (new_active != target_active) {
      from_active = cur_active;
      target_active = new_active;
      act_start_ms = now_ms;
      act_animating = true;
    }
    if (act_animating) {
      uint32_t dt = now_ms - act_start_ms;
      if (dt >= kMotionMs) {
        cur_active = static_cast<float>(target_active);
        act_animating = false;
      } else {
        float e = ease_out_cubic(static_cast<float>(dt) / static_cast<float>(kMotionMs));
        cur_active = from_active + (static_cast<float>(target_active) - from_active) * e;
      }
    } else {
      cur_active = static_cast<float>(target_active);
    }
    return cur_active;
  }

  bool is_animating() const { return win_animating || act_animating; }
};

inline DockSlideAnimator &home_dock_anim() {
  static DockSlideAnimator anim;
  return anim;
}

inline DockSlideAnimator &mode_bar_anim() {
  static DockSlideAnimator anim;
  return anim;
}

inline bool any_dock_anim_active(int gui_page = -1, int home_subpage = 0) {
  if (gui_page >= 0) {
    if (gui_page != 1 || home_subpage != 0) {
      home_dock_anim().win_animating = false;
      home_dock_anim().act_animating = false;
    }
    if (gui_page != 5 && gui_page != 6 && gui_page != 9) {
      mode_bar_anim().win_animating = false;
      mode_bar_anim().act_animating = false;
    }
  }
  return home_dock_anim().is_animating() || mode_bar_anim().is_animating();
}

inline int get_dock_slot_stride(const int *win_xs, int vis_count) {
  if (vis_count >= 2) return win_xs[1] - win_xs[0];
#if LATTER_ROUND_DISPLAY
  return 81;
#else
  return (SCREEN_W < 380) ? 74 : scale_x(98);
#endif
}

template <typename DisplayT>
inline void render_in_app_mode_bar_track(
    DisplayT &it, int cx, const int *win_xs, int vis_count, int active_vis_idx,
    bool has_left_arrow, bool has_right_arrow, int total_count, int active_global_idx,
    esphome::Color theme_primary, esphome::Color col_white, int explicit_win_start = -1) {
  using esphome::Color;
  if (vis_count <= 0) return;

  const uint32_t now_ms = esphome::millis();
  const int win_start_idx = (explicit_win_start >= 0)
                                ? explicit_win_start
                                : ((active_vis_idx >= 0) ? (active_global_idx - active_vis_idx) : 0);
  float anim_start  = mode_bar_anim().step_window(win_start_idx, now_ms);
  float anim_active = mode_bar_anim().step_active(active_global_idx, now_ms);

  const int by = layout().app_dock_y;
  const int dr = layout().dock_r;
  const int r_track = dr + scale_s(3);
  const int left_cx  = win_xs[0];
  const int right_cx = win_xs[vis_count - 1];
  const int stride   = get_dock_slot_stride(win_xs, vis_count);
  const Color track_bg = kChromeBarTrack;
  const Color track_brd = kChromeBarBorder;
  const Color cap_bg = kChromeBarTrack;

  // 1. Continuous Scanline Stadium Pill Track connecting left_cx .. right_cx
  for (int dy = -r_track; dy <= r_track; dy++) {
    int py = by + dy;
    if (py < 0 || py >= SCREEN_H) continue;
    int dx_cap = chord_half_span_int(r_track, dy, false);
    int x0 = left_cx - dx_cap;
    int x1 = right_cx + dx_cap;
    if (x1 > x0) {
      fast_hline(it, x0, py, x1 - x0 + 1, track_bg);
      if (std::abs(dy) == r_track) {
        fast_hline(it, left_cx, py, right_cx - left_cx + 1, track_brd);
      } else {
        fast_pixel(it, x0, py, track_brd);
        fast_pixel(it, x1, py, track_brd);
      }
    }
  }

  // 2. Subtle vertical slot dividers between inactive slots inside the track
  const int div_h = dr - scale_s(6);
  const Color div_col = kChromeTrack;
  const float rel_active = anim_active - anim_start;
  for (int i = 0; i < vis_count - 1; i++) {
    if (std::fabs(static_cast<float>(i) - rel_active) < 0.65f ||
        std::fabs(static_cast<float>(i + 1) - rel_active) < 0.65f) {
      continue;
    }
    int mid_x = (win_xs[i] + win_xs[i + 1]) / 2;
    fast_vline(it, mid_x, by - div_h, div_h * 2 + 1, div_col);
  }

  // 3. Smoothly Gliding Active Slot Capsule Highlight inside the track
  if (rel_active >= -0.55f && rel_active <= static_cast<float>(vis_count - 1) + 0.55f) {
    int abx = left_cx + static_cast<int>(std::roundf(rel_active * static_cast<float>(stride)));
    if (abx >= left_cx - scale_x(16) && abx <= right_cx + scale_x(16)) {
      float ring_alpha = 1.0f;
      if (rel_active < 0.0f) {
        ring_alpha = std::max(0.20f, 1.0f + rel_active * 1.6f);
      } else if (rel_active > static_cast<float>(vis_count - 1)) {
        ring_alpha = std::max(0.20f, 1.0f - (rel_active - static_cast<float>(vis_count - 1)) * 1.6f);
      }
      Color ring_col(static_cast<uint8_t>(track_bg.r + (theme_primary.r - track_bg.r) * ring_alpha),
                     static_cast<uint8_t>(track_bg.g + (theme_primary.g - track_bg.g) * ring_alpha),
                     static_cast<uint8_t>(track_bg.b + (theme_primary.b - track_bg.b) * ring_alpha));
      fast_filled_circle(it, abx, by, dr - 1, cap_bg);
      fast_circle(it, abx, by, dr, ring_col);
      fast_circle(it, abx, by, dr - 1, ring_col);
    }
  }

  // 4. Outer Left '<' and Right '>' Conveyor Chevrons when total_count > 4
  const int chev_off = dr + ((SCREEN_W < 380) ? 10 : scale_x(13));
  const int ch_s = scale_s(5);
  if (has_left_arrow) {
    int lx = left_cx - chev_off;
    fast_line(it, lx + ch_s / 2, by - ch_s, lx - ch_s / 2, by, col_white);
    fast_line(it, lx - ch_s / 2, by, lx + ch_s / 2, by + ch_s, col_white);
    fast_line(it, lx + ch_s / 2 + 1, by - ch_s, lx - ch_s / 2 + 1, by, theme_primary);
    fast_line(it, lx - ch_s / 2 + 1, by, lx + ch_s / 2 + 1, by + ch_s, theme_primary);
  }
  if (has_right_arrow) {
    int rx = right_cx + chev_off;
    fast_line(it, rx - ch_s / 2, by - ch_s, rx + ch_s / 2, by, col_white);
    fast_line(it, rx + ch_s / 2, by, rx - ch_s / 2, by + ch_s, col_white);
    fast_line(it, rx - ch_s / 2 - 1, by - ch_s, rx + ch_s / 2 - 1, by, theme_primary);
    fast_line(it, rx + ch_s / 2 - 1, by, rx - ch_s / 2 - 1, by + ch_s, theme_primary);
  }

  // 5. Proportional Viewport Scrollbar below the Mode Bar when total_count > 4
  if (total_count > 4) {
    render_viewport_scrollbar(it, cx, layout().app_dock_pips_y, total_count, anim_start,
                              theme_primary, false);
  }
}

// ==============================================================================
// 6. Universal Touchless / Rotary Encoder / Button Focus Navigation Engine
// ==============================================================================
// Home dock state, defined in ui_clock_home.h (the focus ring reads the same dock window).
inline int &home_dock_page();
inline int collect_home_dock_apps(int out_apps[8]);

inline int &ui_focus_index() {
  static int s_idx = 0;
  return s_idx;
}

inline uint32_t &ui_focus_last_ms() {
  static uint32_t s_ms = 0;
  return s_ms;
}

inline bool ui_focus_active(uint32_t now_ms) {
#if !LATTER_HAS_TOUCH
  (void) now_ms;
  return true;
#else
  const uint32_t last = ui_focus_last_ms();
  return (last > 0) && ((now_ms - last) < 8000U);
#endif
}

inline void activate_ui_focus(uint32_t now_ms) {
  ui_focus_last_ms() = (now_ms == 0) ? 1U : now_ms;
}

inline void clear_ui_focus() {
#if LATTER_HAS_TOUCH
  ui_focus_last_ms() = 0;
#endif
}

template<typename DisplayT>
inline void render_focus_ring_circle(DisplayT &it, int cx, int cy, int r, esphome::Color accent, esphome::Color inner_hi) {
  fast_circle(it, cx, cy, r + 2, accent);
  fast_circle(it, cx, cy, r + 3, accent);
  fast_circle(it, cx, cy, r + 1, inner_hi);
}

template<typename DisplayT>
inline void render_focus_ring_rect(DisplayT &it, int cx, int cy, int w, int h, esphome::Color accent, esphome::Color inner_hi) {
  const int x0 = cx - w / 2;
  const int y0 = cy - h / 2;
  fast_rect_outline(it, x0 - 2, y0 - 2, w + 4, h + 4, accent);
  fast_rect_outline(it, x0 - 3, y0 - 3, w + 6, h + 6, accent);
  fast_rect_outline(it, x0 - 1, y0 - 1, w + 2, h + 2, inner_hi);
}

template<typename DisplayT>
inline void render_active_focus_overlay(DisplayT &it, int page, int subpage, uint32_t now_ms,
                                        esphome::Color theme_primary, esphome::Color col_white) {
  if (!ui_focus_active(now_ms)) return;
  const auto &ly = layout();
  const int f_idx = std::max(0, ui_focus_index());

  if (page == 1 && subpage == 0) {
    // Home Dock Icon Focus: the ring sits on the icon `select` opens (same window and slots as the dock).
    int dock_apps[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const int dock_count = collect_home_dock_apps(dock_apps);
    if (dock_count > 0) {
      int win_xs[4] = {ly.cx, ly.cx, ly.cx, ly.cx};
      bool has_l = false, has_r = false;
      get_home_dock_window_xs(dock_count, home_dock_page(), ly.cx, win_xs, has_l, has_r);
      const int vis_count = std::min(4, dock_count - get_home_dock_start_index(dock_count, home_dock_page()));
      const int vis_idx = f_idx % std::max(1, vis_count);
      render_focus_ring_circle(it, win_xs[vis_idx], ly.dock_y, ly.dock_r, col_white, theme_primary);
    }
  } else if (page == 2) {
    if (subpage == 0) {
      // Music Now Playing Transport Controls (0: Prev, 1: Play/Pause, 2: Next, 3: Library, 4: Speakers)
      const int idx = f_idx % 5;
      if (idx == 0) render_focus_ring_circle(it, ly.cx - scale_x(86), scale_y(310), scale_s(26), col_white, theme_primary);
      else if (idx == 1) render_focus_ring_circle(it, ly.cx, scale_y(310), scale_s(34), col_white, theme_primary);
      else if (idx == 2) render_focus_ring_circle(it, ly.cx + scale_x(86), scale_y(310), scale_s(26), col_white, theme_primary);
      else if (idx == 3) render_focus_ring_circle(it, ly.cx - scale_x(64), ly.app_dock_y, scale_s(24), col_white, theme_primary);
      else render_focus_ring_circle(it, ly.cx + scale_x(64), ly.app_dock_y, scale_s(24), col_white, theme_primary);
    } else {
      const int slot = f_idx % 5;
      render_focus_ring_rect(it, ly.cx, ly.slot5_y(slot), ly.list_card_w, ly.slot5_h, col_white, theme_primary);
    }
  } else if ((page == 3 && subpage == 0) || (page == 8 && subpage == 0)) {
    // 2x3 Launcher Grid Focus (Games & Tools)
    const int slot = f_idx % 6;
    const int col = slot % 2;
    const int row = slot / 2;
    const int gcx = ly.cx + ((col == 0) ? -ly.tools_col_off : ly.tools_col_off);
    const int gcy = ly.tools_row_y(row);
    const int gw = (SCREEN_W < 380) ? 110 : scale_x(122);
    render_focus_ring_rect(it, gcx, gcy, gw, scale_y(86), col_white, theme_primary);
  } else if (page == 4 || (page == 8 && (subpage >= 9 && subpage <= 11))) {
    // 5-Slot Vertical Menu List Focus
    const int slot = f_idx % 5;
    render_focus_ring_rect(it, ly.cx, ly.slot5_y(slot), ly.list_card_w, ly.slot5_h, col_white, theme_primary);
  } else if (page == 6) {
    if (subpage == 1) {
      const int slot = f_idx % 5;
      render_focus_ring_rect(it, ly.cx, ly.slot5_y(slot), ly.list_card_w, ly.slot5_h, col_white, theme_primary);
    } else {
      const int slot = f_idx % 7;
      if (slot < 3) {
        render_focus_ring_rect(it, ly.cx, ly.area_card3_y(slot), ly.area_card3_w, ly.area_card3_h, col_white, theme_primary);
      } else {
        const int m_idx = (slot - 3) % 4;
        const int stride = (SCREEN_W < 380) ? 56 : scale_x(68);
        const int fx = ly.cx + static_cast<int>((static_cast<float>(m_idx) - 1.5f) * static_cast<float>(stride));
        render_focus_ring_circle(it, fx, ly.app_dock_y, ly.dock_r, col_white, theme_primary);
      }
    }
  } else if (page == 5 || page == 9) {
    const int m_idx = f_idx % 4;
    const int stride = (SCREEN_W < 380) ? 56 : scale_x(68);
    const int fx = ly.cx + static_cast<int>((static_cast<float>(m_idx) - 1.5f) * static_cast<float>(stride));
    render_focus_ring_circle(it, fx, ly.app_dock_y, ly.dock_r, col_white, theme_primary);
  } else if (page == 7) {
    // TV Remote D-Pad & Action Focus
    const int slot = f_idx % 8;
    if (slot == 0) render_focus_ring_circle(it, ly.cx - scale_x(108), scale_y(118), scale_s(24), col_white, theme_primary);
    else if (slot == 1) render_focus_ring_circle(it, ly.cx, scale_y(158), scale_s(24), col_white, theme_primary);
    else if (slot == 2) render_focus_ring_circle(it, ly.cx - scale_x(68), scale_y(226), scale_s(24), col_white, theme_primary);
    else if (slot == 3) render_focus_ring_circle(it, ly.cx, scale_y(226), scale_s(32), col_white, theme_primary);
    else if (slot == 4) render_focus_ring_circle(it, ly.cx + scale_x(68), scale_y(226), scale_s(24), col_white, theme_primary);
    else if (slot == 5) render_focus_ring_circle(it, ly.cx, scale_y(294), scale_s(24), col_white, theme_primary);
    else if (slot == 6) render_focus_ring_circle(it, ly.cx - scale_x(82), scale_y(352), scale_s(24), col_white, theme_primary);
    else render_focus_ring_circle(it, ly.cx + scale_x(82), scale_y(352), scale_s(24), col_white, theme_primary);
  }
}

// ==============================================================================
// 7. Unified OTA Firmware Update Display & Poller Lifecycle Helpers
// ==============================================================================
template<typename DispT>
inline void handle_ota_begin(DispT *disp, bool &ota_in_progress, int &ota_progress_pct,
                             uint32_t &sleep_start_time, int &display_power_state) {
  ota_in_progress = true;
  stall_guard_pause(true);
  wifi_power_ota_begin();
  ota_progress_pct = 0;
  sleep_start_time = 0;
  screen_is_off_standby() = false;
  disp_poller_held() = true;
  display_power_state = 0;
  if (disp != nullptr) {
    disp->stop_poller();
    hal_set_display_brightness(disp, 230);
  }
}

template<typename DispT>
inline void handle_ota_begin(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct,
                             uint32_t &sleep_start_time, int &display_power_state) {
  if (disp_ptr != nullptr) {
    handle_ota_begin(*disp_ptr, ota_in_progress, ota_progress_pct, sleep_start_time, display_power_state);
  }
}

template<typename DispT>
inline void handle_ota_progress(DispT *disp, float raw_pct, int &ota_progress_pct, uint32_t now_m) {
  static uint32_t last_ota_draw_ms = 0;
  int pct = static_cast<int>(raw_pct);
#if defined(LATTER_DSI_IRAM_SAFE)
  constexpr int min_step_pct = 10;
  constexpr uint32_t min_step_ms = 800U;
#else
  constexpr int min_step_pct = 5;
  constexpr uint32_t min_step_ms = 400U;
#endif
  if (pct != ota_progress_pct &&
      (pct == 100 || (pct - ota_progress_pct >= min_step_pct && (now_m - last_ota_draw_ms >= min_step_ms)) || pct < ota_progress_pct)) {
    ota_progress_pct = pct;
    last_ota_draw_ms = now_m;
    if (disp != nullptr) disp->update();
  }
}

template<typename DispT>
inline void handle_ota_progress(DispT *const *disp_ptr, float raw_pct, int &ota_progress_pct, uint32_t now_m) {
  if (disp_ptr != nullptr) handle_ota_progress(*disp_ptr, raw_pct, ota_progress_pct, now_m);
}

template<typename DispT>
inline void handle_ota_end(DispT *disp, bool &ota_in_progress, int &ota_progress_pct) {
  if (ota_in_progress) {
    ota_progress_pct = 100;
  }
  disp_poller_held() = false;
  if (disp != nullptr) {
    disp->update();
    disp->start_poller();
    // A successful OTA reboots immediately. Hand the panel to the new firmware in the same state as the
    // "Restart Device" path (brightness 0 + DCS sleep / backlight off) rather than awake mid-frame;
    // AMOLED panels left awake across an OTA reboot would not come back from their next DCS sleep.
    hal_set_display_brightness(disp, 0);
  }
  ota_in_progress = false;
  stall_guard_pause(false);
  wifi_power_ota_end();
}

template<typename DispT>
inline void handle_ota_end(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct) {
  if (disp_ptr != nullptr) handle_ota_end(*disp_ptr, ota_in_progress, ota_progress_pct);
}

template<typename DispT>
inline void handle_ota_error(DispT *disp, bool &ota_in_progress, int &ota_progress_pct,
                             bool &notif_active, std::string &notif_title, std::string &notif_message,
                             std::string &notif_priority, uint32_t &notif_duration_ms,
                             uint32_t &notif_start_ms, uint32_t now_m) {
  ota_in_progress = false;
  ota_progress_pct = 0;
  notif_title = "Update failed";
  notif_message = "Firmware update interrupted";
  notif_priority = "warning";
  notif_duration_ms = 5000U;
  notif_start_ms = now_m;
  notif_active = true;
  handle_ota_end(disp, ota_in_progress, ota_progress_pct);
}

template<typename DispT>
inline void handle_ota_error(DispT *const *disp_ptr, bool &ota_in_progress, int &ota_progress_pct,
                             bool &notif_active, std::string &notif_title, std::string &notif_message,
                             std::string &notif_priority, uint32_t &notif_duration_ms,
                             uint32_t &notif_start_ms, uint32_t now_m) {
  if (disp_ptr != nullptr) {
    handle_ota_error(*disp_ptr, ota_in_progress, ota_progress_pct,
                     notif_active, notif_title, notif_message,
                     notif_priority, notif_duration_ms, notif_start_ms, now_m);
  }
}

}  // namespace latter_ui


