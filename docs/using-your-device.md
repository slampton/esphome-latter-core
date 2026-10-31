# Using Your Device

## Getting around

- **Home:** swipe up from the bottom edge, or tap the pill at the bottom of the screen.
- **Home dock:** tap an app. If more than four apps are enabled, swipe the dock sideways. The dock's apps and their order are set in **Studio → Devices → Display & Home**.
- **Apps with modes:** Weather, Area Control, and Whole-Home Hub have a mode bar along the bottom. Tap a mode, or swipe sideways in the lower part of the screen. In Area Control, swiping on the cards moves the sliders, and swiping below the cards changes the category.
- **Button:**
  - On battery boards, a short press of the crown wakes the screen, turns it off, snoozes an alarm, or dismisses a notification, depending on what's showing.
  - A long press starts a 5-second hibernate countdown. With **Sleep & Wake: Hold Crown Power Off** on, it hibernates immediately instead.

## Apps

| App | Highlights |
| :--- | :--- |
| **Clock** | Minimal Digital, Modern Roman (analog; round screens only), Modern Digital (Bold) |
| **Home** | Clock, date, weather, today's agenda, wallpaper, and the app dock |
| **Music** | Now playing, a seek bar with multi-tap skip (−10 s to +10 min), speaker picker, transfer to another speaker, join or unjoin speaker groups, a library of favorites, playlists, artists, albums, podcasts, radio, and audiobooks, the queue, lyrics, and bios |
| **Area Control** | 3 cards per page with fill sliders. Categories: Scenes, Lights, Blinds & Curtains, Fans, Switches, Climate, Media, All Devices. Open a light's detail view for a color wheel or a 2700–6500 K slider. |
| **Whole-Home Hub** | Tabs for Alarm, Thermostat, Garage & Locks, and Vacuum |
| **Remote** | D-pad, Home, Back, volume, and a device list with master power. Up to 10 remotes. |
| **Sky & Weather** | Current conditions, 5-day forecast, sun, and moon. Animated effects: clouds, fog, 3-layer rain, snow, storms with lightning, and the occasional meteor at night. |
| **Tools** | Timers (3), alarms (3, with daily, weekday, weekend, or one-time repeats and a 5-minute snooze), stopwatch, calculator, calendar (agenda, quick-add, event map with zoom and pan), intercom, camera (carousel, live snapshot, doorbell call), dice, coin flip, flashlight, and a level (IMU boards only) |
| **Games** | Marbles, Archery, Breakout, Chomp, 2048, Memory Match. On IMU boards the first four are tilt-controlled, with touch-drag as a fallback. On boards without an IMU, touch steering is always used. |
| **Settings** | Device status, sleep and wake, display, audio, voice, and firmware update |

## Voice

The wake word runs on the device with [micro_wake_word](https://github.com/kahrendt/microWakeWord). Speech recognition and replies go through your Home Assistant Assist pipeline.

- **Wake words:** Okay Nabu (the default), Hey Jarvis, Hey Mycroft, Alexa, and Hey Luna. Up to two can be active at once.
- **When it listens:** controlled by **Voice: Wake Word Mode**: `Docked Only` (the default on battery devices), `Always On`, or `Disabled`. The wake word is paused during music playback, games, the camera, and OTA updates.
- **Music during voice:** music pauses while you speak and resumes after the reply, unless you asked it to stop.
- **Visual styles:** 5 animations for the voice overlay: Nebula, Solaris, Jarvis, Alexa, and Gemini.

## Power profiles

**Sleep & Wake: Power Profile** sets several sleep and wake options at once. Changing any one of them by hand switches the profile to `Custom`.

| Profile | Wake word | Tap to wake | Pickup to wake | Screen timeout on battery | Screen timeout when docked | Hibernate after |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| Pocket Carry | Docked only | Docked only | Docked only | 30 s | 1 min | 15 min |
| Handheld Roaming | Docked only | Always | Always | 30 s | 1 min | 30 min |
| Wearable Watch | Docked only | Always | Always | 15 s | 1 min | 15 min |
| Living Room Tabletop | Docked only | Docked only | Off | 30 s | 5 min | 15 min |
| Always-On Wall / Desk | Always | Always | Off | Never | 5 min | Off |

A device doesn't hibernate while it's on USB power, while **Stay Awake** is on, or while music, a timer, a voice session, a game, or a call is active.

---

## Settings exposed to Home Assistant

These appear as entities on the ESPHome device. Most of them can also be changed in Studio.

| Group | Entities |
| :--- | :--- |
| **Display** | Clock Face, Time Format, Start Screen, Color Profile, Auto Brightness Mode (Disabled, Sun Synchronized, Time Schedule), Day / Night / Manual Brightness, Day Start Hour, Night Start Hour, and the Display light |
| **Home** | Weather Background, Wake Greeting, dock shortcuts for each app |
| **Sleep & Wake** | Power Profile, Screensaver Style (Ambient Clock, Aurora Borealis, Starfield, Matrix Rain, Fireworks), Battery Screen Timeout, Docked Idle Timeout, Battery Hibernate Timeout, Pickup to Wake, Tap to Wake, Docked Screensaver, Stay Awake, Hold Crown Power Off |
| **Voice** | Wake Word Mode, Active Wake Word, a sensitivity setting for each wake word, Visual Profile, Wake Screen on Assist |
| **Music** | Album Art Style, Overlay Style, Album / Artist Album / Podcast sort orders, Speaker Shortcut and Library Shortcut |
| **Sound** | Volume Step Size, Alert Volume, Game Sound Effects |
| **Area** | Default Category, Media Handoff, Show All Category, BLE Presence Beacon |
| **Weather** | Condition Simulation, which forces any of 16 conditions so you can preview them |
| **Actions** | Trigger Voice Assistant, Find My Device, Screensaver Now, Hibernate Now (battery boards), Restart Device |
| **Status** | Battery Level, Battery Voltage, Battery Charging, USB Power Connected, Timer Active, Timer Remaining, Alarm Ringing, Wi-Fi Signal, IP Address, Firmware Version, Hardware Manifest |
| **Diagnostics** | System: Telemetry Profiler, which logs per-frame render times, touch, audio, power, and memory |
