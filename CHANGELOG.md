# Changelog

Every pull request adds its changes under "Next". At the merge into `main` the section takes the
number of the published release (`0.1.<build>`).

## Next

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
