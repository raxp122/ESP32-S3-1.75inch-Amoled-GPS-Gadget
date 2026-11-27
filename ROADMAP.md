# Roadmap

What AmuletOS may get next. Nothing here is promised; the order is decided one app at a time.

## Decided

- **Tarot**: major and minor arcana, round cards (find a round deck or draw one for the
  pendant). Shake or tap to draw; single card or small spreads; the meaning on a second page.
  Other oracles (I Ching, runes, Lenormand) could share the app.
- **Timers**: timer, stopwatch, pomodoro and alarms. To decide: a separate app or pages of the
  Clock app.
- **Step counter**: steps of the day from the accelerometer, kept quiet (no badges, no streaks).
- **Aura** (not urgent): liquid animations that react to movement, several kinds and colours, as
  an alternative to photos in pendant mode.
- **Moon & sky**: moon phase, the Moon's sign, next full moon, retrograde planets, all computed
  offline. Turning the bezel moves through the days and the sky moves with it; stopping shows
  the details of that day.
- **Magic: The Gathering life counter**: 2 players, Commander with 4 players (commander damage,
  poison), the circle split in sectors.
- **Bubble level**: like the other gadget, a tap calibrates; flat side only (one face).

- **Calendar** (offline, no account for now): days, weeks, Italian national holidays (fixed ones
  plus Easter and Easter Monday computed), name day of the saint, day of the year, ISO week,
  days until a date or the next holiday. A view made for the circle, e.g. the days of the month
  as a ring along the rim with the bezel as a cursor and the details in the middle, a rolling
  drum of days, and a year ring (the "wheel of the year" idea below).

## Later (needs the GPS)

- **Saved places**: save a spot, distance and direction back to it.
- **Weather**: today and tomorrow on a ring, location from the GPS.

## Ideas

- Card or sentence of the day (from a list uploaded from the phone)
- Decision wheel: options from the phone, spun with the bezel
- Watch faces: analogue, words ("quarter past three"), binary, a single ring
- Golden hour: sunrise, sunset, golden hour for photos
- Countdown to a date, also on the clock
- World clocks: two or three time zones
- Messages from the phone: a small web page sends a sentence or a photo to the pendant
- Bluetooth camera remote (shutter of the phone)
- Bluetooth music control: play/pause, next, volume on the bezel
- iPhone notifications over Bluetooth (ANCS): icon and sender (the biggest one)
- Next calendar event from an .ics link
- Tally counter: knitting rows, reps, people
- Torch: white, or red for the night
- Metronome: visual beat, click on the speaker if the board's audio works well
- Business card: QR of a contact or of the guest Wi-Fi
- Tilt maze with a ball
- Mood diary: one tap a day, a year ring of colours, no statistics

### Round-screen ideas

- **Watching eye**: an eye that fills the circle, the iris follows the movement, it blinks.
- **Sand hourglass**: falling-sand physics, turn the pendant over to restart; also a timer face.
- **Circular pong**: the paddle runs along the rim with the bezel, the ball bounces inside.
- **Finger drawing**: draw on the round canvas, save it as a pendant image.
- **Music ring**: the microphone drives a circular spectrum around the edge.
- **Friend radar**: another AmuletOS nearby over Bluetooth shows up as a blip on a sweeping radar;
  both glow when close.
- **Sigil**: a word becomes a personal sigil drawn inside the circle (and can be worn).
- **Wheel of the year**: the year as a ring with seasons, birthdays and festivals as dots; the
  bezel moves through the months.
- **Bezel combination lock**: unlock the screen by turning the bezel like a safe dial.
- **Sundial**: the shadow of a gnomon for the current time and place.

## Known issues

- Some GIFs show blue bands and blue tints after the upload (gallery and pendant); videos and
  photos are fine. The page's GIF decoder matches a reference decoder exactly, and the pendant's
  JPEG decoder (run in QEMU) decodes the same frames correctly when they are encoded by
  Chromium. Next step: the files produced by Safari on the iPhone.
