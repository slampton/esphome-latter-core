// Host-compiled unit tests for Protocol v3 state parsers and monotonic epoch/rev guards.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "../../include/latter_protocol.h"
#include "../../include/latter_state.h"

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

  // Weather Forecast (COUNT = 11)
  std::vector<std::string> weather_t(static_cast<size_t>(latter_ui::WeatherField::COUNT), "");
  weather_t[static_cast<size_t>(latter_ui::WeatherField::CONDITION)] = "sunny";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::TEMPERATURE)] = "68";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::HIGH)] = "72";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::LOW)] = "54";
  weather_t[static_cast<size_t>(latter_ui::WeatherField::FORECAST_5D)] = "Mon|sunny|72|54";
  assert(latter_ui::parse_v3_weather_payload(build_v3_packet(10, 1, weather_t)));
  assert(latter_ui::weather_state().condition == "sunny");
  assert(latter_ui::weather_state().forecast_5d == "Mon|sunny|72|54");

  // Calendar Agenda (COUNT = 6)
  std::vector<std::string> cal_t(static_cast<size_t>(latter_ui::CalendarField::COUNT), "");
  cal_t[static_cast<size_t>(latter_ui::CalendarField::SUMMARY)] = "Today|14:00|Architecture Review";
  cal_t[static_cast<size_t>(latter_ui::CalendarField::EVENT_COUNT)] = "1";
  cal_t[static_cast<size_t>(latter_ui::CalendarField::NEXT_EVENT)] = "Architecture Review (14:00)";
  assert(latter_ui::parse_v3_calendar_payload(build_v3_packet(10, 1, cal_t)));
  assert(latter_ui::calendar_state().count == 1);
  assert(latter_ui::calendar_state().next_event == "Architecture Review (14:00)");
}

}  // namespace

int main() {
  test_epoch_and_rev_monotonicity();
  test_all_five_domain_parsers();
  std::printf("[HOST TEST PASS] All Protocol v3 parsers and epoch/rev guards verified.\n");
  return 0;
}
