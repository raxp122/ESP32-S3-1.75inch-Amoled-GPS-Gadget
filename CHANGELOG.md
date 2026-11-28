# Changelog

Every pull request adds its changes under "Next". At the merge into `main` the section takes the
number of the published release (`0.1.<build>`).

## Next

- Tarot app: 78 round cards in a neon style (22 major arcana drawn one by one, the minor arcana
  with their suit signs), one card or a three-card spread, reversed cards, meanings in Italian
  and English, shake to draw. Secular names for II, V, XV, XX.
- Asset packs: app files (images, texts) are downloaded onto the microSD from the release the
  first time an app needs them (`main/packs.*`, format and reader in `core.*` with tests,
  generators in `tools/packs/`, the CI attaches `*.amp` to every release).
- Home: the apps are in alphabetical order of their names in the current language (the home
  still opens on Pendant); the shortcut setting goes through them in the same order.
- Upload page: every JPEG (photos, GIF and video frames, previews) is made by a small baseline
  encoder in the page instead of the browser's: GIFs uploaded from an iPhone (Safari's encoder)
  showed blue bands on the pendant. Checked against a reference decoder and the pendant's
  decoder (esp_new_jpeg in QEMU).
- Battery: a fuel gauge stuck near 0 % (not learned the battery yet) is always replaced by the
  voltage estimate, even on a half-empty cell.
- Settings › Battery › Power off below: never, 3 % (default) up to 50 %. Under it, without a
  charger, for two checks in a row, a screen says the pendant switches off in 30 s to protect
  the battery, with a button straight to the setting (one more minute to choose). Plugging in
  the charger or lowering the threshold calls it off.
- Pendant, battery: with the screen off nothing is read or decoded and the player checks only
  every 150 ms; animations run at full CPU speed only while reading and decoding each frame
  (the wait until the next frame is at the low clock / light sleep); a still image is checked
  every 100 ms instead of 40. Locked with PWR, the IMU is not read and the UI housekeeping
  timer runs once a second instead of five times.
- Pendant: *Content* setting (images and videos / images only / GIFs and videos only) and a
  slideshow interval for each (default 10 s, 60 s, 20 s). BOOT's next media follows the content
  too; a media opened from the gallery is still shown as chosen.
- Calendar: the rings now turn with the bezel (later days, months and years sit
  counter-clockwise, so turning clockwise brings them to the window); before they ran the
  other way. Swipe down now moves the active ring inwards (days → months → years), as the
  window reads from top to bottom; swipe up goes back out.

- Settings › Pendant › Plays: *Single media* (BOOT for the next one) or *Slideshow* (every media
  in turn, after the interval; an animation always plays to the end of a loop first). Before,
  only the gallery could switch it (tap / double tap), so the interval seemed to do nothing.
- Calendar app: days, months and years on three concentric rings with a window at 12 o'clock,
  the bezel turns the active ring (months and years follow), swipe changes it, tap on the date
  opens the day page, long press goes back to today. Italian national holidays (Easter computed,
  4 October from 2026) as non-working days, ISO week, day of the year, days from today.
- A 17 px font for small labels; calendar icon.
- `ROADMAP.md`: the apps decided, the ideas and the known issues.

- Battery: a "~" before the percentage (clock, quick panel, Settings › Battery, Info) while it
  is estimated from the voltage because the fuel gauge has not learned the battery yet; the
  gauge takes over again once it is within 15 points of the voltage.
- Battery: a new battery is learned again by itself. Changing the capacity in Settings, or
  plugging a battery in while on USB, restarts the PMU fuel gauge (the voltage is used until it
  agrees again).
- Clock: stays on (Settings › Screen › Clock always on, on by default). Untouched for the
  auto-off time it dims to a third of the brightness (a sixth at night), the seconds ring stops
  and the Wi-Fi sleeps; a touch or a button brings it back. A night window set to "screen off"
  still turns it off. The text moves a few pixels every minute against burn-in. Touch only
  wakes it from the dimmed state (no swipe back, no quick panel): BOOT leaves.

- Battery: when the PMU fuel gauge is far from the voltage (a new battery it has not learned
  yet showed 0 % at 4.09 V) the percentage comes from the voltage; warnings and the
  low-battery power off use the corrected value.
- Home: a fixed shortcut at 6 o'clock between the two horizon strokes, any app of the ring
  (Settings by default, Settings › Screen › Shortcut at the bottom). The ring now fades out a
  little earlier to leave it room.
- Menus: a name too long for the circle scrolls after resting on it for 1.5 s.
- Upload page: the name field is read-only until tapped (iOS kept opening the keyboard);
  a new file clears the crop view first; videos are started right away so Safari loads them
  (the previous GIF stayed on screen until play was pressed).

## 0.1.15


- Upload page: Delete asks for confirmation inside the page (tap twice); the browser's own
  dialogs never showed in the mini browser of the pendant's hotspot, so Delete did nothing.
- Upload page: no focused field is left behind after the PIN (the keyboard kept opening);
  "Done" closes the keyboard on the name field.

## 0.1.12


- Display: windows sent to the panel always start on an even row. An odd chunk height made
  the CO5300 shift the next window and left two rows of old pixels on screen (the dotted line
  through texts on the home screen, gallery and dice).
- Dice: new gestures: swipe left / right = die type, bezel = number of dice, swipe up / down =
  roll mode, BOOT leaves the app.
- Dice: Daggerheart duality roll (2d12 Hope and Fear, with Hope / with Fear, doubles are a
  critical, +d6 / -d6 for advantage / disadvantage, modifier).

## 0.1.9

- Battery: settings for the capacity (charge current at ~0.5 C) and charge to 90 %; warnings
  at 15 % and 7 %, power off at 3 %; PMU cut-off at 3.2 V.
- Power saving: CPU frequency scaling (80-240 MHz) with automatic light sleep, panel sleep with
  the screen off, LVGL tick without a 2 ms timer, slower input polling when only the buttons
  matter, Wi-Fi paused in pendant mode and with the screen off. Lower default brightness and
  menu timeout.
- Launcher: endless ring with a horizon at the bottom where apps fade out and come back from
  the other side; "App list" setting to loop or stop at the ends. The icons follow the finger
  (no more jump of the app on the back of the ring).
- No LVGL transforms, whole-object opacity or screen fades any more (extra layers: they tore on
  the panel and cost CPU): gallery previews are made round and small once in memory, the die
  rattles instead of spinning.
- Dice: the bezel follows the finger along the die types.
- Upload page: GIFs and videos play while you crop them, the whole picture is visible with the
  part outside the circle dimmed; GIFs are decoded by the browser itself when it can, and the
  built-in decoder no longer paints missing pixels of a damaged frame (half-coloured frames).


## 0.1.6

- Dice app for D&D: d4 to d100 on the bezel, up to 20 dice, modifier, advantage /
  disadvantage, natural 20 / natural 1 highlighted, tap or shake to roll, saved rolls,
  history of the last 30 rolls, ability scores (4d6 drop the lowest, with modifiers).

## 0.1.3

First version of AmuletOS.

- Launcher for the round screen: apps on a ring along the edge, turned with a virtual bezel.
- Gestures: swipes, tap, double tap, long press, bezel, pull-down quick panel; BOOT and PWR with
  hold rings (BOOT 2 s home / leave pendant mode, PWR 5 s power off).
- Pendant mode: one media in a loop or a slideshow, touch off, screen-off timer, wake on movement
  (IMU), night mode (dim or off), separate brightness.
- Gallery with round previews; show, slideshow from here, delete.
- Upload page served by the pendant (PIN protected): crop to the circle, GIF and video
  conversion to AMJ in the browser, previews, delete. Own hotspot when there is no Wi-Fi.
- Wi-Fi: setup from the phone (hotspot + captive page) or with the round on-screen keyboard.
- Clock (NTP + RTC), Info, Settings (screen rotation for the case, accent colours, Italian /
  English, time zone).
- OTA updates from the GitHub releases with automatic rollback; CI builds, tests, releases and
  publishes the web installer.
- Tools: screen simulator with layout check, mock device for the web pages, native tests.
