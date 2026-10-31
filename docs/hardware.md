# Hardware

Latter firmware supports five Waveshare boards. Purchase links are in the [device table](../README.md#devices).

## What else you need

- **A USB-C data cable** for the first flash. Some cables only carry power and won't work.
- **2.4 GHz Wi-Fi.** The ESP32-S3 and the ESP32-C6 Wi-Fi co-processor on the P4 boards can't join 5 GHz networks.
- **A LiPo battery** for the battery-capable boards, if your kit doesn't include one. Check the product listing for the connector type and recommended capacity.
- **Home Assistant** with the [Latter integration](https://github.com/slampton/ha-latter).
- **Optional:** one ESPHome Bluetooth proxy per area, for area presence.

---

## Comparison

| | Pocket Assistant | Mobile Assistant | Wrist Assist | Remote Assistant | Desk Assistant |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Board** | ESP32-S3-Touch-AMOLED-1.75C | ESP32-S3-Touch-AMOLED-2.16 | ESP32-S3-Touch-AMOLED-2.06 | ESP32-P4-WIFI6-Touch-LCD-3.5 | ESP32-P4-WIFI6-Touch-LCD-4B |
| **Chip** | ESP32-S3 | ESP32-S3 | ESP32-S3 | ESP32-P4 + C6 (Wi-Fi/BLE) | ESP32-P4 + C6 (Wi-Fi/BLE) |
| **Flash / PSRAM** | 32 MB / 8 MB | 16 MB / 8 MB | 32 MB / 8 MB | 16 MB / 32 MB | 32 MB / 32 MB |
| **Display** | 1.75" round AMOLED, 466×466 (CO5300, QSPI) | 2.16" square AMOLED, 480×480 (CO5300, QSPI) | 2.06" AMOLED, 410×502 (QSPI) | 3.5" IPS, 320×480 (SPI) | 4" IPS, 720×720 (MIPI-DSI) |
| **Touch** | CST9220 | CST9220 | FT3168 | FT5x06 | GT911 |
| **Audio** | ES8311 + ES7210 (speaker and mic array) | ES8311 + ES7210 | ES8311 + ES7210 | ES8311 (speaker and mic) | ES8311 + ES7210 |
| **IMU** | QMI8658 | QMI8658 | QMI8658 | — | — |
| **Power** | Battery (AXP2101) | Battery (AXP2101) | Battery (AXP2101) | Battery (AXP2101) | USB / mains |
| **Default power profile** | Pocket Carry | Handheld Roaming | Wearable Watch | Living Room Tabletop | Always-On Wall / Desk |
| **Board file** | [`waveshare_s3_175c.yaml`](../boards/waveshare_s3_175c.yaml) | [`waveshare_s3_216.yaml`](../boards/waveshare_s3_216.yaml) | [`waveshare_s3_206.yaml`](../boards/waveshare_s3_206.yaml) | [`waveshare_p4_35.yaml`](../boards/waveshare_p4_35.yaml) | [`waveshare_p4_4b.yaml`](../boards/waveshare_p4_4b.yaml) |

PSRAM sizes for the S3 boards are the minimum the firmware expects. Check the listing for your exact variant.

---

## Choosing

- **Pocket Assistant** is the smallest handheld. The round screen looks best for album art and clock faces. The crown button wakes and sleeps it, and long-pressing the crown starts hibernate.
- **Mobile Assistant** fits the most on screen of the three S3 handhelds, which matters most for Area Control and music browsing.
- **Wrist Assist** is a watch. It's the best choice for per-person presence, because it's always on you. Its default power profile uses a 15 s screen timeout.
- **Remote Assistant** is the fastest handheld, thanks to the P4 CPU. It has no IMU, so tilt games use touch steering and the Level tool is hidden.
- **Desk Assistant** has the largest and sharpest screen. It runs on USB power, is always on, and has its wake word on by default.

## Board-specific notes

- **Desk Assistant (P4 4B):** the current build targets **ESP32-P4 engineering-sample silicon (chip revision earlier than 3.0)**, which is what early 4B boards shipped with. If your board reports revision 3.x, the published binary won't boot. Build it yourself with `engineering_sample: false` set in the board file.
- **Remote Assistant (P4 3.5):** built for production silicon (revision 3.x). The board has a camera connector, but the camera isn't enabled in the default firmware. See the [`p4_camera`](mixins.md#p4_camera) mixin, which is experimental.
- **Both P4 boards:** Wi-Fi and Bluetooth run on the onboard ESP32-C6 co-processor. The firmware exposes the C6's own firmware as a separate **System: C6 Co-Processor Firmware** update entity.
- **Wrist Assist:** a PCF85063 RTC is present on the board but isn't used. Time comes from Home Assistant.
