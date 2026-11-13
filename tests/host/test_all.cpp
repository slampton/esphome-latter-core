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
#include "../../include/latter_state.h"
#include "../../include/latter_rpc.h"
#include "../../include/latter_actions.h"
#include "../../include/hal_battery.h"
#include "../../include/hal_link_report.h"
#include "../../include/hal_panel_power.h"
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
  LATTER_CHECK(latter_ui::parse_v3_media_payload(build_v3_packet(10, 3, media_t)));
  LATTER_CHECK(latter_ui::media_state().has_volume);
  LATTER_CHECK(std::fabs(latter_ui::media_state().volume - 1.00f) < 0.0005f);

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
  future_cal[static_cast<size_t>(latter_ui::CalendarField::NEXT_EVENT)] = "Standup";
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

  // 6. Verify production C++ sources reference the shared policies
  const std::string batt_h = read_repo_file("../../include/hal_battery.h");
  const std::string pmu_cpp = read_repo_file("../../components/latter_core/power_battery.cpp");
  const std::string link_h = read_repo_file("../../include/hal_link_report.h");

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
  bool failed{false};
  int enables{0};
  int disables{0};
  bool is_active() const { return active; }
  bool is_failed() const { return failed; }
  void enable() {
    active = true;
    ++enables;
  }
  void disable() {
    active = false;
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
  LATTER_CHECK(latter_ui::kPersistedPrefsVersion == 2U);

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
}

}  // namespace

int main() {
  test_protocol_v3_and_rpc();
  test_ui_layout_hit_table_and_dirty_band();
  test_battery_model_and_power_policy();
  test_music_remote_override_and_volume_feedback();
  test_beacon_boot_gate_and_touch_reboot_safety();
  test_device_prefs_v2_and_event_timer_sync();
  std::printf(
      "[HOST TEST PASS] Protocol v3 + device_prefs v2 + RPC (<=240B UTF-8), 7-panel UiLayout/HitTable/DirtyBand, "
      "music override/volume feedback, beacon boot gate + touch reboot safety, and hal_battery/hal_link_report verified.\n");
  return 0;
}
