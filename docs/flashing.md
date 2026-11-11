# Flashing

There are two ways to install Latter firmware:

| | Web installer | ESPHome Device Builder |
| :--- | :--- | :--- |
| **Best for** | Getting started quickly | Customizing, or local network push-OTA |
| **Needs** | Chrome or Edge on a desktop, and a USB-C cable | The [ESPHome Device Builder](https://esphome.io/guides/getting_started_hassio) add-on, version 2026.11.0b3 or newer |
| **Wi-Fi setup** | Over the USB cable (Improv Serial), all five devices | Your `secrets.yaml` |
| **Updates** | HTTPS pull-OTA via the **Project Firmware** update entity in Home Assistant, plus wireless 1-click **Adopt** in Device Builder (runtime Noise PSK) | Noise-encrypted push-OTA (`ota_esphome` using `api.encryption.key`) alongside HTTPS pull-OTA |

---

## Option A: Web installer

1. Open the [web installer](https://slampton.github.io/esphome-latter-core/) in Chrome or Edge on a desktop. Web Serial doesn't work on phones or in other browsers.
2. Pick your device and plug it in over USB-C.
3. Click **Connect & Install**, choose the serial port, and accept the prompt to erase the device. First installs always erase.
4. Wait for the flash to finish, typically a minute or two. The device reboots into the Latter boot screen.

### Connecting to Wi-Fi

When flashing finishes, the installer asks for your Wi-Fi network and password and sends them to the device over the same USB cable ([Improv Serial](https://www.improv-wifi.com/serial/)). This works on all five devices. If you close the dialog, click **Connect** again with the device plugged in and choose **Configure Wi-Fi**.

Once the device is on Wi-Fi, continue with [Getting started](https://github.com/slampton/ha-latter/blob/main/docs/getting-started.md#2-add-it-to-home-assistant-through-esphome). Because Latter uses a bound diagnostic RPC outbox and direct Native API pushes, you **do not** need to enable *"Allow the device to perform Home Assistant actions"* in Home Assistant.

---

## Option B: ESPHome Device Builder

1. In Device Builder, click **New Device**, give it a name, and skip the install step.
2. Replace the generated YAML with the following, changing the device file to match your board:

```yaml
substitutions:
  name: pocket-assistant          # unique network hostname
  friendly_name: Pocket Assistant # name shown in Home Assistant (rename freely anytime)

packages:
  latter:
    url: https://github.com/slampton/esphome-latter-core
    ref: main
    files:
      - devices/pocket-assistant.yaml
    refresh: 0s   # pull the latest YAML on every build (the shorthand github:// form caches for a day)

api:
  encryption:
    key: !secret api_key

ota:
  - id: !extend ota_esphome
    encryption:

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
```

| Board | Device file |
| :--- | :--- |
| Pocket Assistant | `devices/pocket-assistant.yaml` |
| Mobile Assistant | `devices/mobile-assistant.yaml` |
| Wrist Assist | `devices/wrist-assist.yaml` |
| Remote Assistant | `devices/remote-assistant.yaml` |
| Desk Assistant | `devices/desk-assistant.yaml` |

3. Click **Install → Plug into this computer** for the first flash (or **Wirelessly** if adopting an already-paired Web-Installer device). After that, wireless updates go over Noise-encrypted OTA authenticated by `api.encryption.key`—no separate OTA password is needed.

> [!NOTE]
> **Multiple identical devices, client-initiated dial-back & free renaming.** Latter binds every Device by its ESPHome `device_id` and MAC address and pushes Protocol v3 state directly over the Native API (`latter_push_v3`). You can run multiple identical displays (e.g. `pocket-assistant-1`, `pocket-assistant-2`) and rename them in Home Assistant at any time without editing YAML. Every build also enables ESPHome's Noise-verified `outgoing_connection` (`delay: 10s` -> Home Assistant TCP `:6054`), automatically learning and saving Home Assistant's IP in NVS on first pairing so waking battery devices and roaming displays reconnect in seconds even when mDNS or inbound backoff lags (if your IoT VLAN blocks mDNS on first setup, add `outgoing_connection: { host: <ha-ip> }` under `api:`).

If the device was flashed with the web installer and paired with Home Assistant, `api.encryption` has already provisioned a runtime Noise key—Device Builder discovers it on your network and offers wireless 1-click **Adopt**. Adopting generates a short `github://` package line; replace it with the long form above (keeping the generated `api.encryption.key`, adding the 3-line `ota:` `encryption:` extension, and setting `refresh: 0s`).

> [!WARNING]
> **Use `refresh: 0s` on the package.** The firmware's C++ code always comes from `main`, but ESPHome caches `github://` packages for a day. Without `refresh: 0s` a build can combine new C++ code with day-old YAML. The build stops with a "different revisions" error when that happens.

The first build downloads the toolchain and compiles the 3-component Latter C++20 SDK (`latter_hal`, `latter_core`, and `latter_ui`, plus `latter_beacon`), taking several minutes. Later builds reuse PlatformIO's compiled object cache and finish much faster.

> [!TIP]
> **Remote Assistant (`waveshare_p4_35`) battery hibernate vs. USB alarms.** On the Remote Assistant, `K1` (`GPIO49`) is wired to the AXP2101 PMU's `PWRON` pin rather than an ESP32-P4 LP deep-sleep wake pin (`!LATTER_PWR_KEY_DEEP_SLEEP_WAKE`). On battery, hibernate powers the AXP2101 cleanly off—press `K1` once to power back on—and on-battery wake alarms are disabled; scheduled alarms stay active whenever the Remote Assistant is docked on USB power.

---

## Updates

- **Web-installer devices:** Home Assistant shows a **Project Firmware** update entity backed by HTTPS `http_request` pull-OTA, and each device checks for updates in the background every 6 hours while on USB power and idle. Once paired with Home Assistant, the runtime-provisioned `api.encryption` Noise PSK also secures wireless 1-click **Adopt** in Device Builder.
- **Device Builder devices:** `core_system.yaml` includes both `ota: - platform: http_request` and `ota: - platform: esphome` (`id: ota_esphome`), and extending `ota_esphome` with `encryption:` locks push-OTA down to `Encryption: required` using `api.encryption.key` alongside HTTPS pull-OTA. With `refresh: 0s` on the package, each build pulls the latest `main`. The boot log line `Latter SDK rev … (YAML rev …)` shows which revision is running.
- **Placement:** wireless updates need a good link. Keep docked devices at least 30 cm apart (neighbouring devices detune each other's antennas) and check the **Wi-Fi Signal** sensor: below about -70 dBm an update can slow to several minutes or stall. Update one device at a time.

## Recovery

If a device won't boot or won't show up as a serial port:
1. Hold the **BOOT** button while plugging it in. This forces the ROM bootloader.
2. Flash again with either method, and let the installer erase the device.

The firmware also includes ESPHome's safe mode. After repeated boot failures it starts with only Wi-Fi and OTA, so you can push a fixed build.
