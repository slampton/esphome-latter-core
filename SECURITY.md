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

- **Zero Home Assistant Action Privileges**: Latter firmware never requires *"Allow the device to perform Home Assistant actions"*. Device-to-Home Assistant RPCs travel over a bound diagnostic outbox authenticated by `esphome_device_id`, and physical perimeter actions (`unlock`, `disarm`, `open_cover`) are gated server-side by Latter's 3-tier Perimeter Security Policy.
- **No Unauthenticated LAN OTA Port on Factory Builds**: Pre-built Web Installer binaries omit ESPHome TCP port `3232` network OTA and update exclusively via HTTPS pull-OTA (`ota: platform: http_request`). Custom ESPHome Device Builder configurations require `api: encryption:` and `ota: password:`.
- **Signed Resource Fetch**: Firmware fetches artwork, wallpapers, camera snapshots, and maps over expiring, Device-scoped HMAC-SHA256 URLs without storing Home Assistant long-lived bearer tokens in flash or NVS.
