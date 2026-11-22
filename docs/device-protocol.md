# Device Protocol & Backend Interaction

This guide explains how an ESPHome Device and the **[Latter (`ha-latter`)](https://github.com/slampton/ha-latter)** Home Assistant integration discover, authenticate, and synchronize state at runtime. Implement this contract and your Device receives Area controls, media, remotes, weather, calendar events, and two-way Studio preferences—whether you build on `esphome-latter-core` or your own firmware.

> **Canonical Wire Schema:** Exact field indices, enum constants, action variables, RPC methods, and Home Assistant service signatures are auto-generated from [`protocol/latter_v3.json`](../protocol/latter_v3.json) into **[`DEVICE_CONTRACT.md`](../DEVICE_CONTRACT.md)**. This page explains how the pieces work together at runtime.

The contract has four moving parts:
1. **Discovery & Bound Identity.** How `ha-latter` identifies and binds to an ESPHome Device by `esphome_device_id` and MAC address.
2. **Handshake, Hardware Manifest & Offline Preference Sync.** What `device_hello` reports on connect and how offline NVS edits reconcile.
3. **Inbound (`HA -> Device`).** Bound Native API actions (`latter_push_v3`, `latter_set_slots`, `latter_show_notification`, `latter_device_command`, `latter_set_beacon_uuid`, `latter_factory_reset`, `latter_ui_input`, `latter_ping`).
4. **Outbound (`Device -> HA`).** Zero-privilege diagnostic RPC frames emitted on **`System: RPC Outbox`** (`latter_rpc_outbox`).

Reference implementations: [`modules/latter_display_firmware.yaml`](../modules/latter_display_firmware.yaml) for displays and [`modules/base.yaml`](../modules/base.yaml) ([`examples/minimal_headless_companion.yaml`](../examples/minimal_headless_companion.yaml)) for headless nodes. **Devices never require ESPHome's *"Allow the device to perform Home Assistant actions"* permission.**

---

## 1. Discovery & Strict Bound Identity

An ESPHome device qualifies as a Latter Device if it has at least one ESPHome entity **and** meets **any** of these checks:
- It registers at least one Latter Native API action (`latter_ping`, `latter_push_v3`, `latter_device_command`, `latter_set_slots`, or `latter_show_notification`).
- Its model or firmware version string contains `latter`.
- It exposes an `update` entity whose ID ends in `_project_firmware`.

When you accept **Discovered: Latter**, `ha-latter` creates a Device subentry storing `esphome_device_id` and hardware MAC (`via_device_id`). Every inbound `esphome.<node>_latter_*` call and outbound `System: RPC Outbox` subscription resolves strictly from that bound ESPHome device—never from entity slugs or substring matches. You can run multiple identical boards and rename them freely in Home Assistant without editing YAML.

**Minimum to be discovered (`modules/base.yaml`):** declares `latter_ping` (`id: latter_ping_action`), `latter_push_v3`, `latter_device_command`, `System: Hardware Manifest`, and `System: RPC Outbox`. After Home Assistant starts, `ha-latter` calls `latter_ping` on every bound Device so a `device_hello` emitted during startup is never missed.

---

## 2. Handshake, Hardware Manifest & Two-Way Preference Sync

Each Device describes its hardware in two places, both generated at compile time from `LATTER_*` flags by [`include/hal_manifest.h`](../include/hal_manifest.h):

| Surface | Builder | Purpose |
| :--- | :--- | :--- |
| **`device_hello`** (emitted on `System: RPC Outbox` on API connect and `latter_ping`) | `latter_ui::build_hardware_manifest_json()` | Full Manifest v2 JSON (`v: 2`, `contract: 3`), plus `prefs_seq` and `dirty_prefs` for offline NVS preference reconciliation |
| **`System: Hardware Manifest`** (`latter_hardware_manifest`, published at boot and every 300 s) | `latter_ui::build_hardware_manifest_state_json()` | Compact `<= 230`-byte copy inside Home Assistant's 255-character state limit |

| Manifest Keys | How `ha-latter` Uses Them |
| :--- | :--- |
| `w`, `h`, `shape`, `ips` | Server-side baseline-JPEG crop dimensions (`<= 720×720` wallpapers, `<= 480×480` art) and circular masking (`circle` vs. `square` / `none`) |
| `display`, `touch`, `imu`, `battery`, `has_rtc` | Gates Studio views/controls (`Display` & `Interface` hide when `display == 0`; motion wake hides when `imu == 0`; battery policy hides when `battery == 0`) |
| `stationary`, `wearable` | Classifies roaming hardware (`stationary == 0` or `wearable == 1`), enabling *Voice Follows Me* Area tracking |
| `slot_count`, `card_slot_count`, `max_payload_bytes` | Sizes paginated browse lists (`5` or `7`) and Area entity card pages (`3`, `5`, or `6`) to the panel geometry |
| `audio` (`out_rate`, `out_ch`, `out_bits`, `in_rate`), `mic`, `spk` | Matches FFmpeg sample rate/channels for doorbell audio streams and gates Tier 2 wake-word / alert-volume entities |
| `beacon` (`uuid`, `major`, `minor`), `board`, `chip`, `psram_mb` | Pairs BLE iBeacon advertisements (`minor` + MAC + provisioned 128-bit System UUID) and populates Studio diagnostics |
| `sdk`, `fw`, `boot_id`, `reset`, `crash`, `rollback` | Enforces `MIN_FIRMWARE_SDK_REV` lockstep and raises Home Assistant Repairs on panics, brownouts, or OTA rollbacks |

**Sleep-Safe Two-Way `device_prefs` Reconciliation:**
- Studio and Tier 2 Home Assistant entities write preferences immediately to the Device's subentry—even while a battery Device is hibernating—incrementing `prefs_seq` and pushing the `device_prefs` domain when connected.
- On-device **Settings** edits save to `PersistedDevicePrefs` v1 in NVS (`include/state/state_nvs_snapshot.h`) and emit `set_device_prefs` (`base_seq`, `changes`). If the Device was offline when edited, `device_hello` sends `prefs_seq` + `dirty_prefs` on reconnect so `ha-latter` merges offline edits with per-key last-writer-wins reconciliation before pushing the converged `device_prefs` frame.

---

## 3. Inbound (`HA -> Device` Native API Actions)

All ESPHome Native API actions use the `latter_` prefix (full variable schemas in [`DEVICE_CONTRACT.md` §3](../DEVICE_CONTRACT.md#3-esphome-device-actions-esphomenode_action)):

- **`latter_push_v3` (`domain`, `payload`)**: Pushes any of the **7 Protocol v3 domains**—`area_context` (Area climate/counts, Home Hub states & capability bitmasks `alarm_caps` / `climate_caps` / `vacuum_caps`, `area_icon`, `home_categories`, `local_tz_info`, `world_clocks_summary`, alerts, and cameras), `active_media`, `remote_config`, `weather_forecast`, `calendar_agenda`, `shortcuts`, and `device_prefs`. Payloads are `\x1f`-delimited (`v3\x1f<epoch>\x1f<rev>\x1f...`), content-hashed per domain, and reduced by `latter_ui::v3_domain_reducer()` (`components/latter_core/state_v3_parsers_*.cpp`, `components/latter_core/runtime_controllers.cpp`). On API reconnect, `latter_ui::reset_all_v3_rev_guards()` clears revision guards so fresh state applies cleanly.
- **`latter_set_slots` (`is_area_slots`, `req_id`, `mode`, `page`, `total_pages`, `slots`)**: Unified paginator pushing `\x1e`-separated records into `SlotBank` (`AREA` when `is_area_slots == true`, `BROWSE`/`CALENDAR` otherwise). Out-of-order responses with stale `req_id` are discarded.
- **`latter_show_notification` (`title`, `message`, `priority`, `duration`, `chime`, `camera_url`, `alert_id`)**: Deduplicates by `alert_id`, wakes the panel, plays the embedded WAV chime, and loads the signed baseline-JPEG camera snapshot if present.
- **`latter_device_command` (`command`)**: Executes immediate lifecycle/UI commands (`restart`, `find_my`, `activate_screensaver`, `deactivate_screensaver`, `wake_screen`, `screen_off`, `set_screen_bri:<1..100>`, `set_profiler_on`, `set_profiler_off`, `request_telemetry`, `set_pin_key:<key_id>:<hex_key>`, `trigger_va`, `dismiss_ringing`, `cancel_timer`, `reset_stopwatch`).
- **`latter_set_beacon_uuid` (`uuid`)**, **`latter_factory_reset`**, and **`latter_ping`**: Provision the System's 128-bit BLE UUID into `latter_beacon` NVS, wipe NVS preferences and reboot, or trigger `device_hello`.

---

## 4. Outbound (`Device -> HA` Diagnostic RPC Outbox & Security)

Instead of calling `homeassistant.action`, firmware emits typed RPC calls on **`System: RPC Outbox`** (`latter_rpc_outbox`, bound at boot via `latter_rpc::bind_default_outbox` and auto-hidden in Home Assistant via `RegistryEntryHider.INTEGRATION`) using helpers in [`include/latter_actions.h`](../include/latter_actions.h):

```cpp
// Automatically issues a monotonic req_id, uses LATTER_SLOT_COUNT, and emits on latter_rpc_outbox
const int req_id = latter_actions::emit_music_browse("favorites", 1);
```

`latter_rpc::emit` ([`include/latter_rpc.h`](../include/latter_rpc.h)) slices JSON envelopes along UTF-8 codepoint boundaries into `<boot_hex>:<seq>:<part>/<total>|<json_slice>` frames (`<= 230` bytes each), paced across loop ticks by `LatterLoopProbe` when `total > 1`. `LatterRpcReceiver` in `ha-latter` (`services/rpc_receiver.py`) reassembles chunks, deduplicates `(boot_hex, seq)`, binds `caller_coord`, and dispatches privately to `LatterRpcDispatcher` across all 20 outbound RPC methods ([`DEVICE_CONTRACT.md` §4](../DEVICE_CONTRACT.md#4-firmware-rpc-outbox-methods-latter_rpc_outbox)).

**Separate Alarm Capability PINs vs. Perimeter Security:**
- `area_context` field `alarm_caps` (`AreaCtxField::ALARM_CAPS`) encodes the Home Assistant alarm entity's supported arm modes plus `code_disarm_required` and `code_arm_required`. When an alarm panel uses its own code, arming/disarming prompts `ui::pin_pad_modal` as required by `alarm_caps` and passes the entered code straight through to Home Assistant's `alarm_control_panel` (arming is never blocked by `security_pin_policy: block`).
- Opening locks (`unlock`/`open`), opening garage/gate covers, and disarming code-less alarm panels are governed by the Device's Studio **Perimeter Security Policy** (`auto`, `allow`, `require_pin`, or `block`), verified server-side in constant time against the Device Security PIN (falling back to the System Security PIN).
