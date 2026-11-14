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
#include "../../components/latter_core/power_pm.cpp"
#include "../../components/latter_core/runtime_controllers.cpp"
#include "../../include/ui_hit_table.h"
#include "../../components/latter_ui/hit_table.cpp"
#include "../../include/ui_compositor.h"
#include "../../components/latter_ui/ui_compositor.cpp"

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
  // Exact 466x466 @ 316 PPI reference calibration invariants (D-002)
  const auto ref = latter_ui::compute_panel_layout(466, 466, true, false, 316);
  LATTER_CHECK(ref.drag_start == 7);
  LATTER_CHECK(ref.swipe_min == 29);
  LATTER_CHECK(ref.tap_max == 36);
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
    LATTER_CHECK(ly.swipe_min > ly.drag_start);
    LATTER_CHECK(ly.tap_max > ly.swipe_min);
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

  // 5. Production WeakLinkGate RSSI hysteresis (-65 dBm enter, -61 dBm clear after 120s hold) (hal_link_report.h)
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
  LATTER_CHECK(touch_cpp.find("touch_router().note_tap(") != std::string::npos);
  LATTER_CHECK(touch_cpp.find(".last_tap_ms") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("{\"Full Screen\", \"Letterbox\", \"Standard\", \"None\"}") != std::string::npos);

  const std::string music_cpp = read_repo_file("../../components/latter_ui/page_music.cpp");
  const std::string remote_cpp = read_repo_file("../../components/latter_ui/page_remote.cpp");
  const std::string gui_yaml = read_repo_file("../../guis/universal/gui.yaml");
  const std::string state_h = read_repo_file("../../include/latter_state.h");
  LATTER_CHECK(music_cpp.find("art_mode == \"None\"") != std::string::npos);
  LATTER_CHECK(music_cpp.find("render_page_wallpaper_base(") != std::string::npos);
  LATTER_CHECK(remote_cpp.find("render_page_wallpaper_base(") != std::string::npos);
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
  LATTER_CHECK(latter_ui::kPersistedPrefsVersion == 4U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::PrefKey::COUNT) == 45U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::DevicePrefsField::COUNT) == 51U);
  LATTER_CHECK(static_cast<size_t>(latter_ui::AreaCtxField::COUNT) == 42U);

  // Weather dock token + 10-slot dock array capacity (8 user tokens + Settings)
  LATTER_CHECK(latter_ui::kMaxDockApps == 10);
  LATTER_CHECK(latter_ui::dock_app_id_for_token("weather") == 5);
  LATTER_CHECK(latter_ui::dock_app_id_for_token("camera") == 12);
  LATTER_CHECK(latter_ui::dock_app_id_for_token("cameras") == 12);
  LATTER_CHECK(latter_ui::dock_app_id_for_token("home_hub") == 9);
  LATTER_CHECK(latter_ui::PageId::HOME_HUB == 9);
  LATTER_CHECK(latter_ui::PageId::CAMERAS == 12);

  // Start screen cameras direct resolution
  LATTER_CHECK(latter_ui::resolve_start_screen_page(12) == 12);
  latter_ui::PersistedDevicePrefs cam_prefs{};
  cam_prefs.start_screen = static_cast<uint8_t>(latter_ui::find_opt_index(latter_ui::kStartScreenOpts, "Cameras"));
  LATTER_CHECK(latter_ui::resolve_start_screen_page(cam_prefs) == 12);
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
  latter_ui::home_hub_state().thermostat_mode = "heat";
  LATTER_CHECK(latter_ui::home_hub_state().thermostat_mode == "heat");

  // Sentinel purge verification
  LATTER_CHECK(latter_ui::is_configured_remote_slot("remote.living_room"));
  LATTER_CHECK(!latter_ui::is_configured_remote_slot(""));

  // Ordered Home Hub category CSV parser & collect_home_hub_tabs
  int hh_tabs[4] = {-1, -1, -1, -1};
  latter_ui::AreaContextState uninit_ac{};
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 4);
  LATTER_CHECK(hh_tabs[0] == 0 && hh_tabs[1] == 1 && hh_tabs[2] == 2 && hh_tabs[3] == 3);
  uninit_ac.epoch = 10U;
  uninit_ac.home_categories = "vacuum,thermostat,unknown,vacuum";
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(hh_tabs, uninit_ac) == 2);
  LATTER_CHECK(hh_tabs[0] == 3 && hh_tabs[1] == 1);
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
      "Tabletop Dock", "Modern Roman (Analog)", "Home", "Aurora",
      "Full Screen", "Alphabetical", "Newest on Top", "Oldest on Top");
  LATTER_CHECK(std::string_view(latter_ui::pref_power_profile_cstr()) == "Tabletop Dock");
  LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr()) == "Docked Only");
  LATTER_CHECK(std::string_view(latter_ui::pref_clock_face_cstr()) == "Modern Roman (Analog)");
  LATTER_CHECK(std::string_view(latter_ui::pref_screensaver_style_cstr()) == "Aurora");
  LATTER_CHECK(p.prefs_seq == 0U && p.dirty_mask == 0ULL);

  // Fresh boot (prefs_seq == 0): device_hello dirty_prefs advertises board-seeded defaults (excluding ble_beacon_enabled)
  const std::string seed_dirty = latter_ui::build_hello_dirty_prefs(p);
  LATTER_CHECK(seed_dirty.find("power_profile=Tabletop Dock") != std::string::npos);
  LATTER_CHECK(seed_dirty.find("screensaver_style=Aurora") != std::string::npos);
  LATTER_CHECK(seed_dirty.find("ble_beacon_enabled=") == std::string::npos);

  // Inbound Protocol v3 device_prefs push updates prefs_seq, clears dirty_mask, and flags side effects
  latter_ui::reset_all_v3_rev_guards();
  std::vector<std::string> dp_tok(static_cast<size_t>(latter_ui::DevicePrefsField::COUNT), "");
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::PREFS_SEQ)] = "7";
  dp_tok[static_cast<size_t>(latter_ui::DevicePrefsField::POWER_PROFILE)] = "Portable Carry";
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

  // Online local touchscreen edit: flips Power Profile to "Custom" when editing a sub-key and emits set_device_prefs
  MockRpcOutbox outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_ui::link_state().ha_connected = true;
  uint32_t local_eff = 0;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::WAKE_WORD_MODE, "Always On", 1000U, local_eff, &outbox));
  LATTER_CHECK(std::string_view(latter_ui::pref_wake_word_mode_cstr()) == "Always On");
  LATTER_CHECK(std::string_view(latter_ui::pref_power_profile_cstr()) == "Custom");
  LATTER_CHECK(!outbox.published.empty());
  LATTER_CHECK(outbox.published.back().find("set_device_prefs") != std::string::npos);
  LATTER_CHECK(outbox.published.back().find("wake_word_mode=Always On") != std::string::npos);
  LATTER_CHECK(outbox.published.back().find("power_profile=Custom") != std::string::npos);

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

  uint64_t nav_chg = 0ULL;
  uint32_t nav_eff = 0;
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      p, latter_ui::PrefKey::HOME_SCREEN_ENABLED, "0", false, nav_chg, nav_eff));
  LATTER_CHECK(!p.home_screen_enabled);
  LATTER_CHECK(std::string_view(latter_ui::pref_start_screen_cstr(p)) == "Clock");
  LATTER_CHECK(latter_ui::resolve_anchor_dock_app(p) == 0);
  LATTER_CHECK((nav_eff & latter_ui::kPrefEffectNavPolicy) != 0U);

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
  rs.nav.gui_page = 8;
  rs.nav.tools_subpage = 1;
  rs.timers.running = true;
  LATTER_CHECK(!latter_ui::should_return_to_home_on_idle(rs, p, false));  // Active countdown stays put
  rs.timers.running = false;
  LATTER_CHECK(latter_ui::should_return_to_home_on_idle(rs, p, false));
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
  LATTER_CHECK(LATTER_SDK_REV == 2026111401L);

  // 1. HibernateSnapshot round-trip & cold hydration without clobbering Protocol v3 revision guards
  latter_ui::reset_hibernate_snapshot_for_test();
  latter_ui::reset_all_v3_rev_guards();
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::runtime_state().area = latter_ui::AreaRuntimeState{};
  latter_ui::persisted_prefs() = latter_ui::PersistedDevicePrefs{};

  // Cold boot with no saved snapshot returns false and leaves defaults untouched
  LATTER_CHECK(!latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::area_ctx_state().dock_apps.empty());
  LATTER_CHECK(latter_ui::weather_state().condition.empty());

  // Populate live state prior to hibernate and persist snapshot (including POSIX TZ)
  latter_ui::area_ctx_state().epoch = 42U;
  latter_ui::area_ctx_state().rev = 9U;
  latter_ui::area_ctx_state().dock_apps = "clock,music,weather,area";
  latter_ui::area_ctx_state().area_name = "Primary Suite";
  latter_ui::weather_state().epoch = 42U;
  latter_ui::weather_state().rev = 5U;
  latter_ui::weather_state().condition = "partlycloudy";
  latter_ui::weather_state().temperature = 68.5f;
  latter_ui::weather_state().sun_state = "below_horizon";
  LATTER_CHECK(latter_ui::save_hibernate_snapshot("PST8PDT,M3.2.0,M11.1.0/2"));

  // Simulate cold boot after hibernate wake before Home Assistant connects
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  latter_ui::runtime_state().area = latter_ui::AreaRuntimeState{};
  latter_ui::persisted_prefs() = latter_ui::PersistedDevicePrefs{};
  latter_ui::device_prefs_state() = latter_ui::DevicePrefsState{};
  latter_ui::hydrated_posix_tz().clear();

  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::hydrated_posix_tz() == "PST8PDT,M3.2.0,M11.1.0/2");
  LATTER_CHECK(latter_ui::area_ctx_state().dock_apps == "clock,music,weather,area");
  LATTER_CHECK(!latter_ui::device_prefs_state().nvs_save_pending);
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Primary Suite");
  LATTER_CHECK(latter_ui::runtime_state().area.active_area_name == "Primary Suite");
  LATTER_CHECK(latter_ui::weather_state().condition == "partlycloudy");
  LATTER_CHECK(std::fabs(latter_ui::weather_state().temperature - 68.5f) < 0.01f);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "below_horizon");

  // Critical: v3 epoch/rev guards MUST remain 0 so the very first live HA push overwrites everything!
  LATTER_CHECK(latter_ui::area_ctx_state().epoch == 0U && latter_ui::area_ctx_state().rev == 0U);
  LATTER_CHECK(latter_ui::weather_state().epoch == 0U && latter_ui::weather_state().rev == 0U);

  // Calling load_hibernate_snapshot_if_cold() when live state is already populated must NOT clobber live state
  latter_ui::area_ctx_state().dock_apps = "clock,tools";
  latter_ui::area_ctx_state().area_name = "Office";
  latter_ui::weather_state().condition = "sunny";
  latter_ui::weather_state().temperature = 74.0f;
  latter_ui::weather_state().sun_state = "above_horizon";
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::area_ctx_state().dock_apps == "clock,tools");
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Office");
  LATTER_CHECK(latter_ui::weather_state().condition == "sunny");
  LATTER_CHECK(std::fabs(latter_ui::weather_state().temperature - 74.0f) < 0.01f);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "above_horizon");

  // Re-hydrate cold state, then verify first live v3 push (epoch=1, rev=1) cleanly overwrites hydrated snapshot
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());

  std::vector<std::string> ac_tok(static_cast<size_t>(latter_ui::AreaCtxField::COUNT), "");
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_ID)] = "kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::AREA_NAME)] = "Kitchen";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::DOCK_APPS)] = "clock,calendar";
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::COVERS_OPEN)] = "3";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(1, 1, ac_tok)));
  LATTER_CHECK(latter_ui::area_ctx_state().epoch == 1U && latter_ui::area_ctx_state().rev == 1U);
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Kitchen");
  LATTER_CHECK(latter_ui::area_ctx_state().dock_apps == "clock,calendar");
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
  latter_ui::area_ctx_state().dock_apps = std::string(140, 'x');
  latter_ui::area_ctx_state().area_name = std::string(80, 'y');
  latter_ui::weather_state().sun_state = "above_horizon";
  LATTER_CHECK(latter_ui::save_hibernate_snapshot("EST5EDT,M3.2.0,M11.1.0/2"));
  latter_ui::area_ctx_state() = latter_ui::AreaContextState{};
  latter_ui::weather_state() = latter_ui::WeatherState{};
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::area_ctx_state().dock_apps.size() == 95U);
  LATTER_CHECK(latter_ui::area_ctx_state().area_name.size() == 47U);
  LATTER_CHECK(latter_ui::weather_state().sun_state == "above_horizon");

  // 2. Handheld Roaming power profile automatically syncs ble_beacon_mode = 1 ("Handheld + Screen Off")
  auto &prefs = latter_ui::persisted_prefs();
  prefs = latter_ui::PersistedDevicePrefs{};
  LATTER_CHECK(prefs.ble_beacon_mode == 0U);

  uint64_t chg = 0ULL;
  uint32_t eff = 0U;
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      prefs, latter_ui::PrefKey::POWER_PROFILE, "Handheld Roaming", true, chg, eff));
  LATTER_CHECK(prefs.ble_beacon_mode == 1U);
  LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(prefs)) == "Handheld + Screen Off");
  LATTER_CHECK((chg & latter_ui::pref_key_bit(latter_ui::PrefKey::BLE_BEACON_MODE)) != 0ULL);
  LATTER_CHECK((eff & latter_ui::kPrefEffectPowerPolicy) != 0U);

  // Switching to Custom preserves current ble_beacon_mode
  chg = 0ULL;
  eff = 0U;
  LATTER_CHECK(latter_ui::set_pref_by_key_str(
      prefs, latter_ui::PrefKey::POWER_PROFILE, "Custom", true, chg, eff));
  LATTER_CHECK(prefs.ble_beacon_mode == 1U);

  // Switching to Portable Carry / Wearable Watch / Tabletop Dock / Always-On Wall resets ble_beacon_mode = 0
  for (const char *prof : {"Portable Carry", "Wearable Watch", "Tabletop Dock", "Always-On Wall / Desk"}) {
    prefs.ble_beacon_mode = 1U;
    chg = 0ULL;
    eff = 0U;
    LATTER_CHECK(latter_ui::set_pref_by_key_str(
        prefs, latter_ui::PrefKey::POWER_PROFILE, prof, true, chg, eff));
    LATTER_CHECK(prefs.ble_beacon_mode == 0U);
    LATTER_CHECK(std::string_view(latter_ui::pref_ble_beacon_mode_cstr(prefs)) == "Handheld Only");
    LATTER_CHECK((chg & latter_ui::pref_key_bit(latter_ui::PrefKey::BLE_BEACON_MODE)) != 0ULL);
    LATTER_CHECK((eff & latter_ui::kPrefEffectPowerPolicy) != 0U);
  }

  // Local touchscreen edit to Handheld Roaming also publishes ble_beacon_mode=Handheld + Screen Off in set_device_prefs
  MockRpcOutbox roam_outbox{};
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  latter_ui::link_state().ha_connected = true;
  prefs.dirty_mask = 0ULL;
  eff = 0U;
  LATTER_CHECK(latter_actions::record_local_pref_edit(
      latter_ui::PrefKey::POWER_PROFILE, "Handheld Roaming", 3000U, eff, &roam_outbox));
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

  // Mode 1 ("Handheld + Screen Off", selected by Handheld Roaming) keeps BLE active when screen is off on battery!
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 9500U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && !ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.enables == 3 && ble.pauses == 2 && ble.disables == 0);

  // Docking on USB in mode 1 -> pause()
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, true, false, false, 0, true, 10000U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.is_transport_up());
  LATTER_CHECK(ble.pauses == 3 && ble.disables == 0);

  // Turning BLE beacon switch OFF while paused -> full disable() tears controller down!
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, false, 1, true, false, false, 0, true, 10500U, true, 4000U, false);
  LATTER_CHECK(!ble.is_active() && !ble.is_paused() && !ble.is_transport_up());
  LATTER_CHECK(ble.disables == 1);

  // Re-enable, pause via Wi-Fi drop, then enter hibernate -> full disable()
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, true, true, 1, false, false, false, 2, false, 11000U, true, 4000U, false);
  LATTER_CHECK(ble.is_active() && ble.enables == 4);
  latter_ui::evaluate_radio_policy(
      &ble, &wifi, false, true, 1, false, false, false, 2, false, 11500U, false, 4000U, false);
  LATTER_CHECK(!ble.is_active() && ble.is_paused() && ble.pauses == 4 && ble.disables == 1);
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
  constexpr HostRgb kModalBg{16, 20, 28};
  constexpr HostRgb kWhite{255, 255, 255};
  constexpr int kAlphaClearMin = 148;
  constexpr int kAlphaReadableMin = 208;

  const int bg_lvl = bd_valid ? ((static_cast<int>(bd_mean.r) + bd_mean.g + bd_mean.b) / 3) : 0;
  const int tint_lvl = (static_cast<int>(tint.r) + tint.g + tint.b) / 3;
  const bool dark_tint = (std::max({tint.r, tint.g, tint.b}) <= 160);
  const bool is_readable = (variant == HostGlassVariant::ReadableDark);

  int frost_q8 = 0;
  if (dark_tint && !is_readable) {
    frost_q8 = (bg_lvl < 42) ? (32 - (bg_lvl * 32) / 42) : 0;
    if (bg_lvl >= 18 && bg_lvl < 84 && tint_lvl <= bg_lvl + 8) {
      frost_q8 = std::max(frost_q8, 22);
    }
  }
  out_tint = (frost_q8 > 0) ? host_mix_q8(tint, kHomePill, frost_q8) : tint;

  const HostRgb contrast_fg = ((static_cast<int>(ref_text.r) + ref_text.g + ref_text.b) < 48) ? kWhite : ref_text;
  const float fg_lum = host_rel_lum(contrast_fg);
  const int floor_q8 = is_readable ? kAlphaReadableMin : kAlphaClearMin;

  int base_a = bd_valid ? host_glass_alpha_for_text(out_tint, bd_bright, contrast_fg, floor_q8)
                        : (is_readable ? kAlphaReadableMin : 160);
  if (!is_readable && dark_tint &&
      (base_a >= 200 || host_contrast(fg_lum, host_rel_lum(out_tint)) < 4.5f)) {
    out_tint = host_mix_q8(tint, kModalBg, 192);
    base_a = bd_valid ? host_glass_alpha_for_text(out_tint, bd_bright, contrast_fg, 200)
                      : std::max(base_a, 200);
  }
  const int da = is_readable ? 8 : (dark_tint ? ((bg_lvl < 42) ? (20 - (bg_lvl * 6) / 42) : 12) : 0);
  out_a_top = std::min(256, base_a + da);
  const int min_bot = is_readable ? 200 : kAlphaClearMin;
  out_a_bot = std::max(min_bot, base_a - (da * 2) / 3);
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

  for (const auto &bd : kBackdrops) {
    for (HostRgb fg : {kWhiteText, kMutedText}) {
      for (HostGlassVariant var : {HostGlassVariant::Clear, HostGlassVariant::ReadableDark}) {
        const HostRgb base_tint = (var == HostGlassVariant::ReadableDark) ? kReadableTint : kClearTint;
        HostRgb eff_tint{};
        int a_top = 0, a_bot = 0;
        host_resolve_glass_fill(true, bd, bd, base_tint, fg, var, eff_tint, a_top, a_bot);
        const int min_floor = (var == HostGlassVariant::ReadableDark) ? 200 : 148;
        LATTER_CHECK(a_bot >= min_floor && a_top >= a_bot && a_top <= 256);

        const float fg_lum = host_rel_lum(fg);
        const float top_cr = host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd, eff_tint, a_top)));
        const float bot_cr = host_contrast(fg_lum, host_rel_lum(host_mix_q8(bd, eff_tint, a_bot)));
        LATTER_CHECK(top_cr >= 4.5f && bot_cr >= 4.5f);
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
  const std::string comms_cpp = read_repo_file("../../components/latter_ui/page_comms.cpp");

  LATTER_CHECK(ui_types_h.find("enum class GlassVariant : uint8_t") != std::string::npos);
  LATTER_CHECK(ui_types_h.find("kChromeModalBg(16, 20, 28)") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassAlphaMinQ8 = 148") != std::string::npos);
  LATTER_CHECK(ui_gfx_h.find("kGlassReadableAlphaMinQ8 = 208") != std::string::npos);
  LATTER_CHECK(home_cpp.find("resolve_home_card_palette") == std::string::npos);
  LATTER_CHECK(tools_cpp.find("Color(230, 115, 0)") == std::string::npos);
  LATTER_CHECK(comms_cpp.find("kChromeGlassTint, 176") == std::string::npos);
}

void test_phase3_weather_sky_backdrop_and_home_scene() {
  using latter_ui::BackdropMode;
  using latter_ui::resolve_backdrop_mode;

  // 1. Single Authoritative Backdrop Precedence (D-038)
  // On Home (gui_page=1, home_subpage=0) AND Sky & Weather (gui_page=5):
  // weather_background ON (or weather_sim_active) wins over CustomWallpaper!
  LATTER_CHECK(resolve_backdrop_mode(1, 0, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, true, false, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, true, true) == BackdropMode::LiveWeatherSky);
  LATTER_CHECK(resolve_backdrop_mode(5, 2, false, true, true) == BackdropMode::LiveWeatherSky);
  // When weather_background is OFF (and sim inactive), CustomWallpaper renders when loaded, else SolidBlack
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, false, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(1, 0, false, false, false) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(5, 0, false, false, false) == BackdropMode::SolidBlack);
  // Home subpage 1 & 2 keep CustomWallpaper (dimmed on subpage 2) even while weather_background is ON!
  LATTER_CHECK(resolve_backdrop_mode(1, 1, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(1, 2, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(1, 2, true, false, false) == BackdropMode::SolidBlack);
  // System (4), Area (6), Remote (7), Tools (8), Home Hub (9), Music (2), Clock (0), Games (3)
  LATTER_CHECK(resolve_backdrop_mode(4, 0, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(6, 0, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(7, 0, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(8, 0, true, false, true, false) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(8, 0, true, false, true, true) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(9, 0, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(2, 0, true, false, true, false, true) == BackdropMode::CustomWallpaper);
  LATTER_CHECK(resolve_backdrop_mode(2, 0, true, false, true, false, false) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(0, 0, true, false, true) == BackdropMode::SolidBlack);
  LATTER_CHECK(resolve_backdrop_mode(3, 0, true, false, true) == BackdropMode::SolidBlack);

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
  prefs.ble_beacon_enabled = true;
  LATTER_CHECK(latter_ui::is_roaming_area_indicator_active());  // Host test defaults to portable (!LATTER_IS_STATIONARY)
  prefs.ble_beacon_enabled = false;
  LATTER_CHECK(!latter_ui::is_roaming_area_indicator_active());

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
  LATTER_CHECK(home_cpp.find("render_area_archetype_icon(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("register_home_activity_hit_regions(") != std::string::npos);
  LATTER_CHECK(home_cpp.find("std::max(prev_clip_y0, card_y0 + 1)") != std::string::npos);
  LATTER_CHECK(home_cpp.find("std::min(prev_clip_y1, card_y0 + card_h - 2)") != std::string::npos);
  LATTER_CHECK(home_cpp.find("Color(28, 54, 84)") == std::string::npos);
  LATTER_CHECK(touch_cpp.find("WidgetId::HOME_AREA_ICON") != std::string::npos);
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
  static_assert(resolve_idle_screen_timeout_ms(false, true, true, "15s", "30s") == 15000U);
  static_assert(resolve_idle_screen_timeout_ms(false, false, true, "5 min", "2 min") == 120000U);
  static_assert(resolve_idle_screen_timeout_ms(false, true, true, "Never", "30s") == 0U);
  static_assert(resolve_idle_screen_timeout_ms(true, true, false, "1 min", "30s") == 0U);
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
  static_assert(kDefPrefs.power_profile_enum() == PowerProfilePref::PortableCarry);
  static_assert(kDefPrefs.wake_word_mode_enum() == WakeWordMode::DockedOnly);
  static_assert(kDefPrefs.tap_to_wake_enum() == WakePolicyMode::DockedOnly);
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
  static_assert(kDefPrefs.manual_brightness_pct() == 90U);
  static_assert(kDefPrefs.day_brightness_pct() == 90U);
  static_assert(kDefPrefs.night_brightness_pct() == 70U);
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
  ac.dock_apps = "music,area,weather,tools";
  ac.wallpaper_url = "/api/latter/wp.jpg";
  ac.alert_id = 0;  // Seed initial alert_id
  rt.nav.gui_page = 6;
  rt.nav.area_subpage = 1;  // Unassigned picker should snap to entities overview on assignment!

  V3ReduceContext rctx{};
  rctx.now_ms = 5000U;
  rctx.friendly_name = "Kitchen Display";
  auto eff = v3_domain_reducer().reduce("area_context", "", rctx);
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
  LATTER_CHECK(latter_dock_order_csv() == "music,area,weather,tools");

  // Critical alert push on subsequent rev triggers banner + critical chime SFX 9
  ac.alert_id = 1;
  ac.alert_title = "Smoke Detected";
  ac.alert_message = "Check kitchen";
  ac.alert_priority = "critical";
  ac.alert_chime = true;
  ac.wallpaper_url.clear();
  rt.music.loaded_wallpaper_url = "/api/latter/wp.jpg";
  eff = v3_domain_reducer().reduce("area_context", ac.dock_apps, rctx);
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
  eff = v3_domain_reducer().reduce("active_media", "", rctx);
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
  eff = v3_domain_reducer().reduce("active_media", "", rctx);
  LATTER_CHECK(remote_vol_pct() == 80);  // Local 3s tap lockout preserved 80%

  ms.position = 1.0f;  // Matches 0.5s elapsed interpolation (no jump)
  rctx.now_ms = 6000U;
  eff = v3_domain_reducer().reduce("active_media", "", rctx);
  LATTER_CHECK(!eff.throttled_disp_update);

  // Position jump > 2.5s triggers throttled_disp_update
  ms.position = 45.0f;
  eff = v3_domain_reducer().reduce("active_media", "", rctx);
  LATTER_CHECK(eff.throttled_disp_update);

  // weather_forecast sun transition triggers brightness sync once
  weather_state().sun_state = "below_horizon";
  eff = v3_domain_reducer().reduce("weather_forecast", "", rctx);
  LATTER_CHECK(eff.sync_display_brightness && eff.weather_sim_dirty);
  eff = v3_domain_reducer().reduce("weather_forecast", "", rctx);
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
  std::printf(
      "[HOST TEST PASS] Protocol v3 (<charconv>) + V3DomainReducer + TimerAlarmController + "
      "DisplayLifecycleController + AudioPathArbitrator + FrameCadenceController + "
      "7-panel UiLayout/HitTable/DirtyBand + hal_battery verified.\n");
  return 0;
}
