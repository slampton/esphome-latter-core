# Latter Client–Server Protocol & Device Contract (`v3`)

This document specifies the canonical bi-directional contract between the **Latter Home Assistant Middleware** (`slampton/ha-latter`) and the **Latter Universal ESPHome SDK** (`slampton/esphome-latter-core`).

---

## 1. Architectural Principles

1. **Atomic State Revisioning (`Protocol v3`)**:
   - Every per-device state domain (`area_context`, `active_media`, `remote_config`, `weather`, `calendar_agenda`) exposes a monotonic `rev` integer persisted across Home Assistant restarts (`.storage/latter.runtime.<entry_id>`) alongside a single atomic `payload` string attribute.
   - Multi-field updates arrive in a single native ESPHome state packet (`attribute: payload`), eliminating torn multi-attribute states while preserving individual attributes for Home Assistant templates and diagnostics.
2. **Dynamic Capability Negotiation**:
   - Devices self-report their screen geometry, capability bitmask, and slot capacity via `esphome.latter_hardware_manifest` on boot and upon `esphome.latter_ping`.
   - The server dynamically sizes browse and area slot pushes (`slot_count`, default `16`, max `32`) rather than hardcoding fixed slot parameters.
3. **Standalone & Offline Resilience**:
   - Battery-backed and portable devices (`LATTER_IS_STATIONARY == 0`) disable `wifi.reboot_timeout` and `api.reboot_timeout` (`0s`) so local alarms, timers, games, and clocks continue uninterrupted when carried off-network or during Home Assistant restarts.
   - Stationary mains-powered displays (`LATTER_IS_STATIONARY == 1`) retain a `15min` `api.reboot_timeout` failsafe.
   - All devices configure `time: - platform: sntp` (`id: sntp_time`) as a fallback when `homeassistant` time (`id: ha_time`) is not yet synchronized.

---

## 2. Delimiter & Wire Encoding (`Protocol v3`)

All `v3` atomic payloads and slot lists use ASCII control characters that never collide with UTF-8 user text, artist/track titles, or entity IDs:
- **Unit Separator (`\x1f`, ASCII `31`)**: Field separator within a record or payload.
- **Record Separator (`\x1e`, ASCII `30`)**: Item/record separator within a collection (e.g., browse/area slots).

Every atomic sensor `payload` begins with the header:
```text
v3\x1f<rev>\x1f<field_0>\x1f<field_1>...
```
On the device (`include/latter_state.h`), zero-allocation `std::string_view` tokenizers (`next_unit_token`, `next_record_token`) parse the payload and reject out-of-order revisions (`rev < current_rev`).

---

## 3. Device-to-Server Events (`esphome.*` Home Assistant Events)

### 3.1 `esphome.latter_hardware_manifest`
Fired automatically at boot (`api.on_client_connected`) and in response to `latter_ping`.
| Field | Type | Description |
| :--- | :--- | :--- |
| `device` | `string` | Canonical device slug (e.g., `pocket_assistant`). |
| `contract_version` | `string` | Protocol version (`"3"`). |
| `board_id` | `string` | Board identifier (e.g., `waveshare_s3_18`, `waveshare_p4_4b`). |
| `screen_w` | `string` | Horizontal resolution in pixels. |
| `screen_h` | `string` | Vertical resolution in pixels. |
| `round` | `string` | `"1"` for circular displays, `"0"` for rectangular/square. |
| `imu` | `string` | `"1"` if IMU is present, `"0"` otherwise. |
| `battery` | `string` | `"1"` if battery-backed, `"0"` otherwise. |
| `stationary` | `string` | `"1"` if stationary mains-powered, `"0"` if portable/wearable. |
| `wearable` | `string` | `"1"` if wrist wearable, `"0"` otherwise. |
| `slot_count` | `string` | Maximum browse/area slots supported by the device (`"16"`). |

### 3.2 `esphome.latter_ping`
Fired on connection and whenever the device requests an immediate slot and state synchronization from the coordinator.
| Field | Type | Description |
| :--- | :--- | :--- |
| `device` | `string` | Canonical device slug. |
| `contract` | `string` | Protocol version (`"3"`). |

### 3.3 `esphome.ui_input`
Unified UI command bus emitted by touch, encoder, and hardware button interactions.
| Field | Type | Description |
| :--- | :--- | :--- |
| `device` | `string` | Canonical device slug. |
| `page` | `string` | Source subsystem (`"media"`, `"remote"`, `"area"`, `"browse"`, `"whole_home"`, `"intercom"`, `"doorbell"`, `"system"`). |
| `action` | `string` | Action verb (e.g., `"play_pause"`, `"set_area"`, `"toggle_slot"`, `"slider_set"`, `"browse_select"`). |
| `value` | `string` | Optional action argument (`slot` index, `entity_id`, percentage, etc.). |

---

## 4. Server-to-Device Native API Actions (`esphome.<slug>_*`)

### 4.1 `set_browse_slots` & `set_area_slots`
Pushes media/app browser items or area control slots to the device.
- **Contract v3 Signature**:
  - `contract` (`int`): Set to `3` for Record/Unit-separated single-string transport (`0` for legacy positional `s0`–`s15` fallback).
  - `title` (`string`): Header title for the current view/category.
  - `page` (`int`): Current page index (`1`-based).
  - `slots` (`string`): Up to `slot_count` records separated by `\x1e`, where each record encodes `label\x1fmeta\x1fstate\x1fvalue` (`set_area_slots`) or `label\x1fid_or_meta` (`set_browse_slots`).

### 4.2 `set_timer` & `cancel_timer`
- `set_timer(seconds: int, label: string)`: Starts a synchronized hardware countdown timer on the device.
- `cancel_timer()`: Stops any active countdown or ringing timer on the device.

### 4.3 `show_notification`
- `show_notification(title: string, message: string, duration_s: int, critical: bool)`: Wakes the display, renders the full-screen or toast notification overlay, and optionally triggers the critical alert chime/border.

---

## 5. Server-to-Device Sensor Schemas (`sensor.latter_<slug>_*`)

Each coordinator sensor exposes both human-readable attributes and the atomic `v3` `payload` attribute:

1. **`sensor.latter_<slug>_area_context`**:
   - `payload`: `v3\x1f<rev>\x1f<area_id>\x1f<area_name>\x1f<temp>\x1f<humidity>\x1f<lights_on>\x1f<lights_total>\x1f<climate_state>\x1f<climate_target>\x1f<occupied>\x1f<doorbell_camera_url>\x1f<doorbell_label>\x1f<locks_state>\x1f<garage_state>\x1f<people_home>`
   - Tri-state security values (`locks_state`, `garage_state`): `"locked"` / `"unlocked"` / `"unavailable"` and `"closed"` / `"open"` / `"unavailable"`.
2. **`sensor.latter_<slug>_active_media`**:
   - `payload`: `v3\x1f<rev>\x1f<title>\x1f<artist>\x1f<album>\x1f<app_name>\x1f<volume_pct>\x1f<muted>\x1f<shuffle>\x1f<repeat>\x1f<art_url>\x1f<position>\x1f<duration>\x1f<updated_at>`
3. **`sensor.latter_<slug>_remote_config`**:
   - `payload`: `v3\x1f<rev>\x1f<target_entity>\x1f<friendly_name>\x1f<mode>\x1f<source>\x1f<source_list>\x1f<power_state>\x1f<supported_features>`
4. **`sensor.latter_<slug>_weather`**:
   - `payload`: `v3\x1f<rev>\x1f<condition>\x1f<temperature>\x1f<temp_high>\x1f<temp_low>\x1f<humidity>\x1f<wind_speed>\x1f<uv_index>\x1f<aqi>\x1f<alert_title>\x1f<forecast_summary>`
5. **`sensor.latter_<slug>_calendar_agenda`**:
   - `payload`: `v3\x1f<rev>\x1f<next_title>\x1f<next_start>\x1f<next_location>\x1f<today_count>\x1f<agenda_summary>`
