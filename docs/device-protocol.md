# Device Protocol

This is the contract between an ESPHome device and the [Latter integration](https://github.com/slampton/ha-latter). Implement it and your device gets Latter's data, whether or not you use the rest of this SDK.

The protocol has four parts:
1. **Discovery.** How Latter decides a device belongs to it.
2. **Hardware manifest.** What the device tells Latter about itself.
3. **Inbound.** Native API actions Latter calls on the bound ESPHome device (`latter_push_v3`, `set_browse_slots`, `set_area_slots`, `show_notification`, `latter_set_beacon_uuid`, `latter_factory_reset`).
4. **Outbound.** Zero-privilege diagnostic RPC frames published on **`System: RPC Outbox`** (`latter_rpc_outbox`). See the [services reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/services.md).

The reference implementation is [`modules/latter_client.yaml`](../modules/latter_client.yaml). The smallest working implementation is [`modules/base.yaml`](../modules/base.yaml) plus [`modules/area_control.yaml`](../modules/area_control.yaml).

> ESPHome exposes an API action named `foo` on a device named `my-panel` as the Home Assistant service `esphome.my_panel_foo`. Below, `<slug>` is the ESPHome node name, lowercase with underscores. Latter binds each Device to its ESPHome device ID and MAC address, and resolves both inbound `esphome.<slug>_*` actions and the outbound `text_sensor` outbox strictly from that bound ESPHome device—never from entity slugs or names typed into Home Assistant. **Devices never need ESPHome's *"Allow the device to perform Home Assistant actions"* permission.**

---

## 1. Discovery

An ESPHome device counts as a Latter device if it has at least one ESPHome entity **and** meets **any** of these conditions:
- It registers at least one of the actions `latter_ping`, `latter_push_v3`, `set_browse_slots`, `set_area_slots`, or `show_notification`.
- Its model or firmware version string contains `latter`.
- The device has an `update` entity whose ID ends in `_project_firmware`.

Latter rescans after Home Assistant starts, on any device-registry change (immediately, and again after a 5 s debounce), and whenever ESPHome registers one of the actions above. Matching devices raise a **Discovered: Latter** config flow.

**Minimum to be discovered.** Latter Core's `base.yaml` includes `latter_ping`, `latter_push_v3`, `System: Hardware Manifest`, and `System: RPC Outbox`:

```yaml
api:
  actions:
    - action: latter_ping
      id: latter_ping_action   # fixed id, so packages that both declare it merge without conflict
      then:
        - script.execute: send_device_hello
```

---

## 2. Hardware manifest

The device describes itself twice. Both are built from the board's `LATTER_*` flags in [`ui_layout.h`](../include/ui_layout.h):

| Where | Built by | Content |
| :--- | :--- | :--- |
| `manifest` field of `device_hello` (emitted on `System: RPC Outbox` on every Home Assistant connection and on `latter_ping`) | `latter_ui::build_hardware_manifest_json()` | The full manifest, including the boot health keys below |
| Text sensor **`System: Hardware Manifest`** (`text_sensor.<slug>_system_hardware_manifest`), published at boot and every 300 s | `latter_ui::build_hardware_manifest_state_json()` | A compact copy (≤ 230 bytes, inside Home Assistant's 255-character state limit). No `audio` details, `beacon` block or health keys; the beacon minor is sent as `minor`. |

Latter reads the `text_sensor` of the bound ESPHome device and overlays the latest `device_hello` manifest on it, so the hello wins wherever both have a key.

| Key | Used by Latter for |
| :--- | :--- |
| `w`, `h` | The size it renders images at for this device |
| `shape` | `circle` adds a circular mask to images. Anything else is treated as rectangular. |
| `stationary`, `wearable` | Roaming. A device is roaming when `stationary` is `0` or `wearable` is `1`; a missing value means not roaming. Only roaming devices can use Voice Follows Me (moving the device's Home Assistant Area) and get the *No Area-assigned Bluetooth proxies* repair. |
| `display` | `0` marks a headless node: it gets no doorbell popups, isn't a target of `latter.notify` broadcasts, and is left out of remote assignments. |
| `beacon` (`uuid`, `major`, `minor`), or `minor` on the sensor | Matches this device's iBeacon advertisements for Bluetooth Area tracking |
| `audio` (`out_rate`, `out_ch`) | The sample rate and channel count of the doorbell listen stream |
| `imu` | Studio shows the motion controls |
| `board` | Studio's model label |
| `sdk`, `reset`, `crash`, `rollback` | Boot health repairs (below). Hello manifest only. |

### Boot health and repairs

Every hello reports how the device last started (`sdk`, `reset`, `crash`, `rollback`), which Latter evaluates once per `boot_id` to raise or clear crash, brownout, and rollback Repairs in Home Assistant.

---

## 3. Inbound (`HA -> Device` Native API Actions)

### 3a. `latter_push_v3` (Protocol v3 Domain Push)

Latter pushes each of the 5 Protocol v3 domains (`area_context`, `active_media`, `remote_config`, `weather_forecast`, `calendar_agenda`) directly to the bound ESPHome device via `esphome.<slug>_latter_push_v3`:

| Variable | Type | Notes |
| :--- | :--- | :--- |
| `domain` | string | `area_context`, `active_media`, `remote_config`, `weather_forecast`, or `calendar_agenda` |
| `payload` | string | `\x1f`-packed (`US`, `0x1F`) Protocol v3 payload prefixed with `v3\x1f<epoch>\x1f<rev>\x1f...` (defined in [`protocol/latter_v3.json`](../protocol/latter_v3.json) and generated into [`DEVICE_CONTRACT.md`](../DEVICE_CONTRACT.md) and [`include/latter_protocol.h`](../include/latter_protocol.h)) |

- **`area_context`** includes `security_pin_policy` (`allow` | `require_pin` | `block`) alongside Area metadata, dock apps, activity settings, signed `wallpaper_url`, cameras, intercom targets, whole-home entities, and `alert_*` fields.
- **`weather_forecast`** includes `sun_state`, `sun_elevation`, `sun_azimuth`, `sun_next_rising`, `sun_next_setting`, and `moon_phase` alongside weather conditions and 5-day forecasts, so devices require zero `sun.sun` or `sensor.moon_phase` entity subscriptions.
- Image URLs (`art_url`, `wallpaper_url`, `camera_snapshot_url`, `alert_camera_url`, and per-event `map_url`) are HMAC-signed with rotatable 48-hour resource-scoped tokens.

### 3b. `set_browse_slots` and `set_area_slots`

Latter pushes paginated lists through these: music library pages, area pickers, calendar pages, lyrics, and area entity cards. Both actions declare the 6-variable Protocol v3 packed schema:

| Variable | Type | Notes |
| :--- | :--- | :--- |
| `contract` | int | Always `3` for Protocol v3 |
| `req_id` | int | Monotonic request correlation ID (`0` for unsolicited pushes); stale responses are discarded |
| `mode` | string | `areas`/`area_areas`, `entities`/`home`, `calendar_agenda`, or music modes (`speakers`, `favorites`, etc.) |
| `page` | int | 1-based page number |
| `total_pages` | int | Total number of pages |
| `slots` | string | Packed slot records separated by `\x1e`, with fields separated by `\x1f` |

### 3c. `show_notification`, `latter_set_beacon_uuid`, and `latter_factory_reset`

- **`show_notification`** (`title`, `message`, `priority`, `duration`, `chime`, `camera_url`, `alert_id`): Wakes the screen and shows a modal alert deduplicated by `alert_id`.
- **`latter_set_beacon_uuid`** (`uuid`): Programs the System's CSPRNG 128-bit BLE presence UUID into `latter_beacon` (persisted in NVS only when changed).
- **`latter_factory_reset`** (no variables): Turns the panel off, erases NVS, and reboots to defaults.

### 3d. Optional actions in the reference firmware

| Action | Variables | Behavior |
| :--- | :--- | :--- |
| `ui_input` | `action: string`, `delta: int` | Moves GUI focus (`home`, `back`, `select`, `next`, `prev`). |

---

## 4. Outbound (`Device -> HA` Diagnostic RPC Outbox)

Instead of calling `homeassistant.action` (which would grant a compromised LAN device unrestricted access to every service in Home Assistant), Latter devices emit compact JSON envelopes on the diagnostic `text_sensor` **`System: RPC Outbox`** (`id: latter_rpc_outbox`) using `latter_rpc::emit` ([`include/latter_rpc.h`](../include/latter_rpc.h)):

```cpp
latter_rpc::emit(&id(latter_rpc_outbox), "music_browse", {
  {"req_id", std::to_string(req_id)},
  {"mode", "favorites"},
  {"page", "1"},
  {"page_size", "5"},
});
```

Each frame is formatted as `<boot_hex>:<seq>:<part>/<total>|<json_slice>` (`<= 230` bytes per frame, UTF-8 codepoint-boundary safe, paced across loop passes when `total > 1` such as `device_hello`). `ha-latter`'s `core/rpc_receiver.py` subscribes strictly to each bound ESPHome device's outbox entity, reassembles multi-part envelopes, deduplicates `(boot_hex, seq)`, and dispatches to the corresponding handler (enforcing `core/security_policy.py` on perimeter actions carrying `code`) with the caller's `DeviceEntry` authenticated by its ESPHome transport binding.
