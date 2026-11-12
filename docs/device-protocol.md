# Device Protocol

This is the contract between an ESPHome Device and the [Latter integration](https://github.com/slampton/ha-latter). Implement it and your Device receives Latter's Area, media, remote, weather, and agenda state, whether or not you use the rest of this SDK.

The protocol has four parts:
1. **Discovery.** How Latter identifies a compatible ESPHome Device.
2. **Hardware manifest.** What the Device reports about its capabilities and boot health.
3. **Inbound.** Native API actions Latter calls on the bound ESPHome Device (`latter_push_v3`, `set_browse_slots`, `set_area_slots`, `show_notification`, `latter_set_beacon_uuid`, `latter_factory_reset`, `latter_ping`).
4. **Outbound.** Zero-privilege diagnostic RPC frames published on **`System: RPC Outbox`** (`latter_rpc_outbox`).

The full display reference implementation is [`modules/latter_display_firmware.yaml`](../modules/latter_display_firmware.yaml) (backed by [`modules/base.yaml`](../modules/base.yaml) and [`modules/system_core.yaml`](../modules/system_core.yaml)). The smallest headless implementation is [`modules/base.yaml`](../modules/base.yaml) (see [`examples/minimal_headless_companion.yaml`](../examples/minimal_headless_companion.yaml)).

> ESPHome exposes an API action named `foo` on a Device named `my-panel` as the Home Assistant service `esphome.my_panel_foo`. Latter binds each Device to its ESPHome device ID and MAC address, resolving both inbound actions and the outbound `text_sensor` outbox strictly from that bound ESPHome device—never from entity slugs or substring matches. **Devices never require ESPHome's *"Allow the device to perform Home Assistant actions"* permission.**

---

## 1. Discovery

An ESPHome device counts as a Latter Device if it has at least one ESPHome entity **and** meets **any** of these conditions:
- It registers at least one of the actions `latter_ping`, `latter_push_v3`, `set_browse_slots`, `set_area_slots`, or `show_notification`.
- Its model or firmware version string contains `latter`.
- It exposes an `update` entity whose ID ends in `_project_firmware`.

Latter rescans after Home Assistant starts, on device-registry changes, and whenever ESPHome registers one of the actions above. After setup, Latter calls each bound Device's `latter_ping` action so a `device_hello` emitted while Home Assistant was still starting is never missed.

**Minimum to be discovered.** [`modules/base.yaml`](../modules/base.yaml) declares `latter_ping` (`id: latter_ping_action`), `latter_push_v3`, `latter_set_beacon_uuid`, `System: Hardware Manifest`, and `System: RPC Outbox`:

```yaml
api:
  actions:
    - action: latter_ping
      id: latter_ping_action   # fixed id so base.yaml and system_core.yaml never conflict
      then:
        - script.execute: send_device_hello
```

---

## 2. Hardware manifest

Each Device describes itself in two places, both built from the board's `LATTER_*` compile-time traits by [`include/hal_manifest.h`](../include/hal_manifest.h):

| Where | Built by | Content |
| :--- | :--- | :--- |
| `manifest` field of `device_hello` (emitted on `System: RPC Outbox` on every Home Assistant connection and on `latter_ping`) | `latter_ui::build_hardware_manifest_json()` | Full manifest including audio rates, beacon metadata, and boot health keys (`sdk`, `reset`, `crash`, `rollback`) |
| Text sensor **`System: Hardware Manifest`** (`latter_hardware_manifest`), published at boot and every 300 s | `latter_ui::build_hardware_manifest_state_json()` | Compact copy (`<= 230` bytes, inside Home Assistant's 255-character state limit) |

Latter reads the `text_sensor` of the bound ESPHome device and overlays the latest `device_hello` manifest on top of it.

| Key | Used by Latter for |
| :--- | :--- |
| `w`, `h` | Server-side image render dimensions (`LATTER_SCREEN_W`, `LATTER_SCREEN_H`) |
| `shape` | `circle` (`LATTER_ROUND_DISPLAY=1`) applies circular masking; `square` otherwise |
| `stationary`, `wearable` | Roaming classification (`stationary == 0` or `wearable == 1`), enabling Voice Follows Me Area tracking |
| `display` | `0` marks a headless node (omitted from display popups, notifications, and remote assignments) |
| `beacon` (`uuid`, `major`, `minor`), or `minor` | Matches iBeacon advertisements for BLE Area presence |
| `audio` (`out_rate`, `out_ch`) | Sample rate and channel count for doorbell listen streams |
| `imu` | Enables motion settings in Latter Studio |
| `board` | Hardware board identifier (`LATTER_BOARD_ID`) |
| `sdk`, `reset`, `crash`, `rollback` | Firmware revision lockstep (`LATTER_SDK_REV`) and boot health Repairs |

---

## 3. Inbound (`HA -> Device` Native API Actions)

### 3a. `latter_push_v3` (Protocol v3 Domain Push)

Latter pushes each of the 5 Protocol v3 domains (`area_context`, `active_media`, `remote_config`, `weather_forecast`, `calendar_agenda`) via `latter_push_v3`, unpacked into `latter_ui::protocol_state()` (`include/latter_state.h` + generated `include/latter_protocol.h` from [`protocol/latter_v3.json`](../protocol/latter_v3.json)):

| Variable | Type | Notes |
| :--- | :--- | :--- |
| `domain` | string | `area_context`, `active_media`, `remote_config`, `weather_forecast`, or `calendar_agenda` |
| `payload` | string | `\x1f`-packed (`0x1F`) payload prefixed with `v3\x1f<epoch>\x1f<rev>\x1f...`. Domain revisions are content-hashed; the Device resets its rev guards on epoch change or API reconnect. |

Image URLs (`art_url`, `wallpaper_url`, `camera_snapshot_url`, `alert_camera_url`, and per-event `map_url`) carry deterministic, day-bucketed, resource-scoped HMAC signatures.

### 3b. `set_browse_slots` and `set_area_slots`

Paginated lists (music library pages, Area pickers, calendar pages, lyrics, and Area/Site entity cards) arrive through `set_browse_slots` and `set_area_slots`, both handled by `latter_ui::apply_slots_action(...)` (`include/latter_state.h`) with the 6-variable Protocol v3 schema:

| Variable | Type | Notes |
| :--- | :--- | :--- |
| `contract` | int | `3` for Protocol v3 |
| `req_id` | int | Monotonic request correlation ID (`0` for unsolicited pushes); stale responses are discarded |
| `mode` | string | `areas`/`area_areas`, `entities`/`home`, `calendar_agenda`, `lyrics`, or music browse modes |
| `page` | int | 1-based page number |
| `total_pages` | int | Total page count |
| `slots` | string | `\x1e`-delimited records with `\x1f`-delimited fields, unpacked into `DeviceRuntimeState::slots` |

### 3c. `show_notification`, `latter_set_beacon_uuid`, `latter_factory_reset`, and `ui_input`

- **`show_notification`** (`title`, `message`, `priority`, `duration`, `chime`, `camera_url`, `alert_id`): Deduplicates by `alert_id` via `latter_ui::trigger_alert_banner(...)`, wakes the panel, plays the requested chime, and optionally loads the signed camera snapshot.
- **`latter_set_beacon_uuid`** (`uuid`): Updates the System's 128-bit BLE presence UUID in `latter_beacon` (persisting to NVS only on change).
- **`latter_factory_reset`** (no variables): Turns the panel off, erases NVS preferences, and reboots cleanly.
- **`ui_input`** (`action: string`, `delta: int`): Drives GUI navigation (`home`, `back`, `select`, `next`, `prev`).

---

## 4. Outbound (`Device -> HA` Diagnostic RPC Outbox)

Instead of calling `homeassistant.action`, Latter Devices emit compact JSON envelopes on **`System: RPC Outbox`** (`id: latter_rpc_outbox`) through typed C++ helpers in [`include/latter_actions.h`](../include/latter_actions.h) backed by `latter_rpc::emit` ([`include/latter_rpc.h`](../include/latter_rpc.h)):

```cpp
latter_actions::emit_music_browse(
    &id(latter_rpc_outbox), "favorites", 1, 5, req_id, "", "Alphabetical");
```

Each frame is formatted as `<boot_hex>:<seq>:<part>/<total>|<json_slice>` (`<= 230` bytes per frame, UTF-8 codepoint-boundary safe, paced across loop passes when `total > 1`). The Home Assistant integration subscribes strictly to each bound ESPHome device's outbox entity, reassembles multi-part envelopes, deduplicates `(boot_hex, seq)`, and enforces Area and security Policies on perimeter actions.
