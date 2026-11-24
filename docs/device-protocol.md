# Device Protocol & Backend Interaction

This guide explains how an ESPHome Device running **Latter Core** and the **[Latter (`ha-latter`)](https://github.com/slampton/ha-latter)** Home Assistant integration discover, authenticate, and synchronize state at runtime.

> **Canonical Wire Schema:** Exact field indices (`LATTER_CORE_REV = 2026112402`, `STUDIO_REV = 50`), enum constants, `constexpr` `PrefContractSpec` bounds, action variables, and outbound RPC methods are auto-generated from [`protocol/latter_v3.json`](../protocol/latter_v3.json) into **[`DEVICE_CONTRACT.md`](../DEVICE_CONTRACT.md)** and [`include/latter_protocol.h`](../include/latter_protocol.h) (while `ha-latter/custom_components/latter/studio_contract.json` owns HA services, Studio WS commands, world-clock catalogs, and UI preference metadata). This page explains how the pieces work together at runtime.

The contract has four moving parts:
1. **Discovery & Bound Identity.** How `ha-latter` identifies and binds to an ESPHome Device by `esphome_device_id` and MAC address.
2. **Handshake, Hardware Manifest & Offline Preference Sync.** What `device_hello` reports on connect and how offline NVS edits reconcile.
3. **Inbound (`HA -> Device`).** Nine bound Native API actions (`latter_ping`, `latter_push_v3`, `latter_set_beacon_uuid`, `latter_factory_reset`, `latter_device_command`, `latter_set_slots`, `latter_set_music_lyrics`, `latter_set_calendar_view`, `latter_show_notification`).
4. **Outbound (`Device -> HA`).** Twenty private RPC methods emitted on the auto-hidden **`System: RPC Outbox`** (`latter_rpc_outbox`) transport pipe and routed strictly through `LatterRpcDispatcher` (kept separate from the 6 public `latter.*` Home Assistant automation actions).

Reference implementations: [`modules/latter_display_firmware.yaml`](../modules/latter_display_firmware.yaml) for full displays, [`modules/latter_display_only_firmware.yaml`](../modules/latter_display_only_firmware.yaml) for audio-less/touch-optional displays, and [`modules/system_core.yaml`](../modules/system_core.yaml) for the unified API/RPC/boot foundation. **Devices never require ESPHome's *"Allow the device to perform Home Assistant actions"* permission.**

---

## 1. Discovery & Strict Bound Identity

An ESPHome device qualifies as a Latter Device if it has at least one ESPHome entity **and** meets **any** of these checks:
- It registers at least one Latter Native API action (`latter_ping`, `latter_push_v3`, `latter_device_command`, `latter_set_slots`, or `latter_show_notification`).
- Its model or firmware version string contains `latter`.
- It exposes an `update` entity whose ID ends in `_project_firmware`.

When you accept **Discovered: Latter**, `ha-latter` creates a Device subentry storing `esphome_device_id` and hardware MAC (`via_device_id`). Every inbound `esphome.<node>_latter_*` call and outbound `System: RPC Outbox` subscription resolves strictly from that bound ESPHome device—never from entity slugs or substring matches. You can run multiple identical boards and rename them freely in Home Assistant without editing YAML.

**Minimum to be discovered (`modules/system_core.yaml`):** declares `latter_ping` (`id: latter_ping_action`), `latter_push_v3`, `latter_device_command`, `send_device_hello`, and `System: RPC Outbox` (`latter_rpc_outbox`, auto-hidden via `RegistryEntryHider.INTEGRATION`). After Home Assistant starts, `ha-latter` calls `latter_ping` on every bound Device so a `device_hello` emitted during startup is never missed, while `LatterRpcReceiver.rebind()` seeds its baseline silently without replaying stale boot frames.

---

## 2. Handshake, Hardware Manifest, Telemetry & Two-Way Preference Sync

Each Device describes its hardware at compile time from `LATTER_*` flags via [`include/hal_manifest.h`](../include/hal_manifest.h) and reports live operational health over `latter_rpc_outbox`:

| RPC Method | Builder / Source | Purpose |
| :--- | :--- | :--- |
| **`device_hello`** (emitted on API connect and `latter_ping`) | `latter_core::build_hardware_manifest_json()` | Full Manifest v2 JSON (`v: 2`, `contract: 3`), plus `prefs_seq` and `dirty_prefs` for offline NVS preference reconciliation |
| **`device_telemetry`** (on connect, `request_telemetry`, profiler toggle, every `15 min` awake/docked or `60 s` while profiler is ON; paused in battery screen-off quiet) | `system_core.yaml` / `hal_link_report.h` | Reports `link_json`, `int_ram_free_kb`, `int_ram_min_kb`, `int_ram_largest_kb`, `psram_min_kb`, `ble_active`, and `profiler_on` into Studio **Developer & telemetry** and `diagnostics.py` without cluttering stock ESPHome device pages |
| **`display_state`** (on physical power/brightness/saver transitions) | `latter_core::emit_display_state()` | Synchronizes physical panel power, perceptual brightness (`1..100%`), and screensaver state two-way with `light.<slug>_screen` and `switch.<slug>_screensaver` |

| Manifest Keys | How `ha-latter` Uses Them |
| :--- | :--- |
| `w`, `h`, `shape`, `ips` | Server-side baseline-JPEG crop dimensions (`<= 1280×1280` wallpapers, `<= 480×480` art) and circular masking (`circle` vs. `square` / `none`) |
| `display`, `touch`, `imu`, `battery`, `has_rtc`, `video`, `hw_jpeg` | Gates Studio views/controls (`Display` & `Interface` hide when `display == 0`; motion wake hides when `imu == 0`; battery policy hides when `battery == 0`; live doorbell video popup gates on `hw_jpeg == 1`) |
| `stationary`, `wearable` | Classifies roaming hardware (`stationary == 0` or `wearable == 1`), enabling *Voice Follows Me* Area tracking |
| `slot_count`, `card_slot_count` | Sizes paginated browse lists (`5` or `7`) and Area entity card pages (`3`, `5`, or `6`) to the panel geometry |
| `audio` (`out_rate`, `out_ch`, `out_bits`, `in_rate`), `mic`, `spk` | Matches FFmpeg sample rate/channels for doorbell audio streams and gates wake-word / alert-volume / DND entities |
| `beacon` (`uuid`, `major`, `minor`, or `null` without `latter_beacon`), `board`, `chip`, `psram_mb` | Pairs BLE iBeacon advertisements (`minor` + MAC + provisioned 128-bit System UUID) and populates Studio diagnostics |
| `core_rev`, `fw`, `boot_id`, `reset`, `crash`, `rollback` | Checks `MIN_FIRMWARE_CORE_REV` (`2026112402`) lockstep (raising a non-blocking auto-clearing `outdated_firmware` HA Repair & Studio warning banner if older) and raises Repairs on panics, brownouts, or OTA rollbacks |

**Sleep-Safe Two-Way `device_prefs` Reconciliation:**
- Studio and sleep-safe Home Assistant policy entities (`select.<slug>_device_group`, `select.<slug>_wake_word_mode`, `switch.<slug>_bedtime_mode`, `switch.<slug>_do_not_disturb`, `number.<slug>_alert_volume`) write preferences immediately to the Device's subentry—even while a battery Device is hibernating—incrementing `prefs_seq` and pushing the `device_prefs` domain when connected.
- On-device **Settings** and **Quick Actions** edits save to `PersistedDevicePrefs` (`kPersistedPrefsVersion = 1U`, append-only prefix loader in `include/state/state_nvs_snapshot.h`) and emit `set_device_prefs` (`base_seq`, `changes`). If the Device was offline when edited, `device_hello` sends `prefs_seq` + `dirty_prefs` on reconnect so `ha-latter` merges offline edits with per-key last-writer-wins reconciliation before pushing the converged `device_prefs` frame.

---

## 3. Inbound (`HA -> Device` Native API Actions)

All 9 ESPHome Native API actions use the `latter_` prefix (full variable schemas in [`DEVICE_CONTRACT.md` §3](../DEVICE_CONTRACT.md#3-esphome-device-actions-esphomenode_action)):

- **`latter_push_v3` (`domain`, `payload`)**: Pushes any of the **7 Protocol v3 domains**—`area_context` (Area climate/counts, calibrated `area_ambient_light` `"<pct>|<lux>"`, Home Hub states & capability bitmasks `alarm_caps` / `climate_caps` / `vacuum_caps`, `area_icon`, `home_categories`, `broadcast_phrases`, `local_tz_info`, `world_clocks_summary`, alerts, and cameras), `active_media`, `remote_config`, `weather_forecast`, `calendar_agenda`, `shortcuts` (up to 24 dock items + chunked pinned items), and `device_prefs` (65 typed preferences including `glass_style`, `music_auto_open`, `AutoBrightnessMode::AmbientLight = 3`, and `do_not_disturb`). Payloads are `\x1f`-delimited (`v3\x1f<epoch>\x1f<rev>\x1f...`), symmetrically subfield-escaped (`\\` / `\p` / `\s`), UTF-8 byte-safe, and reduced by `latter_core::v3_domain_reducer()`.
- **`latter_set_slots` (`is_area_slots`, `req_id`, `mode`, `page`, `total_pages`, `slots`)**, **`latter_set_music_lyrics`**, and **`latter_set_calendar_view`**: Push paginated media/Area browse slots, chunked LRC/plain Music Assistant lyrics, and chunked windowed calendar events. Out-of-order responses with stale `req_id` are discarded.
- **`latter_show_notification` (`title`, `message`, `priority`, `duration`, `chime`, `camera_url`, `camera_entity`, `alert_id`)**: Deduplicates by `alert_id`, respects `bedtime_active` and `do_not_disturb` (rendering silent visual doorbell popups with `chime = false` under DND), and loads the signed LAN camera snapshot or live MJPEG stream.
- **`latter_device_command` (`command`)**: Executes typed lifecycle, display, telemetry, and security commands (`restart`, `find_my`, `wake_screen`, `screen_off`, `set_screen_bri:<1..100>`, `activate_screensaver`, `deactivate_screensaver`, `set_profiler_on` [emits compact boot/link/heap/power forensic burst and auto-disables after `24 h`], `set_profiler_off`, `request_telemetry`, `set_pin_key:<key_id>:<hex_key>`, `trigger_va`, `dismiss_ringing`, `cancel_timer`, `reset_stopwatch`).
- **`latter_set_beacon_uuid` (`uuid`)**, **`latter_factory_reset`**, and **`latter_ping`**: Provision the System's 128-bit BLE UUID into `latter_beacon` NVS, wipe NVS preferences and reboot, or trigger `device_hello`.

---

## 4. Outbound (`Device -> HA` Private RPC Dispatcher & PIN Wrapping)

Instead of calling `homeassistant.action` or exposing internal device methods on `hass.services`, firmware emits typed RPC envelopes on **`System: RPC Outbox`** (`latter_rpc_outbox`) using helpers in [`include/latter_actions.h`](../include/latter_actions.h):

```cpp
// Issues a monotonic req_id, uses LATTER_SLOT_COUNT, and emits on latter_rpc_outbox
const int req_id = latter_core::emit_music_browse("favorites", 1);
```

`latter_rpc::emit` ([`include/latter_rpc.h`](../include/latter_rpc.h)) slices JSON envelopes along UTF-8 codepoint boundaries into `<boot_hex>:<seq>:<part>/<total>|<json_slice>` frames (`<= 230` bytes each, up to `kMaxRpcSlices = 32` parts), paced across loop ticks (`kMinInterFrameMs`) and evicted atomically by `(seq)`. `LatterRpcReceiver` (`services/rpc_receiver.py`) reassembles chunks (`MAX_RPC_PARTS = 32`), deduplicates `(boot_hex, seq)`, binds `caller_subentry_id`, and dispatches privately to **`LatterRpcDispatcher`** (`services/rpc_dispatcher.py`) across all **20 outbound RPC methods** ([`DEVICE_CONTRACT.md` §4](../DEVICE_CONTRACT.md#4-firmware-rpc-outbox-methods-latter_rpc_outbox)), while `hass.services` exposes only the **6 public user automation actions** (`latter.notify`, `latter.intercom_broadcast`, `latter.set_active_area`, `latter.set_active_speaker`, `latter.reset_device`, `latter.navigate`).

**Authenticated Session-Key PIN Wrapping (`4–12` Digits) & Brute-Force Lockout:**
- Home Assistant provisions a per-session 256-bit key (`set_pin_key:<key_id>:<hex_key>`). Before emitting `area_action` or `shortcut_action`, firmware wraps any `4–12` digit PIN as `"v1:<key_id>:<nonce_hex8>:<ct_hex>:<mac_hex32>"` bound to `entity_id` + `action` and scrubs all RAM/ring buffers via `latter_rpc::secure_zero_string` so plaintext PINs never touch `latter_rpc_outbox` or Home Assistant's state history.
- `area_context.alarm_caps` passes Home Assistant alarm panel codes straight through after unwrapping (never blocking `arm_*` under `security_pin_policy: block`), while opening locks, opening garage covers, and disarming code-less alarms enforce the Device's **Perimeter Security Policy** (`auto`, `allow`, `require_pin`, or `block`) with PBKDF2-HMAC-SHA256 verification and brute-force lockout (`5` failed attempts -> `60 s` backoff).
