#pragma once

// ==============================================================================
// Latter UI Panel Geometry & Hardware Capability Traits (include/ui_panel_traits.h)
// Pure C++20 UI leaf — zero ESPHome, HAL, or runtime-state includes.
// ==============================================================================

#if __has_include("esphome/core/defines.h")
#include "esphome/core/defines.h"
#endif

#include <cstdint>

#ifndef LATTER_GUI_UNIVERSAL
#define LATTER_GUI_UNIVERSAL 1
#endif

#ifndef LATTER_PSRAM_BSS
#define LATTER_PSRAM_BSS
#endif

// Canonical Display Geometry & Hardware Capability Constants (LATTER_* standard)
#ifndef LATTER_SCREEN_W
#define LATTER_SCREEN_W 466
#endif
#ifndef LATTER_SCREEN_H
#define LATTER_SCREEN_H 466
#endif
#ifndef LATTER_ROUND_DISPLAY
#define LATTER_ROUND_DISPLAY 1
#endif
#ifndef LATTER_HAS_IMU
#define LATTER_HAS_IMU 1
#endif
#ifndef LATTER_HAS_BATTERY
#define LATTER_HAS_BATTERY 1
#endif
#ifndef LATTER_IS_STATIONARY
#define LATTER_IS_STATIONARY 0
#endif
#ifndef LATTER_IS_WEARABLE
#define LATTER_IS_WEARABLE 0
#endif
#ifndef LATTER_HAS_TOUCH
#define LATTER_HAS_TOUCH 1
#endif
#ifndef LATTER_HAS_DISPLAY
#define LATTER_HAS_DISPLAY 1
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
#ifndef LATTER_CHIP_P4
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define LATTER_CHIP_P4 1
#else
#define LATTER_CHIP_P4 0
#endif
#endif
#ifndef LATTER_HAS_HW_JPEG
#define LATTER_HAS_HW_JPEG LATTER_CHIP_P4
#endif
#ifndef LATTER_PSRAM_MB
#define LATTER_PSRAM_MB (LATTER_CHIP_P4 ? 32 : 8)
#endif
#ifndef LATTER_PANEL_IPS
#if defined(LATTER_BACKLIGHT_PIN)
#define LATTER_PANEL_IPS 1
#else
#define LATTER_PANEL_IPS 0
#endif
#endif
#ifndef LATTER_PANEL_PPI
#define LATTER_PANEL_PPI 266
#endif
#ifndef LATTER_CORNER_R
#if LATTER_ROUND_DISPLAY
#define LATTER_CORNER_R (LATTER_SCREEN_W / 2)
#elif LATTER_IS_WEARABLE
#define LATTER_CORNER_R 98
#else
#define LATTER_CORNER_R 20
#endif
#endif
#ifndef LATTER_ART_CACHE_ENTRIES
#define LATTER_ART_CACHE_ENTRIES (LATTER_CHIP_P4 ? 3 : 1)
#endif

namespace latter_ui {

static constexpr int SCREEN_W = LATTER_SCREEN_W;
static constexpr int SCREEN_H = LATTER_SCREEN_H;
static constexpr int SCREEN_CX = SCREEN_W / 2;
static constexpr int SCREEN_CY = SCREEN_H / 2;
static constexpr int SCREEN_R = SCREEN_W / 2;
static constexpr int SCREEN_MIN_DIM = (SCREEN_W < SCREEN_H) ? SCREEN_W : SCREEN_H;
static constexpr int SCREEN_MIN_R = SCREEN_MIN_DIM / 2;

}  // namespace latter_ui
