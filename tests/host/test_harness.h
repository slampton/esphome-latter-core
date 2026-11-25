#pragma once

// ==============================================================================
// Latter Core — Shared C++20 Multi-TU Host Test Harness (tests/host/test_harness.h)
// Zero unity-build `#include "*.cpp"` hacks; links cleanly against compiled
// latter_core + latter_ui TUs across all 5 canonical panel geometries.
// ==============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "../../include/audio_router.h"
#include "../../include/hal_battery.h"
#include "../../include/hal_display_lifecycle.h"
#include "../../include/hal_link_report.h"
#include "../../include/hal_manifest.h"
#include "../../include/hal_panel_power.h"
#include "../../include/hal_radio_policy.h"
#include "../../include/hal_sleep_pm.h"
#include "../../include/hal_touch.h"
#include "../../include/hal_video_stream.h"
#include "../../include/latter_actions.h"
#include "../../include/latter_protocol.h"
#include "../../include/latter_rpc.h"
#include "../../include/state/device_runtime_state.h"
#include "../../include/state/state_nvs_snapshot.h"
#include "../../include/ui_cadence.h"
#include "../../include/ui_color.h"
#include "../../include/ui_compositor.h"
#include "../../include/ui_dirty_band.h"
#include "../../include/ui_frame_context.h"
#include "../../include/ui_gfx.h"
#include "../../include/ui_hit_table.h"
#include "../../include/ui_layout.h"
#include "../../include/ui_mdi.h"
#include "../../include/ui_page_geoms.h"
#include "../../include/ui_pages.h"
#include "../../include/ui_panel_metrics.h"
#include "../../include/ui_scale.h"
#include "../../include/ui_surface.h"
#include "../../include/ui_tile_grid.h"
#include "../../include/ui_touch_engine.h"
#include "../../include/ui_touch_recognizer.h"
#include "../../include/ui_types.h"
#include "../../include/ui_widgets.h"

#define LATTER_CHECK(expr)                                                                         \
  do {                                                                                             \
    if (!(expr)) {                                                                                 \
      std::fprintf(stderr, "[HOST TEST FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);             \
      std::exit(1);                                                                                \
    }                                                                                              \
  } while (false)

namespace latter_test {

struct MockRpcOutbox {
  std::vector<std::string> published{};
  void publish_state(const std::string &payload) { published.push_back(payload); }
};

struct PanelSpec5 {
  const char *name;
  int w;
  int h;
  bool round;
  bool wearable;
  int corner_r;
  int ppi;
  bool chip_p4;
  int view_scale_pct;
  bool stationary;
};

inline constexpr PanelSpec5 kFlagshipPanels[5] = {
    {"p4_320x480_rect",   320, 480, false, false,   0, 165, true,  145, false},
    {"s3_410x502_wrist",  410, 502, false, true,   98, 315, false, 100, false},
    {"s3_466x466_pocket", 466, 466, true,  false, 233, 266, false, 100, false},
    {"s3_480x480_mobile", 480, 480, false, false,  76, 313, false, 100, false},
    {"p4_720x720_desk",   720, 720, false, false,   0, 254, true,  145, true},
};

inline constexpr PanelSpec5 kAllNinePanels[9] = {
    {"p4_320x480_rect",    320, 480, false, false,   0, 165, true,  145, false},
    {"s3_410x502_wrist",   410, 502, false, true,   98, 315, false, 100, false},
    {"s3_466x466_pocket",  466, 466, true,  false, 233, 266, false, 100, false},
    {"s3_480x480_mobile",  480, 480, false, false,  76, 313, false, 100, false},
    {"p4_720x720_desk",    720, 720, false, false,   0, 254, true,  145, true},
    {"s3_360x360_mini",    360, 360, true,  false, 180, 255, false, 100, false},
    {"p4_800x480_wide",    800, 480, false, false,   0, 187, true,  135, true},
    {"p4_1024x600_wall",  1024, 600, false, false,   0, 170, true,  145, true},
    {"p4_1280x800_tab",   1280, 800, false, false,   0, 149, true,  150, true},
};

inline std::string build_v3_packet(uint32_t epoch, uint32_t rev,
                                   const std::vector<std::string> &tokens) {
  std::string out = "v3\x1f" + std::to_string(epoch) + "\x1f" + std::to_string(rev);
  for (size_t i = 3; i < tokens.size(); ++i) {
    out.push_back('\x1f');
    out += tokens[i];
  }
  return out;
}

inline bool is_valid_utf8(std::string_view sv) {
  size_t i = 0;
  while (i < sv.size()) {
    const uint8_t b0 = static_cast<uint8_t>(sv[i]);
    if (b0 < 0x80U) {
      ++i;
    } else if ((b0 & 0xE0U) == 0xC0U) {
      if (i + 1 >= sv.size() || (static_cast<uint8_t>(sv[i + 1]) & 0xC0U) != 0x80U) return false;
      i += 2;
    } else if ((b0 & 0xF0U) == 0xE0U) {
      if (i + 2 >= sv.size() ||
          (static_cast<uint8_t>(sv[i + 1]) & 0xC0U) != 0x80U ||
          (static_cast<uint8_t>(sv[i + 2]) & 0xC0U) != 0x80U) {
        return false;
      }
      i += 3;
    } else if ((b0 & 0xF8U) == 0xF0U) {
      if (i + 3 >= sv.size() ||
          (static_cast<uint8_t>(sv[i + 1]) & 0xC0U) != 0x80U ||
          (static_cast<uint8_t>(sv[i + 2]) & 0xC0U) != 0x80U ||
          (static_cast<uint8_t>(sv[i + 3]) & 0xC0U) != 0x80U) {
        return false;
      }
      i += 4;
    } else {
      return false;
    }
  }
  return true;
}

inline std::filesystem::path host_test_dir() {
  return std::filesystem::path(__FILE__).parent_path();
}

inline std::string read_repo_file(const std::filesystem::path &rel_from_test_dir) {
  const auto target = host_test_dir() / rel_from_test_dir;
  std::ifstream ifs(target, std::ios::binary);
  if (!ifs.good()) {
    std::fprintf(stderr, "[HOST TEST FAIL] Missing repo file: %s\n", target.string().c_str());
    std::exit(1);
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

inline std::string read_repo_files(std::initializer_list<const char *> rel_paths) {
  std::string out;
  for (const char *rel : rel_paths) {
    if (!out.empty()) out.push_back('\n');
    out += read_repo_file(rel);
  }
  return out;
}

inline std::string read_repo_glob(const std::filesystem::path &rel_dir,
                                  std::string_view prefix,
                                  std::string_view suffix) {
  const auto dir = host_test_dir() / rel_dir;
  LATTER_CHECK(std::filesystem::is_directory(dir));
  std::vector<std::filesystem::path> matched;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    const std::string fname = entry.path().filename().string();
    if (!prefix.empty() && fname.rfind(prefix, 0) != 0U) continue;
    if (!suffix.empty() &&
        (fname.size() < suffix.size() ||
         std::string_view(fname).substr(fname.size() - suffix.size()) != suffix)) {
      continue;
    }
    matched.push_back(entry.path());
  }
  std::sort(matched.begin(), matched.end());
  LATTER_CHECK(!matched.empty());
  std::string out;
  for (const auto &p : matched) {
    if (!out.empty()) out.push_back('\n');
    std::ifstream ifs(p, std::ios::binary);
    LATTER_CHECK(ifs.good());
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out += oss.str();
  }
  return out;
}

struct HostSurfaceFixture {
  std::vector<uint16_t> pixels;
  latter_ui::Rgb565Surface surf{};
  latter_ui::UiFrameContext ctx{};

  HostSurfaceFixture()
      : pixels(static_cast<size_t>(latter_ui::SCREEN_W) * static_cast<size_t>(latter_ui::SCREEN_H), 0U) {
    surf = latter_ui::bind_host_surface(pixels.data(), latter_ui::SCREEN_W, latter_ui::SCREEN_H, 1);
    latter_ui::reset_clip_y_band();
    ctx.cx = latter_ui::SCREEN_CX;
    ctx.cy = latter_ui::SCREEN_CY;
    ctx.now_ms = 25000U;
    ctx.friendly_name = "Host Verify Display";
    ctx.fw_version = "3.0.0";
    ctx.now.year = 2026;
    ctx.now.month = 11;
    ctx.now.day_of_month = 21;
    ctx.now.day_of_week = 7;  // Saturday
    ctx.now.day_of_year = 325;
    ctx.now.hour = 14;
    ctx.now.minute = 35;
    ctx.now.second = 20;
    ctx.now.timestamp = 1795271720LL;
    ctx.font_time = latter_ui::UiFontView::make_host_font(latter_ui::kAutoFonts.time);
    ctx.font_chrono_time = latter_ui::UiFontView::make_host_font(latter_ui::kAutoFonts.chrono);
    ctx.font_date = latter_ui::UiFontView::make_host_font(latter_ui::kAutoFonts.date);
    ctx.font_btn_bold = latter_ui::UiFontView::make_host_font(latter_ui::kAutoFonts.btn_bold);
    ctx.font_small = latter_ui::UiFontView::make_host_font(latter_ui::kAutoFonts.small);
    ctx.font_micro = latter_ui::UiFontView::make_host_font(latter_ui::kAutoFonts.micro);
    ctx.font_games_title = latter_ui::UiFontView::make_host_font(latter_ui::kAutoFonts.games);
  }

  ~HostSurfaceFixture() {
    latter_ui::reset_clip_y_band();
    latter_ui::active_frame_buf16() = nullptr;
  }

  void fill(uint16_t val = 0U) {
    std::fill(pixels.begin(), pixels.end(), val);
    latter_ui::frame_base_begin();
  }
};

void run_protocol_rpc_tests();
void run_state_prefs_nvs_tests();
void run_power_audio_hal_tests();
void run_ui_layout_compositor_tests();
void run_ui_rgb565_5geom_tests();
void run_touch_engine_tests();
void run_pages_apps_tests();
void run_showcase_nav_tests();
void run_mdi_plus_tests();
void run_games_tests();
void run_tile_grid_tests();

}  // namespace latter_test
