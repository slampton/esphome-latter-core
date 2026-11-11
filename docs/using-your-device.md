# Using Your Device

## Getting around

- **Home:** swipe up from the bottom edge, or tap the pill at the bottom of the screen.
- **Home dock:** tap an app. If more than four apps are enabled, swipe the dock sideways. The dock's apps and their order are set in **Studio → Devices → Display & Home**; **Settings → Home** on the device shows or hides Music, Alarms, Area, Remote and Games and saves the change to Studio too. Settings is always in the dock. Until Home Assistant has sent the dock, the device shows Music, Alarms, Area, Remote, Games and Tools.
- **Activity card:** above the dock, Home shows a card with running timers, the stopwatch, the music queue, upcoming alarms and the next calendar event. Two items at a time on rectangular screens, one on round ones; when there are more, they rotate every 8 seconds. Tap a line to open it, or use its button. The card and each of its sources can be turned off in **Studio → Devices → Display & Home**.
- **Apps with modes:** Weather, Area Control, and Whole-Home Hub have a mode bar along the bottom. Tap a mode, or swipe sideways in the lower part of the screen. In Area Control, swiping on the cards moves the sliders, and swiping below the cards changes the category.
- **Button:**
  - On battery boards, a short press of the crown wakes the screen, turns it off, snoozes an alarm, or dismisses a notification, depending on what's showing.
  - A long press starts a 5-second hibernate countdown. With **Sleep & Wake: Hold Crown Power Off** on, it hibernates immediately instead.

## Apps

| App | Highlights |
| :--- | :--- |
| **Clock** | Minimal Digital, Modern Roman (analog), Modern Digital (Bold) |
| **Home** | Clock, date, weather, today's agenda, wallpaper, and the app dock |
| **Music** | Now playing, a seek bar with multi-tap skip (−10 s to +10 min), speaker picker, transfer to another speaker, join or unjoin speaker groups, a library of favorites, playlists, artists, albums, podcasts, radio, and audiobooks, the queue, lyrics, and bios |
| **Area Control** | 3 cards per page with fill sliders. Categories: Scenes, Lights, Blinds & Curtains, Fans, Switches, Climate, Media, All Devices. Open a light's detail view for a color wheel or a 2700–6500 K slider. |
| **Whole-Home Hub** | Tabs for Alarm, Thermostat, Garage & Locks, and Vacuum. Opening actions (unlocking locks, disarming alarms, opening garage/gate/door covers—including doorbell unlock) obey the device's **Perimeter Security Policy** in Studio (`Auto`, `Allow`, `Require PIN` via an on-device keypad, or `Block`), while locking, arming, and closing covers remain one-tap. |
| **Remote** | D-pad, Home, Back, volume, and a device list with master power. Up to 10 remotes. |
| **Sky & Weather** | Current conditions, 5-day forecast, sun, and moon. Animated effects: clouds, fog, 3-layer rain, snow, storms with lightning, and the occasional meteor at night. |
| **Tools** | Timers (3), alarms (3, with daily, weekday, weekend, or one-time repeats and a 5-minute snooze; on Remote Assistant, scheduled alarms run while USB-docked), stopwatch, calculator, calendar (agenda, quick-add, event map with zoom and pan), intercom, camera (carousel of auto-refreshing snapshots, about 1–2 fps, doorbell call), dice, coin flip, flashlight, and a level (IMU boards only) |
| **Games** | Marbles, Archery, Bricks, Chomp, 2048, Memory Match. On IMU boards the first four are tilt-controlled, with touch-drag as a fallback. On boards without an IMU, touch steering is always used. |
| **Settings** | Device status, sleep and wake, display, audio, voice, and firmware update |

## Voice

The wake word runs on the device with [micro_wake_word](https://github.com/kahrendt/microWakeWord). Speech recognition and replies go through your Home Assistant Assist pipeline.

- **Wake words:** Okay Nabu (the default), Hey Jarvis, Hey Mycroft, Alexa, and Hey Luna. Up to two can be active at once. Choose them with Home Assistant's **Wake word** and **Wake word 2** selects on the device (Assist satellite settings) or in Settings > Voice on the device; both change the same setting, and the device keeps it across restarts.
- **When it listens:** controlled by **Voice: Wake Word Mode**: `Docked Only` (the default on battery devices), `Always On`, or `Disabled`. The wake word is paused while this device plays media or a ringing alarm or timer, during voice replies, games, a doorbell call, OTA updates, and while Wi-Fi is down. (There is no on-device echo cancellation yet, so it can't listen over its own speaker.)
- **Music during voice:** music pauses while you speak and resumes after the reply, unless you asked it to stop.
- **Visual styles:** 5 animations for the voice overlay: Nebula, Solaris, Rings, Light Bar, and Dot Wave.

## Power profiles

**Sleep & Wake: Power Profile** sets several sleep and wake options at once. Changing any one of them by hand switches the profile to `Custom`.

| Profile | Wake word | Tap to wake | Pickup to wake | Screen timeout on battery | Screen timeout when docked | Hibernate after |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Portable Carry | Docked only | Docked only | Docked only | 30 s | 1 min | 15 min |
| Handheld Roaming | Docked only | Always | Always | 30 s | 1 min | 30 min |
| Wearable Watch | Docked only | Always | Always | 15 s | 1 min | 15 min |
| Tabletop Dock | Docked only | Docked only | Off | 30 s | 5 min | 15 min |
| Always-On Wall / Desk | Always | Always | Off | Never | 5 min | Off |

A device doesn't hibernate while it's on USB power or while music, a timer, a voice session, a game, or a call is active.

How the wake inputs behave when the screen is off:

- **Pickup to wake** is a raise gesture: a deliberate movement followed by the screen settling roughly face-up for a moment. Arm swing, walking, and pocket jostling don't wake the screen.
- **Tap to wake** on a watch running on battery needs a **double tap**, so sleeve and skin contact don't wake it. On USB power, and on every other device, a single tap wakes it.
- **Animated weather** on the Home screen runs continuously on USB power. On battery it animates for 10 seconds after you wake or touch the screen, then holds still until the next touch.
- **Light sleep (ESP32-S3 battery devices).** With the screen off on battery and nothing playing, listening or ringing, the main loop stretches to `5,000 ms` and the processor sleeps between Wi-Fi modem-sleep intervals (`WIFI_PS_MAX_MODEM`) and timer ticks (`~5–8 mA` average on home Wi-Fi; `~2–4 mA` off-site once exponential Wi-Fi scan backoff engages and the BLE beacon pauses). It stays connected to Home Assistant, and timers and alarms still fire on time. Hardware GPIO interrupts from the crown, the action button, a tap (when Tap to Wake works on battery) and a pickup (when Pickup to Wake works on battery) wake the loop immediately; commands from Home Assistant can take a fraction of a second longer to arrive than with the screen on.

Whether music follows the device when you pick another Area on it (or Bluetooth moves it) is a Latter setting: **Studio → Devices → Audio & Voice → Media Follows This Device to Its New Area**.

---

## Battery

Battery devices use a single-cell 3.7 V LiPo. Every battery board handles it the same way. Only the cell's capacity differs.

- **Charging.** Plug in USB-C. The charger runs at about half the cell's capacity per hour (0.5C, with a `200 mA` minimum floor and up to `500 mA`—folding back automatically if the PMIC die temperature reaches `65 °C`), then holds 4.20 V until the current tapers to termination. **Battery Charge State** reads `Charging`, `Full`, `Not Charging` (on USB, charger paused) or `Discharging`. A typical charge from empty takes 2 to 2.5 hours.
- **Battery Level** blends the AXP2101 hardware fuel gauge (where fitted) with resting-voltage estimation: Latter compensates for the voltage drop caused by the screen or speaker, smooths the reading over about a minute, and maps it across per-capacity (`400`, `500`, and `1000+` mAh) LiPo OCV/internal-resistance curves measured from full cell discharges. On battery the level never goes up. While charging it never goes down and stops at 99 %. It reads 100 % only once the charger reports the charge is complete, and holds 100 % until you unplug.
- **Low battery.** At 10 % the **Battery Low** entity turns on, and the device shows a "Battery Low" notice the next time its screen is on. It never wakes the screen just to show the notice.
- **Automatic power-off.** When the cell reaches empty (3.40 V at rest, or 3.30 V under load) and stays there for 30 seconds—or when `Remote Assistant` (`P4 3.5`) enters scheduled/critical battery power-off (since its `K1` button wires to AXP2101 `PWRON` rather than an RTC GPIO)—the device powers down cleanly: it saves state, disconnects from Home Assistant and switches the AXP2101 power rails off, so the cell is never run flat. A short voltage dip from Wi-Fi or the speaker doesn't trigger it. Plug in USB or press the power button (`crown` / `K1`) to turn it back on. (`Remote Assistant` rings scheduled alarms when docked on USB-C; on S3 battery boards, armed alarms wake from light/hibernation sleep unless the cell is critically empty.) If the cell is still past empty when it starts, it switches straight back off before the screen and Wi-Fi come up; charge it first.
- The level reflects the cell as fitted. Older or damaged cells sag more under load and reach the power-off point sooner.

---

## Settings exposed to Home Assistant

These appear as entities on the ESPHome device. Most of them can also be changed in Studio.

| Group | Entities |
| :--- | :--- |
| **Display** | Clock Face, Time Format, Start Screen, Color Profile, Auto Brightness Mode (Disabled, Sun Synchronized, Time Schedule), Day / Night / Manual Brightness, Day Start Hour, Night Start Hour, and the Display light |
| **Home** | Weather Background, Wake Greeting (the dock and the activity card are Latter settings, not entities) |
| **Sleep & Wake** | Power Profile, Screensaver Style (Ambient Clock, Aurora, Starfield, Digital Rain, Fireworks), Battery Screen Timeout, Docked Idle Timeout, Battery Hibernate Timeout, Pickup to Wake, Tap to Wake, Docked Screensaver, Hold Crown Power Off (to never hibernate, set Battery Hibernate Timeout to Disabled) |
| **Voice** | Wake Word Mode, a sensitivity setting for each wake word, Visual Profile, Wake Screen on Assist (the wake words themselves are Home Assistant's Assist satellite Wake word selects) |
| **Music** | Album Art Style, Overlay Style, Album / Artist Album / Podcast sort orders, Speaker Shortcut and Library Shortcut |
| **Sound** | Volume Step Size, Alert Volume, Game Sound Effects |
| **Area** | Default Category, Show All Category, BLE Presence Beacon (off out of the box; Latter turns it on for devices with BLE presence tracking), Presence Beacon Mode |
| **Weather** | Condition Simulation, which forces any of 16 conditions so you can preview them |
| **Actions** | Trigger Voice Assistant, Find My Device, Screensaver Now, Hibernate Now (battery boards), Restart Device |
| **Status** | Battery Level, Battery Voltage, Battery Charging, Battery Charge State, Battery Low and USB Power Connected (battery boards only), Timer Active, Timer Remaining, Alarm Ringing, Wi-Fi Signal, IP Address, Firmware Version, Hardware Manifest |
| **Diagnostics** | System: RPC Outbox (the bound Zero-Privilege RPC channel read by Latter; leave enabled). System: Telemetry Profiler, which logs per-frame render times, touch, audio, power, and memory. System: Internal RAM Low Water, Internal RAM Largest Block and PSRAM Low Water, which Home Assistant keeps in history. System: Link Report, a line of Wi-Fi, Home Assistant and battery-sleep counters (beacon timeouts, disconnects, signal, longest time without Home Assistant, share of time in light sleep and held awake, what woke the device, and on battery boards the battery voltage, level, charge state and average drain) refreshed every 15 minutes |

### Logs

At the default `INFO` log level the device is quiet once it has booted. Taps, gestures, frames, voice-turn steps, Bluetooth beacon cycles and wake word pauses log at `DEBUG`, and `[PROF:*]` diagnostics only while the profiler is on. These lines still log at `INFO`:

- the boot summary: SDK revision, display buffers, `[PROF:BOOT]` with the reset reason, crash flag and previous power-off, and charger and light-sleep setup. They are replayed to every log viewer that connects, so a viewer opened after a restart still shows why the device restarted.
- real events: wake word detected, Assist timers started, cancelled or finished, alarms and timers firing, hibernate countdown and power-off, Home Assistant client changes.
- warnings and errors, always.

To see more:

1. Turn on **System: Telemetry Profiler**: `[PROF:*]` lines for frames, touch, audio, power, battery, memory, light sleep and wake sources, until you turn it off. Turn it off afterwards: it adds log traffic and CPU time.
2. For everything, set `logger:` `level: DEBUG` in the device's configuration in ESPHome Device Builder and install. `DEBUG` lines are compiled out of the default `INFO` build, so a runtime log-level change can't bring them back.

Crash details survive a restart: the next boot's `[PROF:BOOT]` line and ESPHome's crash report show them, and Latter raises a "Device restarted after a crash" repair in Home Assistant.

### Reset to defaults

To put every setting above back to its default, open the device in Latter Studio and choose **Tools → Danger Zone → Reset**, or call the `latter.reset_device` service. The device erases its saved settings, including the Wi-Fi driver's cached network data and RF calibration, then restarts. Wi-Fi and pairing are compiled into the firmware, so it rejoins Home Assistant on its own and stays bound to Latter. A second checkbox also resets Latter's options for that device.
