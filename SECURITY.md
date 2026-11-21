# Security Policy

## Supported Versions

Latter follows a lockstep release cadence across [`esphome-latter-core`](https://github.com/slampton/esphome-latter-core) and [`ha-latter`](https://github.com/slampton/ha-latter). Security fixes land on `dev`, promote automatically to `main` once CI passes, and ship in the latest tagged release.

| Version | Supported |
| :--- | :--- |
| `3.0.x` (latest release & `main`) | Yes |
| `< 3.0.0` | No |

## Reporting a Vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Instead, report vulnerabilities privately through **[GitHub Security Advisories](https://github.com/slampton/esphome-latter-core/security/advisories/new)**. Include:

1. Affected component(s), board HAL, or protocol message.
2. Reproduction steps or proof-of-concept payload.
3. Expected security boundary vs. observed behavior.

We acknowledge private vulnerability reports within 72 hours and coordinate fixes across both `esphome-latter-core` and `ha-latter` before disclosure.

## Firmware Security Architecture

- **Zero Home Assistant Action Privileges & Auto-Hidden Outbox**: Latter firmware never requires *"Allow the device to perform Home Assistant actions"*. Device-to-Home Assistant RPCs travel over a bound diagnostic outbox (`latter_rpc_outbox`, auto-hidden via `RegistryEntryHider.INTEGRATION`, excluded from Home Assistant recorder history, and seeded silently on reconnect to prevent frame replay) authenticated by `esphome_device_id` and dispatched to `ha-latter`'s private `LatterRpcDispatcher`. Physical perimeter actions (`unlock`, `disarm`, `open_cover`) are gated server-side by Latter's 3-tier Perimeter Security Policy and `PinLockoutTracker` (`5` tries -> `60 s` exponential backoff up to `900 s`).
- **`v1:` Session-Key PIN Wrapping & Volatile RAM Scrubbing**: Security PINs (`4..12` ASCII digits) are never emitted in plaintext over `latter_rpc_outbox`. Home Assistant provisions an ephemeral 256-bit per-session key (`set_pin_key:<key_id>:<hex_key>`), and `latter_rpc::wrap_pin_code` encrypts and authenticates each PIN submission as `v1:<key_id>:<nonce_hex8>:<ct_hex>:<mac_hex32>` using a monotonic `uint32_t` nonce and HMAC-SHA256 bound to `key_id`, `nonce`, `entity_id`, and `action` (blocking PIN RPC emission if no session key is provisioned). PIN modal buffers, `target_code`, `set_pin_key:` command payloads, and `RpcQueue` ring frames are volatile-scrubbed immediately after use (`latter_rpc::secure_zero_string`).
- **Noise-Encrypted OTA & HTTPS Pull-OTA**: Firmware updates use HTTPS pull-OTA (`ota: - platform: http_request`) and Noise-encrypted ESPHome push-OTA (`ota: - platform: esphome`, `id: ota_esphome`) backed by 256-bit `api: encryption:` keys (runtime-provisioned upon Home Assistant pairing on Web-Installer builds, and locked to `Encryption: required` via `ota:` `encryption:` inheriting `api.encryption.key` on ESPHome Device Builder configurations—plaintext `ota` passwords are never used).
- **Signed Local Resource Fetch & 100% Zero-WAN Operation**: Firmware fetches local artwork, wallpapers, and camera snapshots/streams (`camera_stream:` allowlist capability token + private-LAN IP enforcement in `ha-latter`) over short-lived, Device-scoped HMAC-SHA256 URLs without storing Home Assistant long-lived bearer tokens in flash or NVS, and performs zero WAN/cloud API calls (external map/tile/lyrics/metadata endpoints are retired).
