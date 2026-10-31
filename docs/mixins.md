# Mixins

Mixins are ESPHome [packages](https://esphome.io/components/packages.html) that add one Latter feature to a config you already have. Each one (except `p4_camera`) also registers a `latter_ping` API action. That action is how the Latter integration discovers the device. The action uses a fixed ID, so you can add several mixins to one config without conflicts.

```yaml
packages:
  latter_proxy: github://slampton/esphome-latter-core/mixins/ble_proxy.yaml@main
  latter_diag:  github://slampton/esphome-latter-core/mixins/diagnostics.yaml@main
```

> [!IMPORTANT]
> Mixins are for **your own** ESPHome configs. The Latter firmware devices already include the beacon, VPN, and diagnostics features. Adding mixins to them causes ID and name collisions.

| Mixin | Status | Needs |
| :--- | :--- | :--- |
| [`ble_proxy`](#ble_proxy) | Stable | ESP32 with Bluetooth, preferably mains-powered |
| [`ble_beacon`](#ble_beacon) | Stable | ESP32 with Bluetooth |
| [`roaming_vpn`](#roaming_vpn) | Stable | 4 secrets |
| [`diagnostics`](#diagnostics) | Stable | — |
| [`rotary_input`](#rotary_input) | Experimental | A config built on Latter Core's `latter_client.yaml` |
| [`p4_camera`](#p4_camera) | Experimental | ESP32-P4 with an OV5647 camera |

---

## `ble_proxy`

Turns the node into an active Bluetooth proxy. Latter uses it as a presence scanner for whatever Area the node is assigned to.

```yaml
packages:
  latter_proxy: github://slampton/esphome-latter-core/mixins/ble_proxy.yaml@main
```

- **Adds:** `esp32_ble_tracker` with active scanning, plus `bluetooth_proxy: active: true`.
- **After flashing:** assign the node to an Area in Home Assistant. Latter maps the scanner to an area through the device's Area.

## `ble_beacon`

Broadcasts an iBeacon so Latter can track which area *this* node is in. This is useful for something you carry, such as a battery-powered button or a tag.

```yaml
packages:
  latter_ble: github://slampton/esphome-latter-core/mixins/ble_beacon.yaml@main
```

- **Beacon:**
  - UUID `4C415454-4552-434F-5245-000000000220`, which is the shared Latter UUID.
  - Major 1, minor 1, −59 dBm at 1 m, advertised every 500–1000 ms.
- **Entity:** **Area: BLE Presence Beacon** switch, which turns the Bluetooth radio on and off.
- **How Latter identifies the node:** by its MAC address in Home Assistant's device registry. The UUID is only a filter. So the node must be added to Home Assistant through the ESPHome integration, and **This device roams** must be checked when you add it to Latter.
- **Existing `esp32_ble:` block:** the mixin's switch refers to `id: latter_ble_ctrl`. If your config already defines `esp32_ble:` with a different `id`, remove that `id` or change it to `latter_ble_ctrl`.
- **Untested combination:** a beacon and `ble_proxy` on the same node.

## `roaming_vpn`

Brings up a WireGuard tunnel back to your network, so a portable device stays connected to Home Assistant on any Wi-Fi, including a phone hotspot.

```yaml
packages:
  latter_vpn: github://slampton/esphome-latter-core/mixins/roaming_vpn.yaml@main
```

Required in `secrets.yaml`. **Studio → Devices → Administration → Device Roaming Identity** generates all four:

```yaml
latter_vpn_private_key: "<device private key>"
latter_vpn_endpoint: "<your WireGuard host or IP>"
latter_vpn_server_public_key: "<server public key>"
latter_vpn_preshared_key: "<preshared key>"
```

Optional substitutions:

| Substitution | Default |
| :--- | :--- |
| `latter_vpn_ip` | `10.10.10.2` |
| `latter_vpn_netmask` | `255.255.255.0` |
| `latter_vpn_allowed_subnet` | `10.10.10.0/24` |
| `latter_vpn_port` | `51820` |

- **Entities:** System: VPN Tunnel Connected, System: VPN Tunnel Enabled, System: VPN Latest Handshake.
- **Keepalive:** 25 s.
- **You still need a WireGuard server**, such as a router or the Home Assistant WireGuard add-on, with the peer config that Studio generates.

## `diagnostics`

Adds three entities: **System: Uptime**, **System: Wi-Fi Signal** (updated every 60 s), and an **Actions: Restart Device** button. This is a good choice if all you want is discovery and Studio's device view.

```yaml
packages:
  latter_diag: github://slampton/esphome-latter-core/mixins/diagnostics.yaml@main
```

## `rotary_input`

*Experimental.* Drives the Latter GUI's focus navigation from a rotary encoder with a push button.

```yaml
substitutions:
  rotary_pin_a: "GPIO40"
  rotary_pin_b: "GPIO41"
  rotary_pin_btn: "GPIO42"
packages:
  latter_rotary: github://slampton/esphome-latter-core/mixins/rotary_input.yaml@main
```

- **Controls:** turn to move focus. Click to select, double-click to go home, and hold for 600 ms or longer to go back.
- **Requires `latter_client.yaml`**, because it calls the `ui_input_event` script.
- **Change the default pins.** GPIO40–42 conflict with the audio and touch pins on the 2.06" and 2.16" boards.

## `p4_camera`

*Experimental.* Exposes an OV5647 MIPI-CSI camera on an ESP32-P4 board as a Home Assistant camera entity. To show it in the camera carousel on Latter devices, add it under **Studio → Shared Entities → Cameras**.

```yaml
packages:
  latter_p4_cam: github://slampton/esphome-latter-core/mixins/p4_camera.yaml@main
```

- **Stream:** 640×480 at 10 fps.
- **Requires `i2c` with `id: bus_internal`.**
- **Requires `mipi_csi_cam`.** This mixin uses the `mipi_csi_cam` component and doesn't declare where it comes from. Your ESPHome build has to provide it.
- **No discovery action.** This mixin doesn't register `latter_ping`, so pair it with another mixin if you want the device discovered.
