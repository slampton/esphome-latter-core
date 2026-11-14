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
#include "../../include/hal_manifest.h"
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
  const std::string timers_yaml = read_repo_file("../../modules/timers_and_fetchers.yaml");
  const std::string pwr_yaml = read_repo_file("../../modules/power_manager.yaml");
  const std::string weather_cpp = read_repo_file("../../components/latter_ui/page_weather.cpp");

  LATTER_CHECK(gfx_cpp.find("last_ambient_anim_ms() = start_m") != std::string::npos);
  LATTER_CHECK(comp_h.find("sync_disp_poller_for_anim(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("sync_disp_poller_for_anim(&id(disp1), anim_driving_disp)") != std::string::npos);
  LATTER_CHECK(pwr_yaml.find("sync_disp_poller_for_anim(&id(disp1), ss_anim_driving)") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("speaker_audio_busy") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("mww_passive_busy") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("app_weather_active") != std::string::npos);
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
  LATTER_CHECK(static_cast<size_t>(latter_ui::AreaCtxField::COUNT) == 43U);

  // Weather dock token + 10-slot dock array capacity (8 user tokens + Settings)
  LATTER_CHECK(latter_ui::kMaxDockApps == 10);
  LATTER_CHECK(latter_ui::dock_app_id_for_token("weather") == 5);
  LATTER_CHECK(latter_ui::dock_app_id_for_token("camera") == 12);

  // Ordered Home Hub category CSV parser & collect_home_hub_tabs
  int wh_tabs[4] = {-1, -1, -1, -1};
  latter_ui::AreaContextState uninit_ac{};
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(wh_tabs, uninit_ac) == 4);
  LATTER_CHECK(wh_tabs[0] == 0 && wh_tabs[1] == 1 && wh_tabs[2] == 2 && wh_tabs[3] == 3);
  uninit_ac.epoch = 10U;
  uninit_ac.home_categories = "vacuum,thermostat,unknown,vacuum";
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(wh_tabs, uninit_ac) == 2);
  LATTER_CHECK(wh_tabs[0] == 3 && wh_tabs[1] == 1);
  uninit_ac.home_categories.clear();
  LATTER_CHECK(latter_ui::collect_home_hub_tabs(wh_tabs, uninit_ac) == 0);

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
  LATTER_CHECK(LATTER_SDK_REV == 2026111304L);

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
  latter_ui::area_ctx_state().theme_color_hex = "#33AA77";
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
  LATTER_CHECK(latter_ui::area_ctx_state().theme_color_hex == "#33AA77");
  LATTER_CHECK(latter_ui::persisted_prefs().custom_theme_rgb == 0x33AA77U);
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
  latter_ui::area_ctx_state().theme_color_hex = "#FF5500";
  latter_ui::weather_state().condition = "sunny";
  latter_ui::weather_state().temperature = 74.0f;
  latter_ui::weather_state().sun_state = "above_horizon";
  LATTER_CHECK(latter_ui::load_hibernate_snapshot_if_cold());
  LATTER_CHECK(latter_ui::area_ctx_state().dock_apps == "clock,tools");
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Office");
  LATTER_CHECK(latter_ui::area_ctx_state().theme_color_hex == "#FF5500");
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
  ac_tok[static_cast<size_t>(latter_ui::AreaCtxField::THEME_COLOR_HEX)] = "#1188CC";
  LATTER_CHECK(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(1, 1, ac_tok)));
  LATTER_CHECK(latter_ui::area_ctx_state().epoch == 1U && latter_ui::area_ctx_state().rev == 1U);
  LATTER_CHECK(latter_ui::area_ctx_state().area_name == "Kitchen");
  LATTER_CHECK(latter_ui::area_ctx_state().dock_apps == "clock,calendar");
  LATTER_CHECK(latter_ui::area_ctx_state().theme_color_hex == "#1188CC");

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
  // System (4), Area (6), Remote (7), Tools (8), Whole-Home (9), Music (2), Clock (0), Games (3)
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
  LATTER_CHECK(timers_yaml.find("resolve_backdrop_mode(") != std::string::npos);
  LATTER_CHECK(timers_yaml.find("weather_sky_motion_stepping(") != std::string::npos);
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
  std::printf(
      "[HOST TEST PASS] Protocol v3 + device_prefs v2 + RPC (<=240B UTF-8), 7-panel UiLayout/HitTable/DirtyBand, "
      "2-tier GlassVariant + Y-scissor, Phase 3 WeatherSkyEngine/BackdropMode/HomeScene, HibernateSnapshot + BLE roaming, and hal_battery verified.\n");
  return 0;
}
