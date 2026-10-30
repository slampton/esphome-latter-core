# Latter Wire & API Contract Specification

This document specifies the bidirectional contract between the **Latter Home Assistant Middleware (`ha-latter`)** and any **ESPHome Device** so third-party developers can build custom LVGL, canvas, or headless ESPHome nodes that integrate natively with Latter.

---

## 1. Auto-Discovery Handshake

Latter automatically discovers an ESPHome device when either:
1. The device registers any of the following ESPHome API actions (`esphome.<device_slug>_*`):
   - `set_browse_slots`
   - `set_area_slots`
   - `show_notification`
   - `latter_ping` (used by lightweight 1-line mixins)
2. Or the ESPHome project metadata (`esphome: project: name:`) contains `latter`, `pocket-assistant`, or `wrist-assist`.

---

## 2. Inbound State Push Sensors (`sensor.<device_slug>_*`)

When a device is onboarded, Latter creates 5 push sensors in Home Assistant. An ESPHome device can subscribe to any of these attributes via `platform: homeassistant`:

| Sensor Entity ID | State Value | Key Attributes |
| :--- | :--- | :--- |
| `sensor.<slug>_active_media` | `playing`, `paused`, `idle`, `off` | `title`, `artist`, `album`, `duration`, `position`, `volume` (`0.0..1.0`), `media_type`, `art_url`, `active_speaker`, `local_speaker` |
| `sensor.<slug>_area_context` | Active Area Name (e.g. `Living Room`) | `area_id`, `area_temp`, `area_humidity`, `available_domains`, `alarm_entity`, `alarm_state`, `thermostat_state`, `thermostat_temp`, `lock_state`, `garage_state`, `vacuum_state`, `wallpaper_url`, `camera_snapshot_url`, `theme_color_hex`, `custom_bindings_summary`, `alert_id`, `alert_title`, `alert_message`, `alert_priority`, `alert_camera_url` |
| `sensor.<slug>_weather_forecast` | `MON:72:55:sunny;TUE:68:52:partlycloudy;...` | `condition`, `temperature`, `humidity`, `wind_speed`, `wind_bearing`, `high`, `low` |
| `sensor.<slug>_calendar_agenda` | `TODAY 14:30\|Dentist;TOM 09:00\|Flight;...` | `event_count`, `events` (structured list with geocoded `map_url`), `calendars` |
| `sensor.<slug>_remote_config` | Active Remote Name | `active_slot` (`1..4`), `slots_summary` (`1:Name:remote_ent:mp_ent:state;...`) |

---

## 3. Inbound Paginated Slot Callbacks (`api: actions:`)

### A. `set_browse_slots` (5-Slot Lists: Music, Area Picker, Calendar, Lyrics/Bio)
Latter inspects your action's schema capacity (`1..20` slots; standard is 5) and pushes paginated items:
- `mode` (`string`), `page` (`int`), `total_pages` (`int`), `has_prev` (`bool`), `has_next` (`bool`)
- `s1_name`..`s5_name` (`string`), `s1_id`..`s5_id` (`string`), `s1_type`..`s5_type` (`string`), `s1_active`..`s5_active` (`bool`)

### B. `set_area_slots` (Interactive Area Control Cards with Slider Levels)
Same as `set_browse_slots`, plus `s1_level`..`s5_level` (`int`, `0..100` for dimmers/covers/fans or `-1` for non-slider entities).

### C. `show_notification` (Toast / Camera Peephole Takeover)
- `title` (`string`), `message` (`string`), `priority` (`string`: `normal` | `warning` | `critical`), `duration` (`int`), `chime` (`bool`), `camera_url` (`string`)

---

## 4. Outbound Services (`homeassistant.action:`)

Your ESPHome device can call these Latter services in Home Assistant:
- `latter.music_browse`: Request a page of speakers, library items, queue, lyrics, or bio (`target_device`, `mode`, `page`, `page_size`, `search`).
- `latter.media_command`: Execute playback transport (`target_device`, `command`: `toggle` | `play` | `pause` | `next` | `previous` | `seek` | `clear_queue`, `seek_position`).
- `latter.media_volume`: Adjust speaker/AVR volume (`target_device`, `action`: `up` | `down` | `mute` | `set`, `volume_level`).
- `latter.set_active_speaker`: Switch target speaker or transfer queue (`target_device`, `entity_id`, `transfer_queue`).
- `latter.area_browse`: Request area entities or areas list (`target_device`, `mode`: `entities` | `areas`, `area_id`, `domain_filter`, `page`, `page_size`).
- `latter.area_action`: Execute an area or Whole-Home Hub action (`target_device`, `entity_id`, `action`: `toggle` | `set_level` | `set_color` | `set_kelvin` | `arm_home` | `arm_away` | `disarm`, `level`, `code`).
- `latter.set_active_area`: Switch active area (`target_device`, `area`, `handoff_media`).
- `latter.remote_action`: Send TV/AVR remote command (`target_device`, `slot`, `action`, `command`).
- `latter.trigger_binding`: Fire a mapped macro slot (`target_device`, `slot`).
- `latter.calendar_fetch` & `latter.calendar_create_event`: Browse or add calendar events.
- `latter.intercom_broadcast`: Broadcast TTS across Music Assistant speakers (`message`).
- `latter.save_custom_color`: Persist a custom `#RRGGBB` theme color to the System Color Library.
