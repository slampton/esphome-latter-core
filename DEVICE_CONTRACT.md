# Latter Protocol v3 Device Contract

> **Auto-Generated Reference**: Generated from [`protocol/latter_v3.json`](protocol/latter_v3.json) by [`tools/gen_protocol.py`](tools/gen_protocol.py). Do not edit by hand.

## 1. Overview & Transport Framing

- **Contract Version**: `3`
- **Field Separator**: `0x1F` (`\x1f`, ASCII Unit Separator `␟`)
- **Record Separator**: `0x1E` (`\x1e`, ASCII Record Separator `␞`) with pipe (`|`) subfields inside record lists
- **Absent Values**: A field with no value is the empty string, never a placeholder (none, NONE, None, unknown, unavailable, Idle, Unassigned) or a made-up number. A state field carries only the values its description lists.
- **Inbound Channel (`HA -> Device`)**: Home Assistant pushes each domain's `payload` (`v3␟<epoch>␟<rev>␟...`) directly via the bound ESPHome Native API action `esphome.<node>_latter_push_v3` (`domain`, `payload`) resolved by `esphome_device_id`.
- **Outbound Channel (`Device -> HA`)**: Devices emit bound RPC frames (`<boot_hex>:<seq>:<part>/<total>|<json>`) on their hidden transport `text_sensor` (`latter_rpc_outbox`), eliminating `allow_service_calls` and `target_device` spoofing.
- **Revision Guard & Version Tolerance**:
  - `epoch` (`u32`, non-zero): Generated randomly when the Home Assistant coordinator starts. If `in_epoch != cur_epoch`, the device accepts the frame unconditionally and updates `cur_epoch` and `cur_rev`.
  - `rev` (`u32`, `>= 1`): Monotonic per-domain revision counter bumped only when that domain's body fields change. Frames with `rev == 0` are rejected; within the same `epoch`, frames with `in_rev <= cur_rev` are ignored.
  - Parsers accept any frame with `n >= kProtocolMinRequiredFields` (`3`), defaulting missing trailing fields to `""` and logging a warning on field-count mismatch.
  - On ESPHome API reconnect (`on_client_connected` from Home Assistant), the device calls `latter_core::reset_all_v3_rev_guards()` and sends `device_hello`.

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
| `dock_routes` | `app:pinned,app:music,app:home_hub,app:cameras,app:area,app:remote,app:games,app:tools` |
| `activity_card` | `1` |
| `activity_sources` | `calendar,music_queue,stopwatch,alarms,timers` |
| `area_categories` | `scene,light,cover,fan,switch,climate,media` |
| `home_categories` | `alarm,intercom,thermostat,entry,vacuum` |
| `tools_items` | `stopwatch,level,flashlight,calculator,dice,coin_flip` |
| `games_items` | `blocks,snake,drop_four,marbles,archery,bricks,chomp` |

## 2. Domain Payloads

### `area_context` (`AreaCtxField`, `43` total fields)

Active Area state, climate/security/cover summaries, camera/intercom directories, and alert banner.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `area_id` | `AreaCtxField::AREA_ID` | `string` | Active Home Assistant Area ID |
| `4` | `area_name` | `AreaCtxField::AREA_NAME` | `string` | Friendly display name of the active Area |
| `5` | `site_id` | `AreaCtxField::SITE_ID` | `string` | Active Site ID |
| `6` | `ble_closest_proxy` | `AreaCtxField::BLE_CLOSEST_PROXY` | `string` | Friendly name of closest Bluetooth proxy |
| `7` | `wallpaper_url` | `AreaCtxField::WALLPAPER_URL` | `url` | Signed HTTP URL for active wallpaper, or "latter://day_night_sky" to select the native Day & Night Sky wallpaper |
| `8` | `available_domains` | `AreaCtxField::AVAILABLE_DOMAINS` | `csv` | Comma-separated enabled Area categories with >= 1 entity in the active Area, in configured order (scene, light, cover, fan, switch, climate, media, all); empty when no categories have entities |
| `9` | `area_temp` | `AreaCtxField::AREA_TEMP` | `string` | Formatted Area temperature |
| `10` | `area_humidity` | `AreaCtxField::AREA_HUMIDITY` | `string` | Formatted Area relative humidity |
| `11` | `lights_on` | `AreaCtxField::LIGHTS_ON` | `u16` | Count of active lights in Area |
| `12` | `covers_open` | `AreaCtxField::COVERS_OPEN` | `u16` | Count of open covers in Area |
| `13` | `alarm_entity` | `AreaCtxField::ALARM_ENTITY` | `entity_id` | Primary alarm control panel entity ID |
| `14` | `alarm_state` | `AreaCtxField::ALARM_STATE` | `string` | Primary alarm state |
| `15` | `thermostats_summary` | `AreaCtxField::THERMOSTATS_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|state|target_temp|current_temp|caps thermostat zone records (up to 8, in configured order; caps is the u16 climate capability bitmask: 1=off, 2=heat, 4=cool, 8=heat_cool_or_auto, 16=fan_only, 32=dry, 64=target_temp, 128=celsius) |
| `16` | `locks_summary` | `AreaCtxField::LOCKS_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|state door lock records (up to 8, in configured order) |
| `17` | `garages_summary` | `AreaCtxField::GARAGES_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|state garage door cover records (up to 6, in configured order) |
| `18` | `vacuums_summary` | `AreaCtxField::VACUUMS_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|state|caps robot vacuum records (up to 6, in configured order; caps is the u16 VacuumEntityFeature bitmask: 1=start, 2=pause, 4=stop, 8=return_home, 16=clean_spot, 32=locate) |
| `19` | `area_climates_summary` | `AreaCtxField::AREA_CLIMATES_SUMMARY` | `records` | 0x1E-joined entity_id|name|state|target_temp|current_temp|caps climate records for the active Area (up to 6) |
| `20` | `camera_snapshot_url` | `AreaCtxField::CAMERA_SNAPSHOT_URL` | `url` | Signed HTTP URL for primary camera snapshot |
| `21` | `cameras_summary` | `AreaCtxField::CAMERAS_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|caps|unlock_entity camera records (up to 10, in configured order; name uses custom label when configured; area_id and unlock_entity empty when unassigned; caps u16 bitmask: 1=show_listen, 2=show_talk, 4=is_doorbell, 8=in_carousel, 16=show_reply, 32=show_unlock) |
| `22` | `doorbell_talk_state` | `AreaCtxField::DOORBELL_TALK_STATE` | `string` | Doorbell talkback state |
| `23` | `broadcast_phrases` | `AreaCtxField::BROADCAST_PHRASES` | `records` | 0x1E-joined ordered quick broadcast phrases (up to 12 phrases, max 64 chars each) configured in Latter Studio; empty when no quick phrases are configured |
| `24` | `alert_id` | `AreaCtxField::ALERT_ID` | `u32` | Monotonic alert/notification identifier |
| `25` | `alert_title` | `AreaCtxField::ALERT_TITLE` | `string` | Alert banner title |
| `26` | `alert_message` | `AreaCtxField::ALERT_MESSAGE` | `string` | Alert banner message body |
| `27` | `alert_priority` | `AreaCtxField::ALERT_PRIORITY` | `string` | Alert priority level (normal|warning|critical) |
| `28` | `alert_duration` | `AreaCtxField::ALERT_DURATION` | `u16` | Alert display duration in seconds |
| `29` | `alert_chime` | `AreaCtxField::ALERT_CHIME` | `bool` | 1 = play the alert chime (embedded WAV: sfx 9 for warning/critical, otherwise sfx 6), else 0 |
| `30` | `alert_camera_url` | `AreaCtxField::ALERT_CAMERA_URL` | `url` | Signed HTTP URL for alert camera snapshot |
| `31` | `activity_card` | `AreaCtxField::ACTIVITY_CARD` | `bool` | 1 = show the Home Activity Card, 0 = hide it (also sent when every source is off); empty until set: the device uses defaults.activity_card |
| `32` | `activity_sources` | `AreaCtxField::ACTIVITY_SOURCES` | `csv` | Activity Card sources that are on, in this order: calendar, music_queue, stopwatch, alarms, timers (unknown keys are ignored); empty until set: the device uses defaults.activity_sources |
| `33` | `security_pin_policy` | `AreaCtxField::SECURITY_PIN_POLICY` | `string` | Perimeter action policy for unlock, disarm, and open_cover (allow|require_pin|block) |
| `34` | `home_categories` | `AreaCtxField::HOME_CATEGORIES` | `csv` | Comma-separated enabled Home Hub categories with >= 1 configured entity, in configured order (alarm, intercom, thermostat, entry, vacuum); empty when no categories have entities |
| `35` | `area_icon` | `AreaCtxField::AREA_ICON` | `string` | Home Assistant AreaEntry icon string (e.g. mdi:sofa) or inferred Area archetype icon; empty when unassigned |
| `36` | `local_tz_info` | `AreaCtxField::LOCAL_TZ_INFO` | `string` | Pipe-delimited local time zone status ('<iana>|<city>|<abbr>|<utc_offset_min>|<is_dst>') |
| `37` | `world_clocks_summary` | `AreaCtxField::WORLD_CLOCKS_SUMMARY` | `string` | Semicolon-delimited configured World Clocks ('<iana>|<label>|<abbr>|<utc_offset_min>|<is_dst>;...') |
| `38` | `alarm_caps` | `AreaCtxField::ALARM_CAPS` | `u16` | Alarm capability & code bitmask (1=code_disarm_required, 2=code_arm_required, 4=supports_arm_home, 8=supports_arm_away, 16=supports_arm_night, 32=supports_arm_vacation) |
| `39` | `camera_stream_url` | `AreaCtxField::CAMERA_STREAM_URL` | `url` | Signed device-scoped base URL for live MJPEG single and 2x2 grid camera streaming |
| `40` | `alert_camera_entity` | `AreaCtxField::ALERT_CAMERA_ENTITY` | `entity_id` | Camera entity ID associated with the active alert or doorbell notification |
| `41` | `screensaver_photo_url` | `AreaCtxField::SCREENSAVER_PHOTO_URL` | `url` | Signed HTTP URL for the active Photo Frame screensaver image (un-dimmed, darken=1.0); empty when screensaver_style is not Photo Frame or the photo pool is empty |
| `42` | `area_ambient_light` | `AreaCtxField::AREA_AMBIENT_LIGHT` | `string` | Calibrated active Area ambient light level ('<norm_pct_0_100>|<formatted_lux>', e.g. '57|5.6'); empty when no illuminance sensor is available for the active Area |

### `active_media` (`MediaField`, `24` total fields)

Active media player state, metadata, playback progress, artwork URL, and speaker routing.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `state` | `MediaField::STATE` | `string` | Playback state (playing|paused|buffering|idle|off); empty while the speaker is missing or unavailable |
| `4` | `title` | `MediaField::TITLE` | `string` | Track title |
| `5` | `artist` | `MediaField::ARTIST` | `string` | Track artist |
| `6` | `album` | `MediaField::ALBUM` | `string` | Album title |
| `7` | `duration` | `MediaField::DURATION` | `f32` | Track duration in seconds |
| `8` | `position` | `MediaField::POSITION` | `f32` | Track position in seconds |
| `9` | `volume` | `MediaField::VOLUME` | `u16` | Volume percentage (0..100) |
| `10` | `media_type` | `MediaField::MEDIA_TYPE` | `string` | Media content type |
| `11` | `art_url` | `MediaField::ART_URL` | `url` | Signed HTTP URL for album art proxy |
| `12` | `active_speaker` | `MediaField::ACTIVE_SPEAKER` | `entity_id` | Active media_player entity ID |
| `13` | `speaker_name` | `MediaField::SPEAKER_NAME` | `string` | Friendly name of active speaker |
| `14` | `last_speaker` | `MediaField::LAST_SPEAKER` | `entity_id` | Previously active media_player entity ID |
| `15` | `local_speaker` | `MediaField::LOCAL_SPEAKER` | `entity_id` | Device's own local media_player entity ID |
| `16` | `is_local` | `MediaField::IS_LOCAL` | `bool` | 1 if active_speaker is this device's own speaker, else 0 |
| `17` | `shuffle` | `MediaField::SHUFFLE` | `bool` | 1 if shuffle is enabled on the active speaker/queue, else 0 |
| `18` | `repeat` | `MediaField::REPEAT` | `string` | Active speaker/queue repeat mode (off|all|one); empty when unknown |
| `19` | `muted` | `MediaField::MUTED` | `bool` | 1 if the active speaker or volume target is muted, else 0 |
| `20` | `favorite` | `MediaField::FAVORITE` | `bool` | 1 if the current track is marked favorite in Music Assistant, else 0 |
| `21` | `queue_count` | `MediaField::QUEUE_COUNT` | `u16` | Total items in the active Music Assistant playback queue (0 when empty or non-MA) |
| `22` | `media_caps` | `MediaField::MEDIA_CAPS` | `u16` | Active speaker capability bitmask (1=pause, 2=seek, 4=volume_set, 8=volume_mute, 16=previous_track, 32=next_track, 64=shuffle_set, 128=repeat_set, 256=grouping, 512=music_assistant, 1024=favorite) |
| `23` | `is_default_speaker` | `MediaField::IS_DEFAULT_SPEAKER` | `bool` | 1 if active_speaker is this device's configured default speaker (or its own local speaker when unset), else 0 |

### `remote_config` (`RemoteField`, `6` total fields)

Universal TV/AV remote slot directory and active remote selection.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `active_slot` | `RemoteField::ACTIVE_SLOT` | `u16` | Currently selected 1-based remote slot index |
| `4` | `configured_count` | `RemoteField::CONFIGURED_COUNT` | `u16` | Number of configured remotes in active group |
| `5` | `slots_summary` | `RemoteField::SLOTS_SUMMARY` | `records` | 0x1E-joined index|label|entity_id|kind|area_id|state remote records (configured slots only, up to 10; index is the 1-based slot; kind is remote or media_player, the domain of entity_id) |

### `weather_forecast` (`WeatherField`, `18` total fields)

Current outdoor weather telemetry and 5-day daily forecast.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `condition` | `WeatherField::CONDITION` | `string` | Current weather condition slug |
| `4` | `temperature` | `WeatherField::TEMPERATURE` | `f32` | Current outdoor temperature |
| `5` | `humidity` | `WeatherField::HUMIDITY` | `f32` | Current outdoor relative humidity percentage |
| `6` | `wind_speed` | `WeatherField::WIND_SPEED` | `f32` | Current wind speed |
| `7` | `wind_bearing` | `WeatherField::WIND_BEARING` | `f32` | Wind bearing in degrees (0..360) |
| `8` | `high` | `WeatherField::HIGH` | `f32` | Today's forecast high temperature |
| `9` | `low` | `WeatherField::LOW` | `f32` | Today's forecast low temperature |
| `10` | `forecast_5d` | `WeatherField::FORECAST_5D` | `records` | 0x1E-joined day|cond|high|low daily forecast records (up to 5; day MON..SUN in local time; cond is the Home Assistant condition; high and low are whole degrees in the weather entity's unit, empty when unknown) |
| `11` | `sun_state` | `WeatherField::SUN_STATE` | `string` | Sun horizon state from Home Assistant (above_horizon|below_horizon) |
| `12` | `sun_elevation` | `WeatherField::SUN_ELEVATION` | `f32` | Solar elevation angle in degrees |
| `13` | `sun_azimuth` | `WeatherField::SUN_AZIMUTH` | `f32` | Solar azimuth angle in degrees (0..360) |
| `14` | `sun_next_rising` | `WeatherField::SUN_NEXT_RISING` | `string` | Next sunrise ISO-8601 UTC timestamp |
| `15` | `sun_next_setting` | `WeatherField::SUN_NEXT_SETTING` | `string` | Next sunset ISO-8601 UTC timestamp |
| `16` | `moon_phase` | `WeatherField::MOON_PHASE` | `string` | Lunar phase slug (new_moon|waxing_crescent|first_quarter|waxing_gibbous|full_moon|waning_gibbous|last_quarter|waning_crescent) |
| `17` | `temp_unit` | `WeatherField::TEMP_UNIT` | `string` | Outdoor temperature unit ('C' or 'F') |

### `calendar_agenda` (`CalendarField`, `7` total fields)

Upcoming calendar agenda events and next-event headline.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `summary` | `CalendarField::SUMMARY` | `records` | 0x1E-joined time|title|location|all_day agenda records (up to 5; time is a local label such as TODAY 14:30, TOM ALL DAY or NOV 20 08:05; all_day 1 or 0) |
| `4` | `count` | `CalendarField::EVENT_COUNT` | `u16` | Number of upcoming agenda events |
| `5` | `calendars_meta` | `CalendarField::CALENDARS_META` | `records` | 0x1E-joined entity_id|label|color_idx|can_create|can_delete calendar source records (up to 8; color_idx 0..5, can_create and can_delete 1 or 0) |
| `6` | `locations_catalog` | `CalendarField::LOCATIONS_CATALOG` | `records` | 0x1E-joined Home Assistant Zone friendly names and recent calendar locations (up to 12) for Add Event location selection |

### `device_prefs` (`DevicePrefsField`, `78` total fields)

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
| `10` | `screensaver_mode` | `DevicePrefsField::SCREENSAVER_MODE` | `string` | Screensaver activation policy when idle (Docked Only|Always|Never) |
| `11` | `screensaver_style` | `DevicePrefsField::SCREENSAVER_STYLE` | `string` | Ambient screensaver style (Ambient Clock|Photo Frame|Aurora|Starfield|Digital Rain|Fireworks) |
| `12` | `auto_brightness_mode` | `DevicePrefsField::AUTO_BRIGHTNESS_MODE` | `string` | Automatic brightness mode (Disabled|Sun Synchronized|Time Schedule|Ambient Light) |
| `13` | `manual_brightness` | `DevicePrefsField::MANUAL_BRIGHTNESS` | `u16` | Manual brightness target when auto brightness is Disabled (1..100%) |
| `14` | `day_brightness` | `DevicePrefsField::DAY_BRIGHTNESS` | `u16` | Daytime brightness target (1..100%) |
| `15` | `night_brightness` | `DevicePrefsField::NIGHT_BRIGHTNESS` | `u16` | Nighttime brightness target (1..100%) |
| `16` | `day_start_hour` | `DevicePrefsField::DAY_START_HOUR` | `u16` | Time schedule daytime start hour (0..23) |
| `17` | `night_start_hour` | `DevicePrefsField::NIGHT_START_HOUR` | `u16` | Time schedule nighttime start hour (0..23) |
| `18` | `color_profile` | `DevicePrefsField::COLOR_PROFILE` | `string` | Theme accent preset (Steel Blue|Pink|Blue|Green|Custom) |
| `19` | `va_visual_profile` | `DevicePrefsField::VA_VISUAL_PROFILE` | `string` | Voice assistant visual overlay style (Nebula|Solaris|Rings|Light Bar|Dot Wave) |
| `20` | `home_screen_enabled` | `DevicePrefsField::HOME_SCREEN_ENABLED` | `bool` | 1 = enable the Home Screen and App Dock, 0 = single-app mode locked to start_screen |
| `21` | `start_screen` | `DevicePrefsField::START_SCREEN` | `string` | Boot / single-app start screen (Home|Pinned|Time App|Calendar|Clock Face|Media|Home Hub|Area Controls|Remote|Weather|Cameras|Games|Tools) |
| `22` | `return_to_home_timeout` | `DevicePrefsField::RETURN_TO_HOME_TIMEOUT` | `string` | Idle timeout before resetting navigation back to the Home Screen (15s|30s|1 min|2 min|5 min|10 min|Never) |
| `23` | `clock_face` | `DevicePrefsField::CLOCK_FACE` | `string` | Clock face style (Minimal Digital|Modern Analog|Hybrid Digital) |
| `24` | `time_format` | `DevicePrefsField::TIME_FORMAT` | `string` | Clock time format (12-Hour (AM/PM)|24-Hour) |
| `25` | `weather_background` | `DevicePrefsField::WEATHER_BACKGROUND` | `bool` | 1 = animate ambient weather particles on Home screen, else 0 |
| `26` | `home_sun` | `DevicePrefsField::HOME_SUN` | `bool` | 1 = show daytime solar disc on Home screen, else 0 |
| `27` | `home_moon` | `DevicePrefsField::HOME_MOON` | `bool` | 1 = show nighttime lunar phase disc on Home screen, else 0 |
| `28` | `wake_greeting` | `DevicePrefsField::WAKE_GREETING` | `bool` | 1 = show time-of-day wake greeting toast, else 0 |
| `29` | `album_art_style` | `DevicePrefsField::ALBUM_ART_STYLE` | `string` | Now Playing album art layout (Full Screen|Letterbox|Standard|None) |
| `30` | `music_overlay_style` | `DevicePrefsField::MUSIC_OVERLAY_STYLE` | `string` | Now Playing controls overlay visibility (Art Only|Art + Metadata|Always Visible) |
| `31` | `album_sort_order` | `DevicePrefsField::ALBUM_SORT_ORDER` | `string` | Album browser sort order (Alphabetical|Newest on Top|Oldest on Top) |
| `32` | `artist_album_sort_order` | `DevicePrefsField::ARTIST_ALBUM_SORT_ORDER` | `string` | Artist discography sort order (Oldest on Top|Newest on Top|Alphabetical) |
| `33` | `podcast_sort_order` | `DevicePrefsField::PODCAST_SORT_ORDER` | `string` | Podcast episode sort order (Newest on Top|Oldest on Top) |
| `34` | `ww_sens_okay_nabu` | `DevicePrefsField::WW_SENS_OKAY_NABU` | `string` | Okay Nabu wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `35` | `ww_sens_hey_luna` | `DevicePrefsField::WW_SENS_HEY_LUNA` | `string` | Hey Luna wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `36` | `ww_sens_hey_jarvis` | `DevicePrefsField::WW_SENS_HEY_JARVIS` | `string` | Hey Jarvis wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `37` | `ww_sens_hey_mycroft` | `DevicePrefsField::WW_SENS_HEY_MYCROFT` | `string` | Hey Mycroft wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `38` | `ww_sens_alexa` | `DevicePrefsField::WW_SENS_ALEXA` | `string` | Alexa wake word sensitivity (Slightly sensitive|Moderately sensitive|Very sensitive) |
| `39` | `wake_screen_on_assist` | `DevicePrefsField::WAKE_SCREEN_ON_ASSIST` | `bool` | 1 = wake display automatically when voice assistant triggers, else 0 |
| `40` | `alert_volume` | `DevicePrefsField::ALERT_VOLUME` | `u16` | Timer, alarm, and notification chime volume percentage (20..100) |
| `41` | `volume_step_size` | `DevicePrefsField::VOLUME_STEP_SIZE` | `u16` | Rotary/button volume increment step percentage (1..10) |
| `42` | `game_sound_effects` | `DevicePrefsField::GAME_SOUND_EFFECTS` | `bool` | 1 = play arcade game sound effects, else 0 |
| `43` | `ble_beacon_enabled` | `DevicePrefsField::BLE_BEACON_ENABLED` | `bool` | 1 = enable BLE iBeacon presence advertising (derived from native_ble_tracking), else 0 |
| `44` | `ble_beacon_mode` | `DevicePrefsField::BLE_BEACON_MODE` | `string` | BLE beacon advertising duty policy (Handheld Only|Handheld + Screen Off|Always) |
| `45` | `weather_sim_condition` | `DevicePrefsField::WEATHER_SIM_CONDITION` | `string` | Developer weather animation override (Live (Home Assistant) or simulated condition label) |
| `46` | `theme_color_hex` | `DevicePrefsField::THEME_COLOR_HEX` | `string` | Custom theme accent color (#RRGGBB) synchronized with device_custom_color |
| `47` | `home_clock_tap_action` | `DevicePrefsField::HOME_CLOCK_TAP_ACTION` | `string` | Action when tapping the time readout on the Home Screen (Time App|Clock Face|None) |
| `48` | `show_utc_clock` | `DevicePrefsField::SHOW_UTC_CLOCK` | `bool` | 1 = show a dedicated UTC World Clock card on the Time App Clock tab, else 0 |
| `49` | `world_clocks` | `DevicePrefsField::WORLD_CLOCKS` | `string` | Semicolon-delimited custom-labeled World Clock entries (up to 4, '<label>|<iana>;...') shown on the Time App Clock tab |
| `50` | `alarms_summary` | `DevicePrefsField::ALARMS_SUMMARY` | `string` | Semicolon-delimited 3-slot device alarm schedule ('HH:MM|enabled|recurrence;...') |
| `51` | `local_clock_label` | `DevicePrefsField::LOCAL_CLOCK_LABEL` | `string` | Optional user-defined label shown above local time on the Time App Clock tab (up to 32 chars) |
| `52` | `tools_items` | `DevicePrefsField::TOOLS_ITEMS` | `csv` | Comma-separated enabled and ordered Tools launcher items (stopwatch,level,flashlight,calculator,dice,coin_flip) |
| `53` | `games_items` | `DevicePrefsField::GAMES_ITEMS` | `csv` | Comma-separated enabled and ordered Games launcher items (blocks,snake,drop_four,marbles,archery,bricks,chomp) |
| `54` | `home_date_tap_action` | `DevicePrefsField::HOME_DATE_TAP_ACTION` | `string` | Action when tapping the date readout on the Home Screen (Day View|Overview|None) |
| `55` | `calendar_lookahead_days` | `DevicePrefsField::CALENDAR_LOOKAHEAD_DAYS` | `u16` | Calendar Overview upcoming lookahead horizon in days (7|14|30|60) |
| `56` | `calendar_week_start` | `DevicePrefsField::CALENDAR_WEEK_START` | `string` | First day of week in Calendar Week and Month views (Sunday|Monday) |
| `57` | `bedtime_active` | `DevicePrefsField::BEDTIME_ACTIVE` | `bool` | 1 = Bedtime Mode currently active (ultra-dim wake brightness, 15s idle -> bedtime_display, suppressed wake greeting, and silenced non-critical notification chimes/screen wakes), else 0 |
| `58` | `bedtime_display` | `DevicePrefsField::BEDTIME_DISPLAY` | `string` | Idle display behavior while Bedtime Mode is active (Screen Off|Ambient Clock) |
| `59` | `bedtime_schedule` | `DevicePrefsField::BEDTIME_SCHEDULE` | `bool` | 1 = automatically toggle Bedtime Mode on at bedtime_start_hour and off at bedtime_end_hour (manual/HA switch overrides hold until next boundary), else 0 |
| `60` | `bedtime_start_hour` | `DevicePrefsField::BEDTIME_START_HOUR` | `u16` | Bedtime Mode schedule start hour in local 24h time (0..23) |
| `61` | `bedtime_end_hour` | `DevicePrefsField::BEDTIME_END_HOUR` | `u16` | Bedtime Mode schedule end hour in local 24h time (0..23) |
| `62` | `bedtime_brightness` | `DevicePrefsField::BEDTIME_BRIGHTNESS` | `u16` | Bedtime Mode active wake brightness percentage (1..100%) |
| `63` | `camera_default_view` | `DevicePrefsField::CAMERA_DEFAULT_VIEW` | `string` | Default view when opening the Cameras app (Grid|Single|Carousel) |
| `64` | `camera_carousel_interval` | `DevicePrefsField::CAMERA_CAROUSEL_INTERVAL` | `string` | Auto-cycle interval in Cameras Carousel view (10s|15s|30s|60s) |
| `65` | `camera_framing` | `DevicePrefsField::CAMERA_FRAMING` | `string` | Camera video framing mode (Uncropped|Fill) |
| `66` | `camera_stream_mode` | `DevicePrefsField::CAMERA_STREAM_MODE` | `string` | Camera live streaming quality and bandwidth mode (Auto|Low Bandwidth|Snapshots) |
| `67` | `camera_doorbell_popup` | `DevicePrefsField::CAMERA_DOORBELL_POPUP` | `string` | Doorbell ring popup media mode on hardware-JPEG devices (Snapshot|Live Video) |
| `68` | `screensaver_photo_clock` | `DevicePrefsField::SCREENSAVER_PHOTO_CLOCK` | `bool` | 1 = show subtle clock overlay on Photo Frame screensaver, else 0 |
| `69` | `weather_app_background` | `DevicePrefsField::WEATHER_APP_BACKGROUND` | `bool` | 1 = show live animated weather sky background in the Sky & Weather app, else 0 |
| `70` | `do_not_disturb` | `DevicePrefsField::DO_NOT_DISTURB` | `bool` | 1 = Do Not Disturb active, else 0 |
| `71` | `music_auto_open` | `DevicePrefsField::MUSIC_AUTO_OPEN` | `string` | Auto-open Music view when default speaker starts playing (Now Playing|Lyrics|Off) |
| `72` | `glass_style` | `DevicePrefsField::GLASS_STYLE` | `string` | Glass chrome surface style (Auto|Clear|Dark); Auto adapts contrast over bright backdrops |
| `73` | `home_show_date` | `DevicePrefsField::HOME_SHOW_DATE` | `bool` | 1 = show date readout on Home screen, else 0 |
| `74` | `home_show_time` | `DevicePrefsField::HOME_SHOW_TIME` | `bool` | 1 = show clock readout on Home screen, else 0 |
| `75` | `home_show_weather` | `DevicePrefsField::HOME_SHOW_WEATHER` | `bool` | 1 = show current weather summary text on Home screen, else 0 |
| `76` | `home_weather_tap_action` | `DevicePrefsField::HOME_WEATHER_TAP_ACTION` | `string` | Action when tapping the weather summary on the Home Screen (Current Weather|5-Day Forecast|None) |
| `77` | `home_show_battery` | `DevicePrefsField::HOME_SHOW_BATTERY` | `bool` | 1 = show battery indicator on Home screen on battery-powered devices, else 0 |

### `shortcuts` (`ShortcutsField`, `9` total fields)

App Dock and Pinned App shortcut tables (Latter OS routes and Home Assistant entity actions with live state), chunked across <=24-item frames for unlimited pages.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `chunk_index` | `ShortcutsField::CHUNK_INDEX` | `u16` | 0-based chunk index (0 .. chunk_count - 1) |
| `4` | `chunk_count` | `ShortcutsField::CHUNK_COUNT` | `u16` | Total chunks in this shortcuts snapshot (>= 1; commits atomically on chunk_index + 1 == chunk_count) |
| `5` | `dock_items` | `ShortcutsField::DOCK_ITEMS` | `records` | 0x1E-joined kind|target|action|label|icon|confirm|state|badge_style|accent|page|col|row|col_span|row_span App Dock shortcut records (sent on chunk_index == 0, up to 24; badge_style is empty/glass or inverted; accent is empty/default or primary|gold|green|red|orange; page/col/row/col_span/row_span empty on App Dock; empty until configured so device uses defaults.dock_routes; Settings is always appended last) |
| `6` | `pinned_total` | `ShortcutsField::PINNED_TOTAL` | `u16` | Total configured Pinned App items across all chunks (0 auto-hides app:pinned on the App Dock) |
| `7` | `pinned_items` | `ShortcutsField::PINNED_ITEMS` | `records` | 0x1E-joined kind|target|action|label|icon|confirm|state|badge_style|accent|page|col|row|col_span|row_span Pinned App shortcut records for this chunk (up to 24 per chunk; badge_style is empty/glass or inverted; accent is empty/default or primary|gold|green|red|orange; page 0..31, col 0..3, row 0..2 empty when auto-flow; col_span 1..4, row_span 1..3 empty/1 for 1x1) |
| `8` | `pinned_page_titles` | `ShortcutsField::PINNED_PAGE_TITLES` | `records` | 0x1E-joined page_index|title optional custom titles for Pinned App pages (sent on chunk_index == 0, up to 32 entries, title max 24 chars; absent pages render the default PINNED header) |

## 3. ESPHome Device Actions (`esphome.<node>_<action>`)

| Action | Variables | Description |
| :--- | :--- | :--- |
| `latter_ping` | *(none)* | No-op discovery marker action announcing Latter Protocol v3 compatibility and hardware manifest |
| `latter_push_v3` | `domain: string`, `payload: string` | Push a revision-guarded Protocol v3 domain payload directly to the bound Device over the ESPHome Native API |
| `latter_set_beacon_uuid` | `uuid: string` | Provision the System's random 128-bit BLE iBeacon UUID into NVS and restart advertising if active |
| `latter_factory_reset` | *(none)* | Reset to defaults: erases every saved setting (the whole NVS partition, including the Wi-Fi driver's cached network data and RF calibration) and reboots. Compiled-in Wi-Fi credentials and the API key are kept, so the device rejoins Home Assistant unchanged |
| `latter_device_command` | `command: string` | Execute a typed Device lifecycle, display, telemetry, security, remote navigation, or volatile showcase command (restart|find_my|activate_screensaver|deactivate_screensaver|wake_screen|screen_off|set_screen_bri:<1..100>|set_profiler_on|set_profiler_off|request_telemetry|set_pin_key:<key_id>:<hex_key>|trigger_va|dismiss_ringing|cancel_timer|reset_stopwatch|nav:<route>|nav_silent:<route>|showcase:<on[:sec]|off|va:<stage>[:profile]|screensaver:<style>|weather:<cond>|clock:<style>|theme:<preset|#RRGGBB>|brightness:<0..100>|timer:<sec|off>|stopwatch:<start|stop|reset>|roll|find_my:<on|off>|alarm:<on|off>|reel[:sec]>) |
| `latter_set_slots` | `is_area_slots: bool`, `req_id: int`, `mode: string`, `page: int`, `total_pages: int`, `slots: string` | Push paginated media/speaker/queue/search browse slots or Area/Site entity control slots to the device (0x1E-joined name\x1fid\x1ftype\x1factive\x1flevel[\x1fqueue_item_id\x1fis_current] records) |
| `latter_set_music_lyrics` | `req_id: int`, `chunk_index: int`, `chunk_count: int`, `track_key: string`, `synced: bool`, `lines: string` | Push full-track LRC timestamp-synced or plain lyrics (up to 128 lines across multi-chunk frames; lines is 0x1E-joined '<t_ms>\x1f<text>' records where t_ms >= 0 for synced LRC lines and -1 for plain unsynced lines) |
| `latter_set_calendar_view` | `req_id: int`, `chunk_index: int`, `chunk_count: int`, `window_start: string`, `window_end: string`, `events: string` | Push chunked windowed calendar events (up to 64 events across multi-chunk frames) to the device |
| `latter_show_notification` | `title: string`, `message: string`, `priority: string`, `duration: int`, `chime: bool`, `camera_url: string`, `camera_entity: string`, `alert_id: int` | Wake display, show an on-screen alert banner (priority normal|warning|critical, duration 3-60 s, default 10), optionally play the embedded WAV chime and show a camera snapshot or live doorbell video; alert_id > 0 deduplicates repeats |

## 4. Firmware RPC Outbox Methods (`latter_rpc_outbox`)

| Method | Canonical Outbox Fields | Description |
| :--- | :--- | :--- |
| `device_hello` | `contract`, `boot_id`, `manifest`, `prefs_seq`, `dirty_prefs` | Device-initiated handshake sent when Home Assistant connects to the ESPHome API, carrying optional NVS preference sync state |
| `device_telemetry` | `link_json`, `int_ram_free_kb`, `int_ram_min_kb`, `int_ram_largest_kb`, `psram_min_kb`, `ble_active`, `profiler_on` | Periodic and on-demand device link, heap, PSRAM, BLE, and profiler telemetry reported to Home Assistant |
| `display_state` | `power_state`, `brightness_pct`, `screensaver_active` | Report physical display power, perceptual brightness percentage, and screensaver transitions to Home Assistant |
| `music_browse` | `req_id`, `mode`, `page`, `page_size`, `search`, `speaker`, `last_speaker`, `album_sort`, `artist_album_sort`, `podcast_sort` | Browse speakers, library, favorites, playlists, artists, albums, tracks, podcasts, active queue, artist/album bios, or fetch full-track lyrics (lyrics_fetch) |
| `set_active_speaker` | `entity_id`, `transfer_queue` | Select the active media_player target for a Latter device |
| `media_command` | `command`, `seek_position`, `entity_id`, `media_id`, `media_type`, `enqueue`, `radio_mode`, `queue_item_id`, `pos_shift`, `group_members` | Execute transport, seek, play_media (enqueue play|next|add|replace + radio_mode), queue manipulation (queue_play, queue_move, queue_delete), player toggles (mute_toggle, shuffle_toggle, repeat_cycle, favorite_toggle, lyrics_fetch), or speaker grouping on the active speaker |
| `media_volume` | `entity_id`, `action`, `volume_level` | Adjust or set volume on the active speaker or volume override target |
| `area_browse` | `req_id`, `mode`, `area_id`, `domain_filter`, `page`, `page_size` | Browse Areas, Area domains, or paginated controllable entities |
| `area_action` | `entity_id`, `action`, `level`, `area_id`, `code` | Execute an explicit domain action on an Area or specific entity |
| `set_active_area` | `area_id` | Set the active Area context for a Latter device (an on-device Area pick). Whether media follows is the Device's Latter setting |
| `remote_action` | `slot`, `action`, `command`, `hold_secs` | Send a TV/AV remote command or select active remote slot |
| `calendar_fetch` | `req_id`, `mode`, `start_date`, `end_date`, `days` | Fetch windowed calendar events for a date range |
| `calendar_create_event` | `calendar_entity`, `summary`, `start_date_time`, `end_date_time`, `all_day`, `start_date`, `end_date`, `location`, `rrule` | Create a timed, all-day, or recurring event on a writable Home Assistant calendar |
| `calendar_delete_event` | `calendar_entity`, `uid`, `recurrence_id` | Delete an event or recurring instance from a writable Home Assistant calendar |
| `intercom_broadcast` | `message` | Broadcast a voice announcement to configured broadcast speakers, chosen speakers, or occupied Areas |
| `doorbell_intercom` | `action`, `camera_entity`, `camera_index`, `message`, `noise_reduction` | Doorbell talkback and requests (talk_message, quick_reply, listen_start, listen_stop, set_noise_reduction) |
| `save_custom_color` | `hex_color` | Save a custom RGB accent color from the on-device color wheel |
| `shortcut_action` | `entity_id`, `action`, `code` | Execute an allowlisted Home Assistant entity action from a configured App Dock or Pinned shortcut |
| `set_device_prefs` | `base_seq`, `changes` | Persist on-device settings edits back to the Device subentry and increment prefs_seq |
| `timer_alarm_state` | `timer_running`, `timer_paused`, `remaining_sec`, `ringing`, `stopwatch_running`, `stopwatch_paused`, `stopwatch_elapsed_sec` | Report on-device countdown timer, stopwatch, and alarm ringing state transitions to Home Assistant |

## 5. Declarative Device Preferences (`device_pref_specs`, `70` entries)

Pure wire and behavioral preference contracts compiled into `include/latter_protocol.h` (`kPrefContractSpecs`). Studio/HA presentation metadata lives in `ha-latter/custom_components/latter/studio_contract.json`.

| Key | Domain | Default | Options / Bounds | Capability |
| :--- | :---: | :--- | :--- | :---: |
| `wake_word_mode` | `select` | `Docked Only` | `Docked Only` \| `Always On` \| `Disabled` | `mic` |
| `tap_to_wake` | `select` | `Docked Only` | `Single Tap` \| `Double Tap` \| `Docked Only` \| `Disabled` | `display` |
| `pickup_to_wake` | `select` | `Docked Only` | `Always On` \| `Docked Only` \| `Disabled` | `display_imu` |
| `battery_screen_timeout` | `select` | `30s` | `10s` \| `15s` \| `30s` \| `1 min` \| `2 min` \| `Never` | `display_battery` |
| `docked_idle_timeout` | `select` | `1 min` | `10s` \| `15s` \| `30s` \| `1 min` \| `2 min` \| `5 min` \| `Never` | `display` |
| `battery_hibernate_timeout` | `select` | `15 min` | `5 min` \| `10 min` \| `15 min` \| `30 min` \| `Disabled` | `display_battery` |
| `screensaver_mode` | `select` | `Docked Only` | `Docked Only` \| `Always` \| `Never` | `display` |
| `screensaver_style` | `select` | `Ambient Clock` | `Ambient Clock` \| `Photo Frame` \| `Aurora` \| `Starfield` \| `Digital Rain` \| `Fireworks` | `display` |
| `auto_brightness_mode` | `select` | `Sun Synchronized` | `Disabled` \| `Sun Synchronized` \| `Time Schedule` \| `Ambient Light` | `display` |
| `manual_brightness` | `number` | `80` | `1..100` (step `1`) | `display` |
| `day_brightness` | `number` | `90` | `1..100` (step `1`) | `display` |
| `night_brightness` | `number` | `35` | `1..100` (step `1`) | `display` |
| `day_start_hour` | `number` | `7` | `0..23` (step `1`) | `display` |
| `night_start_hour` | `number` | `20` | `0..23` (step `1`) | `display` |
| `color_profile` | `select` | `Steel Blue` | `Steel Blue` \| `Pink` \| `Blue` \| `Green` \| `Custom` | `display` |
| `va_visual_profile` | `select` | `Nebula` | `Nebula` \| `Solaris` \| `Rings` \| `Light Bar` \| `Dot Wave` | `display` |
| `home_screen_enabled` | `switch` | `true` | `0` \| `1` | `display` |
| `start_screen` | `select` | `Home` | `Home` \| `Pinned` \| `Time App` \| `Calendar` \| `Clock Face` \| `Media` \| `Home Hub` \| `Area Controls` \| `Remote` \| `Weather` \| `Cameras` \| `Games` \| `Tools` | `display` |
| `return_to_home_timeout` | `select` | `2 min` | `15s` \| `30s` \| `1 min` \| `2 min` \| `5 min` \| `10 min` \| `Never` | `display` |
| `clock_face` | `select` | `Minimal Digital` | `Minimal Digital` \| `Modern Analog` \| `Hybrid Digital` | `display` |
| `time_format` | `select` | `12-Hour (AM/PM)` | `12-Hour (AM/PM)` \| `24-Hour` | `display` |
| `weather_background` | `switch` | `true` | `0` \| `1` | `display` |
| `home_sun` | `switch` | `true` | `0` \| `1` | `display` |
| `home_moon` | `switch` | `true` | `0` \| `1` | `display` |
| `wake_greeting` | `switch` | `true` | `0` \| `1` | `display` |
| `album_art_style` | `select` | `Full Screen` | `Full Screen` \| `Letterbox` \| `Standard` \| `None` | `display` |
| `music_overlay_style` | `select` | `Art + Metadata` | `Art Only` \| `Art + Metadata` \| `Always Visible` | `display` |
| `music_auto_open` | `select` | `Now Playing` | `Now Playing` \| `Lyrics` \| `Off` | `display` |
| `album_sort_order` | `select` | `Alphabetical` | `Alphabetical` \| `Newest on Top` \| `Oldest on Top` | `display` |
| `artist_album_sort_order` | `select` | `Oldest on Top` | `Oldest on Top` \| `Newest on Top` \| `Alphabetical` | `display` |
| `podcast_sort_order` | `select` | `Newest on Top` | `Newest on Top` \| `Oldest on Top` | `display` |
| `ww_sens_okay_nabu` | `select` | `Moderately sensitive` | `Slightly sensitive` \| `Moderately sensitive` \| `Very sensitive` | `mic` |
| `ww_sens_hey_luna` | `select` | `Moderately sensitive` | `Slightly sensitive` \| `Moderately sensitive` \| `Very sensitive` | `mic` |
| `ww_sens_hey_jarvis` | `select` | `Moderately sensitive` | `Slightly sensitive` \| `Moderately sensitive` \| `Very sensitive` | `mic` |
| `ww_sens_hey_mycroft` | `select` | `Moderately sensitive` | `Slightly sensitive` \| `Moderately sensitive` \| `Very sensitive` | `mic` |
| `ww_sens_alexa` | `select` | `Moderately sensitive` | `Slightly sensitive` \| `Moderately sensitive` \| `Very sensitive` | `mic` |
| `wake_screen_on_assist` | `switch` | `true` | `0` \| `1` | `display` |
| `alert_volume` | `number` | `80` | `20..100` (step `5`) | `speaker` |
| `volume_step_size` | `number` | `5` | `1..10` (step `1`) | `speaker` |
| `game_sound_effects` | `switch` | `true` | `0` \| `1` | `display` |
| `ble_beacon_mode` | `select` | `Handheld Only` | `Handheld Only` \| `Handheld + Screen Off` \| `Always` | `beacon_battery` |
| `weather_sim_condition` | `select` | `Live (Home Assistant)` | `Live (Home Assistant)` \| `Clear Night` \| `Sunny / Clear Day` \| `Partly Cloudy (Day)` \| `Partly Cloudy (Night)` \| `Cloudy / Overcast (Day)` \| `Cloudy / Overcast (Night)` \| `Fog / Mist (Day)` \| `Fog / Mist (Night)` \| `Rainy (Day)` \| `Rainy (Night)` \| `Pouring / Heavy Rain` \| `Snow (Day)` \| `Snow (Night)` \| `Thunderstorm (Day)` \| `Thunderstorm (Night)` \| `Windy` | `display` |
| `home_clock_tap_action` | `select` | `Time App` | `Time App` \| `Clock Face` \| `None` | `display` |
| `home_date_tap_action` | `select` | `Overview` | `Day View` \| `Overview` \| `None` | `display` |
| `local_clock_label` | `text` | *(empty)* | *(freeform string)* | `display` |
| `show_utc_clock` | `switch` | `false` | `0` \| `1` | `display` |
| `world_clocks` | `text` | *(empty)* | *(freeform string)* | `display` |
| `alarms_summary` | `text` | `07:00\|0\|1;08:30\|0\|2;09:00\|0\|2` | *(freeform string)* | `display` |
| `calendar_lookahead_days` | `number` | `30` | `7..60` (step `1`) | `display` |
| `calendar_week_start` | `select` | `Sunday` | `Sunday` \| `Monday` | `display` |
| `bedtime_active` | `switch` | `false` | `0` \| `1` | `display` |
| `bedtime_display` | `select` | `Screen Off` | `Screen Off` \| `Ambient Clock` | `display` |
| `bedtime_schedule` | `switch` | `false` | `0` \| `1` | `display` |
| `bedtime_start_hour` | `number` | `22` | `0..23` (step `1`) | `display` |
| `bedtime_end_hour` | `number` | `7` | `0..23` (step `1`) | `display` |
| `bedtime_brightness` | `number` | `15` | `1..100` (step `1`) | `display` |
| `camera_default_view` | `select` | `Grid` | `Grid` \| `Single` \| `Carousel` | `display` |
| `camera_carousel_interval` | `select` | `15s` | `10s` \| `15s` \| `30s` \| `60s` | `display` |
| `camera_framing` | `select` | `Uncropped` | `Uncropped` \| `Fill` | `display` |
| `camera_stream_mode` | `select` | `Auto` | `Auto` \| `Low Bandwidth` \| `Snapshots` | `display` |
| `camera_doorbell_popup` | `select` | `Snapshot` | `Snapshot` \| `Live Video` | `hw_jpeg` |
| `screensaver_photo_clock` | `switch` | `false` | `0` \| `1` | `display` |
| `weather_app_background` | `switch` | `true` | `0` \| `1` | `display` |
| `do_not_disturb` | `switch` | `false` | `0` \| `1` | `display_or_audio` |
| `glass_style` | `select` | `Auto` | `Auto` \| `Clear` \| `Dark` | `display` |
| `home_show_date` | `switch` | `true` | `0` \| `1` | `display` |
| `home_show_time` | `switch` | `true` | `0` \| `1` | `display` |
| `home_show_weather` | `switch` | `true` | `0` \| `1` | `display` |
| `home_weather_tap_action` | `select` | `Current Weather` | `Current Weather` \| `5-Day Forecast` \| `None` | `display` |
| `home_show_battery` | `switch` | `true` | `0` \| `1` | `display_battery` |
