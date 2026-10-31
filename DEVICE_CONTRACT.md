# Latter Protocol v3 Device Contract

> **Auto-Generated Reference**: Generated from [`protocol/latter_v3.json`](protocol/latter_v3.json) by [`tools/gen_protocol.py`](tools/gen_protocol.py). Do not edit by hand.

## 1. Overview & Transport Framing

- **Contract Version**: `3`
- **Field Separator**: `0x1F` (`\x1f`, ASCII Unit Separator `␟`)
- **Record Separator**: `0x1E` (`\x1e`, ASCII Record Separator `␞`) with pipe (`|`) subfields inside record lists
- **Inbound Channel**: Each domain publishes a single `payload` attribute on `sensor.<device>_<domain>` prefixed with `v3␟<epoch>␟<rev>`.
- **Revision Guard Semantics**:
  - `epoch` (`u32`, non-zero): Generated randomly when the Home Assistant coordinator starts. If `in_epoch != cur_epoch`, the device accepts the frame unconditionally and updates `cur_epoch` and `cur_rev`.
  - `rev` (`u32`, `>= 1`): Monotonic per-domain revision counter bumped only when that domain's body fields change. Frames with `rev == 0` are rejected; within the same `epoch`, frames with `in_rev <= cur_rev` are ignored.
  - On ESPHome API reconnect (`on_client_connected` from Home Assistant), the device calls `latter_ui::reset_all_v3_rev_guards()` and sends `latter.device_hello`.

### Header Fields (All Domains)

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `0` | `version` | `HEADER_VERSION` | `string` | Protocol version literal ('v3') |
| `1` | `epoch` | `HEADER_EPOCH` | `u32` | Coordinator session epoch (non-zero random u31; resets rev guards on change) |
| `2` | `rev` | `HEADER_REV` | `u32` | Per-domain monotonic revision counter (>= 1; bumped only when domain body changes) |

## 2. Domain Payloads

### `sensor.<device>_area_context` (`AreaCtxField`, `40` total fields)

Active Area state, climate/security/cover summaries, camera/intercom directories, and alert banner.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `area_id` | `AreaCtxField::AREA_ID` | `string` | Active Home Assistant Area ID |
| `4` | `area_name` | `AreaCtxField::AREA_NAME` | `string` | Friendly display name of the active Area |
| `5` | `site_id` | `AreaCtxField::SITE_ID` | `string` | Active Site ID |
| `6` | `vpn_enabled` | `AreaCtxField::VPN_ENABLED` | `bool` | WireGuard roaming VPN enabled flag (0 or 1) |
| `7` | `vpn_client_ip` | `AreaCtxField::VPN_CLIENT_IP` | `string` | Assigned WireGuard client IP |
| `8` | `ble_closest_proxy` | `AreaCtxField::BLE_CLOSEST_PROXY` | `string` | Friendly name of closest Bluetooth proxy |
| `9` | `dock_apps` | `AreaCtxField::DOCK_APPS` | `csv` | Comma-separated Home Dock app IDs |
| `10` | `wallpaper_url` | `AreaCtxField::WALLPAPER_URL` | `url` | Signed HTTP URL for active wallpaper |
| `11` | `theme_color_hex` | `AreaCtxField::THEME_COLOR_HEX` | `string` | Active theme accent color (#RRGGBB) |
| `12` | `available_domains` | `AreaCtxField::AVAILABLE_DOMAINS` | `csv` | Comma-separated available Area domains |
| `13` | `area_temp` | `AreaCtxField::AREA_TEMP` | `string` | Formatted Area temperature |
| `14` | `area_humidity` | `AreaCtxField::AREA_HUMIDITY` | `string` | Formatted Area relative humidity |
| `15` | `lights_on` | `AreaCtxField::LIGHTS_ON` | `u16` | Count of active lights in Area |
| `16` | `covers_open` | `AreaCtxField::COVERS_OPEN` | `u16` | Count of open covers in Area |
| `17` | `alarm_entity` | `AreaCtxField::ALARM_ENTITY` | `entity_id` | Primary alarm control panel entity ID |
| `18` | `alarm_state` | `AreaCtxField::ALARM_STATE` | `string` | Primary alarm state |
| `19` | `thermostat_entity` | `AreaCtxField::THERMOSTAT_ENTITY` | `entity_id` | Primary climate entity ID |
| `20` | `thermostat_state` | `AreaCtxField::THERMOSTAT_STATE` | `string` | Primary climate state/HVAC mode |
| `21` | `thermostat_temp` | `AreaCtxField::THERMOSTAT_TEMP` | `f32` | Target thermostat setpoint temperature |
| `22` | `thermostat_current_temp` | `AreaCtxField::THERMOSTAT_CURRENT_TEMP` | `f32` | Current measured thermostat temperature |
| `23` | `lock_entity` | `AreaCtxField::LOCK_ENTITY` | `entity_id` | Primary lock entity ID |
| `24` | `lock_state` | `AreaCtxField::LOCK_STATE` | `string` | Primary lock state |
| `25` | `garage_entity` | `AreaCtxField::GARAGE_ENTITY` | `entity_id` | Primary garage cover entity ID |
| `26` | `garage_state` | `AreaCtxField::GARAGE_STATE` | `string` | Primary garage cover state |
| `27` | `vacuum_entity` | `AreaCtxField::VACUUM_ENTITY` | `entity_id` | Primary vacuum entity ID |
| `28` | `vacuum_state` | `AreaCtxField::VACUUM_STATE` | `string` | Primary vacuum state |
| `29` | `camera_snapshot_url` | `AreaCtxField::CAMERA_SNAPSHOT_URL` | `url` | Signed HTTP URL for primary camera snapshot |
| `30` | `cameras_summary` | `AreaCtxField::CAMERAS_SUMMARY` | `records` | 0x1E-joined entity_id|name|area_id|snapshot_url camera records |
| `31` | `doorbell_talk_state` | `AreaCtxField::DOORBELL_TALK_STATE` | `string` | Doorbell talkback state |
| `32` | `intercom_targets_summary` | `AreaCtxField::INTERCOM_TARGETS_SUMMARY` | `records` | 0x1E-joined target_id|label|kind intercom target records |
| `33` | `alert_id` | `AreaCtxField::ALERT_ID` | `u32` | Monotonic alert/notification identifier |
| `34` | `alert_title` | `AreaCtxField::ALERT_TITLE` | `string` | Alert banner title |
| `35` | `alert_message` | `AreaCtxField::ALERT_MESSAGE` | `string` | Alert banner message body |
| `36` | `alert_priority` | `AreaCtxField::ALERT_PRIORITY` | `string` | Alert priority level (info|normal|high|critical) |
| `37` | `alert_duration` | `AreaCtxField::ALERT_DURATION` | `u16` | Alert display duration in seconds |
| `38` | `alert_chime` | `AreaCtxField::ALERT_CHIME` | `string` | Alert RTTTL/chime preset name |
| `39` | `alert_camera_url` | `AreaCtxField::ALERT_CAMERA_URL` | `url` | Signed HTTP URL for alert camera snapshot |

### `sensor.<device>_active_media` (`MediaField`, `20` total fields)

Active media player state, metadata, timestamped playback progress, artwork URL, and speaker routing.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `state` | `MediaField::STATE` | `string` | Playback state (playing|paused|idle|off) |
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
| `5` | `slots_summary` | `RemoteField::SLOTS_SUMMARY` | `records` | 0x1E-joined index|label|entity_id|kind|area_id|mac|state remote records |

### `sensor.<device>_weather_forecast` (`WeatherField`, `11` total fields)

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
| `10` | `forecast_5d` | `WeatherField::FORECAST_5D` | `records` | 0x1E-joined day|cond|high|low 5-day forecast records |

### `sensor.<device>_calendar_agenda` (`CalendarField`, `6` total fields)

Upcoming calendar agenda events and next-event headline.

| Index | Field | C++ Enum | Type | Description |
| :---: | :--- | :--- | :---: | :--- |
| `3` | `summary` | `CalendarField::SUMMARY` | `records` | 0x1E-joined time|title|location|all_day agenda records |
| `4` | `count` | `CalendarField::COUNT` | `u16` | Number of upcoming agenda events |
| `5` | `next_event` | `CalendarField::NEXT_EVENT` | `string` | Headline summary of the next upcoming event |

## 3. ESPHome Device Actions (`esphome.<node>_<action>`)

| Action | Variables | Description |
| :--- | :--- | :--- |
| `latter_ping` | *(none)* | No-op discovery marker action announcing Latter Protocol v3 compatibility and hardware manifest |
| `ui_input` | `command: string` | Remote hardware navigation/input command (up|down|left|right|select|back|home|wake|sleep) |
| `set_browse_slots` | `contract: int`, `mode: string`, `page: int`, `total_pages: int`, `slots: string` | Push paginated media/speaker/search browse slots to the device |
| `set_area_slots` | `contract: int`, `mode: string`, `page: int`, `total_pages: int`, `slots: string` | Push paginated Area/Site entity control slots to the device |
| `show_notification` | `title: string`, `message: string`, `priority: string`, `duration_s: int`, `chime: string` | Wake display, show an on-screen alert banner, and optionally play an RTTTL chime or camera preview |

## 4. Outbound Home Assistant Services (`latter.<service>`)

| Service | Canonical Fields | Description |
| :--- | :--- | :--- |
| `latter.device_hello` | `target_device`, `contract`, `boot_id`, `manifest` | Device-initiated handshake sent when Home Assistant connects to the ESPHome API |
| `latter.music_browse` | `target_device`, `mode`, `page`, `page_size`, `speaker`, `last_speaker`, `search`, `album_sort`, `artist_album_sort`, `podcast_sort` | Browse speakers, favorites, playlists, artists, albums, tracks, radio, or search |
| `latter.set_active_speaker` | `target_device`, `entity_id`, `transfer_queue` | Select the active media_player target for a Latter device |
| `latter.media_command` | `target_device`, `command`, `entity_id`, `seek_position`, `media_id`, `media_type`, `enqueue` | Execute transport, seek, queue, or play_media command on the active speaker |
| `latter.media_volume` | `target_device`, `entity_id`, `action`, `volume_level` | Adjust or set volume on the active speaker or volume override target |
| `latter.area_browse` | `target_device`, `mode`, `area_id`, `domain_filter`, `page`, `page_size`, `handoff_media` | Browse Areas, Area domains, or paginated controllable entities |
| `latter.area_action` | `target_device`, `entity_id`, `area_id`, `action`, `level`, `code` | Execute an explicit domain action on an Area or specific entity |
| `latter.set_active_area` | `target_device`, `area_id`, `handoff_media` | Set the active Area context for a Latter device |
| `latter.remote_action` | `target_device`, `slot`, `action`, `command` | Send a TV/AV remote command or select active remote slot |
| `latter.calendar_fetch` | `target_device`, `days`, `page`, `page_size` | Refresh upcoming calendar agenda events for a Latter device |
| `latter.calendar_create_event` | `target_device`, `calendar_entity`, `summary`, `start_date_time`, `end_date_time`, `in_minutes`, `duration_minutes`, `description` | Create a new event on the primary configured calendar |
| `latter.intercom_broadcast` | `target_device`, `message`, `media_url`, `target_speakers`, `tts_entity` | Broadcast a voice announcement across all, occupied, follow_me, or targeted speakers |
| `latter.doorbell_intercom` | `target_device`, `action`, `camera_entity`, `camera_index`, `message`, `anc_mode` | Control two-way doorbell talkback or trigger explicit lock actions during a doorbell event |
| `latter.save_custom_color` | `target_device`, `hex_color`, `name` | Save a custom RGB accent color from the on-device color wheel |
| `latter.notify` | `target_device`, `title`, `message`, `priority`, `duration`, `chime`, `announce`, `camera_entity` | Deliver a visual banner, chime, optional TTS announcement, and optional camera snapshot |
