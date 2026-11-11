#if __has_include("ui_hit_table.h")
#include "ui_hit_table.h"
#else
#include "../../include/ui_hit_table.h"
#endif

#include <algorithm>

namespace latter_ui {

namespace {
FrameHitTable g_frame_hits{};
}  // namespace

FrameHitTable &frame_hits() {
  return g_frame_hits;
}

void FrameHitTable::add_rect(int x0, int y0, int w, int h, uint16_t widget_id,
                             uint8_t flags, int16_t param,
                             UiActionFn on_event, void *ctx) {
  if (w <= 0 || h <= 0 || count >= kMaxRegions) return;
  const int rx1 = x0 + w - 1;
  const int ry1 = y0 + h - 1;
  // Deduplicate if the same non-zero widget_id + param is re-registered in the same frame
  if (widget_id != 0) {
    for (uint8_t i = 0; i < count; i++) {
      if (regions[i].widget_id == widget_id && regions[i].param == param) {
        regions[i].x0 = static_cast<int16_t>(x0);
        regions[i].y0 = static_cast<int16_t>(y0);
        regions[i].x1 = static_cast<int16_t>(rx1);
        regions[i].y1 = static_cast<int16_t>(ry1);
        regions[i].flags = flags;
        regions[i].on_event = on_event;
        regions[i].ctx = ctx;
        return;
      }
    }
  }
  HitRegion &reg = regions[count++];
  reg.x0 = static_cast<int16_t>(x0);
  reg.y0 = static_cast<int16_t>(y0);
  reg.x1 = static_cast<int16_t>(rx1);
  reg.y1 = static_cast<int16_t>(ry1);
  reg.widget_id = widget_id;
  reg.flags = flags;
  reg.param = param;
  reg.on_event = on_event;
  reg.ctx = ctx;
}

void FrameHitTable::add_circle(int cx, int cy, int r, uint16_t widget_id,
                               uint8_t flags, int16_t param,
                               UiActionFn on_event, void *ctx) {
  if (r <= 0) return;
  add_rect(cx - r, cy - r, 2 * r + 1, 2 * r + 1, widget_id, flags, param, on_event, ctx);
}

const HitRegion *FrameHitTable::hit_test(int tx, int ty, uint8_t required_flags) const {
  // Reverse iteration respects z-order: top-most drawn widgets win first.
  for (int i = static_cast<int>(count) - 1; i >= 0; i--) {
    const HitRegion &reg = regions[i];
    if ((reg.flags & required_flags) != required_flags) continue;
    if (!reg.contains(tx, ty)) continue;
    if ((reg.flags & kHitFocusCircle) != 0U) {
      const int dx = tx - reg.cx();
      const int dy = ty - reg.cy();
      const int r = reg.radius() + 4;
      if (dx * dx + dy * dy > r * r) continue;
    }
    return &reg;
  }
  return nullptr;
}

const HitRegion *FrameHitTable::find_by_id(uint16_t widget_id) const {
  if (widget_id == 0) return nullptr;
  for (int i = static_cast<int>(count) - 1; i >= 0; i--) {
    if (regions[i].widget_id == widget_id) return &regions[i];
  }
  return nullptr;
}

int FrameHitTable::focus_count() const {
  int total = 0;
  for (uint8_t i = 0; i < count; i++) {
    if ((regions[i].flags & (kHitFocusSlot | kHitFocusCircle)) != 0U) total++;
  }
  return total;
}

const HitRegion *FrameHitTable::focus_at(int focus_idx) const {
  const int n = focus_count();
  if (n <= 0) return nullptr;
  int target = focus_idx % n;
  if (target < 0) target += n;
  int seen = 0;
  for (uint8_t i = 0; i < count; i++) {
    if ((regions[i].flags & (kHitFocusSlot | kHitFocusCircle)) != 0U) {
      if (seen == target) return &regions[i];
      seen++;
    }
  }
  return nullptr;
}

}  // namespace latter_ui
