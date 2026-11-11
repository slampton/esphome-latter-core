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
| `dock_apps` | `music,alarm,area,remote,games,tools` |
| `activity_card` | `1` |
| `activity_sources` | `calendar,music_queue,stopwatch,alarms,timers` |

## 2. Domain Payloads

### `sensor.<device>_area_context` (`AreaCtxField`, `41` total fields)

Active Area state, climate/security/cover summaries, camera/intercom directories, and alert banner.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `area_id` | `AreaCtxField::AREA_ID` | `string` | Active Home Assistant Area ID |
| `4` | `area_name` | `AreaCtxField::AREA_NAME` | `string` | Friendly display name of the active Area |
| `5` | `site_id` | `AreaCtxField::SITE_ID` | `string` | Active Site ID |
| `6` | `ble_closest_proxy` | `AreaCtxField::BLE_CLOSEST_PROXY` | `string` | Friendly name of closest Bluetooth proxy |
| `7` | `dock_apps` | `AreaCtxField::DOCK_APPS` | `csv` | Home dock apps in order, and the only ones shown (music, alarm, area, remote, camera, games, tools; Settings is always last). Empty until set: the device shows defaults.dock_apps |
| `8` | `wallpaper_url` | `AreaCtxField::WALLPAPER_URL` | `url` | Signed HTTP URL for active wallpaper |
| `9` | `theme_color_hex` | `AreaCtxField::THEME_COLOR_HEX` | `string` | Active theme accent color (#RRGGBB) |
| `10` | `available_domains` | `AreaCtxField::AVAILABLE_DOMAINS` | `csv` | Comma-separated available Area domains |
| `11` | `area_temp` | `AreaCtxField::AREA_TEMP` | `string` | Formatted Area temperature |
| `12` | `area_humidity` | `AreaCtxField::AREA_HUMIDITY` | `string` | Formatted Area relative humidity |
| `13` | `lights_on` | `AreaCtxField::LIGHTS_ON` | `u16` | Count of active lights in Area |
| `14` | `covers_open` | `AreaCtxField::COVERS_OPEN` | `u16` | Count of open covers in Area |
| `15` | `alarm_entity` | `AreaCtxField::ALARM_ENTITY` | `entity_id` | Primary alarm control panel entity ID |
| `16` | `alarm_state` | `AreaCtxField::ALARM_STATE` | `string` | Primary alarm state |
| `17` | `thermostat_entity` | `AreaCtxField::THERMOSTAT_ENTITY` | `entity_id` | Primary climate entity ID |
| `18` | `thermostat_state` | `AreaCtxField::THERMOSTAT_STATE` | `string` | Primary climate state/HVAC mode |
| `19` | `thermostat_temp` | `AreaCtxField::THERMOSTAT_TEMP` | `f32` | Target thermostat setpoint temperature |
| `20` | `thermostat_current_temp` | `AreaCtxField::THERMOSTAT_CURRENT_TEMP` | `f32` | Current measured thermostat temperature |
| `21` | `lock_entity` | `AreaCtxField::LOCK_ENTITY` | `entity_id` | Primary lock entity ID |
| `22` | `lock_state` | `AreaCtxField::LOCK_STATE` | `string` | Primary lock state |
| `23` | `garage_entity` | `AreaCtxField::GARAGE_ENTITY` | `entity_id` | Primary garage cover entity ID |
| `24` | `garage_state` | `AreaCtxField::GARAGE_STATE` | `string` | Primary garage cover state |
| `25` | `vacuum_entity` | `AreaCtxField::VACUUM_ENTITY` | `entity_id` | Primary vacuum entity ID |
| `26` | `vacuum_state` | `AreaCtxField::VACUUM_STATE` | `string` | Primary vacuum state |
| `27` | `camera_snapshot_url` | `AreaCtxField::CAMERA_SNAPSHOT_URL` | `url` | Signed HTTP URL for primary camera snapshot |
| `28` | `cameras_summary` | `AreaCtxField::CAMERAS_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|snapshot_url camera records (up to 10, in configured order; area_id empty when the camera has no Area) |
| `29` | `doorbell_talk_state` | `AreaCtxField::DOORBELL_TALK_STATE` | `string` | Doorbell talkback state |
| `30` | `intercom_targets_summary` | `AreaCtxField::INTERCOM_TARGETS_SUMMARY` | `records` | 0x1E-joined target_id|label|kind call_target records (up to 10; kind all = every other Latter Device, listed first; kind device = one Latter Device, labelled by its title); empty when there is no other Device |
| `31` | `alert_id` | `AreaCtxField::ALERT_ID` | `u32` | Monotonic alert/notification identifier |
| `32` | `alert_title` | `AreaCtxField::ALERT_TITLE` | `string` | Alert banner title |
| `33` | `alert_message` | `AreaCtxField::ALERT_MESSAGE` | `string` | Alert banner message body |
| `34` | `alert_priority` | `AreaCtxField::ALERT_PRIORITY` | `string` | Alert priority level (normal|warning|critical) |
| `35` | `alert_duration` | `AreaCtxField::ALERT_DURATION` | `u16` | Alert display duration in seconds |
| `36` | `alert_chime` | `AreaCtxField::ALERT_CHIME` | `bool` | 1 = play the alert chime (embedded WAV: sfx 9 for warning/critical, otherwise sfx 6), else 0 |
| `37` | `alert_camera_url` | `AreaCtxField::ALERT_CAMERA_URL` | `url` | Signed HTTP URL for alert camera snapshot |
| `38` | `activity_card` | `AreaCtxField::ACTIVITY_CARD` | `bool` | 1 = show the Home Activity Card, 0 = hide it (also sent when every source is off); empty until set: the device uses defaults.activity_card |
| `39` | `activity_sources` | `AreaCtxField::ACTIVITY_SOURCES` | `csv` | Activity Card sources that are on, in this order: calendar, music_queue, stopwatch, alarms, timers (unknown keys are ignored); empty until set: the device uses defaults.activity_sources |
| `40` | `security_pin_policy` | `AreaCtxField::SECURITY_PIN_POLICY` | `string` | Perimeter action policy for unlock, disarm, and open_cover (allow|require_pin|block) |

### `sensor.<device>_active_media` (`MediaField`, `20` total fields)

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
| `12` | `supported_features` | `MediaField::SUPPORTED_FEATURES` | `u32` | Home Assistant MediaPlayerEntityFeature bitmask |
| `13` | `art_url` | `MediaField::ART_URL` | `url` | Signed HTTP URL for album art proxy |
| `14` | `active_speaker` | `MediaField::ACTIVE_SPEAKER` | `entity_id` | Active media_player entity ID |
| `15` | `speaker_name` | `MediaField::SPEAKER_NAME` | `string` | Friendly name of active speaker |
| `16` | `last_speaker` | `MediaField::LAST_SPEAKER` | `entity_id` | Previously active media_player entity ID |
| `17` | `local_speaker` | `MediaField::LOCAL_SPEAKER` | `entity_id` | Device's own local media_player entity ID |
| `18` | `volume_entity` | `MediaField::VOLUME_ENTITY` | `entity_id` | Entity ID used for volume control |
| `19` | `is_local` | `MediaField::IS_LOCAL` | `bool` | 1 if active_speaker is this device's own speaker, else 0 |

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

### `sensor.<device>_calendar_agenda` (`CalendarField`, `6` total fields)

Upcoming calendar agenda events and next-event headline.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `summary` | `CalendarField::SUMMARY` | `records` | 0x1E-joined time|title|location|all_day agenda records (up to 5; time is a local label such as TODAY 14:30, TOM ALL DAY or NOV 20 08:05; all_day 1 or 0) |
| `4` | `count` | `CalendarField::EVENT_COUNT` | `u16` | Number of upcoming agenda events |
| `5` | `next_event` | `CalendarField::NEXT_EVENT` | `string` | Headline summary of the next upcoming event |

## 3. ESPHome Device Actions (`esphome.<node>_<action>`)

| Action | Variables | Description |
| :--- | :--- | :--- |
| `latter_ping` | *(none)* | No-op discovery marker action announcing Latter Protocol v3 compatibility and hardware manifest |
| `latter_push_v3` | `domain: string`, `payload: string` | Push a revision-guarded Protocol v3 domain payload directly to the bound Device over the ESPHome Native API |
| `latter_set_beacon_uuid` | `uuid: string` | Provision the System's random 128-bit BLE iBeacon UUID into NVS and restart advertising if active |
| `latter_factory_reset` | *(none)* | Reset to defaults: erases every saved setting (the whole NVS partition, including the Wi-Fi driver's cached network data and RF calibration) and reboots. Compiled-in Wi-Fi credentials and the API key are kept, so the device rejoins Home Assistant unchanged |
| `ui_input` | `action: string`, `delta: int` | Remote navigation input: action home|back|select|next|prev|left|right|up|down|cw|ccw (ok/click alias select); delta overrides the step for next/prev-style actions (0 = derive from action) |
| `set_browse_slots` | `contract: int`, `req_id: int`, `mode: string`, `page: int`, `total_pages: int`, `slots: string` | Push paginated media/speaker/search browse slots to the device |
| `set_area_slots` | `contract: int`, `req_id: int`, `mode: string`, `page: int`, `total_pages: int`, `slots: string` | Push paginated Area/Site entity control slots to the device |
| `show_notification` | `title: string`, `message: string`, `priority: string`, `duration: int`, `chime: bool`, `camera_url: string`, `alert_id: int` | Wake display, show an on-screen alert banner (priority normal|warning|critical, duration 3-60 s, default 10), optionally play the embedded WAV chime and show a camera snapshot; alert_id > 0 deduplicates repeats |

## 4. Outbound Home Assistant Services (`latter.<service>`)

| Service | Canonical Fields | Description |
| :--- | :--- | :--- |
| `latter.device_hello` | `target_device`, `contract`, `boot_id`, `manifest` | Device-initiated handshake sent when Home Assistant connects to the ESPHome API |
| `latter.music_browse` | `target_device`, `req_id`, `mode`, `page`, `page_size`, `speaker`, `last_speaker`, `search`, `album_sort`, `artist_album_sort`, `podcast_sort` | Browse speakers, favorites, playlists, artists, albums, tracks, radio, or search |
| `latter.set_active_speaker` | `target_device`, `entity_id`, `transfer_queue` | Select the active media_player target for a Latter device |
| `latter.media_command` | `target_device`, `command`, `entity_id`, `seek_position`, `media_id`, `media_type`, `enqueue`, `group_members` | Execute transport, seek, queue, speaker grouping, or play_media command on the active speaker |
| `latter.media_volume` | `target_device`, `entity_id`, `action`, `volume_level` | Adjust or set volume on the active speaker or volume override target |
| `latter.area_browse` | `target_device`, `req_id`, `mode`, `area_id`, `domain_filter`, `page`, `page_size` | Browse Areas, Area domains, or paginated controllable entities |
| `latter.area_action` | `target_device`, `entity_id`, `area_id`, `action`, `level`, `code` | Execute an explicit domain action on an Area or specific entity |
| `latter.set_active_area` | `target_device`, `area_id` | Set the active Area context for a Latter device (an on-device Area pick). Whether media follows is the Device's Latter setting |
| `latter.remote_action` | `target_device`, `slot`, `action`, `command` | Send a TV/AV remote command or select active remote slot |
| `latter.calendar_fetch` | `target_device`, `req_id`, `days`, `page`, `page_size` | Refresh upcoming calendar agenda events for a Latter device |
| `latter.calendar_create_event` | `target_device`, `calendar_entity`, `summary`, `start_date_time`, `end_date_time`, `in_minutes`, `duration_minutes`, `description` | Create a new event on the primary configured calendar |
| `latter.intercom_broadcast` | `target_device`, `message`, `media_url`, `target_speakers`, `tts_entity` | Broadcast a voice announcement to chosen speakers, the speakers in occupied Areas (target_device: occupied_areas), or every speaker |
| `latter.doorbell_intercom` | `target_device`, `action`, `camera_entity`, `camera_index`, `message`, `noise_reduction`, `code` | Doorbell talkback and requests. action: talk_message (push-to-talk speech transcribed on the device, spoken at the door by Home Assistant), quick_reply, listen_start|listen_stop (door audio played on the device), call_target (rings other displays with a notification; no display-to-display audio), unlock_door (unlock request to Home Assistant), set_noise_reduction. noise_reduction: strong|light|off, the noise filter preset Home Assistant applies to door audio (FFmpeg). No echo cancellation; camera is auto-refreshing snapshots |
| `latter.save_custom_color` | `target_device`, `hex_color`, `name` | Save a custom RGB accent color from the on-device color wheel |
| `latter.set_dock_apps` | `target_device`, `dock_apps` | Save the Home dock apps chosen on the device (Home settings) as the Device's Latter dock setting |
