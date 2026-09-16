# Changelog

All notable changes to the ESP8266 String Art Indexer are recorded here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [1.5.0] — 2026-09-16

### Fixed

- **Resume after a power cut came back one nail behind.** `saveState()` was
  called immediately after `beginMoveToStep()`, at which point `currentStep`
  still held the position the disc was leaving, not the one it was heading to.
  The move then completed without saving again, so the file permanently
  described the previous nail. State is now written twice per nail: once when a
  move starts, recording the target and that a move is in flight, and once when
  it lands, recording the position actually reached.

### Added

- **Interrupted-move detection.** If power was cut mid-move, the position is
  restored as the intended target but flagged unverified, and a banner in the
  Wrap section asks for a home before carrying on. Progress is kept either way.
  Any homing operation clears the flag.

- **Find home on power-up** (Advanced → Motor, off by default). With the limit
  switch fitted, the machine homes against it at boot and drives back to the
  saved nail, recovering an absolute reference without you touching anything.
  It deliberately does not start running again — thread may be loose, and
  starting an unattended machine on power-up is a bad default.

- **Go to step #** in the Wrap section: jumps to a position in the sequence and
  takes progress with it, so the wrap continues from there. New `gotostep`
  action on `/action`. It shows the valid range and where you currently are.

- **Go to nail #** promoted out of the collapsed panel into the Wrap section
  alongside it. This one moves the disc without changing progress — the two are
  now labelled so the difference is obvious. Both accept Enter.

- `positionKnown` and `autoHomeOnBoot` in the `/status` JSON; `autoHomeOnBoot`
  persisted as an eleventh line in `/config.txt`.

- A resume line on the serial console at boot, reporting the step, the disc
  position, and whether it is verified.

### Notes

- The state file gained two lines. Files written by earlier firmware still load
  — the missing fields fall back to "no move in flight", which is correct for
  anything that was sitting idle.

- Two small LittleFS writes per nail. The filesystem wear-levels these, but on
  a several-thousand-chord piece it is a reason not to run the dwell down to
  near zero.

---

## [1.4.1] — 2026-09-15

### Fixed

- **Sketch failed to compile**, reporting `expected constructor, destructor, or
  type conversion before '(' token` against a line of minified JavaScript
  inside the web page.

  The Arduino build does not hand your `.ino` to the compiler directly. It
  preprocesses it first — concatenating files, inserting `#include <Arduino.h>`,
  scanning for function definitions and injecting generated prototypes. That
  scan does not reliably understand C++11 raw string literals, and on a literal
  approaching 100 KB it can cut the string short. Everything past the cut is
  then parsed as C++, and the first thing it hits is the vendored Cropper.js
  bundle.

  The literal itself is valid: it compiles cleanly under g++ on its own. The
  corruption happens in Arduino's preprocessing step, which only ever runs on
  `.ino` files.

### Changed

- **The web page moved to `web_page.h`.** Arduino does not preprocess `.h`
  files, so the raw string is now out of reach of the step that was breaking
  it. The sketch is still one sketch: keep `web_page.h` next to the `.ino` in
  the same folder and it compiles and flashes in one click as before. Do not
  paste the page back into the `.ino`.

- **All function prototypes are now declared explicitly** near the top of the
  sketch. Arduino normally generates these by scanning the `.ino`; declaring
  them by hand costs nothing and means the build no longer depends on that scan
  succeeding.

- The `.ino` dropped from about 2160 lines to about 760, which makes the
  firmware logic considerably easier to navigate.

---

## [1.4.0] — 2026-09-15

### Fixed

- **Settings appeared not to take effect.** `/status` is polled once a second
  and its reply was written straight into every input, so a field being typed
  in got reset mid-keystroke. A longer settle time entered by hand was wiped
  before it could be saved. Fields now go "dirty" on the first keystroke and
  are left alone until saved or reset, and a focused field is never written to.

### Changed

- **Default settle raised from 250 ms to 800 ms, recover from 200 ms to
  400 ms.** A quarter-second settle is close enough to instant that the servo
  still appeared to fire the moment the nail arrived. The default feed cycle is
  now 1.50 s (0.80 settle + 0.30 pulse + 0.40 recover).

- **All persistent settings collected into one "Advanced settings" section**,
  grouped as Motor, Feed cycle and Feeder servo angles, with a single Save.
  They were previously split across two collapsible panels in different
  sections with separate save buttons. The Wrap section keeps only "Jump to a
  nail"; the Feed section keeps only the auto-feed toggle and Feed now.

- Boot defaults are now named constants (`DEF_FEEDER_SETTLE_MS` and friends) at
  the top of the sketch rather than literals scattered through the code.

### Added

- **Restore defaults** button, handled server-side via `reset=1` on `/config`
  so the page never carries its own copy of the default values.

- **Live feed-cycle breakdown** that updates as you type, before saving:
  settle → pulse → recover, the total feed time, the dwell, and the resulting
  seconds per nail. Warns when settle is under 0.3 s.

- The compiled-in defaults are published in the `/status` JSON under
  `defaults`.

- Unsaved changes are marked with an asterisk on the Save button, and saving
  or resetting reports success or failure inline.

---

## [1.3.0] — 2026-09-15

### Fixed

- **Nail count had to be set in three places** — the Design slider, the base
  template designer, and the machine settings field — and nothing kept them
  in agreement. They are now three views of one value (`window.NailCount`):
  editing any one updates the others immediately and saves to the machine
  after a short pause. A `/status` poll arriving mid-edit no longer yanks the
  field back, because values from the machine are ignored while a local edit
  is unsaved.

- **Feeds could be skipped entirely on a zero-length move.** The feed was
  triggered by the move-completion event, but `beginMoveToStep()` leaves
  `stepping` false when a move works out to zero steps — a sequence repeating
  a nail, or two nails rounding to the same step. The feed never fired and
  auto mode stalled waiting on a dwell that was never restarted.
  `armFeedAfterMove()` now handles that case directly.

### Changed

- **The feeder is a four-phase cycle instead of an immediate pulse:**
  settle → pulse → recover → idle. The servo now waits `feederSettleMs`
  (default 250 ms) after the nail arrives before grabbing the thread, so it
  isn't feeding into a disc that is still ringing, and waits
  `feederRecoverMs` (default 200 ms) after returning to rest before the disc
  is allowed to move again.

- **Auto-advance waits for the feeder,** not just the stepper. A move can no
  longer start while thread is being paid out.

- **The auto-advance interval now means dwell after the feed**, measured from
  the moment the feed cycle completes, rather than an interval that overlapped
  the move and the feed.

- Next / Prev / Goto are refused while a feed is in progress rather than
  dragging thread out of the servo's grip.

- The Design section's nail control is relabelled "Nails" and its range
  widened from 60–400 step 4 to 24–720 step 1, so it can reach every count the
  base designer can offer.

### Added

- `feederSettleMs` and `feederRecoverMs` settings, persisted to `/config.txt`
  (two more appended lines; older files still load) and editable under feeder
  settings, with a readout of the resulting per-nail cycle time.

- `feederSettleMs`, `feederRecoverMs` and `feederBusy` in the `/status` JSON;
  the progress line shows "feeding thread…" while a cycle runs.

### Removed

- The base designer's "Use this nail count" button — the count syncs on its
  own now. The field shows "saving…" or "on machine" instead.

---

## [1.2.0] — 2026-09-15

### Added

- **Base template designer**, merged into the machine's own page as section
  *4 · Base template*. Draws the numbered nail ring for the physical base,
  with live preview, real-size SVG export, optional laser-cut circle, centre
  drill mark and red nail dots. Adapted from StringArt-CircleBase-Design by
  Chanchal Sakarde.

- **Motor-synchronised nail layout.** The designer reads the live motor
  profile from `/status` instead of hardcoding a step count, so a template can
  never be printed for a configuration the firmware isn't running. It reports
  steps per nail, degrees per nail, nail pitch in mm, and — when the count
  doesn't divide the step grid evenly — the worst-case placement error in both
  degrees and millimetres at the chosen radius.

- **Suggested nail counts.** When the chosen count doesn't divide evenly, the
  designer offers the nearest counts that do, filtered to ones that still
  leave at least 3 mm between nails at the current radius.

- **Warnings for unbuildable rings**: fewer than 2 steps between neighbouring
  nails (the indexer cannot separate them), or a nail pitch under 3 mm.

- **"Use this nail count"** pushes the designer's count straight to the
  machine's `numNails` setting, so the template, the chord generator and the
  indexer cannot drift apart.

- `internalSteps`, `halfStep`, `gearRatioX100000` and `stepsPerRevX100` added
  to the `/status` JSON.

### Changed

- **Motor geometry is now a single derived profile.** `MOTOR_INTERNAL_STEPS`,
  `MOTOR_HALF_STEP` and `MOTOR_GEAR_RATIO_X100000` are the only inputs;
  `STEPS_PER_REV_X100` and `STEPS_PER_REV` are computed from them at compile
  time via `constexpr`. Nothing else in the firmware hardcodes a step count.

- **Default gear ratio changed from 63.68395:1 back to 64.00000:1**, giving
  2048 full-steps / 4096 half-steps per revolution as specified. This makes
  counts like 64, 128 and 256 land exactly on step boundaries. The measured
  tooth-count ratio is kept in the source as a one-line alternative, together
  with a ten-revolution test for deciding which one your motor actually is.
  See "Which gear ratio" in the README — at 4096 half-steps the worst-case
  *within-revolution* rounding error is only ±0.044°, so the choice matters
  for accumulated full-revolution error, not for individual nail placement.

### Notes

- The designer places nail 0 at three o'clock with numbering increasing
  clockwise, matching `pinPositions()` in the chord generator and the Python
  generator. Physical nail index, generated sequence and indexer position all
  refer to the same nail.

- The page grew by roughly 9 KB of PROGMEM. On a 4 MB ESP8266 there is ample
  room; on a 1 MB module check the build size after flashing.



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
