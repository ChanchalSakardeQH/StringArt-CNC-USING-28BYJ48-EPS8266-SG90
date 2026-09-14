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
- [Rotation direction](#rotation-direction)
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

**Send.** Push the sequence to the machine, or download the step list as text
if you want to wrap by hand from a printout.

**Wrap.** The big number is the nail currently at the feeder. Loop the thread
around it, press **Next**, repeat. **Start Auto** advances on a timer instead,
which works once you have a rhythm.

Progress is saved after every advance, so a power cut costs you one nail, not
the whole piece. The disc de-energises between moves — it will not hold
position against a hard pull, but it also will not cook itself or buzz.

---

## Machine settings

| Setting | What it does |
|---|---|
| Number of nails | Must match the generator. Wrong value mis-indexes everything |
| Step delay (ms/half-step) | Lower is faster. Too low stalls the motor — 2 ms is a sane floor, raise it if the disc is heavy |
| Auto-advance interval | Dwell time per nail in Auto mode |
| Reverse rotation direction | See below |

Feeder servo settings — rest angle, feed angle, pulse duration and auto-feed —
live in their own panel. All settings persist to flash.

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

### A note on step counts

One output revolution of a 28BYJ-48 is **4075.77** half-steps, not the 4096
usually quoted. The internal gearbox is 63.68395:1, not 64:1. Using 4096 puts
every nail about 0.5% too far round, which is roughly 1.8° of error by the
time the disc gets back to nail 0 — enough to smear the last chords of a long
sequence. The firmware uses the exact figure in integer arithmetic and snaps
the position tracker back to an absolute target after every move, so nothing
accumulates.

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

## Copy Rights 

Chanchal Sakarde All Rights Reserved 
