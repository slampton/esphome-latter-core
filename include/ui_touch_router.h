#pragma once
// ==============================================================================
// Latter Core SDK - Touch Gesture & Dispatch Router (include/ui_touch_router.h)
// ==============================================================================
// Bridges ESPHome's touchscreen callbacks (on_touch, on_update, on_release) in
// guis/universal/gui.yaml to the centralized gesture & page-routing scripts in
// guis/universal/touch_handlers.yaml while keeping gui.yaml lambdas concise.
// ==============================================================================

#include "ui_core.h"
#include "ui_layout.h"

namespace latter_ui {

struct TouchPt {
  int x = 0;
  int y = 0;
};

struct TouchRouter {
  uint8_t active_touch_count = 0;

  inline bool allow_tap(uint32_t now_ms, uint32_t cooldown_ms = TOUCH_DEBOUNCE_BUTTON) const {
    return touch_allow_tap(now_ms, cooldown_ms);
  }

  template<typename TouchT, typename TouchesT, typename HandlerFn>
  inline void dispatch_press(const TouchT &touch, const TouchesT &touches, HandlerFn &&handler) {
    active_touch_count = static_cast<uint8_t>(touches.size());
    handler(static_cast<int>(touch.x), static_cast<int>(touch.y));
  }

  template<typename TouchesT, typename HandlerFn>
  inline void dispatch_move(const TouchesT &touches, HandlerFn &&handler) {
    active_touch_count = static_cast<uint8_t>(touches.size());
    if (touches.empty()) return;
    handler(static_cast<int>(touches[0].x), static_cast<int>(touches[0].y));
  }

  template<typename HandlerFn>
  inline void dispatch_release(HandlerFn &&handler) {
    active_touch_count = 0;
    handler();
  }
};

inline TouchRouter &touch_router() {
  static TouchRouter s_router;
  return s_router;
}

}  // namespace latter_ui
