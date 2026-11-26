#include "test_harness.h"

namespace latter_test {

namespace {

void test_protocol_v3_and_rpc() {
  latter_core::reset_all_v3_rev_guards();
  auto &rem = latter_core::remote_state();
  LATTER_CHECK(rem.epoch == 0 && rem.rev == 0);

  std::vector<std::string> r_tok(static_cast<size_t>(latter_core::RemoteField::COUNT), "");
  r_tok[static_cast<size_t>(latter_core::RemoteField::ACTIVE_SLOT)] = "2";
  r_tok[static_cast<size_t>(latter_core::RemoteField::CONFIGURED_COUNT)] = "2";
  r_tok[static_cast<size_t>(latter_core::RemoteField::SLOTS_SUMMARY)] = "Apple TV\x1e" "remote.apple_tv";

  LATTER_CHECK(latter_core::parse_v3_remote_payload(build_v3_packet(100, 1, r_tok)));
  LATTER_CHECK(rem.epoch == 100 && rem.rev == 1 && rem.active_slot == 1 && rem.configured_count == 2);

  r_tok[static_cast<size_t>(latter_core::RemoteField::ACTIVE_SLOT)] = "9";
  LATTER_CHECK(!latter_core::parse_v3_remote_payload(build_v3_packet(100, 1, r_tok)));
  LATTER_CHECK(rem.active_slot == 1);

  r_tok[static_cast<size_t>(latter_core::RemoteField::ACTIVE_SLOT)] = "3";
  LATTER_CHECK(latter_core::parse_v3_remote_payload(build_v3_packet(100, 2, r_tok)));
  LATTER_CHECK(rem.epoch == 100 && rem.rev == 2 && rem.active_slot == 2);

  LATTER_CHECK(!latter_core::parse_v3_remote_payload(build_v3_packet(100, 1, r_tok)));
  LATTER_CHECK(rem.active_slot == 2);

  r_tok[static_cast<size_t>(latter_core::RemoteField::ACTIVE_SLOT)] = "1";
  LATTER_CHECK(latter_core::parse_v3_remote_payload(build_v3_packet(200, 1, r_tok)));
  LATTER_CHECK(rem.epoch == 200 && rem.rev == 1 && rem.active_slot == 0);

  latter_core::reset_all_v3_rev_guards();
  LATTER_CHECK(rem.epoch == 0 && rem.rev == 0);
  LATTER_CHECK(latter_core::parse_v3_remote_payload(build_v3_packet(200, 1, r_tok)));

  const std::string bad_v2 = std::string("v2\x1f") + "200\x1f" + "9\x1f" + "1\x1f" + "1\x1f" + "a";
  const std::string sub_floor = std::string("v3\x1f") + "200";
  LATTER_CHECK(!latter_core::parse_v3_remote_payload(bad_v2));
  LATTER_CHECK(!latter_core::parse_v3_remote_payload(sub_floor));
  LATTER_CHECK(!latter_core::parse_v3_area_ctx_payload(sub_floor));
  LATTER_CHECK(!latter_core::parse_v3_media_payload(sub_floor));
  LATTER_CHECK(!latter_core::parse_v3_weather_payload(sub_floor));
  LATTER_CHECK(!latter_core::parse_v3_calendar_payload(sub_floor));

  latter_core::reset_all_v3_rev_guards();

  std::vector<std::string> area_t(static_cast<size_t>(latter_core::AreaCtxField::COUNT), "");
  area_t[static_cast<size_t>(latter_core::AreaCtxField::AREA_ID)] = "office";
  area_t[static_cast<size_t>(latter_core::AreaCtxField::AREA_NAME)] = "Office";
  area_t[static_cast<size_t>(latter_core::AreaCtxField::AREA_TEMP)] = "71.5";
  area_t[static_cast<size_t>(latter_core::AreaCtxField::LIGHTS_ON)] = "2";
  area_t[static_cast<size_t>(latter_core::AreaCtxField::ALERT_ID)] = "42";
  area_t[static_cast<size_t>(latter_core::AreaCtxField::SECURITY_PIN_POLICY)] = "require_pin";
  LATTER_CHECK(latter_core::parse_v3_area_ctx_payload(build_v3_packet(10, 1, area_t)));
  LATTER_CHECK(latter_core::area_ctx_state().area_id == "office");
  LATTER_CHECK(latter_core::area_ctx_state().lights_on == 2);
  LATTER_CHECK(latter_core::area_ctx_state().alert_id == 42);
  LATTER_CHECK(latter_core::area_ctx_state().security_pin_policy == "require_pin");
  LATTER_CHECK(latter_core::activity_source_mask() == latter_core::kActivitySourceAll);

  area_t[static_cast<size_t>(latter_core::AreaCtxField::ACTIVITY_CARD)] = "1";
  area_t[static_cast<size_t>(latter_core::AreaCtxField::ACTIVITY_SOURCES)] = "calendar,timers,future_key";
  LATTER_CHECK(latter_core::parse_v3_area_ctx_payload(build_v3_packet(10, 2, area_t)));
  LATTER_CHECK(latter_core::activity_source_mask() == ((1U << 5) | (1U << 1)));

  area_t[static_cast<size_t>(latter_core::AreaCtxField::ACTIVITY_CARD)] = "0";
  LATTER_CHECK(latter_core::parse_v3_area_ctx_payload(build_v3_packet(10, 3, area_t)));
  LATTER_CHECK(latter_core::activity_source_mask() == 0);

  area_t[static_cast<size_t>(latter_core::AreaCtxField::ACTIVITY_CARD)] = "";
  area_t[static_cast<size_t>(latter_core::AreaCtxField::ACTIVITY_SOURCES)] = "";
  LATTER_CHECK(latter_core::parse_v3_area_ctx_payload(build_v3_packet(10, 4, area_t)));
  LATTER_CHECK(latter_core::activity_source_mask() == latter_core::kActivitySourceAll);

  std::vector<std::string> media_t(static_cast<size_t>(latter_core::MediaField::COUNT), "");
  media_t[static_cast<size_t>(latter_core::MediaField::STATE)] = "playing";
  media_t[static_cast<size_t>(latter_core::MediaField::TITLE)] = "Time";
  media_t[static_cast<size_t>(latter_core::MediaField::VOLUME)] = "1";
  media_t[static_cast<size_t>(latter_core::MediaField::LAST_SPEAKER)] = "media_player.office_speaker";
  media_t[static_cast<size_t>(latter_core::MediaField::IS_LOCAL)] = "1";
  LATTER_CHECK(latter_core::parse_v3_media_payload(build_v3_packet(10, 1, media_t)));
  LATTER_CHECK(latter_core::media_state().has_volume);
  LATTER_CHECK(std::fabs(latter_core::media_state().volume - 0.01f) < 0.0005f);
  LATTER_CHECK(latter_core::media_state().last_speaker == "media_player.office_speaker");
  LATTER_CHECK(latter_core::media_state().is_local);

  media_t[static_cast<size_t>(latter_core::MediaField::VOLUME)] = "";
  LATTER_CHECK(latter_core::parse_v3_media_payload(build_v3_packet(10, 2, media_t)));
  LATTER_CHECK(!latter_core::media_state().has_volume);
  LATTER_CHECK(std::fabs(latter_core::media_state().volume - 0.01f) < 0.0005f);

  media_t[static_cast<size_t>(latter_core::MediaField::VOLUME)] = "100";
  LATTER_CHECK(latter_core::parse_v3_media_payload(build_v3_packet(10, 3, media_t)));
  LATTER_CHECK(latter_core::media_state().has_volume);
  LATTER_CHECK(std::fabs(latter_core::media_state().volume - 1.00f) < 0.0005f);
  static_assert(static_cast<size_t>(latter_core::MediaField::IS_DEFAULT_SPEAKER) == 23U);
  static_assert(static_cast<size_t>(latter_core::MediaField::COUNT) == 24U);
  static_assert(static_cast<size_t>(latter_core::WeatherField::TEMP_UNIT) == 17U);
  static_assert(static_cast<size_t>(latter_core::WeatherField::COUNT) == 18U);

  std::vector<std::string> weather_t(static_cast<size_t>(latter_core::WeatherField::COUNT), "");
  weather_t[static_cast<size_t>(latter_core::WeatherField::CONDITION)] = "sunny";
  weather_t[static_cast<size_t>(latter_core::WeatherField::TEMPERATURE)] = "68";
  weather_t[static_cast<size_t>(latter_core::WeatherField::WIND_BEARING)] = "247.5";
  weather_t[static_cast<size_t>(latter_core::WeatherField::SUN_STATE)] = "above_horizon";
  weather_t[static_cast<size_t>(latter_core::WeatherField::MOON_PHASE)] = "waxing_gibbous";
  weather_t[static_cast<size_t>(latter_core::WeatherField::TEMP_UNIT)] = "C";
  LATTER_CHECK(latter_core::parse_v3_weather_payload(build_v3_packet(10, 1, weather_t)));
  LATTER_CHECK(latter_core::weather_state().condition == "sunny");
  LATTER_CHECK(latter_core::weather_state().sun_state == "above_horizon");
  LATTER_CHECK(std::fabs(latter_core::weather_state().wind_bearing - 247.5f) < 0.01f);
  LATTER_CHECK(latter_core::weather_state().temp_unit == "C");
  LATTER_CHECK(latter_core::v3_mismatch_warn_count() == 0);

  std::vector<std::string> min_weather(latter_core::kProtocolMinRequiredFields, "");
  if (min_weather.size() > static_cast<size_t>(latter_core::WeatherField::CONDITION)) {
    min_weather[static_cast<size_t>(latter_core::WeatherField::CONDITION)] = "clear-night";
  }
  LATTER_CHECK(latter_core::parse_v3_weather_payload(build_v3_packet(10, 2, min_weather)));
  LATTER_CHECK(latter_core::v3_mismatch_warn_count() == 1);

  std::vector<std::string> future_cal(static_cast<size_t>(latter_core::CalendarField::COUNT) + 3, "future_ext");
  future_cal[static_cast<size_t>(latter_core::CalendarField::SUMMARY)] = "Today|09:00|Standup";
  future_cal[static_cast<size_t>(latter_core::CalendarField::EVENT_COUNT)] = "1";
  LATTER_CHECK(latter_core::parse_v3_calendar_payload(build_v3_packet(10, 1, future_cal)));
  LATTER_CHECK(latter_core::calendar_state().count == 1);
  LATTER_CHECK(latter_core::v3_mismatch_warn_count() == 2);

  struct FakeOutbox {
    std::vector<std::string> frames;
    void publish_state(const std::string &s) { frames.push_back(s); }
  } outbox;
  latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
  LATTER_CHECK(latter_rpc::kMaxRpcFrameBytes <= 240);

  latter_rpc::emit(&outbox, "set_active_area", {{"area_id", "kitchen"}});
  LATTER_CHECK(outbox.frames.size() == 1);
  LATTER_CHECK(outbox.frames[0].size() <= 240 && outbox.frames[0].size() <= latter_rpc::kMaxRpcFrameBytes);

  std::string utf8_stress;
  for (int i = 0; i < 95; ++i) {
    utf8_stress += (i % 2 == 0) ? "\xE2\x80\x94" : "\xF0\x9F\x8E\xB5";
    utf8_stress += "ab";
  }
  uint32_t now_ms = 1000;
  latter_rpc::step_outbox_loop(now_ms);
  now_ms += 40;
  latter_rpc::step_outbox_loop(now_ms);
  latter_rpc::emit(&outbox, "device_hello", {
      {"contract", "3"},
      {"boot_id", "777"},
      {"manifest", utf8_stress},
  });
  LATTER_CHECK(latter_rpc::has_pending_frames());
  const size_t after_emit_count = outbox.frames.size();
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

  for (size_t k = 0; k < latter_rpc::kRpcQueueCapacity + 8; ++k) {
    latter_rpc::emit(&outbox, "area_action", {{"entity_id", "light.flood"}, {"action", "toggle"}});
  }
  LATTER_CHECK(latter_rpc::rpc_state().count <= latter_rpc::kRpcQueueCapacity);
  while (latter_rpc::has_pending_frames()) {
    now_ms += 20;
    latter_rpc::step_outbox_loop(now_ms);
  }

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

void test_v3_domain_reducer_and_timer_alarm() {
  using namespace latter_ui;
  using namespace latter_core;

  reset_runtime_state();

  const char raw_packed[] = "123456789";
  const std::string_view slice_123(raw_packed, 3);
  LATTER_CHECK(v3_parse_int(slice_123, -1) == 123);
  LATTER_CHECK(v3_parse_u32(slice_123, 0U) == 123U);
  LATTER_CHECK(std::fabs(v3_parse_float(slice_123, -1.0f) - 123.0f) < 0.001f);

  LATTER_CHECK(v3_parse_int("  -42 ", 0) == -42);
  LATTER_CHECK(v3_parse_int("\t+85%", 0) == 85);
  LATTER_CHECK(v3_parse_int("+-5", 99) == 99);
  LATTER_CHECK(v3_parse_int("99999999999999999999", -7) == -7);
  LATTER_CHECK(v3_parse_u32(" -1", 55U) == 55U);
  LATTER_CHECK(v3_parse_u32(" +2026111306", 0U) == 2026111306U);
  LATTER_CHECK(v3_parse_u32("99999999999999999999", 77U) == 77U);
  LATTER_CHECK(std::fabs(v3_parse_float("  +72.5", 0.0f) - 72.5f) < 0.001f);
  LATTER_CHECK(std::fabs(v3_parse_float("-14.25", 0.0f) - (-14.25f)) < 0.001f);
  LATTER_CHECK(std::isnan(v3_parse_float("invalid")));

  auto &rt = runtime_state();
  auto &prefs = persisted_prefs();
  prefs.color_profile = 4;

  auto &ac = area_ctx_state();
  ac.epoch = 1U;
  ac.rev = 1U;
  ac.area_id = "kitchen";
  ac.area_name = "Kitchen";
  ac.area_icon = "kitchen";
  ac.site_id = "example_site";
  ac.ble_closest_proxy = "proxy_kitchen";
  ac.available_domains = "light,climate";
  ac.wallpaper_url = "/api/latter/wp.jpg";
  ac.alert_id = 0;
  rt.nav.gui_page = 6;
  rt.nav.area_subpage = 1;

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
  LATTER_CHECK(latter_site_id() == "example_site");

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

  rt.music.loaded_art_url = ms.art_url;
  last_local_vol_tap_ms() = 5200U;
  remote_vol_pct() = 80;
  ms.volume = 0.40f;
  ms.position = 0.5f;
  rctx.now_ms = 5500U;
  eff = v3_domain_reducer().reduce("active_media", rctx);
  LATTER_CHECK(remote_vol_pct() == 80);

  ms.position = 1.0f;
  rctx.now_ms = 6000U;
  eff = v3_domain_reducer().reduce("active_media", rctx);
  LATTER_CHECK(!eff.throttled_disp_update);

  ms.position = 45.0f;
  eff = v3_domain_reducer().reduce("active_media", rctx);
  LATTER_CHECK(eff.throttled_disp_update);

  weather_state().sun_state = "below_horizon";
  eff = v3_domain_reducer().reduce("weather_forecast", rctx);
  LATTER_CHECK(eff.sync_display_brightness && eff.weather_sim_dirty);
  eff = v3_domain_reducer().reduce("weather_forecast", rctx);
  LATTER_CHECK(!eff.sync_display_brightness);

  reset_runtime_state();
  auto &tc = timer_alarm_controller();

  auto va_eff = tc.on_va_timer_started("t1", "Pasta", 180, 2, true, 1000U);
  LATTER_CHECK(rt.timers.running);
  LATTER_CHECK(timer_slots()[0].duration_sec == 180);
  LATTER_CHECK(va_eff.schedule_disp);

  auto t_eff = tc.tick_timers_250ms(3100U);
  LATTER_CHECK(t_eff.any_expired);
  LATTER_CHECK(rt.timers.ringing);
  LATTER_CHECK(!timer_slots()[0].running);
  LATTER_CHECK(timer_slots()[0].remaining_sec == 180);

  t_eff = tc.tick_timers_250ms(303099U);
  LATTER_CHECK(!t_eff.auto_silenced && rt.timers.ringing);
  t_eff = tc.tick_timers_250ms(303100U);
  LATTER_CHECK(t_eff.auto_silenced && !rt.timers.ringing);

  prefs.alarms[0].enabled = true;
  prefs.alarms[0].hour = 7;
  prefs.alarms[0].minute = 30;
  prefs.alarms[0].recurrence = 3;
  auto a_eff = tc.tick_alarms_1s(true, 1700004600U, 7, 30, 5, 2, 10000U);
  LATTER_CHECK(a_eff.triggered_idx == 1);
  LATTER_CHECK(a_eff.prefs_saved);
  LATTER_CHECK(!prefs.alarms[0].enabled);
  LATTER_CHECK(rt.alarms.ringing);

  LATTER_CHECK(tc.snooze_active_alarm(true, 1700004600U, 12000U));
  LATTER_CHECK(!rt.alarms.ringing);
  LATTER_CHECK(rt.alarms.snooze_until == 1700004900U);

  a_eff = tc.tick_alarms_1s(true, 1700004900U, 7, 35, 0, 2, 310000U);
  LATTER_CHECK(a_eff.snooze_fired && rt.alarms.ringing);
  a_eff = tc.tick_alarms_1s(true, 1700005200U, 7, 40, 0, 2, 610000U);
  LATTER_CHECK(a_eff.auto_silenced && !rt.alarms.ringing);

  remote_vol_pct() = 12;
  latter_site_id() = "mutated_site";
  home_dock_page() = 3;
  reset_runtime_state();
  LATTER_CHECK(remote_vol_pct() == 70);
  LATTER_CHECK(latter_site_id() == "primary");
  LATTER_CHECK(home_dock_page() == 0);
}

void test_phase4_security_dnd_hit_targets_and_iso_tz() {
  using namespace latter_ui;
  using namespace latter_core;

  static_assert(static_cast<uint8_t>(AutoBrightnessMode::AmbientLight) == 3U);
  static_assert(ShortcutsState::kMaxDockItems == 24U);
  {
    PersistedDevicePrefs p{};
    p.auto_brightness_mode = 3U;
    LATTER_CHECK(p.auto_brightness_enum() == AutoBrightnessMode::AmbientLight);

    reset_all_v3_rev_guards();
    shortcuts_state() = ShortcutsState{};
    std::string dock24;
    for (int i = 0; i < 24; ++i) {
      if (i > 0) dock24.push_back(kProtocolRecordSep);
      dock24 += "entity|light.item_" + std::to_string(i) + "|toggle|Light " + std::to_string(i) + "|mdi:lightbulb|0|0";
    }
    std::vector<std::string> sc_tok(static_cast<size_t>(ShortcutsField::COUNT), "");
    sc_tok[static_cast<size_t>(ShortcutsField::CHUNK_INDEX)] = "0";
    sc_tok[static_cast<size_t>(ShortcutsField::CHUNK_COUNT)] = "1";
    sc_tok[static_cast<size_t>(ShortcutsField::DOCK_ITEMS)] = dock24;
    sc_tok[static_cast<size_t>(ShortcutsField::PINNED_TOTAL)] = "0";
    LATTER_CHECK(parse_v3_shortcuts_payload(build_v3_packet(24, 1, sc_tok)));
    LATTER_CHECK(shortcuts_state().dock_items.size() == 24U);
  }

  {
    PersistedDevicePrefs src{};
    src.day_brightness = 77U;
    src.do_not_disturb = true;
    src.glass_style = 2U;
    const size_t sub_floor_len = offsetof(PersistedDevicePrefs, do_not_disturb);
    PersistedDevicePrefs dst{};
    dst.do_not_disturb = false;
    LATTER_CHECK(!load_persisted_prefs_from_buffer(&src, sub_floor_len, dst));

    LATTER_CHECK(load_persisted_prefs_from_buffer(&src, kPersistedPrefsMinSize, dst));
    LATTER_CHECK(dst.day_brightness == 77U);
    LATTER_CHECK(dst.do_not_disturb);
    LATTER_CHECK(dst.glass_style == 2U);

    dst = PersistedDevicePrefs{};
    LATTER_CHECK(load_persisted_prefs_from_buffer(&src, sizeof(PersistedDevicePrefs), dst));
    LATTER_CHECK(dst.do_not_disturb);

    PersistedDevicePrefs bad = src;
    bad.version = 17U;
    PersistedDevicePrefs clean{};
    LATTER_CHECK(!load_persisted_prefs_from_buffer(&bad, sizeof(bad), clean));
  }

  {
    latter_rpc::clear_pin_session_key();
    LATTER_CHECK(!latter_rpc::has_pin_session_key());
    LATTER_CHECK(latter_rpc::wrap_pin_code("lock.front_door", "unlock", "123456").empty());

    LATTER_CHECK(latter_rpc::set_pin_session_key(
        "11223344", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    LATTER_CHECK(latter_rpc::has_pin_session_key());
    const std::string w1 = latter_rpc::wrap_pin_code("lock.front_door", "unlock", "987654321012");
    const std::string w2 = latter_rpc::wrap_pin_code("lock.front_door", "unlock", "987654321012");
    LATTER_CHECK(w1.rfind("v1:11223344:", 0) == 0U);
    LATTER_CHECK(w2.rfind("v1:11223344:", 0) == 0U);
    LATTER_CHECK(w1 != w2);
    LATTER_CHECK(w1.find("987654321012") == std::string::npos);

    std::string secret = "123456789012";
    const char *raw_ptr = secret.data();
    const size_t raw_len = secret.size();
    latter_rpc::secure_zero_string(secret);
    for (size_t i = 0; i < raw_len; ++i) {
      LATTER_CHECK(raw_ptr[i] == '\0');
    }
    latter_rpc::clear_pin_session_key();
    LATTER_CHECK(!latter_rpc::has_pin_session_key());
  }

  {
    LATTER_CHECK(should_suppress_alert_in_dnd(true, AlertPriority::Normal, false));
    LATTER_CHECK(!should_suppress_alert_in_dnd(true, AlertPriority::Critical, false));
    LATTER_CHECK(!should_suppress_alert_in_dnd(true, AlertPriority::Normal, true));
    LATTER_CHECK(should_suppress_alert_chime_in_dnd(true, static_cast<int>(AlertPriority::Normal), true));
    LATTER_CHECK(!should_suppress_alert_chime_in_dnd(true, static_cast<int>(AlertPriority::Critical), false));
    LATTER_CHECK(!should_suppress_alert_in_dnd(false, AlertPriority::Normal, false));

    PersistedDevicePrefs vp{};
    vp.alert_volume = 20U;
    LATTER_CHECK(std::fabs(alarm_output_volume(static_cast<float>(vp.alert_volume), 0.0f) - 0.28f) < 0.001f);
    vp.alert_volume = 65U;
    LATTER_CHECK(std::fabs(alarm_output_volume(static_cast<float>(vp.alert_volume), 0.0f) - 0.685f) < 0.001f);
    vp.alert_volume = 100U;
    LATTER_CHECK(std::fabs(alarm_output_volume(static_cast<float>(vp.alert_volume), 0.0f) - 1.00f) < 0.001f);
    LATTER_CHECK(std::fabs(alarm_output_volume(20.0f, 0.80f) - media_dac_volume(0.80f)) < 0.001f);
  }

  {
    for (const auto &g : kAllNinePanels) {
      LATTER_CHECK(is_inside_bezel_for(g.w / 2, g.h / 2, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
      if (g.round || g.corner_r >= 16) {
        LATTER_CHECK(!is_inside_bezel_for(6, 6, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
        LATTER_CHECK(!is_inside_bezel_for(g.w - 7, g.h - 7, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
      } else {
        LATTER_CHECK(!is_inside_bezel_for(4, 4, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
        LATTER_CHECK(!is_inside_bezel_for(g.w - 5, g.h - 5, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
        LATTER_CHECK(is_inside_bezel_for(6, 6, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
        LATTER_CHECK(is_inside_bezel_for(g.w - 7, g.h - 7, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
      }
      if (g.corner_r >= 76) {
        LATTER_CHECK(!is_inside_bezel_for(18, 18, g.w, g.h, g.round, g.wearable, 5, g.corner_r));
      }

      const int min_hit = std::max(dp_px_for(36, compute_ui_scale_milli(g.ppi, g.view_scale_pct, g.stationary)),
                                   mm_to_px_for(7.0f, g.ppi));
      const int div_off = compute_area_card_div_offset(g.w, g.h, g.ppi);
      LATTER_CHECK(div_off >= min_hit);

      const auto ly = compute_panel_layout(g.w, g.h, g.round, g.wearable, g.ppi, g.corner_r, g.view_scale_pct,
                                           g.stationary);
      const int card_max_pad = std::max(0, (ly.area_card3_stride - ly.area_card3_h - 1) / 2);
      const int card_pad = std::min(card_max_pad, std::max(0, (min_hit - ly.area_card3_h + 1) / 2));
      const int expected_card_floor = (g.h >= 460) ? min_hit : min_row_hit_px_for(g.ppi);
      LATTER_CHECK(ly.area_card3_h + 2 * card_pad >= expected_card_floor);
    }

    FrameHitTable ht{};
    for (uint8_t i = 0; i < FrameHitTable::kMaxRegions; ++i) {
      ht.add_rect(i * 4, 10, 20, 20, static_cast<uint16_t>(100U + i), kHitTap, 0);
    }
    LATTER_CHECK(ht.count == FrameHitTable::kMaxRegions && ht.dropped_count == 0U);
    ht.add_rect(50, 80, 120, 44, 105U, kHitTap | kHitFocusSlot, 0, 8);
    const HitRegion *updated = ht.find_by_id_param(105U, 0);
    LATTER_CHECK(updated != nullptr && updated->x0 == 50 && updated->y0 == 80 && updated->hit_pad == 8);
    LATTER_CHECK(ht.dropped_count == 0U);
    ht.add_rect(10, 10, 20, 20, 999U, kHitTap, 0);
    LATTER_CHECK(ht.count == FrameHitTable::kMaxRegions && ht.dropped_count == 1U);
  }

  {
    LATTER_CHECK(std::fabs(weather_temp_to_fahrenheit(22.0f, "C") - 71.6f) < 0.05f);
    LATTER_CHECK(std::fabs(weather_temp_to_fahrenheit(22.0f, "°C") - 71.6f) < 0.05f);
    LATTER_CHECK(std::fabs(weather_temp_to_fahrenheit(72.0f, "F") - 72.0f) < 0.05f);

    char out[24]{};
    LATTER_CHECK(format_iso_time_with_offset("2026-07-15T13:45:00Z", false, -420, out, sizeof(out)));
    LATTER_CHECK(std::string_view(out) == "6:45 AM");
    LATTER_CHECK(format_iso_time_with_offset("2026-07-15T13:45:00.123456+00:00", true, -420, out, sizeof(out)));
    LATTER_CHECK(std::string_view(out) == "06:45");
    LATTER_CHECK(format_iso_time_with_offset("2026-11-21T16:30:00+02:00", true, -480, out, sizeof(out)));
    LATTER_CHECK(std::string_view(out) == "06:30");
    LATTER_CHECK(format_iso_time_with_offset("2026-11-21T06:52:00", false, -480, out, sizeof(out)));
    LATTER_CHECK(std::string_view(out) == "6:52 AM");
    LATTER_CHECK(!format_iso_time_with_offset("unavailable", true, 0, out, sizeof(out)));
  }

  {
    const std::string cal_cpp = read_repo_file("../../components/latter_ui/page_calendar.cpp");
    const std::string wth_cpp = read_repo_files({
        "../../components/latter_ui/page_weather.cpp",
        "../../components/latter_ui/weather_sky_fx.cpp",
    });
    const std::string tch_cpp = read_repo_glob("../../components/latter_ui", "touch_", ".cpp");
    const std::string gui_yml = read_repo_file("../../guis/universal/gui.yaml");
    LATTER_CHECK(cal_cpp.find("render_map_drilldown") == std::string::npos);
    LATTER_CHECK(cal_cpp.find("CAL_VIEW_MAP_BTN") == std::string::npos);
    LATTER_CHECK(tch_cpp.find("CAL_VIEW_MAP_BTN") == std::string::npos);
    LATTER_CHECK(tch_cpp.find("FETCH_CALENDAR_MAP_SNAPSHOT") == std::string::npos);
    LATTER_CHECK(gui_yml.find("is_calendar_map_view_active") == std::string::npos);
    LATTER_CHECK(wth_cpp.find("sscanf") == std::string::npos);
  }
}

void test_sram_psram_budget_and_http_stall_guards() {
  using namespace latter_ui;
  using namespace latter_core;

#if LATTER_CHIP_P4
  static_assert(LATTER_ART_CACHE_ENTRIES == 3, "32 MB PSRAM P4 builds allow 3 ArtCanvasCache slots");
#else
  static_assert(LATTER_ART_CACHE_ENTRIES == 1, "8 MB PSRAM S3 builds must cap ArtCanvasCache to 1 slot");
#endif
  static_assert(!std::is_default_constructible_v<CalendarWindowStore>,
                "CalendarWindowStore (~51 KB) must never be default-constructible on the stack");
  static_assert(!std::is_copy_constructible_v<CalendarWindowStore> &&
                    !std::is_copy_assignable_v<CalendarWindowStore> &&
                    !std::is_move_constructible_v<CalendarWindowStore> &&
                    !std::is_move_assignable_v<CalendarWindowStore>,
                "CalendarWindowStore (~51 KB) must never be copied or moved by value");

  LATTER_CHECK(can_start_online_art_fetch(false));
  LATTER_CHECK(can_start_online_art_fetch(true));
  cam_error_backoff_until_ms() = 4200U;
  LATTER_CHECK(runtime_state().intercom.cam_error_backoff_until_ms == 4200U);
  cam_error_backoff_until_ms() = 0U;

  const std::string st_src = read_repo_glob("../../include/state", "", ".h");
  const std::string cal_src = read_repo_file("../../components/latter_ui/page_calendar.cpp");
  const std::string th_src = read_repo_file("../../components/latter_core/touch_hal.cpp");
  const std::string s3_yaml = read_repo_file("../../modules/hal/chip_esp32s3.yaml");
  const std::string gfx_src = read_repo_files({
      "../../components/latter_ui/ui_gfx.cpp",
      "../../components/latter_ui/ui_gfx_text_art.cpp",
      "../../include/ui_gfx.h",
  });
  const std::string wth_src = read_repo_files({
      "../../components/latter_ui/page_weather.cpp",
      "../../components/latter_ui/weather_sky_fx.cpp",
  });
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
  LATTER_CHECK(th_src.find("IRAM_ATTR") != std::string::npos);
  LATTER_CHECK(mdi_src.find("DecodedMdiEntry g_mdi_cache[") == std::string::npos);
  LATTER_CHECK(mdi_src.find("uint8_t g_bucket_scratch[") == std::string::npos);
  LATTER_CHECK(mdi_src.find("MdiRuntimeScratch *mdi_scratch()") != std::string::npos);
  LATTER_CHECK(gfx_src.find("static uint16_t row_buf[") == std::string::npos);
  LATTER_CHECK(gfx_src.find("static uint16_t sx_lut[") == std::string::npos);
  LATTER_CHECK(gfx_src.find("static uint16_t scaled_row[") == std::string::npos);
  LATTER_CHECK(gfx_src.find("sky_base_cache_drop()") == std::string::npos);
  LATTER_CHECK(gfx_src.find("evict_registered_psram_caches()") != std::string::npos);
  LATTER_CHECK(gfx_src.find("blit_dimmed_rgb565_scanline(") != std::string::npos);
  LATTER_CHECK(gfx_src.find("full_rect_overwrite") != std::string::npos);
  LATTER_CHECK(gfx_src.find("sample_glass_mean(") != std::string::npos);
  const std::string wid_src = read_repo_files({
      "../../components/latter_ui/ui_widgets.cpp",
      "../../components/latter_ui/ui_widgets_icons_chrome.cpp",
  });
  const std::string pm_yaml = read_repo_files({
      "../../modules/power_policy.yaml",
      "../../modules/sleep_lifecycle.yaml",
  });
  const std::string sys_core_yaml = read_repo_file("../../modules/system_core.yaml");
  LATTER_CHECK(wid_src.find("sample_surface ? sample_glass_mean(") != std::string::npos);
  LATTER_CHECK(pm_yaml.find("defined(CONFIG_PM_ENABLE) && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE)") != std::string::npos);
  LATTER_CHECK(sys_core_yaml.find("update_interval: 300s") == std::string::npos);
  LATTER_CHECK(wth_src.find("art_canvas_drop_all()") != std::string::npos);
  LATTER_CHECK(dp_yaml.find("timeout: 15s") == std::string::npos);
  LATTER_CHECK(dp_yaml.find("timeout: 4s") != std::string::npos);
}

void test_wave2_v3_wire_escaping_and_base_fold() {
  using namespace latter_core;

  // 1. v3_escape_subfield / v3_unescape_subfield exact round-trip for \, |, ;
  {
    const std::string raw = "A\\B|C;D\\\\|;";
    const std::string esc = v3_escape_subfield(raw);
    LATTER_CHECK(esc == "A\\\\B\\pC\\sD\\\\\\\\\\p\\s");
    LATTER_CHECK(v3_unescape_subfield(esc) == raw);
    LATTER_CHECK(v3_escape_subfield("").empty());
    LATTER_CHECK(v3_unescape_subfield("").empty());
  }

  // 2. parse_calendar_records(): unescapes time, summary/title, and location subfields
  {
    const std::string wire =
        std::string("TODAY 09:00\\s PDT|Q1 \\p Review \\s Ops|Suite \\\\A\\p1|0") +
        kProtocolRecordSep +
        "TOMORROW|1:1 \\s Sync|HQ\\\\2\\sWest|1";
    CalendarRecord recs[4]{};
    const size_t n = parse_calendar_records(wire, recs, 4);
    LATTER_CHECK(n == 2U);
    LATTER_CHECK(recs[0].time == "TODAY 09:00; PDT");
    LATTER_CHECK(recs[0].title == "Q1 | Review ; Ops");
    LATTER_CHECK(recs[0].location == "Suite \\A|1");
    LATTER_CHECK(!recs[0].all_day);
    LATTER_CHECK(recs[1].time == "TOMORROW");
    LATTER_CHECK(recs[1].title == "1:1 ; Sync");
    LATTER_CHECK(recs[1].location == "HQ\\2;West");
    LATTER_CHECK(recs[1].all_day);
  }

  // 3. parse_broadcast_phrases(): unescapes BEFORE truncating to 64 bytes (escaped input > 64 B!)
  {
    const std::string long_escaped = std::string(60U, 'a') + "\\s\\s\\s\\s";  // 68 wire bytes -> 64 raw bytes
    LATTER_CHECK(long_escaped.size() == 68U);
    const std::string wire =
        std::string("Dinner\\s kids\\p now \\\\ home") +
        kProtocolRecordSep + long_escaped +
        kProtocolRecordSep + std::string(70U, 'b');
    std::vector<std::string> phrases;
    const int n = parse_broadcast_phrases(wire, phrases);
    LATTER_CHECK(n == 3);
    LATTER_CHECK(phrases[0] == "Dinner; kids| now \\ home");
    LATTER_CHECK(phrases[1].size() == 64U);
    LATTER_CHECK(phrases[1] == std::string(60U, 'a') + ";;;;");
    LATTER_CHECK(phrases[2].size() == 64U);
    LATTER_CHECK(phrases[2] == std::string(64U, 'b'));
  }

  // 4. parse_calendar_locations_catalog(): unescapes \, |, ; in every entry
  {
    const std::string wire =
        std::string("Office \\s East") + kProtocolRecordSep +
        "Lab \\p B" + kProtocolRecordSep +
        "Home \\\\ HQ";
    std::string locs[4];
    const size_t n = parse_calendar_locations_catalog(wire, locs, 4);
    LATTER_CHECK(n == 3U);
    LATTER_CHECK(locs[0] == "Office ; East");
    LATTER_CHECK(locs[1] == "Lab | B");
    LATTER_CHECK(locs[2] == "Home \\ HQ");
  }

  // 5. build_music_track_key() & emit_music_lyrics_fetch(): escape title/artist symmetrically with HA lyrics.py
  {
    const std::string title = "Run | Hide; Go\\Now";
    const std::string artist = "A|B;C\\D";
    const std::string key = build_music_track_key(title, artist);
    LATTER_CHECK(key == "Run \\p Hide\\s Go\\\\Now|A\\pB\\sC\\\\D");
    std::string_view parts[3];
    LATTER_CHECK(v3_split_fields(key, parts, 3, '|') == 2U);
    LATTER_CHECK(v3_unescape_subfield(parts[0]) == title);
    LATTER_CHECK(v3_unescape_subfield(parts[1]) == artist);

    media_state() = MediaState{};
    media_state().title = title;
    media_state().artist = artist;
    MockRpcOutbox rpc_box{};
    latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
    const int req_id = emit_music_lyrics_fetch("", "media_player.desk", 1500U, &rpc_box);
    LATTER_CHECK(req_id > 0 && !rpc_box.published.empty());
    std::string joined;
    for (const auto &f : rpc_box.published) joined += f;
    LATTER_CHECK(joined.find("\"mode\":\"lyrics_fetch\"") != std::string::npos);
    // JSON-escaped form of "Run \p Hide\s Go\\Now|A\pB\sC\\D"
    LATTER_CHECK(joined.find("Run \\\\p Hide\\\\s Go\\\\\\\\Now|A\\\\pB\\\\sC\\\\\\\\D") != std::string::npos);
  }

  // 6. collect_active_world_clocks(): unescapes cols[1] before matching custom_lbl
  {
    PersistedDevicePrefs p{};
    p.show_utc_clock = false;
    std::strncpy(p.world_clocks_spec,
                 "HQ\\West|America/Los_Angeles;Other|America/Los_Angeles",
                 sizeof(p.world_clocks_spec) - 1U);
    AreaContextState ac{};
    ac.world_clocks_summary =
        "America/Los_Angeles|Other|PST|-480|0;America/Los_Angeles|HQ\\\\West|PDT|-420|1";
    WorldClockEntry entries[kMaxWorldClocks + 1]{};
    const int n = collect_active_world_clocks(p, ac, entries);
    LATTER_CHECK(n == 2);
    LATTER_CHECK(entries[0].city == "HQ\\West");
    LATTER_CHECK(entries[0].abbr == "PDT");
    LATTER_CHECK(entries[0].utc_offset_min == -420);
    LATTER_CHECK(entries[0].is_dst);
    LATTER_CHECK(entries[1].city == "Other");
    LATTER_CHECK(entries[1].abbr == "PST");
    LATTER_CHECK(entries[1].utc_offset_min == -480);
    LATTER_CHECK(!entries[1].is_dst);
  }

  // 7. parse_cameras_records(): unescapes f[4] (unlock_entity) alongside entity_id/name/area_id
  {
    const std::string wire =
        std::string("camera.front\\sdoor|Front \\p Entry|area\\\\foyer|0x3F|lock.front\\sdoor\\p1\\\\a") +
        kProtocolRecordSep +
        "camera.back|Back Yard|backyard|0x05|lock.back_gate";
    CameraRecord cams[4]{};
    const int n = parse_cameras_records(wire, cams, 4);
    LATTER_CHECK(n == 2);
    LATTER_CHECK(cams[0].entity_id == "camera.front;door");
    LATTER_CHECK(cams[0].name == "Front | Entry");
    LATTER_CHECK(cams[0].area_id == "area\\foyer");
    LATTER_CHECK(cams[0].caps == 0x3FU);
    LATTER_CHECK(cams[0].unlock_entity == "lock.front;door|1\\a");
    LATTER_CHECK(cams[0].has_unlock() && cams[0].unlock_is_lock());
    LATTER_CHECK(cams[1].unlock_entity == "lock.back_gate");
  }

  // 8. modules/base.yaml fold & removed hook pointers / dead code source-level invariants
  {
    const std::string sys_core = read_repo_file("../../modules/system_core.yaml");
    const std::string sys_sync = read_repo_file("../../modules/system_ui_sync.yaml");
    const std::string pmu_yaml = read_repo_file("../../modules/hal/pmu_axp2101.yaml");
    LATTER_CHECK(sys_core.find("LATTER_YAML_REV = 2026112501L") != std::string::npos);
    LATTER_CHECK(sys_core.find("base.yaml") == std::string::npos);
    LATTER_CHECK(sys_core.find("send_device_hello") != std::string::npos);
    LATTER_CHECK(sys_core.find("latter_rpc_outbox") != std::string::npos);
    LATTER_CHECK(sys_core.find("on_ha_client_connected") == std::string::npos);
    LATTER_CHECK(sys_core.find("on_v3_domain_pushed") == std::string::npos);
    LATTER_CHECK(sys_core.find("on_device_command") == std::string::npos);
    LATTER_CHECK(sys_core.find("on_hello_sent") == std::string::npos);
    LATTER_CHECK(sys_sync.find("on_v3_domain_pushed") == std::string::npos);
    LATTER_CHECK(sys_sync.find("on_device_command") == std::string::npos);
    LATTER_CHECK(sys_sync.find("STOP_HA_MUSIC") == std::string::npos);
    LATTER_CHECK(sys_sync.find("media_stop_playback") == std::string::npos);
    LATTER_CHECK(sys_sync.find("apply_device_ui_command_effects") != std::string::npos);
    LATTER_CHECK(pmu_yaml.find("hw_wait_pwr_key_release") == std::string::npos);
    LATTER_CHECK(pmu_yaml.find("hal_wait_pwr_key_release") == std::string::npos);
  }

  // 9. Compiled YAML-offload helpers & out-of-line RPC default sink parity
  {
    reset_runtime_state();
    reset_all_v3_rev_guards();
    auto &prefs = persisted_prefs();
    prefs = PersistedDevicePrefs{};

    // dispatch_v3_domain_push handles all 7 domains
    std::vector<std::string> w_tok(static_cast<size_t>(WeatherField::COUNT), "");
    w_tok[static_cast<size_t>(WeatherField::CONDITION)] = "rainy";
    w_tok[static_cast<size_t>(WeatherField::TEMPERATURE)] = "61.5";
    LATTER_CHECK(dispatch_v3_domain_push("weather_forecast", build_v3_packet(700, 1, w_tok), 5000U));
    LATTER_CHECK(weather_state().condition == "rainy");
    LATTER_CHECK(!dispatch_v3_domain_push("unknown_domain", "v3\x1f" "1\x1f" "1", 5000U));

    // try_handle_pin_session_key_command
    latter_rpc::clear_pin_session_key();
    LATTER_CHECK(!try_handle_pin_session_key_command("restart"));
    LATTER_CHECK(try_handle_pin_session_key_command(
        "set_pin_key:a1b2c3d4:000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
    LATTER_CHECK(latter_rpc::has_pin_session_key());
    latter_rpc::clear_pin_session_key();

    // apply_remote_screen_brightness_cmd (both auto-brightness override and manual brightness)
    bool need_wake = false;
    LATTER_CHECK(apply_remote_screen_brightness_cmd("set_screen_bri:64", 6000U, false, need_wake));
    LATTER_CHECK(need_wake && runtime_state().nav.ambient_bri_override_pct == 64);
    prefs.auto_brightness_mode = static_cast<uint8_t>(AutoBrightnessMode::Disabled);
    LATTER_CHECK(apply_remote_screen_brightness_cmd("set_screen_bri:72", 6100U, false, need_wake));
    LATTER_CHECK(need_wake && persisted_prefs().manual_brightness == 72 &&
                 runtime_state().nav.ambient_bri_override_pct == -1);
    LATTER_CHECK(!apply_remote_screen_brightness_cmd("set_screen_bri:abc", 6200U, true, need_wake));

    // Out-of-line latter_actions default nullptr sink after explicit bind_sensor_sink
    MockRpcOutbox default_box{};
    latter_rpc::rpc_state() = latter_rpc::RpcQueueState{};
    latter_rpc::bind_sensor_sink(&default_box);
    emit_media_command("play");
    LATTER_CHECK(!default_box.published.empty());
    LATTER_CHECK(default_box.published[0].find("\"s\":\"media_command\"") != std::string::npos);
  }
}

}  // namespace

void run_protocol_rpc_tests() {
  test_protocol_v3_and_rpc();
  test_v3_domain_reducer_and_timer_alarm();
  test_phase4_security_dnd_hit_targets_and_iso_tz();
  test_sram_psram_budget_and_http_stall_guards();
  test_wave2_v3_wire_escaping_and_base_fold();
}

}  // namespace latter_test
