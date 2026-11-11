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
| **Board** | ESP32-S3-Touch-AMOLED-1.75C | ESP32-S3-Touch-AMOLED-2.16 | ESP32-S3-Touch-AMOLED-2.06 | ESP32-P4-WIFI6-Touch-LCD-3.5 | ESP32-P4-86-Panel-ETH-2RO |
| **Chip** | ESP32-S3 | ESP32-S3 | ESP32-S3 | ESP32-P4 + C6 (Wi-Fi/BLE) | ESP32-P4 + C6 (Wi-Fi/BLE) |
| **Flash / PSRAM** | 32 MB / 8 MB | 16 MB / 8 MB | 32 MB / 8 MB | 16 MB / 32 MB | 32 MB / 32 MB |
| **Display** | 1.75" round AMOLED, 466×466 (CO5300, QSPI) | 2.16" square AMOLED, 480×480 (CO5300, QSPI) | 2.06" AMOLED, 410×502 (QSPI) | 3.5" IPS, 320×480 (SPI) | 4" IPS, 720×720 (MIPI-DSI) |
| **Touch** | CST9217 | CST9217 (sold as CST9220) | FT3168 | FT6336 | GT911 |
| **Audio** | ES8311 + ES7210 (speaker and mic array) | ES8311 + ES7210 | ES8311 + ES7210 | ES8311 (speaker and mic) | ES8311 + ES7210 |
| **IMU** | QMI8658 | QMI8658 | QMI8658 | — | — |
| **Power** | Battery (AXP2101) | Battery (AXP2101) | Battery (AXP2101) | Battery (AXP2101) | USB / mains |
| **Default power profile** | Portable Carry | Handheld Roaming | Wearable Watch | Tabletop Dock | Always-On Wall / Desk |
| **Board file** | [`waveshare_s3_175c.yaml`](../boards/waveshare_s3_175c.yaml) | [`waveshare_s3_216.yaml`](../boards/waveshare_s3_216.yaml) | [`waveshare_s3_206.yaml`](../boards/waveshare_s3_206.yaml) | [`waveshare_p4_35.yaml`](../boards/waveshare_p4_35.yaml) | [`waveshare_p4_86_panel.yaml`](../boards/waveshare_p4_86_panel.yaml) |

PSRAM sizes for the S3 boards are the minimum the firmware expects. Check the listing for your exact variant.

---

## Choosing

- **Pocket Assistant** is the smallest handheld. The round screen looks best for album art and clock faces. The crown button wakes and sleeps it, and long-pressing the crown starts hibernate.
- **Mobile Assistant** fits the most on screen of the three S3 handhelds, which matters most for Area Control and music browsing.
- **Wrist Assist** is a watch. It's the best choice for per-person presence, because it's always on you. Its default power profile uses a 15 s screen timeout.
- **Remote Assistant** is the fastest handheld, thanks to the P4 CPU. It has no IMU, so tilt games use touch steering and the Level tool is hidden.
- **Desk Assistant** has the largest and sharpest screen. It runs on USB power, is always on, and has its wake word on by default.

## Board-specific notes

- **ESP32-P4 chip revision (Remote and Desk Assistant):** Waveshare has shipped both revision 1.x and revision 3.x P4 silicon, and a build for one won't boot on the other. The web installer offers both. Your boot log shows the revision (`chip revision: v1.3` or `v3.x`). In ESPHome Device Builder, set `p4_silicon`, `p4_engineering_sample`, and `p4_cpu_frequency` in your device file's `substitutions:` (see the board file header for the two profiles). The Desk Assistant defaults to revision 1.x and the Remote Assistant to revision 3.x.
- **Desk Assistant (P4 86-Panel):** the ESP32-P4-86-Panel-ETH-2RO shares its mainboard with the ESP32-P4-WIFI6-Touch-LCD-4B, which hasn't been validated yet. The bottom board's Ethernet, RS485, and relay pins are reserved and not used by the firmware. The C6 module has a U.FL connector, so attach an external antenna.
- **Remote Assistant (P4 3.5):** the board has a camera connector, but the firmware doesn't use the camera. Because `K1` (`GPIO49`) wakes the AXP2101 PMU (`PWRON`) rather than ESP32-P4 LP deep sleep, battery hibernate powers the PMU cleanly off (press `K1` once to boot), and scheduled alarms run whenever the device is docked on USB power.
- **Both P4 boards:** Wi-Fi and Bluetooth run on the onboard ESP32-C6 co-processor. The firmware exposes the C6's own firmware as a separate **System: C6 Co-Processor Firmware** update entity.
- **Wrist and Mobile Assistant:** a PCF85063 RTC is present on the board but isn't used. Time comes from Home Assistant.

<!-- BEGIN GENERATED: tools/check_boards.py --render-docs (do not edit by hand) -->

### Pin maps (from `boards/facts/*.json`)

Confidence: **H** validated on hardware, **M** schematic or vendor code, **L** unconfirmed (blocks release).

#### Remote Assistant: Waveshare ESP32-P4-WIFI6-Touch-LCD-3.5 (`waveshare_p4_35`)

| GPIO | Role | Notes | Conf. |
|---:|---|---|:---:|
| 6 | c6_io2_host_wake | LP-capable; unused | M |
| 7 | i2c_sda |  | H |
| 8 | i2c_scl |  | H |
| 9 | i2s_dout |  | H |
| 10 | i2s_ws |  | H |
| 11 | i2s_din |  | H |
| 12 | i2s_bclk |  | H |
| 13 | i2s_mclk |  | H |
| 14 | c6_sdio_d0 |  | H |
| 15 | c6_sdio_d1 |  | H |
| 16 | c6_sdio_d2 |  | H |
| 17 | c6_sdio_d3 |  | H |
| 18 | c6_sdio_clk |  | H |
| 19 | c6_sdio_cmd |  | H |
| 20 | display_spi_mosi |  | H |
| 21 | display_spi_clk |  | H |
| 23 | display_cs |  | H |
| 26 | display_dc |  | H |
| 27 | display_reset |  | H |
| 28 | backlight_pwm | active_high; FET, 2 kHz / 8-bit | H |
| 29 | touch_reset | pulsed at boot by the HAL | H |
| 35 | boot_button | active_low | H |
| 45 | sd_power_gate | high_off; AO3401 gate, 10k pull-down: HIGH = SD off; left undriven | M |
| 49 | pwr_key | active_high; K1 -> AXP PWRON and BSS138 -> GPIO49. GPIO49 reads presses; it is not LP, so it cannot wake deep sleep (alarm-sleep wakes on its timer only). Wake from hibernate is the AXP2101: hibernate powers the PMIC off and K1 on PWRON powers it back on, so the P4 cold-boots. Sean, bench 2026-11-09: press + wake from hibernate verified | H |
| 50 | touch_int | active_low | H |
| 53 | speaker_pa | active_high | H |
| 54 | c6_chip_pu | active_high; 10k pull-up + 1 uF; held low in alarm-sleep (rev >= 3.0) | H |

#### Desk Assistant: Waveshare ESP32-P4-86-Panel-ETH-2RO (`waveshare_p4_86_panel`)

| GPIO | Role | Notes | Conf. |
|---:|---|---|:---:|
| 6 | c6_io2_host_wake | LP-capable; unused | M |
| 7 | i2c_sda |  | H |
| 8 | i2c_scl |  | H |
| 9 | i2s_dout |  | H |
| 10 | i2s_ws |  | H |
| 11 | i2s_din |  | H |
| 12 | i2s_bclk |  | H |
| 13 | i2s_mclk |  | H |
| 14 | c6_sdio_d0 |  | H |
| 15 | c6_sdio_d1 |  | H |
| 16 | c6_sdio_d2 |  | H |
| 17 | c6_sdio_d3 |  | H |
| 18 | c6_sdio_clk |  | H |
| 19 | c6_sdio_cmd |  | H |
| 23 | touch_reset | not driven (GT911 INT unrouted, so no reset sequence) | M |
| 26 | backlight_analog_dim | inverted; 10k/100nF RC into the AP3032 FB; 5 kHz / 10-bit, usable 75-100 % on-duty | H |
| 27 | display_reset |  | M |
| 28 | reserved | IP101 Ethernet RMII (bottom board) | M |
| 29 | reserved | IP101 Ethernet RMII (bottom board) | M |
| 30 | reserved | IP101 Ethernet RMII (bottom board) | M |
| 31 | reserved | IP101 Ethernet MDC (bottom board) | M |
| 32 | reserved | relay 1 (bottom board) | M |
| 33 | backlight_enable | active_high; 100k pull-up to 5 V; LOW = boost off | H |
| 34 | reserved | IP101 Ethernet RMII (bottom board) | M |
| 35 | boot_button | active_low (shared with IP101 RMII TXD1 (bottom board)) | H |
| 39 | sd_d0 | unused | M |
| 40 | sd_d1 | unused | M |
| 41 | sd_d2 | unused | M |
| 42 | sd_d3 | unused | M |
| 43 | sd_clk | unused | M |
| 44 | sd_cmd | unused | M |
| 45 | sd_power_gate | high_off; Waveshare P4 BSP family convention; left undriven | M |
| 46 | reserved | relay 2 (bottom board) | M |
| 47 | reserved | RS485 TX (bottom board) | M |
| 48 | reserved | RS485 RX (bottom board) | M |
| 49 | reserved | IP101 Ethernet RMII (bottom board) | M |
| 50 | reserved | IP101 Ethernet RMII (bottom board) | M |
| 51 | reserved | IP101 Ethernet RMII (bottom board) | M |
| 52 | reserved | IP101 Ethernet MDIO (bottom board) | M |
| 53 | speaker_pa | active_high; NS4150B | H |
| 54 | c6_chip_pu | active_high | H |

#### Pocket Assistant: Waveshare ESP32-S3-Touch-AMOLED-1.75C (`waveshare_s3_175c`)

| GPIO | Role | Notes | Conf. |
|---:|---|---|:---:|
| 0 | boot_button | active_low; side PTT / action button; light-sleep wake | H |
| 1 | display_reset |  | H |
| 2 | touch_reset |  | H |
| 3 | pwr_key | active_high; AXP PWRON -> BSS138 -> SYS_OUT; ext0 wake; light-sleep wake | H |
| 4 | display_qspi_d0 |  | H |
| 5 | display_qspi_d1 |  | H |
| 6 | display_qspi_d2 |  | H |
| 7 | display_qspi_d3 |  | H |
| 8 | i2s_dout | ES8311 DAC | H |
| 9 | i2s_bclk |  | H |
| 10 | i2s_din | ES7210 ADC | H |
| 11 | touch_int | active_low; light-sleep wake | H |
| 12 | display_cs |  | H |
| 13 | display_te | routed, unused | M |
| 14 | i2c_scl |  | H |
| 15 | i2c_sda |  | H |
| 16 | i2s_mclk |  | H |
| 21 | imu_int1 | QMI8658 INT1 (wake-on-motion); INT2 not connected. Hardware WoM validated (plan 2b.1, core d562880); light-sleep wake | H |
| 38 | display_qspi_clk |  | H |
| 45 | i2s_ws |  | H |
| 46 | speaker_pa | active_high | H |

#### Wrist Assist: Waveshare ESP32-S3-Touch-AMOLED-2.06 (`waveshare_s3_206`)

| GPIO | Role | Notes | Conf. |
|---:|---|---|:---:|
| 0 | boot_button | active_low; light-sleep wake | H |
| 1 | sd_cmd | unused | M |
| 2 | sd_clk | unused | M |
| 3 | sd_d0 | unused | M |
| 4 | display_qspi_d0 |  | H |
| 5 | display_qspi_d1 |  | H |
| 6 | display_qspi_d2 |  | H |
| 7 | display_qspi_d3 |  | H |
| 8 | display_reset |  | H |
| 9 | touch_reset | pulsed at boot by the HAL | H |
| 10 | pwr_key | active_high; light-sleep wake | H |
| 11 | display_qspi_clk |  | H |
| 12 | display_cs |  | H |
| 13 | display_te | routed, unused | M |
| 14 | i2c_scl |  | H |
| 15 | i2c_sda |  | H |
| 16 | i2s_mclk |  | H |
| 17 | sd_cs | unused | M |
| 18 | haptic_motor | active_high; MMBT3904 driver, ALDO3 supply; unused | M |
| 21 | imu_int1 | QMI8658 INT1 (wake-on-motion). Hardware WoM validated (plan 2b.1, core d562880); light-sleep wake | H |
| 38 | touch_int | active_low; light-sleep wake | H |
| 39 | rtc_int | PCF85063 INT, unused | M |
| 40 | i2s_dout |  | H |
| 41 | i2s_bclk |  | H |
| 42 | i2s_din |  | H |
| 45 | i2s_ws |  | H |
| 46 | speaker_pa | active_high | H |

#### Mobile Assistant: Waveshare ESP32-S3-Touch-AMOLED-2.16 (`waveshare_s3_216`)

| GPIO | Role | Notes | Conf. |
|---:|---|---|:---:|
| 0 | boot_button | active_low; unused | M |
| 1 | sd_cmd | unused | M |
| 2 | sd_clk | unused | M |
| 3 | sd_d0 | unused | M |
| 4 | display_qspi_d0 |  | H |
| 5 | display_qspi_d1 |  | H |
| 6 | display_qspi_d2 |  | H |
| 7 | display_qspi_d3 |  | H |
| 8 | i2s_dout |  | H |
| 9 | i2s_bclk |  | H |
| 10 | i2s_din |  | H |
| 11 | touch_int | active_low; light-sleep wake | H |
| 12 | display_cs |  | H |
| 13 | rtc_int | PCF85063 INT, unused | M |
| 14 | i2c_scl |  | H |
| 15 | i2c_sda |  | H |
| 16 | pwr_key | active_high; light-sleep wake | H |
| 17 | imu_int1 | QMI8658 INT1; wake-on-motion events seen in [PROF:IMU]; light-sleep wake | H |
| 18 | key3_button | active_low; light-sleep wake | H |
| 21 | imu_int2 | unused | M |
| 38 | display_qspi_clk |  | H |
| 39 | display_reset |  | H |
| 40 | touch_reset |  | H |
| 41 | sd_cs | unused | M |
| 42 | i2s_mclk |  | H |
| 45 | i2s_ws |  | H |
| 46 | speaker_pa | active_high | H |

<!-- END GENERATED: tools/check_boards.py -->
