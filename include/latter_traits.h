#pragma once

// ==============================================================================
// Latter Core Canonical Hardware & Capability Trait Defaults (latter_traits.h)
// ==============================================================================
// Pure C++20 leaf header (zero project includes). Consolidates every board-level
// `#ifndef LATTER_*` fallback macro shared across HAL, Core state, and UI layers.
// On ESP32/ESPHome builds, `latter_core:` emits board defines into
// `esphome/core/defines.h` which is pulled in first so board traits always win.
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#if (defined(USE_ESP32) || defined(ESP_PLATFORM)) && __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

// ------------------------------------------------------------------------------
// Required Board Identity & Display Geometry Traits
// ------------------------------------------------------------------------------
#ifdef USE_ESP32
#ifndef LATTER_BOARD_ID
#error "Missing required board trait LATTER_BOARD_ID (configure latter_core: board_id)"
#endif
#ifndef LATTER_SCREEN_W
#error "Missing required board trait LATTER_SCREEN_W (configure latter_core: display: width)"
#endif
#ifndef LATTER_SCREEN_H
#error "Missing required board trait LATTER_SCREEN_H (configure latter_core: display: height)"
#endif
#ifndef LATTER_PANEL_PPI
#error "Missing required board trait LATTER_PANEL_PPI (configure latter_core: display: ppi)"
#endif
#else
#ifndef LATTER_BOARD_ID
#define LATTER_BOARD_ID "generic_esp32"
#endif
#ifndef LATTER_SCREEN_W
#define LATTER_SCREEN_W 466
#endif
#ifndef LATTER_SCREEN_H
#define LATTER_SCREEN_H 466
#endif
#ifndef LATTER_PANEL_PPI
#define LATTER_PANEL_PPI 266
#endif
#endif

// ------------------------------------------------------------------------------
// Chip / SoC Target Traits
// ------------------------------------------------------------------------------
#ifndef LATTER_CHIP_P4
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_CHIP_P4 1
#else
#define LATTER_CHIP_P4 0
#endif
#endif

#ifndef LATTER_SOC_S3
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define LATTER_SOC_S3 1
#else
#define LATTER_SOC_S3 0
#endif
#endif

// ------------------------------------------------------------------------------
// Hardware Capability Flags
// ------------------------------------------------------------------------------
#ifndef LATTER_HAS_DISPLAY
#define LATTER_HAS_DISPLAY 1
#endif
#ifndef LATTER_HAS_TOUCH
#define LATTER_HAS_TOUCH 1
#endif
#ifndef LATTER_HAS_AUDIO
#define LATTER_HAS_AUDIO 1
#endif
#ifndef LATTER_HAS_MIC
#define LATTER_HAS_MIC LATTER_HAS_AUDIO
#endif
#ifndef LATTER_HAS_SPEAKER
#define LATTER_HAS_SPEAKER LATTER_HAS_AUDIO
#endif
#ifndef LATTER_HAS_IMU
#define LATTER_HAS_IMU 1
#endif
#ifndef LATTER_HAS_BATTERY
#define LATTER_HAS_BATTERY 1
#endif
#ifndef LATTER_BATTERY_MAH
#define LATTER_BATTERY_MAH 0
#endif
#ifndef LATTER_HAS_RTC
#define LATTER_HAS_RTC 0
#endif
#ifndef LATTER_IS_STATIONARY
#define LATTER_IS_STATIONARY 0
#endif
#ifndef LATTER_IS_WEARABLE
#define LATTER_IS_WEARABLE 0
#endif
#ifndef LATTER_HAS_VIDEO_STREAM
#define LATTER_HAS_VIDEO_STREAM LATTER_HAS_DISPLAY
#endif
#ifndef LATTER_HAS_HW_JPEG
#define LATTER_HAS_HW_JPEG LATTER_CHIP_P4
#endif
#ifndef LATTER_VIDEO_MAX_VIEWPORT_DIM
#define LATTER_VIDEO_MAX_VIEWPORT_DIM (LATTER_HAS_HW_JPEG ? 768 : 512)
#endif

// ------------------------------------------------------------------------------
// Display Panel, UI Build, Memory & Beacon Traits
// ------------------------------------------------------------------------------
#ifndef LATTER_GUI_UNIVERSAL
#define LATTER_GUI_UNIVERSAL 1
#endif
#ifndef LATTER_PSRAM_BSS
#define LATTER_PSRAM_BSS
#endif
#ifndef LATTER_PSRAM_MB
#if defined(USE_ESP32) && !defined(CONFIG_SPIRAM)
#define LATTER_PSRAM_MB 0
#else
#define LATTER_PSRAM_MB (LATTER_CHIP_P4 ? 32 : 8)
#endif
#endif
#ifndef LATTER_ART_CACHE_ENTRIES
#define LATTER_ART_CACHE_ENTRIES ((LATTER_CHIP_P4 || LATTER_PSRAM_MB >= 16) ? 6 : ((LATTER_PSRAM_MB >= 8) ? 2 : 1))
#endif
#ifndef LATTER_ROUND_DISPLAY
#define LATTER_ROUND_DISPLAY 1
#endif
#ifndef LATTER_PANEL_IPS
#if defined(LATTER_BACKLIGHT_PIN)
#define LATTER_PANEL_IPS 1
#else
#define LATTER_PANEL_IPS 0
#endif
#endif
#ifndef LATTER_CORNER_R
#if LATTER_ROUND_DISPLAY
#define LATTER_CORNER_R (LATTER_SCREEN_W / 2)
#elif LATTER_IS_WEARABLE
#define LATTER_CORNER_R 98
#else
#define LATTER_CORNER_R 0
#endif
#endif
#ifndef LATTER_VIEW_SCALE_PCT
#if LATTER_IS_STATIONARY
#define LATTER_VIEW_SCALE_PCT 145
#else
#define LATTER_VIEW_SCALE_PCT 100
#endif
#endif
#ifndef LATTER_SLOT_COUNT
#define LATTER_SLOT_COUNT 8
#endif
#ifndef LATTER_CARD_SLOT_COUNT
#define LATTER_CARD_SLOT_COUNT 3
#endif
#ifndef LATTER_BLE_MINOR
#define LATTER_BLE_MINOR 1
#endif

// ------------------------------------------------------------------------------
// Audio, Backlight & Power Hardware Traits
// ------------------------------------------------------------------------------
#ifndef LATTER_AUDIO_OUT_RATE
#define LATTER_AUDIO_OUT_RATE 48000
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
#ifndef LATTER_BACKLIGHT_DUTY_MIN_PM
#define LATTER_BACKLIGHT_DUTY_MIN_PM 0
#endif
#ifndef LATTER_BACKLIGHT_DUTY_MAX_PM
#define LATTER_BACKLIGHT_DUTY_MAX_PM 1000
#endif
#ifndef LATTER_BACKLIGHT_EN_INVERTED
#define LATTER_BACKLIGHT_EN_INVERTED 0
#endif
#ifndef LATTER_BACKLIGHT_CUSTOM
#define LATTER_BACKLIGHT_CUSTOM 0
#endif
#ifndef LATTER_PWR_KEY_DEEP_SLEEP_WAKE
#define LATTER_PWR_KEY_DEEP_SLEEP_WAKE 0
#endif
#ifndef LATTER_PWR_KEY_ACTIVE_HIGH
#define LATTER_PWR_KEY_ACTIVE_HIGH 1
#endif
#ifndef LATTER_PMU_TS_DISABLE
#define LATTER_PMU_TS_DISABLE 0
#endif

// ------------------------------------------------------------------------------
// Explicit Driver-Identity Guards (default 0 unless opted in via latter_core: drivers:)
// ------------------------------------------------------------------------------
#ifndef LATTER_DRV_PMU_AXP2101
#define LATTER_DRV_PMU_AXP2101 0
#endif
#ifndef LATTER_DRV_RTC_PCF85063
#define LATTER_DRV_RTC_PCF85063 0
#endif
#ifndef LATTER_DRV_IMU_QMI8658
#define LATTER_DRV_IMU_QMI8658 0
#endif
#ifndef LATTER_DRV_CODEC_ES8311
#define LATTER_DRV_CODEC_ES8311 0
#endif
