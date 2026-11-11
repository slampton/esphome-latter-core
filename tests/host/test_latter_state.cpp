// Host-compiled unit tests for Protocol v3 state parsers and monotonic epoch/rev guards.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "../../include/latter_protocol.h"
#include "../../include/latter_state.h"
#include "../../include/latter_rpc.h"

namespace {

std::string build_v3_packet(uint32_t epoch, uint32_t rev, const std::vector<std::string> &tokens) {
  std::string out = "v3\x1f" + std::to_string(epoch) + "\x1f" + std::to_string(rev);
  for (size_t i = 3; i < tokens.size(); ++i) {
    out.push_back('\x1f');
    out += tokens[i];
  }
  return out;
}

void test_epoch_and_rev_monotonicity() {
  latter_ui::reset_all_v3_rev_guards();
  auto &st = latter_ui::remote_state();
  assert(st.epoch == 0 && st.rev == 0);

  std::vector<std::string> tokens(static_cast<size_t>(latter_ui::RemoteField::COUNT), "");
  tokens[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "2";
  tokens[static_cast<size_t>(latter_ui::RemoteField::CONFIGURED_COUNT)] = "2";
  tokens[static_cast<size_t>(latter_ui::RemoteField::SLOTS_SUMMARY)] = "Apple TV\x1eremote.apple_tv";

  // 1. Accept initial packet (epoch 100, rev 1; wire active_slot=2 -> 0-based st.active_slot=1)
  assert(latter_ui::parse_v3_remote_payload(build_v3_packet(100, 1, tokens)));
  assert(st.epoch == 100 && st.rev == 1);
  assert(st.active_slot == 1);
  assert(st.configured_count == 2);
  assert(st.slots_summary == "Apple TV\x1eremote.apple_tv");

  // 2. Reject duplicate rev=1 in same epoch=100
  tokens[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "9";
  assert(!latter_ui::parse_v3_remote_payload(build_v3_packet(100, 1, tokens)));
  assert(st.active_slot == 1);

  // 3. Accept rev=2 in same epoch=100 (wire active_slot=3 -> 0-based st.active_slot=2)
  tokens[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "3";
  assert(latter_ui::parse_v3_remote_payload(build_v3_packet(100, 2, tokens)));
  assert(st.epoch == 100 && st.rev == 2);
  assert(st.active_slot == 2);

  // 4. Reject lower rev=1 in same epoch=100
  tokens[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "7";
  assert(!latter_ui::parse_v3_remote_payload(build_v3_packet(100, 1, tokens)));
  assert(st.active_slot == 2);

  // 5. Accept HA restart (new epoch=200 with rev=1; wire active_slot=1 -> 0-based st.active_slot=0)
  tokens[static_cast<size_t>(latter_ui::RemoteField::ACTIVE_SLOT)] = "1";
  assert(latter_ui::parse_v3_remote_payload(build_v3_packet(200, 1, tokens)));
  assert(st.epoch == 200 && st.rev == 1);
  assert(st.active_slot == 0);

  // 6. Reset all guards on API reconnect -> accepts epoch=200, rev=1 again
  latter_ui::reset_all_v3_rev_guards();
  assert(st.epoch == 0 && st.rev == 0);
  assert(latter_ui::parse_v3_remote_payload(build_v3_packet(200, 1, tokens)));
  assert(st.epoch == 200 && st.rev == 1);
}

void test_all_five_domain_parsers() {
  latter_ui::reset_all_v3_rev_guards();

  // Area Context (COUNT = 40)
  std::vector<std::string> area_t(static_cast<size_t>(latter_ui::AreaCtxField::COUNT), "");
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::AREA_ID)] = "office";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::AREA_NAME)] = "Office";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::AREA_TEMP)] = "71.5";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::LIGHTS_ON)] = "2";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::WALLPAPER_URL)] = "/api/latter/wallpaper/desk?sig=abc";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ALERT_ID)] = "42";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ALERT_TITLE)] = "Front Door";
  assert(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 1, area_t)));
  assert(latter_ui::area_ctx_state().area_id == "office");
  assert(latter_ui::area_ctx_state().lights_on == 2);
  assert(latter_ui::area_ctx_state().alert_id == 42);

  // Activity Card setting: empty fields take the spec defaults (card on, every source).
  assert(latter_ui::activity_source_mask() == latter_ui::kActivitySourceAll);
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_CARD)] = "1";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_SOURCES)] = "calendar,timers,future_key";
  assert(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 2, area_t)));
  assert(latter_ui::activity_source_mask() == ((1U << 5) | (1U << 1)));
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_CARD)] = "0";
  assert(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 3, area_t)));
  assert(latter_ui::activity_source_mask() == 0);
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_CARD)] = "";
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::ACTIVITY_SOURCES)] = "";
  assert(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 4, area_t)));
  assert(latter_ui::activity_source_mask() == latter_ui::kActivitySourceAll);

  // Active Media (COUNT = 20, including position_updated_at & is_local)
  std::vector<std::string> media_t(static_cast<size_t>(latter_ui::MediaField::COUNT), "");
  media_t[static_cast<size_t>(latter_ui::MediaField::STATE)] = "playing";
  media_t[static_cast<size_t>(latter_ui::MediaField::ACTIVE_SPEAKER)] = "media_player.desk_assistant";
  media_t[static_cast<size_t>(latter_ui::MediaField::TITLE)] = "Time";
  media_t[static_cast<size_t>(latter_ui::MediaField::ARTIST)] = "Pink Floyd";
  media_t[static_cast<size_t>(latter_ui::MediaField::DURATION)] = "413";
  media_t[static_cast<size_t>(latter_ui::MediaField::POSITION)] = "120";
  media_t[static_cast<size_t>(latter_ui::MediaField::POSITION_UPDATED_AT)] = "1761900100";
  media_t[static_cast<size_t>(latter_ui::MediaField::VOLUME)] = "65";
  media_t[static_cast<size_t>(latter_ui::MediaField::IS_LOCAL)] = "1";
  assert(latter_ui::parse_v3_media_payload(build_v3_packet(10, 1, media_t)));
  assert(latter_ui::media_state().title == "Time");
  assert(latter_ui::media_state().position_updated_at == 1761900100u);
  assert(latter_ui::media_state().has_volume);
  assert(std::abs(latter_ui::media_state().volume - 0.65f) < 0.001f);
  assert(latter_ui::media_state().is_local);

  // Empty volume field preserves previous volume and marks has_volume = false
  media_t[static_cast<size_t>(latter_ui::MediaField::VOLUME)] = "";
  assert(latter_ui::parse_v3_media_payload(build_v3_packet(10, 2, media_t)));
  assert(!latter_ui::media_state().has_volume);
  assert(std::abs(latter_ui::media_state().volume - 0.65f) < 0.001f);

  // 1% volume ("1") normalizes to 0.01f (not 1.0f), and 100% ("100") to 1.0f
  media_t[static_cast<size_t>(latter_ui::MediaField::VOLUME)] = "1";
  assert(latter_ui::parse_v3_media_payload(build_v3_packet(10, 3, media_t)));
  assert(latter_ui::media_state().has_volume);
  assert(std::abs(latter_ui::media_state().volume - 0.01f) < 0.0005f);
  media_t[static_cast<size_t>(latter_ui::MediaField::VOLUME)] = "100";
  assert(latter_ui::parse_v3_media_payload(build_v3_packet(10, 4, media_t)));
  assert(std::abs(latter_ui::media_state().volume - 1.00f) < 0.0005f);

  // Weather Forecast (COUNT = 17, including sun + moon fields)
  std::vector<std::string> weather_t(static_cast<size_t>(latter_ui::WeatherField::COUNT), "");
  weather_t[static_cast<size_t>(latter_ui::WeatherField::CONDITION)] = "sunny";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::TEMPERATURE)] = "68";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::HIGH)] = "72";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::LOW)] = "54";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::FORECAST_5D)] = "Mon|sunny|72|54";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::SUN_STATE)] = "above_horizon";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::SUN_ELEVATION)] = "42.5";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::SUN_AZIMUTH)] = "198.2";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::SUN_NEXT_RISING)] = "2026-11-11T14:45:00Z";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::SUN_NEXT_SETTING)] = "2026-11-11T01:02:00Z";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::MOON_PHASE)] = "waxing_gibbous";
  assert(latter_ui::parse_v3_weather_payload(build_v3_packet(10, 1, weather_t)));
  assert(latter_ui::weather_state().condition == "sunny");
  assert(latter_ui::weather_state().forecast_5d == "Mon|sunny|72|54");
  assert(latter_ui::weather_state().sun_state == "above_horizon");
  assert(std::abs(latter_ui::weather_state().sun_elevation - 42.5f) < 0.01f);
  assert(std::abs(latter_ui::weather_state().sun_azimuth - 198.2f) < 0.01f);
  assert(latter_ui::weather_state().moon_phase == "waxing_gibbous");

  // Security PIN policy field on area_context
  area_t[static_cast<size_t>(latter_ui::AreaCtxField::SECURITY_PIN_POLICY)] = "require_pin";
  assert(latter_ui::parse_v3_area_ctx_payload(build_v3_packet(10, 5, area_t)));
  assert(latter_ui::area_ctx_state().security_pin_policy == "require_pin");
  assert(latter_ui::v3_mismatch_warn_count() == 0);

  // Version skew tolerance: shorter payload (>= kProtocolMinRequiredFields) defaults trailing fields and records warning
  std::vector<std::string> short_weather(11, "");
  short_weather[static_cast<size_t>(latter_ui::WeatherField::CONDITION)] = "clear-night";
  assert(latter_ui::parse_v3_weather_payload(build_v3_packet(10, 2, short_weather)));
  assert(latter_ui::weather_state().condition == "clear-night");
  assert(latter_ui::weather_state().sun_state.empty());
  assert(latter_ui::v3_mismatch_warn_count() == 1);

  // Calendar Agenda (COUNT = 6)
  std::vector<std::string> cal_t(static_cast<size_t>(latter_ui::CalendarField::COUNT), "");
  cal_t[static_cast<size_t>(latter_ui::CalendarField::SUMMARY)] = "Today|14:00|Architecture Review";
  cal_t[static_cast<size_t>(latter_ui::CalendarField::EVENT_COUNT)] = "1";
  cal_t[static_cast<size_t>(latter_ui::CalendarField::NEXT_EVENT)] = "Architecture Review (14:00)";
  assert(latter_ui::parse_v3_calendar_payload(build_v3_packet(10, 1, cal_t)));
  assert(latter_ui::calendar_state().count == 1);
  assert(latter_ui::calendar_state().next_event == "Architecture Review (14:00)");
}

struct FakeOutboxSensor {
  std::vector<std::string> published;
  void publish_state(const std::string &s) { published.push_back(s); }
};

void test_rpc_outbox_framing_and_req_id() {
  FakeOutboxSensor outbox;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};

  // Single-frame RPC dispatches immediately on emit()
  latter_rpc::emit(&outbox, "set_active_area", {{"area_id", "kitchen"}});
  assert(outbox.published.size() == 1);
  assert(outbox.published[0].size() <= latter_rpc::kMaxRpcFrameBytes);
  assert(outbox.published[0].find(":1:1/1|{\"s\":\"set_active_area\",\"d\":{\"area_id\":\"kitchen\"}}") != std::string::npos);

  // Multi-chunk RPC (> 230 B, with multi-byte UTF-8 glyphs) splits cleanly across loop ticks
  std::string big_manifest(380, 'x');
  big_manifest.replace(195, 9, "\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94");  // UTF-8 em-dashes across cut zone
  uint32_t t = 100;
  latter_rpc::step_outbox_loop(t);
  t += 30;
  latter_rpc::step_outbox_loop(t);
  latter_rpc::emit(&outbox, "device_hello", {
      {"contract", "3"},
      {"boot_id", "12345"},
      {"manifest", big_manifest},
  });
  // First chunk published immediately; remaining chunks wait for >= 2 loop passes and >= 25 ms
  assert(outbox.published.size() == 2);
  assert(latter_rpc::has_pending_frames());
  while (latter_rpc::has_pending_frames()) {
    t += 15;
    latter_rpc::step_outbox_loop(t);
  }
  assert(outbox.published.size() >= 4);
  std::string reassembled;
  for (size_t i = 1; i < outbox.published.size(); ++i) {
    assert(outbox.published[i].size() <= latter_rpc::kMaxRpcFrameBytes);
    size_t bar = outbox.published[i].find('|');
    assert(bar != std::string::npos);
    reassembled += outbox.published[i].substr(bar + 1);
  }
  assert(reassembled.find("\"s\":\"device_hello\"") != std::string::npos);
  assert(reassembled.find(big_manifest) != std::string::npos);

  // Request-ID monotonic guard discards out-of-order responses
  int r1 = latter_rpc::issue_req_id(latter_rpc::SlotChannel::MUSIC);
  int r2 = latter_rpc::issue_req_id(latter_rpc::SlotChannel::MUSIC);
  assert(!latter_rpc::accept_req_id(latter_rpc::SlotChannel::MUSIC, r1));
  assert(latter_rpc::accept_req_id(latter_rpc::SlotChannel::MUSIC, r2));
  assert(latter_rpc::accept_req_id(latter_rpc::SlotChannel::MUSIC, 0));
}

}  // namespace

int main() {
  test_epoch_and_rev_monotonicity();
  test_all_five_domain_parsers();
  test_rpc_outbox_framing_and_req_id();
  std::printf("[HOST TEST PASS] All Protocol v3 parsers, version tolerance, and RPC outbox verified.\n");
  return 0;
}
