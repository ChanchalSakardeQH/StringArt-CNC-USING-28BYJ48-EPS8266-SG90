# Changelog

All notable changes to the ESP8266 String Art Indexer are recorded here.

---

## [1.1.0] — 2026-09-14

The indexer was presenting the wrong nail. Asking for nail 258 brought nail
102 to the feeder; 109 brought 251; 263 brought 97; 179 brought 181. Every
reading came out as `numNails − requested`, which is the signature of a
mirrored rotation rather than a step-count error.

### Fixed

- **Rotation direction (the bug).** The nails ride on the rotating disc and
  the feeder is fixed, so to present nail N the disc must turn *backwards* by
  N — nail N starts N steps ahead of the feeder and has to travel back to it.
  The old `nailToStep()` turned forwards, so nail `numNails − N` arrived
  instead. A new `dirSign` constant, defaulting to `-1`, applies the correct
  sense.

- **Steps per revolution.** `STEPS_PER_REV` was 4096, which assumes a 64:1
  gearbox. The 28BYJ-48's real gear ratio is 63.68395:1, giving
  64 × 63.68395 = **4075.77** half-steps per output revolution. The old value
  placed every nail roughly 0.5% too far round — about 1.8° of accumulated
  error by the time the disc returned to nail 0, enough to visibly smear the
  last chords of a long sequence. Nail-to-step conversion now uses the exact
  figure held as a scaled integer (`STEPS_PER_REV_X100 = 407577`), so there is
  no floating-point maths on the ESP8266 and no drift.

- **Position tracker overflow.** `currentStep` was never wrapped after a move
  completed (`targetStep = currentStep + delta` could run past one revolution
  or below zero), so it grew without bound across a long run and was written
  to `/state.txt` that way. It is now normalised to `0 … STEPS_PER_REV−1`
  whenever a move finishes, and on load, so rounding cannot accumulate across
  thousands of chords.

- **Stale saved state.** `loadState()` normalises the restored step position,
  so a `/state.txt` written by 1.0.0 (in 4096-step units, possibly unbounded)
  no longer puts the disc in a nonsensical place on first boot after upgrade.

### Added

- **Reverse-direction toggle** in the web UI under *Jump to nail # / machine
  settings*. Flips `dirSign` live and persists it to `/config.txt` — no
  reflash needed to correct a mirrored machine.

- **Direction self-test** button. Sends the disc to nail 0, ¼, ½, ¾ and back
  to 0, naming each stop on screen as it goes, so the nail actually arriving
  at the feeder can be checked against the nail requested.

- `dirSign` exposed in the `/status` JSON and accepted by `POST /config`.

### Changed

- `/config.txt` gained an eighth line holding `dirSign`. Files written by
  1.0.0 have seven lines; `readConfigLine()` already returns the caller's
  default for missing lines, so older config files load cleanly and pick up
  `dirSign = -1`.

### Upgrade notes

1. Flash the sketch. Existing `/sequence.csv`, `/config.txt` and `/state.txt`
   on LittleFS are preserved and remain readable.
2. Run **Find Home** (or **Set Home**) once — the step scale changed from 4096
   to 4076 units per revolution, so any previously saved position is no longer
   meaningful.
3. Run the **direction test**. If the nail arriving does not match the nail
   named, flip *Reverse rotation direction* and re-home.

### Not a firmware issue

If the wrapping indexes correctly but the finished artwork comes out
left-right flipped, the generator is numbering nails the opposite way round
the canvas from the physical frame. Fix it in the generator's nail layout —
flipping `dirSign` back would break indexing again to compensate for an
unrelated mirror.

---

## [1.0.0] — Initial release

### Added

- Web-controlled nail indexer for a 28BYJ-48 stepper driven through a ULN2003
  board, with Next / Prev / Auto advance and live progress.
- Browser-based string art generator on the same page: photo upload, crop via
  Cropper.js, chord solver, live preview, downloadable step list.
- Direct upload of a generated sequence to the machine over `POST /upload`,
  stored on LittleFS at `/sequence.csv`.
- Optional SG90 feeder servo with adjustable rest angle, feed angle and pulse
  length; fires on demand or automatically after each advance.
- Optional limit switch for physically verified homing, with a 1.25-revolution
  safety cap and an error flag when the switch never trips.
- Non-blocking stepper, servo and homing routines so the web server stays
  responsive throughout.
- Persistent sequence, machine config and progress across reboots.
- WiFi station mode with softAP fallback (`StringArtCNC`) and mDNS at
  `stringart.local`.
