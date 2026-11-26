# Changelog

Every pull request adds its changes under "Next". At the merge into `main` the section takes the
number of the published release (`0.1.<build>`).

## Next

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
