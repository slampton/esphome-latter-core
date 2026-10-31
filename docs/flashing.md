# Flashing

There are two ways to install Latter firmware:

| | Web installer | ESPHome Device Builder |
| :--- | :--- | :--- |
| **Best for** | Getting started quickly | Customizing, or the P4 devices |
| **Needs** | Chrome or Edge on a desktop, and a USB-C cable | The [ESPHome Device Builder](https://esphome.io/guides/getting_started_hassio) add-on, version 2026.10 or newer |
| **Wi-Fi setup** | S3 devices over Bluetooth; P4 devices not yet | Your `secrets.yaml` |
| **Updates** | The **Project Firmware** update entity in Home Assistant | Rebuild in Device Builder |

---

## Option A: Web installer

1. Open the [web installer](https://slampton.github.io/esphome-latter-core/) in Chrome or Edge on a desktop. Web Serial doesn't work on phones or in other browsers.
2. Pick your device and plug it in over USB-C.
3. Click **Connect & Install**, choose the serial port, and accept the prompt to erase the device. First installs always erase.
4. Wait for the flash to finish, typically a minute or two. The device reboots into the Latter boot screen.

### Connecting to Wi-Fi

**Pocket, Mobile, and Wrist Assistant (ESP32-S3).** These broadcast [Improv](https://www.improv-wifi.com/) over Bluetooth until they have Wi-Fi credentials. Either way works:
- **From Home Assistant:** if Home Assistant has Bluetooth or a Bluetooth proxy nearby, it shows **Discovered: Improv via BLE**. Click it and enter your Wi-Fi details.
- **From a phone or laptop:** open [improv-wifi.com](https://www.improv-wifi.com/) in Chrome on Android or desktop, connect to the device, and enter your Wi-Fi details.

**Remote and Desk Assistant (ESP32-P4).** The web-installer build doesn't include Wi-Fi provisioning yet. Use [Option B](#option-b-esphome-device-builder) for these boards.

<!-- TODO(release): remove the P4 note above once improv_serial (or another provisioning path) ships in the P4 device configs. -->

Once the device is on Wi-Fi, continue with [Getting started](https://github.com/slampton/ha-latter/blob/main/docs/getting-started.md#2-add-it-to-home-assistant-through-esphome).

---

## Option B: ESPHome Device Builder

1. In Device Builder, click **New Device**, give it a name, and skip the install step.
2. Replace the generated YAML with the following, changing the device file to match your board:

```yaml
substitutions:
  name: pocket-assistant          # hostname
  friendly_name: Pocket Assistant # name shown in Home Assistant
  device_slug: pocket_assistant   # friendly_name, lowercase with underscores

packages:
  latter: github://slampton/esphome-latter-core/devices/pocket-assistant.yaml@main

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

3. Click **Install → Plug into this computer** for the first flash. After that, updates go over Wi-Fi.

> [!IMPORTANT]
> **Keep `device_slug` in step with `friendly_name`.** Latter names a device's Home Assistant sensors after its name in Home Assistant, and the firmware subscribes to those sensors using `device_slug`. If the two don't match, the screens stay empty.

If the device was flashed with the web installer and is already on your network, Device Builder shows it under **Discovered** and offers **Adopt**. Adopting generates the same kind of config.

The first build downloads the toolchain and takes several minutes. Later builds are faster.

---

## Updates

- **Web-installer devices:** Home Assistant shows a **Project Firmware** update entity. Each device also checks for updates in the background every 6 hours while it's on USB power and idle.
- **Device Builder devices:** the core is pulled from `main` each time you build, so a rebuild picks up the latest release.

## Recovery

If a device won't boot or won't show up as a serial port:
1. Hold the **BOOT** button while plugging it in. This forces the ROM bootloader.
2. Flash again with either method, and let the installer erase the device.

The firmware also includes ESPHome's safe mode. After repeated boot failures it starts with only Wi-Fi and OTA, so you can push a fixed build.
