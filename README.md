**ESP8266 String Art CNC**

An ESP8266 string art machine you drive from your phone. A 28BYJ-48 stepper turns a disc of nails, an SG90 servo hooks the thread onto each one, and a 0.96" OLED shows where it is in the sequence.

Everything runs on the board itself. The web page it serves turns a photo into a nail sequence, designs a printable base template for the nail ring, sends the sequence to the machine and drives it — with no app, no desktop software and no internet connection.

------

**Contents**

- [Features](#features)
- [Repository layout](#repository-layout)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Flashing](#flashing)
- [First run](#first-run)
- [Setting up a new machine](#setting-up-a-new-machine)
- [Making a piece](#making-a-piece)
- [How the wrap works](#how-the-wrap-works)
- [The OLED display](#the-oled-display)
- [Electronics enclosure](#electronics-enclosure)
- [Power cuts](#power-cuts)
- [When the wrong nail turns up](#when-the-wrong-nail-turns-up)
- [Which gear ratio](#which-gear-ratio)
- [Configuration in the sketch](#configuration-in-the-sketch)
- [HTTP API](#http-api)
- [Files on flash](#files-on-flash)
- [Troubleshooting](#troubleshooting)
- [Known limitations](#known-limitations)
- [Licence](#licence)
- [Credits](#credits)

------

**Features**

- **Photo     to string art in the browser** — upload, crop, choose nail and chord     counts, and watch the solver build the image. Download the steps as text     or the result as SVG.
- **Greeting     card PDF** — the photo and finished art on one A4 landscape sheet,     folded down the middle, with an optional title and message.
- **Base     template designer** — a printable, numbered nail ring sized to your disc,     with SVG export and an optional laser-cut outline.
- **Automatic     thread wrapping** — the disc and servo together trace a loop around each     nail, following the direction of travel so it works whichever way the     chord runs.
- **Everything     tuned by eye** — servo angles and the disc's wrap positions are sliders     the hardware follows live as you drag.
- **Calibration**     — re-sync the nail at the feeder, measure the real steps-per-turn, and     re-home periodically on long runs.
- **Resume     after a power cut**, with detection of a move that was interrupted.
- **Live     time estimate** from measured cycle times, not guesses.
- **OLED     dashboard** showing the current and next nail, progress, time left, and     the IP address at boot.
- **Printable     electronics enclosure** — a 6.3 × 3.3 × 5 in box with the OLED set into     its front, a lid, and a universal component tray. Plus a desk stand for     the display.
- **No     extra libraries** — builds on the bare ESP8266 core.
- **Free     software** — GPL v3 or later, with every outside author credited.

------

**Repository layout**

esp8266_string_art_standalone.ino  firmware
 web_page.h              the web UI, one PROGMEM string
 oled_display.h            OLED driver and dashboard
 ​
 parts.py               machine parts -> stl/
 fitcheck.py             checks every machine-part bolt fits
 assembly.py             renders the assembled machine
 enclosure.py             electronics box, lid, tray, OLED mount -> stl/
 enclosure_fitcheck.py        checks every enclosure part fits
 enclosure_render.py         renders enclosure_guide.png
 stl/                 all the printed parts
 PRINTING.md             printing and assembly guide
 ​
 oled_wiring.png oled_screens.png assembly_guide.png enclosure_guide.png
 README.md CHANGELOG.md LICENSE THIRD_PARTY_NOTICES.md

The Arduino IDE needs the .ino to sit in a folder with the same name. Clone the repo into a folder called esp8266_string_art_standalone, or copy the three firmware files into one.

**The three firmware files must stay together.** web_page.h is separate because the Arduino preprocessor corrupts very large raw string literals in .ino files but leaves .h files alone. oled_display.h is separate so its drawing code can be compiled and tested on a PC.

------

**Hardware**

| **Part**                          | **Notes**                                                    |
| --------------------------------- | ------------------------------------------------------------ |
| ESP8266 board                     | NodeMCU 1.0 or similar                                       |
| 28BYJ-48  stepper + ULN2003 board | 5 V  version                                                 |
| SG90 micro servo                  | The thread hook                                              |
| External  5 V supply              | 2 A.  Powers the motor and servo — never run them from the ESP8266's regulator |
| 0.96" OLED, SSD1306, I2C          | *Optional.* 128×64. Four-pin I2C modules only, not  SPI      |
| Microswitch  + 10 kΩ resistor     | *Optional.* Home position sensor                             |
| Nail disc                         | Plywood or MDF, nails around the rim                         |
| Thin  tube                        | Brass  or steel, about 2 mm, for the thread to run through   |

For the thread to stay on a nail, three things must be true, and none of them can be fixed in firmware: the nails need heads (panel pins or brads, not headless pins), the tube must carry the thread past the outside of the nail ring at shank height, and the spool needs light tension.

------

**Wiring**

ULN2003 IN1 -> D1 (GPIO5)
 ULN2003 IN2 -> D2 (GPIO4)
 ULN2003 IN3 -> D5 (GPIO14)
 ULN2003 IN4 -> D6 (GPIO12)
 ULN2003 +   -> external 5 V
 ULN2003 GND -> common ground
 ​
 SG90 signal -> D3 (GPIO0)
 SG90 V+    -> external 5 V
 SG90 GND   -> common ground
 ​
 OLED VCC   -> 3V3       3.3 V -- never 5 V
 OLED GND   -> GND
 OLED SDA   -> D7 (GPIO13)
 OLED SCL   -> D4 (GPIO2)
 ​
 Limit switch -> D0 (GPIO16) and GND
 10k resistor -> from D0 to 3V3

**Share ground, not power.** The motor and servo draw current spikes the ESP8266's regulator can't supply while WiFi is transmitting. Feed them from the external 5 V supply and tie every ground together.

**Power the OLED from 3V3.** Its I2C pull-up resistors connect to whatever powers it, and the ESP8266's pins are not 5 V tolerant.

**SCK and SCL are the same pin.** Many 4-pin OLED modules label the clock **SCK** instead of SCL; wire it to D4 all the same. Check the pin count first, though: a module with 6 or 7 pins (RES, DC, CS and so on) is the SPI version, and SCK means something different there. The firmware only supports I2C.

**Upgrading from before 1.12.0?** The limit switch used to be on D7. It has moved to D0 to make room for the display, and now needs the 10 kΩ resistor. See the [changelog](CHANGELOG.md) for the full list.

**Why the pins are where they are**

The ESP8266 has few free pins, and several have jobs at boot.

- **D1     and D2** are the usual I2C pair, but they drive the stepper.
- **D8**     must be LOW at boot. An I2C pull-up would hold it HIGH and the board would     not start.
- **D0**     cannot do I2C — it sits on a separate register the driver can't use.
- That     leaves **D4 and D7** for the display, which pushes the limit switch to **D0**.
- **D0     has no internal pull-up**, so the switch needs the 10 kΩ resistor to     3V3. Without it the pin floats and homing reads noise.
- **D3     and D4** must read HIGH at reset. The servo input and the OLED's pull-up     both leave them HIGH, so the board boots normally.
- **D4**     also drives the onboard blue LED on most boards. It flickers faintly while     the display updates. That is harmless.

------

**Flashing**

1. In     the Arduino IDE Board Manager, install **esp8266** by ESP8266     Community.
2. **Tools     → Board** → *NodeMCU 1.0 (ESP-12E Module)*, or your board.
3. **Tools     → Flash Size** → any option with a LittleFS partition.
4. Open     the esp8266_string_art_standalone folder containing the three firmware     files.
5. Set     WIFI_SSID and WIFI_PASSWORD near the top of the .ino.
6. If     you fitted the limit switch and resistor, set HAS_LIMIT_SWITCH = true.
7. Upload.

No libraries to install. WiFi, the web server, mDNS, LittleFS, Servo and Wire all come with the ESP8266 core, and the OLED driver is in oled_display.h.

------

**First run**

Power it up and watch the OLED. It shows **Joining WiFi**, then the IP address to open and stringart.local, for eight seconds.

If it can't join your network it starts its own: an open network called **StringArtCNC**. Connect to it and open **192.168.4.1**. The OLED shows this too.

Without a display, the address is printed on the serial monitor at 115200 baud. With no sequence loaded, the OLED keeps showing the address until you load one.

------

**Setting up a new machine**

Do these once, in this order. Each step depends on the one before it.

**1. Set the nail count.** Section 1 · Design → **Nails**. This one value is shared by the design, the base template and the machine, so it only needs setting once. Settle on it before tuning anything else — changing it later resets the wrap settings, because step counts don't carry over to a different nail spacing.

**2. Print and fit the base template.** Section 4 · Base template. Print the SVG at 100% scale with fit-to-page turned off, glue it face up on the disc, and drill at every red dot. Nail 0 is at three o'clock and numbering runs clockwise.

**3. Home the disc.** Line nail 0 up with the feeder and press **Set Home**. With a limit switch fitted, press **Find Home (limit switch)** instead.

**4. Check the direction.** Advanced settings → Motor → **Run direction test**. The disc visits nails at the quarter points. If the nail arriving at the feeder isn't the one named on screen, turn on **Reverse rotation direction** and home again.

**5. Set the servo angles.** Advanced settings → Feeder servo (SG90) angles. Drag the sliders and the arm follows. **Rest** sits clear of the nails; **Feed** has the tube out past the ring. **Swing rest → feed → rest** plays the real motion.

**6. Set the wrap.** Advanced settings → Wrap cycle. Drag **Ahead of the nail** and **Behind the nail**; the disc follows. The defaults are half a nail each way. Then use **Walk through one wrap by hand**, one button at a time, with thread on. If it hooks by hand it will hook on the run. Turn on **Preview as if the disc arrived travelling −** and walk through again to check the other direction.

**7. Measure the steps per turn** *(recommended before long pieces)*. Section 2 · Wrap → Calibrate the nail position. **Spin** ten turns, then enter how many nails past the start it finished and press **Correct**.

------

**Making a piece**

**Design.** In section 1, choose a photo, crop it to the circle, set the chord count and line weight, and press **Generate string art**. More chords make a darker, more detailed result and a much longer wrap. Download the steps or an SVG if you like.

**Greeting card.** Under the Design section, **Greeting card (A4 PDF)** puts your photo and the finished art on one A4 landscape sheet: art and title on the cover, photo and message inside. Print at 100% with fit-to-page off.

**Send.** Press **Send to this machine**.

**Wrap.** In section 2 the large number is the nail at the feeder. **Next →** advances one nail; **Start Auto** runs the sequence hands-free with a pause between nails. Under the progress bar you get elapsed time, time left, the clock time it will finish, and seconds per nail.

**Go to step #** jumps to a point in the sequence and carries on from there. **Go to nail #** turns the disc without changing your place.

------

**How the wrap works**

A feeder that swings out and back along the same line cannot hook a nail. That path encloses nothing, so the thread touches the nail and lets go. The tube has to travel around the nail. With one servo, the disc supplies half of that loop:

| **Phase** | **What   moves**               | **Default** |
| --------- | ------------------------------ | ----------- |
| Approach  | Disc, to just past the nail    | —           |
| Settle    | Nothing  — let the disc stop   | 800 ms      |
| Tube in   | Servo swings to the feed angle | 900 ms      |
| Wait      | Let the  thread seat           | 400 ms      |
| Sweep     | Disc, back across the nail     | —           |
| Wait      | Let the  thread hook           | 400 ms      |
| Tube out  | Servo swings back to rest      | 900 ms      |
| Recover   | Let the  thread settle         | 400 ms      |
| Land      | Disc onto the nail             | —           |

The loop takes its direction from the way the disc travelled to reach the nail: it runs past the nail, sweeps back across, and lands. The thread always trails behind the direction of travel, so this keeps the tube on the far side from it. A loop with a fixed direction wraps correctly one way round and drops the thread the other.

The servo is moved a couple of degrees at a time rather than sent straight to its angle. Servo.write() has no speed setting, so an SG90 otherwise slams to position.

All the timings are in Advanced settings → Timings. If the thread snatches, raise **Settle before feed** first. Usually the disc hasn't stopped moving yet.

------

**The OLED display**

- **Header:**     the machine's state — RUN, WRAP, FEED, HOMING, PAUSED, DONE — and the step     count. It turns into a highlighted bar for **CHECK POS** (position not     verified after a power cut) or **HOME FAIL**.
- **Centre:**     the nail at the feeder in large digits, and the next nail.
- **Bottom:**     a live countdown of time left, and a progress bar.

The display is found automatically at address 0x3C or 0x3D. If none is connected, the firmware carries on without it. If the text comes out upside down, turn on **OLED display upside down** in Advanced settings → Motor.

It never slows the machine down. It skips redrawing while the disc is moving, sends the picture in small pieces between other work, and sends nothing when the numbers haven't changed.

------

**Electronics enclosure**

A box for the NodeMCU, the ULN2003 board and the wiring, with the OLED set into its front. **6.3 × 3.3 × 5 in outside with the lid on** (160 × 84 × 127 mm); inside it's 155 × 79 × 121 mm. Every size is a named constant at the top of enclosure.py.

| **Part**           | **Print   it**       | **What   it does**                                           |
| ------------------ | -------------------- | ------------------------------------------------------------ |
| enclosure_box.stl  | Upright, open top up | The box. OLED opening in front, cable port and DC jack  hole in the back, vents in both ends |
| enclosure_lid.stl  | Outer  face down     | Screws  onto four corner bosses                              |
| enclosure_tray.stl | Flat                 | Perforated plate on four standoffs. The 0.1 in hole grid  takes M2.5 or M3 self-tapping screws or cable ties, so any board fits without  knowing its hole pattern |
| oled_frame.stl     | Front  face down     | The  bezel. Holds the display by its board edges             |
| oled_retainer.stl  | Flat, fingers up     | Clamps the frame to the wall and keeps the display in        |
| oled_stand.stl     | On its  base         | *Optional.* Desk stand for the display.  Uses the same frame and retainer |

None of them need support. The box's OLED opening has a 38 mm flat top edge, which most printers bridge cleanly. The box needs a bed of at least 165 × 90 mm and 130 mm of height.

**How the display is held**

The frame goes in from the front, and the retainer goes on from inside. Two screws through the retainer into the frame clamp the wall between them, and at the same time the retainer's two fingers keep the display's board seated in its pocket. So the same two screws do both jobs, and nothing shows on the front.

The display is held **by its board edges, not its mounting holes.** The drawing it was designed from gives the board, glass and flex-cable sizes, but not the hole spacing — so nothing depends on it. The fingers leave the board 0.1 mm of play rather than squeeze the glass.

**One measurement before printing the frame**

The drawing doesn't say exactly where the glass sits up the board. Measure the distance from the **top edge of the board** (the pin header side) to the **top of the black glass**, and set GLASS_TOP in enclosure.py. The default, 5.5 mm, assumes the glass is centred. The glass pocket allows 1 mm either way, so anything close is fine. The frame is a 4 cm³ print, so a second try costs about fifteen minutes; the box never needs reprinting for this.

**Screws**

| **Where**              | **What**                               |
| ---------------------- | -------------------------------------- |
| Lid to box             | 4 × M3 × 8 countersunk, self-tapping   |
| Tray to  standoffs     | 4 × M3  × 6 self-tapping               |
| OLED retainer to frame | 2 × M3 × 6 self-tapping                |
| Box to  machine base   | 4 × M3,  through the floor             |
| Boards to tray         | M2.5 or M3 self-tapping, or cable ties |

After changing anything in enclosure.py, run it, then enclosure_fitcheck.py. The check places every part in its assembled position with a model of the display, and confirms nothing collides, that the bezel and retainer really clamp the wall, and that every screw meets its hole.

------

**Power cuts**

The disc holds its position when the power goes, so nothing physically moves. What matters is whether the firmware still knows where the disc is. Progress is saved twice per nail — when a move starts and when it lands — which covers both cases:

- **Cut     while stopped between nails:** the saved position is exact. It picks up     on the same step.
- **Cut     during a move:** the disc stopped somewhere between two nails. The firmware     restores where it was heading, marks the position unverified, and the OLED     and web page show **CHECK POS**. Home before carrying on. Your progress     is kept either way.

With a limit switch, **Find home on power-up** (Advanced settings → Motor) homes automatically at boot and drives back to the saved nail. It does not restart the run on its own, since the thread may be loose.

------

**When the wrong nail turns up**

On a long run the nail at the feeder can stop matching the one on screen. There are three different causes, and they need different fixes.

**Out by the same amount every time.** Section 2 · Wrap → Calibrate the nail position. Jog the disc until the right nail lines up, then enter which nail it is and press **Set**. Your place in the sequence is untouched.

**A little further out with every turn.** The steps-per-turn figure is wrong. **Spin** ten turns, enter how far past the start it finished, and press **Correct**. Ten turns can measure an error of 0.05%.

**Wandering at random, even after both fixes.** The motor is losing steps. The 28BYJ-48 can't sense this, so the fixes are mechanical: raise the step delay, make the disc lighter, and check nothing is rubbing. **Re-home every N nails** (needs the limit switch) keeps a long run on track, but it works around the problem rather than fixing it.

------

**Which gear ratio**

The 28BYJ-48 is sold as a 64:1 gearbox, which gives **4096** half-steps per turn of the output shaft. That's what the firmware starts with. Many units are actually **63.68395:1** — the ratio you get by multiplying out the real gear teeth — which gives **4075.77**. The difference is about half a percent, or roughly 1.8° by the time the disc has gone once round.

You don't have to know which one you have. Section 2 · Wrap → Calibrate the nail position → **Spin** ten turns, enter how many nails past the start it finished, and press **Correct**. The firmware works out the real figure and saves it. A disc that finishes 18 nails short over ten turns measures 4075.6 — the tooth-count ratio, found by measurement.

Within a single turn the choice barely matters: rounding to the nearest half-step is at most ±0.044°. It only shows up as error that builds over whole turns.

------

**Configuration in the sketch**

Most settings live in the web page and are saved to flash. These few are set in the .ino before uploading:

| **Constant**             | **Default**  | **Purpose**                                                  |
| ------------------------ | ------------ | ------------------------------------------------------------ |
| WIFI_SSID, WIFI_PASSWORD | placeholders | Your network                                                 |
| AP_FALLBACK_SSID         | StringArtCNC | Network  it starts if yours fails                            |
| HOSTNAME                 | stringart    | Reached at stringart.local                                   |
| HAS_LIMIT_SWITCH         | false        | Set  true once the switch and resistor are fitted            |
| MOTOR_GEAR_RATIO_X100000 | 6400000      | 64:1. A starting point only — see [Which gear ratio](#which-gear-ratio) |
| DEF_*                    | see  file    | Values  used on first boot and by **Restore defaults**       |

**Restore defaults** in Advanced settings puts every web setting back to the DEF_* values without re-flashing.

------

**HTTP API**

The page talks to the machine over four endpoints, all CORS-enabled.

| **Method** | **Path** | **Purpose**                                                  |
| ---------- | -------- | ------------------------------------------------------------ |
| GET        | /        | The web page                                                 |
| GET        | /status  | JSON:  position, progress, timings, every setting, detected hardware |
| POST       | /upload  | The sequence, one nail number per line                       |
| POST       | /config  | Form  fields for any setting; reset=1 restores defaults; recalcWrap=1 recomputes  the wrap for the nail count |
| POST       | /action  | cmd= plus value= where needed                                |

**Actions:**

| **cmd**      | **value** | **Does**                                            |
| ------------ | --------- | --------------------------------------------------- |
| next, prev   | —         | Move one step through the sequence                  |
| start,  stop | —         | Auto-run  on or off                                 |
| gotostep     | step      | Jump to a step, taking progress with it             |
| goto         | nail      | Turn  the disc to a nail, progress unchanged        |
| home         | —         | The nail at the feeder becomes nail 0               |
| findhome     | —         | Seek  the limit switch. Refused if none is fitted   |
| feed         | —         | One servo cycle, rest → feed → rest                 |
| wraptest     | —         | One  full wrap on the current nail                  |
| wrappreview  | steps     | Park the disc this many steps from the current nail |
| servotest    | angle     | Move  the servo to an angle                         |
| jog          | steps     | Nudge the disc without changing the numbering       |
| setnail      | nail      | "The  nail at the feeder is this one" — re-syncs    |
| calmove      | turns     | Spin whole turns for steps-per-turn calibration     |
| calreport    | nails     | Report  how far past the start it finished          |

curl http://stringart.local/status
 curl -X POST http://stringart.local/action -d "cmd=goto&value=258"
 curl -X POST http://stringart.local/config -d "feederSettleMs=1000"

------

**Files on flash**

| **Path**      | **Contents**                                                 |
| ------------- | ------------------------------------------------------------ |
| /sequence.csv | The nail sequence, one number per line                       |
| /state.txt    | Step,  disc position, target, whether a move was in progress, elapsed time, average  time per nail |
| /config.txt   | Every setting, one value per line                            |

Settings files from older firmware load fine. Any value that's missing falls back to its default rather than zero.

------

**Troubleshooting**

**The wrong nail arrives, and it's always the mirror image** — you ask for 258 and get 102. The rotation direction is reversed. Turn on **Reverse rotation direction** in Advanced settings → Motor, then home again.

**The thread hooks on some nails but not others.** Check both approach directions with **Test wrap, coming from −** and **Test wrap, coming from +**. Both should make the same small reversing move at the nail. If both do but the thread still drops, go through the three mechanical checks under [Hardware](#hardware): nail heads, the tube crossing the ring, and spool tension.

**The motor hums but doesn't turn.** The step delay is too short for the load, or a coil wire is loose. Raise the step delay to 3–5 ms first. If it still won't move, check that the four LEDs on the ULN2003 light in sequence.

**Find Home never completes.** Check that HAS_LIMIT_SWITCH is true, that the switch connects **D0** to GND when pressed, and that the 10 kΩ resistor runs from D0 to 3V3. Without the resistor D0 floats and never reads cleanly. The status line shows the switch's live state, so press it by hand and watch it change. The search gives up after a turn and a quarter and reports **HOME FAIL**.

**Find Home does nothing at all.** HAS_LIMIT_SWITCH is false, so homing is refused. Use **Set Home** instead: line nail 0 up with the feeder by hand and press it.

**The OLED stays blank.** Check that it's powered from 3V3, that SDA goes to D7 and SCL to D4 (swapping them is the most common mistake), and that it's an I2C module — four pins marked VCC, GND, SCL, SDA. The web page shows **Display found at 0x3C**, or says none was detected.

**The board won't boot after wiring the OLED.** Something is holding D3 or D4 LOW at power-up. Check the OLED isn't wired to D8, and that nothing else is connected to D4.

**The thread snatches or feeds unevenly.** Raise **Settle before feed** in Advanced settings → Timings. The disc usually needs longer to stop than it looks.

**The page loads but the buttons do nothing.** Check the IP on the OLED. A bookmarked address stops working when your router hands out a new one.

**It's on 192.168.4.1 instead of your network.** It couldn't join your WiFi and started its own. Check the network name and password — and note the ESP8266 only works with 2.4 GHz networks, not 5 GHz.

------

**Known limitations**

- **The     stepper can't sense missed steps.** The 28BYJ-48 has no feedback, so a     missed step goes unnoticed until the wrong nail arrives. The limit switch     and periodic re-homing work around this; they don't prevent it.
- **The     28BYJ-48 is weak.** It has little torque above about 15 rpm. A heavy     disc or a short step delay will make it lose steps.
- **Wrapping     depends on the mechanics.** The firmware controls the timing and the     path, but whether the thread stays on depends on nail shape, tube position     and thread tension. Expect to tune these on your own machine.
- **Only     one servo axis.** The disc has to make half of each wrap loop, so every     nail costs two short extra moves.

------

**Licence**

ESP8266 String Art CNC is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License, version 3 or (at your option) any later version** — see [LICENSE](LICENSE). It comes with no warranty.

In short: anyone can use, study, change and share it, including commercially, but anything distributed that's built from it must also be under the GPL, with its source available. The web page shows this notice at the bottom of the sidebar, as the GPL asks interactive programs to.

Each source file starts with an SPDX-License-Identifier line. Two files also contain other people's work under their own licences:

| **File**       | **Licence**                                       |
| -------------- | ------------------------------------------------- |
| web_page.h     | GPL-3.0-or-later AND MIT (bundles Cropper.js)     |
| oled_display.h | GPL-3.0-or-later  AND BSD-2-Clause (the 5×7 font) |

------

**Credits**

This project builds on other people's work. Their credits and licences are kept as they were, in the files themselves and in full in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

- **Cropper.js**     1.6.1 by **Chen Fengyuan** — MIT. Bundled into the web page,     unmodified, with its copyright banner.
- **5×7     font** ("glcdfont") by **Adafruit Industries**, from     Adafruit-GFX-Library — BSD 2-Clause. The OLED driver around it was written     for this project.
- **Base     template designer** and remaining *Design and Coding done* by **Chanchal     Sakarde**.

 
