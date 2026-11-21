// ==============================================================================
// Latter Core SDK — Comprehensive C++20 Host Unit Test Suite (tests/host/test_all.cpp)
//
// Compiles standalone with zero ESPHome/ESP-IDF dependencies:
//   g++ -std=c++20 -O2 -Wall -Wextra esphome-latter-core/tests/host/test_all.cpp -o latter_host_tests
//
// Coverage:
//   1. Protocol v3 pack/unpack across all 5 domains, epoch/rev monotonicity,
//      reconnect reset, ACTIVITY_CARD "0"/"" mask reset, VOLUME "1" & "100"
//      normalization, kProtocolMinRequiredFields version-skew floor, and
//      latter_rpc <=240B UTF-8-safe chunking, pacing, saturation, & req_id=0/monotonic guards.
//   2. Multi-geometry UiPanelLayout / scale_w_for / safe_span_for / FrameHitTable
//      across all 7 panel profiles (320x480..720x720) + DirtyBandTracker 2px
//      horizontal DMA band alignment & compute_subpage_id folding.
//   3. Production hal_battery.h (BatteryModel, 0.5C floor, 65C thermal foldback /
//      58C hysteresis, 0mAh zero-division guard, OCV curves, 30s/4-sample cutoff,
//      !can_wake_for_on_battery_alarm() policy) & production hal_link_report.h
//      (WeakLinkGate -65dBm/-61dBm 120s RSSI hysteresis).
// ==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../../include/latter_protocol.h"
#include "../../include/audio_router.h"
#include "../../include/latter_state.h"
#include "../../include/latter_rpc.h"
#include "../../include/latter_actions.h"
#include "../../components/latter_core/audio_session.cpp"
#include "../../include/hal_battery.h"
#include "../../include/hal_link_report.h"
#include "../../include/hal_manifest.h"
#include "../../include/hal_panel_power.h"
#include "../../include/hal_power_pm.h"
#include "../../include/hal_touch.h"
#include "../../include/hal_video_stream.h"
#include "../../components/latter_core/video_stream.cpp"
#include "../../components/latter_core/power_pm.cpp"
#include "../../components/latter_core/runtime_controllers.cpp"
#include "../../include/ui_hit_table.h"
#include "../../components/latter_ui/hit_table.cpp"
#include "../../include/ui_touch_recognizer.h"
#include "../../include/ui_compositor.h"
#include "../../components/latter_ui/ui_compositor.cpp"
#include "../../include/ui_touch_engine.h"
#include "../../include/ui_mdi.h"
#include "../../components/latter_ui/ui_mdi.cpp"

#define LATTER_CHECK(expr)                                                                         \
  do {                                                                                             \
    if (!(expr)) {                                                                                 \
      std::fprintf(stderr, "[HOST TEST FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expr);             \
      std::exit(1);                                                                                \
    }                                                                                              \
  } while (false)

namespace {

// ==============================================================================
// 1. PROTOCOL v3 & LATTER_RPC OUTBOX TESTS
// ==============================================================================

std::string build_v3_packet(uint32_t epoch, uint32_t rev, const std::vector<std::string> &tokens) {
  std::string out = "v3\x1f" + std::to_string(epoch) + "\x1f" + std::to_string(rev);
  for (size_t i = 3; i < tokens.size(); ++i) {
    out.push_back('\x1f');
    out += tokens[i];
  }
  return out;
}

bool is_valid_utf8(std::string_view sv) {
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

void test_protocol_v3_and_rpc() {
  latter_ui::reset_all_v3_rev_guards();
  auto &rem = latter_ui::remote_state();
  LATTER_CHECK(rem.epoch == 0 && rem.rev == 0);

  std::vector<std::string> r_tok(static_cast<size_t>(latter_ui::RemoteField::COUNT), "");
  r_tok[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "2";
  r_tok[static_cast<size_t>(latter_ui::RemoteField::CONFIGURED_COUNT)] = "2";
  r_tok[static_cast<size_t>(latter_ui::RemoteField::SLOTS_SUMMARY)] = "Apple TV\x1e" "remote.apple_tv";

  // Monotonic epoch & rev acceptance / duplicate & lower rejection / reconnect reset
  LATTER_CHECK(latter_ui::parse_v3_remote_payload(build_v3_packet(100, 1, r_tok)));
  LATTER_CHECK(rem.epoch == 100 && rem.rev == 1 && rem.active_slot == 1 && rem.configured_count == 2);

  r_tok[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "9";
  LATTER_CHECK(!latter_ui::parse_v3_remote_payload(build_v3_packet(100, 1, r_tok)));
  LATTER_CHECK(rem.active_slot == 1);

  r_tok[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "3";
  LATTER_CHECK(latter_ui::parse_v3_remote_payload(build_v3_packet(100, 2, r_tok)));
  LATTER_CHECK(rem.epoch == 100 && rem.rev == 2 && rem.active_slot == 2);

  LATTER_CHECK(!latter_ui::parse_v3_remote_payload(build_v3_packet(100, 1, r_tok)));
  LATTER_CHECK(rem.active_slot == 2);

  r_tok[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "1";
  LATTER_CHECK(latter_ui::parse_v3_remote_payload(build_v3_packet(200, 1, r_tok)));
  LATTER_CHECK(rem.epoch == 200 && rem.rev == 1 && rem.active_slot == 0);

  latter_ui::reset_all_v3_rev_guards();
  LATTER_CHECK(rem.epoch == 0 && rem.rev == 0);
  LATTER_CHECK(latter_ui::parse_v3_remote_payload(build_v3_packet(200, 1, r_tok)));

  // Reject malformed / sub-floor payloads (< kProtocolMinRequiredFields == 3)
  const std::string bad_v2 = std::string("v2\x1f") + "200\x1f" + "9\x1f" + "1\x1f" + "1\x1f" + "a";
  const std::string sub_floor = std::string("v3\x1f") + "200";
  LATTER_CHECK(!latter_ui::parse_v3_remote_payload(bad_v2));
  LATTER_CHECK(!latter_ui::parse_v3_remote_payload(sub_floor));
  LATTER_CHECK(!latter_ui::parse_v3_area_ctx_payload(sub_floor));
  LATTER_CHECK(!latter_ui::parse_v3_media_payload(sub_floor));
  LATTER_CHECK(!latter_ui::parse_v3_weather_payload(sub_floor));
  LATTER_CHECK(!latter_ui::parse_v3_calendar_payload(sub_floor));

  // All 5 domain payloads + min-field floor + extra-field forward compatibility
  latter_ui::reset_all_v3_rev_guards();

  std::vector<std::string> area_t(static_cast<size_t>(latter_ui::AreaCtxField::COUNT), "");
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::AREA_ID)] = "office";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::AREA_NAME)] = "Office";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::AREA_TEMP)] = "71.5";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::LIGHTS_ON)] = "2";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ALERT_ID)] = "42";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::SECURITY_PIN_POLICY)] = "require_pin";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 1, area_t)));
  LATTER_CHECK(latter_ui::area_ctx_state().area_id == "office");
  LATTER_CHECK(latter_ui::area_ctx_state().lights_on == 2);
  LATTER_CHECK(latter_ui::area_ctx_state().alert_id == 42);
  LATTER_CHECK(latter_ui::area_ctx_state().security_pin_policy == "require_pin");
  LATTER_CHECK(latter_ui::activity_source_mask() == latter_ui::kActivitySourceAll);

  // ACTIVITY_CARD "1" custom mask, "0" explicit zero mask, and "" spec-default reset
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_CARD)] = "1";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_SOURCES)] = "calendar,timers,future_key";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 2, area_t)));
  LATTER_CHECK(latter_ui::activity_source_mask() == ((1U << 5) | (1U << 1)));

  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_CARD)] = "0";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 3, area_t)));
  LATTER_CHECK(latter_ui::activity_source_mask() == 0);

  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_CARD)] = "";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_SOURCES)] = "";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 4, area_t)));
  LATTER_CHECK(latter_ui::activity_source_mask() == latter_ui::kActivitySourceAll);

  // Media domain: VOLUME "1" -> 0.01f, "" preserves volume & clears has_volume, "100" -> 1.00f, last_speaker wired
  std::vector<std::string> media_t(static_cast<size_t>(latter_ui::MediaField::COUNT), "");
  media_t[static_cast<size_t>(latter_ui::MediaField::STATE)] = "playing";
  media_t[static_cast<size_t>(latter_ui::MediaField::TITLE)] = "Time";
  media_t[static_cast<size_t>(latter_ui::MediaField::VOLUME)] = "1";
  media_t[static_cast<size_t>(latter_ui::MediaField::LAST_SPEAKER)] = "media_player.office_speaker";
  media_t[static_cast<size_t>(latter_ui::MediaField::IS_LOCAL)] = "1";
  LATTER_CHECK(latter_ui::parse_v3_media_payload(build_v3_packet(10, 1, media_t)));
  LATTER_CHECK(latter_ui::media_state().has_volume);
  LATTER_CHECK(std::fabs(latter_ui::media_state().volume - 0.01f) < 0.0005f);
  LATTER_CHECK(latter_ui::media_state().last_speaker == "media_player.office_speaker");
  LATTER_CHECK(latter_ui::media_state().is_local);

  media_t[static_cast<size_t>(latter_ui::MediaField::VOLUME)] = "";
  LATTER_CHECK(latter_ui::parse_v3_media_payload(build_v3_packet(10, 2, media_t)));
  LATTER_CHECK(!latter_ui::media_state().has_volume);
  LATTER_CHECK(std::fabs(latter_ui::media_state().volume - 0.01f) < 0.0005f);

  media_t[static_cast<size_t>(latter_ui::MediaField::VOLUME)] = "100";
  media_t[static_cast<size_t>(latter_ui::MediaField::POSITION_UPDATED_AT)] = "1700000000";
  LATTER_CHECK(latter_ui::parse_v3_media_payload(build_v3_packet(10, 3, media_t)));
  LATTER_CHECK(latter_ui::media_state().has_volume);
  LATTER_CHECK(std::fabs(latter_ui::media_state().volume - 1.00f) < 0.0005f);
  LATTER_CHECK(latter_ui::media_state().position_updated_at == 1700000000U);

  std::vector<std::string> weather_t(static_cast<size_t>(latter_ui::WeatherField::COUNT), "");
  weather_t[static_cast<size_t>(latter_ui::WeatherField::CONDITION)] = "sunny";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::TEMPERATURE)] = "68";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::SUN_STATE)] = "above_horizon";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::MOON_PHASE)] = "waxing_gibbous";
  LATTER_CHECK(latter_ui::parse_v3_weather_payload(build_v3_packet(10, 1, weather_t)));
  LATTER_CHECK(latter_ui::weather_state().condition == "sunny");
  LATTER_CHECK(latter_ui::weather_state().sun_state == "above_horizon");
  LATTER_CHECK(latter_ui::v3_mismatch_warn_count() == 0);

  // Min-field floor acceptance: exact kProtocolMinRequiredFields tokens succeeds and warns
  std::vector<std::string> min_weather(latter_ui::kProtocolMinRequiredFields, "");
  if (min_weather.size() > static_cast<size_t>(latter_ui::WeatherField::CONDITION)) {
    min_weather[static_cast<size_t>(latter_ui::WeatherField::CONDITION)] = "clear-night";
  }
  LATTER_CHECK(latter_ui::parse_v3_weather_payload(build_v3_packet(10, 2, min_weather)));
  LATTER_CHECK(latter_ui::v3_mismatch_warn_count() == 1);

  // Extra trailing fields (> COUNT) accepted cleanly
  std::vector<std::string> future_cal(static_cast<size_t>(latter_ui::CalendarField::COUNT) + 3, "future_ext");
  future_cal[static_cast<size_t>(latter_ui::CalendarField::SUMMARY)] = "Today|09:00|Standup";
  future_cal[static_cast<size_t>(latter_ui::CalendarField::EVENT_COUNT)] = "1";
  LATTER_CHECK(latter_ui::parse_v3_calendar_payload(build_v3_packet(10, 1, future_cal)));
  LATTER_CHECK(latter_ui::calendar_state().count == 1);
  LATTER_CHECK(latter_ui::v3_mismatch_warn_count() == 2);

  // latter_rpc <= 240B framing, UTF-8 codepoint safety, pacing, and ring overflow bound
  struct FakeOutbox {
    std::vector<std::string> frames;
    void publish_state(const std::string &s) { frames.push_back(s); }
  } outbox;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_rpc::kMaxRpcFrameBytes <= 240);

  latter_rpc::emit(&outbox, "set_active_area", {{"area_id", "kitchen"}});
  LATTER_CHECK(outbox.frames.size() == 1);
  LATTER_CHECK(outbox.frames[0].size() <= 240 && outbox.frames[0].size() <= latter_rpc::kMaxRpcFrameBytes);

  // Build > 600B payload packed with 3-byte UTF-8 em-dashes and 4-byte UTF-8 musical notes across chunk cuts
  std::string utf8_stress;
  for (int i = 0; i < 95; ++i) {
    utf8_stress += (i % 2 == 0) ? "\xE2\x80\x94" : "\xF0\x9F\x8E\xB5";
    utf8_stress += "ab";
  }
  uint32_t now_ms = 1000;
  latter_rpc::step_outbox_loop(now_ms);
  now_ms += 30;
  latter_rpc::step_outbox_loop(now_ms);
  latter_rpc::emit(&outbox, "device_hello", {
      {"contract", "3"},
      {"boot_id", "777"},
      {"manifest", utf8_stress},
  });
  LATTER_CHECK(latter_rpc::has_pending_frames());
  const size_t after_emit_count = outbox.frames.size();
  // First loop pass stamps awaiting_publish_stamp; sub-interval pass (< kMinInterFrameMs) must NOT coalesce chunk 2!
  now_ms += 2;
  latter_rpc::step_outbox_loop(now_ms);
  now_ms += 10;
  latter_rpc::step_outbox_loop(now_ms);
  LATTER_CHECK(outbox.frames.size() == after_emit_count);
  while (latter_rpc::has_pending_frames()) {
    now_ms += 20;
    latter_rpc::step_outbox_loop(now_ms);
  }
  std::string joined;
  for (size_t i = 1; i < outbox.frames.size(); ++i) {
    LATTER_CHECK(outbox.frames[i].size() <= 240);
    LATTER_CHECK(outbox.frames[i].size() <= latter_rpc::kMaxRpcFrameBytes);
    LATTER_CHECK(is_valid_utf8(outbox.frames[i]));
    const size_t bar = outbox.frames[i].find('|');
    LATTER_CHECK(bar != std::string::npos);
    joined += outbox.frames[i].substr(bar + 1);
  }
  LATTER_CHECK(joined.find(utf8_stress) != std::string::npos);

  // Saturation bound: flooding > kRpcQueueCapacity never exceeds ring capacity or crashes
  for (size_t k = 0; k < latter_rpc::kRpcQueueCapacity + 8; ++k) {
    latter_rpc::emit(&outbox, "area_action", {{"entity_id", "light.flood"}, {"action", "toggle"}});
  }
  LATTER_CHECK(latter_rpc::rpc_state().count <= latter_rpc::kRpcQueueCapacity);
  while (latter_rpc::has_pending_frames()) {
    now_ms += 20;
    latter_rpc::step_outbox_loop(now_ms);
  }

  // Monotonic req_id per SlotChannel + req_id == 0 unsolicited push acceptance
  LATTER_CHECK(latter_rpc::classify_browse_mode("areas") == latter_rpc::SlotChannel::AREA_LIST);
  LATTER_CHECK(latter_rpc::classify_browse_mode("entities") == latter_rpc::SlotChannel::AREA_ENTITIES);
  LATTER_CHECK(latter_rpc::classify_browse_mode("area_light") == latter_rpc::SlotChannel::AREA_ENTITIES);
  LATTER_CHECK(latter_rpc::classify_browse_mode("calendar_agenda") == latter_rpc::SlotChannel::CALENDAR);
  LATTER_CHECK(latter_rpc::classify_browse_mode("favorites") == latter_rpc::SlotChannel::MUSIC);
  const int r1 = latter_rpc::issue_req_id(latter_rpc::SlotChannel::AREA_ENTITIES);
  const int r2 = latter_rpc::issue_req_id(latter_rpc::SlotChannel::AREA_ENTITIES);
  LATTER_CHECK(!latter_rpc::accept_req_id(latter_rpc::SlotChannel::AREA_ENTITIES, r1));
  LATTER_CHECK(latter_rpc::accept_req_id(latter_rpc::SlotChannel::AREA_ENTITIES, r2));
  LATTER_CHECK(latter_rpc::accept_req_id(latter_rpc::SlotChannel::AREA_ENTITIES, 0));
  LATTER_CHECK(latter_rpc::accept_req_id(latter_rpc::SlotChannel::MUSIC, 0));
}

// ==============================================================================
// 2. MULTI-GEOMETRY UI LAYOUT, HIT TABLE & 2PX DIRTY BAND TESTS (ALL PANELS)
// ==============================================================================

struct PanelSpec {
  const char *name;
  int w;
  int h;
  bool round;
  bool wearable;
  int ppi;
};

constexpr PanelSpec kFlagshipPanels[7] = {
    {"p4_320x480_rect",  320, 480, false, false, 165},
    {"s3_360x360_round", 360, 360, true,  true,  277},
    {"s3_410x502_watch", 410, 502, false, true,  315},
    {"s3_412x412_round", 412, 412, true,  true,  398},
    {"s3_466x466_ref",   466, 466, true,  false, 316},
    {"s3_480x480_sq",    480, 480, false, false, 313},
    {"p4_720x720_hd",    720, 720, true,  false, 254},
};

void test_ui_layout_hit_table_and_dirty_band() {
  // Exact 466x466 @ 316 PPI reference calibration invariants (D-002, D-049 kinematic deadband)
  const auto ref = latter_ui::compute_panel_layout(466, 466, true, false, 316);
  LATTER_CHECK(ref.drag_start == 8);
  LATTER_CHECK(ref.tap_max == 21);
  LATTER_CHECK(ref.swipe_min == 29);
  LATTER_CHECK(ref.dock_hit_r >= 43);
  LATTER_CHECK(ref.back_y == 426);
  LATTER_CHECK(ref.home_bar_y == 434);
  LATTER_CHECK(latter_ui::scale_x_for(233, 466) == 233);
  LATTER_CHECK(latter_ui::scale_y_for(233, 466) == 233);
  LATTER_CHECK(latter_ui::scale_s_for(100, 466) == 100);
  LATTER_CHECK(latter_ui::scale_min_for(100, 466) == 100);
  LATTER_CHECK(latter_ui::scale_w_for(276, 466) == 276);

  for (const auto &p : kFlagshipPanels) {
    const auto ly = latter_ui::compute_panel_layout(p.w, p.h, p.round, p.wearable, p.ppi);
    const int min_dim = (p.w < p.h) ? p.w : p.h;
    LATTER_CHECK(ly.drag_start >= 3);
    LATTER_CHECK(ly.tap_max > ly.drag_start);
    LATTER_CHECK(ly.swipe_min > ly.tap_max);
    LATTER_CHECK(ly.dock_hit_r >= latter_ui::mm_to_px_for(3.6f, p.ppi));
    LATTER_CHECK(ly.hdr_y > 0 && ly.hdr_y < ly.sub_hdr_y);
    LATTER_CHECK(ly.sub_hdr_y < ly.dock_y);
    LATTER_CHECK(ly.dock_y < ly.back_y);
    LATTER_CHECK(ly.back_y < ly.home_bar_y);
    LATTER_CHECK(ly.home_bar_y < p.h);
    LATTER_CHECK(ly.list_card_w > 0 && ly.list_card_w <= p.w);
    LATTER_CHECK(ly.area_card3_w > 0 && ly.area_card3_w <= p.w);
    LATTER_CHECK(ly.tool_card3_w > 0 && ly.tool_card3_w <= p.w);
    LATTER_CHECK(latter_ui::scale_w_for(200, p.w) > 0);
    LATTER_CHECK(latter_ui::scale_min_for(200, min_dim) > 0);

    // Chord & corner-inset safe_span_for across upper, mid, and lower Y positions
    for (int frac : {18, 35, 50, 72, 85}) {
      const int y = (p.h * frac) / 100;
      const int inset = latter_ui::scale_min_for(12, min_dim);
      const int span = latter_ui::safe_span_for(y, p.w, p.h, p.round, p.wearable, inset);
      LATTER_CHECK(span > 0 && span <= p.w - 2 * inset);
      if (p.round) {
        const int r = p.w / 2;
        const int dy = std::abs(y - p.h / 2);
        const int chord = 2 * latter_ui::chord_half_span_constexpr(r, dy, false);
        LATTER_CHECK(span <= std::max(0, chord - 2 * inset));
        if (frac == 50) {
          LATTER_CHECK(latter_ui::chord_half_span_constexpr(r, 0, false) == r);
        }
      }
    }

    // Mobile Assistant (480x480, LATTER_CORNER_R = 76) rounded-square bezel clearance invariant:
    // Waveshare 2D CAD G2 corner spline cuts 21.85 px at 45 deg; outer alert border (pad = 3, cr_out = 73)
    // sits at 76 - 73 * cos(45 deg) = 24.38 px from (0, 0), clearing the bezel lip across all 4 corners.
    if (p.w == 480 && p.h == 480 && !p.round && !p.wearable) {
      constexpr int kMobileCornerR = 76;
      constexpr int kMobilePad = 3;
      constexpr int kMobileCrOut = kMobileCornerR - kMobilePad;
      const double diag_outer_px = static_cast<double>(kMobileCornerR) -
                                   static_cast<double>(kMobileCrOut) * 0.7071067811865475;
      LATTER_CHECK(diag_outer_px >= 24.0 && diag_outer_px <= 25.0);
      const int hdr_span_cr76 = latter_ui::safe_span_for(ly.hdr_y, 480, 480, false, false, 16, kMobileCornerR);
      const int hdr_span_cr20 = latter_ui::safe_span_for(ly.hdr_y, 480, 480, false, false, 16, 20);
      const int mid_span_cr76 = latter_ui::safe_span_for(240, 480, 480, false, false, 16, kMobileCornerR);
      LATTER_CHECK(hdr_span_cr76 < hdr_span_cr20 && hdr_span_cr76 == 412);
      LATTER_CHECK(mid_span_cr76 == 448);
    }

    // FrameHitTable hit testing, deduplication, slider norm_x, and focus traversal on each geometry
    latter_ui::FrameHitTable tbl;
    tbl.clear();
    LATTER_CHECK(tbl.count == 0 && tbl.focus_count() == 0 && tbl.focus_at(0) == nullptr);

    const int cx = p.w / 2;
    const int card_w = ly.list_card_w;
    const int card_x = cx - card_w / 2;
    const int card_y = ly.sub_hdr_y + 12;
    const int card_h = 44;

    tbl.add_rect(card_x, card_y, card_w, card_h,
                 latter_ui::WidgetId::REMOTE_SLOT_CARD,
                 latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
    // Duplicate (widget_id, param) updates in-place instead of appending a second entry
    tbl.add_rect(card_x, card_y, card_w, card_h + 2,
                 latter_ui::WidgetId::REMOTE_SLOT_CARD,
                 latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
    LATTER_CHECK(tbl.count == 1);
    LATTER_CHECK(tbl.find_by_id(latter_ui::WidgetId::REMOTE_SLOT_CARD) != nullptr);

    // Non-focusable horizontal slider bar
    const int sl_y = card_y + card_h + 16;
    tbl.add_rect(card_x, sl_y, card_w, 24,
                 latter_ui::WidgetId::SYS_TOGGLE_ROW,
                 latter_ui::kHitHorizSlider, 1);

    // Circle button & overlapping modal button (reverse z-order: modal drawn later wins!)
    tbl.add_circle(cx, ly.back_y, 24,
                   latter_ui::WidgetId::REMOTE_PRIMARY_PWR,
                   latter_ui::kHitTap | latter_ui::kHitFocusCircle, 0);
    tbl.add_centered_rect(cx, ly.back_y, 80, 40,
                          latter_ui::WidgetId::PIN_PAD_CANCEL,
                          latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);

    LATTER_CHECK(tbl.count == 4);
    LATTER_CHECK(tbl.focus_count() == 3);
    LATTER_CHECK(tbl.focus_at(0)->widget_id == latter_ui::WidgetId::REMOTE_SLOT_CARD);
    LATTER_CHECK(tbl.focus_at(1)->widget_id == latter_ui::WidgetId::REMOTE_PRIMARY_PWR);
    LATTER_CHECK(tbl.focus_at(2)->widget_id == latter_ui::WidgetId::PIN_PAD_CANCEL);
    LATTER_CHECK(tbl.focus_at(3)->widget_id == latter_ui::WidgetId::REMOTE_SLOT_CARD);
    LATTER_CHECK(tbl.focus_at(-1)->widget_id == latter_ui::WidgetId::PIN_PAD_CANCEL);

    // Reverse z-order: overlapping tap at (cx, back_y) hits PIN_PAD_CANCEL over REMOTE_PRIMARY_PWR
    const auto *top_hit = tbl.hit_test(cx, ly.back_y, latter_ui::kHitTap);
    LATTER_CHECK(top_hit != nullptr && top_hit->widget_id == latter_ui::WidgetId::PIN_PAD_CANCEL);

    // Slider hit test & clamped 0..1 norm_x
    const auto *drag_hit = tbl.hit_test(cx, sl_y + 12, latter_ui::kHitHorizSlider);
    LATTER_CHECK(drag_hit != nullptr && drag_hit->widget_id == latter_ui::WidgetId::SYS_TOGGLE_ROW);
    LATTER_CHECK(drag_hit->norm_x(card_x - 50) == 0.0f);
    LATTER_CHECK(drag_hit->norm_x(card_x + card_w + 50) == 1.0f);
    LATTER_CHECK(std::fabs(drag_hit->norm_x(card_x + card_w / 2) - 0.5f) < 0.02f);

    // 2px alignment across every panel height
    int ay0 = -1, ay1 = -1;
    latter_ui::DirtyBandTracker::align_band(85, 210, p.h, ay0, ay1);
    LATTER_CHECK((ay0 & 1) == 0 && ay0 == 84);
    LATTER_CHECK((ay1 & 1) == 1 && ay1 == 211);
    LATTER_CHECK(((ay1 - ay0 + 1) % 2) == 0);

    latter_ui::DirtyBandTracker::align_band(-15, p.h + 40, p.h, ay0, ay1);
    LATTER_CHECK(ay0 == 0 && ay1 == p.h - 1);
    LATTER_CHECK(((ay1 - ay0 + 1) % 2) == 0);
  }

  // Stateful DirtyBandTracker transitions & compute_subpage_id folding
  auto &db = latter_ui::dirty_band();
  db = latter_ui::DirtyBandTracker{};
  const uint32_t sig_idle = latter_ui::compute_takeover_signature(false, false, true, 0, false, false, false, false);
  const uint32_t sig_va   = latter_ui::compute_takeover_signature(false, false, true, 0, false, true,  false, false);
  LATTER_CHECK(sig_idle != sig_va);

  const int home_sub_dock0 = latter_ui::compute_subpage_id(1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
  const int home_sub_dock1 = latter_ui::compute_subpage_id(1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
  const int tools_slot0    = latter_ui::compute_subpage_id(8, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0);
  const int tools_slot2    = latter_ui::compute_subpage_id(8, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 2, 0);
  const int cal_list       = latter_ui::compute_subpage_id(8, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 11, 0, 0, 0, 0);
  const int cal_detail     = latter_ui::compute_subpage_id(8, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 11, 0, 1, 0, 0);
  LATTER_CHECK(home_sub_dock0 != home_sub_dock1);
  LATTER_CHECK(tools_slot0 != tools_slot2);
  LATTER_CHECK(cal_list != cal_detail);

  // Frame 1: initial page promotes to full-frame [0, SCREEN_H - 1]
  db.begin_frame(1, home_sub_dock0, sig_idle, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  // Frame 2: localized 1s countdown band + marquee band union into 2px-aligned [12, 233]
  latter_ui::mark_dirty_band(13, 60);
  latter_ui::mark_dirty_band(180, 232);
  db.begin_frame(1, home_sub_dock0, sig_idle, false);
  LATTER_CHECK(!db.is_full() && db.y0() == 12 && db.y1() == 233);

  // Frame 3: dock page flip promotes localized band to full frame via compute_subpage_id
  latter_ui::mark_dirty_band(40, 80);
  db.begin_frame(1, home_sub_dock1, sig_idle, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  // Frame 4: takeover transition (VA active) promotes localized band to full frame
  latter_ui::mark_dirty_band(40, 80);
  db.begin_frame(1, home_sub_dock1, sig_va, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  // Two-pass FrameHitTable hit_pad halo (D-019 >= 7 mm) + exact visual radius preservation
  static_assert(sizeof(latter_ui::HitRegion) == 14, "HitRegion must stay 14 bytes");
  latter_ui::FrameHitTable pad_ht{};
  pad_ht.add_circle(638, 216, 28, latter_ui::WidgetId::MUSIC_VOL_SLIDER,
                    latter_ui::kHitTap | latter_ui::kHitFocusSlot | latter_ui::kHitFocusCircle, 1, 40);
  pad_ht.add_circle(540, 216, 34, latter_ui::WidgetId::MUSIC_NEXT_BTN,
                    latter_ui::kHitTap | latter_ui::kHitFocusSlot | latter_ui::kHitFocusCircle, 0, 28);
  const auto *vol_reg = pad_ht.find_by_id_param(latter_ui::WidgetId::MUSIC_VOL_SLIDER, 1);
  LATTER_CHECK(vol_reg != nullptr && vol_reg->radius() == 28);
  // Touch 52px away from Vol+ center: outside 28px visual glass circle, inside 68px padded halo
  const auto *halo_hit = pad_ht.hit_test(638, 216 - 52, latter_ui::kHitTap);
  LATTER_CHECK(halo_hit != nullptr && halo_hit->widget_id == latter_ui::WidgetId::MUSIC_VOL_SLIDER);
  // Touch inside Next exact circle wins Pass 1 even where Vol+ padded halo overlaps
  const auto *exact_next = pad_ht.hit_test(540 + 30, 216, latter_ui::kHitTap);
  LATTER_CHECK(exact_next != nullptr && exact_next->widget_id == latter_ui::WidgetId::MUSIC_NEXT_BTN);
  // Midpoint tie-breaker in Pass 2 picks nearest button center
  const auto *near_vol = pad_ht.hit_test(602, 216, latter_ui::kHitTap);
  LATTER_CHECK(near_vol != nullptr && near_vol->widget_id == latter_ui::WidgetId::MUSIC_VOL_SLIDER);
}

// ==============================================================================
// 3. PRODUCTION HAL BATTERY MODEL, POWER POLICY & WEAK-LINK GATE TESTS
// ==============================================================================

std::string read_repo_file(const std::filesystem::path &rel_from_test_dir) {
  const auto base = std::filesystem::path(__FILE__).parent_path();
  const auto target = base / rel_from_test_dir;
  std::ifstream ifs(target, std::ios::binary);
  LATTER_CHECK(ifs.good());
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

void test_battery_model_and_power_policy() {
  // 1. Production 0.5C charge floor & 500 mA cap across fitted capacities + 0 mAh guard (hal_battery.h)
  LATTER_CHECK(latter_ui::path_mohm_for_capacity(0) == 80);       // 0 mAh guard: no div-by-zero
  LATTER_CHECK(latter_ui::precharge_ma_for_capacity(0) == 50);    // 0 mAh guard -> 500 mAh fallback
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(0) == 25);       // 0.8C of 0 floored safely to 25 mA
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(400) == 200);    // Wrist 400 mAh -> 200 mA (0.5C)
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(500) == 200);    // Pocket 500 mAh -> 250 mA floored to 200 mA
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(1000) == 500);   // Mobile 1000 mAh -> 500 mA (0.5C)
  LATTER_CHECK(latter_ui::charge_ma_for_capacity(2000) == 500);   // Capped at 500 mA
  LATTER_CHECK(latter_ui::axp2101_icc_code(100) == 4);
  LATTER_CHECK(latter_ui::axp2101_icc_code(200) == 8);
  LATTER_CHECK(latter_ui::axp2101_icc_code(500) == 11);

  // 2. Production 65C thermal foldback to 100 mA & 58C hysteresis restore (hal_battery.h)
  bool foldback_active = false;
  int active_icc_ma = 0;
  latter_ui::step_thermal_foldback_policy(true, 45.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  latter_ui::step_thermal_foldback_policy(true, 64.9f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  LATTER_CHECK(latter_ui::step_thermal_foldback_policy(true, 65.0f, 500, foldback_active, active_icc_ma));
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  latter_ui::step_thermal_foldback_policy(true, 60.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  LATTER_CHECK(latter_ui::step_thermal_foldback_policy(true, 58.0f, 500, foldback_active, active_icc_ma));
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);
  latter_ui::step_thermal_foldback_policy(true, 68.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(foldback_active && active_icc_ma == 100);
  latter_ui::step_thermal_foldback_policy(false, 68.0f, 500, foldback_active, active_icc_ma);
  LATTER_CHECK(!foldback_active && active_icc_ma == 500);

  // 3. Production BatteryModel 30s + 4-sample critical battery cutoff debounce (hal_battery.h)
  latter_ui::BatteryModel bm;
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 1000, -1, 500));
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 11000, -1, 500));
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 21000, -1, 500));
  // 4th sample at +30,000 ms trips once
  LATTER_CHECK(bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 31000, -1, 500));
  LATTER_CHECK(bm.cutoff_tripped);
  // Already latched -> returns false
  LATTER_CHECK(!bm.update(3280, true, latter_ui::ChargePhase::IDLE, latter_ui::BattLoad::QUIET, 41000, -1, 500));

  // 4. Production !can_wake_for_on_battery_alarm_policy() & resolve_alarm_sleep_seconds() (hal_battery.h)
  LATTER_CHECK(!latter_ui::can_wake_for_on_battery_alarm_policy(0, 1));
  LATTER_CHECK(latter_ui::can_wake_for_on_battery_alarm_policy(1, 1));
  LATTER_CHECK(latter_ui::can_wake_for_on_battery_alarm_policy(0, 0));
  LATTER_CHECK(latter_ui::resolve_alarm_sleep_seconds(0, 1, false, false, 3600) == -1);
  LATTER_CHECK(latter_ui::resolve_alarm_sleep_seconds(0, 1, false, true, 3600) == 3600);
  LATTER_CHECK(latter_ui::resolve_alarm_sleep_seconds(1, 1, true, false, 3600) == -1);

  // 5. Production WeakLinkGate RSSI hysteresis (-65 dBm enter across 2 samples after 5s settle, -61 dBm clear after 120s hold)
  LATTER_CHECK(!latter_ui::weak_link_can_sample(4064U, 3899U));  // post-association AGC settle guard (< 5s)
  LATTER_CHECK(latter_ui::weak_link_can_sample(8900U, 3899U));
  LATTER_CHECK(latter_ui::weak_link_can_sample(1000U, 0U));

  // Single transient weak sample (-69 dBm followed by -55 dBm) must NOT latch weak_link
  latter_ui::WeakLinkGate transient_gate{};
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-69, 10000U, transient_gate));
  LATTER_CHECK(transient_gate.below_count == 1U);
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-50, 20000U, transient_gate));
  LATTER_CHECK(transient_gate.below_count == 0U);
  LATTER_CHECK(!transient_gate.weak);

  latter_ui::WeakLinkGate gate{};
  uint32_t t = 10000;
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-55, t, gate));
  for (int i = 0; i < 6; ++i) {
    t += 10000;
    latter_ui::weak_link_feed_rssi(-74, t, gate);
  }
  LATTER_CHECK(gate.weak);

  for (int i = 0; i < 5; ++i) {
    t += 10000;
    LATTER_CHECK(latter_ui::weak_link_feed_rssi(-63, t, gate));
  }
  LATTER_CHECK(gate.above_since_ms == 0);

  bool saw_hold_before_clear = false;
  for (int i = 0; i < 20; ++i) {
    t += 10000;
    const bool w = latter_ui::weak_link_feed_rssi(-50, t, gate);
    if (gate.above_since_ms != 0 && (t - gate.above_since_ms) < latter_ui::kWeakLinkClearHoldMs) {
      LATTER_CHECK(w);
      saw_hold_before_clear = true;
    }
  }
  LATTER_CHECK(saw_hold_before_clear);
  LATTER_CHECK(!gate.weak);

  // Disconnect resets transient/latched state while folding elapsed weak duration into weak_total_ms
  latter_ui::WeakLinkGate disc_gate{};
  LATTER_CHECK(!latter_ui::weak_link_feed_rssi(-72, 10000U, disc_gate));
  LATTER_CHECK(latter_ui::weak_link_feed_rssi(-72, 20000U, disc_gate));
  latter_ui::weak_link_note_disconnect(25000U, disc_gate);
  LATTER_CHECK(!disc_gate.weak && !disc_gate.have && disc_gate.below_count == 0U);
  LATTER_CHECK(disc_gate.weak_total_ms == 5000U);

  // 6. Verify battery_motion_allowed() covers active 15s/30s handheld wake windows (35s) & renews on touch
  latter_ui::last_screen_wake_ms() = 1000U;
  latter_ui::last_user_touch_ms() = 1000U;
  LATTER_CHECK(latter_ui::battery_motion_allowed(false, 13000U, 0U));   // post-wake-greeting Activity Card stays smooth!
  LATTER_CHECK(latter_ui::battery_motion_allowed(false, 35500U, 0U));
  LATTER_CHECK(!latter_ui::battery_motion_allowed(false, 36500U, 0U));
  latter_ui::last_user_touch_ms() = 20000U;
  LATTER_CHECK(latter_ui::battery_motion_allowed(false, 54000U, 0U));
  LATTER_CHECK(!latter_ui::battery_motion_allowed(false, 55500U, 0U));
  LATTER_CHECK(latter_ui::battery_motion_allowed(true, 90000U, 0U));

  // 7. Verify production C++ & YAML sources reference the shared policies and ambient pacing invariants
  const std::string batt_h = read_repo_file("../../include/hal_battery.h");
  const std::string pmu_cpp = read_repo_file("../../components/latter_core/power_battery.cpp");
  const std::string link_h = read_repo_file("../../include/hal_link_report.h");
  const std::string gfx_cpp = read_repo_file("../../components/latter_ui/ui_gfx.cpp");
  const std::string comp_h = read_repo_file("../../include/ui_compositor.h");
  const std::string comp_cpp = read_repo_file("../../components/latter_ui/ui_compositor.cpp");
  const std::string timers_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");
  const std::string pwr_yaml = read_repo_file("../../modules/power_manager.yaml");
  const std::string weather_cpp = read_repo_file("../../components/latter_ui/page_weather.cpp");

  LATTER_CHECK(gfx_cpp.find("last_ambient_anim_ms() = start_m") != std::string::npos);
  LATTER_CHECK(comp_h.find("sync_disp_poller_for_anim(") != std::string::npos);
  LATTER_CHECK(comp_h.find("sync_disp_poller_for_anim(disp, act.sync_disp_poller_val)") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("dispatch_cadence_20ms(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("dispatch_cadence_screensaver_20ms(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("speaker_audio_busy") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("app_weather_active") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("mww_running") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("cloud_shape_get<kColStep>") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("last_rain_ms") == std::string::npos);
  LATTER_CHECK(weather_cpp.find("AnimeRainDrop") == std::string::npos);

  LATTER_CHECK(batt_h.find("kChargeCapMa = 500") != std::string::npos);
  LATTER_CHECK(batt_h.find("kChargeThermalFoldbackMa = 100") != std::string::npos);
  LATTER_CHECK(batt_h.find("kAxpThermalFoldbackEnterC = 65.0f") != std::string::npos);
  LATTER_CHECK(batt_h.find("kAxpThermalFoldbackClearC = 58.0f") != std::string::npos);
  LATTER_CHECK(batt_h.find("can_wake_for_on_battery_alarm()") != std::string::npos);
  LATTER_CHECK(pmu_cpp.find("step_thermal_foldback_policy") != std::string::npos);
  LATTER_CHECK(pmu_cpp.find("!can_wake_for_on_battery_alarm()") != std::string::npos);
  LATTER_CHECK(link_h.find("kWeakLinkEnterDbm = -65") != std::string::npos);
  LATTER_CHECK(link_h.find("kWeakLinkClearDbm = -61") != std::string::npos);
  LATTER_CHECK(link_h.find("kWeakLinkClearHoldMs = 120000U") != std::string::npos);
}

void test_music_remote_override_and_volume_feedback() {
  latter_ui::clear_remote_play_override();

  // Remote "playing" and "buffering" both report effective playing when no override is active
  LATTER_CHECK(latter_ui::is_music_playing_effective(true, false, "playing", 1000U));
  LATTER_CHECK(latter_ui::is_music_playing_effective(true, false, "buffering", 1000U));
  LATTER_CHECK(!latter_ui::is_music_playing_effective(true, true, "idle", 1000U));
  LATTER_CHECK(latter_ui::is_music_playing_effective(false, true, "idle", 1000U));

  // Single-owner optimistic remote pause override (must NOT double-flip before toggle_ha_music)
  latter_ui::set_remote_play_override(false, 2000U);
  LATTER_CHECK(!latter_ui::is_music_playing_effective(true, false, "playing", 3500U));
  // Expires cleanly at >= 2500 ms, falling back to authoritative remote_state_str
  LATTER_CHECK(latter_ui::is_music_playing_effective(true, false, "playing", 4600U));

  const std::string touch_cpp = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  LATTER_CHECK(touch_cpp.find("set_remote_play_override(") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("step_music_volume_feedback(") != std::string::npos);
  // D-049: Waking auto-hidden Music HUD must NOT arm global tap cooldown so Tap 2 fires immediately
  LATTER_CHECK(touch_cpp.find("touch_router().note_tap(") == std::string::npos);
  LATTER_CHECK(touch_cpp.find(".last_tap_ms") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("{\"Full Screen\", \"Letterbox\", \"Standard\", \"None\"}") != std::string::npos);

  const std::string music_cpp = read_repo_file("../../components/latter_ui/page_music.cpp");
  const std::string remote_cpp = read_repo_file("../../components/latter_ui/page_remote.cpp");
  const std::string gui_yaml = read_repo_file("../../guis/universal/gui.yaml");
  const std::string state_h = read_repo_file("../../include/latter_state.h");
  LATTER_CHECK(music_cpp.find("art_mode == \"None\"") != std::string::npos);
  LATTER_CHECK(music_cpp.find("render_page_wallpaper_base(") != std::string::npos);
  // D-004 / D-070: gui.yaml owns the single frame base for Remote (page 7); page_remote.cpp must not double-paint wallpaper
  LATTER_CHECK(remote_cpp.find("render_page_wallpaper_base(") == std::string::npos);
  LATTER_CHECK(gui_yaml.find("rs.nav.gui_page == 7") != std::string::npos);
  LATTER_CHECK(state_h.find("\"Full Screen\", \"Letterbox\", \"Standard\", \"None\"") != std::string::npos);

  // TouchRouter::note_tap() stamps debounce timestamp on HUD wake without gating the wake itself
  latter_ui::touch_router().note_tap(10000U);
  LATTER_CHECK(!latter_ui::touch_router().allow_tap(10080U, latter_ui::TOUCH_DEBOUNCE_BUTTON));
  LATTER_CHECK(latter_ui::touch_router().allow_tap(10200U, latter_ui::TOUCH_DEBOUNCE_BUTTON));

  // Volume percentage display feedback ("Volume: XX%") + consecutive remote_vol_pct() stepping
  latter_ui::MusicRuntimeState m{};
  LATTER_CHECK(latter_ui::step_music_volume_feedback(1, 5.0f, true, 0.55f, 5000U, m) == 60);
  LATTER_CHECK(m.action_feedback == "Volume: 60%" && m.feedback_timer == 5000U);
  LATTER_CHECK(latter_ui::step_music_volume_feedback(-1, 5.0f, true, 0.12f, 5200U, m) == 10);
  LATTER_CHECK(m.action_feedback == "Volume: 10%");

  latter_ui::remote_vol_pct() = 55;
  LATTER_CHECK(latter_ui::step_music_volume_feedback(1, 5.0f, false, 0.55f, 6000U, m) == 60);
  LATTER_CHECK(latter_ui::remote_vol_pct() == 60 && m.action_feedback == "Volume: 60%");
  LATTER_CHECK(latter_ui::step_music_volume_feedback(1, 5.0f, false, 0.55f, 6200U, m) == 65);
  LATTER_CHECK(latter_ui::remote_vol_pct() == 65 && m.action_feedback == "Volume: 65%");
}

struct MockBleCtrl {
  bool active{false};
  bool paused{false};
  bool transport_up{false};
  bool failed{false};
  int enables{0};
  int pauses{0};
  int disables{0};
  bool is_active() const { return active; }
  bool is_paused() const { return paused; }
  bool is_transport_up() const { return transport_up; }
  bool is_failed() const { return failed; }
  void enable() {
    active = true;
    paused = false;
    transport_up = true;
    ++enables;
  }
  void pause() {
    active = false;
    paused = true;
    ++pauses;
  }
  void disable() {
    active = false;
    paused = false;
    transport_up = false;
    ++disables;
  }
};

struct MockWifiCtrl {};

struct MockTouchscreen {
  bool failed{true};
  int clear_err_calls{0};
  int reset_calls{0};
  int setup_calls{0};
  int disable_loop_calls{0};
  int enable_loop_calls{0};
  bool is_failed() const { return failed; }
  void status_clear_error() { ++clear_err_calls; }
  void reset_to_construction_state() { ++reset_calls; }
  void call() { ++setup_calls; }
  void disable_loop() { ++disable_loop_calls; }
  void enable_loop() { ++enable_loop_calls; }
};

void test_beacon_boot_gate_and_touch_reboot_safety() {
  // 1. beacon_boot_gate_open: opens 2s after HA API connect instead of waiting a blind 20s,
  //    or falls back to 12s on Wi-Fi when HA has not connected yet.
  LATTER_CHECK(!latter_ui::beacon_boot_gate_open(false, true, 4000U, 8000U));
  LATTER_CHECK(!latter_ui::beacon_boot_gate_open(true, false, 0U, 8000U));
  LATTER_CHECK(!latter_ui::beacon_boot_gate_open(true, true, 4500U, 6000U));   // 1.5s < 2.0s settle
  LATTER_CHECK(latter_ui::beacon_boot_gate_open(true, true, 4500U, 6500U));    // 2.0s settle met at 6.5s!
  LATTER_CHECK(latter_ui::beacon_boot_gate_open(true, false, 0U, 12500U));     // 12s Wi-Fi-only fallback

  MockBleCtrl ble{};
  MockWifiCtrl wifi{};
  // At t=5.5s with HA connected at t=4.5s (<2s settle), BLE stays off
  latter_ui::evaluate_radio_policy(&ble, &wifi, true, true, 0, false, false, false, 0, true, 5500U, true, 4500U);
  LATTER_CHECK(!ble.is_active() && ble.enables == 0);
  // At t=6.6s (>=2s settle), BLE enables immediately on wake without waiting 20s
  latter_ui::evaluate_radio_policy(&ble, &wifi, true, true, 0, false, false, false, 0, true, 6600U, true, 4500U);
  LATTER_CHECK(ble.is_active() && ble.enables == 1);

  // 2. hal_recover_touchscreen_if_failed: test both TouchT* (id(touch_screen)) and TouchT* const* (&id(touch_screen))
  MockTouchscreen ts{};
  MockTouchscreen *const ts_const_ptr = &ts;
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 0U, true);
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 1000U);  // <= 1500ms boot guard
  LATTER_CHECK(ts.setup_calls == 0);
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 2000U);  // attempt 1 via TouchT*
  LATTER_CHECK(ts.setup_calls == 1 && ts.clear_err_calls == 1 && ts.reset_calls == 1 && ts.enable_loop_calls == 1);
  latter_ui::hal_recover_touchscreen_if_failed(&ts_const_ptr, 3200U);  // < 2000ms cooldown
  LATTER_CHECK(ts.setup_calls == 1);
  latter_ui::hal_recover_touchscreen_if_failed(&ts_const_ptr, 4100U);  // attempt 2 via TouchT* const*
  LATTER_CHECK(ts.setup_calls == 2 && ts.enable_loop_calls == 2);
  latter_ui::hal_recover_touchscreen_if_failed(&ts, 7000U);  // capped at 2 attempts per boot
  LATTER_CHECK(ts.setup_calls == 2);

  // 3. Verify production YAML & touch confirm-release / CST9217 warm-reboot protections
  const std::string touch_cpp = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  const std::string panel_h = read_repo_file("../../include/hal_panel_power.h");
  const std::string sys_yaml = read_repo_file("../../modules/system_core.yaml");
  const std::string pwr_yaml = read_repo_file("../../modules/power_manager.yaml");

  // Confirm modal (modes 4 & 6) must NOT dispatch RESTART_DEVICE on finger-down; must resolve on_touch_up
  const auto on_up_pos = touch_cpp.find("void TouchGestureEngine::on_touch_up(");
  LATTER_CHECK(on_up_pos != std::string::npos);
  LATTER_CHECK(touch_cpp.substr(0, on_up_pos).find("UiHostCmd::RESTART_DEVICE") == std::string::npos);
  LATTER_CHECK(touch_cpp.substr(on_up_pos).find("UiHostCmd::RESTART_DEVICE") != std::string::npos);

  LATTER_CHECK(panel_h.find("hal_prepare_touch_for_restart()") != std::string::npos);
  LATTER_CHECK(panel_h.find("hal_recover_touchscreen_if_failed(") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("hal_prepare_touch_for_restart()") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("flush_persisted_prefs_if_dirty()") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("hal_recover_touchscreen_if_failed(") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("publish_state(\"hibernating\")") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("area_changed") != std::string::npos);
}

struct MockRpcOutbox {
  std::vector<std::string> published{};
  void publish_state(const std::string &payload) { published.push_back(payload); }
};

void test_device_prefs_v2_and_event_timer_sync() {
  static_assert(std::is_trivially_copyable_v<latter_ui::PersistedDevicePrefs>);
  LATTER_CHECK(latter_ui::kPersistedPrefsVersion == 16U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::PrefKey::COUNT) == 65U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::DevicePrefsField::COUNT) == 71U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::AreaCtxField::COUNT) == 42U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::ShortcutsField::COUNT) == 8U);

  // Universal shortcut routes + PageId::PINNED == 11 + PageId::CALENDAR == 12 + PageId::CAMERAS == 13
  LATTER_CHECK(latter_ui::PageId::HOME_HUB == 9);
  LATTER_CHECK(latter_ui::PageId::TIME == 10);
  LATTER_CHECK(latter_ui::PageId::PINNED == 11);
  LATTER_CHECK(latter_ui::PageId::CALENDAR == 12);
  LATTER_CHECK(latter_ui::PageId::CAMERAS == 13);
  LATTER_CHECK(latter_ui::find_shortcut_route("app:pinned") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("app:calendar") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("calendar:month") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("tools:calendar") == nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("weather:moon") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("app:cameras") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("cameras:grid") != nullptr);
  LATTER_CHECK(latter_ui::find_shortcut_route("home_hub:cameras") == nullptr);

  // Start screen Pinned, Calendar & Cameras resolution
  latter_ui::PersistedDevicePrefs cam_prefs{};
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Pinned"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == latter_ui::PageId::PINNED);
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Calendar"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == latter_ui::PageId::CALENDAR);
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Cameras"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == latter_ui::PageId::CAMERAS);
  LATTER_CHECK(latter_ui::is_cameras_start_screen(cam_prefs));
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Tools"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == 8);

  // Perimeter security policy - sensitive action gate
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("home_hub.garage", "turn_on", 100) == true);
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("home_hub.garage", "turn_on") == true);
  const std::string retired_target = std::string("whole") + "_home.garage";
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl(retired_target, "turn_on", 100) == false);
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("lock", "open") == true);
  LATTER_CHECK(latter_ui::is_sensitive_perimeter_action_impl("cover.garage_door", "open") == true);
  LATTER_CHECK(!latter_ui::is_sensitive_perimeter_action_impl("light.porch", "turn_on"));

  // Home Hub state & tab
  latter_ui::home_hub_tab() = 2;
  LATTER_CHECK(latter_ui::home_hub_tab() == 2);
  latter_ui::home_hub_state().thermostat_count = 1;
  latter_ui::home_hub_state().thermostats[0].mode = "heat";
  LATTER_CHECK(latter_ui::home_hub_state().thermostats[0].mode == "heat");

  // Sentinel purge verification
  LATTER_CHECK(latter_ui::is_configured_remote_slot("remote.living_room"));
  LATTER_CHECK(!latter_ui::is_configured_remote_slot(""));

  // Ordered Home Hub category CSV parser & collect_home_hub_tabs (5-category Home Hub, zero pre-sync placeholder tabs)
  int hh_tabs[latter_ui::kMaxHomeHubTabs] = {-1, -1, -1, -1, -1};
  latter_ui::AreaContextState uninit_ac{};
  LATTER_CHECK(uninit_ac.epoch == 0U && uninit_ac.home_categories.empty());
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);
  uninit_ac.home_categories = latter_ui::kDefaultHomeCategories;
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);  // epoch == 0 still returns 0
  uninit_ac.epoch = 1U;
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 5);
  LATTER_CHECK(hh_tabs[0] == 0 && hh_tabs[1] == 1 && hh_tabs[2] == 2 &&
               hh_tabs[3] == 3 && hh_tabs[4] == 4);
  uninit_ac.epoch = 10U;
  uninit_ac.home_categories = "vacuum,thermostat,unknown,vacuum,intercom";
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 3);
  LATTER_CHECK(hh_tabs[0] == latter_ui::HomeHubTab::VACUUM &&
               hh_tabs[1] == latter_ui::HomeHubTab::THERMOSTAT &&
               hh_tabs[2] == latter_ui::HomeHubTab::INTERCOM);
  uninit_ac.home_categories.clear();
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 0);

  // Manifest slot capacities (LATTER_SLOT_COUNT=5, LATTER_CARD_SLOT_COUNT=3) + compact <=230B state guard
  const std::string full_manifest = latter_ui::build_hardware_manifest_json();
  const std::string compact_manifest = latter_ui::build_hardware_manifest_state_json();
  LATTER_CHECK(full_manifest.find("\"slot_count\":5") != std::string::npos);
  LATTER_CHECK(full_manifest.find("\"card_slot_count\":3") != std::string::npos);
  LATTER_CHECK(compact_manifest.size() <= 230U);

  // Ordered Area category CSV parser (custom order, dedup, unknown filtering, empty Area)
  std::string parsed_cats[8];
  const int parsed_n = latter_ui::parse_area_categories_csv("media,light,unknown,media,all", parsed_cats, 8);
  LATTER_CHECK(parsed_n == 3 && parsed_cats[0] == "media" && parsed_cats[1] == "light" && parsed_cats[2] == "all");
  LATTER_CHECK(latter_ui::parse_area_categories_csv("", parsed_cats, 8) == 0);
  const int def_n = latter_ui::parse_area_categories_csv(latter_ui::kDefaultAreaCategories, parsed_cats, 8);
  LATTER_CHECK(def_n == 7 && parsed_cats[0] == "scene" && parsed_cats[6] == "media");

  auto &p = latter_ui::persisted_prefs();
  p = latter_ui::PersistedDevicePrefs{};
  latter_ui::device_prefs_state() = latter_ui::DevicePrefsState{};
  latter_ui::seed_default_prefs_if_needed(
      "Modern Analog", "Home", "Aurora",
      "Full Screen", "Alphabetical", "Newest on Top", "Oldest on Top");
  LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr()) == "Docked Only");
  LATTER_CHECK(std::string_view(latter_ui::pref_clock_face_cstr()) == "Modern Analog");
  LATTER_CHECK(std::string_view(latter_ui::pref_screensaver_style_cstr()) == "Aurora");
  LATTER_CHECK(p.prefs_seq == 0U && p.dirty_mask == 0ULL);

  // Fresh boot (prefs_seq == 0): device_hello dirty_prefs advertises board-seeded defaults (excluding ble_beacon_enabled)
  const std::string seed_dirty = latter_ui::build_hello_dirty_prefs(p);
  LATTER_CHECK(seed_dirty.find("wake_word_mode=Docked Only") != std::string::npos);
  LATTER_CHECK(seed_dirty.find("screensaver_style=Aurora") != std::string::npos);
  LATTER_CHECK(seed_dirty.find("ble_beacon_enabled=") == std::string::npos);

  // Inbound Protocol v3 device_prefs push updates prefs_seq, clears dirty_mask, and flags side effects
  latter_ui::reset_all_v3_rev_guards();
  std::vector<std::string> dp_tok(static_cast<size_t>(latter_ui::DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::PREFS_SEQ)] = "7";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::WAKE_WORD_MODE)] = "Disabled";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::ALERT_VOLUME)] = "65";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::COLOR_PROFILE)] = "Custom";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::THEME_COLOR_HEX)] = "#2288FF";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::WEATHER_SIM_CONDITION)] = "Thunderstorm (Day)";

  p.dirty_mask = (1ULL << static_cast<uint8_t>(latter_ui::PrefKey::ALERT_VOLUME));
  uint32_t eff = 0;
  LATTER_CHECK(latter_ui::parse_v3_device_prefs_payload(build_v3_packet(300, 1, dp_tok), eff));
  LATTER_CHECK(p.prefs_seq == 7U);
  LATTER_CHECK(p.dirty_mask == 0ULL);
  LATTER_CHECK(p.alert_volume == 65U);
  LATTER_CHECK(std::string_view(latter_ui::pref_color_profile_cstr()) == "Custom");
  LATTER_CHECK(p.custom_theme_rgb == 0x2288FFU);
  LATTER_CHECK(std::string_view(latter_ui::pref_weather_sim_cstr()) == "Thunderstorm (Day)");
  LATTER_CHECK((eff & latter_ui::kPrefEffectThemePreset) != 0U);
  LATTER_CHECK((eff & latter_ui::kPrefEffectWakeWordSync) != 0U);
  LATTER_CHECK((eff & latter_ui::kPrefEffectRedraw) != 0U);

  // Online local touchscreen edit emits set_device_prefs directly for the edited key
  MockRpcOutbox outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_ui::link_state().ha_connected = true;
  uint32_t local_eff = 0;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::WAKE_WORD_MODE, "Always On", 1000U, local_eff, &outbox));
  LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr()) == "Always On");
  LATTER_CHECK(!outbox.published.empty());
  LATTER_CHECK(outbox.published.back().find("set_device_prefs") != std::string::npos);
  LATTER_CHECK(outbox.published.back().find("wake_word_mode=Always On") != std::string::npos);

  // Offline local touchscreen edit: queues in dirty_mask without RPC, then surfaces in build_hello_dirty_prefs
  p.dirty_mask = 0ULL;
  outbox.published.clear();
  latter_ui::link_state().ha_connected = false;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::TIME_FORMAT, "24-Hour", 2000U, local_eff, &outbox));
  LATTER_CHECK(outbox.published.empty());
  LATTER_CHECK(p.is_24h_format);
  const std::string offline_dirty = latter_ui::build_hello_dirty_prefs(p);
  LATTER_CHECK(offline_dirty == "time_format=24-Hour");

  // Event-driven timer_alarm_state: zero emissions during normal 1 Hz countdown, immediate emission on start/jump/ring/stop
  latter_actions::timer_alarm_sync_tracker() = latter_actions::TimerAlarmSyncTracker{};
  auto &rs = latter_ui::runtime_state();
  rs.timers.running = false;
  rs.timers.paused = false;
  rs.timers.ringing = false;
  rs.alarms.ringing = false;
  latter_ui::link_state().ha_connected = true;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  outbox.published.clear();

  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(5000U, false, &outbox));
  rs.timers.running = true;
  rs.timers.remaining_sec = 120;
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(5250U, false, &outbox));
  LATTER_CHECK(outbox.published.size() == 1U && outbox.published.back().find("timer_alarm_state") != std::string::npos);

  // Steady 1 Hz countdown tick (119s at +1000ms) must NOT emit Wi-Fi RPC traffic
  rs.timers.remaining_sec = 119;
  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(6250U, false, &outbox));

  // Manual +1m jump (179s vs expected ~118s) emits immediately
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rs.timers.remaining_sec = 179;
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(7250U, false, &outbox));
  LATTER_CHECK(outbox.published.size() == 2U);

  // Single-App Mode (home_screen_enabled=false), 10-item Start Screen, and Return-to-Home idle exemptions
  p = latter_ui::PersistedDevicePrefs{};
  LATTER_CHECK(p.home_screen_enabled);
  LATTER_CHECK(std::string_view(latter_ui::pref_start_screen_cstr(p)) == "Home");
  LATTER_CHECK(std::string_view(latter_ui::pref_return_to_home_timeout_cstr(p)) == "2 min");
  LATTER_CHECK(latter_ui::return_to_home_timeout_ms(p) == 120000U);

  latter_ui::PrefMask nav_chg{};
  uint32_t nav_eff = 0;
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::HOME_SCREEN_ENABLED, "0", false, nav_chg, nav_eff));
  LATTER_CHECK(!p.home_screen_enabled);
  LATTER_CHECK(std::string_view(latter_ui::pref_start_screen_cstr(p)) == "Time App");
  LATTER_CHECK(latter_ui::resolve_anchor_dock_app(p) == 10);
  LATTER_CHECK((nav_eff & latter_ui::kPrefEffectNavPolicy) != 0U);
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::START_SCREEN, "Clock Face", false, nav_chg, nav_eff));
  LATTER_CHECK(latter_ui::resolve_anchor_dock_app(p) == 0);

  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::START_SCREEN, "Area Controls", false, nav_chg, nav_eff));
  LATTER_CHECK(latter_ui::resolve_anchor_dock_app(p) == 6);
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));

  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::HOME_SCREEN_ENABLED, "1", false, nav_chg, nav_eff));
  rs = latter_ui::DeviceRuntimeState{};
  rs.nav.gui_page = 6;
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.nav.gui_page = 1;
  rs.nav.home_subpage = 0;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.nav.gui_page = 2;
  rs.music.overlay_mode = 0;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, true));   // Playing on Now Playing stays put
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, false));   // Paused on Now Playing returns Home
  rs.music.overlay_mode = 2;
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, true));    // Idle Library menu returns Home
  rs.nav.gui_page = 10;
  rs.nav.time_subpage = 1;
  rs.timers.running = true;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));  // Active Time App countdown stays put
  rs.timers.running = false;
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, false));
  rs.nav.time_subpage = 3;
  rs.stopwatch.running = true;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));  // Active Time App stopwatch stays put
  rs.stopwatch.running = false;
  rs.nav.gui_page = 8;
  rs.nav.tools_subpage = 5;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));  // Flashlight stays put
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::RETURN_TO_HOME_TIMEOUT, "Never", false, nav_chg, nav_eff));
  rs.nav.gui_page = 6;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));
}

struct MockAnimPoller {
  int starts{0};
  int stops{0};
  void start_poller() { ++starts; }
  void stop_poller() { ++stops; }
};

void test_phase1_boot_wake_hibernate_and_ble_roaming() {
  static_assert(std::is_trivially_copyable_v<latter_ui::HibernateSnapshot>);
  LATTER_CHECK(LATTER_SDK_REV == 2026112004L);
  LATTER_CHECK(latter_ui::kHibernateSnapshotVersion == 3U);

  // 1. HibernateSnapshot v3 round-trip & cold hydration without clobbering Protocol v3 revision guards
  latter_ui::reset_hibernate_snapshot_for_test();
  latter_ui::reset_all_v3_rev_guards();
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::shortcuts_state() = latter_ui::ShortcutsState{};
  latter_ui::runtime_state().area = latter_ui::AreaRuntimeState{};
  latter_ui::persisted_prefs() = latter_ui::PersistedDevicePrefs{};

  // Cold boot with no saved snapshot returns false and leaves defaults untouched
  LATTER_CHECK(!latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(!latter_ui::shortcuts_state().dock_configured);
  LATTER_CHECK(latter_ui::weather_state().condition.empty());

  // Populate live state prior to hibernate and persist snapshot (including custom dock & pinned Page 1 + POSIX TZ)
  latter_ui::area_ctx_state().epoch = 42U;
  latter_ui::area_ctx_state().rev = 9U;
  latter_ui::area_ctx_state().area_name = "Primary Suite";
  latter_ui::weather_state().epoch = 42U;
  latter_ui::weather_state().rev = 5U;
  latter_ui::weather_state().condition = "partlycloudy";
  latter_ui::weather_state().temperature = 68.5f;
  latter_ui::weather_state().sun_state = "below_horizon";

  auto &sc_save = latter_ui::shortcuts_state();
  sc_save.epoch = 42U;
  sc_save.rev = 3U;
  sc_save.dock_configured = true;
  sc_save.live_received = true;
  sc_save.dock_items.push_back(latter_ui::ShortcutItem{
      latter_ui::ShortcutKind::ROUTE, latter_ui::RouteId::APP_PINNED, "app:pinned", "", "Pinned", "", false, -1});
  sc_save.dock_items.push_back(latter_ui::ShortcutItem{
      latter_ui::ShortcutKind::ENTITY, latter_ui::RouteId::NONE, "script.call_help", "turn_on",
      "Call Thu", "mdi:bell-ring", true, -1});
  sc_save.pinned_items.push_back(latter_ui::ShortcutItem{
      latter_ui::ShortcutKind::ROUTE, latter_ui::RouteId::WEATHER_MOON, "weather:moon", "", "Moon Phase", "", false, -1});
  LATTER_CHECK(latter_ui::save_hibernate_snapshot("PST8PDT,M3.2.0,M11.1.0/2"));

  // Simulate cold boot after hibernate wake before Home Assistant connects
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::shortcuts_state() = latter_ui::ShortcutsState{};
  latter_ui::runtime_state().area = latter_ui::AreaRuntimeState{};
  latter_ui::persisted_prefs() = latter_ui::PersistedDevicePrefs{};
  latter_ui::device_prefs_state() = latter_ui::DevicePrefsState{};
  latter_ui::hydrated_posix_tz().clear();

  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::hydrated_posix_tz() == "PST8PDT,M3.2.0,M11.1.0/2");
  LATTER_CHECK(latter_ui::shortcuts_state().dock_configured);
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items.size() == 2U);
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items[1].target == "script.call_help");
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items[1].icon == "mdi:bell-ring");
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items[1].confirm);
  LATTER_CHECK(latter_ui::shortcuts_state().pinned_items.size() == 1U);
  LATTER_CHECK(latter_ui::shortcuts_state().pinned_items[0].route_id == latter_ui::RouteId::WEATHER_MOON);
  LATTER_CHECK(!latter_ui::device_prefs_state().nvs_save_pending);
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Primary Suite");
  LATTER_CHECK(latter_ui::runtime_state().area.active_area_name == "Primary Suite");
  LATTER_CHECK(latter_ui::weather_state().condition == "partlycloudy");
  LATTER_CHECK(std::fabs(latter_ui::weather_state().temperature - 68.5f) < 0.01f);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "below_horizon");

  // Critical: v3 epoch/rev guards MUST remain 0 so the very first live HA push overwrites everything!
  LATTER_CHECK(latter_ui::area_ctx_state().epoch == 0U && latter_ui::area_ctx_state().rev == 0U);
  LATTER_CHECK(latter_ui::weather_state().epoch == 0U && latter_ui::weather_state().rev == 0U);
  LATTER_CHECK(latter_ui::shortcuts_state().epoch == 0U && latter_ui::shortcuts_state().rev == 0U);

  // Calling load_hibernate_snapshot_if_cold() when live state is already populated must NOT clobber live state
  latter_ui::shortcuts_state().live_received = true;
  latter_ui::shortcuts_state().dock_items.clear();
  latter_ui::area_ctx_state().area_name = "Office";
  latter_ui::weather_state().condition = "sunny";
  latter_ui::weather_state().temperature = 74.0f;
  latter_ui::weather_state().sun_state = "above_horizon";
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::shortcuts_state().dock_items.empty());
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Office");
  LATTER_CHECK(latter_ui::weather_state().condition == "sunny");
  LATTER_CHECK(std::fabs(latter_ui::weather_state().temperature - 74.0f) < 0.01f);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "above_horizon");

  // Re-hydrate cold state, then verify first live v3 push (epoch=1, rev=1) cleanly overwrites hydrated snapshot
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::shortcuts_state() = latter_ui::ShortcutsState{};
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());

  std::vector<std::string> ac_tok(static_cast<size_t>(latter_ui::AreaCtxField::COUNT), "");
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_ID)] = "kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_NAME)] = "Kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::COVERS_OPEN)] = "3";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(1, 1, ac_tok)));
  LATTER_CHECK(latter_ui::area_ctx_state().epoch == 1U && latter_ui::area_ctx_state().rev == 1U);
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Kitchen");
  LATTER_CHECK(latter_ui::area_ctx_state().covers_open == 3);

  std::vector<std::string> wx_tok(static_cast<size_t>(latter_ui::WeatherField::COUNT), "");
  wx_tok[static_cast<size_t>(latter_ui::WeatherField::CONDITION)] = "rainy";
  wx_tok[static_cast<size_t>(latter_ui::WeatherField::TEMPERATURE)] = "59.0";
  wx_tok[static_cast<size_t>(latter_ui::WeatherField::SUN_STATE)] = "above_horizon";
  LATTER_CHECK(latter_ui::parse_v3_weather_payload(build_v3_packet(1, 1, wx_tok)));
  LATTER_CHECK(latter_ui::weather_state().epoch == 1U && latter_ui::weather_state().rev == 1U);
  LATTER_CHECK(latter_ui::weather_state().condition == "rainy");
  LATTER_CHECK(std::fabs(latter_ui::weather_state().temperature - 59.0f) < 0.01f);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "above_horizon");

  // Bounded char-array truncation & sun above_horizon round-trip
  latter_ui::area_ctx_state().area_name = std::string(80, 'y');
  latter_ui::weather_state().sun_state = "above_horizon";
  LATTER_CHECK(latter_ui::save_hibernate_snapshot("EST5EDT,M3.2.0,M11.1.0/2"));
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::area_ctx_state().area_name.size() == 47U);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "above_horizon");

  // 2. seed_hardware_sleep_defaults & 4-archetype trait parity + direct BLE_BEACON_MODE preference sync
  {
    latter_ui::PersistedDevicePrefs p_wall{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_wall, true, false, false, false);
    LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr(p_wall)) == "Always On");
    LATTER_CHECK(std::string_view(latter_ui::pref_tap_to_wake_cstr(p_wall)) == "Single Tap");
    LATTER_CHECK(std::string_view(latter_ui::pref_pickup_to_wake_cstr(p_wall)) == "Disabled");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_screen_timeout_cstr(p_wall)) == "Never");
    LATTER_CHECK(std::string_view(latter_ui::pref_docked_idle_timeout_cstr(p_wall)) == "Never");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_hibernate_timeout_cstr(p_wall)) == "Disabled");
    LATTER_CHECK(p_wall.screensaver_mode == 2U);
    LATTER_CHECK(std::string_view(latter_ui::pref_screensaver_mode_cstr(p_wall)) == "Never");
    LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(p_wall)) == "Handheld Only");

    latter_ui::PersistedDevicePrefs p_pocket{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_pocket, false, false, true, true);
    LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr(p_pocket)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_ui::pref_tap_to_wake_cstr(p_pocket)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_ui::pref_pickup_to_wake_cstr(p_pocket)) == "Disabled");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_screen_timeout_cstr(p_pocket)) == "30s");
    LATTER_CHECK(std::string_view(latter_ui::pref_docked_idle_timeout_cstr(p_pocket)) == "1 min");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_hibernate_timeout_cstr(p_pocket)) == "15 min");
    LATTER_CHECK(p_pocket.screensaver_mode == 0U);
    LATTER_CHECK(std::string_view(latter_ui::pref_screensaver_mode_cstr(p_pocket)) == "Docked Only");

    latter_ui::PersistedDevicePrefs p_wear{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_wear, false, true, false, true);
    LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr(p_wear)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_ui::pref_tap_to_wake_cstr(p_wear)) == "Double Tap");
    LATTER_CHECK(std::string_view(latter_ui::pref_pickup_to_wake_cstr(p_wear)) == "Always On");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_screen_timeout_cstr(p_wear)) == "15s");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_hibernate_timeout_cstr(p_wear)) == "15 min");
    LATTER_CHECK(p_wear.day_brightness_pct() == 80U);
    LATTER_CHECK(p_wear.night_brightness_pct() == 25U);

    latter_ui::PersistedDevicePrefs p_hand{};
    latter_ui::seed_hardware_sleep_defaults_for_traits(p_hand, false, false, false, true);
    LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr(p_hand)) == "Docked Only");
    LATTER_CHECK(std::string_view(latter_ui::pref_tap_to_wake_cstr(p_hand)) == "Single Tap");
    LATTER_CHECK(std::string_view(latter_ui::pref_pickup_to_wake_cstr(p_hand)) == "Always On");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_screen_timeout_cstr(p_hand)) == "30s");
    LATTER_CHECK(std::string_view(latter_ui::pref_battery_hibernate_timeout_cstr(p_hand)) == "30 min");
  }
  auto &prefs = latter_ui::persisted_prefs();
  prefs = latter_ui::PersistedDevicePrefs{};
  LATTER_CHECK(prefs.ble_beacon_mode == 0U);
  latter_ui::seed_hardware_sleep_defaults(prefs);
  LATTER_CHECK(prefs.ble_beacon_mode == 1U);
  LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(prefs)) == "Handheld + Screen Off");

  latter_ui::PrefMask chg{};
  uint32_t eff = 0U;
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      prefs, latter_ui::PrefKey::BLE_BEACON_MODE, "Handheld Only", true, chg, eff));
  LATTER_CHECK(prefs.ble_beacon_mode == 0U);
  LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(prefs)) == "Handheld Only");
  LATTER_CHECK((chg & latter_ui::pref_key_bit(latter_ui::PrefKey::BLE_BEACON_MODE)).any());
  LATTER_CHECK((eff & latter_ui::kPrefEffectPowerPolicy) != 0U);

  MockRpcOutbox roam_outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_ui::link_state().ha_connected = true;
  prefs.dirty_mask = 0ULL;
  eff = 0U;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::BLE_BEACON_MODE, "Handheld + Screen Off", 3000U, eff, &roam_outbox));
  uint32_t pump_t = 3100U;
  while (latter_rpc::has_pending_frames()) {
    latter_rpc::step_outbox_loop(pump_t);
    pump_t += 50U;
  }
  std::string combined_rpc;
  for (const auto &chunk : roam_outbox.published) combined_rpc += chunk;
  LATTER_CHECK(combined_rpc.find("ble_beacon_mode=Handheld + Screen Off") != std::string::npos);

  // 3. evaluate_radio_policy: fast HCI pause() vs full controller disable() + va_radio_priority
  MockBleCtrl ble{};
  MockWifiCtrl wifi{};

  // Battery + screen active (mode 0) after 2s HA settle -> enable() brings transport up
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 7000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.enables == 1 && ble.pauses == 0 && ble.disables == 0);

  // Voice session / radio boost (va_radio_priority = true) -> pause() (<1ms HCI adv stop, zero controller teardown)
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 7500U, true, 4000U, true);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.enables == 1 && ble.pauses == 1 && ble.disables == 0);

  // Voice session finishes (va_radio_priority = false) -> enable() resumes advertising immediately
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 0, true, 8000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.enables == 2 && ble.pauses == 1 && ble.disables == 0);

  // Screen turns off on battery in mode 0 ("Handheld Only") -> pause()
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 2, false, 9000U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.pauses == 2 && ble.disables == 0);

  // Plugging into USB with screen off in mode 0 ("Handheld Only") -> enable() (USB keeps Area beacon live!)
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, true, false, false, 2, false, 9250U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.enables == 3 && ble.pauses == 2 && ble.disables == 0);

  // Unplugging back to battery with screen off in mode 0 -> pause()
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 0, false, false, false, 2, false, 9400U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.pauses == 3 && ble.disables == 0);

  // Mode 1 ("Handheld + Screen Off", selected by Handheld Roaming) keeps BLE active when screen is off on battery!
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 9500U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.enables == 4 && ble.pauses == 3 && ble.disables == 0);

  // Docking on USB in mode 1 -> stays active so charging roaming Devices lock onto the local Area BLE proxy!
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, true, false, false, 0, true, 10000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.enables == 4 && ble.pauses == 3 && ble.disables == 0);

  // Yield for voice session -> pause(), then turning BLE beacon switch OFF while paused -> full disable()!
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, true, false, false, 0, true, 10250U, true, 4000U, true);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.pauses == 4);
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, false, 1, true, false, false, 0, true, 10500U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && !ble.is_paused() && !ble.is_transport_up());
  LATTER_CHECK(ble.disables == 1);

  // Re-enable, pause via Wi-Fi drop, then enter hibernate -> full disable()
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 11000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && ble.enables == 5);
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, false, true, 1, false, false, false, 2, false, 11500U, false, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.pauses == 5 && ble.disables == 1);
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, false, true, 1, false, true, false, 2, false, 12000U, false, 4000U, false);
  LATTER_CHECK(!ble.is_active() && !ble.is_paused() && !ble.is_transport_up() && ble.disables == 2);

  // 4. RaiseToWakeDetector::arm_from_wom pickup qualification (even when dyn_g == 0 after WoM baseline reset)
  auto &r2w = latter_ui::raise_wake_detector();
  r2w.reset();
  const float thresh = latter_ui::get_pickup_motion_threshold(false);
  // Resting face-up without motion or WoM must NOT wake
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10000U));
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10300U));

  // Hardware WoM IRQ arms detector directly at t=10000ms
  r2w.arm_from_wom(10000U);
  LATTER_CHECK(r2w.armed);
  // Edge-on / vertical orientation rejects face-up hold
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.95f, 0.0f, 0.15f, 10050U));
  LATTER_CHECK(r2w.face_up_since_ms == 0U);
  // Tilted face-up with dyn_g == 0.0f starts 200ms hold at t=10100ms and qualifies at t=10300ms!
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10100U));
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10250U));
  LATTER_CHECK(r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10300U));
  LATTER_CHECK(!r2w.armed);
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 10350U));

  // Expired WoM arm window (>1500ms without reaching face-up) resets cleanly without waking
  r2w.arm_from_wom(20000U);
  LATTER_CHECK(!r2w.update(0.0f, thresh, 0.0f, 0.25f, 0.95f, 21600U));
  LATTER_CHECK(!r2w.armed);

  // 5. send_device_hello ping deduplication & ui_anim_poller coordination
  latter_ui::reset_hello_sync_on_connect();
  LATTER_CHECK(latter_ui::should_trigger_hello_on_ping(1000U, 0ULL));
  latter_ui::mark_hello_in_flight(true, 1000U);
  LATTER_CHECK(!latter_ui::should_trigger_hello_on_ping(1150U, 0ULL));  // Suppressed while in flight!
  latter_ui::mark_hello_sent(1300U, 0ULL);
  LATTER_CHECK(!latter_ui::should_trigger_hello_on_ping(1800U, 0ULL));  // Suppressed <1500ms with same dirty_mask
  LATTER_CHECK(latter_ui::should_trigger_hello_on_ping(1800U, 4ULL));   // New dirty_mask bypasses suppression
  LATTER_CHECK(latter_ui::should_trigger_hello_on_ping(2900U, 0ULL));   // Allowed again >=1500ms later

  MockAnimPoller anim{};
  latter_ui::ui_anim_poller_running() = true;
  latter_ui::ensure_ui_anim_poller(&anim);
  LATTER_CHECK(anim.starts == 0 && anim.stops == 0);
  latter_ui::set_ui_anim_poller(&anim, false);
  LATTER_CHECK(!latter_ui::ui_anim_poller_running() && anim.stops == 1);
  latter_ui::set_ui_anim_poller(&anim, false);
  LATTER_CHECK(anim.stops == 1);
  latter_ui::ensure_ui_anim_poller(&anim);
  LATTER_CHECK(latter_ui::ui_anim_poller_running() && anim.starts == 1);
}

// ==============================================================================
// 8. PHASE 2: DIRTY-BAND SCISSOR COMPOSITOR, PARTIAL-BAND HIT TABLE & 2-TIER GLASS
// ==============================================================================

struct HostRgb {
  uint8_t r{0};
  uint8_t g{0};
  uint8_t b{0};
};

enum class HostGlassVariant : uint8_t {
  Clear = 0,
  ReadableDark = 1,
};

float host_srgb_chan(uint8_t v) {
  const float x = static_cast<float>(v) * (1.0f / 255.0f);
  return (x <= 0.04045f) ? (x * (1.0f / 12.92f)) : std::pow((x + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float host_rel_lum(HostRgb c) {
  return 0.2126f * host_srgb_chan(c.r) + 0.7152f * host_srgb_chan(c.g) + 0.0722f * host_srgb_chan(c.b);
}

float host_contrast(float la, float lb) {
  return (la > lb) ? ((la + 0.05f) / (lb + 0.05f)) : ((lb + 0.05f) / (la + 0.05f));
}

HostRgb host_mix_q8(HostRgb under, HostRgb over, int a) {
  a = std::clamp(a, 0, 256);
  const int ia = 256 - a;
  return {
      static_cast<uint8_t>((static_cast<int>(over.r) * a + static_cast<int>(under.r) * ia + 128) >> 8),
      static_cast<uint8_t>((static_cast<int>(over.g) * a + static_cast<int>(under.g) * ia + 128) >> 8),
      static_cast<uint8_t>((static_cast<int>(over.b) * a + static_cast<int>(under.b) * ia + 128) >> 8),
  };
}

int host_glass_alpha_for_text(HostRgb tint, HostRgb backdrop, HostRgb text, int min_alpha_q8) {
  const float lt = host_rel_lum(text);
  const int start_a = std::clamp(min_alpha_q8, 0, 256);
  for (int a = start_a; a < 256; a += 8) {
    if (host_contrast(lt, host_rel_lum(host_mix_q8(backdrop, tint, a))) >= 5.0f) return a;
  }
  return 256;
}

int host_glass_alpha_smooth(int &state, int target, uint32_t dt_ms) {
  if (state <= 0 || target >= state) {
    state = target;
  } else {
    const uint32_t clamped_dt = std::min<uint32_t>(dt_ms, 250U);
    const int fall = (clamped_dt == 0U)
                         ? 0
                         : std::max(1, static_cast<int>((4U * clamped_dt + 8U) / 16U));
    state = std::max(target, state - fall);
  }
  return state;
}

void host_resolve_glass_fill(bool bd_valid, HostRgb bd_bright, HostRgb bd_mean,
                             HostRgb tint, HostRgb ref_text, HostGlassVariant variant,
                             HostRgb &out_tint, int &out_a_top, int &out_a_bot) {
  constexpr HostRgb kHomePill{175, 190, 210};
  constexpr HostRgb kWhite{255, 255, 255};
  constexpr int kAlphaClearBase = 104;
  constexpr int kAlphaReadableMin = 208;

  const bool dark_tint = (std::max({tint.r, tint.g, tint.b}) <= 160);
  const bool is_readable = (variant == HostGlassVariant::ReadableDark);

  if (!is_readable) {
    const int mx = std::max({static_cast<int>(tint.r), static_cast<int>(tint.g), static_cast<int>(tint.b)});
    const int mn = std::min({static_cast<int>(tint.r), static_cast<int>(tint.g), static_cast<int>(tint.b)});
    const int chroma = mx - mn;
    const bool sem_tint = (chroma > 12) &&
                          !(tint.r == kHomePill.r && tint.g == kHomePill.g && tint.b == kHomePill.b) &&
                          !(tint.r <= tint.g && tint.g <= tint.b && mx <= 96 && chroma <= 30);
    const int frost_q8 = dark_tint ? (sem_tint ? 32 : 54) : 0;
    out_tint = (frost_q8 > 0) ? host_mix_q8(tint, kHomePill, frost_q8) : tint;
    const int da = dark_tint ? 12 : 0;
    out_a_top = std::min(256, kAlphaClearBase + da);
    out_a_bot = std::max(0, kAlphaClearBase - (da * 2) / 3);
    return;
  }

  (void)bd_mean;
  out_tint = tint;
  const HostRgb contrast_fg = ((static_cast<int>(ref_text.r) + ref_text.g + ref_text.b) < 48) ? kWhite : ref_text;
  const float fg_lum = host_rel_lum(contrast_fg);

  const int base_a = bd_valid
      ? host_glass_alpha_for_text(out_tint, bd_bright, contrast_fg, kAlphaReadableMin)
      : kAlphaReadableMin;
  const int da = 8;
  out_a_top = std::min(256, base_a + da);
  out_a_bot = std::max(200, base_a - (da * 2) / 3);
  if (bd_valid && host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd_bright, out_tint, out_a_bot))) < 4.5f) {
    out_a_bot = base_a;
  }
}

void test_phase2_glass_and_scissor_compositor() {
  // 1. DirtyBandTracker: minute-only band invalidation vs full-frame fallback invariants
  auto &db = latter_ui::dirty_band();
  db = latter_ui::DirtyBandTracker{};
  const uint32_t sig_idle = latter_ui::compute_takeover_signature(false, false, true, 0, false, false, false, false);
  const uint32_t sig_notif = latter_ui::compute_takeover_signature(false, false, true, 0, false, false, false, true);
  const int home_sub = latter_ui::compute_subpage_id(1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);

  // Initial settle frame -> full frame [0, SCREEN_H - 1]
  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  // Minute-only tick marks just the clock readout band [93, 168] -> 2px-aligned [92, 169], partial frame!
  latter_ui::mark_dirty_band(93, 168);
  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(!db.is_full());
  LATTER_CHECK(db.y0() == 92 && db.y1() == 169);
  LATTER_CHECK((db.y0() & 1) == 0 && ((db.y1() - db.y0() + 1) % 2) == 0);

  // Fallback A: subsequent frame with no mark_dirty_band promotes to full frame
  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  // Fallback B: anim_or_scroll_active == true promotes even if mark_dirty_band was called
  latter_ui::mark_dirty_band(93, 168);
  db.begin_frame(1, home_sub, sig_idle, true);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  // Fallback C: explicit mark_dirty_full() overrides pending narrow band
  latter_ui::mark_dirty_band(93, 168);
  latter_ui::mark_dirty_full();
  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(db.is_full() && db.y0() == 0 && db.y1() == latter_ui::SCREEN_H - 1);

  // Fallback D: page change or takeover change promotes narrow band to full frame
  latter_ui::mark_dirty_band(93, 168);
  db.begin_frame(0, 0, sig_idle, false);
  LATTER_CHECK(db.is_full());
  latter_ui::mark_dirty_band(93, 168);
  db.begin_frame(0, 0, sig_notif, false);
  LATTER_CHECK(db.is_full());

  // 8-arg begin_frame: Home (subpage==0) HH:MM rollover band [0 .. home_clock_band_y1], static 1 Hz idle-skip
  // preserving frame_hits(), Home Activity Overlay (subpage==2) HH:MM full-frame promotion, and state fallbacks
  db = latter_ui::DirtyBandTracker{};
  LATTER_CHECK(db.begin_frame(1, home_sub, sig_idle, false, 720, 85U, 174, false));
  LATTER_CHECK(db.is_full() && !db.is_idle_skip() && db.band_y0() == 0 && db.band_y1() == latter_ui::SCREEN_H - 1);
  latter_ui::frame_hits().add_centered_rect(233, 398, 280, 52, latter_ui::WidgetId::DOCK_SLOT_BASE,
                                            latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
  LATTER_CHECK(latter_ui::frame_hits().count == 1);

  // Unchanged static 1 Hz poll -> idle skip (returns false), preserving frame_hits() intact!
  LATTER_CHECK(!db.begin_frame(1, home_sub, sig_idle, false, 720, 85U, 174, false));
  LATTER_CHECK(db.is_idle_skip() && latter_ui::frame_hits().count == 1);

  // Minute rollover (hhmm_key 720 -> 721) on Home subpage 0 -> partial 2px-aligned band [0 .. 175] above weather hero
  LATTER_CHECK(db.begin_frame(1, home_sub, sig_idle, false, 721, 85U, 174, false));
  LATTER_CHECK(!db.is_full() && !db.is_idle_skip() && db.band_y0() == 0 && db.band_y1() == 175);

  // Home Activity Overlay (home_subpage == 2) promotes HH:MM rollover to full-frame so glass cards never get bisected
  const int home_overlay_sub = latter_ui::compute_subpage_id(1, 2, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0);
  LATTER_CHECK(db.begin_frame(1, home_overlay_sub, sig_idle, false, 721, 85U, 174, false));
  LATTER_CHECK(db.is_full());
  LATTER_CHECK(!db.begin_frame(1, home_overlay_sub, sig_idle, false, 721, 85U, 174, false));
  LATTER_CHECK(db.is_idle_skip());
  LATTER_CHECK(db.begin_frame(1, home_overlay_sub, sig_idle, false, 722, 85U, 174, false));
  LATTER_CHECK(db.is_full() && !db.is_idle_skip() && db.band_y0() == 0 && db.band_y1() == latter_ui::SCREEN_H - 1);

  // Battery/focus/status signature change or live_sec_active -> promotes to full frame
  LATTER_CHECK(db.begin_frame(1, home_sub, sig_idle, false, 722, 86U, 174, false));
  LATTER_CHECK(db.is_full() && db.band_y0() == 0 && db.band_y1() == latter_ui::SCREEN_H - 1);
  LATTER_CHECK(db.begin_frame(1, home_sub, sig_idle, false, 722, 86U, 174, true));
  LATTER_CHECK(db.is_full() && !db.is_idle_skip());

  // 2. FrameHitTable: partial-band frame MUST preserve 100% of hit & rotary focus regions outside [y0, y1]
  db.begin_frame(1, home_sub, sig_idle, false);  // settle on page 1
  latter_ui::mark_dirty_band(92, 169);           // minute-only band
  db.begin_frame(1, home_sub, sig_idle, false);
  LATTER_CHECK(!db.is_full() && db.y0() == 92 && db.y1() == 169);

  auto &ht = latter_ui::frame_hits();
  LATTER_CHECK(ht.count == 0);
  // Register interactive controls ABOVE (y=42), INSIDE (y=130), and BELOW (y=286, y=398, y=426) the [92, 169] band
  ht.add_centered_rect(233, 42, 220, 36, latter_ui::WidgetId::REMOTE_SLOT_PILL,
                       latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
  ht.add_centered_rect(233, 130, 260, 64, latter_ui::WidgetId::SUBHEADER_ACTION,
                       latter_ui::kHitTap, 0);
  ht.add_centered_rect(233, 286, 360, 68, latter_ui::WidgetId::LIST_SLOT_BASE,
                       latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);
  ht.add_circle(160, 398, 26, latter_ui::WidgetId::DOCK_SLOT_BASE,
                latter_ui::kHitTap | latter_ui::kHitFocusSlot | latter_ui::kHitFocusCircle, 0, 14);
  ht.add_centered_rect(233, 426, 120, 32, latter_ui::WidgetId::BACK_BUTTON,
                       latter_ui::kHitTap | latter_ui::kHitFocusSlot, 0);

  LATTER_CHECK(ht.count == 5);
  LATTER_CHECK(ht.focus_count() == 4);
  LATTER_CHECK(ht.hit_test(233, 42, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::REMOTE_SLOT_PILL);
  LATTER_CHECK(ht.hit_test(233, 130, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::SUBHEADER_ACTION);
  LATTER_CHECK(ht.hit_test(233, 286, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::LIST_SLOT_BASE);
  LATTER_CHECK(ht.hit_test(160, 398 + 34, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::DOCK_SLOT_BASE);
  LATTER_CHECK(ht.hit_test(233, 426, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::BACK_BUTTON);
  LATTER_CHECK(ht.focus_at(0)->widget_id == latter_ui::WidgetId::REMOTE_SLOT_PILL);
  LATTER_CHECK(ht.focus_at(2)->widget_id == latter_ui::WidgetId::DOCK_SLOT_BASE);
  LATTER_CHECK(ht.focus_at(3)->widget_id == latter_ui::WidgetId::BACK_BUTTON);

  // 3. GlassVariant (Clear vs ReadableDark) WCAG >= 4.5:1 contrast, alpha clamping & dt_ms smoothing invariants
  constexpr HostRgb kClearTint{22, 27, 36};
  constexpr HostRgb kReadableTint{16, 20, 28};
  constexpr HostRgb kWhiteText{255, 255, 255};
  constexpr HostRgb kMutedText{158, 158, 158};
  constexpr HostRgb kBackdrops[5] = {
      {6, 8, 12},        // Pitch-dark OLED / night sky
      {34, 42, 56},      // Dim slate wallpaper
      {92, 126, 168},    // Mid-day overcast
      {156, 210, 252},   // Bright daytime cyan sky
      {250, 252, 255},   // Sunlit snow / white camera region
  };

  HostRgb ref_clear_tint{};
  int ref_clear_top = 0, ref_clear_bot = 0;
  host_resolve_glass_fill(false, HostRgb{}, HostRgb{}, kClearTint, kWhiteText,
                          HostGlassVariant::Clear, ref_clear_tint, ref_clear_top, ref_clear_bot);
  LATTER_CHECK(ref_clear_top == 116 && ref_clear_bot == 96);

  for (const auto &bd : kBackdrops) {
    for (HostRgb fg : {kWhiteText, kMutedText}) {
      for (HostGlassVariant var : {HostGlassVariant::Clear, HostGlassVariant::ReadableDark}) {
        const HostRgb base_tint = (var == HostGlassVariant::ReadableDark) ? kReadableTint : kClearTint;
        HostRgb eff_tint{};
        int a_top = 0, a_bot = 0;
        host_resolve_glass_fill(true, bd, bd, base_tint, fg, var, eff_tint, a_top, a_bot);

        if (var == HostGlassVariant::Clear) {
          // D-041: Clear glass is strictly backdrop- and weather-invariant across all luminance levels
          LATTER_CHECK(eff_tint.r == ref_clear_tint.r &&
                       eff_tint.g == ref_clear_tint.g &&
                       eff_tint.b == ref_clear_tint.b);
          LATTER_CHECK(a_top == 116 && a_bot == 96);
        } else {
          LATTER_CHECK(a_bot >= 200 && a_top >= a_bot && a_top <= 256);
          const float fg_lum = host_rel_lum(fg);
          const float top_cr = host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd, eff_tint, a_top)));
          const float bot_cr = host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd, eff_tint, a_bot)));
          LATTER_CHECK(top_cr >= 4.5f && bot_cr >= 4.5f);
        }
      }
    }
  }

  // Calculator active operator (15% orange semantic wash on kChromeGlassTint + white text) & 2048 gold tile >= 4.5:1
  const HostRgb calc_active_wash = host_mix_q8(kClearTint, HostRgb{255, 145, 30}, 38);
  LATTER_CHECK(host_contrast(host_rel_lum(kWhiteText), host_rel_lum(calc_active_wash)) >= 8.0f);
  const HostRgb tile_2048_wash = host_mix_q8(kClearTint, HostRgb{158, 133, 28}, 38);
  LATTER_CHECK(host_contrast(host_rel_lum(kWhiteText), host_rel_lum(tile_2048_wash)) >= 8.0f);

  // dt_ms-normalized glass_alpha_smooth: instant rise, proportional decay, clamped at 250 ms
  int alpha_st = 0;
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 220, 16U) == 220);
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 240, 16U) == 240);  // Instant step-up on brighter backdrop
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 160, 16U) == 236);  // -4 at 60 FPS (16 ms)
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 160, 32U) == 228);  // -8 at 30 FPS (32 ms)
  LATTER_CHECK(host_glass_alpha_smooth(alpha_st, 160, 1000U) >= 165);  // Clamped at 250 ms max jump

  // Cross-check production headers & page sources so tokens and retired bespoke overrides never regress
  const std::string ui_types_h = read_repo_file("../../include/ui_types.h");
  const std::string ui_gfx_h = read_repo_file("../../include/ui_gfx.h");
  const std::string home_cpp = read_repo_file("../../components/latter_ui/page_clock_home.cpp");
  const std::string tools_cpp = read_repo_file("../../components/latter_ui/page_tools.cpp");
  const std::string home_hub_cpp = read_repo_file("../../components/latter_ui/page_home_hub.cpp");

  LATTER_CHECK(ui_types_h.find("enum class GlassVariant : uint8_t") != std::string::npos);
  LATTER_CHECK(ui_types_h.find("kChromeModalBg(16, 20, 28)") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassAlphaMinQ8 = 96") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassClearAlphaQ8 = 104") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassClearAlphaMaxQ8 = 116") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassReadableAlphaMinQ8 = 208") != std::string::npos);
  LATTER_CHECK(home_cpp.find("resolve_home_card_palette") == std::string::npos);
  LATTER_CHECK(home_cpp.find("glass_alpha_for_text") == std::string::npos);
  LATTER_CHECK(tools_cpp.find("Color(230, 115, 0)") == std::string::npos);
  LATTER_CHECK(tools_cpp.find("GlassVariant::ReadableDark") == std::string::npos);
  LATTER_CHECK(home_hub_cpp.find("kChromeGlassTint, 176") == std::string::npos);
  LATTER_CHECK(home_hub_cpp.find("GlassVariant::ReadableDark") == std::string::npos);
}

void test_phase3_weather_sky_backdrop_and_home_scene() {
  using latter_ui::BackdropMode;
  using latter_ui::resolve_backdrop_mode;

  // 1. Single Authoritative Backdrop Precedence (D-038, D-067)
  // On Home (gui_page=1, home_subpage=0) AND Sky & Weather (gui_page=5):
  // Independent home_weather_bg / weather_app_bg (or weather_sim_active) wins over CustomWallpaper & DayNightSky!
  LATTER_CHECK(resolve_backdrop_mode(1, 0, true, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, true, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, false, true, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(5, 2, false, false, true, true) == BackdropMode::LiveWeatherSky);
  // Independent split: Home OFF + Weather App ON vs Home ON + Weather App OFF
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, false, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, true, false, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, true, false, false, true) == BackdropMode::CustomWallpaper);
  // When weather_background is OFF (and sim inactive), DayNightSky > CustomWallpaper > SolidBlack
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, false, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, false, false, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, false, false, false, true) == BackdropMode::DayNightSky);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, false, false, false, false, true) == BackdropMode::DayNightSky);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, false, false, false, false) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, false, false, false, false, false) == BackdropMode::SolidBlack);
  // Home subpage 1 & 2 keep DayNightSky / CustomWallpaper even while weather_background is ON!
  LATTER_CHECK(resolve_backdrop_mode(1, 1, true, true, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(1, 2, true, true, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(1, 2, true, true, false, false, true) == BackdropMode::DayNightSky);
  LATTER_CHECK(resolve_backdrop_mode(1, 2, true, true, false, false, false) == BackdropMode::SolidBlack);
  // System (4), Area (6), Remote (7), Tools (8), Home Hub (9), Music (2), Clock (0), Games (3)
  LATTER_CHECK(resolve_backdrop_mode(4, 0, true, true, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(6, 0, true, true, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(7, 0, true, true, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(8, 0, true, true, false, true, false, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(8, 0, true, true, false, true, false, true) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(9, 0, true, true, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(2, 0, true, true, false, true, false, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(2, 0, true, true, false, false, true, false, true) == BackdropMode::DayNightSky);
  LATTER_CHECK(resolve_backdrop_mode(2, 0, true, true, false, true, false, false, false) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(0, 0, true, true, false, true, true) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(3, 0, true, true, false, true, true) == BackdropMode::SolidBlack);

  // 2. Zero-Copy UiStrView invariants & temporary-rvalue compile-time guard
  static_assert(!std::is_constructible_v<latter_ui::UiStrView, std::string &&>);
  static_assert(std::is_trivially_copyable_v<latter_ui::UiStrView>);
  const std::string live_s = "Thunderstorm (Night)";
  const latter_ui::UiStrView v_empty{};
  const latter_ui::UiStrView v_lit{"Minimal Digital"};
  const latter_ui::UiStrView v_str{live_s};
  LATTER_CHECK(v_empty.empty() && std::string_view(v_empty.c_str()) == "");
  LATTER_CHECK(!v_lit.empty() && v_lit == "Minimal Digital" && v_lit != "Aurora");
  LATTER_CHECK(v_str == live_s && v_str.find("Night") != std::string_view::npos);
  LATTER_CHECK(v_str.rfind("Thunder", 0) == 0 && v_str.substr(0, 7) == "Thunder");

  // 3. FrameHitTable Activity Card sub-regions (single-row round + two-row square/rect + rotary focus order)
  latter_ui::FrameHitTable ht{};
  ht.clear();
  latter_ui::register_home_activity_hit_regions(ht, 233, 270, 380, 68, false, true, 3, -1, 56, 52);
  LATTER_CHECK(ht.count == 3 && ht.focus_count() == 3);
  LATTER_CHECK(ht.focus_at(0)->widget_id == latter_ui::WidgetId::HOME_ACT_ROW_0 && ht.focus_at(0)->param == 3);
  LATTER_CHECK(ht.focus_at(1)->widget_id == latter_ui::WidgetId::HOME_ACT_ACTION_0 && ht.focus_at(1)->param == 3);
  LATTER_CHECK(ht.focus_at(2)->widget_id == latter_ui::WidgetId::HOME_ACT_EXPAND);
  // Left label body, quick-action button column, and dropdown chevron column never overlap
  LATTER_CHECK(ht.hit_test(140, 270, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ROW_0);
  LATTER_CHECK(ht.hit_test(423 - 80, 270, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ACTION_0);
  LATTER_CHECK(ht.hit_test(423 - 20, 270, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_EXPAND);

  ht.clear();
  latter_ui::register_home_activity_hit_regions(ht, 240, 290, 380, 104, true, true, 2, 7, 56, 52);
  LATTER_CHECK(ht.count == 5 && ht.focus_count() == 5);
  LATTER_CHECK(ht.focus_at(0)->widget_id == latter_ui::WidgetId::HOME_ACT_ROW_0 && ht.focus_at(0)->param == 2);
  LATTER_CHECK(ht.focus_at(1)->widget_id == latter_ui::WidgetId::HOME_ACT_ACTION_0 && ht.focus_at(1)->param == 2);
  LATTER_CHECK(ht.focus_at(2)->widget_id == latter_ui::WidgetId::HOME_ACT_ROW_1 && ht.focus_at(2)->param == 7);
  LATTER_CHECK(ht.focus_at(3)->widget_id == latter_ui::WidgetId::HOME_ACT_ACTION_1 && ht.focus_at(3)->param == 7);
  LATTER_CHECK(ht.focus_at(4)->widget_id == latter_ui::WidgetId::HOME_ACT_EXPAND);
  LATTER_CHECK(ht.hit_test(150, 265, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ROW_0);
  LATTER_CHECK(ht.hit_test(150, 315, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ROW_1);
  LATTER_CHECK(ht.hit_test(430 - 80, 265, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ACTION_0);
  LATTER_CHECK(ht.hit_test(430 - 80, 315, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_ACTION_1);
  LATTER_CHECK(ht.hit_test(430 - 20, 290, latter_ui::kHitTap)->widget_id == latter_ui::WidgetId::HOME_ACT_EXPAND);

  // 4. Protocol v3 area_icon (field 42), Roaming Area predicate, and Wake Greeting >= 30 min standby trigger
  latter_ui::reset_all_v3_rev_guards();
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  std::vector<std::string> ac_tok(static_cast<size_t>(latter_ui::AreaCtxField::COUNT), "");
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_ID)] = "kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_NAME)] = "Kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_ICON)] = "mdi:countertop";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(500, 1, ac_tok)));
  LATTER_CHECK(latter_ui::area_ctx_state().area_icon == "mdi:countertop");

  auto &prefs = latter_ui::persisted_prefs();
  // D-071: Active Area icon drives the Home Dock's Area Control slot dynamically (fallback "mdi:sofa")
  LATTER_CHECK(std::string_view(latter_ui::resolve_area_dock_icon(latter_ui::area_ctx_state())) == "mdi:countertop");
  latter_ui::area_ctx_state().area_icon.clear();
  LATTER_CHECK(std::string_view(latter_ui::resolve_area_dock_icon(latter_ui::area_ctx_state())) == "mdi:sofa");
  latter_ui::area_ctx_state().area_id.clear();
  latter_ui::area_ctx_state().area_name = "Unassigned";
  latter_ui::area_ctx_state().area_icon = "mdi:desk";
  LATTER_CHECK(std::string_view(latter_ui::resolve_area_dock_icon(latter_ui::area_ctx_state())) == "mdi:sofa");

  auto &rs = latter_ui::runtime_state();
  prefs.wake_greeting = true;
  rs.nav.wake_greeting_pending = false;
  rs.nav.display_power_state = 2;
  rs.nav.sleep_start_time = 0;
  rs.nav.standby_entered_ms = 1000U;
  latter_ui::note_wake_from_standby(60000U);  // 59s standby (< 30 min) -> stays false
  LATTER_CHECK(!rs.nav.wake_greeting_pending);
  rs.nav.standby_entered_ms = 1000U;
  latter_ui::note_wake_from_standby(1000U + latter_ui::kStandbyGreetingIdleMs);  // >= 30 min standby -> true!
  LATTER_CHECK(rs.nav.wake_greeting_pending);

  // 5. Phase 3 source-level architectural guards
  const std::string comp_h = read_repo_file("../../include/ui_compositor.h");
  const std::string comp_cpp = read_repo_file("../../components/latter_ui/ui_compositor.cpp");
  const std::string weather_cpp = read_repo_file("../../components/latter_ui/page_weather.cpp");
  const std::string home_cpp = read_repo_file("../../components/latter_ui/page_clock_home.cpp");
  const std::string touch_cpp = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  const std::string gui_yaml = read_repo_file("../../guis/universal/gui.yaml");
  const std::string timers_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");

  LATTER_CHECK(comp_h.find("struct UiStrView") != std::string::npos);
  LATTER_CHECK(comp_h.find("resolve_backdrop_mode(") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("kHomeActivityRotateMs") == std::string::npos);
  LATTER_CHECK(weather_cpp.find("SkyBaseCache") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("MALLOC_CAP_SPIRAM") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("LightningFlashState") != std::string::npos);
  LATTER_CHECK(latter_ui::kBatteryMotionWindowMs == 35000U);
  LATTER_CHECK(weather_cpp.find("WeatherSkyFadeState") != std::string::npos);
  LATTER_CHECK(weather_cpp.find("fmodf(t_sec") == std::string::npos);
  LATTER_CHECK(weather_cpp.find("/ 220U") == std::string::npos);
  LATTER_CHECK(home_cpp.find("fast_print_alpha(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("render_area_archetype_icon(") == std::string::npos);
  LATTER_CHECK(home_cpp.find("render_mdi_icon(") != std::string::npos);
  LATTER_CHECK(latter_ui::mdi_catalog_count() == 7447U);
  for (std::string_view slug : {
           "mdi:garage-open-variant", "mdi:cog-outline", "mdi:faucet-variant",
           "mdi:sofa-outline", "mdi:desk", "mdi:door", "mdi:bathtub-outline",
           "mdi:bed-outline", "mdi:cradle-outline", "mdi:stairs", "mdi:map-marker-outline"}) {
    LATTER_CHECK(latter_ui::has_mdi_icon(slug));
    int span_count = 0;
    int covered_px = 0;
    const bool ok = latter_ui::rasterize_mdi_icon_fn(
        slug, 100, 100, 28, [&](int x0, int x1_excl, int y, uint8_t a) {
          LATTER_CHECK(x1_excl > x0 && y >= 84 && y <= 116 && a > 0);
          ++span_count;
          covered_px += (x1_excl - x0);
        });
    LATTER_CHECK(ok && span_count >= 12 && covered_px >= 40 && covered_px <= 700);
  }
  LATTER_CHECK(!latter_ui::has_mdi_icon("mdi:definitely-not-a-real-icon-xyz"));
  LATTER_CHECK(home_cpp.find("register_home_activity_hit_regions(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("std::max(prev_clip_y0, card_y0 + 1)") != std::string::npos);
  LATTER_CHECK(home_cpp.find("std::min(prev_clip_y1, card_y0 + card_h - 2)") != std::string::npos);
  LATTER_CHECK(home_cpp.find("Color(28, 54, 84)") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("WidgetId::HOME_AREA_ICON") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("WidgetId::HOME_ACT_EXPAND") != std::string::npos);
  LATTER_CHECK(touch_cpp.find("WidgetId::HOME_ACTIVITY_CARD") == std::string::npos);
  LATTER_CHECK(gui_yaml.find("resolve_backdrop_mode(") != std::string::npos);
  LATTER_CHECK(gui_yaml.find("schedule_weather_disp_update(") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("resolve_backdrop_mode(") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("weather_sky_motion_stepping(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("dispatch_cadence_20ms(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("dispatch_cadence_250ms(") != std::string::npos);
}

// ==============================================================================
// 9. PHASE 4: AUDIO SESSION ARBITRATION, INTENT AUTO-NAV & 5-MIN PAUSE TIMEOUT
// ==============================================================================

void test_phase4_audio_session_and_music_pause_timeout() {
  using latter_ui::AudioSessionController;
  using latter_ui::AudioSessionPhase;
  using latter_ui::MusicPlayIntent;

  LATTER_CHECK(AudioSessionController::kRecentPausedMusicTimeoutMs == 300000U);

  // 1. should_auto_open_music_page across intents, continuous sessions, Single-App Mode & alerts
  AudioSessionController ctrl;
  ctrl.reset();

  // Fresh ExternalOrUserExplicit session opens PageId::MUSIC when home_screen_enabled == true
  LATTER_CHECK(ctrl.should_auto_open_music_page(
      /*current_gui_page=*/1, /*home_screen_enabled=*/true, /*start_screen_page=*/1,
      /*alert_or_ringing=*/false, 1000U));
  LATTER_CHECK(ctrl.phase() == AudioSessionPhase::MusicPlaying);

  // Continuous session after user navigated back to Home (current_gui_page == 1) does NOT yank back to Music
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 2000U));
  // Already on Music (current_gui_page == 2) stays true
  LATTER_CHECK(ctrl.should_auto_open_music_page(2, true, 1, false, 2500U));

  // Stopping local playback ends the continuous session so next explicit play opens Music again
  ctrl.note_local_playback_stopped();
  LATTER_CHECK(ctrl.should_auto_open_music_page(1, true, 1, false, 3000U));

  // AutoResumeAfterVa suppresses auto-open even on a fresh playback start
  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::AutoResumeAfterVa, 5000U);
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 5200U));

  // HomeQuickToggle suppresses auto-open so toggling from Home Activity Card stays on Home
  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::HomeQuickToggle, 6000U);
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 6100U));

  // TrackAdvanceOrSeek suppresses auto-open
  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::TrackAdvanceOrSeek, 7000U);
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 7050U));

  // Expired intent (> kPlayIntentWindowMs) falls back to ExternalOrUserExplicit -> true
  ctrl.reset();
  ctrl.set_play_intent(MusicPlayIntent::HomeQuickToggle, 8000U);
  LATTER_CHECK(ctrl.should_auto_open_music_page(
      1, true, 1, false, 8000U + AudioSessionController::kPlayIntentWindowMs + 1U));

  // Single-App Mode: suppressed when start_screen != 2, allowed when start_screen == 2
  ctrl.reset();
  LATTER_CHECK(!ctrl.should_auto_open_music_page(6, /*home_screen_enabled=*/false, /*start_screen_page=*/6, false, 9000U));
  ctrl.reset();
  LATTER_CHECK(ctrl.should_auto_open_music_page(2, /*home_screen_enabled=*/false, /*start_screen_page=*/2, false, 9500U));

  // Active alarm/timer alert or OTA/hibernating suppresses Music auto-open
  ctrl.reset();
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, /*alert_or_ringing=*/true, 10000U));
  ctrl.reset();
  LATTER_CHECK(!ctrl.should_auto_open_music_page(1, true, 1, false, 10100U, /*ota_or_hibernating=*/true));

  // Natural track gap (IDLE -> PLAYING while ms.is_local && ms.state == "playing") keeps continuous session
  // so user who navigated back to Home is NOT yanked back to PageId::MUSIC when Track 2 starts
  {
    auto &sess = latter_ui::audio_session();
    sess.reset();
    latter_ui::music_queue_cleared() = false;
    latter_ui::clear_remote_play_override();
    auto &ms = latter_ui::media_state();
    auto &ac = latter_ui::area_ctx_state();
    ms = latter_ui::MediaState{};
    ms.is_local = true;
    ms.state = "playing";
    ms.title = "Track 1";
    ms.active_speaker = "media_player.local_puck";
    ac.area_id = "living_room";

    // Track 1 starts -> auto-opens Music
    latter_ui::sync_audio_session_media_observation(true, false, 11000U, ms, ac);
    LATTER_CHECK(sess.should_auto_open_music_page(1, true, 1, false, 11000U));
    // User navigates back to Home (gui_page = 1); Track 1 hits transient IDLE before Track 2 opens
    // (on_idle skips note_local_playback_stopped() while ms.is_local && ms.state == "playing")
    latter_ui::sync_audio_session_media_observation(false, false, 11400U, ms, ac);
    LATTER_CHECK(sess.is_music_visible_on_home_activity(
        false, latter_ui::is_music_paused_effective(false, ms.is_local, ms.state), true, 11400U));
    // Track 2 opens and fires on_play -> continuous session stays on Home (returns false!)
    ms.title = "Track 2";
    latter_ui::sync_audio_session_media_observation(true, false, 11800U, ms, ac);
    LATTER_CHECK(!sess.should_auto_open_music_page(1, true, 1, false, 11800U));
  }

  // 2. note_media_state_observation + is_music_visible_on_home_activity + paused_music_expiry_due_ms
  ctrl.reset();
  // Cold boot / reconnect into an already-paused speaker -> NEVER visible on Home Activity Card
  ctrl.note_media_state_observation(
      /*is_effectively_playing=*/false, /*is_paused_state=*/true,
      "media_player.kitchen", "kitchen", 1000U);
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 1000U));
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(1000U) == 0U);

  // Live playing -> visible; then live playing -> paused on same speaker/Area -> visible for 5 min (300,000 ms)
  ctrl.note_media_state_observation(true, false, "media_player.kitchen", "kitchen", 5000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(true, false, true, 5000U));
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(true, false, false, 5000U));
  LATTER_CHECK(ctrl.phase() == AudioSessionPhase::MusicPlaying);

  ctrl.note_media_state_observation(false, true, "media_player.kitchen", "kitchen", 10000U);
  LATTER_CHECK(ctrl.phase() == AudioSessionPhase::MusicPausedRecent);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, true, true, 10000U));      // +0 ms
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, false, 10000U));    // empty title gated
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(10000U) == 300000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, true, true, 309999U));     // +299,999 ms -> still visible
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(309999U) == 1U);
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 310000U));    // +300,000 ms -> expired!
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(310000U) == 0U);

  // Area roam (speaker_id or area_id change) into an Area with an already-paused speaker -> false immediately
  ctrl.note_media_state_observation(true, false, "media_player.kitchen", "kitchen", 400000U);
  ctrl.note_media_state_observation(false, true, "media_player.kitchen", "kitchen", 405000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, true, true, 406000U));
  ctrl.note_media_state_observation(false, true, "media_player.office", "office", 407000U);
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 407000U));
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(407000U) == 0U);

  // Area roam with area_id change even before speaker_id updates -> false immediately
  ctrl.note_media_state_observation(true, false, "media_player.office", "office", 410000U);
  ctrl.note_media_state_observation(false, true, "media_player.office", "office", 412000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, true, true, 412500U));
  ctrl.note_media_state_observation(false, true, "media_player.office", "patio", 413000U);
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 413000U));

  // set_paused_for_va(true) preserves visibility while VA turn runs and survives transient IDLE states
  // (e.g. TTS ANNOUNCING -> IDLE before ms.state updates) without wiping paused_for_va_ or last_live_pause_ms_
  ctrl.note_media_state_observation(true, false, "media_player.patio", "patio", 500000U);
  ctrl.set_paused_for_va(true, 502000U);
  ctrl.note_media_state_observation(false, false, "media_player.patio", "patio", 503000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, false, true, 900000U));
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(505000U) == 0U);
  ctrl.set_paused_for_va(false, 505000U);
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(505000U) == 297000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, true, true, 505000U));

  // Sendspin local PLAYING -> IDLE race before HA pushes ms.state = "paused":
  // is_music_paused_effective stamps last_live_pause_ms_ at 605000U so subsequent ms.state = "paused" keeps 5-min window
  {
    auto &sess = latter_ui::audio_session();
    sess.reset();
    latter_ui::music_queue_cleared() = false;
    latter_ui::clear_remote_play_override();
    auto &ms = latter_ui::media_state();
    auto &ac = latter_ui::area_ctx_state();
    ms = latter_ui::MediaState{};
    ms.is_local = true;
    ms.state = "playing";
    ms.title = "Sendspin Song";
    ms.active_speaker = "media_player.study_puck";
    ac.area_id = "study";

    latter_ui::sync_audio_session_media_observation(true, false, 600000U, ms, ac);
    LATTER_CHECK(sess.is_music_visible_on_home_activity(true, false, true, 600000U));

    // Local media_player transitions PLAYING -> IDLE 200 ms before HA pushes ms.state = "paused"
    LATTER_CHECK(latter_ui::is_music_paused_effective(false, true, ms.state, false));
    latter_ui::sync_audio_session_media_observation(false, false, 605000U, ms, ac);
    LATTER_CHECK(sess.paused_music_expiry_due_ms(605000U) == 300000U);
    LATTER_CHECK(sess.is_music_visible_on_home_activity(
        false, latter_ui::is_music_paused_effective(false, ms.is_local, ms.state), true, 605000U));

    // HA pushes ms.state = "paused" 200 ms later -> 5-min window stays anchored at 605000U
    ms.state = "paused";
    latter_ui::sync_audio_session_media_observation(false, false, 605200U, ms, ac);
    LATTER_CHECK(sess.paused_music_expiry_due_ms(605200U) == 299800U);
    LATTER_CHECK(sess.is_music_visible_on_home_activity(
        false, latter_ui::is_music_paused_effective(false, ms.is_local, ms.state), true, 904999U));
    LATTER_CHECK(!sess.is_music_visible_on_home_activity(
        false, latter_ui::is_music_paused_effective(false, ms.is_local, ms.state), true, 905000U));
  }

  // clear_paused_music() and "idle" state clear visibility immediately
  ctrl.clear_paused_music();
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, true, true, 505100U));
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(505100U) == 0U);

  ctrl.note_media_state_observation(true, false, "media_player.patio", "patio", 510000U);
  ctrl.note_media_state_observation(false, true, "media_player.patio", "patio", 512000U);
  LATTER_CHECK(ctrl.is_music_visible_on_home_activity(false, true, true, 512500U));
  ctrl.note_media_state_observation(false, false, "media_player.patio", "patio", 513000U);  // "idle" / "off"
  LATTER_CHECK(!ctrl.is_music_visible_on_home_activity(false, false, true, 513000U));
  LATTER_CHECK(ctrl.paused_music_expiry_due_ms(513000U) == 0U);

  // Alert preemption transitions phase to AlertRinging
  ctrl.preempt_for_alert(520000U);
  LATTER_CHECK(ctrl.phase() == AudioSessionPhase::AlertRinging);

  // 3. Phase 4 source-level architectural invariants
  const std::string audio_yaml = read_repo_file("../../modules/audio_voice.yaml");
  const std::string timers_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");
  const std::string home_cpp = read_repo_file("../../components/latter_ui/page_clock_home.cpp");
  const std::string touch_cpp = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  const std::string comp_cpp = read_repo_file("../../components/latter_ui/ui_compositor.cpp");
  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");

  LATTER_CHECK(audio_yaml.find("- id: preempt_va_for_alert") != std::string::npos);
  LATTER_CHECK(audio_yaml.find("- script.execute: preempt_va_for_alert") != std::string::npos);
  LATTER_CHECK(audio_yaml.find("- id: start_voice_assistant") != std::string::npos);
  LATTER_CHECK(audio_yaml.find("- script.stop: start_voice_assistant") != std::string::npos);

  LATTER_CHECK(timers_yaml.find("id(preempt_va_for_alert).execute();") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("cancel_voice_assistant") == std::string::npos);
  LATTER_CHECK(comp_cpp.find("paused_music_expiry_due_ms(") != std::string::npos);

  LATTER_CHECK(touch_cpp.find("set_play_intent(MusicPlayIntent::HomeQuickToggle") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("is_music_paused_effective(") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("is_music_visible_on_home_activity(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("is_music_paused_effective(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("is_music_visible_on_home_activity(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("has_pending_home_activity_fade_settle(") != std::string::npos);
  LATTER_CHECK(comp_cpp.find("has_pending_home_activity_fade_settle(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("struct HomeActivityAnimState") != std::string::npos);
  LATTER_CHECK(home_cpp.find("fade_out_start_ms") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-039:") != std::string::npos);
}

// ==============================================================================
// 11. BATTERY WAKE REGRESSION: PANEL DCS TIMEOUT ISOLATION & POST-WAKE GRACE
// ==============================================================================
void test_panel_dcs_timeout_isolation_and_wake_grace() {
  using namespace latter_ui;

  // 1. Compile-time + runtime timeout ID isolation on disp1 Component
  static_assert(kTimeoutPanelDcsId == 0x4C410010U, "Panel DCS timeout must live in HAL range 0x4C410010..0x4C41001F");
  static_assert(kTimeoutPanelDcsId != kTimeoutAsyncDrawId);
  static_assert(kTimeoutPanelDcsId != kTimeoutFbClearId);
  static_assert(kTimeoutPanelDcsId != kTimeoutWeatherStepId);
  static_assert(kTimeoutPanelDcsId != kTimeoutActivityRotateId);

  // 2. Simulate disp1 numeric set_timeout table across Battery Screen Off -> Wake -> Home WeatherSky render
  struct SimTimeoutEntry {
    uint32_t id = 0U;
    uint32_t due_ms = 0U;
    std::function<void()> fn;
  };
  std::vector<SimTimeoutEntry> timeouts;
  auto set_sim_timeout = [&](uint32_t id, uint32_t now_ms, uint32_t delay_ms, std::function<void()> fn) {
    for (auto &e : timeouts) {
      if (e.id == id) {
        e.due_ms = now_ms + delay_ms;
        e.fn = std::move(fn);
        return;
      }
    }
    timeouts.push_back(SimTimeoutEntry{id, now_ms + delay_ms, std::move(fn)});
  };
  auto run_due_timeouts = [&](uint32_t now_ms) {
    for (size_t i = 0; i < timeouts.size();) {
      if (static_cast<int32_t>(now_ms - timeouts[i].due_ms) >= 0) {
        auto fn = std::move(timeouts[i].fn);
        timeouts.erase(timeouts.begin() + static_cast<std::ptrdiff_t>(i));
        if (fn) fn();
      } else {
        ++i;
      }
    }
  };

  PanelDcsState dcs{};
  uint8_t hw_bri = 255U;
  std::vector<uint8_t> dcs_log;

  std::function<void(uint32_t)> service_sim;
  service_sim = [&](uint32_t now_ms) {
    const PanelDcsStepResult step = step_panel_dcs_state(dcs, now_ms);
    if (step.send_slpout) dcs_log.push_back(0x11U);
    if (step.send_dispon) dcs_log.push_back(0x29U);
    if (step.apply_hw_bri) hw_bri = step.hw_bri;
    if (step.send_dispoff) dcs_log.push_back(0x28U);
    if (step.send_slpin) dcs_log.push_back(0x10U);
    if (step.rearm_ms > 0U) {
      set_sim_timeout(kTimeoutPanelDcsId, now_ms, step.rearm_ms, [&, due = now_ms + step.rearm_ms]() {
        service_sim(due);
      });
    }
  };

  // Screen timeout on battery (t = 5000 ms): AWAKE -> ASLEEP (DISPOFF 0x28 + SLPIN 0x10, hw_bri = 0)
  dcs.want_bri = 0U;
  service_sim(5000U);
  LATTER_CHECK(dcs.phase == PanelDcsState::ASLEEP);
  LATTER_CHECK(hw_bri == 0U);
  LATTER_CHECK((dcs_log == std::vector<uint8_t>{0x28U, 0x10U}));
  dcs_log.clear();

  // Short Crown press / Tap-to-Wake / Pickup-to-Wake at t = 10000 ms:
  // SLPOUT (0x11) fires immediately, panel enters WAKING, DISPON + brightness deferred +120 ms on kTimeoutPanelDcsId.
  dcs.want_bri = 230U;
  service_sim(10000U);
  LATTER_CHECK(dcs.phase == PanelDcsState::WAKING);
  LATTER_CHECK(hw_bri == 0U);
  LATTER_CHECK((dcs_log == std::vector<uint8_t>{0x11U}));

  // Immediate Home render at t = 10003 ms schedules AsyncDraw, LiveWeatherSky / WakeGreeting (0x4C410003), and ActivityRotate.
  // Regression check: none of those compositor timeouts may clobber kTimeoutPanelDcsId (0x4C410010)!
  int weather_ticks = 0;
  set_sim_timeout(kTimeoutAsyncDrawId, 10003U, 0U, []() {});
  set_sim_timeout(kTimeoutWeatherStepId, 10003U, 66U, [&]() { ++weather_ticks; });
  set_sim_timeout(kTimeoutWeatherStepId, 10003U, 1500U, [&]() { ++weather_ticks; });
  set_sim_timeout(kTimeoutActivityRotateId, 10003U, 5000U, []() {});

  run_due_timeouts(10050U);
  LATTER_CHECK(dcs.phase == PanelDcsState::WAKING);
  LATTER_CHECK(hw_bri == 0U);

  // At t = 10120 ms (+120 ms post-SLPOUT), kTimeoutPanelDcsId fires DISPON (0x29) + restores brightness 230.
  run_due_timeouts(10120U);
  LATTER_CHECK(dcs.phase == PanelDcsState::AWAKE);
  LATTER_CHECK(hw_bri == 230U);
  LATTER_CHECK((dcs_log == std::vector<uint8_t>{0x11U, 0x29U}));
  LATTER_CHECK(weather_ticks == 0);
  run_due_timeouts(11510U);
  LATTER_CHECK(weather_ticks == 1);

  // 3. Early re-sleep during WAKING respects MIPI-DCS 120 ms SLPOUT->SLPIN minimum wait + poll self-heal
  dcs = PanelDcsState{};
  dcs.phase = PanelDcsState::ASLEEP;
  dcs.want_bri = 180U;
  PanelDcsStepResult w1 = step_panel_dcs_state(dcs, 20000U);
  LATTER_CHECK(w1.send_slpout && !w1.send_dispon && w1.rearm_ms == 120U);
  LATTER_CHECK(dcs.phase == PanelDcsState::WAKING);

  // Request screen-off at +40 ms (20040 ms): brightness drops to 0 immediately, SLPIN waits remaining 80 ms
  dcs.want_bri = 0U;
  PanelDcsStepResult w2 = step_panel_dcs_state(dcs, 20040U);
  LATTER_CHECK(w2.apply_hw_bri && w2.hw_bri == 0U && !w2.send_slpin && !w2.send_dispoff && w2.rearm_ms == 80U);
  LATTER_CHECK(dcs.phase == PanelDcsState::WAKING);

  PanelDcsStepResult w3 = step_panel_dcs_state(dcs, 20120U);
  LATTER_CHECK(w3.send_slpin && !w3.send_dispoff && dcs.phase == PanelDcsState::ASLEEP);

  // Self-healing poll path: if WAKING settled >=120 ms ago and want_bri > 0, next poll completes DISPON + AWAKE
  dcs.want_bri = 200U;
  (void)step_panel_dcs_state(dcs, 30000U);
  LATTER_CHECK(dcs.phase == PanelDcsState::WAKING);
  LATTER_CHECK(panel_dcs_settle_left_for(dcs, 30050U) == 70U);
  LATTER_CHECK(panel_dcs_settle_left_for(dcs, 30120U) == 0U);
  PanelDcsStepResult w4 = step_panel_dcs_state(dcs, 30250U);
  LATTER_CHECK(w4.send_dispon && w4.apply_hw_bri && w4.hw_bri == 200U && w4.rearm_ms == 0U);
  LATTER_CHECK(dcs.phase == PanelDcsState::AWAKE);

  // 4. Post-GPIO-wake fast-loop grace window (PmWakeGraceWindow, kPmWakeGraceMs = 600 ms)
  static_assert(kPmWakeGraceMs == 600U, "Post-GPIO-wake grace must cover 20ms button debounce + 450ms double-tap");
  PmWakeGraceWindow grace{};
  LATTER_CHECK(!grace.active(0U));
  LATTER_CHECK(grace.remaining_ms(0U) == 0U);

  grace.arm(1000U);
  LATTER_CHECK(grace.active(1000U) && grace.remaining_ms(1000U) == 600U);
  LATTER_CHECK(grace.active(1020U));  // 20 ms pwr_key delayed_on_off debounce completes in fast loop
  LATTER_CHECK(grace.active(1150U));  // 150 ms short press release completes in fast loop
  LATTER_CHECK(grace.active(1450U));  // 450 ms wearable double-tap window completes in fast loop
  LATTER_CHECK(grace.active(1599U) && grace.remaining_ms(1599U) == 1U);
  LATTER_CHECK(!grace.active(1600U) && grace.remaining_ms(1600U) == 0U);

  // Explicit clear on awake -> quiet transition
  grace.arm(2000U);
  LATTER_CHECK(grace.active(2100U));
  grace.clear();
  LATTER_CHECK(!grace.active(2100U));

  // uint32_t millis() wraparound across 0xFFFFFFFFU (including exact 0U target sentinel)
  grace.arm(0xFFFFFFFFU - 599U, 600U);  // target == 0U -> promoted to 1U sentinel
  LATTER_CHECK(grace.until_ms == 1U);
  LATTER_CHECK(grace.active(0xFFFFFFFFU));
  LATTER_CHECK(grace.active(0U));
  LATTER_CHECK(!grace.active(1U));

  // 5. Source-level invariants across power_manager.yaml, pmu_axp2101.yaml, latter_core.h, power_pm.cpp
  const std::string pm_yaml = read_repo_file("../../modules/power_manager.yaml");
  const std::string axp_yaml = read_repo_file("../../modules/hal/pmu_axp2101.yaml");
  const std::string core_h = read_repo_file("../../components/latter_core/latter_core.h");
  const std::string pm_cpp = read_repo_file("../../components/latter_core/power_pm.cpp");

  LATTER_CHECK(pm_yaml.find("latter_ui::hal_panel_dcs_poll(&id(disp1));") != std::string::npos);
  LATTER_CHECK(axp_yaml.find("latter_ui::PanelDcsState::AWAKE") != std::string::npos);
  LATTER_CHECK(axp_yaml.find("last_screen_wake_ms()) < 300U") != std::string::npos);
  LATTER_CHECK(core_h.find("::latter_ui::pm_poll_loop(now);") != std::string::npos);
  LATTER_CHECK(pm_cpp.find("PmWakeGraceWindow grace") != std::string::npos);
  LATTER_CHECK(pm_cpp.find("s_pm_grace_end_timer") != std::string::npos);
}

void test_display_lifecycle_controller() {
  using namespace latter_ui;

  // 1. Compile-time timeout resolution tables
  static_assert(resolve_idle_screen_timeout_ms(false, true, 0U, "15s", "30s") == 15000U);
  static_assert(resolve_idle_screen_timeout_ms(false, false, 0U, "5 min", "2 min") == 120000U);
  static_assert(resolve_idle_screen_timeout_ms(false, true, 0U, "Never", "30s") == 0U);
  static_assert(resolve_idle_screen_timeout_ms(true, true, 2U, "1 min", "30s") == 0U);
  static_assert(resolve_idle_screen_timeout_ms(true, true, 0U, "1 min", "30s") == 60000U);
  static_assert(resolve_idle_screen_timeout_ms(false, false, 1U, "5 min", "30s") == 30000U);
  static_assert(resolve_battery_hibernate_timeout_ms("5 min") == 300000U);
  static_assert(resolve_battery_hibernate_timeout_ms("30 min") == 1800000U);
  static_assert(resolve_battery_hibernate_timeout_ms("Disabled") == 0U);

  // 2. Zero-UB CoreTimeoutBridge bind/schedule/cancel round-trip
  struct MockBridgeSink {
    uint32_t last_id{0U};
    uint32_t last_delay{0U};
    uint32_t cancelled_id{0U};
    int fired{0};
  } sink{};

  core_timeout_bridge().bind(
      &sink,
      +[](void *ctx, uint32_t id, uint32_t delay_ms, std::function<void()> &&fn) {
        auto *s = static_cast<MockBridgeSink *>(ctx);
        s->last_id = id;
        s->last_delay = delay_ms;
        if (fn) fn();
      },
      +[](void *ctx, uint32_t id) {
        static_cast<MockBridgeSink *>(ctx)->cancelled_id = id;
      });

  schedule_core_timeout(kTimeoutAsyncDrawId, 18U, [&]() { ++sink.fired; });
  LATTER_CHECK(sink.last_id == kTimeoutAsyncDrawId && sink.last_delay == 18U && sink.fired == 1);
  cancel_core_timeout(kTimeoutWeatherStepId);
  LATTER_CHECK(sink.cancelled_id == kTimeoutWeatherStepId);
  core_timeout_bridge().reset();

  // 3. DisplayLifecycleController: Active -> Inhibitors (2s retry) -> Standby -> Battery Hibernate -> Wake Greeting
  auto &dl = display_lifecycle();
  dl.reset();
  int nav_pwr = 0;
  uint32_t sleep_start = 0U;
  uint32_t standby_entered = 0U;
  bool wake_greeting = false;

  DisplaySupervisorInputs in{};
  in.has_battery = true;
  in.usb_connected = false;
  in.battery_timeout_opt = "30s";
  in.hibernate_timeout_opt = "5 min";
  in.panel_brightness_on = true;

  dl.note_activity(1000U);
  LATTER_CHECK(dl.state == DisplayPowerState::Active && dl.idle_armed());

  // Before 30s timeout: stays active
  auto a0 = dl.evaluate_1s(25000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  LATTER_CHECK(!a0.enter_standby_now && !a0.turn_screen_off_now && !a0.start_hibernate_countdown_now);

  // Voice turn active at 31s resets idle anchor and inhibits sleep
  in.voice_or_announce_busy = true;
  auto a_va = dl.evaluate_1s(31000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  LATTER_CHECK(!a_va.enter_standby_now && dl.idle_anchor_ms == 31000U);
  in.voice_or_announce_busy = false;

  // Game running at 62s (+31s post-VA) blocks sleep and sets 2s retry window
  in.active_game_running = true;
  auto a_game = dl.evaluate_1s(62000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  LATTER_CHECK(!a_game.enter_standby_now && dl.idle_retry_after_ms == 64000U);

  // At 63s (within 2s retry holdoff), even if game exits, waits until 64s
  in.active_game_running = false;
  auto a_hold = dl.evaluate_1s(63000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  LATTER_CHECK(!a_hold.enter_standby_now);

  // At 64s: enters standby deterministically
  auto a_sb = dl.evaluate_1s(64000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  LATTER_CHECK(a_sb.enter_standby_now && !dl.idle_armed());

  dl.on_enter_standby_off(64000U, nav_pwr, sleep_start, standby_entered);
  LATTER_CHECK(dl.state == DisplayPowerState::OffStandby && nav_pwr == 2 && screen_is_off_standby());
  LATTER_CHECK(sleep_start == 64000U && standby_entered == 64000U);
  in.panel_brightness_on = false;

  // Battery hibernate auto-descent after 5 min (300,000 ms) in screen-off standby
  auto a_pre_hib = dl.evaluate_1s(363000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  LATTER_CHECK(!a_pre_hib.start_hibernate_countdown_now);
  auto a_hib = dl.evaluate_1s(364000U, in, nav_pwr, sleep_start, standby_entered, wake_greeting);
  LATTER_CHECK(a_hib.start_hibernate_countdown_now && sleep_start == 0U);

  // Wake after >= 30 min (1,800,000 ms) in standby arms wake_greeting_pending
  in.panel_brightness_on = true;
  dl.on_wake_active(1900000U, nav_pwr, sleep_start, standby_entered, true, wake_greeting);
  LATTER_CHECK(dl.state == DisplayPowerState::Active && nav_pwr == 0 && !screen_is_off_standby());
  LATTER_CHECK(wake_greeting && standby_entered == 0U && last_screen_wake_ms() == 1900000U);

  // OTA preemption holds poller and disarms idle
  dl.on_ota_begin(nav_pwr, sleep_start);
  LATTER_CHECK(dl.state == DisplayPowerState::Ota && disp_poller_held() && !dl.idle_armed());
  dl.on_ota_end(1910000U);
  LATTER_CHECK(dl.state == DisplayPowerState::Active && !disp_poller_held() && dl.idle_armed());

  // 4. Source-level checks: screen_sleep_timer & UB downcasts completely retired
  const std::string pm_yaml = read_repo_file("../../modules/power_manager.yaml");
  const std::string comp_h = read_repo_file("../../include/ui_compositor.h");
  const std::string panel_h = read_repo_file("../../include/hal_panel_power.h");
  LATTER_CHECK(pm_yaml.find("screen_sleep_timer") == std::string::npos);
  LATTER_CHECK(comp_h.find("static_cast<DisplayUpdateScheduler") == std::string::npos);
  LATTER_CHECK(panel_h.find("PanelTimeoutScheduler") == std::string::npos);

  dl.reset();
}

// ==============================================================================
// 13. PHASE 2: AUDIO PATH ARBITRATOR, INTERCOM SESSION, ECHO METER & VOICE CMD
// ==============================================================================

void test_audio_path_and_intercom_arbitrator() {
  using namespace latter_ui;

  // 1. Compile-time + runtime zero-allocation STT local media command matcher
  static_assert(match_local_media_voice_cmd("stop") == LocalVoiceMediaCmd::PauseOrStop);
  static_assert(match_local_media_voice_cmd("  Pause!  ") == LocalVoiceMediaCmd::PauseOrStop);
  static_assert(match_local_media_voice_cmd("Turn off the music.") == LocalVoiceMediaCmd::PauseOrStop);
  static_assert(match_local_media_voice_cmd("Please stop playing song!") == LocalVoiceMediaCmd::PauseOrStop);
  static_assert(match_local_media_voice_cmd("What is the next bus stop?") == LocalVoiceMediaCmd::None);
  static_assert(match_local_media_voice_cmd("") == LocalVoiceMediaCmd::None);

  LATTER_CHECK(match_local_media_voice_cmd("PAUSE") == LocalVoiceMediaCmd::PauseOrStop);
  LATTER_CHECK(match_local_media_voice_cmd("pause playback") == LocalVoiceMediaCmd::PauseOrStop);
  LATTER_CHECK(match_local_media_voice_cmd("stop the kitchen lights") == LocalVoiceMediaCmd::None);

  // 2. Alignment-safe std::span echo_meter_feed (unaligned +1 byte slice inside aligned buffer)
  alignas(8) uint8_t raw_buf[32]{};
  std::span<uint8_t> unaligned_quiet(raw_buf + 1, 16);  // 8 int16_t samples = 0
  echo_meter_active().store(true, std::memory_order_relaxed);
  echo_last_loud_ms().store(777U, std::memory_order_relaxed);
  echo_meter_feed(std::span<const uint8_t>(unaligned_quiet));
  LATTER_CHECK(echo_last_loud_ms().load(std::memory_order_relaxed) == 777U);

  // Write loud int16_t samples (1000 > 33) at odd byte alignment via memcpy
  for (size_t i = 0; i < 8; ++i) {
    const int16_t loud = 1000;
    std::memcpy(unaligned_quiet.data() + i * sizeof(int16_t), &loud, sizeof(int16_t));
  }
  // Inactive meter must no-op
  echo_meter_active().store(false, std::memory_order_relaxed);
  echo_meter_feed(std::span<const uint8_t>(unaligned_quiet));
  LATTER_CHECK(echo_last_loud_ms().load(std::memory_order_relaxed) == 777U);
  // Active meter updates echo_last_loud_ms() to hal_millis_now() (0U on host)
  echo_meter_active().store(true, std::memory_order_relaxed);
  echo_meter_feed(std::span<const uint8_t>(unaligned_quiet));
  LATTER_CHECK(echo_last_loud_ms().load(std::memory_order_relaxed) == 0U);
  echo_meter_active().store(false, std::memory_order_relaxed);

  // 3. AudioPathArbitrator: DrainSources -> QuietSpeaker -> SettleUnmute -> Inactive
  auto &arb = audio_arbitrator();
  arb.reset();
  LATTER_CHECK(!arb.has_pending_work() && !arb.needs_fast_loop());

  arb.arm_echo_gate(1000U, 1500U);
  LATTER_CHECK(arb.echo_stage() == EchoGateStage::DrainSources && arb.needs_fast_loop());
  LATTER_CHECK(echo_meter_active().load(std::memory_order_relaxed));

  AudioArbitratorInputs in{};
  in.ann_has_buffered = true;
  auto act = arb.step(1200U, in);
  LATTER_CHECK(arb.echo_stage() == EchoGateStage::DrainSources && !act.mute_speaker_path && !act.unmute_mic);

  // Sources drained -> advances to QuietSpeaker; simulate loud acoustic tail at 1350ms
  in.ann_has_buffered = false;
  in.phys_spk_has_buffered = true;
  echo_last_loud_ms().store(1350U, std::memory_order_relaxed);
  act = arb.step(1300U, in);
  LATTER_CHECK(arb.echo_stage() == EchoGateStage::QuietSpeaker);

  // Physical speaker drained + >=100ms silence since 1350ms -> mutes speaker path & enters SettleUnmute
  in.phys_spk_has_buffered = false;
  act = arb.step(1460U, in);
  LATTER_CHECK(act.mute_speaker_path && !act.unmute_mic);
  LATTER_CHECK(arb.echo_stage() == EchoGateStage::SettleUnmute);

  // 30ms settle completes -> unmutes mic & turns off echo_meter_active
  act = arb.step(1495U, in);
  LATTER_CHECK(act.unmute_mic && arb.echo_stage() == EchoGateStage::Inactive);
  LATTER_CHECK(!echo_meter_active().load(std::memory_order_relaxed));

  // Media pause settle (500ms or mixer idle) + 3000ms idle debounce + 6000ms announce stall check
  in.media_mixer_running = true;
  arb.arm_media_pause_settle(2000U);
  LATTER_CHECK(arb.media_idle_debounce_active() && arb.needs_fast_loop());
  act = arb.step(2200U, in);
  LATTER_CHECK(!act.run_media_idle_finish);
  act = arb.step(2505U, in);
  LATTER_CHECK(act.run_media_idle_finish && !arb.media_idle_debounce_active());

  arb.arm_media_idle_debounce(3000U, 3000U);
  LATTER_CHECK(!arb.step(5999U, in).run_media_idle_finish);
  LATTER_CHECK(arb.step(6000U, in).run_media_idle_finish);

  in.mp_announcing = true;
  in.ann_has_buffered = false;
  arb.arm_announce_stall_check(10000U);
  LATTER_CHECK(!arb.step(15999U, in).log_announce_stall);
  LATTER_CHECK(arb.step(16000U, in).log_announce_stall);

  // 4. IntercomSessionController & media_seek_relative burst escalation
  struct FakeOutbox {
    std::vector<std::string> frames;
    void publish_state(const std::string &s) { frames.push_back(s); }
  } outbox;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&outbox);

  auto &ic = intercom_session();
  ic.reset();
  area_ctx_state().cameras_summary = "camera.front_door|Front Door|porch|/api/latter/cam?entity_id=camera.front_door";
  cam_carousel_idx() = 0;

  ic.start_call(20000U);
  LATTER_CHECK(ic.phase() == IntercomPhase::TwoWayListening && cam_call_active() && cam_listen_active());
  LATTER_CHECK(!outbox.frames.empty() && outbox.frames.back().find("listen_start") != std::string::npos);

  LATTER_CHECK(!ic.begin_ptt(20100U, false));
  LATTER_CHECK(cam_intercom_status() == "NOT CONNECTED");
  LATTER_CHECK(ic.begin_ptt(20200U, true));
  LATTER_CHECK(ic.phase() == IntercomPhase::TwoWayPttTx && cam_intercom_ptt_active());

  // Empty STT resumes live listen without talk_message
  LATTER_CHECK(!ic.finish_ptt_stt("", 20500U));
  LATTER_CHECK(ic.phase() == IntercomPhase::TwoWayListening && !cam_intercom_ptt_active());

  // Non-empty STT emits talk_message
  LATTER_CHECK(ic.begin_ptt(21000U, true));
  uint32_t drain_ms = 25000U;
  while (latter_rpc::has_pending_frames()) {
    drain_ms += 25U;
    latter_rpc::step_outbox_loop(drain_ms);
  }
  outbox.frames.clear();
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_rpc::bind_sensor_sink(&outbox);
  LATTER_CHECK(ic.finish_ptt_stt("Leave it on the porch", 21500U));
  LATTER_CHECK(ic.phase() == IntercomPhase::TwoWayListening && cam_intercom_status() == "SENT TO DOORBELL");
  LATTER_CHECK(!outbox.frames.empty() && outbox.frames.back().find("talk_message") != std::string::npos);

  bool hw_stopped = false;
  static bool s_hw_stopped_flag = false;
  s_hw_stopped_flag = false;
  ic.bind_host_stop_hw([](uint32_t) { s_hw_stopped_flag = true; });
  ic.stop_call_with_hw(22000U);
  hw_stopped = s_hw_stopped_flag;
  LATTER_CHECK(hw_stopped && ic.phase() == IntercomPhase::Idle && !cam_call_active());
  LATTER_CHECK(arb.media_idle_debounce_active());
  arb.reset();
  ic.bind_host_stop_hw(nullptr);
  ic.reset();

  // Burst seek escalation: +30s on 1st tap, then +60s within 1000ms burst window
  runtime_state().music = MusicRuntimeState{};
  media_state().duration = 300.0f;
  runtime_state().music.pos_base = 50.0f;
  runtime_state().music.pos_sync_time = 30000U;
  media_seek_relative(+1, 30000U);
  LATTER_CHECK(std::fabs(runtime_state().music.pos_base - 80.0f) < 0.05f);
  media_seek_relative(+1, 30500U);
  LATTER_CHECK(std::fabs(runtime_state().music.pos_base - 140.5f) < 0.05f);

  // 5. Source-level check: all retired mode: restart YAML scripts removed from audio_voice.yaml
  const std::string av_yaml = read_repo_file("../../modules/audio_voice.yaml");
  LATTER_CHECK(av_yaml.find("id: va_echo_gate") == std::string::npos);
  LATTER_CHECK(av_yaml.find("id: mp_idle_debounce") == std::string::npos);
  LATTER_CHECK(av_yaml.find("id: mp_pause_settle") == std::string::npos);
  LATTER_CHECK(av_yaml.find("id: announce_stall_check") == std::string::npos);
  LATTER_CHECK(av_yaml.find("id: send_doorbell_intercom_cmd") == std::string::npos);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
}

// ==============================================================================
// 14. PHASE 3 MODERNIZATION: FRAMECADENCECONTROLLER, STRONG ENUMS & CONSTEXPR PREFS
// ==============================================================================
void test_frame_cadence_and_typed_prefs() {
  using namespace latter_ui;

  // 1. Compile-time constexpr typed PersistedDevicePrefs verification (zero NVS v4 size overhead)
  constexpr PersistedDevicePrefs kDefPrefs{};
  static_assert(std::is_trivially_copyable_v<PersistedDevicePrefs>,
                "PersistedDevicePrefs must stay POD");
  static_assert(kDefPrefs.wake_word_mode_enum() == WakeWordMode::DockedOnly);
  static_assert(kDefPrefs.tap_to_wake_enum() == TapWakeMode::DockedOnly);
  static_assert(kDefPrefs.screensaver_style_enum() == ScreensaverStyle::AmbientClock);
  static_assert(kDefPrefs.auto_brightness_enum() == AutoBrightnessMode::SunSynchronized);
  static_assert(kDefPrefs.color_profile_enum() == ColorProfilePref::SteelBlue);
  static_assert(kDefPrefs.va_style() == VaVisualStyle::Nebula);
  static_assert(kDefPrefs.clock_face_style() == ClockFaceStyle::MinimalDigital);
  static_assert(kDefPrefs.battery_screen_timeout_ms() == 30000U);
  static_assert(kDefPrefs.docked_idle_timeout_ms() == 60000U);
  static_assert(kDefPrefs.screen_timeout_ms(false) == 30000U);
  static_assert(kDefPrefs.screen_timeout_ms(true) == 60000U);
  static_assert(kDefPrefs.hibernate_delay_s() == 900U);
  static_assert(kDefPrefs.manual_brightness_pct() == 80U);
  static_assert(kDefPrefs.day_brightness_pct() == 90U);
  static_assert(kDefPrefs.night_brightness_pct() == 35U);
  static_assert(kDefPrefs.bedtime_brightness_pct() == 15U);
  static_assert(kDefPrefs.return_to_home_ms() == 120000U);

  static_assert(parse_media_playback_state("playing") == MediaPlaybackState::Playing);
  static_assert(parse_media_playback_state("paused") == MediaPlaybackState::Paused);
  static_assert(parse_media_playback_state("idle") == MediaPlaybackState::Idle);
  static_assert(parse_alert_priority("critical") == AlertPriority::Critical);
  static_assert(parse_alert_priority("normal") == AlertPriority::Normal);

  // Out-of-range uint8_t clamping fallback on PersistedDevicePrefs
  PersistedDevicePrefs corrupt = kDefPrefs;
  corrupt.battery_screen_timeout = 99U;
  corrupt.docked_idle_timeout = 99U;
  corrupt.battery_hibernate_timeout = 99U;
  corrupt.return_to_home_timeout = 99U;
  LATTER_CHECK(corrupt.battery_screen_timeout_ms() == 30000U);
  LATTER_CHECK(corrupt.docked_idle_timeout_ms() == 60000U);
  LATTER_CHECK(corrupt.hibernate_delay_s() == 900U);
  LATTER_CHECK(corrupt.return_to_home_ms() == 120000U);

  // 2. S3 vs P4 interactive & ambient frame cadence + post-transfer quiet windows
  static_assert(FrameCadenceController::interactive_gap_for(true, true) == 36U);
  static_assert(FrameCadenceController::interactive_gap_for(false, false) == 36U);
  static_assert(FrameCadenceController::interactive_gap_for(false, true) == 54U);

  // S3 with Octal-PSRAM wake word running requires 74ms period & 32ms post-transfer idle
  LATTER_CHECK(!FrameCadenceController::ambient_step_ready(
      1060U, 1000U, 1020U, false, false, false, false, false, true));
  LATTER_CHECK(FrameCadenceController::ambient_step_ready(
      1080U, 1000U, 1020U, false, false, false, false, false, true));
  // P4 with SRAM wake word steps at 54ms period & 16ms post-transfer idle
  LATTER_CHECK(FrameCadenceController::ambient_step_ready(
      1060U, 1000U, 1020U, false, true, false, false, false, true));

  // 3. FrameCadenceController 20ms VA speech transition 1-tick holdoff & 2-idle-tick self-stop
  FrameCadenceController fc;
  fc.reset();

  Cadence20msInputs in20{};
  in20.now_ms = 2000U;
  in20.panel_on = true;
  in20.display_power_state = 0;
  in20.va_visible = true;
  in20.va_running = true;
  in20.tts_received = true;  // First tick where TTS starts!
  auto act20 = fc.evaluate_20ms(in20);
  LATTER_CHECK(act20.va_transition_holdoff);
  LATTER_CHECK(!act20.schedule_anim_full);
  LATTER_CHECK(act20.sync_disp_poller && act20.sync_disp_poller_val);

  // 100ms later during TTS: schedules VA frame (gap >= 94ms)
  in20.now_ms = 2100U;
  act20 = fc.evaluate_20ms(in20);
  LATTER_CHECK(!act20.va_transition_holdoff);
  LATTER_CHECK(act20.schedule_anim_full);

  // VA ends and nothing animating -> self-stops after 2 quiet 20ms ticks
  in20 = Cadence20msInputs{};
  in20.now_ms = 2120U;
  act20 = fc.evaluate_20ms(in20);
  LATTER_CHECK(!act20.stop_anim_poller);
  in20.now_ms = 2140U;
  act20 = fc.evaluate_20ms(in20);
  LATTER_CHECK(act20.stop_anim_poller);

  // 4. FrameCadenceController 250ms Home slot rotation, wallpaper wake fetch & FW version diff
  Cadence250msInputs in250{};
  in250.now_ms = 8000U;
  in250.display_power_state = 0;
  in250.gui_page = 1;
  in250.home_subpage = 0;
  in250.activity_mask = 0x02U;
  in250.rotate_period_ms = 8000U;
  in250.pending_wallpaper_fetch = true;
  auto act250 = fc.evaluate_250ms(in250);
  LATTER_CHECK(act250.ensure_anim_poller);
  LATTER_CHECK(act250.schedule_disp_now);
  LATTER_CHECK(act250.fetch_wallpaper);

  in250.gui_page = 4;
  in250.system_menu_mode = 5;
  in250.latest_fw_version = "3.0.1";
  act250 = fc.evaluate_250ms(in250);
  LATTER_CHECK(act250.fw_version_changed);
  act250 = fc.evaluate_250ms(in250);
  LATTER_CHECK(!act250.fw_version_changed);

  // 5. Source check: YAML function-pointer tables retired from system_core.yaml
  const std::string sys_yaml = read_repo_file("../../modules/system_core.yaml");
  LATTER_CHECK(sys_yaml.find("rt.get_select") == std::string::npos);
  LATTER_CHECK(sys_yaml.find("rt.set_select") == std::string::npos);
  LATTER_CHECK(sys_yaml.find("rt.get_switch") == std::string::npos);
  LATTER_CHECK(sys_yaml.find("rt.toggle_switch") == std::string::npos);
  LATTER_CHECK(sys_yaml.find("rt.get_number") == std::string::npos);
  LATTER_CHECK(sys_yaml.find("rt.set_number") == std::string::npos);
}

void test_v3_domain_reducer_and_timer_alarm() {
  using namespace latter_ui;

  reset_runtime_state();

  // 1. Zero-copy <charconv> number parsers: whitespace, '+', non-null-terminated slices & overflow
  const char raw_packed[] = "123456789";  // Slice "123" is NOT null-terminated at index 3!
  const std::string_view slice_123(raw_packed, 3);
  LATTER_CHECK(v3_parse_int(slice_123, -1) == 123);
  LATTER_CHECK(v3_parse_u32(slice_123, 0U) == 123U);
  LATTER_CHECK(std::fabs(v3_parse_float(slice_123, -1.0f) - 123.0f) < 0.001f);

  LATTER_CHECK(v3_parse_int("  -42 ", 0) == -42);
  LATTER_CHECK(v3_parse_int("\t+85%", 0) == 85);
  LATTER_CHECK(v3_parse_int("+-5", 99) == 99);
  LATTER_CHECK(v3_parse_int("99999999999999999999", -7) == -7);  // Overflow returns fallback
  LATTER_CHECK(v3_parse_u32(" -1", 55U) == 55U);                  // Negative rejected for u32
  LATTER_CHECK(v3_parse_u32(" +2026111306", 0U) == 2026111306U);
  LATTER_CHECK(v3_parse_u32("99999999999999999999", 77U) == 77U);
  LATTER_CHECK(std::fabs(v3_parse_float("  +72.5", 0.0f) - 72.5f) < 0.001f);
  LATTER_CHECK(std::fabs(v3_parse_float("-14.25", 0.0f) - (-14.25f)) < 0.001f);
  LATTER_CHECK(std::isnan(v3_parse_float("invalid")));

  // 2. V3DomainReducer: area_context, active_media, remote_config, weather_forecast
  auto &rt = runtime_state();
  auto &prefs = persisted_prefs();
  prefs.color_profile = 4;  // Custom

  auto &ac = area_ctx_state();
  ac.epoch = 1U;
  ac.rev = 1U;
  ac.area_id = "kitchen";
  ac.area_name = "Kitchen";
  ac.area_icon = "kitchen";
  ac.site_id = "oakland";
  ac.ble_closest_proxy = "proxy_kitchen";
  ac.available_domains = "light,climate";
  ac.wallpaper_url = "/api/latter/wp.jpg";
  ac.alert_id = 0;  // Seed initial alert_id
  rt.nav.gui_page = 6;
  rt.nav.area_subpage = 1;  // Unassigned picker should snap to entities overview on assignment!

  V3ReduceContext rctx{};
  rctx.now_ms = 5000U;
  rctx.friendly_name = "Kitchen Display";
  auto eff = v3_domain_reducer().reduce("area_context", rctx);
  LATTER_CHECK(eff.area_changed);
  LATTER_CHECK(eff.mark_dirty_full);
  LATTER_CHECK(eff.fetch_area_browse);
  LATTER_CHECK(!eff.apply_theme_preset);
  LATTER_CHECK(eff.fetch_wallpaper);
  LATTER_CHECK(!eff.alert_triggered);
  LATTER_CHECK(rt.area.active_area_id == "kitchen");
  LATTER_CHECK(rt.nav.area_subpage == 0);
  LATTER_CHECK(rt.area.domain_filter == "light");
  LATTER_CHECK(latter_site_id() == "oakland");

  // Critical alert push on subsequent rev triggers banner + critical chime SFX 9
  ac.alert_id = 1;
  ac.alert_title = "Smoke Detected";
  ac.alert_message = "Check kitchen";
  ac.alert_priority = "critical";
  ac.alert_chime = true;
  ac.wallpaper_url.clear();
  rt.music.loaded_wallpaper_url = "/api/latter/wp.jpg";
  eff = v3_domain_reducer().reduce("area_context", rctx);
  LATTER_CHECK(eff.alert_triggered);
  LATTER_CHECK(eff.alert_sfx_id == 9);
  LATTER_CHECK(rt.alert.is_critical());
  LATTER_CHECK(eff.stop_wallpaper_fetch && eff.release_wallpaper_art);

  // active_media: Title-Case entity fallback, 3s local volume lockout, and diffing
  rt.nav.gui_page = 2;
  auto &ms = media_state();
  ms.state = "playing";
  ms.title = "So What";
  ms.artist = "Miles Davis";
  ms.active_speaker = "media_player.living_room_sonos";
  ms.speaker_name.clear();
  ms.is_local = false;
  ms.has_volume = true;
  ms.volume = 0.65f;
  ms.art_url = "/api/latter/art1.jpg";
  eff = v3_domain_reducer().reduce("active_media", rctx);
  LATTER_CHECK(rt.music.active_speaker_name == "Living Room Sonos");
  LATTER_CHECK(remote_vol_pct() == 65);
  LATTER_CHECK(eff.fetch_remote_art);
  LATTER_CHECK(eff.throttled_disp_update);

  // Local tap locks out remote vol overwrite; once synced, an identical push produces no redundant redraw
  rt.music.loaded_art_url = ms.art_url;
  last_local_vol_tap_ms() = 5200U;
  remote_vol_pct() = 80;
  ms.volume = 0.40f;
  ms.position = 0.5f;
  rctx.now_ms = 5500U;
  eff = v3_domain_reducer().reduce("active_media", rctx);
  LATTER_CHECK(remote_vol_pct() == 80);  // Local 3s tap lockout preserved 80%

  ms.position = 1.0f;  // Matches 0.5s elapsed interpolation (no jump)
  rctx.now_ms = 6000U;
  eff = v3_domain_reducer().reduce("active_media", rctx);
  LATTER_CHECK(!eff.throttled_disp_update);

  // Position jump > 2.5s triggers throttled_disp_update
  ms.position = 45.0f;
  eff = v3_domain_reducer().reduce("active_media", rctx);
  LATTER_CHECK(eff.throttled_disp_update);

  // weather_forecast sun transition triggers brightness sync once
  weather_state().sun_state = "below_horizon";
  eff = v3_domain_reducer().reduce("weather_forecast", rctx);
  LATTER_CHECK(eff.sync_display_brightness && eff.weather_sim_dirty);
  eff = v3_domain_reducer().reduce("weather_forecast", rctx);
  LATTER_CHECK(!eff.sync_display_brightness);

  // 3. TimerAlarmController: multi-timer expiry (preserving duration_sec), VA timers, alarms & 5-min auto-silence
  reset_runtime_state();
  auto &tc = timer_alarm_controller();

  // Start VA timer -> updates slot & clears ringing
  auto va_eff = tc.on_va_timer_started("t1", "Pasta", 180, 2, true, 1000U);
  LATTER_CHECK(rt.timers.running);
  LATTER_CHECK(timer_slots()[0].duration_sec == 180);
  LATTER_CHECK(va_eff.schedule_disp);

  // Tick past expiration -> fires alert AND preserves duration_sec=180 for 1-tap reuse!
  auto t_eff = tc.tick_timers_250ms(3100U);
  LATTER_CHECK(t_eff.any_expired);
  LATTER_CHECK(rt.timers.ringing);
  LATTER_CHECK(!timer_slots()[0].running);
  LATTER_CHECK(timer_slots()[0].remaining_sec == 180);

  // Unattended timer ringing auto-silences at 5 minutes (300,000 ms)
  t_eff = tc.tick_timers_250ms(303099U);
  LATTER_CHECK(!t_eff.auto_silenced && rt.timers.ringing);
  t_eff = tc.tick_timers_250ms(303100U);
  LATTER_CHECK(t_eff.auto_silenced && !rt.timers.ringing);

  // Scheduled "Once" alarm (recurrence=3) fires, auto-disables, snoozes 5 min, and re-fires
  prefs.alarms[0].enabled = true;
  prefs.alarms[0].hour = 7;
  prefs.alarms[0].minute = 30;
  prefs.alarms[0].recurrence = 3;  // Once
  auto a_eff = tc.tick_alarms_1s(true, 1700004600U, 7, 30, 5, 2 /*Mon*/, 10000U);
  LATTER_CHECK(a_eff.triggered_idx == 1);
  LATTER_CHECK(a_eff.prefs_saved);
  LATTER_CHECK(!prefs.alarms[0].enabled);
  LATTER_CHECK(rt.alarms.ringing);

  // Crown snooze arms +300s epoch and stops ringing
  LATTER_CHECK(tc.snooze_active_alarm(true, 1700004600U, 12000U));
  LATTER_CHECK(!rt.alarms.ringing);
  LATTER_CHECK(rt.alarms.snooze_until == 1700004900U);

  // Snooze expires at +300s -> re-fires alarm, then auto-silences after 5 min unattended
  a_eff = tc.tick_alarms_1s(true, 1700004900U, 7, 35, 0, 2, 310000U);
  LATTER_CHECK(a_eff.snooze_fired && rt.alarms.ringing);
  a_eff = tc.tick_alarms_1s(true, 1700005200U, 7, 40, 0, 2, 610000U);
  LATTER_CHECK(a_eff.auto_silenced && !rt.alarms.ringing);

  // 4. reset_runtime_state() resets all consolidated singletons cleanly
  remote_vol_pct() = 12;
  latter_site_id() = "mutated_site";
  home_dock_page() = 3;
  reset_runtime_state();
  LATTER_CHECK(remote_vol_pct() == 70);
  LATTER_CHECK(latter_site_id() == "primary");
  LATTER_CHECK(home_dock_page() == 0);
}

void test_time_app_world_clocks_alarms_and_horology() {
  using namespace latter_ui;

  reset_runtime_state();
  auto &p = persisted_prefs();
  p = PersistedDevicePrefs{};

  // 1. Out-of-the-box defaults: Home Time Tap -> "Time App", Show UTC Clock -> false, Clock Faces renamed
  LATTER_CHECK(std::string_view(pref_home_clock_tap_cstr(p)) == "Time App");
  LATTER_CHECK(!p.show_utc_clock);
  LATTER_CHECK(std::string_view(p.local_clock_label).empty());
  LATTER_CHECK(std::string_view(p.world_clocks_spec).empty());
  LATTER_CHECK(std::string_view(kClockFaceOpts[0]) == "Minimal Digital");
  LATTER_CHECK(std::string_view(kClockFaceOpts[1]) == "Modern Analog");
  LATTER_CHECK(std::string_view(kClockFaceOpts[2]) == "Hybrid Digital");

  // Backdrop mode: PageId::TIME (10) uses CustomWallpaper; PageId::CLOCK (0) uses pure SolidBlack
  LATTER_CHECK(resolve_backdrop_mode(10, 0, true, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(0, 0, true, true, false, true) == BackdropMode::SolidBlack);

  // 2. Local Clock Label sanitization, World Clocks '<label>|<iana>;...' spec, active collector & day-delta wall math
  LATTER_CHECK(sanitize_clock_label("  San  Francisco | Bay ; Home  ") == "San Francisco Bay Home");
  LATTER_CHECK(sanitize_clock_label("1234567890123456789012345678901\xC3\xA9") ==
               "1234567890123456789012345678901");  // Truncates cleanly at UTF-8 codepoint boundary <= 32 bytes

  LATTER_CHECK(normalize_world_clocks_spec(
                   " Kyoto | Asia/Tokyo ; UTC|UTC ; |Europe/London ; kyoto|Asia/Tokyo ; "
                   "Bengaluru|Asia/Kolkata ; Hanoi|Asia/Bangkok ; Bangkok|Asia/Bangkok ")
               == "Kyoto|Asia/Tokyo;London|Europe/London;Bengaluru|Asia/Kolkata;Hanoi|Asia/Bangkok");

  const LocalTzInfo tz = parse_local_tz_info("America/Los_Angeles|Oakland|PDT|-420|1");
  LATTER_CHECK(tz.valid && tz.iana == "America/Los_Angeles" && tz.city == "Oakland");
  LATTER_CHECK(tz.abbr == "PDT" && tz.utc_offset_min == -420 && tz.is_dst);

  PrefMask chg{};
  uint32_t eff = 0U;
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::LOCAL_CLOCK_LABEL, "  Oakland Studio  ", false, chg, eff));
  LATTER_CHECK(std::string_view(p.local_clock_label) == "Oakland Studio");
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::LOCAL_CLOCK_LABEL, "", false, chg, eff));
  LATTER_CHECK(std::string_view(p.local_clock_label).empty());
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::SHOW_UTC_CLOCK, "1", false, chg, eff));
  LATTER_CHECK(set_pref_by_key_str(
      p, PrefKey::WORLD_CLOCKS,
      "Kyoto|Asia/Tokyo;Bengaluru|Asia/Kolkata;Hanoi|Asia/Bangkok;Bangkok|Asia/Bangkok",
      false, chg, eff));
  auto &ac = area_ctx_state();
  ac = AreaContextState{};
  ac.world_clocks_summary =
      "Asia/Tokyo|Kyoto|JST|540|0;Asia/Bangkok|Hanoi|ICT|420|0;Asia/Bangkok|Bangkok|ICT|420|0";

  WorldClockEntry entries[kMaxWorldClocks + 1]{};
  const int wc_cnt = collect_active_world_clocks(p, ac, entries);
  LATTER_CHECK(wc_cnt == 5);
  LATTER_CHECK(entries[0].is_pinned_utc && entries[0].city == "UTC" && entries[0].utc_offset_min == 0);
  LATTER_CHECK(entries[1].iana == "Asia/Tokyo" && entries[1].city == "Kyoto" &&
               entries[1].abbr == "JST" && entries[1].utc_offset_min == 540);
  // Offline fallback for Asia/Kolkata (not in ac.world_clocks_summary) preserves custom city label "Bengaluru" +330
  LATTER_CHECK(entries[2].iana == "Asia/Kolkata" && entries[2].city == "Bengaluru" && entries[2].utc_offset_min == 330);
  LATTER_CHECK(entries[2].abbr == "UTC+5:30");
  // Two distinct custom cities sharing Asia/Bangkok preserve independent labels
  LATTER_CHECK(entries[3].iana == "Asia/Bangkok" && entries[3].city == "Hanoi" && entries[3].abbr == "ICT");
  LATTER_CHECK(entries[4].iana == "Asia/Bangkok" && entries[4].city == "Bangkok" && entries[4].abbr == "ICT");
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::WORLD_CLOCKS, "", false, chg, eff));
  LATTER_CHECK(std::string_view(p.world_clocks_spec).empty());

  // Wall-clock day delta: 2026-07-15 03:30:00 UTC (Oakland PDT -420 is 20:30 Tue; Tokyo +540 is 12:30 Wed => +1d)
  const uint32_t sample_epoch = 1784086200U;  // 03:30:00 UTC (divisible by 60)
  const int utc_sec_of_day = static_cast<int>(sample_epoch % 86400U);
  int h24 = 0, mm = 0, day_delta = 0;
  compute_world_clock_wall_time(sample_epoch, -420, 540, h24, mm, day_delta);
  const int expected_tokyo_min = ((utc_sec_of_day / 60) + 540 + 1440) % 1440;
  LATTER_CHECK(h24 == expected_tokyo_min / 60 && mm == expected_tokyo_min % 60);
  // Test explicit cross-midnight +1d and -1d boundaries (at 02:00 UTC: -420 is prev day 19:00, +540 is same UTC day 11:00 -> +1d)
  compute_world_clock_wall_time(86400U * 10U + 2U * 3600U, -420, 540, h24, mm, day_delta);
  LATTER_CHECK(h24 == 11 && mm == 0 && day_delta == 1);
  // At 22:00 UTC: +540 is next UTC day 07:00, -420 is same UTC day 15:00 -> Honolulu (-600) vs Tokyo (+540): Honolulu is 12:00 (-1d relative to Tokyo)
  compute_world_clock_wall_time(86400U * 10U + 22U * 3600U, 540, -600, h24, mm, day_delta);
  LATTER_CHECK(h24 == 12 && mm == 0 && day_delta == -1);
  // Half-hour zone Asia/Kolkata (+330) at 02:15 UTC -> 07:45
  compute_world_clock_wall_time(86400U * 10U + 2U * 3600U + 15U * 60U, 0, 330, h24, mm, day_delta);
  LATTER_CHECK(h24 == 7 && mm == 45 && day_delta == 0);

  // 3. Alarms 2-way summary codec (0=Daily, 1=Weekdays, 2=Weekends, 3=Once) + outbound sync_local_alarms_edit
  chg = 0ULL;
  eff = 0U;
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::ALARMS_SUMMARY, "06:47|1|1;08:15|0|2;21:05|1|3", false, chg, eff));
  LATTER_CHECK(p.alarms[0].hour == 6 && p.alarms[0].minute == 47 && p.alarms[0].enabled && p.alarms[0].recurrence == 1);
  LATTER_CHECK(p.alarms[1].hour == 8 && p.alarms[1].minute == 15 && !p.alarms[1].enabled && p.alarms[1].recurrence == 2);
  LATTER_CHECK(p.alarms[2].hour == 21 && p.alarms[2].minute == 5 && p.alarms[2].enabled && p.alarms[2].recurrence == 3);
  LATTER_CHECK(format_alarms_summary(p) == "06:47|1|1;08:15|0|2;21:05|1|3");
  LATTER_CHECK(!set_pref_by_key_str(p, PrefKey::ALARMS_SUMMARY, "25:00|1|0;08:00|0|0;09:00|0|0", false, chg, eff));

  MockRpcOutbox alarm_outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  link_state().ha_connected = true;
  p.prefs_seq = 12U;
  p.alarms[0].minute = 48;
  latter_actions::sync_local_alarms_edit(5000U, &alarm_outbox);
  LATTER_CHECK(!alarm_outbox.published.empty());
  LATTER_CHECK(alarm_outbox.published.back().find("alarms_summary=06:48|1|1;08:15|0|2;21:05|1|3") != std::string::npos);

  // 4. Multi-lap Digital Stopwatch split history & started_in_analog provenance
  StopwatchRuntimeState sw{};
  sw.running = true;
  sw.started_in_analog = false;
  sw.start_time_ms = 1000U;
  sw.record_lap_elapsed(12450U);  // Lap 1 split: 12,450 ms
  LATTER_CHECK(sw.lap_count == 1 && sw.lap_splits_ms[0] == 12450U && sw.lap_ms == 12450U);
  sw.record_lap_elapsed(27900U);  // Total 27,900 ms -> Lap 2 split: 15,450 ms (ring [1])
  LATTER_CHECK(sw.lap_count == 2 && sw.lap_splits_ms[0] == 12450U && sw.lap_splits_ms[1] == 15450U);
  sw.reset();
  LATTER_CHECK(!sw.running && sw.lap_count == 0 && sw.elapsed_ms == 0U && !sw.started_in_analog);

  // 5. Drilldown-aware compute_touch_context & 1 Hz Alarm Editor focus blink
  DeviceRuntimeState &rs_ctx = runtime_state();
  rs_ctx.nav.gui_page = 10;  // PageId::TIME
  rs_ctx.nav.time_subpage = 0;
  const int ctx_time_clock = compute_touch_context(rs_ctx);

  rs_ctx.nav.time_subpage = 1;
  rs_ctx.timers.view_slot = 0;
  const int ctx_timers_root = compute_touch_context(rs_ctx);
  LATTER_CHECK(ctx_timers_root != ctx_time_clock);
  rs_ctx.timers.view_slot = 1;
  LATTER_CHECK(compute_touch_context(rs_ctx) != ctx_timers_root);
  rs_ctx.timers.view_slot = 0;

  rs_ctx.nav.time_subpage = 2;
  rs_ctx.alarms.edit_idx = 0;
  const int ctx_alarms_root = compute_touch_context(rs_ctx);
  rs_ctx.alarms.edit_idx = 2;
  LATTER_CHECK(compute_touch_context(rs_ctx) != ctx_alarms_root);
  rs_ctx.alarms.edit_idx = 0;

  rs_ctx.nav.gui_page = PageId::CAMERAS;
  rs_ctx.cameras.tab = CamerasTab::SINGLE;
  rs_ctx.cameras.hud_active = true;
  rs_ctx.cameras.quick_reply_open = false;
  const int ctx_cam_root = compute_touch_context(rs_ctx);
  rs_ctx.cameras.quick_reply_open = true;
  LATTER_CHECK(compute_touch_context(rs_ctx) != ctx_cam_root);
  rs_ctx.cameras.quick_reply_open = false;
  rs_ctx.cameras.hud_active = false;
  LATTER_CHECK(compute_touch_context(rs_ctx) != ctx_cam_root);
  rs_ctx.cameras.hud_active = true;

  rs_ctx.nav.gui_page = PageId::CALENDAR;
  rs_ctx.cal.subpage = CalendarSubpage::OVERVIEW;
  rs_ctx.cal.drilldown = CalDrilldown::NONE;
  rs_ctx.cal.delete_confirm_open = false;
  const int ctx_cal_root = compute_touch_context(rs_ctx);
  rs_ctx.cal.drilldown = CalDrilldown::DETAIL;
  LATTER_CHECK(compute_touch_context(rs_ctx) != ctx_cal_root);
  rs_ctx.cal.drilldown = CalDrilldown::NONE;
  rs_ctx.cal.drilldown = CalDrilldown::ADD_STEP1;
  LATTER_CHECK(compute_touch_context(rs_ctx) != ctx_cal_root);
  rs_ctx.cal.drilldown = CalDrilldown::NONE;

  LATTER_CHECK(!alarm_editor_blink_dim(1000U, 1000U));
  LATTER_CHECK(!alarm_editor_blink_dim(1499U, 1000U));
  LATTER_CHECK(alarm_editor_blink_dim(1500U, 1000U));
  LATTER_CHECK(alarm_editor_blink_dim(1999U, 1000U));
  LATTER_CHECK(!alarm_editor_blink_dim(2000U, 1000U));
  LATTER_CHECK(alarm_editor_blink_dim(2500U, 1000U));

  // 6. Source-level guards for Time App, mode-bar-exclusive headers, swipe-up-only Home, and SDF AA Clock Faces
  const std::string time_cpp = read_repo_file("../../components/latter_ui/page_time.cpp");
  const std::string weather_cpp = read_repo_file("../../components/latter_ui/page_weather.cpp");
  const std::string tools_cpp = read_repo_file("../../components/latter_ui/page_tools.cpp");
  const std::string clock_cpp = read_repo_file("../../components/latter_ui/page_clock_home.cpp");
  const std::string widgets_cpp = read_repo_file("../../components/latter_ui/ui_widgets.cpp");
  const std::string touch_cpp = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  const std::string layout_h = read_repo_file("../../include/ui_layout.h");
  const std::string hit_h = read_repo_file("../../include/ui_hit_table.h");
  const std::string timers_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");
  LATTER_CHECK(time_cpp.find("render_time_page(") != std::string::npos);
  LATTER_CHECK(time_cpp.find("split_action_card_row(") != std::string::npos);
  LATTER_CHECK(time_cpp.find("local_clock_label") != std::string::npos);
  LATTER_CHECK(time_cpp.find("text_ink_right_edge(it, sec_font, \":00\")") != std::string::npos);
  LATTER_CHECK(time_cpp.find("scale_y(has_label ? 172 : 160)") != std::string::npos);
  LATTER_CHECK(time_cpp.find("scale_y(164)") != std::string::npos);
  LATTER_CHECK(time_cpp.find("scale_y(150)") != std::string::npos);
  LATTER_CHECK(time_cpp.find("std::min(2, std::min(total_laps") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("scale_y(102), latter_ui::scale_y(200)") != std::string::npos);
  LATTER_CHECK(time_cpp.find("alarm_editor_blink_dim(") != std::string::npos);
  LATTER_CHECK(time_cpp.find("TIME_ADD_ZONE_BTN") == std::string::npos);
  LATTER_CHECK(time_cpp.find("TIME_WORLD_DEL_BASE") == std::string::npos);
  LATTER_CHECK(time_cpp.find("TIME_MODE_BTN") == std::string::npos);
  LATTER_CHECK(weather_cpp.find("WEATHER_MODE_BTN") == std::string::npos);
  LATTER_CHECK(time_cpp.find("TIME_UTC_TOGGLE_BTN") == std::string::npos);
  LATTER_CHECK(time_cpp.find("Tap LAP while running") == std::string::npos);
  LATTER_CHECK(widgets_cpp.find("frame_hits().add_rect(x0, y0, split_w, h") != std::string::npos);
  LATTER_CHECK(widgets_cpp.find("frame_hits().add_rect(div_x, y0, right_w, h") != std::string::npos);
  LATTER_CHECK(layout_h.find("is_home_pill_tap") == std::string::npos);
  LATTER_CHECK(hit_h.find("HOME_BAR") == std::string::npos);
  LATTER_CHECK(hit_h.find("TIME_MODE_BTN") == std::string::npos);
  LATTER_CHECK(hit_h.find("WEATHER_MODE_BTN") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("is_home_pill_tap") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("HOME_BAR") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("TIME_MODE_BTN") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("WEATHER_MODE_BTN") == std::string::npos);
  LATTER_CHECK(tools_cpp.find("stopwatch_modern_digital") == std::string::npos);
  LATTER_CHECK(tools_cpp.find("SW_STYLE_BTN") == std::string::npos);
  LATTER_CHECK(tools_cpp.find("ctx.col_slate, false);") != std::string::npos);
  LATTER_CHECK(tools_cpp.find("ui::draw_aa_disc(") != std::string::npos);
  LATTER_CHECK(tools_cpp.find("ui::draw_aa_ring(") != std::string::npos);
  LATTER_CHECK(clock_cpp.find("draw_aa_tapered_hand(") != std::string::npos);
  LATTER_CHECK(clock_cpp.find("render_analog_chapter_ring(") != std::string::npos);
}

void test_home_activity_card_d045() {
  using namespace latter_ui;

  // 1. Recurrence-aware next_alarm_minutes_until (0=Daily, 1=Weekdays, 2=Weekends, 3=Once; 1=SUN..7=SAT)
  LATTER_CHECK(next_alarm_minutes_until(7, 0, false, 0, 6, 0, 2) == -1);
  LATTER_CHECK(next_alarm_minutes_until(24, 0, true, 0, 6, 0, 2) == -1);
  LATTER_CHECK(next_alarm_minutes_until(7, 0, true, 0, -1, 0, 2) == -1);

  // Daily / Once same-day & cross-midnight windows
  LATTER_CHECK(next_alarm_minutes_until(23, 30, true, 0, 22, 0, 3) == 90);    // <= 120m pin window
  LATTER_CHECK(next_alarm_minutes_until(3, 0, true, 3, 22, 0, 3) == 300);     // 121..360m deck window
  LATTER_CHECK(next_alarm_minutes_until(15, 0, true, 0, 8, 0, 3) == 420);     // > 360m off-deck
  LATTER_CHECK(next_alarm_minutes_until(8, 0, true, 0, 8, 0, 3) == 1440);     // exact cur minute -> next occurrence

  // Weekdays (2=MON..6=FRI): Friday night skips Sat/Sun; Sunday late night enters Monday window
  LATTER_CHECK(next_alarm_minutes_until(6, 30, true, 1, 23, 0, 6) == 3330);   // Fri 23:00 -> Mon 06:30 (> 6h)
  LATTER_CHECK(next_alarm_minutes_until(4, 0, true, 1, 23, 0, 1) == 300);     // Sun 23:00 -> Mon 04:00 (5h -> in deck!)
  LATTER_CHECK(next_alarm_minutes_until(6, 30, true, 1, 5, 0, 2) == 90);      // Mon 05:00 -> Mon 06:30 (90m -> pinned!)

  // Weekends (1=SUN, 7=SAT): Friday night enters Saturday morning window; Sunday night skips Mon..Fri
  LATTER_CHECK(next_alarm_minutes_until(4, 0, true, 2, 23, 0, 6) == 300);     // Fri 23:00 -> Sat 04:00 (5h -> in deck!)
  LATTER_CHECK(next_alarm_minutes_until(8, 0, true, 2, 23, 0, 1) == 7740);    // Sun 23:00 -> Sat 08:00 (> 6h)

  // 2. Off-deck items (> 6h alarms / future calendar) never open the front card or inflate height
  {
    const HomeActivityItem off_only[2] = {
        {4, 1, false, false, false, "Alarm • 7:00 AM"},
        {5, 0, false, false, false, "FRI 10:00 • Dentist"},
    };
    const HomeActivityView vr = resolve_home_activity_view(off_only, 2, 0U, false);
    const HomeActivityView vn = resolve_home_activity_view(off_only, 2, 0U, true);
    LATTER_CHECK(vr.front_count == 0 && vr.visible_rows == 0 && vr.top == -1 && vr.bottom == -1);
    LATTER_CHECK(vn.front_count == 0 && vn.visible_rows == 0 && vn.top == -1 && vn.bottom == -1);
  }

  // 3. Single front-deck item + off-deck alarm -> 1-line card (visible_rows == 1, front_count == 1)
  {
    const HomeActivityItem one_plus_off[2] = {
        {3, 0, true, false, true, "Artist • Track"},
        {4, 1, false, false, false, "Alarm • 8:00 AM"},
    };
    const HomeActivityView v = resolve_home_activity_view(one_plus_off, 2, 0U, true);
    LATTER_CHECK(v.front_count == 1 && v.visible_rows == 1 && v.top == 0 && v.bottom == -1 && !v.pinned);
  }

  // 4. Two unpinned front-deck items: static 2-row on non-round, rotating 1-row on round
  {
    const HomeActivityItem two_front[2] = {
        {3, 0, true, false, true, "Artist • Track"},
        {5, 0, false, false, true, "TODAY 14:00 • Design Sync"},
    };
    const HomeActivityView vn0 = resolve_home_activity_view(two_front, 2, 0U, true);
    const HomeActivityView vn1 = resolve_home_activity_view(two_front, 2, kHomeActivityRotateMs, true);
    LATTER_CHECK(vn0.front_count == 2 && vn0.visible_rows == 2 && vn0.top == 0 && vn0.bottom == 1);
    LATTER_CHECK(vn1.top == 0 && vn1.bottom == 1);  // Static when all items fit!

    const HomeActivityView vr0 = resolve_home_activity_view(two_front, 2, 0U, false);
    const HomeActivityView vr1 = resolve_home_activity_view(two_front, 2, kHomeActivityRotateMs, false);
    LATTER_CHECK(vr0.front_count == 2 && vr0.visible_rows == 1 && vr0.top == 0 && vr0.bottom == -1);
    LATTER_CHECK(vr1.front_count == 2 && vr1.visible_rows == 1 && vr1.top == 1 && vr1.bottom == -1);
  }

  // 5. Three unpinned front-deck items conveyor-rotate every 8 s on non-round
  {
    const HomeActivityItem three_front[3] = {
        {3, 0, true, false, true, "Artist • Track"},
        {4, 1, false, false, true, "Alarm • 6:30 AM"},
        {5, 0, false, false, true, "TODAY 16:00 • Flight"},
    };
    const HomeActivityView s0 = resolve_home_activity_view(three_front, 3, 0U, true);
    const HomeActivityView s1 = resolve_home_activity_view(three_front, 3, kHomeActivityRotateMs, true);
    const HomeActivityView s2 = resolve_home_activity_view(three_front, 3, kHomeActivityRotateMs * 2U, true);
    LATTER_CHECK(s0.front_count == 3 && s0.visible_rows == 2 && s0.top == 0 && s0.bottom == 1);
    LATTER_CHECK(s1.top == 1 && s1.bottom == 2);
    LATTER_CHECK(s2.top == 2 && s2.bottom == 0);
  }

  // 6. Urgent pin hierarchy: Doorbell (6) > Running Timer (1, any remaining) > Snoozed (4,0) > Stopwatch (2) > <=2h Alarm (4,1..3)
  {
    const HomeActivityItem timer_and_deck[3] = {
        {1, 0, true, true, true, "Timer 1 • 14:25"},
        {3, 0, true, false, true, "Artist • Track"},
        {5, 0, false, false, true, "TODAY 18:00 • Dinner"},
    };
    const HomeActivityView p0 = resolve_home_activity_view(timer_and_deck, 3, 0U, true);
    const HomeActivityView p1 = resolve_home_activity_view(timer_and_deck, 3, kHomeActivityRotateMs, true);
    LATTER_CHECK(p0.pinned && p0.visible_rows == 2 && p0.top == 0 && p0.bottom == 1);
    LATTER_CHECK(p1.pinned && p1.visible_rows == 2 && p1.top == 0 && p1.bottom == 2);

    // Multi-pin competition: Doorbell pins Row 0 while Running Timer & Snoozed Alarm rotate in Row 1 (never dropped)
    const HomeActivityItem multi_pin[4] = {
        {4, 1, false, true, true, "Alarm • 7:00 AM"},   // Imminent <= 2h alarm
        {2, 0, true, true, true, "Stopwatch • 01:12.4"},
        {4, 0, true, true, true, "Alarm • Snoozed"},
        {1, 1, true, true, true, "Timer 2 • 08:00"},
    };
    const HomeActivityView mp0 = resolve_home_activity_view(multi_pin, 4, 0U, true);
    const HomeActivityView mp1 = resolve_home_activity_view(multi_pin, 4, kHomeActivityRotateMs, true);
    LATTER_CHECK(mp0.pinned && mp0.top == 3 && mp0.front_count == 4 && mp0.visible_rows == 2 && mp0.bottom == 0);
    LATTER_CHECK(mp1.pinned && mp1.top == 3 && mp1.bottom == 1);

    const HomeActivityItem snooze_vs_sw[2] = {
        {2, 0, true, true, true, "Stopwatch • 00:10.0"},
        {4, 0, true, true, true, "Alarm • Snoozed"},
    };
    const HomeActivityView svs = resolve_home_activity_view(snooze_vs_sw, 2, 0U, true);
    LATTER_CHECK(svs.pinned && svs.top == 1 && svs.bottom == 0);

    const HomeActivityItem sw_vs_imm_alarm[2] = {
        {4, 2, false, true, true, "Alarm • 6:45 AM"},
        {2, 0, true, true, true, "Stopwatch • 00:10.0"},
    };
    const HomeActivityView swa = resolve_home_activity_view(sw_vs_imm_alarm, 2, 0U, true);
    LATTER_CHECK(swa.pinned && swa.top == 1 && swa.bottom == 0);

    const HomeActivityItem db_vs_timer[2] = {
        {1, 0, true, true, true, "Timer 1 • 00:15"},
        {6, 0, true, true, true, "Doorbell • Ringing"},
    };
    const HomeActivityView dbt = resolve_home_activity_view(db_vs_timer, 2, 0U, true);
    LATTER_CHECK(dbt.pinned && dbt.top == 1 && dbt.bottom == 0);
  }

  // 7. 5-Board Geometry Clearance & Disjoint 1-Row / 2-Row Hit Regions
  struct BoardSpec {
    int w;
    int h;
    bool round;
    bool wearable;
    int ppi;
    int weather_h;
  };
  constexpr BoardSpec kBoards[5] = {
      {466, 466, true, false, 266, 32},
      {480, 480, false, false, 313, 32},
      {410, 502, false, true, 315, 32},
      {320, 480, false, false, 165, 32},
      {720, 720, false, false, 254, 45},
  };
  for (const auto &b : kBoards) {
    const UiPanelLayout L = compute_panel_layout(b.w, b.h, b.round, b.wearable, b.ppi);
    const int card_w = (b.w > 520) ? scale_x_for(380, b.w) : std::min(380, b.w - 32);
    const int pitch = scale_y_for(56, b.h);
    const int h1 = b.round ? scale_y_for(68, b.h) : scale_y_for(60, b.h);
    const int h2 = b.round ? h1 : (h1 + pitch);
    const int dock_top = L.dock_y - L.dock_r;
    const int gap = mm_to_px_for(3.0f, b.ppi);

    for (int rows = 1; rows <= (b.round ? 1 : 2); ++rows) {
      const int ch = (rows == 2) ? h2 : h1;
      const int y1 = b.round ? (((L.home_weather_y + b.weather_h / 2 + dock_top) / 2) + ch / 2)
                             : (dock_top - gap);
      const int y0 = y1 - ch;
      const int cy = y0 + ch / 2;
      LATTER_CHECK(y0 >= L.home_weather_y + b.weather_h / 2 + 4);
      LATTER_CHECK(y1 <= dock_top);

      FrameHitTable hits{};
      register_home_activity_hit_regions(
          hits, L.cx, cy, card_w, ch, rows == 2, true, 0, rows == 2 ? 1 : -1,
          scale_x_for(56, b.w), scale_x_for(52, b.w));
      const HitRegion *r0 = hits.find_by_id(WidgetId::HOME_ACT_ROW_0);
      const HitRegion *a0 = hits.find_by_id(WidgetId::HOME_ACT_ACTION_0);
      const HitRegion *ex = hits.find_by_id(WidgetId::HOME_ACT_EXPAND);
      LATTER_CHECK(r0 != nullptr && a0 != nullptr && ex != nullptr);
      LATTER_CHECK(r0->x1 < a0->x0 && a0->x1 < ex->x0);
      if (rows == 2) {
        const HitRegion *r1 = hits.find_by_id(WidgetId::HOME_ACT_ROW_1);
        const HitRegion *a1 = hits.find_by_id(WidgetId::HOME_ACT_ACTION_1);
        LATTER_CHECK(r1 != nullptr && a1 != nullptr);
        LATTER_CHECK(r0->y1 < r1->y0 && a0->y1 < a1->y0);
        LATTER_CHECK(r0->height() >= 56 && r1->height() >= 56);
      }
    }
  }

  // 8. Source-level invariants for D-045
  const std::string layout_h = read_repo_file("../../include/ui_layout.h");
  const std::string home_cpp = read_repo_file("../../components/latter_ui/page_clock_home.cpp");
  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");
  LATTER_CHECK(layout_h.find("home_activity_h_for_rows(") != std::string::npos);
  LATTER_CHECK(layout_h.find("home_activity_rendered_rows()") != std::string::npos);
  LATTER_CHECK(home_cpp.find("draw_glass_activity_surface") != std::string::npos);
  LATTER_CHECK(home_cpp.find("kUpcomingAlarmPinMinutes") != std::string::npos);
  LATTER_CHECK(home_cpp.find("kUpcomingAlarmDeckMinutes") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-045:") != std::string::npos);
}

void test_home_hub_v6_alarm_caps_and_tools_pagination() {
  using namespace latter_ui;

  reset_runtime_state();

  // 1. 5-category Home Hub CSV order, deduplication, conveyor windowing & ALARM_CAPS parsing
  LATTER_CHECK(kMaxHomeHubTabs == 5);
  LATTER_CHECK(std::string_view(kDefaultHomeCategories) ==
               "alarm,intercom,thermostat,entry,vacuum");

  int tabs[kMaxHomeHubTabs]{};
  LATTER_CHECK(parse_home_categories_csv(kDefaultHomeCategories, tabs, kMaxHomeHubTabs) == 5);
  LATTER_CHECK(tabs[0] == HomeHubTab::ALARM &&
               tabs[1] == HomeHubTab::INTERCOM &&
               tabs[2] == HomeHubTab::THERMOSTAT &&
               tabs[3] == HomeHubTab::ENTRY &&
               tabs[4] == HomeHubTab::VACUUM);

  LATTER_CHECK(parse_home_categories_csv(
                   "intercom,alarm,entry,intercom,bogus,vacuum", tabs, kMaxHomeHubTabs) == 4);
  LATTER_CHECK(tabs[0] == HomeHubTab::INTERCOM &&
               tabs[1] == HomeHubTab::ALARM &&
               tabs[2] == HomeHubTab::ENTRY &&
               tabs[3] == HomeHubTab::VACUUM);

  // 4-slot conveyor windowing across 6 tabs
  int start_idx = 0;
  sync_mode_bar_window(start_idx, 0, 6);
  LATTER_CHECK(start_idx == 0);
  sync_mode_bar_window(start_idx, 3, 6);
  LATTER_CHECK(start_idx == 0);
  sync_mode_bar_window(start_idx, 4, 6);
  LATTER_CHECK(start_idx == 1);
  sync_mode_bar_window(start_idx, 5, 6);
  LATTER_CHECK(start_idx == 2);
  sync_mode_bar_window(start_idx, 1, 6);
  LATTER_CHECK(start_idx == 1);

  // Protocol v3 AreaCtxField::ALARM_CAPS (field 44) parsing & 0x0C fallback
  auto build_ac_payload = [](uint32_t rev, std::string_view alarm_ent, std::string_view caps_str,
                             std::string_view cats_str) {
    std::string p = "v3\x1f" "1\x1f" + std::to_string(rev);
    for (size_t i = 3; i < static_cast<size_t>(AreaCtxField::COUNT); ++i) {
      p.push_back(kProtocolFieldSep);
      if (i == static_cast<size_t>(AreaCtxField::ALARM_ENTITY)) p.append(alarm_ent);
      else if (i == static_cast<size_t>(AreaCtxField::HOME_CATEGORIES)) p.append(cats_str);
      else if (i == static_cast<size_t>(AreaCtxField::ALARM_CAPS)) p.append(caps_str);
    }
    return p;
  };

  AreaContextState ac{};
  HomeHubState hub{};
  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_ac_payload(1U, "alarm_control_panel.home_alarm", "29", "alarm,cameras,entry"), ac, hub));
  LATTER_CHECK(ac.alarm_caps == 29U);  // 0x1D = disarm_pin | arm_home | arm_away | arm_night
  LATTER_CHECK(effective_alarm_caps(ac) == 29U);

  // Configured alarm entity with unset/0 ALARM_CAPS defaults to kAlarmCapDefault (0x0C: arm_home | arm_away)
  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_ac_payload(2U, "alarm_control_panel.home_alarm", "0", "alarm"), ac, hub));
  LATTER_CHECK(ac.alarm_caps == kAlarmCapDefault && effective_alarm_caps(ac) == 0x0CU);

  // 2. Separate Security Alarm PIN Capability Gating vs Lock/Garage Perimeter Policy
  // Case A: HA alarm requires PIN to disarm (0x01) ONLY, code_arm_required == false (0x0D)
  const uint16_t caps_disarm_only = kAlarmCapCodeDisarm | kAlarmCapArmHome | kAlarmCapArmAway;
  for (std::string_view pol : {std::string_view(""), std::string_view("allow"), std::string_view("require_pin")}) {
    LATTER_CHECK(!evaluate_perimeter_action_blocked(pol, "home_hub.alarm", "disarm", 0));
    LATTER_CHECK(evaluate_perimeter_pin_prompt(pol, "home_hub.alarm", "disarm", 0, caps_disarm_only, false));
    LATTER_CHECK(evaluate_perimeter_pin_prompt(pol, "alarm_control_panel.home_alarm", "disarm", 0, caps_disarm_only, true));

    // Arming NEVER prompts or blocks when code_arm_required (0x02) is 0!
    for (const char *arm_act : {"arm_home", "arm_away", "arm_night", "arm_vacation"}) {
      LATTER_CHECK(!evaluate_perimeter_action_blocked(pol, "home_hub.alarm", arm_act, 0));
      LATTER_CHECK(!evaluate_perimeter_pin_prompt(pol, "home_hub.alarm", arm_act, 0, caps_disarm_only, false));
      LATTER_CHECK(!evaluate_perimeter_pin_prompt(pol, "home_hub.alarm", arm_act, 0, caps_disarm_only, true));
    }
  }
  // Under "block", disarm is blocked while arming without code_arm_required remains unblocked & unprompted
  LATTER_CHECK(evaluate_perimeter_action_blocked("block", "home_hub.alarm", "disarm", 0));
  LATTER_CHECK(!evaluate_perimeter_action_blocked("block", "home_hub.alarm", "arm_away", 0));
  LATTER_CHECK(!evaluate_perimeter_pin_prompt("block", "home_hub.alarm", "arm_away", 0, caps_disarm_only, true));

  // Case B: HA alarm has code_arm_required == true (0x02) -> arming prompts for PIN!
  const uint16_t caps_arm_and_disarm = caps_disarm_only | kAlarmCapCodeArm;
  for (const char *arm_act : {"arm_home", "arm_away", "arm_night", "arm_vacation"}) {
    LATTER_CHECK(evaluate_perimeter_pin_prompt("allow", "home_hub.alarm", arm_act, 0, caps_arm_and_disarm, false));
    LATTER_CHECK(!evaluate_perimeter_action_blocked("block", "home_hub.alarm", arm_act, 0));
  }

  // Case C: HA alarm has no code configured at all (0x0C) -> disarm prompts only under "require_pin"
  LATTER_CHECK(!evaluate_perimeter_pin_prompt("allow", "home_hub.alarm", "disarm", 0, kAlarmCapDefault, true));
  LATTER_CHECK(!evaluate_perimeter_pin_prompt("", "home_hub.alarm", "disarm", 0, kAlarmCapDefault, true));
  LATTER_CHECK(evaluate_perimeter_pin_prompt("require_pin", "home_hub.alarm", "disarm", 0, kAlarmCapDefault, false));

  // Case D: Lock & Garage perimeter policy preserved (unlock/open gated; lock/close never gated)
  LATTER_CHECK(!evaluate_perimeter_pin_prompt("allow", "home_hub.lock", "unlock", 0, caps_disarm_only, true));
  LATTER_CHECK(evaluate_perimeter_pin_prompt("require_pin", "home_hub.lock", "unlock", 0, caps_disarm_only, false));
  LATTER_CHECK(evaluate_perimeter_action_blocked("block", "home_hub.lock", "unlock", 0));
  LATTER_CHECK(!evaluate_perimeter_pin_prompt("require_pin", "home_hub.lock", "lock", 0, caps_disarm_only, true));
  LATTER_CHECK(!evaluate_perimeter_action_blocked("block", "home_hub.garage", "turn_off", 0));
  LATTER_CHECK(evaluate_perimeter_pin_prompt("require_pin", "home_hub.garage", "turn_on", 100, caps_disarm_only, false));

  // Canonical emit_alarm_action JSON framing (omits "code" when empty; includes "code" when provided)
  MockRpcOutbox rpc_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_actions::emit_alarm_action("arm_home", "", &rpc_box);
  LATTER_CHECK(!rpc_box.published.empty());
  LATTER_CHECK(rpc_box.published.back().find("\"entity_id\":\"home_hub.alarm\"") != std::string::npos);
  LATTER_CHECK(rpc_box.published.back().find("\"action\":\"arm_home\"") != std::string::npos);
  LATTER_CHECK(rpc_box.published.back().find("\"code\"") == std::string::npos);

  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rpc_box.published.clear();
  latter_actions::emit_alarm_action("disarm", "2468", &rpc_box);
  LATTER_CHECK(!rpc_box.published.empty());
  LATTER_CHECK(rpc_box.published.back().find("\"entity_id\":\"home_hub.alarm\"") != std::string::npos);
  LATTER_CHECK(rpc_box.published.back().find("\"action\":\"disarm\"") != std::string::npos);
  LATTER_CHECK(rpc_box.published.back().find("\"code\":\"2468\"") != std::string::npos);

  // 3. Tools N-page launcher pagination & round vertical pips bezel-safe X scaling
  static_assert(tools_launcher_item_count_for(false, false) == 4);
  static_assert(tools_launcher_item_count_for(false, true) == 5);
  static_assert(tools_launcher_item_count_for(true, false) == 5);
  static_assert(tools_launcher_item_count_for(true, true) == 6);

  static_assert(tools_launcher_page_count(0) == 1);
  static_assert(tools_launcher_page_count(1) == 1);
  static_assert(tools_launcher_page_count(6) == 1);
  static_assert(tools_launcher_page_count(7) == 2);
  static_assert(tools_launcher_page_count(12) == 2);
  static_assert(tools_launcher_page_count(13) == 3);
  static_assert(tools_launcher_page_count(19) == 4);

  struct PanelProfile {
    int w;
    int h;
    bool round;
    int ppi;
  };
  constexpr PanelProfile kPanels[7] = {
      {320, 480, false, 165},
      {360, 360, true, 260},
      {410, 502, false, 315},
      {412, 412, true, 286},
      {466, 466, true, 266},
      {480, 480, false, 313},
      {720, 720, false, 254},
  };
  for (const auto &p : kPanels) {
    int mask_x = 0, chev_x = 0, hit_x0 = 0, hit_w = 0, min_hit_h = 0;
    compute_vertical_page_pips_x(p.w, p.round, p.ppi, mask_x, chev_x, hit_x0, hit_w, min_hit_h);
    const int min_7mm = mm_to_px_for(7.0f, p.ppi);
    LATTER_CHECK(mask_x >= 0 && chev_x > mask_x && chev_x + 8 <= p.w);
    LATTER_CHECK(hit_x0 >= 0 && hit_x0 + hit_w <= p.w);
    LATTER_CHECK(hit_w >= min_7mm && min_hit_h >= min_7mm);
  }

  // 4. Camera view active predicate & source-level architectural guards
  reset_runtime_state();
  auto &rs_cam = runtime_state();
  rs_cam.nav.gui_page = PageId::HOME_HUB;
  home_hub_tab() = HomeHubTab::ALARM;
  LATTER_CHECK(!is_camera_view_active(rs_cam));
  rs_cam.nav.gui_page = PageId::CAMERAS;
  LATTER_CHECK(is_camera_view_active(rs_cam));
  const int sub_cam_grid = compute_subpage_id(
      PageId::CAMERAS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 1, 0, 0, 0, 0, CamerasTab::GRID);
  const int sub_cam_single = compute_subpage_id(
      PageId::CAMERAS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 1, 0, 0, 0, 0, CamerasTab::SINGLE);
  LATTER_CHECK(sub_cam_grid != sub_cam_single);
  rs_cam.nav.gui_page = PageId::PINNED;
  LATTER_CHECK(!is_camera_view_active(rs_cam));

  const std::string hh_src = read_repo_file("../../components/latter_ui/page_home_hub.cpp");
  const std::string area_src = read_repo_file("../../components/latter_ui/page_area_hub.cpp");
  const std::string tools_src = read_repo_file("../../components/latter_ui/page_tools.cpp");
  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");
  LATTER_CHECK(hh_src.find("render_home_hub_page(") != std::string::npos);
  LATTER_CHECK(hh_src.find("ui::mode_bar(") != std::string::npos);
  LATTER_CHECK(area_src.find("render_home_hub_page(") == std::string::npos);
  LATTER_CHECK(tools_src.find("ToolsSubpage::INTERCOM") == std::string::npos);
  LATTER_CHECK(tools_src.find("tools_launcher_page_count(") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-046:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-047:") != std::string::npos);

  const std::string wx_src = read_repo_file("../../components/latter_ui/page_weather.cpp");
  LATTER_CHECK(wx_src.find("m_r + 3") == std::string::npos);
  LATTER_CHECK(wx_src.find("scale_y(320)") == std::string::npos);
  LATTER_CHECK(wx_src.find("float u = -static_cast<float>(dx) * inv_hw;") != std::string::npos);
  LATTER_CHECK(wx_src.find("enum class RainTier") != std::string::npos);
  LATTER_CHECK(wx_src.find("coprime_lane_stride(") != std::string::npos);
  LATTER_CHECK(wx_src.find("draw_soft_snowflake_q8(") != std::string::npos);
  LATTER_CHECK(wx_src.find("draw_lightning_seg_q8(") != std::string::npos);
  LATTER_CHECK(wx_src.find("el_ms >= 480U") != std::string::npos);
  LATTER_CHECK(wx_src.find("kSnowCount = 64") != std::string::npos);
}

void test_home_hub_d048_button_only_glass_and_conditional_controls() {
  using namespace latter_ui;

  static_assert(LATTER_SDK_REV == 2026112004L, "LATTER_SDK_REV must match 2026112004L");
  static_assert(static_cast<size_t>(AreaCtxField::THERMOSTATS_SUMMARY) == 15U);
  static_assert(static_cast<size_t>(AreaCtxField::LOCKS_SUMMARY) == 16U);
  static_assert(static_cast<size_t>(AreaCtxField::GARAGES_SUMMARY) == 17U);
  static_assert(static_cast<size_t>(AreaCtxField::VACUUMS_SUMMARY) == 18U);
  static_assert(static_cast<size_t>(AreaCtxField::AREA_CLIMATES_SUMMARY) == 19U);
  static_assert(static_cast<size_t>(AreaCtxField::ALARM_CAPS) == 38U);
  static_assert(static_cast<size_t>(AreaCtxField::CAMERA_STREAM_URL) == 39U);
  static_assert(static_cast<size_t>(AreaCtxField::ALERT_CAMERA_ENTITY) == 40U);
  static_assert(static_cast<size_t>(AreaCtxField::SCREENSAVER_PHOTO_URL) == 41U);
  static_assert(static_cast<size_t>(AreaCtxField::COUNT) == 42U);

  reset_runtime_state();

  // 1. Protocol v3 AreaCtxField::ALARM_CAPS (+0x20 arm_vacation) & multi-entity Home Hub summaries (D-061)
  auto build_hub_caps_payload = [](uint32_t rev, std::string_view alarm_caps,
                                   std::string_view climate_caps, std::string_view vacuum_caps,
                                   std::string_view t_temp, std::string_view t_mode,
                                   std::string_view vac_st, bool with_entities = true,
                                   std::string_view area_id = "downstairs") {
    std::string p = "v3\x1f" "1\x1f" + std::to_string(rev);
    for (size_t i = 3; i < static_cast<size_t>(AreaCtxField::COUNT); ++i) {
      p.push_back(kProtocolFieldSep);
      if (i == static_cast<size_t>(AreaCtxField::AREA_ID)) p.append(area_id);
      else if (with_entities && i == static_cast<size_t>(AreaCtxField::ALARM_ENTITY)) {
        p.append("alarm_control_panel.home");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::THERMOSTATS_SUMMARY)) {
        p.append("climate.downstairs|Downstairs|downstairs|");
        p.append(t_mode);
        p.push_back('|');
        p.append(t_temp);
        p.append("|68.5|");
        p.append(climate_caps);
        p.push_back(kProtocolRecordSep);
        p.append("climate.upstairs|Upstairs|upstairs|cool|74|75.0|127");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::LOCKS_SUMMARY)) {
        p.append("lock.front_door|Front Door|foyer|locked");
        p.push_back(kProtocolRecordSep);
        p.append("lock.back_door|Back Door|patio|unlocked");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::GARAGES_SUMMARY)) {
        p.append("cover.main_garage|Main Garage|garage|closed");
        p.push_back(kProtocolRecordSep);
        p.append("cover.workshop_garage|Workshop Bay|garage|open");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::VACUUMS_SUMMARY)) {
        p.append("vacuum.main_floor|Main Floor|downstairs|");
        p.append(vac_st);
        p.push_back('|');
        p.append(vacuum_caps);
        p.push_back(kProtocolRecordSep);
        p.append("vacuum.upstairs|Upstairs Vac|upstairs|docked|11");
      } else if (with_entities && i == static_cast<size_t>(AreaCtxField::AREA_CLIMATES_SUMMARY)) {
        p.append("climate.office_mini|Office Mini|cool|70|71.2|127");
      } else if (i == static_cast<size_t>(AreaCtxField::ALARM_CAPS)) {
        p.append(alarm_caps);
      }
    }
    return p;
  };

  // Fresh pre-sync AreaContextState{} -> 0 tabs and 0U effective caps across all domains
  {
    const AreaContextState fresh_ac{};
    const HomeHubState fresh_hub{};
    int fresh_tabs[kMaxHomeHubTabs]{};
    LATTER_CHECK(fresh_ac.epoch == 0U && fresh_ac.home_categories.empty());
    LATTER_CHECK(collect_home_hub_tabs(fresh_tabs, fresh_ac) == 0);
    LATTER_CHECK(effective_alarm_caps(fresh_ac) == 0U);
    LATTER_CHECK(effective_climate_caps(fresh_hub) == 0U);
    LATTER_CHECK(effective_vacuum_caps(fresh_hub) == 0U);
  }

  AreaContextState ac{};
  HomeHubState hub{};
  // Configured entities with unset/0 caps fall back cleanly to spec defaults (0x0C, 0x4F, 0x0B)
  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_hub_caps_payload(1U, "0", "0", "0", "72", "heat", "idle", true, "downstairs"), ac, hub));
  LATTER_CHECK(effective_alarm_caps(ac) == kAlarmCapDefault);
  LATTER_CHECK(hub.thermostat_count == 2 && hub.thermostat_idx == 0);
  LATTER_CHECK(hub.entry_count == 4);
  LATTER_CHECK(hub.vacuum_count == 2 && hub.vacuum_idx == 0);
  LATTER_CHECK(hub.area_climate_count == 1);
  LATTER_CHECK(effective_climate_caps(hub) == kClimateCapDefault);
  LATTER_CHECK(effective_vacuum_caps(hub) == kVacuumCapDefault);
  LATTER_CHECK(hub.thermostats[0].target_temp == 72 && hub.thermostats[0].mode == "heat");
  LATTER_CHECK(hub.vacuums[0].state == "Idle");
  LATTER_CHECK(normalize_vacuum_state_str("error") == "Error");
  LATTER_CHECK(normalize_vacuum_state_str("docked") == "Docked");

  // Multi-item Entry & Garage records (D-061 / D-073)
  LATTER_CHECK(hub.entries[0].is_lock && hub.entries[0].entity_id == "lock.front_door" &&
               hub.entries[0].name == "Front Door" && hub.entries[0].lock_state == LockState::LOCKED);
  LATTER_CHECK(hub.entries[1].is_lock && hub.entries[1].entity_id == "lock.back_door" &&
               hub.entries[1].lock_state == LockState::UNLOCKED);
  LATTER_CHECK(!hub.entries[2].is_lock && hub.entries[2].entity_id == "cover.main_garage" &&
               hub.entries[2].garage_state == GarageState::CLOSED);
  LATTER_CHECK(!hub.entries[3].is_lock && hub.entries[3].entity_id == "cover.workshop_garage" &&
               hub.entries[3].garage_state == GarageState::OPEN);
  LATTER_CHECK(find_climate_item_by_id_const("climate.office_mini", hub) != nullptr);
  LATTER_CHECK(find_climate_item_by_id_const("climate.upstairs", hub) != nullptr);

  // Roaming / Area switch to "upstairs" auto-selects upstairs thermostat & upstairs vacuum!
  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_hub_caps_payload(2U, "63", "255", "63", "21.5", "heat_cool", "returning", true, "upstairs"), ac, hub));
  LATTER_CHECK(hub.thermostat_idx == 1 && hub.thermostats[1].entity_id == "climate.upstairs");
  LATTER_CHECK(hub.vacuum_idx == 1 && hub.vacuums[1].entity_id == "vacuum.upstairs");
  hub.thermostat_idx = 0;
  hub.vacuum_idx = 0;
  LATTER_CHECK(effective_alarm_caps(ac) == 0x3FU);
  LATTER_CHECK(effective_climate_caps(hub) == 0xFFU);
  LATTER_CHECK(effective_vacuum_caps(hub) == 0x3FU);
  LATTER_CHECK(hub.thermostats[0].target_temp == 22 && hub.thermostats[0].mode == "heat_cool");
  LATTER_CHECK(hub.vacuums[0].state == "Returning");

  // Unassigned entities -> effective_*_caps MUST return 0U even if alarm_caps was non-zero
  AreaContextState empty_ent_ac{};
  HomeHubState empty_ent_hub{};
  LATTER_CHECK(parse_v3_area_ctx_payload(
      build_hub_caps_payload(3U, "63", "255", "63", "72", "heat", "cleaning", false),
      empty_ent_ac, empty_ent_hub));
  LATTER_CHECK(empty_ent_ac.epoch == 1U);
  LATTER_CHECK(empty_ent_ac.alarm_entity.empty() &&
               empty_ent_hub.thermostat_count == 0 &&
               empty_ent_hub.entry_count == 0 &&
               empty_ent_hub.vacuum_count == 0 &&
               empty_ent_hub.area_climate_count == 0);
  LATTER_CHECK(empty_ent_ac.alarm_caps == 0U && effective_alarm_caps(empty_ent_ac) == 0U);
  LATTER_CHECK(effective_climate_caps(empty_ent_hub) == 0U);
  LATTER_CHECK(effective_vacuum_caps(empty_ent_hub) == 0U);

  // 2. Conditional Security Alarm buttons + FrameHitTable across all 5 flagship panels
  struct HubBoard {
    int w;
    int h;
    bool round;
    bool wearable;
    int ppi;
  };
  constexpr HubBoard kHubBoards[5] = {
      {466, 466, true, false, 266},
      {480, 480, false, false, 313},
      {410, 502, false, true, 315},
      {320, 480, false, false, 165},
      {720, 720, false, false, 254},
  };

  auto register_alarm_hits = [](FrameHitTable &ht, const HubBoard &b, std::string_view alarm_st,
                                uint16_t caps) {
    ht.clear();
    HomeHubActionSpec acts[4]{};
    const int n = resolve_alarm_actions(alarm_st, caps, acts);
    const auto ly = compute_panel_layout(b.w, b.h, b.round, b.wearable, b.ppi);
    const int min_s = (b.w < b.h) ? b.w : b.h;
    const int content_top = ly.hdr_y + ((b.h < 400) ? 20 : scale_y_for(28, b.h));
    const int content_bot = ly.app_dock_y - ly.dock_r - scale_s_for(14, min_s) -
                            ((b.h < 400) ? 4 : scale_y_for(6, b.h));
    const bool two = home_hub_uses_two_rows(
        n, content_top, content_bot, b.w, b.h, b.round, b.wearable, b.ppi);
    const int avail_h = std::max(140, content_bot - content_top);
    const int hero_h = two
        ? std::clamp((avail_h * 44) / 100, 66, scale_y_for(104, b.h))
        : std::clamp((avail_h * 56) / 100, 82, scale_y_for(132, b.h));
    const int deck_top = content_top + hero_h + std::max(6, scale_y_for(10, b.h));
    HomeHubPillGeom geoms[4]{};
    const int gcnt = compute_home_hub_pill_deck(
        n, deck_top, content_bot, b.w, b.h, b.round, b.wearable, b.ppi, two, geoms);
    int codes[4]{};
    for (int i = 0; i < gcnt; ++i) codes[i] = acts[i].code;
    register_home_hub_pill_deck_hits(ht, WidgetId::ALARM_MODE_BASE, codes, geoms, gcnt);
    return gcnt;
  };

  for (const auto &b : kHubBoards) {
    const int min_7mm = mm_to_px_for(7.0f, b.ppi);
    FrameHitTable ht{};

    // Disarmed with default 0x0C -> ONLY Arm Home (+1) & Arm Away (+2); NEVER Disarm (+0)
    LATTER_CHECK(register_alarm_hits(ht, b, "disarmed", kAlarmCapDefault) == 2);
    LATTER_CHECK(ht.find_by_id(WidgetId::ALARM_MODE_BASE + 0) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::ALARM_MODE_BASE + 1) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::ALARM_MODE_BASE + 2) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::ALARM_MODE_BASE + 3) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::ALARM_MODE_BASE + 4) == nullptr);

    // Disarmed (or empty) with all 4 arm modes (0x3C) -> 4 non-overlapping Arm hit rects, zero Disarm
    const uint16_t all_arm =
        kAlarmCapArmHome | kAlarmCapArmAway | kAlarmCapArmNight | kAlarmCapArmVacation;
    LATTER_CHECK(register_alarm_hits(ht, b, "", all_arm) == 4);
    LATTER_CHECK(ht.find_by_id(WidgetId::ALARM_MODE_BASE + 0) == nullptr);
    for (uint16_t c = 1; c <= 4; ++c) {
      const HitRegion *r = ht.find_by_id(static_cast<uint16_t>(WidgetId::ALARM_MODE_BASE + c));
      LATTER_CHECK(r != nullptr);
      LATTER_CHECK(r->x0 >= 0 && r->x1 < b.w && r->y0 >= 0 && r->y1 < b.h);
      LATTER_CHECK(r->height() >= std::min(min_7mm, 34));
    }

    // Armed / arming / pending / triggered -> ONLY single centered Disarm (+0), zero Arm hit rects!
    for (std::string_view armed_st : {std::string_view("armed_home"), std::string_view("armed_away"),
                                      std::string_view("armed_night"), std::string_view("armed_vacation"),
                                      std::string_view("arming"), std::string_view("pending"),
                                      std::string_view("triggered")}) {
      LATTER_CHECK(register_alarm_hits(ht, b, armed_st, 0x3FU) == 1);
      const HitRegion *dis = ht.find_by_id(WidgetId::ALARM_MODE_BASE + 0);
      LATTER_CHECK(dis != nullptr && std::abs(dis->cx() - b.w / 2) <= 1);
      LATTER_CHECK(dis->width() >= min_7mm && dis->height() >= min_7mm);
      for (uint16_t c = 1; c <= 4; ++c) {
        LATTER_CHECK(ht.find_by_id(static_cast<uint16_t>(WidgetId::ALARM_MODE_BASE + c)) == nullptr);
      }
    }
  }

  // 3. Conditional Robot Vacuum buttons by state + VACUUM_CAPS
  auto register_vac_hits = [](FrameHitTable &ht, const HubBoard &b, std::string_view vst,
                              uint16_t vcaps) {
    ht.clear();
    HomeHubActionSpec acts[4]{};
    const int n = resolve_vacuum_actions(vst, vcaps, acts);
    const auto ly = compute_panel_layout(b.w, b.h, b.round, b.wearable, b.ppi);
    const int min_s = (b.w < b.h) ? b.w : b.h;
    const int content_top = ly.hdr_y + ((b.h < 400) ? 20 : scale_y_for(28, b.h));
    const int content_bot = ly.app_dock_y - ly.dock_r - scale_s_for(14, min_s) -
                            ((b.h < 400) ? 4 : scale_y_for(6, b.h));
    const int avail_h = std::max(140, content_bot - content_top);
    const int hero_h = std::clamp((avail_h * 56) / 100, 82, scale_y_for(132, b.h));
    const int deck_top = content_top + hero_h + std::max(6, scale_y_for(10, b.h));
    HomeHubPillGeom geoms[4]{};
    const int gcnt = compute_home_hub_pill_deck(
        n, deck_top, content_bot, b.w, b.h, b.round, b.wearable, b.ppi, false, geoms);
    int codes[4]{};
    for (int i = 0; i < gcnt; ++i) codes[i] = acts[i].code;
    register_home_hub_pill_deck_hits(ht, WidgetId::VACUUM_ACTION_BASE, codes, geoms, gcnt);
    return gcnt;
  };

  for (const auto &b : kHubBoards) {
    FrameHitTable ht{};

    // Docked (standard 0x0B): single centered Clean (+0); NEVER Pause (+1), Dock (+2), or Stop (+3)
    LATTER_CHECK(register_vac_hits(ht, b, "Docked", kVacuumCapDefault) == 1);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 0) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 1) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 2) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 3) == nullptr);

    // Docked (full 0x3F): Clean (+0), Spot (+4), Locate (+5); NEVER Dock (+2), Pause (+1), or Stop (+3)
    LATTER_CHECK(register_vac_hits(ht, b, "docked", 0x3FU) == 3);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 0) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 4) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 5) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 1) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 2) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 3) == nullptr);

    // Cleaning (0x2B = Start | Pause | ReturnHome | Locate): Pause (+1), Dock (+2), Locate (+5); NEVER Clean (+0)
    LATTER_CHECK(register_vac_hits(ht, b, "Cleaning", 0x2BU) == 3);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 0) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 1) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 2) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 5) != nullptr);

    // Cleaning without Pause (0x0D = Start | Stop | ReturnHome): Stop (+3) + Dock (+2); NEVER Clean (+0) or Pause (+1)
    LATTER_CHECK(register_vac_hits(ht, b, "cleaning", 0x0DU) == 2);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 0) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 1) == nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 3) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 2) != nullptr);

    // Paused / Idle: Resume (+0) + Dock (+2)
    HomeHubActionSpec p_acts[4]{};
    LATTER_CHECK(resolve_vacuum_actions("Paused", kVacuumCapDefault, p_acts) == 2);
    LATTER_CHECK(std::string_view(p_acts[0].label) == "Resume" && p_acts[0].code == 0);
    LATTER_CHECK(std::string_view(p_acts[1].label) == "Dock" && p_acts[1].code == 2);
    LATTER_CHECK(register_vac_hits(ht, b, "Idle", kVacuumCapDefault) == 2);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 0) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 2) != nullptr);

    // Returning: Pause (+1) + Clean (+0); NEVER Dock (+2)!
    LATTER_CHECK(register_vac_hits(ht, b, "Returning", 0x3FU) == 2);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 1) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 0) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 2) == nullptr);

    // Error: Locate (+5), Resume (+0), Dock (+2)
    LATTER_CHECK(register_vac_hits(ht, b, "Error", 0x3FU) == 3);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 5) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 0) != nullptr);
    LATTER_CHECK(ht.find_by_id(WidgetId::VACUUM_ACTION_BASE + 2) != nullptr);
  }

  // 4. Conditional Thermostat controls + dynamic HVAC mode pills + Celsius 10..35 clamp
  LATTER_CHECK(!thermostat_shows_setpoint_buttons("off", kClimateCapDefault));
  LATTER_CHECK(!thermostat_shows_setpoint_buttons("OFF", 0xFFU));
  LATTER_CHECK(!thermostat_shows_setpoint_buttons("heat", kClimateCapOff | kClimateCapHeat));
  LATTER_CHECK(thermostat_shows_setpoint_buttons("heat", kClimateCapDefault));
  LATTER_CHECK(thermostat_shows_setpoint_buttons("fan_only", 0x7FU));

  // Fahrenheit clamp 50..90 vs Celsius clamp 10..35
  LATTER_CHECK(step_thermostat_setpoint(50, -1, kClimateCapDefault) == 50);
  LATTER_CHECK(step_thermostat_setpoint(90, +1, kClimateCapDefault) == 90);
  LATTER_CHECK(step_thermostat_setpoint(72, +1, kClimateCapDefault) == 73);
  LATTER_CHECK(step_thermostat_setpoint(0, +1, kClimateCapDefault) == 71);

  const uint16_t caps_celsius = kClimateCapDefault | kClimateCapCelsius;
  LATTER_CHECK(step_thermostat_setpoint(10, -1, caps_celsius) == 10);
  LATTER_CHECK(step_thermostat_setpoint(35, +1, caps_celsius) == 35);
  LATTER_CHECK(step_thermostat_setpoint(21, +1, caps_celsius) == 22);
  LATTER_CHECK(step_thermostat_setpoint(21, -1, caps_celsius) == 20);
  LATTER_CHECK(step_thermostat_setpoint(72, -1, caps_celsius) == 20);  // Out-of-C-range resets from 21 C

  for (const auto &b : kHubBoards) {
    const auto ly = compute_panel_layout(b.w, b.h, b.round, b.wearable, b.ppi);
    const int min_s = (b.w < b.h) ? b.w : b.h;
    const int min_hit = mm_to_px_for(7.0f, b.ppi);
    const int content_top = ly.hdr_y + ((b.h < 400) ? 20 : scale_y_for(28, b.h));
    const int content_bot = ly.app_dock_y - ly.dock_r - scale_s_for(14, min_s) -
                            ((b.h < 400) ? 4 : scale_y_for(6, b.h));
    const int avail_h = std::max(140, content_bot - content_top);

    // Mode == "off": zero -/+ hit rects; only advertised Heat (0) & Off (3) pills registered
    {
      FrameHitTable ht{};
      const uint16_t heat_only_caps = kClimateCapOff | kClimateCapHeat | kClimateCapTargetTemp;
      ThermostatModeSpec modes[6]{};
      const int mc = resolve_thermostat_modes("off", heat_only_caps, modes);
      LATTER_CHECK(mc == 2 && modes[0].code == 0 && !modes[0].active &&
                   modes[1].code == 3 && modes[1].active);
      const bool show_btns = thermostat_shows_setpoint_buttons("off", heat_only_caps);
      LATTER_CHECK(!show_btns);
      const int hero_h = std::clamp((avail_h * 58) / 100, 84, scale_y_for(136, b.h));
      const int hero_cy = content_top + hero_h / 2 + std::max(2, scale_y_for(4, b.h));
      const int hero_w = std::min(scale_x_for(328, b.w),
                                  band_safe_span_for(hero_cy, hero_h, b.w, b.h, b.round, b.wearable, 14));
      const int btn_r = std::clamp(hero_h / 3, 18, scale_s_for(26, min_s));
      const int btn_hit_r = std::max(btn_r, std::min(min_hit / 2, hero_h / 2));
      const int minus_cx = (b.w / 2 - hero_w / 2) + btn_r + std::max(8, scale_x_for(12, b.w));
      const int plus_cx = (b.w / 2 + hero_w / 2) - btn_r - std::max(8, scale_x_for(12, b.w));
      register_thermostat_stepper_hits(ht, show_btns, minus_cx, plus_cx, hero_cy, btn_r, btn_hit_r);

      const int mode_top = (hero_cy - hero_h / 2) + hero_h + std::max(6, scale_y_for(10, b.h));
      HomeHubPillGeom geoms[6]{};
      const int gcnt = compute_home_hub_pill_deck(
          mc, mode_top, content_bot, b.w, b.h, b.round, b.wearable, b.ppi, false, geoms);
      int codes[6]{modes[0].code, modes[1].code};
      register_home_hub_pill_deck_hits(ht, WidgetId::CLIMATE_MODE_BASE, codes, geoms, gcnt);
      LATTER_CHECK(ht.find_by_id(WidgetId::CLIMATE_MINUS_BTN) == nullptr);
      LATTER_CHECK(ht.find_by_id(WidgetId::CLIMATE_PLUS_BTN) == nullptr);
      LATTER_CHECK(ht.find_by_id(WidgetId::CLIMATE_MODE_BASE + 0) != nullptr);
      LATTER_CHECK(ht.find_by_id(WidgetId::CLIMATE_MODE_BASE + 3) != nullptr);
      LATTER_CHECK(ht.find_by_id(WidgetId::CLIMATE_MODE_BASE + 1) == nullptr);
      LATTER_CHECK(ht.find_by_id(WidgetId::CLIMATE_MODE_BASE + 2) == nullptr);
    }

    // Mode == "heat" with all 6 HVAC modes (0x7F): -/+ stepper hits + all 6 mode pills (including Fan +4 & Dry +5)
    {
      FrameHitTable ht{};
      const uint16_t all_clim = 0x7FU;
      ThermostatModeSpec modes[6]{};
      const int mc = resolve_thermostat_modes("heat", all_clim, modes);
      LATTER_CHECK(mc == 6);
      LATTER_CHECK(modes[0].code == 0 && modes[0].active &&
                   modes[1].code == 1 && !modes[1].active &&
                   modes[2].code == 2 && modes[3].code == 4 &&
                   modes[4].code == 5 && modes[5].code == 3);
      const bool two = home_hub_uses_two_rows(
          mc, content_top, content_bot, b.w, b.h, b.round, b.wearable, b.ppi);
      LATTER_CHECK(two);
      const int hero_h = std::clamp((avail_h * 46) / 100, 68, scale_y_for(108, b.h));
      const int hero_cy = content_top + hero_h / 2 + std::max(2, scale_y_for(4, b.h));
      const int hero_w = std::min(scale_x_for(328, b.w),
                                  band_safe_span_for(hero_cy, hero_h, b.w, b.h, b.round, b.wearable, 14));
      const int btn_r = std::clamp(hero_h / 3, 18, scale_s_for(26, min_s));
      const int btn_hit_r = std::max(btn_r, std::min(min_hit / 2, hero_h / 2));
      const int minus_cx = (b.w / 2 - hero_w / 2) + btn_r + std::max(8, scale_x_for(12, b.w));
      const int plus_cx = (b.w / 2 + hero_w / 2) - btn_r - std::max(8, scale_x_for(12, b.w));
      const bool show_btns = thermostat_shows_setpoint_buttons("heat", all_clim);
      LATTER_CHECK(show_btns);
      register_thermostat_stepper_hits(ht, show_btns, minus_cx, plus_cx, hero_cy, btn_r, btn_hit_r);

      const int mode_top = (hero_cy - hero_h / 2) + hero_h + std::max(6, scale_y_for(10, b.h));
      HomeHubPillGeom geoms[6]{};
      const int gcnt = compute_home_hub_pill_deck(
          mc, mode_top, content_bot, b.w, b.h, b.round, b.wearable, b.ppi, two, geoms);
      int codes[6]{};
      for (int i = 0; i < gcnt; ++i) codes[i] = modes[i].code;
      register_home_hub_pill_deck_hits(ht, WidgetId::CLIMATE_MODE_BASE, codes, geoms, gcnt);
      const HitRegion *minus_r = ht.find_by_id(WidgetId::CLIMATE_MINUS_BTN);
      const HitRegion *plus_r = ht.find_by_id(WidgetId::CLIMATE_PLUS_BTN);
      LATTER_CHECK(minus_r != nullptr && plus_r != nullptr && minus_r->x1 < plus_r->x0);
      for (uint16_t c = 0; c < 6; ++c) {
        const HitRegion *mr = ht.find_by_id(static_cast<uint16_t>(WidgetId::CLIMATE_MODE_BASE + c));
        LATTER_CHECK(mr != nullptr && mr->x0 >= 0 && mr->x1 < b.w && mr->y0 > plus_r->y1);
      }
    }
  }

  // 5. Unassigned entity zero-hit-rect proofs across all 5 flagship panels + source-level guards
  for (const auto &b : kHubBoards) {
    FrameHitTable ht{};
    // Unassigned alarm -> 0U caps -> 0 actions -> 0 hit rects (disarmed or armed)
    LATTER_CHECK(register_alarm_hits(ht, b, "disarmed", effective_alarm_caps(empty_ent_ac)) == 0);
    LATTER_CHECK(ht.count == 0);
    LATTER_CHECK(register_alarm_hits(ht, b, "armed_away", effective_alarm_caps(empty_ent_ac)) == 0);
    LATTER_CHECK(ht.count == 0);

    // Unassigned vacuum -> 0U caps -> 0 actions -> 0 hit rects (Docked or Cleaning)
    LATTER_CHECK(register_vac_hits(ht, b, "Docked", effective_vacuum_caps(empty_ent_hub)) == 0);
    LATTER_CHECK(ht.count == 0);
    LATTER_CHECK(register_vac_hits(ht, b, "Cleaning", effective_vacuum_caps(empty_ent_hub)) == 0);
    LATTER_CHECK(ht.count == 0);

    // Unassigned thermostat -> 0U caps -> no -/+ buttons and 0 mode pills -> 0 hit rects
    ht.clear();
    const uint16_t no_clim = effective_climate_caps(empty_ent_hub);
    LATTER_CHECK(no_clim == 0U);
    LATTER_CHECK(!thermostat_shows_setpoint_buttons("heat", no_clim));
    ThermostatModeSpec no_modes[6]{};
    LATTER_CHECK(resolve_thermostat_modes("heat", no_clim, no_modes) == 0);
    register_thermostat_stepper_hits(ht, thermostat_shows_setpoint_buttons("heat", no_clim),
                                     80, b.w - 80, 140, 22, 26);
    HomeHubPillGeom no_geoms[6]{};
    const int no_gcnt = compute_home_hub_pill_deck(
        0, 210, 340, b.w, b.h, b.round, b.wearable, b.ppi, false, no_geoms);
    int no_codes[6]{};
    register_home_hub_pill_deck_hits(ht, WidgetId::CLIMATE_MODE_BASE, no_codes, no_geoms, no_gcnt);
    LATTER_CHECK(no_gcnt == 0 && ht.count == 0);

    // Unassigned lock + garage -> entry_count == 0 -> 0 hit rects
    ht.clear();
    LATTER_CHECK(empty_ent_hub.entry_count == 0 && ht.count == 0);
  }

  // 6. Source-level D-048 & zero-dummy guards: zero passive-text glass containers, static helper subtext, or unassigned fallbacks
  const std::string hh_src = read_repo_file("../../components/latter_ui/page_home_hub.cpp");
  const std::string te_src = read_repo_file("../../components/latter_ui/touch_engine.cpp");
  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");
  LATTER_CHECK(hh_src.find("has_cfg_pair") == std::string::npos);
  LATTER_CHECK(hh_src.find("No Security Alarm configured") != std::string::npos);
  LATTER_CHECK(hh_src.find("No Thermostat configured") != std::string::npos);
  LATTER_CHECK(hh_src.find("No Entry Controls configured") != std::string::npos);
  LATTER_CHECK(hh_src.find("No Robot Vacuum configured") != std::string::npos);
  LATTER_CHECK(hh_src.find("PIN to Disarm") == std::string::npos);
  LATTER_CHECK(hh_src.find("Tap mode below") == std::string::npos);
  LATTER_CHECK(hh_src.find("Select vacuum command") == std::string::npos);
  // D-070: render_empty_state() is strictly bare shadowed text (zero glass), and
  // render_hub_selector_pill() delegates to ui::header_pager_bar() (zero glass behind title).
  LATTER_CHECK(hh_src.find("ui::header_pager_bar(") != std::string::npos);
  // render_card_box may appear ONLY inside render_intercom_tab (tappable rows) and
  // render_entry_card — NEVER inside render_alarm_tab, render_thermostat_tab, or render_vacuum_tab!
  const size_t alarm_fn = hh_src.find("void render_alarm_tab(");
  const size_t sel_fn = hh_src.find("void render_hub_selector_pill(");
  const size_t therm_fn = hh_src.find("void render_thermostat_tab(");
  const size_t entry_fn = hh_src.find("void render_entry_card(");
  const size_t vac_fn = hh_src.find("void render_vacuum_tab(");
  const size_t page_fn = hh_src.find("void render_home_hub_page(");
  LATTER_CHECK(alarm_fn != std::string::npos && sel_fn != std::string::npos && sel_fn > alarm_fn);
  LATTER_CHECK(therm_fn != std::string::npos && entry_fn > therm_fn);
  LATTER_CHECK(vac_fn != std::string::npos && page_fn > vac_fn);
  LATTER_CHECK(hh_src.substr(alarm_fn, sel_fn - alarm_fn).find("render_card_box(") == std::string::npos);
  LATTER_CHECK(hh_src.substr(therm_fn, entry_fn - therm_fn).find("render_card_box(") == std::string::npos);
  LATTER_CHECK(hh_src.substr(vac_fn, page_fn - vac_fn).find("render_card_box(") == std::string::npos);
  LATTER_CHECK(te_src.find("\"arm_vacation\"") != std::string::npos);
  LATTER_CHECK(te_src.find("\"clean_spot\"") != std::string::npos);
  LATTER_CHECK(te_src.find("\"locate\"") != std::string::npos);
  LATTER_CHECK(te_src.find("\"fan_only\"") != std::string::npos);
  LATTER_CHECK(te_src.find("\"dry\"") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-047:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-048:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-049:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-050:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-061:") != std::string::npos);
}

// ==============================================================================
// 19. D-049 & D-050: FIRST-PARTY TOUCH HAL, KINEMATIC DEADBAND & ENTITY INSPECTOR
// ==============================================================================
void test_touch_hal_kinematic_gestures_and_d049_d050() {
  using namespace latter_ui;

  // 1. First-Party Touch HAL Frame Decoders (FT6336/FT3168, GT911, CST9217, CST816S)
  {
    const uint8_t ft_down[5] = {0x01U, 0x81U, 0x2CU, 0x00U, 0xF0U};  // Contact at (300, 240)
    const auto s_ft = decode_focaltech_frame(ft_down);
    LATTER_CHECK(s_ft.kind == TouchSampleKind::Contact && s_ft.raw_x == 300 && s_ft.raw_y == 240 && !s_ft.ack_buffer);

    const uint8_t ft_lift_cnt0[5] = {0x00U, 0x81U, 0x2CU, 0x00U, 0xF0U};
    LATTER_CHECK(decode_focaltech_frame(ft_lift_cnt0).kind == TouchSampleKind::Released);

    const uint8_t ft_lift_flag[5] = {0x01U, 0x41U, 0x2CU, 0x00U, 0xF0U};  // Event flag 01 = Lift
    LATTER_CHECK(decode_focaltech_frame(ft_lift_flag).kind == TouchSampleKind::Released);

    const uint8_t ft_corrupt[5] = {0x0FU, 0x80U, 0x10U, 0x00U, 0x10U};   // Count 15 (> 5) -> Invalid/ignored
    LATTER_CHECK(decode_focaltech_frame(ft_corrupt).kind == TouchSampleKind::Invalid);

    // GT911: status & 0x80 == 0 MUST return NotReady with ack_buffer == false (no premature 0x814E clear!)
    const uint8_t gt_pt[4] = {0x68U, 0x01U, 0xAEU, 0x02U};  // (360, 686) little-endian
    const auto s_gt_busy = decode_gt911_frame(0x01U, gt_pt);
    LATTER_CHECK(s_gt_busy.kind == TouchSampleKind::NotReady && !s_gt_busy.ack_buffer);

    const auto s_gt_down = decode_gt911_frame(0x81U, gt_pt);
    LATTER_CHECK(s_gt_down.kind == TouchSampleKind::Contact && s_gt_down.ack_buffer &&
                 s_gt_down.raw_x == 360 && s_gt_down.raw_y == 686);

    const auto s_gt_up = decode_gt911_frame(0x80U, nullptr);
    LATTER_CHECK(s_gt_up.kind == TouchSampleKind::Released && s_gt_up.ack_buffer);

    // CST9217 / CST9220: 7-byte single-point report at 0xD000 (buf[5]=count, buf[6]=0xAB sync, ack_buffer=false)
    const uint8_t cst_bad[7] = {0x06U, 0x10U, 0x20U, 0x34U, 0x20U, 0x01U, 0x00U};
    LATTER_CHECK(decode_cst9217_frame(cst_bad).kind == TouchSampleKind::NotReady &&
                 !decode_cst9217_frame(cst_bad).ack_buffer);

    const uint8_t cst_up[7] = {0x00U, 0x10U, 0x20U, 0x34U, 0x00U, 0x00U, 0xABU};
    LATTER_CHECK(decode_cst9217_frame(cst_up).kind == TouchSampleKind::Released &&
                 !decode_cst9217_frame(cst_up).ack_buffer);

    const uint8_t cst_down[7] = {0x06U, 0x17U, 0x1DU, 0x42U, 0x20U, 0x01U, 0xABU};  // x=(0x17<<4)|4=372, y=(0x1D<<4)|2=466
    const auto s_cst = decode_cst9217_frame(cst_down);
    LATTER_CHECK(s_cst.kind == TouchSampleKind::Contact && s_cst.raw_x == 372 && s_cst.raw_y == 466 && !s_cst.ack_buffer);

    const uint8_t c816_down[6] = {0x00U, 0x01U, 0x80U, 0xFAU, 0x01U, 0x04U};  // (250, 260)
    const auto s_816 = decode_cst816s_frame(c816_down);
    LATTER_CHECK(s_816.kind == TouchSampleKind::Contact && s_816.raw_x == 250 && s_816.raw_y == 260);

    const uint8_t c816_two_pt[6] = {0x00U, 0x02U, 0x00U, 0x64U, 0x00U, 0xC8U};  // 2-point CST826 Down at (100, 200)
    const auto s_816_two = decode_cst816s_frame(c816_two_pt);
    LATTER_CHECK(s_816_two.kind == TouchSampleKind::Contact && s_816_two.raw_x == 100 && s_816_two.raw_y == 200);

    const uint8_t c816_idle_ff[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};  // Idle all-0xFF -> Released (never phantom tap)
    LATTER_CHECK(decode_cst816s_frame(c816_idle_ff).kind == TouchSampleKind::Released);

    const uint8_t c816_cnt3[6] = {0x00U, 0x03U, 0x80U, 0x64U, 0x00U, 0xC8U};     // Count 3 (> 2) -> Invalid
    LATTER_CHECK(decode_cst816s_frame(c816_cnt3).kind == TouchSampleKind::Invalid);

    const uint8_t c816_cnt5[6] = {0x00U, 0x05U, 0x80U, 0x64U, 0x00U, 0xC8U};     // Count 5 (must not & 0x03 to 1) -> Invalid
    LATTER_CHECK(decode_cst816s_frame(c816_cnt5).kind == TouchSampleKind::Invalid);

    const uint8_t c816_sent_x[6] = {0x00U, 0x01U, 0x8FU, 0xFFU, 0x00U, 0x64U};   // X == 0x0FFF sentinel -> Invalid
    LATTER_CHECK(decode_cst816s_frame(c816_sent_x).kind == TouchSampleKind::Invalid);

    const uint8_t c816_sent_y[6] = {0x00U, 0x01U, 0x80U, 0x64U, 0x0FU, 0xFFU};   // Y == 0x0FFF sentinel -> Invalid
    LATTER_CHECK(decode_cst816s_frame(c816_sent_y).kind == TouchSampleKind::Invalid);

    const uint8_t c816_rsvd_ev[6] = {0x00U, 0x01U, 0xC0U, 0x64U, 0x00U, 0xC8U};  // Event flag 0x03 (Reserved) -> Invalid
    LATTER_CHECK(decode_cst816s_frame(c816_rsvd_ev).kind == TouchSampleKind::Invalid);

    static_assert(default_active_poll_ms(TouchHalProtocol::GT911) == 8U);
    static_assert(default_active_poll_ms(TouchHalProtocol::FT6336) == 10U);
    static_assert(default_active_poll_ms(TouchHalProtocol::FT3168) == 10U);
    static_assert(default_active_poll_ms(TouchHalProtocol::CST9217) == 16U);
  }

  // 2. Kinematic Deadband, Fast-Flick Velocity & Axis Lock Across All 5 Flagship PPIs
  for (int ppi : {165, 254, 266, 313, 315}) {
    const KinematicMetrics km = KinematicMetrics::for_ppi(ppi);
    const int slop = km.tap_slop_px;
    const int lock_px = km.axis_lock_px;
    const int live_px = km.swipe_live_px;
    const int flick_px = km.swipe_flick_px;
    LATTER_CHECK(flick_px <= slop);
    LATTER_CHECK(slop < live_px);
    LATTER_CHECK(lock_px <= live_px);

    // Quick tap with ~1.2 mm finger roll keeps tap_cancelled == false
    TouchGestureState g{};
    g.begin(160, 240, 1000U, PageId::HOME, 101);
    const int roll_px = std::max(1, mm_to_px_for(1.20f, ppi));
    g.record_move(160 + roll_px, 240, 1025U, ppi);
    LATTER_CHECK(!g.tap_cancelled && !g.swipe_committed);
    LATTER_CHECK(eval_kinematic_horiz_swipe(roll_px, 0, 25U, true, ppi, g.axis_lock) == 0);

    // Deadband micro-drag (~2.0 mm over 250 ms = 8 mm/s < 38 mm/s): neither swipe nor tap!
    g.begin(160, 240, 2000U, PageId::HOME, 101);
    const int dead_px = slop + 2;
    if (dead_px < live_px) {
      g.record_move(160 - dead_px, 240, 2250U, ppi);
      LATTER_CHECK(g.tap_cancelled);
      LATTER_CHECK(eval_kinematic_horiz_swipe(-dead_px, 0, 250U, false, ppi, g.axis_lock) == 0);
      LATTER_CHECK(eval_kinematic_horiz_swipe(-dead_px, 0, 250U, true, ppi, g.axis_lock) == 0);
    }

    // Fast 2-sample flick (~1.85 mm in 35 ms = ~53 mm/s >= 38 mm/s): commits cleanly on release!
    g.begin(160, 240, 3000U, PageId::HOME, 101);
    const int fast_px = std::max(flick_px, slop + 1);
    g.record_move(160 - fast_px, 240, 3035U, ppi);
    LATTER_CHECK(g.tap_cancelled);
    LATTER_CHECK(eval_kinematic_horiz_swipe(-fast_px, 0, 35U, true, ppi, g.axis_lock) == +1);

    // Live swipe (>= 2.5 mm): commits immediately during on_touch_move (is_release == false)
    g.begin(160, 240, 4000U, PageId::HOME, 101);
    const int full_px = live_px + 2;
    g.record_move(160 + full_px, 242, 4090U, ppi);
    LATTER_CHECK(g.axis_lock == GestureAxisLock::Horizontal);
    LATTER_CHECK(eval_kinematic_horiz_swipe(full_px, 2, 90U, false, ppi, g.axis_lock) == -1);
    LATTER_CHECK(eval_kinematic_vert_swipe(full_px, 2, 90U, false, ppi, g.axis_lock) == 0);
  }

  // 3. Clamped vs. Wrapping Pagers & EdgeBounceState
  {
    edge_bounce().clear();
    bool changed = true;
    // Boundary swipe left on Home Dock page 0 clamps at 0 and triggers 135 ms rubber-band bounce
    const int p0 = step_pager_index(0, 0, 2, -1, /*wrap=*/false,
                                    GestureAxisLock::Horizontal, PageId::HOME, 5000U, changed);
    LATTER_CHECK(p0 == 0 && !changed);
    LATTER_CHECK(edge_bounce().is_active(PageId::HOME, GestureAxisLock::Horizontal, 5040U));
    LATTER_CHECK(edge_bounce().offset_px(PageId::HOME, GestureAxisLock::Horizontal, 5065U, 266) > 0);
    LATTER_CHECK(!edge_bounce().is_active(PageId::HOME, GestureAxisLock::Horizontal, 5200U));
    LATTER_CHECK(edge_bounce().offset_px(PageId::HOME, GestureAxisLock::Horizontal, 5200U, 266) == 0);

    // Wrapping mode-bar pager wraps 0 -> 3 and 3 -> 0 with zero edge bounce
    edge_bounce().clear();
    const int w_prev = step_pager_index(0, 0, 3, -1, /*wrap=*/true,
                                        GestureAxisLock::Horizontal, PageId::WEATHER, 6000U, changed);
    LATTER_CHECK(w_prev == 3 && changed &&
                 !edge_bounce().is_active(PageId::WEATHER, GestureAxisLock::Horizontal, 6010U));
  }

  // 4. Target-Scoped Debounce (rapid same-target vs. 0 ms cross-target)
  {
    touch_router() = TouchRouter{};
    const uint32_t key_vol_up = make_tap_target_key(PageId::REMOTE, WidgetId::REMOTE_VOL_UP, 0);
    const uint32_t key_vol_dn = make_tap_target_key(PageId::REMOTE, WidgetId::REMOTE_VOL_DOWN, 0);
    LATTER_CHECK(touch_router().allow_tap(1000U, TOUCH_DEBOUNCE_RAPID, key_vol_up));
    LATTER_CHECK(!touch_router().allow_tap(1040U, TOUCH_DEBOUNCE_RAPID, key_vol_up));  // < 65ms same target blocked
    LATTER_CHECK(touch_router().allow_tap(1025U, TOUCH_DEBOUNCE_RAPID, key_vol_dn));   // 25ms cross-target allowed!
    LATTER_CHECK(touch_router().allow_tap(1095U, TOUCH_DEBOUNCE_RAPID, key_vol_dn));   // +70ms >= 65ms repeater allowed!
  }

  // 5. 4-Option tap_to_wake Power Policy & Wearable Watch Double-Tap Window
  {
    (void)accept_screen_off_tap("Disabled", false, 900U);  // reset internal gate
    // Single Tap: wakes immediately on battery or USB
    LATTER_CHECK(accept_screen_off_tap("Single Tap", false, 1000U));
    LATTER_CHECK(accept_screen_off_tap("Single Tap", true, 1200U));

    // Double Tap: 1st tap arms, < 35ms bounce rejected, 35..450ms 2nd tap wakes, > 450ms re-arms
    (void)accept_screen_off_tap("Disabled", false, 1900U);
    LATTER_CHECK(!accept_screen_off_tap("Double Tap", false, 2000U));  // Tap 1 arms
    LATTER_CHECK(!accept_screen_off_tap("Double Tap", false, 2020U));  // 20ms contact bounce ignored
    LATTER_CHECK(accept_screen_off_tap("Double Tap", false, 2240U));   // 240ms Tap 2 wakes!

    LATTER_CHECK(!accept_screen_off_tap("Double Tap", false, 3000U));  // New Tap 1 arms
    LATTER_CHECK(!accept_screen_off_tap("Double Tap", false, 3600U));  // 600ms > 450ms -> re-arms as Tap 1
    LATTER_CHECK(accept_screen_off_tap("Double Tap", false, 3820U));   // +220ms -> wakes!
    LATTER_CHECK(accept_screen_off_tap("Double Tap", true, 3950U));    // Docked on USB wakes on single tap!

    // Docked Only & Disabled
    LATTER_CHECK(!accept_screen_off_tap("Docked Only", false, 5000U));
    LATTER_CHECK(accept_screen_off_tap("Docked Only", true, 5100U));
    LATTER_CHECK(!accept_screen_off_tap("Disabled", false, 5200U));
    LATTER_CHECK(!accept_screen_off_tap("Disabled", true, 5300U));

    // Direct tap_to_wake preference round-trip across all 4 options
    PersistedDevicePrefs pp{};
    PrefMask chg{};
    uint32_t eff = 0U;
    for (const char *tap_opt : {"Double Tap", "Single Tap", "Docked Only", "Disabled"}) {
      chg = 0ULL;
      LATTER_CHECK(set_pref_by_key_str(pp, PrefKey::TAP_TO_WAKE, tap_opt, true, chg, eff));
      LATTER_CHECK(std::string_view(pref_tap_to_wake_cstr(pp)) == tap_opt);
    }
  }

  // 6. D-050 Source Guards: zero inline horizontal sliders on Area Overview; generalized Entity Inspector
  {
    const std::string area_src = read_repo_file("../../components/latter_ui/page_area_hub.cpp");
    const std::string touch_src = read_repo_file("../../components/latter_ui/touch_engine.cpp");
    LATTER_CHECK(area_src.find("AREA_CARD_SLIDER") == std::string::npos);
    LATTER_CHECK(area_src.find("WidgetId::AREA_CARD_TOGGLE") != std::string::npos);
    LATTER_CHECK(area_src.find("WidgetId::AREA_CARD_TAP") != std::string::npos);
    LATTER_CHECK(area_src.find("WidgetId::ENTITY_PRESET_BASE") != std::string::npos);
    LATTER_CHECK(touch_src.find("tap_max_loose") == std::string::npos);
    LATTER_CHECK(touch_src.find("try_commit_kinematic_swipe_") != std::string::npos);
  }
}

void test_custom_icon_tiers_and_d051() {
  using namespace latter_ui;

  // 1. Container-proportional dock_icon_r_for across all flagship geometries (54..57% optical fill)
  static_assert(dock_icon_r_for(24) == 13);  // 320x480 Remote (dock_r=24 -> 27/49 = 55.1%)
  static_assert(dock_icon_r_for(27) == 15);  // 410x502, 466x466, 480x480 (dock_r=27 -> 31/55 = 56.4%)
  static_assert(dock_icon_r_for(40) == 22);  // 720x720 Desk (dock_r=40 -> 45/81 = 55.6%)

  for (const auto &p : kFlagshipPanels) {
    const auto base = compute_panel_layout(p.w, p.h, p.round, p.wearable, p.ppi);
    const int ir = dock_icon_r_for(base.dock_r);
    const float fill = static_cast<float>(2 * ir + 1) / static_cast<float>(2 * base.dock_r + 1);
    LATTER_CHECK(fill >= 0.53f && fill <= 0.58f);

    const int card_ir = card_icon_r_for(base.area_card3_h);
    LATTER_CHECK(card_ir >= 13 && card_ir * 2 + 1 <= base.area_card3_h - 16);

    const int min_dim = std::min(p.w, p.h);
    const int hero_compact = hero_icon_r_for(66, min_dim);
    const int hero_tall = hero_icon_r_for((p.h > 520) ? 204 : 132, min_dim);
    LATTER_CHECK(hero_compact >= scale_s_for(13, min_dim));
    LATTER_CHECK(hero_tall <= scale_s_for(24, min_dim));
    if (p.w == 320) {
      LATTER_CHECK(ir == 13);
    } else if (p.w == 720) {
      LATTER_CHECK(ir == 22);
      LATTER_CHECK(card_ir == 22);
      LATTER_CHECK(hero_tall == 37);
    } else if (p.w >= 410) {
      LATTER_CHECK(ir == 15);
    }
  }

  // 2. Source-level guards: zero internal scale_s() double-scaling inside draw_icon_*, D-051 glyphs & docs
  {
    const std::string w_hdr = read_repo_file("../../include/ui_widgets.h");
    const std::string w_src = read_repo_file("../../components/latter_ui/ui_widgets.cpp");
    const std::string home_src = read_repo_file("../../components/latter_ui/page_clock_home.cpp");
    const std::string hub_src = read_repo_file("../../components/latter_ui/page_home_hub.cpp");
    const std::string time_src = read_repo_file("../../components/latter_ui/page_time.cpp");
    const std::string doc_src = read_repo_file("../../docs/device-ui-design.md");

    LATTER_CHECK(w_hdr.find("draw_icon_clock_face") == std::string::npos);
    LATTER_CHECK(w_hdr.find("draw_icon_clock_solid") != std::string::npos);
    LATTER_CHECK(w_hdr.find("draw_icon_shield_home") != std::string::npos);

    const size_t glyph_sec_start = w_src.find("void draw_icon_power(");
    const size_t glyph_sec_end = w_src.find("void toggle_switch(");
    LATTER_CHECK(glyph_sec_start != std::string::npos && glyph_sec_end != std::string::npos &&
                 glyph_sec_start < glyph_sec_end);
    const std::string glyph_sec = w_src.substr(glyph_sec_start, glyph_sec_end - glyph_sec_start);
    LATTER_CHECK(glyph_sec.find("scale_s(") == std::string::npos);

    LATTER_CHECK(home_src.find("draw_route_icon(") != std::string::npos);
    LATTER_CHECK(w_src.find("draw_icon_clock_solid(") != std::string::npos);
    LATTER_CHECK(hub_src.find("draw_icon_shield_home") != std::string::npos);
    LATTER_CHECK(time_src.find("draw_icon_clock_solid") != std::string::npos);
    LATTER_CHECK(time_src.find("sample_glass_surface_color") == std::string::npos);
    LATTER_CHECK(doc_src.find("### D-051:") != std::string::npos);
    LATTER_CHECK(doc_src.find("### D-052:") != std::string::npos);
  }

  // 3. D-052: First-class kHitLongPress (480 ms hold-in-place, slop cancellation & Remote OK/Home/Back)
  {
    TouchGestureState g{};
    g.begin(233, 233, 1000U, PageId::REMOTE, 70);
    g.arm_long_press(WidgetId::REMOTE_DPAD_OK, 0);
    LATTER_CHECK(g.long_press_armed && !g.long_press_fired);
    LATTER_CHECK(!g.should_fire_long_press(1479U));  // 479 ms < 480 ms
    LATTER_CHECK(g.should_fire_long_press(1480U));   // 480 ms stationary hold fires!
    g.mark_long_press_fired();
    LATTER_CHECK(g.long_press_fired && !g.long_press_armed && g.tap_cancelled);
    LATTER_CHECK(!g.should_fire_long_press(1600U));  // never double-fires while held

    // Exceeding kTapSlopMm (1.65 mm) disarms long_press_armed irreversibly
    g.begin(233, 233, 2000U, PageId::REMOTE, 70);
    g.arm_long_press(WidgetId::REMOTE_DPAD_OK, 0);
    const int slop_px = KinematicMetrics::for_ppi(266).tap_slop_px;
    g.record_move(233 + slop_px + 2, 233, 2120U, 266);
    LATTER_CHECK(g.tap_cancelled && !g.long_press_armed);
    LATTER_CHECK(!g.should_fire_long_press(2600U));

    // Verify page_remote.cpp stamps kHitLongPress on OK, Home, and Back
    const std::string rem_src = read_repo_file("../../components/latter_ui/page_remote.cpp");
    LATTER_CHECK(rem_src.find("WidgetId::REMOTE_DPAD_OK, kHitTap | kHitLongPress") != std::string::npos);
    LATTER_CHECK(rem_src.find("WidgetId::REMOTE_NAV_HOME, kHitTap | kHitLongPress") != std::string::npos);
    LATTER_CHECK(rem_src.find("WidgetId::REMOTE_NAV_BACK, kHitTap | kHitLongPress") != std::string::npos);

    // 4. Full-ring polar Remote D-Pad geometry & rapid header-band flush (no 83% dead ring / full-frame stall)
    const RemoteDpadGeom pk = compute_remote_dpad_geom(466, 466, true);
    LATTER_CHECK(pk.cx == 233 && pk.cy == 233 && pk.r_in == 50 && pk.r_out == 144);
    LATTER_CHECK(pk.hit_radial >= 80 && pk.hit_tangent >= 68);
    // Center OK hole (< r_in) and beyond outer halo (> r_out + outer_pad) return NONE
    LATTER_CHECK(resolve_remote_dpad_widget(233, 233, pk) == WidgetId::NONE);
    LATTER_CHECK(resolve_remote_dpad_widget(233, 233 - 49, pk) == WidgetId::NONE);
    LATTER_CHECK(resolve_remote_dpad_widget(233, 233 - 160, pk) == WidgetId::NONE);
    // Inner rim (r=52), outer rim (r=142), outer halo (r=152), and off-center wedge corners all resolve cleanly
    LATTER_CHECK(resolve_remote_dpad_widget(233, 233 - 52, pk) == WidgetId::REMOTE_DPAD_UP);
    LATTER_CHECK(resolve_remote_dpad_widget(233, 233 - 142, pk) == WidgetId::REMOTE_DPAD_UP);
    LATTER_CHECK(resolve_remote_dpad_widget(233 + 60, 233 - 80, pk) == WidgetId::REMOTE_DPAD_UP);
    LATTER_CHECK(resolve_remote_dpad_widget(233 - 55, 233 + 115, pk) == WidgetId::REMOTE_DPAD_DOWN);
    LATTER_CHECK(resolve_remote_dpad_widget(233 - 140, 233 + 35, pk) == WidgetId::REMOTE_DPAD_LEFT);
    LATTER_CHECK(resolve_remote_dpad_widget(233 + 150, 233 - 20, pk) == WidgetId::REMOTE_DPAD_RIGHT);

    const std::string te_src = read_repo_file("../../components/latter_ui/touch_engine.cpp");
    LATTER_CHECK(te_src.find("resolve_remote_dpad_widget(") != std::string::npos);
    LATTER_CHECK(te_src.find("rt_.schedule_band_disp(0, std::min(SCREEN_H - 1, layout().hdr_y + scale_y(24)))") != std::string::npos);
  }
}

void test_universal_shortcuts_and_pinned_app() {
  using namespace latter_ui;
  reset_runtime_state();

  // 1. Default dock hides app:pinned until >= 1 pinned item exists, hides app:remote without remotes, and appends Settings
  ShortcutItem dock_eff[kMaxEffectiveDockItems]{};
  int cnt = collect_effective_dock_items(dock_eff, kMaxEffectiveDockItems);
  LATTER_CHECK(cnt == 6);  // music, home_hub, area, games, tools + Settings
  LATTER_CHECK(dock_eff[0].route_id == RouteId::APP_MUSIC);
  LATTER_CHECK(dock_eff[cnt - 1].route_id == RouteId::SETTINGS_ROOT);

  // 2. Multi-chunk Protocol v3 shortcuts assembly (60 pinned items across 3 chunks = 10 pages!)
  auto build_sc_chunk = [](uint32_t ep, uint32_t rv, int c_idx, int c_cnt,
                           const std::string &dock_blob, int p_tot, const std::string &pin_blob) {
    std::vector<std::string> tok(static_cast<size_t>(ShortcutsField::COUNT), "");
    tok[static_cast<size_t>(ShortcutsField::CHUNK_INDEX)] = std::to_string(c_idx);
    tok[static_cast<size_t>(ShortcutsField::CHUNK_COUNT)] = std::to_string(c_cnt);
    tok[static_cast<size_t>(ShortcutsField::DOCK_ITEMS)] = dock_blob;
    tok[static_cast<size_t>(ShortcutsField::PINNED_TOTAL)] = std::to_string(p_tot);
    tok[static_cast<size_t>(ShortcutsField::PINNED_ITEMS)] = pin_blob;
    return build_v3_packet(ep, rv, tok);
  };

  auto make_pin_slice = [](int start_i, int count_i) {
    std::string out;
    for (int i = 0; i < count_i; ++i) {
      if (i > 0) out.push_back(kProtocolRecordSep);
      const int idx = start_i + i;
      if (idx == 0) {
        out.append("route|weather:moon||Moon Phase||0|-1");
      } else if (idx == 1) {
        out.append("entity|light.nursery|toggle|Nursery Light|mdi:ceiling-light|0|1");
      } else {
        out.append("entity|script.helper_" + std::to_string(idx) + "|turn_on|Helper " +
                   std::to_string(idx) + "|mdi:bell-ring|1|-1");
      }
    }
    return out;
  };

  const std::string dock_blob =
      std::string("route|app:pinned||Pinned||0|-1") + kProtocolRecordSep +
      "entity|script.call_thu|turn_on|Call Thu|mdi:bell-ring|1|-1" + kProtocolRecordSep +
      "route|weather:moon||Moon Phase||0|-1";

  // Intermediate chunks stage without prematurely committing
  LATTER_CHECK(!parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 0, 3, dock_blob, 60, make_pin_slice(0, 24))));
  LATTER_CHECK(shortcuts_state().pinned_items.empty());
  LATTER_CHECK(!parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 1, 3, "", 60, make_pin_slice(24, 24))));
  LATTER_CHECK(shortcuts_state().pinned_items.empty());
  // Final chunk commits all 60 pinned items + dock items atomically!
  LATTER_CHECK(parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 2, 3, "", 60, make_pin_slice(48, 12))));
  LATTER_CHECK(shortcuts_state().epoch == 7U && shortcuts_state().rev == 1U);
  LATTER_CHECK(shortcuts_state().pinned_items.size() == 60U);
  LATTER_CHECK(pinned_total_pages() == 10);
  // Duplicate replay is rejected by rev guard
  LATTER_CHECK(!parse_v3_shortcuts_payload(build_sc_chunk(7, 1, 0, 1, dock_blob, 0, "")));

  // Now app:pinned is visible on the Home Dock!
  cnt = collect_effective_dock_items(dock_eff, kMaxEffectiveDockItems);
  LATTER_CHECK(cnt == 4);  // Pinned, Call Thu, Moon Phase, Settings
  LATTER_CHECK(dock_eff[0].route_id == RouteId::APP_PINNED);
  LATTER_CHECK(dock_eff[1].kind == ShortcutKind::ENTITY && dock_eff[1].confirm);
  LATTER_CHECK(dock_eff[2].route_id == RouteId::WEATHER_MOON);
  LATTER_CHECK(dock_eff[3].route_id == RouteId::SETTINGS_ROOT);

  // V3DomainReducer clamps out-of-range pinned_menu_page and marks Pinned dirty
  auto &rt = runtime_state();
  rt.nav.gui_page = PageId::PINNED;
  rt.nav.pinned_menu_page = 25;
  V3ReduceContext rctx{};
  rctx.now_ms = 9000U;
  const auto eff = v3_domain_reducer().reduce("shortcuts", rctx);
  LATTER_CHECK(rt.nav.pinned_menu_page == 9);
  LATTER_CHECK(eff.mark_dirty_full && eff.schedule_disp_update);

  // 3. State-aware confirmation copy & danger classification
  std::string title, l1, l2, verb;
  bool danger = false;
  build_shortcut_confirm_copy(shortcuts_state().pinned_items[1], title, l1, l2, verb, danger);
  LATTER_CHECK(verb == "Turn Off" && !danger);
  ShortcutItem lock_it{ShortcutKind::ENTITY, RouteId::NONE, "lock.front_door", "unlock", "Front Door", "mdi:lock-open", true, 0};
  build_shortcut_confirm_copy(lock_it, title, l1, l2, verb, danger);
  LATTER_CHECK(verb == "Unlock" && danger);

  // 4. Outbound shortcut_action RPC outbox serialization
  struct FakeOutbox {
    std::vector<std::string> frames;
    void publish_state(const std::string &s) { frames.push_back(s); }
  } outbox;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_actions::emit_shortcut_action("script.call_thu", "turn_on", "1234", &outbox);
  LATTER_CHECK(!outbox.frames.empty());
  LATTER_CHECK(outbox.frames.back().find("\"s\":\"shortcut_action\"") != std::string::npos);
  LATTER_CHECK(outbox.frames.back().find("\"entity_id\":\"script.call_thu\"") != std::string::npos);
}

void test_tools_games_launcher_prefs_and_stopwatch_commands() {
  using namespace latter_ui;
  reset_runtime_state();

  // 1. Protocol v3 & PersistedDevicePrefs v16 schema invariants
  static_assert(kPersistedPrefsVersion == 16U, "PersistedDevicePrefs must be bumped to v16");
  static_assert(static_cast<size_t>(DevicePrefsField::TOOLS_ITEMS) == 53U);
  static_assert(static_cast<size_t>(DevicePrefsField::GAMES_ITEMS) == 54U);
  static_assert(static_cast<size_t>(DevicePrefsField::HOME_DATE_TAP_ACTION) == 55U);
  static_assert(static_cast<size_t>(DevicePrefsField::CALENDAR_LOOKAHEAD_DAYS) == 56U);
  static_assert(static_cast<size_t>(DevicePrefsField::CALENDAR_WEEK_START) == 57U);
  static_assert(static_cast<size_t>(DevicePrefsField::BEDTIME_ACTIVE) == 58U);
  static_assert(static_cast<size_t>(DevicePrefsField::BEDTIME_DISPLAY) == 59U);
  static_assert(static_cast<size_t>(DevicePrefsField::BEDTIME_SCHEDULE) == 60U);
  static_assert(static_cast<size_t>(DevicePrefsField::BEDTIME_START_HOUR) == 61U);
  static_assert(static_cast<size_t>(DevicePrefsField::BEDTIME_END_HOUR) == 62U);
  static_assert(static_cast<size_t>(DevicePrefsField::BEDTIME_BRIGHTNESS) == 63U);
  static_assert(static_cast<size_t>(DevicePrefsField::CAMERA_DEFAULT_VIEW) == 64U);
  static_assert(static_cast<size_t>(DevicePrefsField::CAMERA_CAROUSEL_INTERVAL) == 65U);
  static_assert(static_cast<size_t>(DevicePrefsField::CAMERA_FRAMING) == 66U);
  static_assert(static_cast<size_t>(DevicePrefsField::CAMERA_STREAM_MODE) == 67U);
  static_assert(static_cast<size_t>(DevicePrefsField::CAMERA_DOORBELL_POPUP) == 68U);
  static_assert(static_cast<size_t>(DevicePrefsField::SCREENSAVER_PHOTO_CLOCK) == 69U);
  static_assert(static_cast<size_t>(DevicePrefsField::WEATHER_APP_BACKGROUND) == 70U);
  static_assert(static_cast<size_t>(DevicePrefsField::COUNT) == 71U);
  static_assert(static_cast<uint8_t>(PrefKey::TOOLS_ITEMS) == 47U);
  static_assert(static_cast<uint8_t>(PrefKey::GAMES_ITEMS) == 48U);
  static_assert(static_cast<uint8_t>(PrefKey::HOME_DATE_TAP_ACTION) == 49U);
  static_assert(static_cast<uint8_t>(PrefKey::CALENDAR_LOOKAHEAD_DAYS) == 50U);
  static_assert(static_cast<uint8_t>(PrefKey::CALENDAR_WEEK_START) == 51U);
  static_assert(static_cast<uint8_t>(PrefKey::BEDTIME_ACTIVE) == 52U);
  static_assert(static_cast<uint8_t>(PrefKey::BEDTIME_DISPLAY) == 53U);
  static_assert(static_cast<uint8_t>(PrefKey::BEDTIME_SCHEDULE) == 54U);
  static_assert(static_cast<uint8_t>(PrefKey::BEDTIME_START_HOUR) == 55U);
  static_assert(static_cast<uint8_t>(PrefKey::BEDTIME_END_HOUR) == 56U);
  static_assert(static_cast<uint8_t>(PrefKey::BEDTIME_BRIGHTNESS) == 57U);
  static_assert(static_cast<uint8_t>(PrefKey::CAMERA_DEFAULT_VIEW) == 58U);
  static_assert(static_cast<uint8_t>(PrefKey::CAMERA_CAROUSEL_INTERVAL) == 59U);
  static_assert(static_cast<uint8_t>(PrefKey::CAMERA_FRAMING) == 60U);
  static_assert(static_cast<uint8_t>(PrefKey::CAMERA_STREAM_MODE) == 61U);
  static_assert(static_cast<uint8_t>(PrefKey::CAMERA_DOORBELL_POPUP) == 62U);
  static_assert(static_cast<uint8_t>(PrefKey::SCREENSAVER_PHOTO_CLOCK) == 63U);
  static_assert(static_cast<uint8_t>(PrefKey::WEATHER_APP_BACKGROUND) == 64U);
  static_assert(static_cast<uint8_t>(PrefKey::COUNT) == 65U);

  LATTER_CHECK(std::string_view(kDefaultToolsItems) ==
               "stopwatch,level,flashlight,calculator,dice,coin_flip");
  LATTER_CHECK(std::string_view(kDefaultGamesItems) ==
               "marbles,archery,bricks,chomp,2048,memory");
  LATTER_CHECK(device_prefs_state().tools_items == kDefaultToolsItems);
  LATTER_CHECK(device_prefs_state().games_items == kDefaultGamesItems);
  LATTER_CHECK(std::string_view(device_prefs().tools_items) == kDefaultToolsItems);
  LATTER_CHECK(std::string_view(device_prefs().games_items) == kDefaultGamesItems);
  LATTER_CHECK(persisted_prefs().home_date_tap_action == 1U);
  LATTER_CHECK(std::string_view(pref_home_date_tap_cstr()) == "Overview");

  // 2. CSV normalization: custom ordering, whitespace trim, dedup, calendar exclusion & empty fallback
  LATTER_CHECK(normalize_tools_items_csv(" coin_flip , flashlight , calendar , dice , coin_flip , bogus ") ==
               "coin_flip,flashlight,dice");
  LATTER_CHECK(normalize_tools_items_csv("") == kDefaultToolsItems);
  LATTER_CHECK(normalize_tools_items_csv("calendar,unknown") == kDefaultToolsItems);

  LATTER_CHECK(normalize_games_items_csv(" memory , 2048 , chomp , memory , invalid ") ==
               "memory,2048,chomp");
  LATTER_CHECK(normalize_games_items_csv("") == kDefaultGamesItems);
  LATTER_CHECK(normalize_games_items_csv("chess,checkers") == kDefaultGamesItems);

  // 3. Cold adoption hello dirty_prefs excludes tools_items & games_items unless locally marked dirty
  {
    PersistedDevicePrefs cold{};
    const std::string seed = build_hello_dirty_prefs(cold);
    LATTER_CHECK(seed.find("tools_items=") == std::string::npos);
    LATTER_CHECK(seed.find("games_items=") == std::string::npos);
  }

  // 4. Protocol v3 device_prefs parsing, NVS POD round-trip, empty-string fallback & reducer clamping
  reset_all_v3_rev_guards();
  auto &rt = runtime_state();
  rt.nav.gui_page = PageId::GAMES;
  rt.nav.game_carousel_idx = 5;
  rt.nav.tools_menu_page = 1;

  std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "21";
  dp_tok[static_cast<size_t>(DevicePrefsField::TOOLS_ITEMS)] = "dice,flashlight";
  dp_tok[static_cast<size_t>(DevicePrefsField::GAMES_ITEMS)] = "2048,marbles";
  uint32_t eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(90, 1, dp_tok), eff, 1200U));
  LATTER_CHECK((eff & kPrefEffectNavPolicy) != 0U && (eff & kPrefEffectRedraw) != 0U);
  LATTER_CHECK(std::string_view(persisted_prefs().tools_items) == "dice,flashlight");
  LATTER_CHECK(std::string_view(persisted_prefs().games_items) == "2048,marbles");
  LATTER_CHECK(device_prefs_state().tools_items == "dice,flashlight");
  LATTER_CHECK(device_prefs_state().games_items == "2048,marbles");
  LATTER_CHECK(device_prefs_state().nvs_save_pending);

  V3ReduceContext rctx{};
  rctx.now_ms = 1200U;
  const auto red_eff = v3_domain_reducer().reduce("device_prefs", rctx);
  LATTER_CHECK(red_eff.apply_device_prefs_effects);
  LATTER_CHECK(rt.nav.game_carousel_idx == 1);  // Clamped from 5 -> 1 (2 enabled games: 0..1)
  LATTER_CHECK(rt.nav.tools_menu_page == 0);    // Clamped from 1 -> 0 (3 items: Dice, Flashlight, Calendar = 1 page)

  // POD NVS binary blob round-trip + empty/corrupt NVS buffer fallback to spec defaults
  LATTER_CHECK(save_persisted_prefs());
  alignas(PersistedDevicePrefs) uint8_t raw_nvs[sizeof(PersistedDevicePrefs)]{};
  std::memcpy(raw_nvs, &persisted_prefs(), sizeof(PersistedDevicePrefs));
  PersistedDevicePrefs restored{};
  std::memcpy(&restored, raw_nvs, sizeof(PersistedDevicePrefs));
  LATTER_CHECK(restored.magic == kPersistedPrefsMagic && restored.version == kPersistedPrefsVersion);
  clamp_prefs_to_capabilities(restored);
  LATTER_CHECK(std::string_view(restored.tools_items) == "dice,flashlight");
  LATTER_CHECK(std::string_view(restored.games_items) == "2048,marbles");

  PersistedDevicePrefs empty_nvs{};
  empty_nvs.tools_items[0] = '\0';
  empty_nvs.games_items[0] = '\0';
  clamp_prefs_to_capabilities(empty_nvs);
  LATTER_CHECK(std::string_view(empty_nvs.tools_items) == kDefaultToolsItems);
  LATTER_CHECK(std::string_view(empty_nvs.games_items) == kDefaultGamesItems);

  // Empty wire fields in Protocol v3 device_prefs restore kDefaultToolsItems / kDefaultGamesItems
  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "22";
  dp_tok[static_cast<size_t>(DevicePrefsField::TOOLS_ITEMS)] = "";
  dp_tok[static_cast<size_t>(DevicePrefsField::GAMES_ITEMS)] = "";
  eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(90, 2, dp_tok), eff, 1500U));
  LATTER_CHECK(std::string_view(device_prefs().tools_items) == kDefaultToolsItems);
  LATTER_CHECK(std::string_view(device_prefs().games_items) == kDefaultGamesItems);
  LATTER_CHECK(device_prefs_state().tools_items == kDefaultToolsItems);
  LATTER_CHECK(device_prefs_state().games_items == kDefaultGamesItems);

  // 5. Capability filtering & ordering for Tools (pure 6-utility launcher) and Games
  {
    ToolsLauncherItemSpec items[kMaxToolsLauncherItems]{};
    // Round + IMU with custom order: level, stopwatch, coin_flip = 3 items
    int n = collect_tools_launcher_items_for("level,stopwatch,coin_flip", true, true, items);
    LATTER_CHECK(n == 3);
    LATTER_CHECK(items[0].id == ToolsSubpage::LEVEL && std::string_view(items[0].label) == "Level");
    LATTER_CHECK(items[1].id == ToolsSubpage::STOPWATCH && std::string_view(items[1].label) == "Stopwatch");
    LATTER_CHECK(items[2].id == ToolsSubpage::COIN_FLIP && std::string_view(items[2].label) == "Coin Flip");

    // Rectangular non-IMU board with only stopwatch,level configured -> 0 items with safe 1-page clamp!
    n = collect_tools_launcher_items_for("stopwatch,level", false, false, items, kMaxToolsLauncherItems);
    LATTER_CHECK(n == 0 && tools_launcher_page_count(n) == 1);

    // Default 6 utility tools on Round + IMU -> 6 items on 1 page
    n = collect_tools_launcher_items_for(kDefaultToolsItems, true, true, items);
    LATTER_CHECK(n == 6 && tools_launcher_page_count(n) == 1);
    // Default on Rectangular non-IMU -> 4 utility tools on 1 page
    n = collect_tools_launcher_items_for(kDefaultToolsItems, false, false, items);
    LATTER_CHECK(n == 4 && tools_launcher_page_count(n) == 1);

    int g_ids[kMaxGamesLauncherItems]{};
    const int gc = collect_enabled_games_for("memory,bricks,chomp", g_ids);
    LATTER_CHECK(gc == 3 && g_ids[0] == 6 && g_ids[1] == 3 && g_ids[2] == 4);
    LATTER_CHECK(collect_enabled_games_for("unknown", g_ids) == 0);
  }

  // 6. Stopwatch live timer_alarm_state transitions, >3s drift, reconnect force_if_active, and co-existence with timers
  reset_runtime_state();
  MockRpcOutbox sw_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  link_state().ha_connected = true;
  auto &rs = runtime_state();

  // Idle -> no emission
  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(1000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.empty());

  // Start stopwatch at t=1000ms -> emits stopwatch_running=1, stopwatch_paused=0, stopwatch_elapsed_sec=0
  rs.stopwatch.running = true;
  rs.stopwatch.start_time_ms = 1000U;
  rs.stopwatch.elapsed_ms = 0U;
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(1000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 1U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_running\":\"1\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_paused\":\"0\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_elapsed_sec\":\"0\"") != std::string::npos);

  // Steady 1 Hz advance (+3s -> elapsed 3s) does NOT emit Wi-Fi traffic
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(4000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 1U);

  // Even while a countdown timer is running steadily, a >3s stopwatch jump MUST still emit!
  rs.timers.running = true;
  rs.timers.remaining_sec = 60;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(5000U, false, &sw_box));  // timer start transition
  LATTER_CHECK(sw_box.published.size() == 2U);

  rs.timers.remaining_sec = 59;        // steady timer (-1s)
  rs.stopwatch.start_time_ms = 0U;     // jump stopwatch elapsed from ~4s -> 6s? Let's jump to 25s at t=25000 while timer matches!
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  rs.timers.remaining_sec = 58;
  rs.stopwatch.start_time_ms = 1000U;  // at t=7000ms expected sw is 4 + 2 = 6s; simulate clock jump by shifting start_time_ms
  // Shift start_time_ms back so cur_sw_sec = 22s vs expected 6s (>3s drift) while timer_rem=58s matches expected!
  LATTER_CHECK(!latter_actions::check_and_emit_timer_alarm_state(7000U, false, &sw_box));
  rs.stopwatch.start_time_ms = 0U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(25000U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 3U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_elapsed_sec\":\"25\"") != std::string::npos);

  // Pause stopwatch at 27,400ms -> emits stopwatch_running=0, stopwatch_paused=1, stopwatch_elapsed_sec=27
  rs.timers.running = false;
  rs.stopwatch.running = false;
  rs.stopwatch.elapsed_ms = 27400U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(27400U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 4U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_running\":\"0\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_paused\":\"1\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_elapsed_sec\":\"27\"") != std::string::npos);

  // Reconnect force_if_active while stopwatch is paused -> emits immediately!
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(28000U, /*force_if_active=*/true, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 5U);

  // Paused elapsed_sec change (27s -> 42s) -> emits immediately!
  rs.stopwatch.elapsed_ms = 42000U;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_actions::check_and_emit_timer_alarm_state(28500U, false, &sw_box));
  LATTER_CHECK(sw_box.published.size() == 6U);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_elapsed_sec\":\"42\"") != std::string::npos);

  // 7. Remote latter_device_command handlers: reset_stopwatch, cancel_timer, dismiss_ringing
  // 7a. reset_stopwatch stops & zeroes stopwatch, marks UI dirty, and emits 0s state
  dirty_band() = DirtyBandTracker{};
  dirty_band().begin_frame(PageId::TIME, 3, 0U, false);
  mark_dirty_band(10, 30);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  sw_box.published.clear();
  const auto rst_sw_eff = latter_actions::handle_device_cmd_reset_stopwatch(29000U, &sw_box);
  LATTER_CHECK(rst_sw_eff.apply_power && !rst_sw_eff.stop_sound);
  LATTER_CHECK(!rs.stopwatch.running && rs.stopwatch.elapsed_ms == 0U && rs.stopwatch.lap_count == 0U);
  dirty_band().begin_frame(PageId::TIME, 3, 0U, false);
  LATTER_CHECK(dirty_band().is_full());
  LATTER_CHECK(!sw_box.published.empty());
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_running\":\"0\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_paused\":\"0\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"stopwatch_elapsed_sec\":\"0\"") != std::string::npos);

  // 7b. cancel_timer stops & resets countdown slots to duration_sec, clears timer ringing, marks dirty & emits
  timer_slots()[0].duration_sec = 300;
  timer_slots()[0].remaining_sec = 95;
  timer_slots()[0].running = true;
  timer_slots()[0].target_ms = 125000U;
  timer_slots()[1].duration_sec = 600;
  timer_slots()[1].remaining_sec = 210;
  timer_slots()[1].paused = true;
  rs.timers.ringing = true;
  rs.timers.va_timer_name = "Tea";
  rs.nav.home_subpage = 2;
  sync_multi_timer_state();
  mark_dirty_band(20, 40);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  sw_box.published.clear();

  const auto can_tmr_eff = latter_actions::handle_device_cmd_cancel_timer(30000U, &sw_box);
  LATTER_CHECK(can_tmr_eff.stop_sound && can_tmr_eff.sync_brightness && can_tmr_eff.apply_power);
  LATTER_CHECK(!rs.timers.running && !rs.timers.paused && !rs.timers.ringing);
  LATTER_CHECK(timer_slots()[0].remaining_sec == 300 && !timer_slots()[0].running);
  LATTER_CHECK(timer_slots()[1].remaining_sec == 600 && !timer_slots()[1].paused);
  LATTER_CHECK(rs.timers.va_timer_name.empty() && rs.nav.home_subpage == 0);
  dirty_band().begin_frame(PageId::TIME, 1, 0U, false);
  LATTER_CHECK(dirty_band().is_full());
  LATTER_CHECK(!sw_box.published.empty());
  LATTER_CHECK(sw_box.published.back().find("\"timer_running\":\"0\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"timer_paused\":\"0\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"remaining_sec\":\"0\"") != std::string::npos);
  LATTER_CHECK(sw_box.published.back().find("\"ringing\":\"0\"") != std::string::npos);

  // 7c. dismiss_ringing silences Once alarm (disabling slot + syncing alarms_summary) & emits ringing=0
  auto &prefs = persisted_prefs();
  prefs.alarms[1].hour = 6;
  prefs.alarms[1].minute = 45;
  prefs.alarms[1].enabled = true;
  prefs.alarms[1].recurrence = 3;  // Once
  rs.alarms.ringing = true;
  rs.alarms.ringing_idx = 2;
  rs.alarms.snooze_until = 99999U;
  rs.timers.ringing = true;
  mark_dirty_band(10, 40);
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  sw_box.published.clear();

  const auto dis_eff = latter_actions::handle_device_cmd_dismiss_ringing(35000U, &sw_box);
  LATTER_CHECK(dis_eff.stop_sound && dis_eff.sync_brightness && dis_eff.apply_power);
  LATTER_CHECK(!rs.alarms.ringing && !rs.timers.ringing && rs.alarms.snooze_until == 0U);
  LATTER_CHECK(!prefs.alarms[1].enabled);
  dirty_band().begin_frame(PageId::TIME, 2, 0U, false);
  LATTER_CHECK(dirty_band().is_full());

  uint32_t pump_ms = 35100U;
  while (latter_rpc::has_pending_frames()) {
    latter_rpc::step_outbox_loop(pump_ms);
    pump_ms += 25U;
  }
  std::string all_frames;
  for (const auto &fr : sw_box.published) all_frames += fr;
  LATTER_CHECK(all_frames.find("set_device_prefs") != std::string::npos);
  LATTER_CHECK(all_frames.find("timer_alarm_state") != std::string::npos);
  LATTER_CHECK(all_frames.find("\"ringing\":\"0\"") != std::string::npos);

  // 8. Source-level guards for system_core.yaml, page_tools.cpp, page_games.cpp & D-057
  const std::string sys_yaml = read_repo_file("../../modules/system_core.yaml");
  const std::string tools_cpp = read_repo_file("../../components/latter_ui/page_tools.cpp");
  const std::string games_cpp = read_repo_file("../../components/latter_ui/page_games.cpp");
  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");
  LATTER_CHECK(sys_yaml.find("sv_cmd == \"dismiss_ringing\"") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("sv_cmd == \"cancel_timer\"") != std::string::npos);
  LATTER_CHECK(sys_yaml.find("sv_cmd == \"reset_stopwatch\"") != std::string::npos);
  LATTER_CHECK(tools_cpp.find("collect_tools_launcher_items(") != std::string::npos);
  LATTER_CHECK(games_cpp.find("collect_enabled_games(") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-057:") != std::string::npos);
}

void test_calendar_app_d058() {
  using namespace latter_ui;
  reset_runtime_state();
  reset_all_v3_rev_guards();

  // 1. Hit-table capacity & schema invariants
  static_assert(FrameHitTable::kMaxRegions == 56U);
  static_assert(static_cast<size_t>(CalendarField::COUNT) == 7U);
  LATTER_CHECK(snap_calendar_lookahead_days(7) == 7U);
  LATTER_CHECK(snap_calendar_lookahead_days(12) == 14U);
  LATTER_CHECK(snap_calendar_lookahead_days(30) == 30U);
  LATTER_CHECK(snap_calendar_lookahead_days(90) == 60U);

  // 2. Civil calendar arithmetic (leap year, month clamping, Sun/Mon week & 7x6 grid starts)
  static_assert(is_leap_year(2028) && !is_leap_year(2027) && !is_leap_year(2100));
  static_assert(days_in_month(2028, 2) == 29 && days_in_month(2027, 2) == 28);
  LATTER_CHECK(parse_ymd_iso("2026-11-17") == 20261117);
  LATTER_CHECK(parse_ymd_iso("20261117") == 20261117);
  char iso_buf[12]{};
  format_ymd_iso(20261117, iso_buf);
  LATTER_CHECK(std::string_view(iso_buf) == "2026-11-17");
  // 2026-11-17 is a Tuesday (weekday_sun0 == 2)
  LATTER_CHECK(weekday_sun0_from_ymd(20261117) == 2);
  LATTER_CHECK(start_of_week_ymd(20261117, /*monday_first=*/false) == 20261115);  // Sun Nov 15
  LATTER_CHECK(start_of_week_ymd(20261117, /*monday_first=*/true) == 20261116);   // Mon Nov 16
  // Month clamping: Jan 31 -> Feb 29 (leap) vs Feb 28 (non-leap)
  LATTER_CHECK(add_months_ymd(20280131, 1) == 20280229);
  LATTER_CHECK(add_months_ymd(20270131, 1) == 20270228);
  // Nov 1, 2026 is Sunday -> Sun-first grid starts Nov 1; Mon-first grid starts Mon Oct 26
  LATTER_CHECK(start_of_week_ymd(20261101, false) == 20261101);
  LATTER_CHECK(start_of_week_ymd(20261101, true) == 20261026);

  // 3. Protocol v3 calendar_agenda sources + locations_catalog parsing
  std::vector<std::string> cal_tok(static_cast<size_t>(CalendarField::COUNT), "");
  cal_tok[static_cast<size_t>(CalendarField::SUMMARY)] = "TODAY 14:00|Design Review|Oakland HQ|0";
  cal_tok[static_cast<size_t>(CalendarField::EVENT_COUNT)] = "1";
  cal_tok[static_cast<size_t>(CalendarField::CALENDARS_META)] =
      std::string("calendar.family|Family|0|1|1") + kProtocolRecordSep +
      "calendar.holidays|US Holidays|3|0|0";
  cal_tok[static_cast<size_t>(CalendarField::LOCATIONS_CATALOG)] =
      std::string("Home") + kProtocolRecordSep + "Oakland HQ" + kProtocolRecordSep + "Gym";
  LATTER_CHECK(parse_v3_calendar_payload(build_v3_packet(12, 1, cal_tok)));

  CalendarSourceMeta sources[kMaxCalendarSources]{};
  const int src_cnt = parse_calendar_sources_meta(calendar_state().calendars_meta, sources);
  LATTER_CHECK(src_cnt == 2);
  LATTER_CHECK(std::string_view(sources[0].entity_id) == "calendar.family");
  LATTER_CHECK(sources[0].can_create && sources[0].can_delete && sources[0].color_idx == 0U);
  LATTER_CHECK(!sources[1].can_create && !sources[1].can_delete && sources[1].color_idx == 3U);

  std::string_view locs[kMaxCalendarLocations]{};
  const int loc_cnt = parse_calendar_locations_catalog(calendar_state().locations_catalog, locs);
  LATTER_CHECK(loc_cnt == 3 && locs[1] == "Oakland HQ");

  // 4. Multi-chunk latter_set_calendar_view atomic commit & query helpers
  auto make_ev_rec = [](const char *s_ymd, const char *e_ymd, int s_min, int e_min,
                        int all_day, int cal_i, const char *uid, const char *rec_id,
                        const char *title, const char *loc, const char *desc) {
    std::string r;
    r += s_ymd; r.push_back(kProtocolFieldSep);
    r += e_ymd; r.push_back(kProtocolFieldSep);
    r += std::to_string(s_min); r.push_back(kProtocolFieldSep);
    r += std::to_string(e_min); r.push_back(kProtocolFieldSep);
    r += std::to_string(all_day); r.push_back(kProtocolFieldSep);
    r += std::to_string(cal_i); r.push_back(kProtocolFieldSep);
    r += uid; r.push_back(kProtocolFieldSep);
    r += rec_id; r.push_back(kProtocolFieldSep);
    r += title; r.push_back(kProtocolFieldSep);
    r += loc; r.push_back(kProtocolFieldSep);
    r += desc;
    return r;
  };

  const std::string chunk0 =
      make_ev_rec("2026-11-17", "2026-11-17", -1, -1, 1, 1, "u1", "", "Release Cut", "", "Lockstep") +
      kProtocolRecordSep +
      make_ev_rec("2026-11-17", "2026-11-17", 540, 600, 0, 0, "u2", "", "Morning Sync | Eng", "Lab", "");
  const std::string chunk1 =
      make_ev_rec("2026-11-17", "2026-11-17", 840, 900, 0, 0, "u3", "r3", "Design Review", "Oakland HQ", "Bring prototype") +
      kProtocolRecordSep +
      make_ev_rec("2026-11-25", "2026-11-25", 1080, 1200, 0, 0, "u4", "", "Dinner", "Bistro", "");

  auto &store = calendar_window_store();
  reset_calendar_window_store();
  LATTER_CHECK(store.window_start_ymd == 0 && store.count == 0 && store.staging_req_id == -1);
  LATTER_CHECK(store.events[0].start_min == -1 && store.events[0].end_min == -1);

  // Cold-boot summary preview fallback while window_start_ymd == 0 (zero stack CalendarWindowStore,
  // zero mutation of store.window_start_ymd / store.count).
  {
    const std::string boot_summary =
        std::string("All Day|Board Review|Oakland Lab|1") + kProtocolRecordSep +
        "14:00||HQ|0";
    CalendarWindowEvent *prev = calendar_summary_preview_scratch();
    const int prev_n = populate_calendar_summary_preview(boot_summary, 20261117, prev);
    LATTER_CHECK(prev_n == 2);
    LATTER_CHECK(prev[0].start_ymd == 20261117 && prev[0].end_ymd == 20261117);
    LATTER_CHECK(prev[0].all_day == 1U && prev[0].start_min == -1);
    LATTER_CHECK(std::string_view(prev[0].title) == "Board Review");
    LATTER_CHECK(std::string_view(prev[0].location) == "Oakland Lab");
    LATTER_CHECK(prev[1].all_day == 0U && std::string_view(prev[1].title) == "Event");
    LATTER_CHECK(std::string_view(prev[1].location) == "HQ");
    LATTER_CHECK(store.window_start_ymd == 0 && store.count == 0);
  }
  // Chunk 0 of 2 stages without committing
  LATTER_CHECK(!apply_calendar_view_action(7, 0, 2, "2026-10-15", "2027-01-20", "", chunk0));
  LATTER_CHECK(store.count == 0);
  // Chunk 1 with wrong req_id is rejected and aborts staging
  LATTER_CHECK(!apply_calendar_view_action(99, 1, 2, "2026-10-15", "2027-01-20", "", chunk1));
  LATTER_CHECK(store.count == 0);
  // Valid 2-chunk sequence commits atomically
  LATTER_CHECK(!apply_calendar_view_action(8, 0, 2, "2026-10-15", "2027-01-20", "", chunk0));
  LATTER_CHECK(apply_calendar_view_action(8, 1, 2, "2026-10-15", "2027-01-20", "", chunk1));
  LATTER_CHECK(store.count == 4);
  LATTER_CHECK(store.window_start_ymd == 20261015 && store.window_end_ymd == 20270120);
  // Literal '|' in title is preserved!
  LATTER_CHECK(std::string_view(store.events[1].title) == "Morning Sync | Eng");

  // Day events for 2026-11-17: all-day first, then chronological timed
  int day_idxs[8]{};
  const int day_cnt = collect_calendar_day_events(20261117, day_idxs, 8, store);
  LATTER_CHECK(day_cnt == 3 && day_idxs[0] == 0 && day_idxs[1] == 1 && day_idxs[2] == 2);

  // Overview at 11:00 (660 min) on 2026-11-17 with 7-day lookahead:
  // Morning Sync (540..600) is past -> excluded! Dinner on Nov 25 (> Nov 24) -> excluded!
  // Leaves Release Cut (all-day today) + Design Review (14:00 today) = 2 upcoming events
  int ov_idxs[8]{};
  const int ov_cnt = collect_calendar_overview_events(20261117, 660, 7, ov_idxs, 8, store);
  LATTER_CHECK(ov_cnt == 2 && ov_idxs[0] == 0 && ov_idxs[1] == 2);

  // Month dots on 2026-11-17: distinct cal_idx 1 and 0 = 2 dots
  uint8_t dots[3]{};
  const int dot_cnt = collect_calendar_day_dots(20261117, dots, store);
  LATTER_CHECK(dot_cnt == 2 && dots[0] == 1U && dots[1] == 0U);

  // Map URL push (chunk_count == 0) updates detail_map_url without wiping store.events
  LATTER_CHECK(apply_calendar_view_action(9, 0, 0, "", "", "http://192.168.1.10:8123/api/latter/map/desk?sig=ok", ""));
  LATTER_CHECK(runtime_state().cal.detail_map_url.find("/api/latter/map/desk") != std::string::npos);
  LATTER_CHECK(store.count == 4);

  // 5. Inline STT dictation & idle return-to-home protection while composing
  auto &rt = runtime_state();
  rt.nav.gui_page = PageId::CALENDAR;
  rt.cal.drilldown = CalDrilldown::ADD_STEP1;
  start_calendar_dictation(1U, 5000U, rt);
  LATTER_CHECK(cal_dictation_active(rt));
  LATTER_CHECK(!should_return_to_home_on_idle(rt, persisted_prefs(), false, false));
  LATTER_CHECK(finish_calendar_dictation("  dentist appointment. ", 5800U, rt));
  LATTER_CHECK(!cal_dictation_active(rt));
  LATTER_CHECK(rt.cal.draft_title == "Dentist appointment");

  // 6. Outbound Calendar RPC serialization
  MockRpcOutbox cal_box{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  (void)latter_actions::emit_calendar_fetch_window("2026-10-01", "2027-01-15", 30, &cal_box);
  LATTER_CHECK(!cal_box.published.empty());
  LATTER_CHECK(cal_box.published.back().find("\"mode\":\"window\"") != std::string::npos);

  cal_box.published.clear();
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_actions::emit_calendar_create_event(
      "calendar.family", "Team Sync", false,
      "2026-11-18 09:00:00", "2026-11-18 10:00:00",
      "Oakland HQ", "FREQ=WEEKLY", &cal_box);
  uint32_t pump_ms = 1000U;
  while (latter_rpc::has_pending_frames()) {
    latter_rpc::step_outbox_loop(pump_ms += 100U);
  }
  std::string joined_create_rpc;
  for (const auto &frame : cal_box.published) joined_create_rpc += frame;
  LATTER_CHECK(joined_create_rpc.find("\"s\":\"calendar_create_event\"") != std::string::npos);
  LATTER_CHECK(joined_create_rpc.find("\"rrule\":\"FREQ=WEEKLY\"") != std::string::npos);

  cal_box.published.clear();
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_actions::emit_calendar_delete_event("calendar.family", "u3", "r3", &cal_box);
  LATTER_CHECK(!cal_box.published.empty());
  LATTER_CHECK(cal_box.published.back().find("\"s\":\"calendar_delete_event\"") != std::string::npos);
  LATTER_CHECK(cal_box.published.back().find("\"uid\":\"u3\"") != std::string::npos);

  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");
  LATTER_CHECK(ui_doc.find("### D-058:") != std::string::npos);
}

void test_music_app_v3_d059() {
  using namespace latter_ui;
  reset_runtime_state();
  reset_all_v3_rev_guards();

  struct FlagshipPanel {
    int w;
    int h;
    bool round;
    bool wearable;
    int ppi;
  };
  static constexpr FlagshipPanel kPanels[5] = {
      {466, 466, true, false, 266},   // Round
      {480, 480, false, false, 313},  // Square
      {410, 502, false, true, 315},   // Watch
      {320, 480, false, false, 165},  // Portrait
      {720, 720, false, false, 254},  // Desk
  };

  auto check_zero_aabb_overlaps = [](const FrameHitTable &ht, int w, int h) {
    for (uint8_t i = 0; i < ht.count; ++i) {
      const auto &a = ht.regions[i];
      LATTER_CHECK(a.x0 >= 0 && a.y0 >= 0 && a.x1 < w && a.y1 < h && a.x0 <= a.x1 && a.y0 <= a.y1);
      for (uint8_t j = static_cast<uint8_t>(i + 1U); j < ht.count; ++j) {
        const auto &b = ht.regions[j];
        const bool x_overlap = (a.x0 <= b.x1) && (b.x0 <= a.x1);
        const bool y_overlap = (a.y0 <= b.y1) && (b.y0 <= a.y1);
        LATTER_CHECK(!(x_overlap && y_overlap));
      }
    }
  };

  // 1. Multi-geometry Now Playing, Peer VOLUME + Mode Bar, Floating SCRUB / ACTION_SHEET,
  //    Wide Bezel-Safe List Rows (MUSIC_ROW_PLAY_BASE), Bento Grid & INFO Gear across all 5 panels
  static constexpr uint16_t kLibSheetWids[5] = {
      WidgetId::MUSIC_ACTION_PLAY_NOW,
      WidgetId::MUSIC_ACTION_PLAY_NEXT,
      WidgetId::MUSIC_ACTION_ADD_QUEUE,
      WidgetId::MUSIC_ACTION_REPLACE_QUEUE,
      WidgetId::MUSIC_ACTION_START_RADIO,
  };
  static constexpr uint16_t kQueueSheetWids[5] = {
      WidgetId::MUSIC_ACTION_PLAY_NOW,
      WidgetId::MUSIC_ACTION_PLAY_NEXT,
      WidgetId::MUSIC_ACTION_MOVE_UP,
      WidgetId::MUSIC_ACTION_MOVE_DOWN,
      WidgetId::MUSIC_ACTION_REMOVE_QUEUE,
  };

  auto pad_w = [](const HitRegion *r) {
    return r->width() + 2 * static_cast<int>(r->hit_pad);
  };
  auto pad_h = [](const HitRegion *r) {
    return r->height() + 2 * static_cast<int>(r->hit_pad);
  };

  for (const auto &p : kPanels) {
    const int min_hit = mm_to_px_for(7.0f, p.ppi);
    const auto ly = compute_panel_layout(p.w, p.h, p.round, p.wearable, p.ppi);
    const auto np = compute_music_now_playing_geom(p.w, p.h, p.round, p.wearable, p.ppi);
    LATTER_CHECK(np.prog_h == ((p.w >= 640) ? 6 : 4));
    LATTER_CHECK(np.scrub_open_y0 + np.scrub_open_h <= music_metadata_band_y1_for(p.h));
    LATTER_CHECK(np.time_y < music_metadata_band_y1_for(p.h));

    // All 6 sliding windows (win_start in 0..5) of the 9-slot stadium mode bar (0..8, SETTINGS = 8)
    for (int win_start = 0; win_start <= 5; ++win_start) {
      FrameHitTable ht{};
      register_music_now_playing_hits(ht, np, true, true, MusicTakeover::NONE, win_start, true);
      check_zero_aabb_overlaps(ht, p.w, p.h);

      const HitRegion *scrub_open = ht.find_by_id(WidgetId::MUSIC_SCRUB_OPEN_BTN);
      const HitRegion *prev_b = ht.find_by_id(WidgetId::MUSIC_PREV_BTN);
      const HitRegion *play_b = ht.find_by_id(WidgetId::MUSIC_PLAY_PAUSE);
      const HitRegion *next_b = ht.find_by_id(WidgetId::MUSIC_NEXT_BTN);
      LATTER_CHECK(scrub_open != nullptr && prev_b != nullptr && play_b != nullptr && next_b != nullptr);
      LATTER_CHECK(pad_w(scrub_open) >= min_hit && pad_h(scrub_open) >= min_hit);
      LATTER_CHECK(pad_w(prev_b) >= min_hit && pad_h(prev_b) >= min_hit);
      LATTER_CHECK(pad_w(play_b) >= min_hit && pad_h(play_b) >= min_hit);
      LATTER_CHECK(pad_w(next_b) >= min_hit && pad_h(next_b) >= min_hit);

      for (int i = 0; i < 4; ++i) {
        const HitRegion *slot_r = ht.find_by_id(static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i));
        LATTER_CHECK(slot_r != nullptr && slot_r->param == win_start + i);
        LATTER_CHECK(pad_w(slot_r) >= min_hit && pad_h(slot_r) >= min_hit);
      }
      if (win_start == 0) {
        LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_RIGHT) != nullptr);
        LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_LEFT) == nullptr);
      } else if (win_start == 5) {
        LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_LEFT) != nullptr);
        LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_RIGHT) == nullptr);
        LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_SLOT_BASE + 3)->param ==
                     static_cast<int16_t>(MusicView::SETTINGS));
      } else {
        LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_LEFT) != nullptr);
        LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_RIGHT) != nullptr);
      }
    }

    // Art Only idle (!show_metadata && !show_controls): single full-screen MUSIC_ART_TAP wake region
    {
      FrameHitTable ht{};
      register_music_now_playing_hits(ht, np, false, false, MusicTakeover::NONE, 0, true);
      LATTER_CHECK(ht.count == 1 && ht.find_by_id(WidgetId::MUSIC_ART_TAP) != nullptr);
    }

    // Peer Tab 3 (MusicView::VOLUME): >= 64 px (96 px Desk) vertical slider + >= 7 mm targets
    // coexisting cleanly ABOVE the 9-slot Stadium Mode Bar with zero AABB overlaps!
    {
      FrameHitTable ht{};
      const auto vg = compute_music_volume_geom(p.w, p.h, p.round, p.wearable, p.ppi);
      LATTER_CHECK(vg.slider_w >= ((p.w >= 640) ? 96 : 64));
      LATTER_CHECK(vg.slider_y0 + vg.slider_h < vg.mode_bar_top);
      register_music_volume_hits(ht, vg);
      for (int i = 0; i < 4; ++i) {
        ht.add_circle(np.mode_slot_xs[i], np.mode_bar_cy, np.mode_bar_r,
                      static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                      kHitTap | kHitFocusSlot | kHitFocusCircle,
                      static_cast<int16_t>(i), np.mode_bar_pad);
      }
      check_zero_aabb_overlaps(ht, p.w, p.h);

      LATTER_CHECK(ht.find_by_id(WidgetId::MUSIC_TAKEOVER_BACKDROP) == nullptr);
      LATTER_CHECK(ht.find_by_id(WidgetId::MODE_BAR_SLOT_BASE + 3) != nullptr);
      const HitRegion *vsl = ht.find_by_id(WidgetId::MUSIC_VOL_SLIDER);
      const HitRegion *vup = ht.find_by_id(WidgetId::MUSIC_VOL_UP_STEP);
      const HitRegion *vdn = ht.find_by_id(WidgetId::MUSIC_VOL_DOWN_STEP);
      const HitRegion *vmu = ht.find_by_id(WidgetId::MUSIC_VOL_MUTE_BTN);
      LATTER_CHECK(vsl != nullptr && (vsl->flags & kHitVertSlider) != 0U);
      LATTER_CHECK(vsl->width() >= min_hit && vsl->height() >= min_hit);
      LATTER_CHECK(vup != nullptr && vup->width() >= min_hit && vup->height() >= min_hit);
      LATTER_CHECK(vdn != nullptr && vdn->width() >= min_hit && vdn->height() >= min_hit);
      LATTER_CHECK(vmu != nullptr && vmu->width() >= min_hit && vmu->height() >= min_hit);
      LATTER_CHECK(std::fabs((1.0f - vsl->norm_y(vsl->y0)) - 1.0f) < 0.001f);
      LATTER_CHECK(std::fabs((1.0f - vsl->norm_y(vsl->y1)) - 0.0f) < 0.001f);
    }

    // SCRUB floating takeover: horizontal seek slider + -15s/+30s + Shuffle/Repeat/Like + backdrop dismiss
    {
      FrameHitTable ht{};
      register_music_now_playing_hits(ht, np, true, true, MusicTakeover::SCRUB, 0, true);
      LATTER_CHECK(ht.count == 0);
      const auto sg = compute_music_scrub_geom(p.w, p.h, p.round, p.wearable, p.ppi);
      if (p.w == 720 && p.h == 720) LATTER_CHECK(sg.slider_w <= 490);
      register_music_scrub_hits(ht, sg);
      check_zero_aabb_overlaps(ht, p.w, p.h);

      LATTER_CHECK(ht.find_by_id(WidgetId::MUSIC_PLAY_PAUSE) == nullptr);
      const HitRegion *bd = ht.hit_test(2, 2, kHitTap);
      LATTER_CHECK(bd != nullptr && bd->widget_id == WidgetId::MUSIC_TAKEOVER_BACKDROP);

      for (uint16_t wid : {WidgetId::MUSIC_SCRUB_BAR, WidgetId::MUSIC_SCRUB_STEP_BACK,
                           WidgetId::MUSIC_SCRUB_STEP_FWD, WidgetId::MUSIC_SHUFFLE_BTN,
                           WidgetId::MUSIC_REPEAT_BTN, WidgetId::MUSIC_FAVORITE_BTN}) {
        const HitRegion *r = ht.find_by_id(wid);
        LATTER_CHECK(r != nullptr && r->width() >= min_hit && r->height() >= min_hit);
      }
      LATTER_CHECK((ht.find_by_id(WidgetId::MUSIC_SCRUB_BAR)->flags & kHitHorizSlider) != 0U);

      // Dynamic/radio queue or non-shuffle/repeat player omits locked hits without exposing backdrop inside bounds
      FrameHitTable ht_locked{};
      register_music_scrub_hits(ht_locked, sg, /*can_shuffle=*/false, /*can_repeat=*/false);
      check_zero_aabb_overlaps(ht_locked, p.w, p.h);
      LATTER_CHECK(ht_locked.find_by_id(WidgetId::MUSIC_SHUFFLE_BTN) == nullptr);
      LATTER_CHECK(ht_locked.find_by_id(WidgetId::MUSIC_REPEAT_BTN) == nullptr);
      LATTER_CHECK(ht_locked.find_by_id(WidgetId::MUSIC_FAVORITE_BTN) != nullptr);
      LATTER_CHECK(ht_locked.hit_test(sg.shuf_x0 + sg.mode_btn_w / 2, sg.mode_y0 + sg.mode_h / 2, kHitTap) == nullptr);
      LATTER_CHECK(ht_locked.hit_test(sg.rep_x0 + sg.mode_btn_w / 2, sg.mode_y0 + sg.mode_h / 2, kHitTap) == nullptr);
    }

    // ACTION_SHEET floating takeover (D-072: canonical slot5_* list_card_w x slot5_h stack + < Cancel, zero underlying rows)
    for (const uint16_t *wids : {kLibSheetWids, kQueueSheetWids}) {
      FrameHitTable ht{};
      const auto ag = compute_music_action_sheet_geom(p.w, p.h, p.round, p.wearable, p.ppi, 5);
      for (int i = 0; i < 5; ++i) {
        LATTER_CHECK(ag.item_w[i] == ly.list_card_w && ag.item_h[i] == ly.slot5_h);
        LATTER_CHECK(ag.item_y0[i] == ly.slot5_y(i) - ly.slot5_h / 2);
        LATTER_CHECK(ag.item_x0[i] == ag.item_x0[0]);
      }
      register_music_action_sheet_hits(ht, ag, wids);
      ht.add_rect(p.w / 2 - 80, ag.back_hit_y0, 160, p.h - ag.back_hit_y0, WidgetId::BACK_BUTTON, kHitTap);
      check_zero_aabb_overlaps(ht, p.w, p.h);
      const HitRegion *bd = ht.hit_test(2, 2, kHitTap);
      LATTER_CHECK(bd != nullptr && bd->widget_id == WidgetId::MUSIC_TAKEOVER_BACKDROP);
      const int min_row_hit_h = std::min(min_hit, ly.slot5_stride - 2);
      for (int i = 0; i < 5; ++i) {
        const HitRegion *r = ht.find_by_id(wids[i]);
        LATTER_CHECK(r != nullptr && pad_w(r) >= min_hit && pad_h(r) >= min_row_hit_h);
      }
      LATTER_CHECK(ht.find_by_id(WidgetId::LIST_SLOT_BASE) == nullptr);
    }

    // Universal slot5_* List Rows (D-072): exact ly.list_card_w x ly.slot5_h across all 5 boards + Leading 1-Tap Play Zone
    {
      FrameHitTable ht{};
      const int c_top = ly.hdr_y + scale_y_for(24, p.h);
      const int c_bot = (p.round ? scale_min_for(404, p.h) : (p.h - scale_y_for(64, p.h))) - scale_y_for(6, p.h);
      for (int r = 0; r < kMusicFullListPageSize; ++r) {
        const auto rg = compute_music_list_row_geom(
            r, kMusicFullListPageSize, c_top, c_bot, /*has_leading_play=*/true, /*has_pips=*/false,
            p.w, p.h, p.round, p.wearable, p.ppi);
        LATTER_CHECK(rg.card_w == ly.list_card_w && rg.card_h == ly.slot5_h);
        LATTER_CHECK(rg.card_x0 == (p.w - ly.list_card_w) / 2);
        LATTER_CHECK(rg.cy_row == ly.slot5_y(r));
        register_music_list_row_hits(ht, rg, r, /*enable_long_press=*/true);
      }
      check_zero_aabb_overlaps(ht, p.w, p.h);
      for (int r = 0; r < kMusicFullListPageSize; ++r) {
        const HitRegion *play_z = ht.find_by_id(static_cast<uint16_t>(WidgetId::MUSIC_ROW_PLAY_BASE + r));
        const HitRegion *body_z = ht.find_by_id(static_cast<uint16_t>(WidgetId::LIST_SLOT_BASE + r));
        LATTER_CHECK(play_z != nullptr && body_z != nullptr);
        LATTER_CHECK(play_z->width() >= min_hit && (play_z->flags & kHitLongPress) != 0U);
        LATTER_CHECK((body_z->flags & kHitLongPress) != 0U);
      }

      // Peer 4-row stack (LIBRARY / SPEAKERS / QUEUE / ALBUM_TRACKS / SETTINGS) above Mode Bar:
      // exact ly.list_card_w x ly.slot5_h across all 5 boards, zero overlaps with chevrons or Mode Bar!
      const int min_s = std::min(p.w, p.h);
      const int mb_top = ly.app_dock_y - ly.dock_r - scale_s_for(14, min_s) - scale_y_for(8, p.h);
      for (bool pips : {false, true}) {
        for (int r = 0; r < kMusicPeerListPageSize; ++r) {
          const auto rg = compute_music_list_row_geom(
              r, kMusicPeerListPageSize, c_top, mb_top, false, pips, p.w, p.h, p.round, p.wearable, p.ppi);
          LATTER_CHECK(rg.card_w == ly.list_card_w && rg.card_h == ly.slot5_h);
          LATTER_CHECK(rg.card_x0 == (p.w - ly.list_card_w) / 2);
          LATTER_CHECK(rg.cy_row == ly.slot5_y(r));
        }
      }

      // Peer Tab 8 (MusicView::SETTINGS) & Peer Tab 1 (MusicView::LIBRARY): 4 standard slot5_* rows + Mode Bar
      // (zero AABB overlap between rows and Mode Bar; >= 14 px visual clearance from right-edge chevron column;
      //  zero AABB overlap between CHEVRON_DOWN and Mode Bar)
      FrameHitTable sht{};
      int pm_x = 0, pc_x = 0, ph_x0 = 0, ph_w = 0, pm_h = 0;
      compute_vertical_page_pips_x(p.w, p.round, p.ppi, pm_x, pc_x, ph_x0, ph_w, pm_h);
      for (int i = 0; i < 4; ++i) {
        const auto sg4 = compute_music_list_row_geom(
            i, 4, c_top, mb_top, false, true, p.w, p.h, p.round, p.wearable, p.ppi);
        LATTER_CHECK(sg4.card_w == ly.list_card_w && sg4.card_h == ly.slot5_h);
        LATTER_CHECK(sg4.cy_row == ly.slot5_y(i));
        LATTER_CHECK(sg4.card_x0 + sg4.card_w <= pm_x);
        register_music_list_row_hits(sht, sg4, i, false);
      }
      for (int i = 0; i < 4; ++i) {
        sht.add_circle(np.mode_slot_xs[i], np.mode_bar_cy, np.mode_bar_r,
                       static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                       kHitTap | kHitFocusSlot | kHitFocusCircle,
                       static_cast<int16_t>(5 + i), np.mode_bar_pad);
      }
      check_zero_aabb_overlaps(sht, p.w, p.h);

      FrameHitTable cht{};
      const int mode_hit_top = std::min(ly.app_dock_y - ly.dock_r - scale_s_for(14, min_s),
                                        ly.app_dock_y - ly.dock_r - scale_y_for(12, p.h));
      const int pip_top = scale_y_for(68, p.h);
      const int pip_mid = (pip_top + std::max(pip_top + 2 * pm_h, mode_hit_top - 2)) / 2;
      cht.add_rect(ph_x0, pip_mid, ph_w, std::max(8, mode_hit_top - pip_mid), WidgetId::CHEVRON_DOWN, kHitTap);
      for (int i = 0; i < 4; ++i) {
        cht.add_circle(np.mode_slot_xs[i], np.mode_bar_cy, np.mode_bar_r,
                       static_cast<uint16_t>(WidgetId::MODE_BAR_SLOT_BASE + i),
                       kHitTap | kHitFocusSlot | kHitFocusCircle,
                       static_cast<int16_t>(5 + i), np.mode_bar_pad);
      }
      check_zero_aabb_overlaps(cht, p.w, p.h);

      const auto ig = compute_music_info_geom(p.w, p.h, p.round, p.wearable, p.ppi);
      LATTER_CHECK(ig.vp_bot_ambient > ig.vp_bot_hud);
    }
  }

  // 2. 9 Peer Tabs (0..8, SETTINGS = 8), Sliding 4-Slot Window (0..5), Page-Size Parity & Auto-Fade/Spring-Back
  {
    static_assert(kMusicPeerTabCount == 9);
    static_assert(static_cast<int>(MusicView::SETTINGS) == 8);

    LATTER_CHECK(step_music_peer_tab(MusicView::NOW_PLAYING, +1) == MusicView::LIBRARY);
    LATTER_CHECK(step_music_peer_tab(MusicView::NOW_PLAYING, -1) == MusicView::SETTINGS);
    LATTER_CHECK(step_music_peer_tab(MusicView::VOLUME, +1) == MusicView::QUEUE);
    LATTER_CHECK(step_music_peer_tab(MusicView::INFO, +1) == MusicView::SETTINGS);
    LATTER_CHECK(step_music_peer_tab(MusicView::SETTINGS, +1) == MusicView::NOW_PLAYING);

    // Stepping forward 0 -> 1 -> ... -> 8 -> 0 glides the 4-slot window smoothly through 0..5!
    MusicRuntimeState slide_m{};
    for (int step = 0; step < kMusicPeerTabCount; ++step) {
      slide_m.view = static_cast<MusicView>(step);
      slide_m.sync_carousel_for_view();
      const int expected_win = (step < 4) ? 0 : (step - 3);
      LATTER_CHECK(slide_m.mode_bar_win_start == expected_win);
      LATTER_CHECK(slide_m.carousel_page == static_cast<uint8_t>(expected_win));
      LATTER_CHECK(step >= slide_m.mode_bar_win_start && step <= slide_m.mode_bar_win_start + 3);
    }
    // Wrapping 8 -> 0 snaps window cleanly back to 0; wrapping 0 -> 8 glides window to 5 (5..8)
    slide_m.view = step_music_peer_tab(MusicView::SETTINGS, +1);
    slide_m.sync_carousel_for_view();
    LATTER_CHECK(slide_m.view == MusicView::NOW_PLAYING && slide_m.mode_bar_win_start == 0);
    slide_m.view = step_music_peer_tab(MusicView::NOW_PLAYING, -1);
    slide_m.sync_carousel_for_view();
    LATTER_CHECK(slide_m.view == MusicView::SETTINGS && slide_m.mode_bar_win_start == 5);

    static_assert(music_browse_page_size_for(MusicView::SPEAKERS, MusicDrilldown::NONE, "speakers") == 4);
    static_assert(music_browse_page_size_for(MusicView::QUEUE, MusicDrilldown::NONE, "active_queue") == 4);
    static_assert(music_browse_page_size_for(MusicView::ALBUM_TRACKS, MusicDrilldown::NONE, "current_album_tracks") == 4);
    static_assert(music_browse_page_size_for(MusicView::LIBRARY, MusicDrilldown::LIB_LIST, "album") == 5);
    static_assert(music_browse_page_size_for(MusicView::INFO, MusicDrilldown::NONE, "bio") == 5);
    static_assert(is_music_playable_container_type("album"));
    static_assert(is_music_playable_container_type("artist"));
    static_assert(is_music_playable_container_type("playlist"));
    static_assert(!is_music_playable_container_type("podcast"));
    static_assert(!is_music_playable_container_type("track"));

    DeviceRuntimeState st{};
    st.nav.gui_page = PageId::MUSIC;
    std::vector<int> peer_ctxs;
    for (int t = 0; t < kMusicPeerTabCount; ++t) {
      st.music.view = static_cast<MusicView>(t);
      st.music.drilldown = MusicDrilldown::NONE;
      st.music.takeover = MusicTakeover::NONE;
      st.music.settings_page = 1;
      st.music.sync_carousel_for_view();
      if (st.music.view == MusicView::ALBUM_TRACKS) {
        st.music.selected_category_mode = "current_album_tracks";
      }
      peer_ctxs.push_back(compute_touch_context(st));
    }
    st.music.view = MusicView::SETTINGS;
    st.music.settings_page = 2;
    st.music.sync_carousel_for_view();
    peer_ctxs.push_back(compute_touch_context(st));
    for (size_t i = 0; i < peer_ctxs.size(); ++i) {
      for (size_t j = i + 1; j < peer_ctxs.size(); ++j) {
        LATTER_CHECK(peer_ctxs[i] != peer_ctxs[j]);
      }
    }
    st.music.view = MusicView::LIBRARY;
    st.music.drilldown = MusicDrilldown::NONE;
    const int lib_root_ctx = compute_touch_context(st);
    st.music.drilldown = MusicDrilldown::LIB_LIST;
    st.music.selected_category_mode = "album";
    LATTER_CHECK(compute_touch_context(st) != lib_root_ctx);
    st.music.apply_route(RouteId::MUSIC_FAVORITES);
    LATTER_CHECK(st.music.view == MusicView::LIBRARY &&
                 st.music.drilldown == MusicDrilldown::LIB_LIST &&
                 st.music.selected_category_mode == "favorites");
    st.music.apply_route(RouteId::MUSIC_INFO);
    LATTER_CHECK(st.music.view == MusicView::INFO &&
                 st.music.drilldown == MusicDrilldown::NONE &&
                 st.music.mode_bar_win_start == 4);

    // Smart Auto-Fade & Spring-Back:
    // - VOLUME springs back to NOW_PLAYING at 5000 ms
    MusicRuntimeState m{};
    m.view = MusicView::VOLUME;
    m.hud_active = true;
    m.hud_timer = 1000U;
    LATTER_CHECK(!step_music_hud_auto_fade(m, 5999U, "Art Only").changed);
    const auto vol_fade = step_music_hud_auto_fade(m, 6000U, "Art Only");
    LATTER_CHECK(vol_fade.changed && vol_fade.sprung_back_to_now_playing);
    LATTER_CHECK(m.view == MusicView::NOW_PLAYING && !m.hud_active);

    // - Transient peer tabs (LIBRARY, SPEAKERS, QUEUE, ALBUM_TRACKS, SETTINGS) spring back at 10000 ms!
    for (MusicView tv : {MusicView::LIBRARY, MusicView::SPEAKERS, MusicView::QUEUE,
                         MusicView::ALBUM_TRACKS, MusicView::SETTINGS}) {
      m = MusicRuntimeState{};
      m.view = tv;
      m.sync_carousel_for_view();
      m.hud_active = true;
      m.hud_timer = 2000U;
      LATTER_CHECK(!step_music_hud_auto_fade(m, 11999U, "Always Visible").changed);
      const auto tr_fade = step_music_hud_auto_fade(m, 12000U, "Always Visible");
      LATTER_CHECK(tr_fade.changed && tr_fade.sprung_back_to_now_playing);
      LATTER_CHECK(m.view == MusicView::NOW_PLAYING && m.carousel_page == 0U && m.mode_bar_win_start == 0);
    }

    // - Ambient LYRICS and INFO fade hud_active -> false at 6000 ms while STAYING on LYRICS / INFO!
    for (MusicView av : {MusicView::LYRICS, MusicView::INFO}) {
      m = MusicRuntimeState{};
      m.view = av;
      m.hud_active = true;
      m.hud_timer = 3000U;
      LATTER_CHECK(!step_music_hud_auto_fade(m, 8999U, "Art + Metadata").changed);
      const auto amb_fade = step_music_hud_auto_fade(m, 9000U, "Art + Metadata");
      LATTER_CHECK(amb_fade.changed && !amb_fade.sprung_back_to_now_playing);
      LATTER_CHECK(m.view == av && !m.hud_active);
    }

    // 480 ms kHitLongPress row hold -> ACTION_SHEET arm/fire without double-tap on release
    TouchGestureState gs{};
    gs.begin(180, 172, 1000U, PageId::MUSIC, 4);
    gs.arm_long_press(WidgetId::MUSIC_ROW_PLAY_BASE + 1, 1);
    LATTER_CHECK(!gs.should_fire_long_press(1479U));
    LATTER_CHECK(gs.should_fire_long_press(1480U));
    gs.mark_long_press_fired();
    LATTER_CHECK(gs.long_press_fired && gs.tap_cancelled && !gs.long_press_armed);
  }

  // 3. Synced LRC multi-chunk parsing, interpolated_position_ms, 240 ms auto-scroll & 6 s manual override
  {
    auto &rs = runtime_state();
    auto &ms = media_state();
    auto &lyr = rs.music.lyrics;
    rs.nav.gui_page = PageId::MUSIC;
    rs.music.view = MusicView::LYRICS;

    ms.title = "Midnight City";
    ms.artist = "M83";
    ms.playing = true;
    ms.state = "playing";
    ms.is_local = false;
    ms.duration = 240.0f;
    ms.position = 10.0f;
    ms.position_updated_ms = 5000U;
    LATTER_CHECK(ms.interpolated_position_ms(6500U) == 11500U);

    const std::string tkey = build_music_track_key(ms.title, ms.artist);
    LATTER_CHECK(tkey == "Midnight City|M83");
    const std::string c0 = std::string("0") + kProtocolFieldSep + "Waiting in a car" +
                           kProtocolRecordSep + "10000" + kProtocolFieldSep + "Waiting for a ride in the dark";
    const std::string c1 = std::string("20000") + kProtocolFieldSep + "The night city grows" +
                           kProtocolRecordSep + "30000" + kProtocolFieldSep + "Look and see her eyes";
    LATTER_CHECK(!apply_music_lyrics_action(4, 0, 2, tkey, true, c0, lyr));
    LATTER_CHECK(apply_music_lyrics_action(4, 1, 2, tkey, true, c1, lyr));
    LATTER_CHECK(lyr.synced && lyr.lines.size() == 4U);
    LATTER_CHECK(lyr.find_active_line(9999U) == 0);
    LATTER_CHECK(lyr.find_active_line(11500U) == 1);
    LATTER_CHECK(lyr.find_active_line(25000U) == 2);
    // Stale lower req_id is rejected
    lyr.latest_req_id = 4;
    LATTER_CHECK(!apply_music_lyrics_action(3, 0, 1, tkey, false, "0\x1fStale", lyr));

    auto lyrics_anim_running = [&](uint32_t now_ms) {
      return (lyr.scroll_px != lyr.target_scroll_px) ||
             (lyr.scroll_anim_start_ms > 0U && (now_ms - lyr.scroll_anim_start_ms) < 260U);
    };

    const int doc_cys[4] = {40, 180, 320, 460};
    const int anchor_vp = 110;
    // Initial entry snaps directly to line 1 (180 - 110 = 70 px)
    step_music_lyrics_auto_scroll(lyr, 11500U, 6500U, anchor_vp, doc_cys, 4, 500);
    LATTER_CHECK(lyr.active_line_idx == 1 && lyr.scroll_px == 70 && lyr.scroll_anim_start_ms == 0U);
    LATTER_CHECK(!lyrics_anim_running(6500U));

    // Line advances to 2 at pos_ms = 20100 -> 240 ms ease_out_cubic toward 320 - 110 = 210 px
    step_music_lyrics_auto_scroll(lyr, 20100U, 15600U, anchor_vp, doc_cys, 4, 500);
    LATTER_CHECK(lyr.active_line_idx == 2 && lyr.target_scroll_px == 210 && lyr.scroll_anim_start_ms == 15600U);
    LATTER_CHECK(lyrics_anim_running(15680U));
    step_music_lyrics_auto_scroll(lyr, 20220U, 15720U, anchor_vp, doc_cys, 4, 500);
    LATTER_CHECK(lyr.scroll_px > 70 && lyr.scroll_px < 210);
    step_music_lyrics_auto_scroll(lyr, 20360U, 15850U, anchor_vp, doc_cys, 4, 500);
    LATTER_CHECK(lyr.scroll_px == 210 && lyr.scroll_anim_start_ms == 0U);
    LATTER_CHECK(!lyrics_anim_running(15850U));

    // Manual drag sets 6 s override; Sync clears it immediately
    lyr.scroll_px = 30;
    lyr.manual_override_until_ms = 22000U;
    step_music_lyrics_auto_scroll(lyr, 31000U, 18000U, anchor_vp, doc_cys, 4, 500);
    LATTER_CHECK(lyr.active_line_idx == 3 && lyr.scroll_px == 30);
    lyr.manual_override_until_ms = 0U;
    lyr.target_scroll_px = -1;
    step_music_lyrics_auto_scroll(lyr, 31000U, 18100U, anchor_vp, doc_cys, 4, 500);
    LATTER_CHECK(lyr.target_scroll_px == 350 && lyr.scroll_anim_start_ms == 18100U);
  }

  // 4. Outbound Music Assistant enqueue / queue / lyrics / seek RPC serialization & 0 FPS Art Only idle
  {
    MockRpcOutbox mbox{};
    latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
    (void)latter_actions::emit_music_lyrics_fetch("Midnight City|M83", "media_player.office", 1000U, &mbox);
    latter_actions::emit_media_play_item("media_player.office", "library://track/42", "track", "next", false, &mbox);
    latter_actions::emit_media_play_item("media_player.office", "library://track/42", "track", "play", true, &mbox);
    latter_actions::emit_media_queue_play("q_item_7", "media_player.office", &mbox);
    latter_actions::emit_media_queue_move("q_item_7", "next", "media_player.office", &mbox);
    latter_actions::emit_media_queue_delete("q_item_7", "media_player.office", &mbox);
    latter_actions::emit_media_seek(20, &mbox);

    uint32_t pump_ms = 1100U;
    while (latter_rpc::has_pending_frames()) {
      latter_rpc::step_outbox_loop(pump_ms += 40U);
    }
    std::string all_rpc;
    for (const auto &f : mbox.published) all_rpc += f;
    LATTER_CHECK(all_rpc.find("\"mode\":\"lyrics_fetch\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"enqueue\":\"next\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"radio_mode\":\"true\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"command\":\"queue_play\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"command\":\"queue_move\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"command\":\"queue_delete\"") != std::string::npos);
    LATTER_CHECK(all_rpc.find("\"seek_position\":\"20\"") != std::string::npos);

    LATTER_CHECK(find_shortcut_route("music:album_tracks") != nullptr);
    LATTER_CHECK(find_shortcut_route("music:lyrics") != nullptr);
    LATTER_CHECK(find_shortcut_route("music:info") != nullptr);

    auto &rt = runtime_state();
    auto &prefs = persisted_prefs();
    prefs.home_screen_enabled = true;
    prefs.return_to_home_timeout = 1;  // non-zero timeout
    rt.nav.gui_page = PageId::MUSIC;
    rt.music.takeover = MusicTakeover::NONE;
    rt.music.view = MusicView::NOW_PLAYING;
    LATTER_CHECK(!should_return_to_home_on_idle(rt, prefs, true, false));
    rt.music.view = MusicView::LYRICS;
    LATTER_CHECK(!should_return_to_home_on_idle(rt, prefs, true, false));
    rt.music.view = MusicView::LIBRARY;
    LATTER_CHECK(should_return_to_home_on_idle(rt, prefs, true, false));
    rt.music.view = MusicView::SETTINGS;
    LATTER_CHECK(should_return_to_home_on_idle(rt, prefs, true, false));

    // Art Only idle cadence: FrameCadenceController stops anim poller and schedules 0 redraws on PageId::MUSIC
    FrameCadenceController fc;
    fc.reset();
    Cadence20msInputs in20{};
    in20.gui_page = PageId::MUSIC;
    in20.now_ms = 20000U;
    in20.speaker_audio_busy = true;
    auto a20_1 = fc.evaluate_20ms(in20);
    in20.now_ms = 20020U;
    auto a20_2 = fc.evaluate_20ms(in20);
    LATTER_CHECK(!a20_1.schedule_anim_full && !a20_2.schedule_anim_full && a20_2.stop_anim_poller);

    Cadence250msInputs in250{};
    in250.gui_page = PageId::MUSIC;
    in250.now_ms = 20250U;
    auto a250 = fc.evaluate_250ms(in250);
    LATTER_CHECK(!a250.ensure_anim_poller && !a250.schedule_disp_now && !a250.schedule_band_now);
  }

  // 5. Zero-legacy & D-043 / D-048 / D-059 / D-060 source-level invariants
  {
    const std::string mu_src = read_repo_file("../../components/latter_ui/page_music.cpp");
    const std::string te_src = read_repo_file("../../components/latter_ui/touch_engine.cpp");
    const std::string comp_src = read_repo_file("../../components/latter_ui/ui_compositor.cpp");
    const std::string gfx_src = read_repo_file("../../components/latter_ui/ui_gfx.cpp");
    const std::string wid_src = read_repo_file("../../components/latter_ui/ui_widgets.cpp");
    const std::string lay_src = read_repo_file("../../include/ui_layout.h");
    const std::string sys_yaml = read_repo_file("../../modules/system_core.yaml");
    const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");

    LATTER_CHECK(mu_src.find("render_progress_arc_r224") == std::string::npos);
    LATTER_CHECK(mu_src.find("overlay_mode") == std::string::npos);
    LATTER_CHECK(mu_src.find("show_speaker_shortcut") == std::string::npos);
    LATTER_CHECK(mu_src.find("show_library_shortcut") == std::string::npos);
    LATTER_CHECK(mu_src.find("GlassVariant::ReadableDark") == std::string::npos);
    LATTER_CHECK(mu_src.find("chrome_modal_bg") == std::string::npos);
    LATTER_CHECK(mu_src.find("MUSIC_ROW_MORE_BASE") == std::string::npos);
    LATTER_CHECK(mu_src.find("MUSIC_HEADER_ENQUEUE_BTN") == std::string::npos);
    LATTER_CHECK(mu_src.find("ly.list_card_w") != std::string::npos);
    LATTER_CHECK(mu_src.find("ly.slot5_h") != std::string::npos);
    LATTER_CHECK(mu_src.find("font_micro") == std::string::npos);
    LATTER_CHECK(mu_src.find("MUSIC_INFO_SETTINGS_BTN") == std::string::npos);
    LATTER_CHECK(mu_src.find("MusicDrilldown::SETTINGS") == std::string::npos);
    LATTER_CHECK(mu_src.find("ui::draw_icon_play_pause(") != std::string::npos);
    LATTER_CHECK(mu_src.find("ui::draw_icon_hamburger(") != std::string::npos);
    LATTER_CHECK(mu_src.find("ui::draw_icon_gear(") != std::string::npos);
    LATTER_CHECK(mu_src.find("render_lyrics_art_backdrop(") != std::string::npos);
    LATTER_CHECK(mu_src.find("register_music_list_row_hits(") != std::string::npos);
    LATTER_CHECK(mu_src.find("case MusicView::SETTINGS:") != std::string::npos);

    LATTER_CHECK(te_src.find("MUSIC_ROW_MORE_BASE") == std::string::npos);
    LATTER_CHECK(te_src.find("MUSIC_INFO_SETTINGS_BTN") == std::string::npos);
    LATTER_CHECK(te_src.find("MusicDrilldown::SETTINGS") == std::string::npos);
    LATTER_CHECK(te_src.find("activate_music_peer_tab(") != std::string::npos);
    LATTER_CHECK(te_src.find("MUSIC_ROW_PLAY_BASE") != std::string::npos);
    LATTER_CHECK(te_src.find("case MusicView::SETTINGS:") != std::string::npos);
    LATTER_CHECK(te_src.find("overlay_mode == 5") == std::string::npos);
    LATTER_CHECK(te_src.find("overlay_mode == 6") == std::string::npos);
    LATTER_CHECK(te_src.find("overlay_mode = 5") == std::string::npos);
    LATTER_CHECK(te_src.find("overlay_mode = 6") == std::string::npos);
    LATTER_CHECK(te_src.find("show_speaker_shortcut") == std::string::npos);
    LATTER_CHECK(te_src.find("show_library_shortcut") == std::string::npos);

    // Unified Mode-Bar & Canvas horizontal swipe: zero separate mode_bar_zone_top() window-only branch!
    const size_t mu_swipe_s = te_src.find("if (rs.nav.gui_page == PageId::MUSIC)");
    const size_t mu_swipe_e = te_src.find("if (rs.music.drilldown == MusicDrilldown::LIB_LIST", mu_swipe_s);
    LATTER_CHECK(mu_swipe_s != std::string::npos && mu_swipe_e != std::string::npos);
    const std::string mu_swipe_blk = te_src.substr(mu_swipe_s, mu_swipe_e - mu_swipe_s);
    LATTER_CHECK(mu_swipe_blk.find("gest.start_y >= ly.mode_bar_zone_top()") == std::string::npos);
    LATTER_CHECK(mu_swipe_blk.find("activate_music_peer_tab(step_music_peer_tab(rs.music.view, h_dir), now_ms)") !=
                 std::string::npos);

    // DockSlideAnimator 9-tab span guard, system_core.yaml SETTINGS check, white headers & uniform 140 HUD scrim
    LATTER_CHECK(lay_src.find("std::abs(new_start - target_start) > 9") != std::string::npos);
    LATTER_CHECK(lay_src.find("std::abs(new_active - target_active) > 9") != std::string::npos);
    LATTER_CHECK(sys_yaml.find("rs.music.view == latter_ui::MusicView::SETTINGS") != std::string::npos);
    LATTER_CHECK(sys_yaml.find("MusicDrilldown::SETTINGS") == std::string::npos);
    LATTER_CHECK(wid_src.find("draw_icon_play_pause(") != std::string::npos);
    LATTER_CHECK(wid_src.find("draw_icon_hamburger(") != std::string::npos);
    const size_t ph_s = wid_src.find("void render_page_header(esphome::display::Display &it, int cx, const char *title");
    const size_t ph_e = wid_src.find("void render_sub_header(", ph_s);
    LATTER_CHECK(ph_s != std::string::npos && ph_e != std::string::npos);
    LATTER_CHECK(wid_src.substr(ph_s, ph_e - ph_s).find("esphome::Color::WHITE") != std::string::npos);

    const size_t art_s = gfx_src.find("compute_art_row_scale_q8(");
    const size_t art_e = gfx_src.find("void art_letterbox_bounds(", art_s);
    LATTER_CHECK(art_s != std::string::npos && art_e != std::string::npos);
    const std::string art_fn = gfx_src.substr(art_s, art_e - art_s);
    LATTER_CHECK(art_fn.find("return 140;") != std::string::npos);
    LATTER_CHECK(art_fn.find("ramp(y, scale_y(104), 140, scale_y(148), 256)") != std::string::npos);
    LATTER_CHECK(art_fn.find("212") == std::string::npos);

    LATTER_CHECK(comp_src.find("if (!show_metadata && !show_controls)") != std::string::npos);
    LATTER_CHECK(comp_src.find("mark_music_progress_dirty_band(") != std::string::npos);

    auto slice_fn = [&](std::string_view sig, std::string_view next_sig) -> std::string {
      const size_t s = mu_src.find(sig);
      const size_t e = mu_src.find(next_sig);
      LATTER_CHECK(s != std::string::npos && e != std::string::npos && e > s);
      return mu_src.substr(s, e - s);
    };

    const std::string scrub_fn = slice_fn("void render_scrub_takeover(", "void render_action_sheet_takeover(");
    const std::string sheet_fn = slice_fn("void render_action_sheet_takeover(", "void render_now_playing_view(");
    const std::string np_fn = slice_fn("void render_now_playing_view(", "void render_volume_peer_tab(");
    const std::string vol_fn = slice_fn("void render_volume_peer_tab(", "void render_lyrics_view(");
    const std::string lyr_fn = slice_fn("void render_lyrics_view(", "void render_music_settings_view(");
    const std::string set_fn = slice_fn("void render_music_settings_view(", "void render_info_view(");
    const std::string info_fn = slice_fn("void render_info_view(", "void render_slot_list_row(");
    const std::string page_fn = mu_src.substr(mu_src.find("void render_music_page("));

    // Zero outer passive glass containers in SCRUB, ACTION_SHEET (only 1 loop render_card_box for
    // squared-off kRadiusCardPx() rows per D-070), LYRICS, and INFO;
    // SETTINGS is Peer Tab 8 with Mode Bar & zero < Back; dim_level = 0 on Now Playing & Volume!
    LATTER_CHECK(scrub_fn.find("render_card_box(") == std::string::npos);
    const size_t first_sheet_card = sheet_fn.find("render_card_box(");
    LATTER_CHECK(first_sheet_card != std::string::npos &&
                 sheet_fn.find("render_card_box(", first_sheet_card + 1U) == std::string::npos &&
                 sheet_fn.find("kRadiusCardPx()") != std::string::npos);
    LATTER_CHECK(lyr_fn.find("render_card_box(") == std::string::npos);
    LATTER_CHECK(info_fn.find("render_card_box(") == std::string::npos);
    LATTER_CHECK(set_fn.find("render_music_stadium_mode_bar(") != std::string::npos);
    LATTER_CHECK(set_fn.find("render_back_button(") == std::string::npos);
    LATTER_CHECK(np_fn.find("/*dim_level=*/0") != std::string::npos);
    LATTER_CHECK(np_fn.find("font_btn_bold") == std::string::npos && np_fn.find("font_date") == std::string::npos);
    LATTER_CHECK(vol_fn.find("/*dim_level=*/0") != std::string::npos);
    LATTER_CHECK(vol_fn.find("font_btn_bold") == std::string::npos);
    const size_t first_vol_card = vol_fn.find("render_card_box(");
    LATTER_CHECK(first_vol_card != std::string::npos &&
                 vol_fn.find("render_card_box(", first_vol_card + 1U) == std::string::npos);

    // Strict ACTION_SHEET early return before any browse list renders (zero buttons-on-buttons!)
    const size_t sheet_guard = page_fn.find("m.takeover == MusicTakeover::ACTION_SHEET");
    const size_t browse_call = page_fn.find("render_music_browse_views(");
    LATTER_CHECK(sheet_guard != std::string::npos && browse_call != std::string::npos &&
                 sheet_guard < browse_call);

    LATTER_CHECK(ui_doc.find("### D-059:") != std::string::npos);
    LATTER_CHECK(ui_doc.find("### D-060:") != std::string::npos);
  }
}

void test_sram_psram_budget_and_http_stall_guards() {
  using namespace latter_ui;

  // 1. S3 (8 MB PSRAM) defaults to a single ArtCanvasCache slot; P4 (32 MB PSRAM) allows 3.
  static_assert(LATTER_CHIP_P4 == 0, "Host default mimics S3 capability baseline");
  static_assert(LATTER_ART_CACHE_ENTRIES == 1, "8 MB PSRAM S3 builds must cap ArtCanvasCache to 1 slot");
  static_assert(!std::is_default_constructible_v<CalendarWindowStore>,
                "CalendarWindowStore (~51 KB) must never be default-constructible on the stack");
  static_assert(!std::is_copy_constructible_v<CalendarWindowStore> &&
                    !std::is_copy_assignable_v<CalendarWindowStore> &&
                    !std::is_move_constructible_v<CalendarWindowStore> &&
                    !std::is_move_assignable_v<CalendarWindowStore>,
                "CalendarWindowStore (~51 KB) must never be copied or moved by value");

  // 2. Online art fetch guard & camera error backoff state
  LATTER_CHECK(can_start_online_art_fetch(false));
  LATTER_CHECK(can_start_online_art_fetch(true));
  cam_error_backoff_until_ms() = 4200U;
  LATTER_CHECK(runtime_state().intercom.cam_error_backoff_until_ms == 4200U);
  cam_error_backoff_until_ms() = 0U;

  // 3. Source-level regressions: zero static .bss or stack bloat for 52 KB CalendarWindowStore / 14 KB MDI scratch /
  //    ArtScaleScratch, 16 KB S3 main-task stack parity, touch GPIO ISR hygiene, mutual sky_base_cache <->
  //    ArtCanvasCache handoff on non-P4, and <=5s HTTP timeout.
  const std::string st_src = read_repo_file("../../include/latter_state.h");
  const std::string cal_src = read_repo_file("../../components/latter_ui/page_calendar.cpp");
  const std::string th_src = read_repo_file("../../components/latter_core/touch_hal.cpp");
  const std::string s3_yaml = read_repo_file("../../modules/hal/chip_esp32s3.yaml");
  const std::string gfx_src = read_repo_file("../../components/latter_ui/ui_gfx.cpp");
  const std::string wth_src = read_repo_file("../../components/latter_ui/page_weather.cpp");
  const std::string mdi_src = read_repo_file("../../components/latter_ui/ui_mdi.cpp");
  const std::string dp_yaml = read_repo_file("../../modules/display_pipeline.yaml");

  LATTER_CHECK(st_src.find("static CalendarWindowStore fallback") == std::string::npos);
  LATTER_CHECK(st_src.find("CalendarWindowStore{}") == std::string::npos);
  LATTER_CHECK(st_src.find("memset(events") == std::string::npos);
  LATTER_CHECK(st_src.find("memset(staging_events") == std::string::npos);
  LATTER_CHECK(st_src.find("memset(&dst") == std::string::npos);
  LATTER_CHECK(st_src.find("memset(&out") == std::string::npos);
  {
    CalendarWindowEvent dirty_ev{};
    dirty_ev.start_ymd = 20261118;
    dirty_ev.end_ymd = 20261119;
    dirty_ev.start_min = 540;
    dirty_ev.end_min = 600;
    dirty_ev.all_day = 1U;
    dirty_ev.cal_idx = 3U;
    copy_bounded_cstr(dirty_ev.uid, "u99");
    copy_bounded_cstr(dirty_ev.recurrence_id, "r99");
    copy_bounded_cstr(dirty_ev.title, "Dirty");
    copy_bounded_cstr(dirty_ev.location, "Lab");
    copy_bounded_cstr(dirty_ev.description, "Notes");
    dirty_ev.reset_in_place();
    LATTER_CHECK(dirty_ev.start_ymd == 0 && dirty_ev.end_ymd == 0);
    LATTER_CHECK(dirty_ev.start_min == -1 && dirty_ev.end_min == -1);
    LATTER_CHECK(dirty_ev.all_day == 0U && dirty_ev.cal_idx == 0U);
    LATTER_CHECK(dirty_ev.uid[0] == '\0' && dirty_ev.recurrence_id[0] == '\0' &&
                 dirty_ev.title[0] == '\0' && dirty_ev.location[0] == '\0' &&
                 dirty_ev.description[0] == '\0');
  }
  LATTER_CHECK(cal_src.find("temp_store") == std::string::npos);
  LATTER_CHECK(cal_src.find("populate_calendar_summary_preview(") != std::string::npos);
  LATTER_CHECK(s3_yaml.find("CONFIG_ESP_MAIN_TASK_STACK_SIZE: \"16384\"") != std::string::npos);
  LATTER_CHECK(th_src.find("IRAM_ATTR") == std::string::npos);
  LATTER_CHECK(mdi_src.find("DecodedMdiEntry g_mdi_cache[") == std::string::npos);
  LATTER_CHECK(mdi_src.find("uint8_t g_bucket_scratch[") == std::string::npos);
  LATTER_CHECK(mdi_src.find("MdiRuntimeScratch *mdi_scratch()") != std::string::npos);
  LATTER_CHECK(gfx_src.find("static uint16_t row_buf[") == std::string::npos);
  LATTER_CHECK(gfx_src.find("static uint16_t sx_lut[") == std::string::npos);
  LATTER_CHECK(gfx_src.find("static uint16_t scaled_row[") == std::string::npos);
  LATTER_CHECK(gfx_src.find("sky_base_cache_drop()") != std::string::npos);
  LATTER_CHECK(gfx_src.find("blit_dimmed_rgb565_scanline(") != std::string::npos);
  LATTER_CHECK(gfx_src.find("full_rect_overwrite") != std::string::npos);
  LATTER_CHECK(gfx_src.find("sample_glass_mean(") != std::string::npos);
  const std::string wid_src = read_repo_file("../../components/latter_ui/ui_widgets.cpp");
  const std::string pm_yaml = read_repo_file("../../modules/power_manager.yaml");
  const std::string base_yaml = read_repo_file("../../modules/base.yaml");
  LATTER_CHECK(wid_src.find("sample_surface ? sample_glass_mean(") != std::string::npos);
  LATTER_CHECK(pm_yaml.find("defined(CONFIG_PM_ENABLE) && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)") != std::string::npos);
  LATTER_CHECK(base_yaml.find("update_interval: 300s") == std::string::npos);
  LATTER_CHECK(wth_src.find("art_canvas_drop_all()") != std::string::npos);
  LATTER_CHECK(dp_yaml.find("timeout: 15s") == std::string::npos);
  LATTER_CHECK(dp_yaml.find("timeout: 4s") != std::string::npos);
}

void test_bedtime_mode_d063() {
  using namespace latter_ui;
  reset_runtime_state();

  // 1. Overnight & same-day window math + edge-triggered BedtimeScheduleTracker manual override
  static_assert(is_hour_in_bedtime_window(22, 22U, 7U));
  static_assert(is_hour_in_bedtime_window(2, 22U, 7U));
  static_assert(!is_hour_in_bedtime_window(7, 22U, 7U));
  static_assert(!is_hour_in_bedtime_window(21, 22U, 7U));
  static_assert(is_hour_in_bedtime_window(1, 1U, 6U));
  static_assert(!is_hour_in_bedtime_window(0, 1U, 6U));
  static_assert(!is_hour_in_bedtime_window(6, 1U, 6U));
  static_assert(!is_hour_in_bedtime_window(22, 22U, 22U));

  BedtimeScheduleTracker tr{};
  LATTER_CHECK(tr.evaluate_hour(false, 22U, 7U, 23) == -1);
  // Enabling schedule at 23:00 immediately enters bedtime (returns 1)
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 23) == 1);
  // Staying within the same window returns -1 so manual HA/on-device override holds until morning!
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 23) == -1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 0) == -1);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 6) == -1);
  // Crossing 07:00 end boundary emits 0 once
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 7) == 0);
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 12) == -1);
  // Crossing 22:00 start boundary emits 1 once
  LATTER_CHECK(tr.evaluate_hour(true, 22U, 7U, 22) == 1);

  // 2. Stationary panel without daytime screensaver still times out after 15 s in Bedtime Mode
  LATTER_CHECK(resolve_idle_screen_timeout_ms(
                   /*is_stationary=*/true, /*usb=*/true, /*screensaver_mode=*/2U,
                   "Never", "30s", /*bedtime_active=*/false) == 0U);
  LATTER_CHECK(resolve_idle_screen_timeout_ms(
                   /*is_stationary=*/true, /*usb=*/true, /*screensaver_mode=*/2U,
                   "Never", "30s", /*bedtime_active=*/true) == kBedtimeIdleTimeoutMs);

  // Wake greeting is suppressed during Bedtime Mode
  DisplayLifecycleController dlc{};
  int nav_ps = 0;
  uint32_t sleep_ts = 1000U;
  uint32_t sb_ts = 1000U;
  bool greet = false;
  DisplaySupervisorInputs d_in{};
  d_in.wake_greeting_enabled = true;
  d_in.bedtime_active = true;
  d_in.panel_brightness_on = true;
  (void)dlc.evaluate_1s(2000000U, d_in, nav_ps, sleep_ts, sb_ts, greet);
  LATTER_CHECK(!greet);

  // 3. Alert suppression: Normal/warning suppressed in Bedtime; Critical breaks through!
  static_assert(should_suppress_alert_in_bedtime(true, AlertPriority::Normal));
  static_assert(should_suppress_alert_in_bedtime(true, parse_alert_priority("warning")));
  static_assert(!should_suppress_alert_in_bedtime(true, AlertPriority::Critical));
  static_assert(!should_suppress_alert_in_bedtime(false, AlertPriority::Normal));

  // 4. Protocol v3 device_prefs round-trip & cold-boot hello exclusion of bedtime_active
  {
    PersistedDevicePrefs cold{};
    const std::string seed = build_hello_dirty_prefs(cold);
    LATTER_CHECK(seed.find("bedtime_active=") == std::string::npos);
  }
  reset_all_v3_rev_guards();
  std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "55";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_ACTIVE)] = "1";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_DISPLAY)] = "Ambient Clock";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_SCHEDULE)] = "1";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_START_HOUR)] = "21";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_END_HOUR)] = "6";
  uint32_t eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(95, 1, dp_tok), eff, 3000U));
  LATTER_CHECK((eff & kPrefEffectBedtime) != 0U && (eff & kPrefEffectBrightness) != 0U);
  const auto &p = persisted_prefs();
  LATTER_CHECK(p.bedtime_active && p.bedtime_schedule);
  LATTER_CHECK(std::string_view(pref_bedtime_display_cstr(p)) == "Ambient Clock");
  LATTER_CHECK(p.bedtime_start_hour == 21U && p.bedtime_end_hour == 6U);
  p_check: {
    PersistedDevicePrefs fp = p;
    fp.screensaver_style = 5U;  // Fireworks in daytime
    LATTER_CHECK(std::string_view(effective_screensaver_style_cstr(fp)) == "Ambient Clock");
    fp.bedtime_active = false;
    LATTER_CHECK(std::string_view(effective_screensaver_style_cstr(fp)) == "Fireworks");
  }
  // 5. Universal 12-hour vs 24-hour formatting on Home Activity Card calendar labels & Display schedule (D-064)
  LATTER_CHECK(format_agenda_time_label("TODAY 14:30", false) == "TODAY 2:30 PM");
  LATTER_CHECK(format_agenda_time_label("TODAY 14:30", true) == "TODAY 14:30");
  LATTER_CHECK(format_agenda_time_label("TODAY 00:05", false) == "TODAY 12:05 AM");
  LATTER_CHECK(format_agenda_time_label("TOM 12:00", false) == "TOM 12:00 PM");
  LATTER_CHECK(format_agenda_time_label("NOV 08 09:15", false) == "NOV 08 9:15 AM");
  LATTER_CHECK(format_agenda_time_label("TODAY ALL DAY", false) == "TODAY ALL DAY");
  LATTER_CHECK(format_agenda_time_label("NOV 08", false) == "NOV 08");

  char sched_buf[36]{};
  format_schedule_range_label(7, 20, false, sched_buf, sizeof(sched_buf));
  LATTER_CHECK(std::string_view(sched_buf) == "Schedule: 7AM-8PM");
  format_schedule_range_label(7, 20, true, sched_buf, sizeof(sched_buf));
  LATTER_CHECK(std::string_view(sched_buf) == "Schedule: 07:00-20:00");
  format_schedule_range_label(0, 12, false, sched_buf, sizeof(sched_buf));
  LATTER_CHECK(std::string_view(sched_buf) == "Schedule: 12AM-12PM");
  (void)0;
}

void test_full_spectrum_brightness_d065() {
  using namespace latter_ui;

  // 1. Universal CIE/Weber-Fechner perceptual curve (hal_map_perceptual_u32 / PWM / DCS)
  LATTER_CHECK(hal_map_perceptual_u32(0.0f, 10U, 255U, 2.20f) == 0U);
  LATTER_CHECK(hal_map_perceptual_u32(0.00005f, 10U, 255U, 2.20f) == 0U);

  // AMOLED DCS range (10..255, default gamma 1.50 to avoid double-gamma on CO5300 DBV LUT)
  LATTER_CHECK(hal_map_dcs_brightness(0.0f) == 0U);
  LATTER_CHECK(hal_map_dcs_brightness(0.01f) == 10U);
  LATTER_CHECK(hal_map_dcs_brightness(1.00f) == 255U);
  const uint8_t dcs_35 = hal_map_dcs_brightness(0.35f);
  const uint8_t dcs_50 = hal_map_dcs_brightness(0.50f);
  LATTER_CHECK(dcs_35 >= 55U && dcs_35 <= 65U);
  LATTER_CHECK(dcs_50 > dcs_35 && dcs_50 < 128U);
  uint8_t prev_dcs = 0U;
  for (int pct = 1; pct <= 100; ++pct) {
    const uint8_t cur = hal_map_dcs_brightness(static_cast<float>(pct) * 0.01f);
    LATTER_CHECK(cur >= 10U && cur >= prev_dcs);
    prev_dcs = cur;
  }

  // 12-bit PWM FET IPS (3..1000 permille = 0.3..100.0% of 4095, gamma 2.20)
  LATTER_CHECK(hal_backlight_max_duty(12U) == 4095U);
  LATTER_CHECK(hal_map_pwm_on_duty(0.0f, 12U, 3U, 1000U) == 0U);
  const uint32_t pwm_fet_1 = hal_map_pwm_on_duty(0.01f, 12U, 3U, 1000U);
  const uint32_t pwm_fet_35 = hal_map_pwm_on_duty(0.35f, 12U, 3U, 1000U);
  const uint32_t pwm_fet_50 = hal_map_pwm_on_duty(0.50f, 12U, 3U, 1000U);
  const uint32_t pwm_fet_100 = hal_map_pwm_on_duty(1.00f, 12U, 3U, 1000U);
  LATTER_CHECK(pwm_fet_1 == 12U);
  LATTER_CHECK(pwm_fet_35 > 350U && pwm_fet_35 < 450U);
  LATTER_CHECK(pwm_fet_50 > pwm_fet_35 && pwm_fet_50 < 2048U);
  LATTER_CHECK(pwm_fet_100 == 4095U);

  // 12-bit inverted Analog RC IPS (480..1000 permille = 48.0..100.0% of 4095 -> 1965..4095, gamma 2.20)
  LATTER_CHECK(hal_map_pwm_on_duty(0.0f, 12U, 480U, 1000U) == 0U);
  const uint32_t pwm_rc_1 = hal_map_pwm_on_duty(0.01f, 12U, 480U, 1000U);
  const uint32_t pwm_rc_35 = hal_map_pwm_on_duty(0.35f, 12U, 480U, 1000U);
  const uint32_t pwm_rc_50 = hal_map_pwm_on_duty(0.50f, 12U, 480U, 1000U);
  const uint32_t pwm_rc_100 = hal_map_pwm_on_duty(1.00f, 12U, 480U, 1000U);
  LATTER_CHECK(pwm_rc_1 == 1965U);
  LATTER_CHECK(pwm_rc_35 > 2100U && pwm_rc_35 < 2250U);
  LATTER_CHECK(pwm_rc_50 > pwm_rc_35 && pwm_rc_50 < 3030U);
  LATTER_CHECK(pwm_rc_100 == 4095U);

  // 2. Proportional screensaver & Bedtime Ambient Clock dimming (never brighter than active!)
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(1.00f) - 0.45f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.80f) - 0.36f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.10f) - 0.05f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.03f) - 0.03f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.01f) - 0.01f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_bedtime_ambient_brightness(0.15f) - 0.0525f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_bedtime_ambient_brightness(0.05f) - 0.02f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_bedtime_ambient_brightness(0.01f) - 0.01f) < 1e-4f);

  // 3. 1..100% brightness prefs & bedtime_brightness Protocol v3 + local edit round-trip
  PersistedDevicePrefs p{};
  PrefMask chg{};
  uint32_t eff = 0U;
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::MANUAL_BRIGHTNESS, "42", false, chg, eff));
  LATTER_CHECK(p.manual_brightness == 42U && get_pref_value_str(PrefKey::MANUAL_BRIGHTNESS, p) == "42");
  LATTER_CHECK((eff & kPrefEffectBrightness) != 0U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::DAY_BRIGHTNESS, "150", false, chg, eff));
  LATTER_CHECK(p.day_brightness == 100U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::NIGHT_BRIGHTNESS, "0", false, chg, eff));
  LATTER_CHECK(p.night_brightness == 1U);
  LATTER_CHECK(set_pref_by_key_str(p, PrefKey::BEDTIME_BRIGHTNESS, "12", false, chg, eff));
  LATTER_CHECK(p.bedtime_brightness == 12U && get_pref_value_str(PrefKey::BEDTIME_BRIGHTNESS, p) == "12");
  const std::string encoded = encode_prefs_mask(
      pref_key_bit(PrefKey::MANUAL_BRIGHTNESS) | pref_key_bit(PrefKey::BEDTIME_BRIGHTNESS), p);
  LATTER_CHECK(encoded.find("manual_brightness=42") != std::string::npos);
  LATTER_CHECK(encoded.find("bedtime_brightness=12") != std::string::npos);

  device_prefs_state() = DevicePrefsState{};
  persisted_prefs() = PersistedDevicePrefs{};
  std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "61";
  dp_tok[static_cast<size_t>(DevicePrefsField::MANUAL_BRIGHTNESS)] = "27";
  dp_tok[static_cast<size_t>(DevicePrefsField::DAY_BRIGHTNESS)] = "85";
  dp_tok[static_cast<size_t>(DevicePrefsField::NIGHT_BRIGHTNESS)] = "18";
  dp_tok[static_cast<size_t>(DevicePrefsField::BEDTIME_BRIGHTNESS)] = "9";
  uint32_t p_eff = 0U;
  LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(96, 1, dp_tok), p_eff, 4000U));
  LATTER_CHECK((p_eff & kPrefEffectBrightness) != 0U);
  LATTER_CHECK(persisted_prefs().manual_brightness_pct() == 27U);
  LATTER_CHECK(persisted_prefs().day_brightness_pct() == 85U);
  LATTER_CHECK(persisted_prefs().night_brightness_pct() == 18U);
  LATTER_CHECK(persisted_prefs().bedtime_brightness_pct() == 9U);
}

void test_cameras_app_and_live_video_d066() {
  using namespace latter_ui;
  reset_runtime_state();

  // 1. MjpegFrameParser: multipart Content-Length (1-byte chunking), SOI..EOI fallback, & oversized skip
  {
    MjpegFrameParser parser(64U);
    parser.set_boundary("multipart/x-mixed-replace; boundary=latterframe");
    LATTER_CHECK(parser.boundary() == "latterframe");

    std::vector<uint8_t> jpeg1 = {0xFF, 0xD8, 0x11, 0x22, 0x33, 0xFF, 0xD9};
    std::string stream = "--latterframe\r\nContent-Type: image/jpeg\r\nContent-Length: 7\r\n\r\n";
    stream.append(reinterpret_cast<const char *>(jpeg1.data()), jpeg1.size());
    stream.append("\r\n--latterframe\r\nContent-Type: image/jpeg\r\n\r\n");
    stream.append(reinterpret_cast<const char *>(jpeg1.data()), jpeg1.size());
    stream.append("\r\n--latterframe\r\nContent-Length: 200\r\n\r\n");
    stream.append(200, 'X');
    stream.append("\r\n--latterframe--\r\n");

    std::vector<size_t> sizes;
    for (char ch : stream) {
      const uint8_t b = static_cast<uint8_t>(ch);
      parser.feed(&b, 1U, [&](const uint8_t *d, size_t len) {
        LATTER_CHECK(len == 7U && d[0] == 0xFFU && d[1] == 0xD8U && d[len - 2] == 0xFFU && d[len - 1] == 0xD9U);
        sizes.push_back(len);
      });
    }
    LATTER_CHECK(sizes.size() == 2U);
    LATTER_CHECK(parser.frames_parsed() == 2U);
    LATTER_CHECK(parser.frames_oversized() == 1U);
  }

  // 2. LatterVideoClient state transitions & host frame injection
  {
    auto &vc = video_client();
    vc.reset_for_test();
    LATTER_CHECK(vc.state() == VideoStreamState::IDLE && !vc.is_active() && !vc.has_frame());
    vc.start("http://ha.local/api/latter/camera_stream/desk?sig=abc&entity_id=camera.front&w=384&h=224&fps=12&fit=contain", 384U, 224U);
    LATTER_CHECK(vc.is_active());
    const uint16_t px[4] = {0xF800U, 0x07E0U, 0x001FU, 0xFFFFU};
    vc.inject_host_frame_for_test(px, 2, 2);
    LATTER_CHECK(vc.has_frame() && vc.state() == VideoStreamState::STREAMING);
    LATTER_CHECK(vc.consume_frame_dirty());
    LATTER_CHECK(!vc.consume_frame_dirty());
    const uint16_t *out_d = nullptr;
    int out_w = 0, out_h = 0;
    LATTER_CHECK(vc.acquire_frame(out_d, out_w, out_h));
    LATTER_CHECK(out_d == px && out_w == 2 && out_h == 2);
    vc.release_frame();
    vc.stop();
    LATTER_CHECK(vc.state() == VideoStreamState::IDLE && !vc.is_active() && !vc.has_frame());
  }

  // 3. CAMERAS_SUMMARY 5-field parsing (entity_id|name|area_id|caps|unlock_entity) & Carousel index filtering
  {
    std::string raw;
    raw += "camera.front_door|Front Door|porch|63|lock.front_door";  // listen+talk+doorbell+carousel+reply+unlock
    raw.push_back(kProtocolRecordSep);
    raw += "camera.driveway|Driveway|exterior|40|cover.garage_door"; // carousel+unlock (garage cover)
    raw.push_back(kProtocolRecordSep);
    raw += "camera.garage|Garage|garage|0|";                         // no controls/carousel/unlock
    CameraRecord cams[MAX_CAMERA_SLOTS]{};
    const int n = parse_cameras_records(raw, cams, MAX_CAMERA_SLOTS);
    LATTER_CHECK(n == 3);
    LATTER_CHECK(cams[0].entity_id == "camera.front_door" && cams[0].name == "Front Door" &&
                 cams[0].has_audio() && cams[0].has_talkback() && cams[0].is_doorbell() &&
                 cams[0].in_carousel() && cams[0].show_reply() && cams[0].has_unlock() &&
                 cams[0].unlock_is_lock() && cams[0].unlock_entity == "lock.front_door");
    LATTER_CHECK(cams[1].name == "Driveway" && !cams[1].has_audio() && !cams[1].has_talkback() &&
                 !cams[1].show_reply() && cams[1].in_carousel() && cams[1].has_unlock() &&
                 !cams[1].unlock_is_lock() && cams[1].unlock_entity == "cover.garage_door");
    LATTER_CHECK(!cams[2].has_audio() && !cams[2].in_carousel() && !cams[2].show_reply() &&
                 !cams[2].has_unlock() && cams[2].unlock_entity.empty());
    LATTER_CHECK(kVideoHttpConnectTimeoutMs == 8000U);

    int car_idx[MAX_CAMERA_SLOTS]{};
    const int car_n = collect_carousel_camera_indices(cams, n, car_idx, MAX_CAMERA_SLOTS);
    LATTER_CHECK(car_n == 2 && car_idx[0] == 0 && car_idx[1] == 1);

    // Fallback to all cameras when none are flagged in_carousel
    cams[0].caps = 0;
    cams[1].caps = 0;
    const int car_fb = collect_carousel_camera_indices(cams, n, car_idx, MAX_CAMERA_SLOTS);
    LATTER_CHECK(car_fb == 3 && car_idx[0] == 0 && car_idx[1] == 1 && car_idx[2] == 2);
  }

  // 4. Stream URL builders & trait-based viewport/FPS resolution (16px-aligned, S3 audio fps=2, P4 12 FPS)
  {
    uint16_t rw = 0, rh = 0;
    resolve_camera_stream_dims(466, 466, /*is_round=*/true, /*is_grid=*/false, false, false, rw, rh);
    LATTER_CHECK((rw % 16U) == 0U && (rh % 16U) == 0U && rw == 384U && rh == 224U);
    resolve_camera_stream_dims(720, 720, /*is_round=*/false, /*is_grid=*/true, true, false, rw, rh);
    LATTER_CHECK((rw % 16U) == 0U && (rh % 16U) == 0U && rw <= 720U);
    // Portrait panel (320x480) with rot90=true requests portrait 9:16 dims (256x480), not landscape 320x176
    resolve_camera_stream_dims(320, 480, /*is_round=*/false, /*is_grid=*/false, false, false, rw, rh, /*rot90=*/false);
    LATTER_CHECK(rw == 320U && rh == 176U);
    resolve_camera_stream_dims(320, 480, /*is_round=*/false, /*is_grid=*/false, false, false, rw, rh, /*rot90=*/true);
    LATTER_CHECK((rw % 16U) == 0U && (rh % 16U) == 0U && rh == 480U && rw == 256U && (static_cast<size_t>(rw) * rh * 2U) <= 262144U);

    // FrameCadenceController keeps ui_anim_tick warm while video_stream_active and wakes on 250ms
    {
      FrameCadenceController fc;
      Cadence20msInputs c20{};
      c20.panel_on = true;
      c20.display_power_state = 0;
      c20.gui_page = PageId::CAMERAS;
      c20.video_stream_active = true;
      for (int tick = 0; tick < 6; ++tick) {
        c20.now_ms = static_cast<uint32_t>(1000 + tick * 20);
        const auto a20 = fc.evaluate_20ms(c20);
        LATTER_CHECK(a20.anim_driving_disp && !a20.stop_anim_poller);
      }
      // Once video stops, idle_anim_ticks reaches 2 and stops anim poller cleanly
      c20.video_stream_active = false;
      c20.now_ms = 1200U;
      LATTER_CHECK(!fc.evaluate_20ms(c20).stop_anim_poller);
      c20.now_ms = 1220U;
      LATTER_CHECK(fc.evaluate_20ms(c20).stop_anim_poller);

      Cadence250msInputs c250{};
      c250.now_ms = 2000U;
      c250.display_power_state = 0;
      c250.gui_page = PageId::CAMERAS;
      c250.anim_poller_running = false;
      c250.video_stream_active = true;
      LATTER_CHECK(fc.evaluate_250ms(c250).ensure_anim_poller);
    }

    LATTER_CHECK(resolve_camera_stream_fps(/*is_p4=*/false, false, false, /*audio_ptt=*/true) == 2U);
    LATTER_CHECK(resolve_camera_stream_fps(/*is_p4=*/false, false, false, /*audio_ptt=*/false) == 5U);
    LATTER_CHECK(resolve_camera_stream_fps(/*is_p4=*/true, false, false, /*audio_ptt=*/true) == 12U);
    LATTER_CHECK(resolve_camera_stream_fps(/*is_p4=*/true, true, false, false) == 6U);

    const std::string base = "http://ha.local:8123/api/latter/camera_stream/desk?sig=deadbeef";
    const std::string single_u = build_camera_single_stream_url(base, "camera.front", 384, 224, 12, false, false);
    LATTER_CHECK(single_u.find("/camera_stream/desk?sig=deadbeef") != std::string::npos);
    LATTER_CHECK(single_u.find("entity_id=camera.front") != std::string::npos);
    LATTER_CHECK(single_u.find("fit=contain") != std::string::npos);

    const std::string ents[4] = {"camera.a", "camera.b", "camera.c", "camera.d"};
    const std::string grid_u = build_camera_grid_stream_url(base, ents, 4, 640, 480, 6);
    LATTER_CHECK(grid_u.find("/camera_grid/desk?sig=deadbeef") != std::string::npos);
    LATTER_CHECK(grid_u.find("cameras=camera.a,camera.b,camera.c,camera.d") != std::string::npos);
    LATTER_CHECK(grid_u.find("w=640&h=480&fps=6") != std::string::npos);
  }

  // 5. Uncropped geometry (round 466x466 zero corner clipping!) & 2x2 Grid quadrant hit regions across 5 panels
  {
    const CameraRect round_vp = compute_uncropped_video_viewport(466, 466, /*round=*/true, /*fill=*/false);
    const int rdx = (round_vp.x0 + round_vp.w / 2) - 233;
    const int corner_dx = round_vp.w / 2;
    const int corner_dy = round_vp.h / 2;
    LATTER_CHECK(rdx == 0);
    LATTER_CHECK(corner_dx * corner_dx + corner_dy * corner_dy <= 228 * 228);  // 100% inside round 466 bezel!

    const CameraRect fit_16_9 = compute_video_display_rect(CameraRect{0, 0, 720, 720}, 1920, 1080, false);
    LATTER_CHECK(fit_16_9.w == 720 && fit_16_9.h == 405 && fit_16_9.y0 == 157);

    struct PanelSpec { int w, h; bool round, wear; int ppi; };
    const PanelSpec panels[5] = {
        {466, 466, true, false, 266},
        {480, 480, false, false, 313},
        {410, 502, false, true, 315},
        {320, 480, false, false, 165},
        {720, 720, false, false, 254},
    };
    for (const auto &pn : panels) {
      const CameraGridGeom gg = compute_camera_grid_geom(pn.w, pn.h, pn.round, pn.wear, pn.ppi);
      LATTER_CHECK(gg.frame.w > 120 && gg.frame.h > 80);
      LATTER_CHECK(gg.frame.x0 >= 0 && gg.frame.x1() < pn.w);
      LATTER_CHECK(gg.frame.y0 >= 0 && gg.frame.y1() < gg.mode_bar_cy - gg.mode_bar_r);
      FrameHitTable ht;
      for (int q = 0; q < 4; ++q) {
        ht.add_rect(gg.tiles[q].x0, gg.tiles[q].y0, gg.tiles[q].w, gg.tiles[q].h,
                    static_cast<uint16_t>(WidgetId::CAM_GRID_TILE_BASE + q), kHitTap, static_cast<int16_t>(q));
      }
      for (int q = 0; q < 4; ++q) {
        const HitRegion *hr = ht.hit_test(gg.tiles[q].cx(), gg.tiles[q].cy(), kHitTap);
        LATTER_CHECK(hr != nullptr && hr->widget_id == static_cast<uint16_t>(WidgetId::CAM_GRID_TILE_BASE + q));
      }
    }
  }

  // 6. Auto-hide app:cameras on Home Dock when 0 cameras configured, show when > 0
  {
    shortcuts_state() = ShortcutsState{};
    area_ctx_state() = AreaContextState{};
    ShortcutItem dock[kMaxEffectiveDockItems]{};
    const int n0 = collect_effective_dock_items(dock, kMaxEffectiveDockItems);
    bool found0 = false;
    for (int i = 0; i < n0; ++i) {
      if (dock[i].route_id == RouteId::APP_CAMERAS) found0 = true;
    }
    LATTER_CHECK(!found0);

    area_ctx_state().cameras_summary = "camera.front|Front|porch|15";
    const int n1 = collect_effective_dock_items(dock, kMaxEffectiveDockItems);
    bool found1 = false;
    for (int i = 0; i < n1; ++i) {
      if (dock[i].route_id == RouteId::APP_CAMERAS) found1 = true;
    }
    LATTER_CHECK(found1);
  }

  // 7. Return-to-Home idle exemption on Stationary Grid/Carousel & Live Video During PTT VA Takeover Precedence
  {
    auto &rs = runtime_state();
    PersistedDevicePrefs p{};
    p.home_screen_enabled = true;
    p.return_to_home_timeout = 3;  // 2 min
    rs.nav.gui_page = PageId::CAMERAS;
    rs.cameras.tab = CamerasTab::CAROUSEL;
    LATTER_CHECK(!should_return_to_home_on_idle(rs, p, false, false, /*is_stationary=*/true));
    LATTER_CHECK(should_return_to_home_on_idle(rs, p, false, false, /*is_stationary=*/false));
    rs.cameras.tab = CamerasTab::GRID;
    LATTER_CHECK(!should_return_to_home_on_idle(rs, p, false, false, /*is_stationary=*/true));
    rs.cameras.tab = CamerasTab::SINGLE;
    LATTER_CHECK(should_return_to_home_on_idle(rs, p, false, false, /*is_stationary=*/true));
    LATTER_CHECK(!should_return_to_home_on_idle(rs, p, false, /*cam_session_active=*/true, true));

    // VA Takeover must NOT hide live video during doorbell PTT (`intercom_turn == true`)!
    LATTER_CHECK(!should_va_takeover_preempt(true, true, /*intercom_turn=*/true, PageId::CAMERAS, false));
    LATTER_CHECK(!should_va_takeover_preempt(true, true, /*intercom_turn=*/true, PageId::HOME, /*live_popup=*/true));
    LATTER_CHECK(should_va_takeover_preempt(true, true, /*intercom_turn=*/false, PageId::CAMERAS, false));
    LATTER_CHECK(should_va_takeover_preempt(true, false, /*intercom_turn=*/true, PageId::HOME, false));
  }

  // 8. P4 Live Doorbell Popup predicate & 5 new Camera DevicePrefs v15 round-trip
  {
    PersistedDevicePrefs p{};
    AreaContextState ac{};
    AlertBannerState ab{};
    ac.camera_stream_url = "http://ha.local/api/latter/camera_stream/desk?sig=123";
    ab.camera_entity = "camera.doorbell";
    p.camera_doorbell_popup = 1U;
    LATTER_CHECK(is_p4_live_doorbell_popup_enabled(p, ac, ab, /*is_p4=*/true));
    LATTER_CHECK(!is_p4_live_doorbell_popup_enabled(p, ac, ab, /*is_p4=*/false));
    p.camera_doorbell_popup = 0U;
    LATTER_CHECK(!is_p4_live_doorbell_popup_enabled(p, ac, ab, /*is_p4=*/true));

    device_prefs_state() = DevicePrefsState{};
    persisted_prefs() = PersistedDevicePrefs{};
    std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "72";
    dp_tok[static_cast<size_t>(DevicePrefsField::CAMERA_DEFAULT_VIEW)] = "Carousel";
    dp_tok[static_cast<size_t>(DevicePrefsField::CAMERA_CAROUSEL_INTERVAL)] = "30s";
    dp_tok[static_cast<size_t>(DevicePrefsField::CAMERA_FRAMING)] = "Fill";
    dp_tok[static_cast<size_t>(DevicePrefsField::CAMERA_STREAM_MODE)] = "Low Bandwidth";
    dp_tok[static_cast<size_t>(DevicePrefsField::CAMERA_DOORBELL_POPUP)] = "Live Video";
    uint32_t eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(97, 1, dp_tok), eff, 5000U));
    LATTER_CHECK(std::string_view(pref_camera_default_view_cstr()) == "Carousel");
    LATTER_CHECK(std::string_view(pref_camera_carousel_interval_cstr()) == "30s");
    LATTER_CHECK(persisted_prefs().camera_carousel_interval_ms() == 30000U);
    LATTER_CHECK(std::string_view(pref_camera_framing_cstr()) == "Fill");
    LATTER_CHECK(std::string_view(pref_camera_stream_mode_cstr()) == "Low Bandwidth");
  }
}

void test_photo_frame_screensaver_day_night_sky_d067() {
  using namespace latter_ui;

  // 1. Universal screensaver_mode ("Docked Only" | "Always" | "Never") power eligibility
  static_assert(screensaver_allowed_for_power(0U, true, false));   // Stationary Docked Only -> allowed
  static_assert(screensaver_allowed_for_power(0U, false, true));   // Battery USB Docked Only -> allowed
  static_assert(!screensaver_allowed_for_power(0U, false, false)); // Battery unplugged Docked Only -> screen off
  static_assert(screensaver_allowed_for_power(1U, false, false));  // Battery unplugged Always -> allowed!
  static_assert(screensaver_allowed_for_power(1U, true, false));   // Stationary Always -> allowed
  static_assert(!screensaver_allowed_for_power(2U, true, true));   // Stationary Never -> disabled
  static_assert(!screensaver_allowed_for_power(2U, false, true));  // Battery USB Never -> disabled

  // 2. Battery hibernate safety ceiling still fires during Always screensaver on battery (not docked)
  {
    DisplayLifecycleController dlc{};
    int nav_pwr = 1;  // Screensaver active on battery
    uint32_t sleep_ts = 0U;
    uint32_t sb_ts = 1000U;
    bool greet = false;
    DisplaySupervisorInputs in{};
    in.is_stationary = false;
    in.has_battery = true;
    in.usb_connected = false;
    in.screensaver_mode = 1U;  // Always
    in.hibernate_timeout_opt = "5 min";
    in.panel_brightness_on = true;
    in.hibernate_blocked = false;

    auto a1 = dlc.evaluate_1s(10000U, in, nav_pwr, sleep_ts, sb_ts, greet);
    LATTER_CHECK(!a1.start_hibernate_countdown_now && sleep_ts == 10000U);
    auto a2 = dlc.evaluate_1s(310000U, in, nav_pwr, sleep_ts, sb_ts, greet);
    LATTER_CHECK(a2.start_hibernate_countdown_now);

    // Disabled hibernate runs indefinitely on battery
    sleep_ts = 0U;
    in.hibernate_timeout_opt = "Disabled";
    auto a3 = dlc.evaluate_1s(10000U, in, nav_pwr, sleep_ts, sb_ts, greet);
    auto a4 = dlc.evaluate_1s(9000000U, in, nav_pwr, sleep_ts, sb_ts, greet);
    LATTER_CHECK(!a3.start_hibernate_countdown_now && !a4.start_hibernate_countdown_now);

    // Docked screensaver (hibernate_blocked = true) never hibernates
    sleep_ts = 0U;
    in.hibernate_timeout_opt = "5 min";
    in.usb_connected = true;
    in.hibernate_blocked = true;
    auto a5 = dlc.evaluate_1s(10000U, in, nav_pwr, sleep_ts, sb_ts, greet);
    auto a6 = dlc.evaluate_1s(900000U, in, nav_pwr, sleep_ts, sb_ts, greet);
    LATTER_CHECK(!a5.start_hibernate_countdown_now && !a6.start_hibernate_countdown_now && sleep_ts == 0U);
  }

  // 3. Photo Frame brightness (0.70x, 0.08 floor, capped at active) vs Ambient/Animated (0.45x, 0.05 floor)
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(1.00f, true) - 0.70f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.80f, true) - 0.56f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.10f, true) - 0.08f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(0.05f, true) - 0.05f) < 1e-4f);
  LATTER_CHECK(std::fabs(resolve_screensaver_brightness(1.00f, false) - 0.45f) < 1e-4f);

  // 4. Photo Frame is strictly 0 FPS static in FrameCadenceController::evaluate_screensaver_20ms
  {
    FrameCadenceController fc{};
    CadenceScreensaverInputs in{};
    in.now_ms = 5000U;
    in.display_power_state = 1;
    in.screensaver_style = "Photo Frame";
    const auto pf_act = fc.evaluate_screensaver_20ms(in);
    LATTER_CHECK(!pf_act.schedule_anim_full && !pf_act.record_ambient_ms);
    LATTER_CHECK(pf_act.sync_disp_poller && !pf_act.sync_disp_poller_val);

    in.screensaver_style = "Aurora";
    const auto au_act = fc.evaluate_screensaver_20ms(in);
    LATTER_CHECK(au_act.schedule_anim_full && au_act.sync_disp_poller && au_act.sync_disp_poller_val);
  }

  // 5. DirtyBandTracker 0 FPS idle skip during static screensaver, waking on 1-min clock rollover
  {
    DirtyBandTracker dbt{};
    const uint32_t ss_sig = compute_takeover_signature(false, false, true, 1, false, false, false, false, false);
    LATTER_CHECK(ss_sig == (1U << 3));
    LATTER_CHECK(dbt.begin_frame(1, 0, ss_sig, false, 600, 0x11U, 180, false));   // Enter screensaver -> full draw
    LATTER_CHECK(!dbt.begin_frame(1, 0, ss_sig, false, 600, 0x11U, 180, false));  // Same minute -> idle skip (0 FPS)
    LATTER_CHECK(dbt.begin_frame(1, 0, ss_sig, false, 601, 0x11U, 180, false));   // Minute rollover -> full draw
    LATTER_CHECK(!dbt.begin_frame(1, 0, ss_sig, false, 601, 0x11U, 180, false));  // Idle skip again
  }

  // 6. Native Day & Night Sky sentinel + horizon/clock day-night resolver
  static_assert(is_day_night_sky_wallpaper_url("latter://day_night_sky"));
  static_assert(!is_day_night_sky_wallpaper_url("http://ha.local/api/latter/wallpaper/1"));
  static_assert(!is_day_night_sky_wallpaper_url(""));
  static_assert(resolve_day_night_sky_is_night("below_horizon", true, 12));
  static_assert(!resolve_day_night_sky_is_night("above_horizon", true, 23));
  static_assert(resolve_day_night_sky_is_night("", true, 5));
  static_assert(!resolve_day_night_sky_is_night("", true, 6));
  static_assert(!resolve_day_night_sky_is_night("", true, 19));
  static_assert(resolve_day_night_sky_is_night("", true, 20));
  static_assert(!resolve_day_night_sky_is_night("unknown", false, 23));

  // 7. 128-bit PrefMask high-bit prefs (63 & 64) + Protocol v3 device_prefs & area_context round-trip
  {
    reset_all_v3_rev_guards();
    device_prefs_state() = DevicePrefsState{};
    persisted_prefs() = PersistedDevicePrefs{};
    runtime_state() = DeviceRuntimeState{};

    const PrefMask hi_mask =
        pref_key_bit(PrefKey::SCREENSAVER_PHOTO_CLOCK) | pref_key_bit(PrefKey::WEATHER_APP_BACKGROUND);
    LATTER_CHECK(hi_mask.lo == (1ULL << 63) && hi_mask.hi == 1ULL);
    const std::string enc = encode_prefs_mask(hi_mask, persisted_prefs());
    LATTER_CHECK(enc.find("screensaver_photo_clock=0") != std::string::npos);
    LATTER_CHECK(enc.find("weather_app_background=1") != std::string::npos);

    const std::string hello_seed = build_hello_dirty_prefs(persisted_prefs());
    LATTER_CHECK(hello_seed.find("screensaver_mode=Docked Only") != std::string::npos);
    LATTER_CHECK(hello_seed.find("screensaver_photo_clock=0") != std::string::npos);
    LATTER_CHECK(hello_seed.find("weather_app_background=1") != std::string::npos);

    std::vector<std::string> dp_tok(static_cast<size_t>(DevicePrefsField::COUNT), "");
    dp_tok[static_cast<size_t>(DevicePrefsField::PREFS_SEQ)] = "88";
    dp_tok[static_cast<size_t>(DevicePrefsField::SCREENSAVER_MODE)] = "Always";
    dp_tok[static_cast<size_t>(DevicePrefsField::SCREENSAVER_STYLE)] = "Photo Frame";
    dp_tok[static_cast<size_t>(DevicePrefsField::SCREENSAVER_PHOTO_CLOCK)] = "1";
    dp_tok[static_cast<size_t>(DevicePrefsField::WEATHER_APP_BACKGROUND)] = "0";
    uint32_t eff = 0U;
    LATTER_CHECK(parse_v3_device_prefs_payload(build_v3_packet(98, 1, dp_tok), eff, 6000U));
    LATTER_CHECK((eff & kPrefEffectScreensaverChange) != 0U);
    LATTER_CHECK(persisted_prefs().screensaver_mode == 1U);
    LATTER_CHECK(std::string_view(pref_screensaver_mode_cstr()) == "Always");
    LATTER_CHECK(persisted_prefs().screensaver_style == 1U);
    LATTER_CHECK(persisted_prefs().screensaver_style_enum() == ScreensaverStyle::PhotoFrame);
    LATTER_CHECK(std::string_view(pref_screensaver_style_cstr()) == "Photo Frame");
    LATTER_CHECK(persisted_prefs().screensaver_photo_clock == true);
    LATTER_CHECK(persisted_prefs().weather_app_background == false);
    LATTER_CHECK(device_prefs_state().weather_app_background == false);

    // V3DomainReducer: native "latter://day_night_sky" releases old HTTP wallpaper & skips fetch;
    // screensaver_photo_url triggers fetch strictly while in screensaver + Photo Frame
    auto &rs = runtime_state();
    rs.nav.display_power_state = 0;
    rs.music.loaded_wallpaper_url = "http://ha.local/api/latter/wallpaper/old";
    v3_domain_reducer().reset();

    std::vector<std::string> ac_tok(static_cast<size_t>(AreaCtxField::COUNT), "");
    ac_tok[static_cast<size_t>(AreaCtxField::WALLPAPER_URL)] = "latter://day_night_sky";
    ac_tok[static_cast<size_t>(AreaCtxField::SCREENSAVER_PHOTO_URL)] = "http://ha.local/api/latter/wallpaper/p1?mode=screensaver";
    LATTER_CHECK(parse_v3_area_ctx_payload(build_v3_packet(98, 1, ac_tok)));
    LATTER_CHECK(area_ctx_state().screensaver_photo_url == "http://ha.local/api/latter/wallpaper/p1?mode=screensaver");

    V3ReduceContext rctx{};
    rctx.now_ms = 7000U;
    const auto e_active = v3_domain_reducer().reduce("area_context", rctx);
    LATTER_CHECK(e_active.stop_wallpaper_fetch && e_active.release_wallpaper_art && !e_active.fetch_wallpaper);
    LATTER_CHECK(!e_active.fetch_screensaver_photo);  // Active display does NOT fetch screensaver photo

    // Enter screensaver -> next area_context rotation triggers fetch_screensaver_photo
    rs.nav.display_power_state = 1;
    ac_tok[static_cast<size_t>(AreaCtxField::SCREENSAVER_PHOTO_URL)] = "http://ha.local/api/latter/wallpaper/p2?mode=screensaver";
    LATTER_CHECK(parse_v3_area_ctx_payload(build_v3_packet(98, 2, ac_tok)));
    const auto e_ss = v3_domain_reducer().reduce("area_context", rctx);
    LATTER_CHECK(e_ss.fetch_screensaver_photo && !e_ss.release_screensaver_photo_art);

    // Clearing screensaver_photo_url stops & releases art
    rs.music.loaded_screensaver_photo_url = "http://ha.local/api/latter/wallpaper/p2?mode=screensaver";
    ac_tok[static_cast<size_t>(AreaCtxField::SCREENSAVER_PHOTO_URL)] = "";
    LATTER_CHECK(parse_v3_area_ctx_payload(build_v3_packet(98, 3, ac_tok)));
    const auto e_clr = v3_domain_reducer().reduce("area_context", rctx);
    LATTER_CHECK(e_clr.stop_screensaver_photo_fetch && e_clr.release_screensaver_photo_art && !e_clr.fetch_screensaver_photo);
  }

  // 8. Photo Frame initial-load grace window eliminates Ambient Clock pre-flash & reuses matching wallpaper art
  {
    const uint32_t kSsSig = (1U << 3);
    const std::string url = "http://ha.local/api/latter/wallpaper/p1?mode=screensaver";
    // Holds framebuffer cleanly while Photo #1 is downloading (< 2500 ms)
    LATTER_CHECK(is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, false, false, 10000U, 10000U));
    LATTER_CHECK(is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, false, false, 10000U, 12499U));
    // Releases immediately once art is valid, on error, or once grace window expires
    LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, true, false, 10000U, 10150U));
    LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, false, true, 10000U, 10150U));
    LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, false, false, 10000U, 12500U));
    // Never holds when URL empty, non-Photo-Frame style, or higher-priority takeover active
    LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", "", false, false, 10000U, 10100U));
    LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Ambient Clock", url, false, false, 10000U, 10100U));
    LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig | (1U << 4), "Photo Frame", url, false, false, 10000U, 10100U));
    LATTER_CHECK(!is_photo_frame_initial_load_pending(kSsSig, "Photo Frame", url, false, false, 0U, 10100U));
    // Wallpaper art reuse check
    LATTER_CHECK(can_reuse_wallpaper_for_photo_frame(url, url, true));
    LATTER_CHECK(!can_reuse_wallpaper_for_photo_frame(url, url, false));
    LATTER_CHECK(!can_reuse_wallpaper_for_photo_frame(url, "http://ha.local/other", true));
    LATTER_CHECK(!can_reuse_wallpaper_for_photo_frame("", "", true));
  }
}

void test_gui_standardization_d070() {
  using namespace latter_ui;

  struct BoardSpec {
    int w;
    int h;
    bool round;
    bool wearable;
    int ppi;
  };
  constexpr BoardSpec kBoards[5] = {
      {320, 480, false, false, 165},  // Remote
      {410, 502, false, true,  315},  // Wrist
      {466, 466, true,  false, 266},  // Pocket
      {480, 480, false, false, 313},  // Mobile
      {720, 720, false, false, 254},  // Desk
  };

  for (const auto &b : kBoards) {
    const int min_dim = (b.w < b.h) ? b.w : b.h;
    const auto ly = compute_panel_layout(b.w, b.h, b.round, b.wearable, b.ppi);

    // 1. Canonical mode-bar slot geometry parity across ui::mode_bar, Cameras, and Music
    int mode_xs[4] = {0, 0, 0, 0};
    compute_mode_bar_slot_xs(4, b.w, b.round, mode_xs);
    const auto cam_g = compute_camera_grid_geom(b.w, b.h, b.round, b.wearable, b.ppi);
    for (int i = 0; i < 4; ++i) {
      LATTER_CHECK(cam_g.mode_slot_xs[i] == mode_xs[i]);
      if (i > 0) LATTER_CHECK(mode_xs[i] > mode_xs[i - 1]);
    }
    const int expected_center = b.w / 2;
    const int bar_mid = (mode_xs[0] + mode_xs[3]) / 2;
    LATTER_CHECK(std::abs(bar_mid - expected_center) <= 2);

    // 2. Canonical corner radii tokens: monotonic & card > key >= 8
    const int r_card = radius_card_for(min_dim);
    const int r_key = radius_key_for(min_dim);
    LATTER_CHECK(r_key >= 8);
    LATTER_CHECK(r_card > r_key);
    if (b.w > 520) {
      LATTER_CHECK(r_card > 12 && r_key > 10);
    }

    // 3. Desk 480 px kMaxContentColW centered column cap on > 520 px panels
    const int stack_w = compute_uniform_stack_w_for(
        ly.slot5_y(0), ly.slot5_y(4), ly.slot5_h, b.w - 20, b.w, b.h, b.round, b.wearable, 14);
    LATTER_CHECK(stack_w >= 160 && stack_w <= b.w - 20);
    if (b.w > 520) {
      LATTER_CHECK(stack_w <= kMaxContentColW);
      LATTER_CHECK(ly.area_card3_w <= kMaxContentColW);
      LATTER_CHECK(ly.tool_card3_w <= kMaxContentColW);
    }
    if (b.round) {
      const int top_span = band_safe_span_for(ly.slot5_y(0), ly.slot5_h, b.w, b.h, true, false, 14);
      const int bot_span = band_safe_span_for(ly.slot5_y(4), ly.slot5_h, b.w, b.h, true, false, 14);
      LATTER_CHECK(stack_w <= std::min(top_span, bot_span));
    }

    // 4. compute_action_pill_row for 1..4 buttons: zero overlap, bezel-safe, Desk <= 480 px
    const int pill_cy = ly.app_dock_y - ly.dock_r - scale_y_for(36, b.h);
    const int pill_h = scale_y_for(36, b.h);
    for (int cnt = 1; cnt <= 4; ++cnt) {
      HomeHubPillGeom geoms[4]{};
      const int out_n = compute_action_pill_row(
          cnt, pill_cy, pill_h, b.w - 28, (cnt == 1) ? scale_x_for(180, b.w) : 0,
          scale_x_for(8, b.w), b.w, b.h, b.round, b.wearable, b.ppi, geoms, 14);
      LATTER_CHECK(out_n == cnt);
      for (int i = 0; i < cnt; ++i) {
        LATTER_CHECK(geoms[i].w >= 40 && geoms[i].h >= 24);
        const int x0 = geoms[i].cx - geoms[i].w / 2;
        const int x1 = x0 + geoms[i].w - 1;
        LATTER_CHECK(x0 >= 0 && x1 < b.w);
        if (i > 0) {
          const int prev_x1 = (geoms[i - 1].cx - geoms[i - 1].w / 2) + geoms[i - 1].w - 1;
          LATTER_CHECK(x0 > prev_x1);  // Strictly non-overlapping
        }
      }
      if (b.w > 520) {
        const int first_x0 = geoms[0].cx - geoms[0].w / 2;
        const int last_x1 = (geoms[cnt - 1].cx - geoms[cnt - 1].w / 2) + geoms[cnt - 1].w;
        LATTER_CHECK((last_x1 - first_x0) <= kMaxContentColW);
      }
    }
  }

  // 5. Source-level D-070 component standardization invariants
  const char *kModeBarFiles[7] = {
      "../../components/latter_ui/page_area_hub.cpp",
      "../../components/latter_ui/page_weather.cpp",
      "../../components/latter_ui/page_home_hub.cpp",
      "../../components/latter_ui/page_time.cpp",
      "../../components/latter_ui/page_calendar.cpp",
      "../../components/latter_ui/page_music.cpp",
      "../../components/latter_ui/page_cameras.cpp",
  };
  for (const char *path : kModeBarFiles) {
    const std::string src = read_repo_file(path);
    LATTER_CHECK(src.find("ui::mode_bar(") != std::string::npos);
    LATTER_CHECK(src.find("render_in_app_mode_bar_track(") == std::string::npos);
  }
  const std::string cam_cpp = read_repo_file("../../components/latter_ui/page_cameras.cpp");
  const std::string hub_cpp = read_repo_file("../../components/latter_ui/page_home_hub.cpp");
  const std::string ui_doc = read_repo_file("../../docs/device-ui-design.md");
  LATTER_CHECK(cam_cpp.find("ui::header_pager_bar(") != std::string::npos);
  LATTER_CHECK(hub_cpp.find("ui::header_pager_bar(") != std::string::npos);
  LATTER_CHECK(hub_cpp.find("ui::labeled_action_row(") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-070:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-071:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-072:") != std::string::npos);
  LATTER_CHECK(ui_doc.find("### D-073:") != std::string::npos);

  // 7. D-073 Dedicated Broadcast tab (parse_broadcast_phrases up to 12, inline STT dictation lifecycle,
  //    VA takeover exemption on HOME_HUB, and button-only Entry & Garage)
  {
    std::string raw_phrases;
    for (int i = 1; i <= 14; ++i) {
      if (i > 1) raw_phrases.push_back('\x1e');
      raw_phrases += "  Phrase " + std::to_string(i) + "  ";
    }
    std::vector<std::string> phrases;
    const int parsed = parse_broadcast_phrases(raw_phrases, phrases);
    LATTER_CHECK(parsed == 12 && phrases.size() == 12U);
    LATTER_CHECK(phrases[0] == "Phrase 1" && phrases[11] == "Phrase 12");

    std::vector<std::string> empty_phrases;
    LATTER_CHECK(parse_broadcast_phrases("", empty_phrases) == 0 && empty_phrases.empty());

    DeviceRuntimeState brs{};
    LATTER_CHECK(!broadcast_dictation_active(brs));
    start_broadcast_dictation(1000U, brs);
    LATTER_CHECK(broadcast_dictation_active(brs) && brs.intercom.broadcast_feedback == "Listening...");
    LATTER_CHECK(!should_va_takeover_preempt(true, true, true, PageId::HOME_HUB, false));
    LATTER_CHECK(finish_broadcast_dictation("  dinner is ready  ", 2000U, brs));
    LATTER_CHECK(!broadcast_dictation_active(brs) &&
                 brs.intercom.broadcast_message == "Dinner is ready" &&
                 brs.intercom.broadcast_feedback == "Broadcast sent");
    start_broadcast_dictation(3000U, brs);
    LATTER_CHECK(!finish_broadcast_dictation("   ", 3500U, brs));
    LATTER_CHECK(!broadcast_dictation_active(brs) && brs.intercom.broadcast_feedback == "Didn't catch that");
    start_broadcast_dictation(4000U, brs);
    cancel_broadcast_dictation("Cancelled", 4200U, brs);
    LATTER_CHECK(!broadcast_dictation_active(brs) && brs.intercom.broadcast_feedback == "Cancelled");
  }

  // 6. D-071 Universal MDI icon catalog parity with Latter Studio & dynamic active Area dock icon
  for (std::string_view slug : {
           "mdi:pin", "mdi:music", "mdi:music-box-outline", "mdi:speaker-multiple",
           "mdi:home", "mdi:shield-home", "mdi:bullhorn", "mdi:microphone-off", "mdi:thermostat",
           "mdi:lock", "mdi:lock-open-variant", "mdi:garage", "mdi:garage-open",
           "mdi:robot-vacuum", "mdi:cctv", "mdi:view-grid", "mdi:video", "mdi:view-carousel",
           "mdi:cog", "mdi:cog-outline", "mdi:sofa", "mdi:floor-plan", "mdi:shimmer",
           "mdi:lightbulb", "mdi:blinds-horizontal", "mdi:blinds-horizontal-closed",
           "mdi:curtains", "mdi:curtains-closed", "mdi:thermometer", "mdi:fan",
           "mdi:toggle-switch", "mdi:toggle-switch-off-outline", "mdi:television-speaker",
           "mdi:remote-tv", "mdi:remote", "mdi:weather-partly-cloudy", "mdi:calendar-week",
           "mdi:white-balance-sunny", "mdi:moon-waning-crescent", "mdi:weather-windy",
           "mdi:clock-outline", "mdi:timer-sand", "mdi:alarm", "mdi:timer-outline",
           "mdi:calendar", "mdi:calendar-text", "mdi:calendar-today", "mdi:calendar-month",
           "mdi:view-grid-outline", "mdi:controller", "mdi:play-pause", "mdi:menu", "mdi:power",
           "mdi:microphone", "mdi:bell-ring", "mdi:water", "mdi:chevron-left",
           "mdi:chevron-right", "mdi:chevron-up", "mdi:chevron-down", "mdi:plus", "mdi:minus",
           "mdi:arrow-up", "mdi:arrow-down", "mdi:play", "mdi:pause", "mdi:stop", "mdi:close",
           "mdi:volume-high", "mdi:volume-medium", "mdi:volume-off"}) {
    LATTER_CHECK(has_mdi_icon(slug));
  }
  const std::string widgets_cpp = read_repo_file("../../components/latter_ui/ui_widgets.cpp");
  LATTER_CHECK(widgets_cpp.find("rasterize_sdf_icon(") == std::string::npos);
  LATTER_CHECK(widgets_cpp.find("resolve_area_dock_icon(") != std::string::npos);
}

}  // namespace

int main() {
  test_protocol_v3_and_rpc();
  test_ui_layout_hit_table_and_dirty_band();
  test_battery_model_and_power_policy();
  test_music_remote_override_and_volume_feedback();
  test_beacon_boot_gate_and_touch_reboot_safety();
  test_device_prefs_v2_and_event_timer_sync();
  test_phase1_boot_wake_hibernate_and_ble_roaming();
  test_phase2_glass_and_scissor_compositor();
  test_phase3_weather_sky_backdrop_and_home_scene();
  test_phase4_audio_session_and_music_pause_timeout();
  test_panel_dcs_timeout_isolation_and_wake_grace();
  test_display_lifecycle_controller();
  test_audio_path_and_intercom_arbitrator();
  test_frame_cadence_and_typed_prefs();
  test_v3_domain_reducer_and_timer_alarm();
  test_time_app_world_clocks_alarms_and_horology();
  test_home_activity_card_d045();
  test_home_hub_v6_alarm_caps_and_tools_pagination();
  test_home_hub_d048_button_only_glass_and_conditional_controls();
  test_touch_hal_kinematic_gestures_and_d049_d050();
  test_custom_icon_tiers_and_d051();
  test_universal_shortcuts_and_pinned_app();
  test_tools_games_launcher_prefs_and_stopwatch_commands();
  test_calendar_app_d058();
  test_music_app_v3_d059();
  test_sram_psram_budget_and_http_stall_guards();
  test_bedtime_mode_d063();
  test_full_spectrum_brightness_d065();
  test_cameras_app_and_live_video_d066();
  test_photo_frame_screensaver_day_night_sky_d067();
  test_gui_standardization_d070();
  std::printf(
      "[HOST TEST PASS] Protocol v3 (<charconv>) + V3DomainReducer + TimerAlarmController + "
      "DisplayLifecycleController + AudioPathArbitrator + FrameCadenceController + "
      "7-panel UiLayout/HitTable/DirtyBand + hal_battery verified.\n");
  return 0;
}
