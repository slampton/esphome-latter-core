# Device Protocol

This is the contract between an ESPHome device and the [Latter integration](https://github.com/slampton/ha-latter). Implement it and your device gets Latter's data, whether or not you use the rest of this SDK.

The protocol has four parts:
1. **Discovery.** How Latter decides a device belongs to it.
2. **Hardware manifest.** What the device tells Latter about itself.
3. **Inbound.** API actions Latter calls on the device, and sensors the device subscribes to.
4. **Outbound.** `latter.*` services the device calls. See the [services reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/services.md).

The reference implementation is [`modules/latter_client.yaml`](../modules/latter_client.yaml). The smallest working implementation is [`modules/base.yaml`](../modules/base.yaml) plus [`modules/area_control.yaml`](../modules/area_control.yaml).

> ESPHome exposes an API action named `foo` on a device named `my-panel` as the Home Assistant service `esphome.my_panel_foo`. Below, `<slug>` is the ESPHome node name, lowercase with underscores. Latter binds each Device to its ESPHome device ID and MAC address, and resolves `esphome.<slug>_*` from that device's ESPHome node name, never from names typed into Home Assistant.

---

## 1. Discovery

An ESPHome device counts as a Latter device if it has at least one ESPHome entity **and** meets **any** of these conditions:
- It registers at least one of the actions `latter_ping`, `set_browse_slots`, `set_area_slots`, or `show_notification`.
- Its model or firmware version string contains `latter`.
- The device has an `update` entity whose ID ends in `_project_firmware`.

Latter rescans after Home Assistant starts, on any device-registry change (immediately, and again after a 5 s debounce), and whenever ESPHome registers one of the actions above. Matching devices raise a **Discovered: Latter** config flow.

**Minimum to be discovered.** Latter Core's `base.yaml` includes this action:

```yaml
api:
  actions:
    - action: latter_ping
      id: latter_ping_action   # fixed id, so packages that both declare it merge without conflict
      then:
        - script.execute: send_device_hello
```

**The device must be allowed to call services.** In the ESPHome integration's options for the device, enable **Allow the device to perform Home Assistant actions**. Without it, every outbound call in [section 4](#4-outbound-calls) is silently blocked.

---

## 2. Hardware manifest

The device describes itself twice. Both are built from the board's `LATTER_*` flags in [`ui_layout.h`](../include/ui_layout.h):

| Where | Built by | Content |
| :--- | :--- | :--- |
| `manifest` field of `latter.device_hello`, sent on every Home Assistant API connection | `latter_ui::build_hardware_manifest_json()` | The full manifest, including the boot health keys below |
| Text sensor **`System: Hardware Manifest`** (`sensor.<slug>_system_hardware_manifest`), published at boot and every 300 s | `latter_ui::build_hardware_manifest_state_json()` | A compact copy (≤ 230 bytes, inside Home Assistant's 255-character state limit). No `audio` details, `beacon` block or health keys; the beacon minor is sent as `minor`. |

Latter reads the sensor of the bound ESPHome device and overlays the latest `device_hello` manifest on it, so the hello wins wherever both have a key. Example hello manifest:

```json
{"v":2,"contract":3,"fw":"2.2.1","board":"waveshare_s3_206","boot_id":123456789,
 "w":410,"h":502,"shape":"square",
 "display":1,"touch":1,"audio":{"out_rate":48000,"out_ch":1,"out_bits":16,"in_rate":16000},
 "mic":1,"spk":1,"imu":1,"battery":1,"stationary":0,"wearable":1,"ips":0,
 "chip":"ESP32-S3","psram_mb":8,"slot_count":5,"max_payload_bytes":2048,
 "beacon":{"uuid":"4C415454-4552-434F-5245-000000000220","major":1,"minor":3},
 "sdk":2026110253,"reset":"power-on","crash":0,"rollback":0}
```

| Key | Used by Latter for |
| :--- | :--- |
| `w`, `h` | The size it renders images at for this device |
| `shape` | `circle` adds a circular mask to images. Anything else is treated as rectangular. |
| `stationary`, `wearable` | Roaming. A device is roaming when `stationary` is `0` or `wearable` is `1`; a missing value means not roaming. Only roaming devices can use Voice Follows Me (moving the device's Home Assistant Area) and get the *No Area-assigned Bluetooth proxies* repair. At setup, `stationary: 1` also turns Bluetooth Area tracking off and pre-selects the device's current Area. |
| `display` | `0` marks a headless node: it gets no doorbell popups, isn't a target of `latter.notify` broadcasts, and is left out of remote assignments. A missing value counts as having a display. |
| `beacon` (`uuid`, `major`, `minor`), or `minor` on the sensor | Matches this device's iBeacon advertisements for Bluetooth Area tracking |
| `audio` (`out_rate`, `out_ch`) | The sample rate and channel count of the doorbell listen stream |
| `imu` | Studio shows the motion controls |
| `board` | Studio's model label |
| `sdk`, `reset`, `crash`, `rollback` | Boot health repairs (below). Hello manifest only. |
| `slot_count`, `contract`, `fw`, `boot_id`, `chip`, `psram_mb`, `ips`, `touch`, `mic`, `spk`, `battery`, `max_payload_bytes`, `v` | Reported in diagnostics. Not used for behavior today. The `boot_id` that counts is the `boot_id` field of `latter.device_hello`. |

There is no `profile` key. Latter treats a device by these traits; BLE phones, watches and tags are a separate **BLE tag** entry in Latter and send no manifest.

### Boot health and repairs

Every hello reports how the device last started:

| Key | Values |
| :--- | :--- |
| `sdk` | `LATTER_SDK_REV` of the running build |
| `reset` | `power-on`, `external`, `software`, `deep-sleep`, `usb`, `panic`, `int-wdt`, `task-wdt`, `wdt`, `stall`, `brownout`, or `unknown` |
| `crash` | `1` while ESPHome's crash handler holds a crash record. Its decoded backtrace is logged to every log client until the device restarts. |
| `rollback` | `1` while the running firmware replaced an update that failed to boot. Stays `1` until a successful OTA. |

`stall` means the main-loop stall guard restarted the device: one ESPHome operation ran for 30 s or longer outside an OTA (the task watchdog misses a loop that keeps feeding it). The boot summary logged to every API client names the operation (`[PROF:STALL] previous boot aborted: main loop blocked <ms> in <component/source>`), and it also logs the longest block of 2 s or more seen during the current boot.

Latter evaluates these once per boot (per `boot_id`, also across Home Assistant restarts) and raises a Home Assistant repair per Device:

| Condition | Repair | Cleared when |
| :--- | :--- | :--- |
| `reset` is `panic`, `int-wdt`, `task-wdt`, `wdt` or `stall` | **Device restarted after a crash (reason)** (warning). Open the device's logs in ESPHome Device Builder before it restarts again; the decoded backtrace is shown there, and decoding needs the build the device is running. Shows the SDK rev and the boot time. | A later boot reports a clean reset (`power-on`, `external`, `software`, `deep-sleep`) |
| `reset` is `brownout` | **Device restarted after a brownout** (warning): the supply voltage or battery sagged | Same as above |
| `rollback` is `1` | **Firmware update failed on Device** (error): the update didn't boot and the device went back to the previous firmware | A hello reports `rollback: 0` |

`unknown` or a missing `reset` leaves the crash and brownout repairs as they are. Removing the Device removes its repairs. Latter's diagnostics download includes the last reported `sdk`, `reset`, `crash` and `rollback` per Device.

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
| `alert_id` | int | Monotonically incrementing notification ID for deduplication |

#### `latter_factory_reset`

No variables. Latter calls it from Studio's **Reset to defaults** and the `latter.reset_device` service when the device settings box is checked. The reference firmware turns the panel off (same hand-off as **Restart Device**), erases the whole NVS partition and reboots. That clears every saved setting plus the Wi-Fi driver's cached network data and RF calibration. Wi-Fi credentials and the API encryption key are compiled into the firmware, so the device rejoins Home Assistant with the same identity and its Latter binding is kept. Settings come back at their defaults, including the board's default power profile.

Latter also publishes every notification on `sensor.<slug>_area_context` inside its Protocol v3 `payload` attribute (`alert_id`, `alert_title`, `alert_message`, `alert_priority`, `alert_duration`, `alert_chime`, `alert_camera_url`). A device can use either path. The reference firmware watches both and deduplicates by `alert_id`.

### 3b. Optional actions in the reference firmware

Latter doesn't call these. They're there for automations and custom hardware input.

| Action | Variables | Behavior |
| :--- | :--- | :--- |
| `ui_input` | `action: string`, `delta: int` | Moves GUI focus. `action` is one of `home`, `back`, `select`, `next`, `prev`. |

### 3c. Sensors to subscribe to

Subscribe with `platform: homeassistant`. Each of the 5 Latter domain sensors (`area_context`, `active_media`, `remote_config`, `weather_forecast`, `calendar_agenda`) exposes a single `payload` attribute packed with `\x1f` (`US`, `0x1F`) delimiters and prefixed by `v3\x1f<epoch>\x1f<rev>\x1f...` (defined in [`protocol/latter_v3.json`](../protocol/latter_v3.json) and generated into [`DEVICE_CONTRACT.md`](../DEVICE_CONTRACT.md) and [`include/latter_protocol.h`](../include/latter_protocol.h)):

| Sensor | Inbound Attribute | Parsed by (`include/latter_state.h`) |
| :--- | :--- | :--- |
| `sensor.<slug>_area_context` | `payload` | `latter_ui::ingest_area_context_payload()` (area ID/name, available domains, dock apps, activity card settings, signed `wallpaper_url`, cameras summary, intercom targets, whole-home entities, and `alert_*` fields) |
| `sensor.<slug>_active_media` | `payload` | `latter_ui::ingest_active_media_payload()` (player ID/name, state, title, artist, album, duration, timestamped position, volume, media type, supported features, signed `art_url`, speaker routing) |
| `sensor.<slug>_remote_config` | `payload` | `latter_ui::ingest_remote_config_payload()` (active slot, configured count, `slots_summary` records) |
| `sensor.<slug>_weather_forecast` | `payload` | `latter_ui::ingest_weather_payload()` (condition, temperature, humidity, wind speed/bearing, today's high/low, 5-day `forecast_5d` records) |
| `sensor.<slug>_calendar_agenda` | `payload` | `latter_ui::ingest_calendar_payload()` (`summary` agenda records, upcoming event `count`, `next_event` headline) |

Image URLs (`art_url`, `wallpaper_url`, `camera_snapshot_url`, and `alert_camera_url`) embedded in each domain's `payload` (plus per-event `map_url` subfields pushed via `set_browse_slots` for `calendar_agenda`) are already HMAC-signed with day-bucketed deterministic tokens scoped to the resource and render dimensions (`w`×`h`, up to `720×720` for wallpapers/cameras/maps and `480×480` for album art). Fetch them with ESPHome's `online_image`. → [HTTP API](https://github.com/slampton/ha-latter/blob/main/docs/reference/http-api.md)

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
| `latter.device_hello` | Handshake sent when Home Assistant connects to the ESPHome API (and on `latter_ping`) |
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
| `latter.set_dock_apps` | Save the Home dock apps chosen in on-device Home settings |

The reference firmware also calls `media_player.join` and `media_player.unjoin` directly for speaker grouping.

Every field is documented in the [services reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/services.md).
