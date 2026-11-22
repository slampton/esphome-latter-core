# Using Your Device

## Getting Around

- **Go Home:** swipe up from the bottom edge (the bottom home pill indicates when swipe-up-to-Home is active).
  - Tapping the **clock readout** on Home opens the **Time** app by default (configurable in **Studio → Devices → Interface → Home Time Tap** or **Settings → Home** on the device to open either **Time App** or the zero-chrome **Clock Face**).
  - Tapping the **date readout** on Home opens the **Calendar** app (`Day View` or `Overview`).
- **App Dock & Single-App Mode:** tap an app in the dock, or swipe the dock sideways when more than four items are enabled. Up to **24 shortcuts** (`Pinned`, `Music`, `Home Hub`, `Cameras`, `Area`—displaying your active Area's Home Assistant icon—`Remote`, `Weather`, `Time`, `Calendar`, `Games`, `Tools`, or custom Home Assistant entity actions, with `Settings` always last) are configured in **Studio → Devices → Interface**. Turning **Enable Home Screen** off in Studio locks the device into single-app kiosk mode on your chosen **Start Screen**.
- **Adaptive Activity Card:** above the dock, Home surfaces active items—live doorbell/intercom calls, running timers, snoozed or upcoming (`<= 6 h`) alarms, running stopwatch, active/recently paused music, and today's calendar events. It collapses to **0 rows** when idle, expands to **1 row** for a single item (and on round screens), and shows **2 rows** on rectangular screens when two or more items are active, rotating secondary items every 8 s while pinning urgent items (calls, timers, snoozed alarms, stopwatch, alarms within 2 h) on top.
- **Quick Actions:** swipe down from the top edge on any non-game screen (or press and hold the physical **Crown** / **K1** button for **1 second** on battery boards) to open the **Quick Actions** overlay—with **Speaker Volume** (Row 0), context-aware **Brightness** (Row 1: targeting Manual, Scheduled/Sun Day or Night, volatile Area **Ambient Light** override, or Bedtime brightness), **Bedtime Mode** (Row 2), **Do Not Disturb** (Row 3), and one-tap **Hibernate** (battery boards) or **Screensaver** / **Screen Off** (mains panels) at Row 4. Dismiss by tapping **< Back**, swiping up from the bottom edge, or short-pressing the Crown.
- **Mode Bars:** **Music**, **Sky & Weather**, **Area Control**, **Home Hub**, **Time**, **Calendar**, and **Cameras** show a stadium mode bar along the bottom—tap a tab or swipe sideways.
- **Physical Button (`Crown` / `K1`):**
  - Short press wakes the screen, dismisses **Quick Actions**, puts an idle screen to sleep, snoozes a ringing alarm, or dismisses an alert banner.
  - Press and hold (**1 s**) opens **Quick Actions** (with one-tap **Hibernate**). Holding the power key for ~6 s triggers the hardware AXP2101 power-off fallback, and idle battery devices continue to auto-hibernate on their configured **Hibernate After** countdown.

---

## Apps

| App | Highlights |
| :--- | :--- |
| **Clock** | Three zero-chrome SDF faces (**Minimal Digital**, **Modern Analog** on round panels, **Hybrid Digital**) with anti-aliased hands and smooth 20 FPS sweep on USB power (and for 15 s after wake/touch on battery). Tap anywhere to return Home. |
| **Home** | Clock, date, live weather sky, native Day & Night Sky, or `720p` wallpaper, adaptive `0/1/2`-row Activity Card, and customizable app dock |
| **Time** | 4-tab app: **Clock** (`HH:MM:SS`, optional custom **Local Clock Label**, local time zone & DST status, optional `UTC` card, and up to 4 custom-city **World Clocks**), **Timers** (3 local slots with `+1M` quick-extend + Assist timers), **Alarms** (3 recurring NVS slots with 1-minute precision, daily/weekday/weekend/once schedules, 5-minute snooze, and 2-way Studio sync), and **Stopwatch** (lap splits + `START` / `LAP` / `RESET`) |
| **Music** | Now Playing, seek bar, speaker picker, one-tap Music Assistant queue transfer, speaker group join/unjoin, 4-row library browser (Favorites, Playlists, Artists, Albums, Podcasts, Radio, Audiobooks), queue management, synced LRC / plain lyrics, and Music Assistant artist/album bios (100% local via Music Assistant) |
| **Area Control** | Overview entity cards (left-icon toggle + right-body inspector for Lights with HSV wheel & 2000–6500 K slider, Blinds & Curtains, Fans, and Climate) across ordered categories: Scenes, Lights, Covers, Fans, Switches, Climate, Media, and optional All Devices |
| **Home Hub** | 5-tab whole-home deck: **Alarm**, **Broadcast** (`Speak` inline STT dictation + paginated Quick Phrases), **Thermostat**, **Entry & Garage** (door locks + garage covers), and **Vacuum**. Alarm arming/disarming prompts a `4–12` digit PIN whenever your Home Assistant alarm panel requires one (`alarm_caps`), while locks, garage covers, and code-less alarms follow the device's **Perimeter Security Policy** (`Auto`, `Allow`, `Require PIN`, or `Block`). |
| **Cameras** | 4-tab live MJPEG app: **Grid** (2×2 live wall), **Single** (uncropped feed, 6 s auto-hiding HUD, doorbell `Listen`, PTT `Talk`, `Reply`, and PIN-gated `Unlock`), **Carousel** (auto-rotating cameras), and **Settings** |
| **Remote** | Tactile D-pad (with `480 ms` hold-in-place long-press on `OK`, `Home`, and `Back`), transport, volume, and power across up to 10 TVs, streaming boxes, or AV receivers |
| **Sky & Weather** | Current conditions, 5-day forecast, solar arc, and 3D lunar phase over animated clouds, fog, rain, snow, plasma-bolt storms, tumbling leaves, and night shooting stars |
| **Calendar** | 4-tab agenda app (**Overview**, **Day**, **Week**, **Month**) with event detail, deletion, and `+ Add Event` composer (including voice dictation and Home Assistant `zone.*` location picker) |
| **Tools & Pinned** | Calculator, Dice, Coin Flip, and Flashlight on all boards, plus Bubble Level (IMU boards), classic Analog Chronograph (round displays), and multi-page 2×3 **Pinned** shortcut grids |
| **Games** | Marbles, Archery, Bricks, Chomp, 2048, and Memory Match (tilt-steered with touch fallback on IMU boards; touch-steered elsewhere) |
| **Settings** | On-device status, Home quick toggles, sleep & wake (including Bedtime Mode and Do Not Disturb), display/theme (including Area **Ambient Light** brightness), audio, voice, and firmware update—synced 2-way with Latter Studio |

---

## Voice Assistant

On-device wake-word detection runs via [microWakeWord](https://github.com/kahrendt/microWakeWord); speech-to-text, intent handling, and replies run through your Home Assistant Assist pipeline.

- **Wake words:** *Okay Nabu* (default), *Hey Jarvis*, *Hey Mycroft*, *Alexa*, and *Hey Luna* (up to two active simultaneously, selected on the device's Home Assistant Assist satellite or in **Studio → Devices → Audio**).
- **Wake Word Mode:** `Docked Only` (default on battery boards), `Always On`, or `Disabled`. Wake-word listening pauses automatically during local speaker playback, ringing alarms/timers, active voice replies, games, doorbell streams, OTA updates, and Wi-Fi disconnects.
- **Media preemption & visual styles:** music pauses synchronously when a voice turn begins and resumes once both Assist and announcement buffers drain (and fades out cleanly if left paused for 5 minutes). Choose from 5 reactive overlays: *Nebula*, *Solaris*, *Rings*, *Light Bar*, and *Dot Wave*.

---

## Device Groups, Sleep & Battery

Each device seeds its initial sleep, wake, and idle settings from its hardware traits on cold boot and adopts its matching **Device Group** in **Latter Studio** (toggling live `Bedtime Mode` or `Do Not Disturb` never detaches a device from its Group to `Custom`).

| Built-in Device Group | Wake Word | Tap to Wake | Pickup to Wake | Battery Screen Timeout | Screensaver | Hibernate After |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Pocket Carry** | Docked Only | Docked Only | Disabled | 30 s | Docked Only (1 min) | 15 min |
| **Handhelds** | Docked Only | Single Tap | Always On | 30 s | Docked Only (1 min) | 30 min |
| **Wearables** | Docked Only | Double Tap | Always On | 15 s | Docked Only (1 min) | 15 min |
| **Wall & Desk Panels** | Always On | Single Tap | Disabled | Never | Never | Disabled |

- **Wake gestures & battery weather motion:** Pickup to wake requires a deliberate raise-and-settle gesture; Wrist Assist requires a **double tap** (`35..450 ms`) on battery to prevent sleeve wakes. On battery, the Home weather sky animates for **35 seconds** after wake or touch, then fades precipitation out over **800 ms** into a 0 FPS cached sky frame.
- **Screen-off quiet & light sleep:** On ESP32-S3 battery boards, screen-off quiet stretches the main loop to **`1,000 ms`** (**`250 ms`** with touch wake armed), enters automatic FreeRTOS light sleep (`-78 / -74 dBm` weak-link hold; `~5–8 mA` with BLE paused, `~24–35 mA` with BLE modem-sleep active, `~2–4 mA` off-site with exponential scan backoff), and keeps GPIO button/touch/IMU wakes and local alarms active.
- **Charging & cell protection:** Single-cell 3.7 V LiPo boards charge at `<= 0.5C` (`200 mA` floor, `500 mA` cap, `65 °C / 58 °C` thermal foldback) with IR-compensated capacity-tiered OCV curves, `RTC_NOINIT` SoC persistence, and open-cell `"Battery Fault"` detection.

---

## Home Assistant Entities, Studio Sync & Logs

Instead of cluttering Home Assistant with dozens of internal entities, Latter uses a **clean 3-part surface**:

1. **Latter Studio (`device_prefs` 2-Way Sync):** All 63 device preferences (display, Area Ambient Light, photo pools, 24-item dock, categories, wake-word sensitivities, sort orders, alarms, World Clocks, and live `device_telemetry`) live in **Latter Studio** and sync two-way with NVS—even while a handheld is hibernating.
2. **Curated Entities on the `Latter` Device (8 Platforms):**
   - **Presence & Spatial:** `device_tracker.<slug>`, `sensor.<slug>_current_area`, `sensor.<slug>_current_floor`, `sensor.<slug>_ble_distance` (native HA distance units).
   - **Live Screen & Ambience** *(honest radio availability)*: `light.<slug>_screen` (`on`/`off` + `1–100%` brightness) and `switch.<slug>_screensaver` (two-way screensaver toggle).
   - **Sleep-Safe Policy Controls** *(queued across hibernate)*: `select.<slug>_device_group`, `select.<slug>_wake_word_mode`, `switch.<slug>_bedtime_mode`, `switch.<slug>_do_not_disturb`, `number.<slug>_alert_volume`.
   - **Timers, Alarms & Alerts:** `binary_sensor.<slug>_timer_active`, `sensor.<slug>_timer_remaining`, `binary_sensor.<slug>_alarm_ringing`, `button.<slug>_dismiss_alert`, `button.<slug>_find_my_device`. → [Entities reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/entities.md)
3. **Stock-Clean Hardware Entities on the `ESPHome` Device:**
   - `Music` (Sendspin `media_player`), Assist satellite + `Wake word` / `Wake word 2` selects, `Project Firmware` (`update`, plus `System: C6 Co-Processor Firmware` on ESP32-P4), `Battery` / `Charging` sensors (battery boards), `Wi-Fi Signal`, `IP Address`, `Restart`, and the auto-hidden `System: RPC Outbox` transport pipe.

### Logs & Reset to Defaults
- **Telemetry, boot & crash forensics:** Enable the runtime profiler or inspect heap, PSRAM, and Wi-Fi link stats in **Studio → Devices → Tools → Developer & telemetry** (streamed via `device_telemetry` RPC); panics raise a **"Device restarted after a crash"** repair in Home Assistant.
- **Reset to defaults:** Open **Studio → Devices → Tools → Danger Zone → Reset to Defaults** (or call `latter.reset_device`). Erasing device settings wipes NVS and reboots cleanly while keeping compiled-in/provisioned Wi-Fi, API encryption, ESPHome binding, and Music Assistant player links intact.
