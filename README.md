# ESP8266 String Art Indexer

A single-file Arduino sketch that turns an ESP8266, a 28BYJ-48 stepper and a
disc of nails into a string art machine you drive from your phone.

The motor's only job is to **index**: bring the next nail in the sequence to a
fixed feeder on the frame so you can wrap the thread, then advance to the next
one. There is no gantry and no thread tensioner to build.

The same sketch also serves the **generator** — upload a photo from the phone,
crop it, solve for the chord sequence in the browser, and send the result
straight to the machine. Nothing else to install, no desktop tooling, no
second device.

---

## Contents

- [Hardware](#hardware)
- [Wiring](#wiring)
- [Flashing](#flashing)
- [First run](#first-run)
- [Using it](#using-it)
- [Machine settings](#machine-settings)
- [Base template designer](#base-template-designer)
- [Power cuts](#power-cuts)
- [Rotation direction](#rotation-direction)
- [Which gear ratio](#which-gear-ratio)
- [HTTP API](#http-api)
- [Files on flash](#files-on-flash)
- [Troubleshooting](#troubleshooting)

---

## Hardware

| Part | Notes |
|---|---|
| ESP8266 board | NodeMCU 1.0 / Wemos D1 mini or similar |
| 28BYJ-48 stepper | 5 V unipolar, the usual blue one |
| ULN2003 driver board | Ships with the motor |
| External 5 V supply | 1 A or better. **Do not** run the motor from the ESP8266's regulator |
| SG90 servo | *Optional* — thread feeder |
| Microswitch | *Optional* — home position sensor |
| Nail disc | 360 nails on the circumference, mounted on the motor shaft |

The disc rotates; the feeder does not. That arrangement matters — see
[Rotation direction](#rotation-direction).

---

## Wiring

```
ULN2003 IN1  ->  D1  (GPIO5)
ULN2003 IN2  ->  D2  (GPIO4)
ULN2003 IN3  ->  D5  (GPIO14)
ULN2003 IN4  ->  D6  (GPIO12)
ULN2003 GND  ->  ESP8266 GND          (common ground, required)
ULN2003  +   ->  external 5 V

SG90 signal  ->  D3  (GPIO0)
SG90 V+      ->  external 5 V
SG90 GND     ->  common ground

Limit switch ->  D7  (GPIO13)  and  GND
```

**Share ground, not power.** Both the stepper and the servo draw current
spikes well past what the ESP8266's onboard regulator can supply while WiFi is
transmitting. Feed them from the external supply and tie all grounds together.

D0, D4 and D8 are avoided because they are boot-strapping pins. D7 has no
boot-time role, which makes it safe for a switch that might be closed at
power-on. D3 must read HIGH at reset, but it is driven as an output here, so
nothing pulls it low before the sketch starts.

The limit switch uses the internal pull-up — it just needs to short D7 to
ground when the disc's home mark passes. No external resistor.

---

## Flashing

The sketch is two files. Keep them together in a folder named after the `.ino`:

```
esp8266_string_art_standalone/
    esp8266_string_art_standalone.ino    firmware
    web_page.h                           the web UI, one PROGMEM string
```

`web_page.h` is separate for a reason. Arduino preprocesses `.ino` files before
compiling — inserting includes and generated prototypes — and that step does
not reliably handle a ~100 KB C++11 raw string literal. When it cuts the string
short, the browser code inside gets compiled as C++ and you get
`expected constructor, destructor, or type conversion before '(' token`
pointing at minified JavaScript. Arduino does not preprocess `.h` files, so
keeping the page there avoids it. **Do not paste it back into the `.ino`.**

1. **Board Manager** → install *esp8266* by ESP8266 Community.
2. **Tools → Board** → e.g. *NodeMCU 1.0 (ESP-12E Module)*.
3. **Tools → Flash Size** → any option that includes a LittleFS partition.
4. Edit `WIFI_SSID` and `WIFI_PASSWORD` near the top of the sketch.
5. Upload.

No extra libraries. `ESP8266WiFi`, `ESP8266WebServer`, `ESP8266mDNS`,
`LittleFS` and `Servo` all ship with the ESP8266 core.

---

## First run

Open the serial monitor at 115200 to see the assigned IP, then browse to it.
`http://stringart.local` works too where mDNS is supported.

If the WiFi credentials fail, the board falls back to an open access point
called **StringArtCNC** — join it and browse to `192.168.4.1`.

Before wrapping anything:

1. Line the disc's nail 0 up with the feeder and press **Set Home** — or press
   **Find Home** if you fitted the limit switch.
2. Open *Jump to nail # / machine settings* and set **Number of nails** to
   match what the generator used.
3. Press **Run direction test** and confirm the nail arriving at the feeder is
   the nail named on screen.

---

## Using it

**Generate.** Upload a photo, crop it to the circle, pick nail count, chord
count and line weight, then let it solve. The preview builds up as it runs.
Chord count is the main quality dial — more chords means a darker, more
detailed result and a much longer wrap.

**Print a card.** *Greeting card (A4 PDF)* puts the photo and the finished art
on one A4 landscape sheet, folded down the middle — art and title on the cover,
photo and your message inside. Print at 100% scale, fit-to-page off. The PDF is
written in the page itself rather than with a library, so it works with no
internet connection.

**Send.** Push the sequence to the machine, or download the step list as text
if you want to wrap by hand from a printout.

**Wrap.** The big number is the nail currently at the feeder. Loop the thread
around it, press **Next**, repeat. **Start Auto** advances on a timer instead,
which works once you have a rhythm.

Under the progress bar you get elapsed time, time remaining, the clock time it
will finish at, and the measured seconds per nail. These come from timing real
nail-to-nail cycles rather than adding up the configured phase times — disc
travel varies with how far apart consecutive nails are, so the configured cycle
always reads low. It shows "measuring" until it has enough samples, and ignores
anything over two minutes so a pause does not skew it.

Progress is saved twice per nail — when a move starts and when it lands — so a
power cut costs you nothing. See [Power cuts](#power-cuts). The disc de-energises between moves — it will not hold
position against a hard pull, but it also will not cook itself or buzz.

---

## Machine settings

| Setting | What it does |
|---|---|
| Number of nails | Shared with the Design slider and the base designer — set it anywhere |
| Step delay (ms/half-step) | Lower is faster. Too low stalls the motor — 2 ms is a sane floor, raise it if the disc is heavy |
| Auto-advance interval | Dwell time per nail in Auto mode |
| Reverse rotation direction | See below |

Feeder servo settings live in their own panel. All settings persist to flash.

### Nail count is shared

The Design slider, the base template field and the machine settings field are
three views of one number. Change it in any of them and the other two follow,
then it saves to the machine a moment after you stop typing. There is nothing
to keep in sync by hand, and no way to print a base for one count while the
indexer runs another.

### The wrap cycle

A feeder that pushes out and pulls back along the same line cannot hook a nail
— that path encloses nothing, so the thread touches the nail and lets go. The
guide has to trace a closed loop around it, and with one servo axis the disc
supplies half of that loop.

| Phase | What moves | Default |
|---|---|---|
| Approach | disc, to the approach offset | — |
| Settle | nothing — let the disc stop ringing | 800 ms |
| Tube in | servo slews to the feed position | 900 ms |
| Wait | let the thread seat | 400 ms |
| Sweep | disc, one nail pitch, tube still there | — |
| Wait | let the thread hook | 400 ms |
| Tube out | servo slews back | 900 ms |
| Recover | settle before moving on | 400 ms |
| Land | disc onto the nail | — |

The servo is walked to its target a couple of degrees at a time rather than
commanded straight there — `Servo.write()` has no speed parameter, so an SG90
slams to position otherwise. Servo speed and both waits are adjustable.

The loop is handed off the direction the disc travels to reach each nail: it
overshoots past the nail the way it is already going, sweeps back across it,
then lands on it. That matters because the thread trails behind the direction of
travel, and the tube has to pass on the far side from it. Signing the loop with
a fixed constant instead wraps correctly one way round the disc and drops the
thread the other way.

The overshoot defaults to half a nail pitch each way, which puts both ring
crossings exactly midway between nails. **Test one wrap** in Advanced → Wrap
cycle runs a single cycle so you can tune it without starting a run. If wraps
shed, try *Approach from the other side* first — it reverses the handedness.

Three things have to be true mechanically or no timing will save it:

- The tube tip must end up **outside the nail ring** when the servo is out.
- It must cross at **shank height, below the nail heads**.
- The thread needs **upstream tension** — a brake on the spool. Slack thread
  falls off a nail however well it is wrapped.

### The timings

Each phase is timed by:

| Phase | Default | What it is for |
|---|---|---|
| Settle | 800 ms | The disc is still ringing the instant a move ends. Feeding into that snatches the thread |
| Pulse | 300 ms | Servo at the feed angle, paying out thread |
| Recover | 400 ms | Servo back at rest, thread stops swinging before the disc moves |
| Dwell | `autoMs` | Your wrapping time, measured *after* the feed completes |

All four live under **Advanced settings → Timings**, with a breakdown that
updates as you type. Fields you are editing are never overwritten by the
status poll, so changes hold until you press Save.

Auto-advance waits for the whole cycle, so the disc never starts turning with
thread still being fed, and Next / Prev are refused mid-feed. The panel shows
the resulting per-nail cycle time as you adjust the numbers.

If the thread still snatches, raise Settle first — it is almost always the
disc not having stopped rather than the servo being too quick. Watch the disc
and count: whatever looks like "stopped" is usually about twice as long as you
think. **Restore defaults** puts everything back if you lose the thread of it.

---

## Rotation direction

The nails move and the feeder stays put, so **to present nail N the disc has
to turn backwards by N**. Nail N sits N steps ahead of the feeder and must
travel back to reach it.

Get that sense wrong and the machine presents nail `numNails − N` instead:
ask for 258 and 102 turns up, ask for 109 and 251 turns up. The giveaway is
that every error is an exact mirror rather than a random offset.

`dirSign` handles this. It defaults to `-1`, which is correct for the standard
arrangement, and the **Reverse rotation direction** toggle flips it live
without reflashing. Re-home after changing it.

**If the wrapping indexes correctly but the finished picture comes out
left-right flipped**, that is not this setting. The generator is numbering
nails the opposite way round the canvas from your physical frame. Fix it in
the generator's nail layout — flipping `dirSign` back to compensate would
break indexing all over again.

---

## Power cuts

The disc holds its position mechanically when the coils are off, so a power cut
does not move anything. What matters is whether the firmware still knows where
it is, and there are two cases.

**Cut while idle between nails** — the common one. The saved position is exact.
It picks up from the same step with nothing to do.

**Cut during a move** — the disc stopped somewhere between two nails and there
is no way to tell where without a reference. The firmware restores the position
it was heading for, flags it unverified, and the Wrap section shows a banner
asking you to home before carrying on. Progress is kept in both cases.

To recover the reference:

- **With a limit switch**, turn on *Find home on power-up* (Advanced → Motor).
  The machine homes at boot and drives back to the saved nail by itself. It
  will not start wrapping again on its own.
- **Without one**, line nail 0 up with the feeder and press Set Home. Progress
  is untouched, so press Next and it carries on from the right chord.

If you lose your place some other way, **Go to step #** puts you anywhere in
the sequence and takes progress with it.

---

## When the wrong nail turns up

On a long run the nail arriving at the feeder eventually stops matching the one
the machine names. There are two different causes and they need different fixes.

**A constant offset** — it is out by the same amount every time. Open
*Calibrate the nail position* in the Wrap section, jog until the right nail
lines up, and tell it which one that is. Fixed for good; your place in the
sequence is untouched.

**A creep that grows** — a little further out every revolution. That is the
steps-per-turn figure being wrong, not steps being lost. Spin ten turns, report
how many nails past the start it finished, and the machine corrects itself. Ten
turns is enough to measure a 0.05% error.

**Random wandering** that comes back after correcting both — those are missed
steps, and the fixes are mechanical: raise the step delay, take mass off the
disc, check nothing binds. *Re-home every N nails* keeps a long run on track,
but it papers over the problem rather than curing it.

---

## Which gear ratio

All motor geometry comes from three constants at the top of the sketch:

```cpp
constexpr long MOTOR_INTERNAL_STEPS     = 32;        // full steps per internal rev
constexpr bool MOTOR_HALF_STEP          = true;      // this firmware half-steps
constexpr long MOTOR_GEAR_RATIO_X100000 = 6400000L;  // 64.00000 : 1
```

Steps per revolution is derived from them, so nothing else hardcodes a number.
As shipped that is **32 × 2 × 64 = 4096 half-steps** (2048 full-steps).

There are two candidate ratios for a 28BYJ-48:

| Ratio | Half-steps/rev | Notes |
|---|---|---|
| `6400000` — 64.00000:1 | 4096 | The figure printed on the motor. Counts like 64, 128 and 256 divide evenly |
| `6368395` — 63.68395:1 | 4075.77 | What you get from the actual gear teeth: 32/9 × 22/11 × 26/9 × 31/10 |

Units differ, so measure rather than assume. Set nail count to 1, home the
disc, mark the frame, then `goto` nail 0 ten times — ten full revolutions.
If the mark returns true, keep 64:1. If it has crept about 18° round, switch
to `6368395` and re-home.

The distinction only matters for error that accumulates over whole
revolutions. Within one revolution, rounding a nail to the nearest half-step
costs at most ±0.044°, which is 0.15 mm at a 200 mm radius — smaller than the
nail itself.

---

## Base template designer

Section **4 · Base template** on the machine's page draws the numbered nail
ring for the physical base: live preview, real-size SVG export, optional
laser-cut circle, centre drill mark, and a red dot at every nail position.

It reads the motor profile from the machine rather than assuming one, and
reports what your chosen count actually costs:

- steps per nail, degrees per nail, and nail pitch in mm
- whether the count divides the step grid evenly
- worst-case placement error in degrees and mm when it doesn't
- the nearest counts that *do* divide evenly, as one-tap buttons
- warnings when nails would be closer than 3 mm, or fewer than 2 steps apart

**Use this nail count** pushes the value straight to the machine's `numNails`
setting, so the printed base, the chord generator and the indexer stay in
agreement.

### Printing it

Download the SVG and print at **100% scale** with "fit to page" turned off —
the file carries real millimetre dimensions, so any scaling silently ruins the
nail spacing. The panel names the smallest A-series and US sheet that fits.

Nail **0 sits at three o'clock and numbering increases clockwise** when viewed
from the printed side. This matches the chord generator and the Python
generator, so a nail index means the same physical nail throughout. Glue the
template printed-side up, and align nail 0 with the feeder before homing.

---

## HTTP API

All endpoints are CORS-enabled so a separately hosted generator page can talk
to the machine directly.

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | The control page |
| `GET` | `/status` | JSON: current/next nail, progress, config, switch state, `dirSign` |
| `POST` | `/upload` | Body is the sequence, one nail index per line |
| `POST` | `/config` | Form-encoded: `numNails`, `stepDelay`, `autoMs`, `dirSign`, `feeder*` |
| `POST` | `/action` | Form-encoded `cmd=`: `next`, `prev`, `home`, `findhome`, `goto` (with `value`), `feed`, `start`, `stop` |

```bash
curl "http://stringart.local/status"
curl -X POST "http://stringart.local/action" -d "cmd=goto&value=258"
```

---

## Files on flash

| Path | Contents |
|---|---|
| `/sequence.csv` | The nail sequence, one index per line |
| `/state.txt` | Current index and absolute step position |
| `/config.txt` | Machine and feeder settings, one value per line |

Config files written by older firmware load fine — missing lines fall back to
the compiled-in default rather than reading as zero.

---

## Troubleshooting

**The wrong nail arrives, and the error is always `numNails − requested`.**
Direction. Flip *Reverse rotation direction*, then re-home.

**The motor hums or vibrates but does not turn.** Step delay is too low for
the load, or a coil wire is off. Raise the step delay to 3–5 ms first. If it
still will not move, check the ULN2003's four LEDs chase in sequence.

**Positions drift slowly over a long wrap.** Almost always missed steps rather
than maths — the 28BYJ-48 has very little torque above about 15 rpm and the
drive is open-loop, so it cannot detect a skip. Raise the step delay, reduce
the disc's inertia, and fit the limit switch so you can re-home mid-piece.

**The disc creeps between nails.** The coils de-energise when idle by design.
If thread tension is dragging it off position, reduce tension at the spool or
add a light detent to the disc.

**The page loads but buttons do nothing.** Check the serial monitor for the
IP — a stale bookmark after a DHCP change is the usual cause.

**The board is on `192.168.4.1` and not your network.** WiFi association
failed and it fell back to AP mode. Re-check the SSID and password; the
ESP8266 is 2.4 GHz only and will not see a 5 GHz network.

**Find Home never completes.** The seek gives up after 1.25 revolutions and
raises an error. Confirm the switch shorts D7 to ground when triggered — the
status line shows the live switch state, so you can press the lever by hand
and watch it change.

---

## Credits

Chanchal Sakarde. All Copy Rights Reserved. 
