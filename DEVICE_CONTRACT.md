# Latter Protocol v3 Device Contract

> **Auto-Generated Reference**: Generated from [`protocol/latter_v3.json`](protocol/latter_v3.json) by [`tools/gen_protocol.py`](tools/gen_protocol.py). Do not edit by hand.

## 1. Overview & Transport Framing

- **Contract Version**: `3`
- **Field Separator**: `0x1F` (`\x1f`, ASCII Unit Separator `␟`)
- **Record Separator**: `0x1E` (`\x1e`, ASCII Record Separator `␞`) with pipe (`|`) subfields inside record lists
- **Absent Values**: A field with no value is the empty string, never a placeholder (none, NONE, None, unknown, unavailable, Idle, Unassigned) or a made-up number. A state field carries only the values its description lists.
- **Inbound Channel (`HA -> Device`)**: Home Assistant pushes each domain's `payload` (`v3␟<epoch>␟<rev>␟...`) directly via the bound ESPHome Native API action `esphome.<node>_latter_push_v3` (`domain`, `payload`) resolved by `esphome_device_id`.
- **Outbound Channel (`Device -> HA`)**: Devices emit bound RPC frames (`<boot_hex>:<seq>:<part>/<total>|<json>`) on their diagnostic `text_sensor` (`System: RPC Outbox`), eliminating `allow_service_calls` and `target_device` spoofing.
- **Revision Guard & Version Tolerance**:
  - `epoch` (`u32`, non-zero): Generated randomly when the Home Assistant coordinator starts. If `in_epoch != cur_epoch`, the device accepts the frame unconditionally and updates `cur_epoch` and `cur_rev`.
  - `rev` (`u32`, `>= 1`): Monotonic per-domain revision counter bumped only when that domain's body fields change. Frames with `rev == 0` are rejected; within the same `epoch`, frames with `in_rev <= cur_rev` are ignored.
  - Parsers accept any frame with `n >= kProtocolMinRequiredFields` (`3`), defaulting missing trailing fields to `""` and logging a warning on field-count mismatch.
  - On ESPHome API reconnect (`on_client_connected` from Home Assistant), the device calls `latter_ui::reset_all_v3_rev_guards()` and sends `device_hello`.

### Header Fields (All Domains)

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `0` | `version` | `HEADER_VERSION` | `string` | Protocol version literal ('v3') |
| `1` | `epoch` | `HEADER_EPOCH` | `u32` | Coordinator session epoch (non-zero random u31; resets rev guards on change) |
| `2` | `rev` | `HEADER_REV` | `u32` | Per-domain monotonic revision counter (>= 1; bumped only when domain body changes) |

### Shared Defaults

Used by both the firmware (`latter_protocol.h`) and Home Assistant until a value is set.

| Name | Value |
| :--- | :--- |
| `dock_routes` | `app:pinned,app:music,app:home_hub,app:area,app:remote,app:games,app:tools` |
| `activity_card` | `1` |
| `activity_sources` | `calendar,music_queue,stopwatch,alarms,timers` |
| `area_categories` | `scene,light,cover,fan,switch,climate,media` |
| `home_categories` | `alarm,cameras,intercom,thermostat,entry,vacuum` |

## 2. Domain Payloads

### `sensor.<device>_area_context` (`AreaCtxField`, `46` total fields)

Active Area state, climate/security/cover summaries, camera/intercom directories, and alert banner.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `area_id` | `AreaCtxField::AREA_ID` | `string` | Active Home Assistant Area ID |
| `4` | `area_name` | `AreaCtxField::AREA_NAME` | `string` | Friendly display name of the active Area |
| `5` | `site_id` | `AreaCtxField::SITE_ID` | `string` | Active Site ID |
| `6` | `ble_closest_proxy` | `AreaCtxField::BLE_CLOSEST_PROXY` | `string` | Friendly name of closest Bluetooth proxy |
| `7` | `wallpaper_url` | `AreaCtxField::WALLPAPER_URL` | `url` | Signed HTTP URL for active wallpaper |
| `8` | `available_domains` | `AreaCtxField::AVAILABLE_DOMAINS` | `csv` | Comma-separated enabled Area categories with >= 1 entity in the active Area, in configured order (scene, light, cover, fan, switch, climate, media, all); empty when no categories have entities |
| `9` | `area_temp` | `AreaCtxField::AREA_TEMP` | `string` | Formatted Area temperature |
| `10` | `area_humidity` | `AreaCtxField::AREA_HUMIDITY` | `string` | Formatted Area relative humidity |
| `11` | `lights_on` | `AreaCtxField::LIGHTS_ON` | `u16` | Count of active lights in Area |
| `12` | `covers_open` | `AreaCtxField::COVERS_OPEN` | `u16` | Count of open covers in Area |
| `13` | `alarm_entity` | `AreaCtxField::ALARM_ENTITY` | `entity_id` | Primary alarm control panel entity ID |
| `14` | `alarm_state` | `AreaCtxField::ALARM_STATE` | `string` | Primary alarm state |
| `15` | `thermostat_entity` | `AreaCtxField::THERMOSTAT_ENTITY` | `entity_id` | Primary climate entity ID |
| `16` | `thermostat_state` | `AreaCtxField::THERMOSTAT_STATE` | `string` | Primary climate state/HVAC mode |
| `17` | `thermostat_temp` | `AreaCtxField::THERMOSTAT_TEMP` | `f32` | Target thermostat setpoint temperature |
| `18` | `thermostat_current_temp` | `AreaCtxField::THERMOSTAT_CURRENT_TEMP` | `f32` | Current measured thermostat temperature |
| `19` | `lock_entity` | `AreaCtxField::LOCK_ENTITY` | `entity_id` | Primary lock entity ID |
| `20` | `lock_state` | `AreaCtxField::LOCK_STATE` | `string` | Primary lock state |
| `21` | `garage_entity` | `AreaCtxField::GARAGE_ENTITY` | `entity_id` | Primary garage cover entity ID |
| `22` | `garage_state` | `AreaCtxField::GARAGE_STATE` | `string` | Primary garage cover state |
| `23` | `vacuum_entity` | `AreaCtxField::VACUUM_ENTITY` | `entity_id` | Primary vacuum entity ID |
| `24` | `vacuum_state` | `AreaCtxField::VACUUM_STATE` | `string` | Primary vacuum state |
| `25` | `camera_snapshot_url` | `AreaCtxField::CAMERA_SNAPSHOT_URL` | `url` | Signed HTTP URL for primary camera snapshot |
| `26` | `cameras_summary` | `AreaCtxField::CAMERAS_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|snapshot_url camera records (up to 10, in configured order; area_id empty when the camera has no Area) |
| `27` | `doorbell_talk_state` | `AreaCtxField::DOORBELL_TALK_STATE` | `string` | Doorbell talkback state |
| `28` | `intercom_targets_summary` | `AreaCtxField::INTERCOM_TARGETS_SUMMARY` | `records` | 0x1E-joined target_id|label|kind call_target records (up to 10; kind all = every other Latter Device, listed first; kind device = one Latter Device, labelled by its title); empty when there is no other Device |
| `29` | `alert_id` | `AreaCtxField::ALERT_ID` | `u32` | Monotonic alert/notification identifier |
| `30` | `alert_title` | `AreaCtxField::ALERT_TITLE` | `string` | Alert banner title |
| `31` | `alert_message` | `AreaCtxField::ALERT_MESSAGE` | `string` | Alert banner message body |
| `32` | `alert_priority` | `AreaCtxField::ALERT_PRIORITY` | `string` | Alert priority level (normal|warning|critical) |
| `33` | `alert_duration` | `AreaCtxField::ALERT_DURATION` | `u16` | Alert display duration in seconds |
| `34` | `alert_chime` | `AreaCtxField::ALERT_CHIME` | `bool` | 1 = play the alert chime (embedded WAV: sfx 9 for warning/critical, otherwise sfx 6), else 0 |
| `35` | `alert_camera_url` | `AreaCtxField::ALERT_CAMERA_URL` | `url` | Signed HTTP URL for alert camera snapshot |
| `36` | `activity_card` | `AreaCtxField::ACTIVITY_CARD` | `bool` | 1 = show the Home Activity Card, 0 = hide it (also sent when every source is off); empty until set: the device uses defaults.activity_card |
| `37` | `activity_sources` | `AreaCtxField::ACTIVITY_SOURCES` | `csv` | Activity Card sources that are on, in this order: calendar, music_queue, stopwatch, alarms, timers (unknown keys are ignored); empty until set: the device uses defaults.activity_sources |
| `38` | `security_pin_policy` | `AreaCtxField::SECURITY_PIN_POLICY` | `string` | Perimeter action policy for unlock, disarm, and open_cover (allow|require_pin|block) |
| `39` | `home_categories` | `AreaCtxField::HOME_CATEGORIES` | `csv` | Comma-separated enabled Home Hub categories with >= 1 configured entity, in configured order (alarm, cameras, intercom, thermostat, entry, vacuum); empty when no categories have entities |
| `40` | `area_icon` | `AreaCtxField::AREA_ICON` | `string` | Home Assistant AreaEntry icon string (e.g. mdi:sofa) or inferred Area archetype icon; empty when unassigned |
| `41` | `local_tz_info` | `AreaCtxField::LOCAL_TZ_INFO` | `string` | Pipe-delimited local time zone status ('<iana>|<city>|<abbr>|<utc_offset_min>|<is_dst>') |
| `42` | `world_clocks_summary` | `AreaCtxField::WORLD_CLOCKS_SUMMARY` | `string` | Semicolon-delimited configured World Clocks ('<iana>|<label>|<abbr>|<utc_offset_min>|<is_dst>;...') |
| `43` | `alarm_caps` | `AreaCtxField::ALARM_CAPS` | `u16` | Alarm capability & code bitmask (1=code_disarm_required, 2=code_arm_required, 4=supports_arm_home, 8=supports_arm_away, 16=supports_arm_night, 32=supports_arm_vacation) |
| `44` | `climate_caps` | `AreaCtxField::CLIMATE_CAPS` | `u16` | Thermostat capability & HVAC mode bitmask (1=off, 2=heat, 4=cool, 8=heat_cool_or_auto, 16=fan_only, 32=dry, 64=target_temp, 128=celsius) |
| `45` | `vacuum_caps` | `AreaCtxField::VACUUM_CAPS` | `u16` | Robot vacuum capability bitmask from VacuumEntityFeature (1=start, 2=pause, 4=stop, 8=return_home, 16=clean_spot, 32=locate) |

### `sensor.<device>_active_media` (`MediaField`, `18` total fields)

Active media player state, metadata, timestamped playback progress, artwork URL, and speaker routing.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `state` | `MediaField::STATE` | `string` | Playback state (playing|paused|buffering|idle|off); empty while the speaker is missing or unavailable |
| `4` | `title` | `MediaField::TITLE` | `string` | Track title |
| `5` | `artist` | `MediaField::ARTIST` | `string` | Track artist |
| `6` | `album` | `MediaField::ALBUM` | `string` | Album title |
| `7` | `duration` | `MediaField::DURATION` | `f32` | Track duration in seconds |
| `8` | `position` | `MediaField::POSITION` | `f32` | Track position in seconds at position_updated_at |
| `9` | `position_updated_at` | `MediaField::POSITION_UPDATED_AT` | `u32` | Unix epoch seconds when position was sampled |
| `10` | `volume` | `MediaField::VOLUME` | `u16` | Volume percentage (0..100) |
| `11` | `media_type` | `MediaField::MEDIA_TYPE` | `string` | Media content type |
| `12` | `art_url` | `MediaField::ART_URL` | `url` | Signed HTTP URL for album art proxy |
| `13` | `active_speaker` | `MediaField::ACTIVE_SPEAKER` | `entity_id` | Active media_player entity ID |
| `14` | `speaker_name` | `MediaField::SPEAKER_NAME` | `string` | Friendly name of active speaker |
| `15` | `last_speaker` | `MediaField::LAST_SPEAKER` | `entity_id` | Previously active media_player entity ID |
| `16` | `local_speaker` | `MediaField::LOCAL_SPEAKER` | `entity_id` | Device's own local media_player entity ID |
| `17` | `is_local` | `MediaField::IS_LOCAL` | `bool` | 1 if active_speaker is this device's own speaker, else 0 |

### `sensor.<device>_remote_config` (`RemoteField`, `6` total fields)

Universal TV/AV remote slot directory and active remote selection.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `active_slot` | `RemoteField::ACTIVE_SLOT` | `u16` | Currently selected 1-based remote slot index |
| `4` | `configured_count` | `RemoteField::CONFIGURED_COUNT` | `u16` | Number of configured remotes in active group |
| `5` | `slots_summary` | `RemoteField::SLOTS_SUMMARY` | `records` | 0x1E-joined index|label|entity_id|kind|area_id|state remote records (configured slots only, up to 10; index is the 1-based slot; kind is remote or media_player, the domain of entity_id) |

### `sensor.<device>_weather_forecast` (`WeatherField`, `17` total fields)

Current outdoor weather telemetry and 5-day daily forecast.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `condition` | `WeatherField::CONDITION` | `string` | Current weather condition slug |
| `4` | `temperature` | `WeatherField::TEMPERATURE` | `f32` | Current outdoor temperature |
| `5` | `humidity` | `WeatherField::HUMIDITY` | `f32` | Current outdoor relative humidity percentage |
| `6` | `wind_speed` | `WeatherField::WIND_SPEED` | `f32` | Current wind speed |
| `7` | `wind_bearing` | `WeatherField::WIND_BEARING` | `string` | Wind cardinal direction or bearing |
| `8` | `high` | `WeatherField::HIGH` | `f32` | Today's forecast high temperature |
| `9` | `low` | `WeatherField::LOW` | `f32` | Today's forecast low temperature |
| `10` | `forecast_5d` | `WeatherField::FORECAST_5D` | `records` | 0x1E-joined day|cond|high|low daily forecast records (up to 5; day MON..SUN in local time; cond is the Home Assistant condition; high and low are whole degrees in the weather entity's unit, empty when unknown) |
| `11` | `sun_state` | `WeatherField::SUN_STATE` | `string` | Sun horizon state from Home Assistant (above_horizon|below_horizon) |
| `12` | `sun_elevation` | `WeatherField::SUN_ELEVATION` | `f32` | Solar elevation angle in degrees |
| `13` | `sun_azimuth` | `WeatherField::SUN_AZIMUTH` | `f32` | Solar azimuth angle in degrees (0..360) |
| `14` | `sun_next_rising` | `WeatherField::SUN_NEXT_RISING` | `string` | Next sunrise ISO-8601 UTC timestamp |
| `15` | `sun_next_setting` | `WeatherField::SUN_NEXT_SETTING` | `string` | Next sunset ISO-8601 UTC timestamp |
| `16` | `moon_phase` | `WeatherField::MOON_PHASE` | `string` | Lunar phase slug (new_moon|waxing_crescent|first_quarter|waxing_gibbous|full_moon|waning_gibbous|last_quarter|waning_crescent) |

### `sensor.<device>_calendar_agenda` (`CalendarField`, `5` total fields)

Upcoming calendar agenda events and next-event headline.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `summary` | `CalendarField::SUMMARY` | `records` | 0x1E-joined time|title|location|all_day agenda records (up to 5; time is a local label such as TODAY 14:30, TOM ALL DAY or NOV 20 08:05; all_day 1 or 0) |
| `4` | `count` | `CalendarField::EVENT_COUNT` | `u16` | Number of upcoming agenda events |

### `sensor.<device>_device_prefs` (`DevicePrefsField`, `55` total fields)

Authoritative Device preferences, power/sleep policies, audio/wake-word tunables, and UI personalization synced between Latter and NVS.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `prefs_seq` | `DevicePrefsField::PREFS_SEQ` | `u32` | Monotonic preference sequence counter incremented by Home Assistant on every preference update |
| `4` | `wake_word_mode` | `DevicePrefsField::WAKE_WORD_MODE` | `string` | Wake word policy (Docked Only|Always On|Disabled) |
| `5` | `tap_to_wake` | `DevicePrefsField::TAP_TO_WAKE` | `string` | Touch wake policy (Single Tap|Double Tap|Docked Only|Disabled) |
| `6` | `pickup_to_wake` | `DevicePrefsField::PICKUP_TO_WAKE` | `string` | IMU pickup wake policy (Always On|Docked Only|Disabled) |
| `7` | `battery_screen_timeout` | `DevicePrefsField::BATTERY_SCREEN_TIMEOUT` | `string` | Battery screen timeout (10s|15s|30s|1 min|2 min|Never) |
| `8` | `docked_idle_timeout` | `DevicePrefsField::DOCKED_IDLE_TIMEOUT` | `string` | Docked idle timeout before screensaver/sleep (10s|15s|30s|1 min|2 min|5 min|Never) |
| `9` | `battery_hibernate_timeout` | `DevicePrefsField::BATTERY_HIBERNATE_TIMEOUT` | `string` | Battery deep sleep / hibernate timeout (5 min|10 min|15 min|30 min|Disabled) |
| `10` | `docked_screensaver` | `DevicePrefsField::DOCKED_SCREENSAVER` | `bool` | 1 = launch screensaver instead of sleeping when docked idle timeout expires, else 0 |
| `11` | `screensaver_style` | `DevicePrefsField::SCREENSAVER_STYLE` | `string` | Ambient screensaver style (Ambient Clock|Aurora|Starfield|Digital Rain|Fireworks) |
| `12` | `hold_crown_power_off` | `DevicePrefsField::HOLD_CROWN_POWER_OFF` | `bool` | 1 = long-holding rotary crown enters deep sleep / power off, else 0 |
| `13` | `auto_brightness_mode` | `DevicePrefsField::AUTO_BRIGHTNESS_MODE` | `string` | Automatic brightness mode (Disabled|Sun Synchronized|Time Schedule) |
| `14` | `manual_brightness` | `DevicePrefsField::MANUAL_BRIGHTNESS` | `string` | Manual brightness target when auto brightness is Disabled (60%|70%|80%|90%|100%) |
| `15` | `day_brightness` | `DevicePrefsField::DAY_BRIGHTNESS` | `string` | Daytime brightness target (60%|70%|80%|90%|100%) |
| `16` | `night_brightness` | `DevicePrefsField::NIGHT_BRIGHTNESS` | `string` | Nighttime brightness target (60%|70%|80%|90%|100%) |
| `17` | `day_start_hour` | `DevicePrefsField::DAY_START_HOUR` | `u16` | Time schedule daytime start hour (0..23) |
| `18` | `night_start_hour` | `DevicePrefsField::NIGHT_START_HOUR` | `u16` | Time schedule nighttime start hour (0..23) |
| `19` | `color_profile` | `DevicePrefsField::COLOR_PROFILE` | `string` | Theme accent preset (Steel Blue|Pink|Blue|Green|Custom) |
| `20` | `va_visual_profile` | `DevicePrefsField::VA_VISUAL_PROFILE` | `string` | Voice assistant visual overlay style (Nebula|Solaris|Rings|Light Bar|Dot Wave) |
| `21` | `home_screen_enabled` | `DevicePrefsField::HOME_SCREEN_ENABLED` | `bool` | 1 = enable the Home Screen and App Dock, 0 = single-app mode locked to start_screen |
| `22` | `start_screen` | `DevicePrefsField::START_SCREEN` | `string` | Boot / single-app start screen (Home|Pinned|Time App|Clock Face|Media|Home Hub|Area Controls|Remote|Weather|Cameras|Games|Tools) |
| `23` | `return_to_home_timeout` | `DevicePrefsField::RETURN_TO_HOME_TIMEOUT` | `string` | Idle timeout before resetting navigation back to the Home Screen (15s|30s|1 min|2 min|5 min|10 min|Never) |
| `24` | `clock_face` | `DevicePrefsField::CLOCK_FACE` | `string` | Clock face style (Minimal Digital|Modern Analog|Hybrid Digital) |
| `25` | `time_format` | `DevicePrefsField::TIME_FORMAT` | `string` | Clock time format (12-Hour (AM/PM)|24-Hour) |
| `26` | `weather_background` | `DevicePrefsField::WEATHER_BACKGROUND` | `bool` | 1 = animate ambient weather particles on Home screen, else 0 |
| `27` | `home_sun` | `DevicePrefsField::HOME_SUN` | `bool` | 1 = show daytime solar disc on Home screen, else 0 |
| `28` | `home_moon` | `DevicePrefsField::HOME_MOON` | `bool` | 1 = show nighttime lunar phase disc on Home screen, else 0 |
| `29` | `wake_greeting` | `DevicePrefsField::WAKE_GREETING` | `bool` | 1 = show time-of-day wake greeting toast, else 0 |
| `30` | `album_art_style` | `DevicePrefsField::ALBUM_ART_STYLE` | `string` | Now Playing album art layout (Full Screen|Letterbox|Standard|None) |
| `31` | `music_overlay_style` | `DevicePrefsField::MUSIC_OVERLAY_STYLE` | `string` | Now Playing controls overlay visibility (Art Only|Art + Metadata|Always Visible) |
| `32` | `show_speaker_shortcut` | `DevicePrefsField::SHOW_SPEAKER_SHORTCUT` | `bool` | 1 = show speaker selector shortcut on Now Playing, else 0 |
| `33` | `show_library_shortcut` | `DevicePrefsField::SHOW_LIBRARY_SHORTCUT` | `bool` | 1 = show library browse shortcut on Now Playing, else 0 |
| `34` | `album_sort_order` | `DevicePrefsField::ALBUM_SORT_ORDER` | `string` | Album browser sort order (Alphabetical|Newest on Top|Oldest on Top) |
| `35` | `artist_album_sort_order` | `DevicePrefsField::ARTIST_ALBUM_SORT_ORDER` | `string` | Artist discography sort order (Oldest on Top|Newest on Top|Alphabetical) |
| `36` | `podcast_sort_order` | `DevicePrefsField::PODCAST_SORT_ORDER` | `string` | Podcast episode sort order (Newest on Top|Oldest on Top) |
| `37` | `ww_sens_okay_nabu` | `DevicePrefsField::WW_SENS_OKAY_NABU` | `string` | Okay Nabu wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `38` | `ww_sens_hey_luna` | `DevicePrefsField::WW_SENS_HEY_LUNA` | `string` | Hey Luna wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `39` | `ww_sens_hey_jarvis` | `DevicePrefsField::WW_SENS_HEY_JARVIS` | `string` | Hey Jarvis wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `40` | `ww_sens_hey_mycroft` | `DevicePrefsField::WW_SENS_HEY_MYCROFT` | `string` | Hey Mycroft wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `41` | `ww_sens_alexa` | `DevicePrefsField::WW_SENS_ALEXA` | `string` | Alexa wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `42` | `wake_screen_on_assist` | `DevicePrefsField::WAKE_SCREEN_ON_ASSIST` | `bool` | 1 = wake display automatically when voice assistant triggers, else 0 |
| `43` | `alert_volume` | `DevicePrefsField::ALERT_VOLUME` | `u16` | Timer, alarm, and notification chime volume percentage (20..100) |
| `44` | `volume_step_size` | `DevicePrefsField::VOLUME_STEP_SIZE` | `u16` | Rotary/button volume increment step percentage (1..10) |
| `45` | `game_sound_effects` | `DevicePrefsField::GAME_SOUND_EFFECTS` | `bool` | 1 = play arcade game sound effects, else 0 |
| `46` | `ble_beacon_enabled` | `DevicePrefsField::BLE_BEACON_ENABLED` | `bool` | 1 = enable BLE iBeacon presence advertising (derived from native_ble_tracking), else 0 |
| `47` | `ble_beacon_mode` | `DevicePrefsField::BLE_BEACON_MODE` | `string` | BLE beacon advertising duty policy (Handheld Only|Handheld + Screen Off|Always) |
| `48` | `weather_sim_condition` | `DevicePrefsField::WEATHER_SIM_CONDITION` | `string` | Developer weather animation override (Live (Home Assistant) or simulated condition label) |
| `49` | `theme_color_hex` | `DevicePrefsField::THEME_COLOR_HEX` | `string` | Custom theme accent color (#RRGGBB) synchronized with device_custom_color |
| `50` | `home_clock_tap_action` | `DevicePrefsField::HOME_CLOCK_TAP_ACTION` | `string` | Action when tapping the time readout on the Home Screen (Time App|Clock Face) |
| `51` | `show_utc_clock` | `DevicePrefsField::SHOW_UTC_CLOCK` | `bool` | 1 = show a dedicated UTC World Clock card on the Time App Clock tab, else 0 |
| `52` | `world_clocks` | `DevicePrefsField::WORLD_CLOCKS` | `string` | Semicolon-delimited custom-labeled World Clock entries (up to 4, '<label>|<iana>;...') shown on the Time App Clock tab |
| `53` | `alarms_summary` | `DevicePrefsField::ALARMS_SUMMARY` | `string` | Semicolon-delimited 3-slot device alarm schedule ('HH:MM|enabled|recurrence;...') |
| `54` | `local_clock_label` | `DevicePrefsField::LOCAL_CLOCK_LABEL` | `string` | Optional user-defined label shown above local time on the Time App Clock tab (up to 32 chars) |

### `sensor.<device>_shortcuts` (`ShortcutsField`, `8` total fields)

App Dock and Pinned App shortcut tables (Latter OS routes and Home Assistant entity actions with live state), chunked across <=24-item frames for unlimited pages.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `chunk_index` | `ShortcutsField::CHUNK_INDEX` | `u16` | 0-based chunk index (0 .. chunk_count - 1) |
| `4` | `chunk_count` | `ShortcutsField::CHUNK_COUNT` | `u16` | Total chunks in this shortcuts snapshot (>= 1; commits atomically on chunk_index + 1 == chunk_count) |
| `5` | `dock_items` | `ShortcutsField::DOCK_ITEMS` | `records` | 0x1E-joined kind|target|action|label|icon|confirm|state App Dock shortcut records (sent on chunk_index == 0, up to 16; empty until configured so device uses defaults.dock_routes; Settings is always appended last) |
| `6` | `pinned_total` | `ShortcutsField::PINNED_TOTAL` | `u16` | Total configured Pinned App items across all chunks (0 auto-hides app:pinned on the App Dock) |
| `7` | `pinned_items` | `ShortcutsField::PINNED_ITEMS` | `records` | 0x1E-joined kind|target|action|label|icon|confirm|state Pinned App shortcut records for this chunk (up to 24 per chunk, rendered 6 per page) |

## 3. ESPHome Device Actions (`esphome.<node>_<action>`)

| Action | Variables | Description |
| :--- | :--- | :--- |
| `latter_ping` | *(none)* | No-op discovery marker action announcing Latter Protocol v3 compatibility and hardware manifest |
| `latter_push_v3` | `domain: string`, `payload: string` | Push a revision-guarded Protocol v3 domain payload directly to the bound Device over the ESPHome Native API |
| `latter_set_beacon_uuid` | `uuid: string` | Provision the System's random 128-bit BLE iBeacon UUID into NVS and restart advertising if active |
| `latter_factory_reset` | *(none)* | Reset to defaults: erases every saved setting (the whole NVS partition, including the Wi-Fi driver's cached network data and RF calibration) and reboots. Compiled-in Wi-Fi credentials and the API key are kept, so the device rejoins Home Assistant unchanged |
| `latter_device_command` | `command: string` | Execute a typed Device lifecycle, diagnostic, or UI command (restart|find_my|activate_screensaver|trigger_va|log_memory_report) |
| `latter_set_slots` | `is_area_slots: bool`, `req_id: int`, `mode: string`, `page: int`, `total_pages: int`, `slots: string` | Push paginated media/speaker/search browse slots or Area/Site entity control slots to the device |
| `latter_show_notification` | `title: string`, `message: string`, `priority: string`, `duration: int`, `chime: bool`, `camera_url: string`, `alert_id: int` | Wake display, show an on-screen alert banner (priority normal|warning|critical, duration 3-60 s, default 10), optionally play the embedded WAV chime and show a camera snapshot; alert_id > 0 deduplicates repeats |
| `latter_ui_input` | `action: string`, `delta: int` | Remote navigation input: action up|down|left|right|select|back|home (canonical navigation actions); delta overrides the step for directional actions (0 = derive from action) |

## 4. Firmware RPC Outbox Methods (`latter_rpc_outbox`)

| Method | Canonical Outbox Fields | Description |
| :--- | :--- | :--- |
| `device_hello` | `contract`, `boot_id`, `manifest`, `prefs_seq`, `dirty_prefs` | Device-initiated handshake sent when Home Assistant connects to the ESPHome API, carrying optional NVS preference sync state |
| `music_browse` | `req_id`, `mode`, `page`, `page_size`, `search`, `speaker`, `last_speaker`, `album_sort`, `artist_album_sort`, `podcast_sort` | Browse speakers, favorites, playlists, artists, albums, tracks, radio, or search |
| `set_active_speaker` | `entity_id`, `transfer_queue` | Select the active media_player target for a Latter device |
| `media_command` | `command`, `seek_position`, `entity_id`, `media_id`, `media_type`, `enqueue`, `group_members` | Execute transport, seek, queue, speaker grouping, or play_media command on the active speaker |
| `media_volume` | `entity_id`, `action`, `volume_level` | Adjust or set volume on the active speaker or volume override target |
| `area_browse` | `req_id`, `mode`, `area_id`, `domain_filter`, `page`, `page_size` | Browse Areas, Area domains, or paginated controllable entities |
| `area_action` | `entity_id`, `action`, `level`, `area_id`, `code` | Execute an explicit domain action on an Area or specific entity |
| `set_active_area` | `area_id` | Set the active Area context for a Latter device (an on-device Area pick). Whether media follows is the Device's Latter setting |
| `remote_action` | `slot`, `action`, `command`, `hold_secs` | Send a TV/AV remote command or select active remote slot |
| `calendar_fetch` | `req_id`, `days`, `page`, `page_size` | Refresh upcoming calendar agenda events for a Latter device |
| `calendar_create_event` | `summary`, `in_minutes`, `duration_minutes` | Create a new event on the primary configured calendar |
| `intercom_broadcast` | `message` | Broadcast a voice announcement to chosen speakers, the speakers in occupied Areas, or every speaker |
| `doorbell_intercom` | `action`, `camera_entity`, `camera_index`, `message`, `noise_reduction`, `code` | Doorbell talkback and requests (talk_message, quick_reply, listen_start, listen_stop, call_target, unlock_door, set_noise_reduction) |
| `save_custom_color` | `hex_color` | Save a custom RGB accent color from the on-device color wheel |
| `shortcut_action` | `entity_id`, `action`, `code` | Execute an allowlisted Home Assistant entity action from a configured App Dock or Pinned shortcut |
| `set_device_prefs` | `base_seq`, `changes` | Persist on-device settings edits back to the Device subentry and increment prefs_seq |
| `timer_alarm_state` | `timer_running`, `timer_paused`, `remaining_sec`, `ringing` | Report on-device countdown timer and alarm ringing state transitions to Home Assistant |

## 5. Public Home Assistant Services (`latter.<service>`)

| Service | Canonical Fields | Description |
| :--- | :--- | :--- |
| `latter.device_hello` | `target_device`, `contract`, `boot_id`, `manifest`, `prefs_seq`, `dirty_prefs` | Device-initiated handshake sent by an ESPHome Latter node on API connection to report its contract version, boot ID, and hardware manifest. |
| `latter.music_browse` | `target_device`, `req_id`, `mode`, `page`, `page_size`, `search`, `speaker`, `last_speaker`, `album_sort`, `artist_album_sort`, `podcast_sort` | Browse Music Assistant speakers, library, podcasts, queues, lyrics, and artist/album bios and push paginated slots to the ESPHome device. |
| `latter.set_active_speaker` | `entity_id`, `transfer_queue`, `target_device` | Change the active target media_player for the ESPHome device and optionally transfer the active playback queue. |
| `latter.media_volume` | `target_device`, `entity_id`, `action`, `volume_level` | Set or step the volume, or toggle mute, on the active speaker. If a volume override is mapped for that speaker (for example an AV receiver), the change goes to the override entity. |
| `latter.set_active_area` | `area_id`, `target_device` | Switch the active area context by area_id or area name. The device's speaker follows to the new area when the device's "Media follows this Device" setting is on. On roaming devices with Voice Follows Me enabled, this also moves the device's Home Assistant Area so area-aware Assist commands target it. |
| `latter.remote_action` | `slot`, `action`, `command`, `hold_secs`, `target_device` | Select one of the up to 10 configured TV remote slots or send a remote command. |
| `latter.area_browse` | `target_device`, `req_id`, `mode`, `area_id`, `domain_filter`, `page`, `page_size` | Browse Home Assistant areas, curated area entities, or home-wide alarm entities and push paginated slots to the ESPHome device. |
| `latter.area_action` | `target_device`, `entity_id`, `area_id`, `action`, `level`, `code` | Execute a normalized area control, light color/Kelvin, thermostat, lock, vacuum, or home security alarm action. |
| `latter.calendar_fetch` | `target_device`, `req_id`, `days`, `page`, `page_size` | Refresh upcoming system calendar events and push a 5-slot agenda page to the ESPHome device. |
| `latter.calendar_create_event` | `target_device`, `calendar_entity`, `summary`, `start_date_time`, `end_date_time`, `in_minutes`, `duration_minutes`, `description` | Create a new event or quick reminder on the primary system calendar and refresh the agenda sensor. |
| `latter.intercom_broadcast` | `target_device`, `message`, `media_url`, `target_speakers`, `tts_entity` | Broadcast a TTS announcement or recorded audio clip to chosen speakers, the speakers in occupied Areas ('occupied_areas'), the speakers in a person's current Area ('person.<slug>'), or all online Music Assistant speakers. |
| `latter.media_command` | `target_device`, `command`, `entity_id`, `seek_position`, `media_id`, `media_type`, `enqueue`, `group_members` | Run a playback command (play, pause, toggle, next, previous, stop, seek, play_media, clear_queue) on the active speaker. If the device restarted or slept and its Sendspin buffer is empty, play re-streams the Music Assistant queue. |
| `latter.notify` | `target_device`, `title`, `message`, `priority`, `duration`, `chime`, `announce`, `camera_entity` | Show an on-screen notification, with an optional chime and an optional camera snapshot, on a specific device, the devices in occupied Areas ('occupied_areas'), the devices assigned to a person ('person.<slug>'), or all displays ('all'). |
| `latter.save_custom_color` | `target_device`, `hex_color`, `name` | Save a custom hex color from the on-device or Studio Color Wheel into the System-Wide Color Library and apply it to the target device. |
| `latter.shortcut_action` | `target_device`, `entity_id`, `action`, `code` | Execute a configured App Dock or Pinned Home Assistant entity action on behalf of a bound Latter Device, enforcing allowlist membership and Perimeter Security Policy. |
| `latter.doorbell_intercom` | `target_device`, `action`, `camera_entity`, `camera_index`, `message`, `noise_reduction`, `code` | Listen to a doorbell camera's noise-reduced audio on a Latter device, reply by push-to-talk or quick reply (spoken with TTS on the camera's media_player), select a carousel camera, unlock the door's lock, or ring other Latter displays with a notification (call_target). There is no live device-to-door audio. |
| `latter.reset_device` | `target_device`, `device_settings`, `latter_options` | Reset a Latter device. Device settings erases everything saved on the device (its settings and the Wi-Fi driver's cached data) and restarts it; Wi-Fi credentials and the API key are compiled into the firmware, so it rejoins Home Assistant unchanged. Latter options puts this device's Latter options back to the defaults it got when it was added, keeping its ESPHome binding and its Music Assistant player link. |
| `latter.set_device_prefs` | `target_device`, `base_seq`, `changes` | Apply on-device preference updates from a bound Latter Device (or admin automation) into the Device's sleep-safe subentry configuration. |
| `latter.timer_alarm_state` | `target_device`, `timer_running`, `timer_paused`, `remaining_sec`, `ringing` | Event-driven timer and alarm state update reported by a bound Latter Device. |
