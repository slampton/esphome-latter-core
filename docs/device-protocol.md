# Device Protocol

This is the contract between an ESPHome device and the [Latter integration](https://github.com/slampton/ha-latter). Implement it and your device gets Latter's data, whether or not you use the rest of this SDK.

The protocol has four parts:
1. **Discovery.** How Latter decides a device belongs to it.
2. **Hardware manifest.** What the device tells Latter about itself.
3. **Inbound.** API actions Latter calls on the device, and sensors the device subscribes to.
4. **Outbound.** `latter.*` services the device calls. See the [services reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/services.md).

The reference implementation is [`modules/latter_client.yaml`](../modules/latter_client.yaml). The smallest working implementation is [`modules/base.yaml`](../modules/base.yaml) plus [`modules/area_control.yaml`](../modules/area_control.yaml).

> ESPHome exposes an API action named `foo` on a device named `my-panel` as the Home Assistant service `esphome.my_panel_foo`. Below, `<slug>` is the device's name in Home Assistant, lowercase with underscores.

---

## 1. Discovery

An ESPHome device counts as a Latter device if it has at least one ESPHome entity **and** meets **any** of these conditions:
- It registers at least one of the actions `latter_ping`, `set_browse_slots`, `set_area_slots`, or `show_notification`.
- Its model or firmware version string contains `latter`.
- An entity named `update.<slug>_project_firmware` exists.

Latter rescans after Home Assistant starts, on any device-registry change (immediately, and again after a 5 s debounce), and whenever ESPHome registers one of the actions above. Matching devices raise a **Discovered: Latter** config flow.

**Minimum to be discovered.** All mixins include this action:

```yaml
api:
  actions:
    - action: latter_ping
      id: latter_ping_action   # fixed id, so multiple packages merge without conflict
      then:
        - logger.log: "Latter companion connected"
```

**The device must be allowed to call services.** In the ESPHome integration's options for the device, enable **Allow the device to perform Home Assistant actions**. Without it, every outbound call in [section 4](#4-outbound-calls) is silently blocked.

---

## 2. Hardware manifest

Publish a template text sensor named **`System: Hardware Manifest`**, which becomes `sensor.<slug>_system_hardware_manifest`, with a JSON value:

```json
{"w":480,"h":480,"shape":"square","profile":"mobile_display",
 "display":1,"touch":1,"audio":1,"mic":1,"spk":1,"imu":1,"battery":1,
 "stationary":0,"wearable":0,"ips":0,"chip":"ESP32-S3","psram_mb":8}
```

| Key | Used by Latter for |
| :--- | :--- |
| `w`, `h` | The image size it renders for this device (64–1920) |
| `shape` | `circle` adds a circular mask to images. Anything else is treated as rectangular. |
| `profile` | `headless_node`, `mobile_display`, `stationary_display`, or `wearable_display`. Applied when the config entry's profile is unset or is `mobile_display`. |
| `slot_count` | *Optional.* The default page size when the slot action's schema can't be inspected |
| Everything else | Reported in Studio. Not used for behavior today. |

`latter_ui::build_hardware_manifest_json()` in [`ui_layout.h`](../include/ui_layout.h) builds this from the `LATTER_*` flags. The universal firmware publishes it at boot and every 300 s.

---

## 3. Inbound

### 3a. Actions Latter calls

#### `set_browse_slots` and `set_area_slots`

Latter pushes paginated lists through these: music library pages, area pickers, calendar pages, lyrics, and area entity cards. `set_area_slots` is the same as `set_browse_slots` plus a slider level per slot. If a device has only `set_browse_slots`, Latter uses it for everything and leaves out levels.

Both actions declare the 5-variable Protocol v3 packed schema:

| Variable | Type | Notes |
| :--- | :--- | :--- |
| `contract` | int | Always `3` for Protocol v3 |
| `mode` | string | Which list this is. See below. |
| `page` | int | 1-based page number |
| `total_pages` | int | Total number of pages |
| `slots` | string | Packed slot records separated by `\x1e`, with fields separated by `\x1f` |

In `slots`, records are separated by `\x1e` and fields by `\x1f`:

```text
name \x1f id \x1f type \x1f active(0|1) \x1f level(-1..100) \x1e name \x1f id ...
```

The parser is `latter_ui::parse_v3_slots_payload()` in [`ui_core.h`](../include/ui_core.h).

**`mode` values the reference firmware routes on:**

| `mode` | Routed to |
| :--- | :--- |
| `areas`, `area_areas` | Area picker |
| `entities`, `home` | Area Control cards |
| `calendar_agenda` | Calendar |
| Anything else | Music slots (`speakers`, `favorites`, `artist_albums`, `lyrics`, and so on) |

#### `show_notification`

| Variable | Type | Notes |
| :--- | :--- | :--- |
| `title` | string | Up to 48 characters |
| `message` | string | Up to 160 characters |
| `priority` | string | `normal`, `warning`, or `critical` |
| `duration` | int | Seconds |
| `chime` | bool | |
| `camera_url` | string | A signed baseline-JPEG snapshot URL, or `none` |

Latter also publishes every notification on `sensor.<slug>_area_context` through the `alert_*` attributes, with an incrementing `alert_id`. A device can use either path. The reference firmware watches both and ignores duplicates that arrive within 1.5 s of each other.

### 3b. Optional actions in the reference firmware

Latter doesn't call these. They're there for automations and other tools.

| Action | Variables | Behavior |
| :--- | :--- | :--- |
| `set_timer` | `seconds: int` | Starts or updates a timer and opens the Timers tool |
| `cancel_timer` | — | Stops all timers |
| `ui_input` | `action: string`, `delta: int` | Moves GUI focus. `action` is one of `home`, `back`, `select`, `next`, `prev`. |

### 3c. Sensors to subscribe to

Subscribe with `platform: homeassistant`. Attribute names are listed in the [entities reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/entities.md). These are the formats that need parsing:

| Source | Format |
| :--- | :--- |
| `sensor.<slug>_weather_forecast` state | `DAY:HIGH:LOW:COND;` × 5, for example `MON:72:55:sunny;TUE:68:52:partlycloudy;…` |
| `sensor.<slug>_calendar_agenda` state | `LABEL\|summary;…`, up to 5 entries. `LABEL` is one of `TODAY`, `TOM`, a weekday (`FRI`), or a date (`NOV 08`). |
| `remote_config` → `slots_summary` | `N:Name:remote_entity:media_player_entity:state;…`, up to 10 entries. `NONE` if there are none. |
| `area_context` → `cameras_summary` | `entity_id\|Name\|signed_snapshot_url;…`, up to 10 entries. `NONE` if there are none. |
| `area_context` → `intercom_targets_summary` | `target\|Label;…`, up to 10 entries. The first is always `all\|Call All Areas`. Other entries are device slugs or `sip:` URIs. |
| `area_context` → `available_domains`, `dock_apps` | Comma-separated lists |
| `area_context` → `alert_id` | Integer that increments on every notification |
| `active_media` → `position`, `duration` | Seconds. Position is pushed every 15 s while playing. Interpolate locally between updates. |

Image URLs, such as `art_url`, `wallpaper_url`, and `camera_snapshot_url`, are already signed. They point at Home Assistant's numeric LAN IP and return baseline JPEG at your manifest's `w`×`h`. Fetch them with ESPHome's `online_image`. → [HTTP API](https://github.com/slampton/ha-latter/blob/main/docs/reference/http-api.md)

---

## 4. Outbound calls

The device calls `latter.*` services with `homeassistant.action`. Always pass `target_device: ${name}` so Latter knows which device is calling:

```yaml
- homeassistant.action:
    action: latter.music_browse
    data:
      target_device: ${name}
      mode: favorites
      page: "1"
      page_size: "5"
```

| Service | Typical use from a device |
| :--- | :--- |
| `latter.music_browse` | Request a page. Latter replies with `set_browse_slots`. |
| `latter.area_browse` | Request areas or entities. Latter replies with `set_area_slots` or `set_browse_slots`. |
| `latter.area_action` | Toggle, set level, color, or color temperature; alarm, lock, garage, and vacuum actions |
| `latter.set_active_area` | The user picked an area |
| `latter.media_command`, `latter.media_volume` | Transport and volume |
| `latter.set_active_speaker` | Speaker picker, with optional queue transfer |
| `latter.remote_action` | Remote keys |
| `latter.calendar_fetch`, `latter.calendar_create_event` | Calendar page and quick-add |
| `latter.intercom_broadcast`, `latter.doorbell_intercom` | Intercom and doorbell |
| `latter.save_custom_color` | Color picker |

The reference firmware also calls `media_player.join` and `media_player.unjoin` directly for speaker grouping.

Every field is documented in the [services reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/services.md).
