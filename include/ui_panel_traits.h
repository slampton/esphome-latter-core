#pragma once

// ==============================================================================
// Latter UI Panel Geometry & Hardware Capability Traits (include/ui_panel_traits.h)
// Pure C++20 UI leaf — zero ESPHome, HAL, or runtime-state includes.
// ==============================================================================

#include "latter_traits.h"

#include <cstdint>

namespace latter_ui {

static constexpr int SCREEN_W = LATTER_SCREEN_W;
static constexpr int SCREEN_H = LATTER_SCREEN_H;
static constexpr int SCREEN_CX = SCREEN_W / 2;
static constexpr int SCREEN_CY = SCREEN_H / 2;
static constexpr int SCREEN_MIN_DIM = (SCREEN_W < SCREEN_H) ? SCREEN_W : SCREEN_H;
static constexpr int SCREEN_MIN_R = SCREEN_MIN_DIM / 2;

}  // namespace latter_ui
