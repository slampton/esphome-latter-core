# Using Your Device

## Getting Around

- **Go Home:** swipe up from the bottom edge (the bottom home pill indicates when swipe-up-to-Home is active).
  - Tapping the **clock readout** on Home opens the **Time** app by default (configurable in **Studio → Devices → Interface → Home Time Tap** or **Settings → Home** on the device to open either **Time App** or the zero-chrome **Clock Face**).
  - Tapping the **Area icon badge** (shown on BLE-roaming devices) opens **Area Control** directly.
- **App Dock & Single-App Mode:** tap an app in the dock, or swipe the dock sideways when more than four apps are enabled. Dock apps and their order (`Music`, `Home Hub`, `Area`, `Remote`, `Weather`, `Time`, `Cameras`, `Games`, `Tools`, with `Settings` always last) are configured in **Studio → Devices → Interface** (**Settings → Home** on the device also provides quick toggles for `Time`, `Music`, `Area`, `Remote`, `Games`, and `Tools`). Turning **Enable Home Screen** off in Studio locks the device into single-app kiosk mode on your chosen **Start Screen**.
- **Adaptive Activity Card:** above the dock, Home surfaces active items—live doorbell/intercom calls, running timers, snoozed or upcoming (`<= 6 h`) alarms, running stopwatch, active/recently paused music, and the next calendar event. It collapses to **0 rows** when idle, expands to **1 row** for a single item (and on round screens), and shows **2 rows** on rectangular screens when two or more items are active, rotating secondary items every 8 s while pinning urgent items (calls, timers, snoozed alarms, stopwatch, alarms within 2 h) on top.
- **Quick Actions:** swipe down from the top edge on any non-game screen (or press and hold the physical **Crown** / **K1** button for **1 second** on battery boards) to open the **Quick Actions** overlay—with live **Speaker Volume** and context-aware **Brightness** sliders (automatically adjusting Manual, Auto Day/Night, or Bedtime brightness), a **Bedtime Mode** toggle, and one-tap **Hibernate** (battery boards) or **Screensaver** / **Screen Off** (mains panels). Dismiss by tapping **< Back**, swiping up from the bottom edge, or short-pressing the Crown.
- **Mode Bars:** **Time**, **Area Control**, **Home Hub**, and **Sky & Weather** show a mode bar along the bottom—tap a tab or swipe sideways below the content cards.
- **Physical Button (`Crown` / `K1`):**
  - Short press wakes the screen, dismisses **Quick Actions**, puts an idle screen to sleep, snoozes a ringing alarm, or dismisses an alert banner.
  - Press and hold (**1 s**) opens **Quick Actions** (with one-tap **Hibernate**). Holding the power key for ~6 s triggers the hardware AXP2101 power-off fallback, and idle battery devices continue to auto-hibernate on their configured **Hibernate After** countdown.

---

## Apps

| App | Highlights |
| :--- | :--- |
| **Clock** | Three zero-chrome SDF faces (**Minimal Digital**, **Modern Analog** on round panels, **Hybrid Digital**) with anti-aliased hands and smooth 20 FPS sweep on USB power (and for 15 s after wake/touch on battery). Tap anywhere to return Home. |
| **Home** | Clock, date, roaming Area icon badge, live weather sky or `720p` wallpaper, adaptive `0/1/2`-row Activity Card, and app dock |
| **Time** | 4-tab app: **Clock** (`HH:MM:SS`, optional custom **Local Clock Label**, local time zone & DST status, optional `UTC` card, and up to 4 custom-city **World Clocks**), **Timers** (3 local slots with `+1M` quick-extend + Assist timers), **Alarms** (3 recurring NVS slots with 1-minute precision, daily/weekday/weekend/once schedules, 5-minute snooze, and 2-way Studio sync), and **Stopwatch** (lap splits + `START` / `LAP` / `RESET`) |
| **Music** | Now Playing, seek bar with multi-tap skip (−10 s to +10 min), speaker picker, seamless Music Assistant queue transfer, speaker group join/unjoin, library browser (favorites, playlists, artists, albums, podcasts, radio, audiobooks), queue, lyrics, and Wikipedia bios |
| **Area Control** | Paginated fill-slider cards (`3`, `5`, or `6` per page depending on screen geometry) across ordered categories: Scenes, Lights (HSV color wheel + 2700–6500 K slider), Blinds & Curtains, Fans, Switches, Climate, Media, and optional All Devices |
| **Home Hub** | 6-tab whole-home deck: **Alarm**, **Cameras** (up to 10 snapshots, fullscreen/landscape toggle, doorbell listen/reply/unlock), **Intercom** (broadcast or call another display), **Thermostat**, **Entry & Garage** (door lock + garage cover), and **Vacuum**. Alarm arming/disarming prompts a PIN whenever your Home Assistant alarm panel requires one (`alarm_caps`), while locks, garage covers, and code-less alarms follow the device's **Perimeter Security Policy** (`Auto`, `Allow`, `Require PIN`, or `Block`). |
| **Remote** | Tactile D-pad, Home, Back, transport, volume, and power across up to 10 TVs, streaming boxes, or AV receivers |
| **Sky & Weather** | Current conditions, 5-day forecast, solar arc, and 3D lunar phase over animated clouds, fog, rain, snow, plasma-bolt storms, tumbling leaves, and night shooting stars |
| **Tools** | Calculator, Calendar (agenda, quick-add, zoomable OpenStreetMap event pins), Dice, Coin Flip, and Flashlight on all boards, plus Bubble Level (IMU boards) and classic Analog Chronograph (round displays)—5 to 7 utilities per board |
| **Games** | Marbles, Archery, Bricks, Chomp, 2048, and Memory Match (tilt-steered with touch fallback on IMU boards; touch-steered elsewhere) |
| **Settings** | On-device status, Home quick toggles, sleep & wake, display/theme, audio, voice, and firmware update—synced 2-way with Latter Studio |

---

## Voice Assistant

On-device wake-word detection runs via [microWakeWord](https://github.com/kahrendt/microWakeWord); speech-to-text, intent handling, and replies run through your Home Assistant Assist pipeline.

- **Wake words:** *Okay Nabu* (default), *Hey Jarvis*, *Hey Mycroft*, *Alexa*, and *Hey Luna* (up to two active simultaneously, selected on the device's Home Assistant Assist satellite or in **Studio → Devices → Audio**).
- **Wake Word Mode:** `Docked Only` (default on battery boards), `Always On`, or `Disabled`. Wake-word listening pauses automatically during local speaker playback, ringing alarms/timers, active voice replies, games, doorbell streams, OTA updates, and Wi-Fi disconnects.
- **Media preemption & visual styles:** music pauses synchronously when a voice turn begins and resumes once both Assist and announcement buffers drain (and fades out cleanly if left paused for 5 minutes). Choose from 5 reactive overlays: *Nebula*, *Solaris*, *Rings*, *Light Bar*, and *Dot Wave*.

---

## Device Groups, Sleep & Battery

Each device seeds its initial sleep, wake, and idle settings from its hardware traits on cold boot and adopts its matching **Device Group** in **Latter Studio** (editing any operational setting switches the device's group to `Custom`, which you can save as a named group).

| Built-in Device Group | Wake Word | Tap to Wake | Pickup to Wake | Battery Screen Timeout | Docked Idle Timeout | Hibernate After |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Pocket Carry** | Docked Only | Docked Only | Docked Only | 30 s | 1 min | 15 min |
| **Handhelds** | Docked Only | Single Tap | Always On | 30 s | 1 min | 30 min |
| **Wearables** | Docked Only | Double Tap | Always On | 15 s | 1 min | 15 min |
| **Wall & Desk Panels** | Always On | Single Tap | Disabled | Never | 5 min | Disabled |

- **Wake gestures & battery weather motion:** Pickup to wake requires a deliberate raise-and-settle gesture; Wrist Assist requires a **double tap** on battery (single tap when docked and on all other boards) to prevent sleeve wakes. On battery, the Home weather sky animates for **35 seconds** after wake or touch, then fades precipitation out over **800 ms** into a 0 FPS cached sky frame.
- **Screen-off quiet & light sleep:** On ESP32-S3 battery boards, screen-off quiet stretches the main loop to **`1,000 ms`** (**`250 ms`** with touch wake armed), pauses the raw-HCI BLE beacon in `< 1 ms` (staying active on USB when `BLE Beacon Mode` is `Always`), and enters automatic FreeRTOS light sleep in `WIFI_PS_MAX_MODEM` (`~5–8 mA` on home Wi-Fi; `~2–4 mA` off-site with exponential scan backoff). GPIO interrupts from buttons, touch, or IMU wake the loop immediately, and armed alarms/timers still fire on time. ESP32-P4 battery boards (`Remote Assistant`) keep fixed `CPLL` clocks and pace the quiet loop at `1,000 / 250 ms`.
- **Charging & cell protection:** Single-cell 3.7 V LiPo boards charge at `<= 0.5C` (`200 mA` floor, `500 mA` cap, with automatic `65 °C` die-temperature foldback) and estimate state-of-charge from load-compensated OCV curves blended with the AXP2101 fuel gauge. At `10%`, **Battery Low** asserts; at sustained empty (`3.40 V` resting / `3.30 V` loaded for 30 s), the device saves a `HibernateSnapshot` to NVS, disconnects cleanly, and powers off the AXP2101 rails (`Remote Assistant` runs scheduled wake alarms while USB-docked, since `K1` wakes AXP2101 `PWRON` rather than LP deep sleep).

---

## Home Assistant Entities, Studio Sync & Logs

Instead of cluttering Home Assistant with ~45 ESPHome configuration entities, Latter uses a **3-tier sleep-safe model**:

1. **Latter Studio (`device_prefs` 2-Way Sync):** All 49 device preferences (display, clock, dock, categories, brightness, wake-word sensitivities, sort orders, alarms, World Clocks) live in **Latter Studio** and sync two-way with NVS—even allowing you to configure a handheld while it is asleep or hibernating.
2. **Automation Entities on the `Latter` Device:** Sleep-safe controls and state entities for Home Assistant automations—`Device Group` (`select`), `Wake Word Mode` (`select`), `Alert Volume` (`number`), `Find My Device` (`button`), `Activate Screensaver` (`button`), `Timer Active` & `Alarm Ringing` (`binary_sensor`), `Timer Remaining` (`sensor`), and BLE `Current Area` / `BLE Distance` / `device_tracker`. → [Entities reference](https://github.com/slampton/ha-latter/blob/main/docs/reference/entities.md)
3. **Hardware & Diagnostic Entities on the `ESPHome` Device:**
   - **Media, Voice & Firmware:** `Music` (Sendspin `media_player`), Assist satellite + `Wake word` / `Wake word 2` selects, `Display: Screen Backlight`, and `Project Firmware` (`update`, plus `System: C6 Co-Processor Firmware` on ESP32-P4).
   - **Battery & Actions:** `Battery Level`, `Battery Voltage`, `Battery Charging`, `Battery Charge State`, `Battery Low`, `USB Power Connected`, `Actions: Hibernate Device` (battery boards), and `Actions: Restart Device`.
   - **Diagnostics & Telemetry:** `System: RPC Outbox` (bound Zero-Privilege RPC channel—leave enabled), `System: Hardware Manifest`, `System: Link Report` (15-minute Wi-Fi/API/sleep/battery summary), `Wi-Fi Signal`, `IP Address`, `Firmware Version`, `System: Telemetry Profiler` (streams `[PROF:*]` frame, touch, audio, power, and sleep logs while on), `System: Log Memory Report`, and internal RAM / PSRAM low-water sensors.

### Logs & Reset to Defaults
- **Boot & crash forensics:** Default `INFO` builds stay quiet after boot while replaying the boot summary (`SDK rev`, `[PROF:BOOT]` reset reason, crash/rollback flags, charger, and sleep config) to any newly connected log viewer; panics raise a **"Device restarted after a crash"** repair in Home Assistant.
- **Reset to defaults:** Open **Studio → Devices → Tools → Danger Zone → Reset to Defaults** (or call `latter.reset_device`). Erasing device settings wipes NVS and reboots cleanly while keeping compiled-in/provisioned Wi-Fi, API encryption, ESPHome binding, and Music Assistant player links intact.
