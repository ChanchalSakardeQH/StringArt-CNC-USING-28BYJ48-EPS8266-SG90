/*
  ESP8266 String Art Indexer
  ==========================
  Drives a 28BYJ-48 stepper (via a ULN2003 driver board) that rotates a
  disc of nails. The motor's job is purely to index: bring the next nail
  in the sequence to a fixed pointer on the frame so you can wrap the
  thread by hand, then advance to the next one.

  Control is via a small web page served over WiFi (works from a phone
  on the same network) showing the current/next nail and Next/Prev/Auto
  buttons. Upload the sequence.txt produced by string_art_generator.py
  from that same page.

  Two optional extras, both non-blocking like everything else here:
    - FEEDER_SERVO (SG90): a small servo that nudges/releases a bit of
      thread on each nail advance. It can fire automatically after every
      "next" step (toggle "Auto-feed" in Settings) or on demand via the
      "Feed now" button. Rest/feed angle and pulse length are adjustable.
    - LIMIT_SWITCH: a lever/microswitch mounted on the frame so the
      rotating disc trips it once per revolution at the physical nail-0
      mark. "Find Home" slowly rotates the disc until the switch trips,
      then declares that the true nail 0 -- a physically-verified
      alternative to "Set Home" (which just relabels wherever the disc
      currently sits, without checking anything).

  ----------------------------------------------------------------------
  CHANGES IN THIS REVISION
    1. Rotation direction. The nails ride on the disc and the feeder is
       fixed, so to present nail N the disc must turn BACKWARDS by N.
       The old code turned forwards, so nail (numNails - N) arrived
       instead -- 258 showed up as 102, 109 as 251, 263 as 97.
       Fixed by dirSign, which now defaults to -1 and is toggleable
       from the web UI without reflashing.
    2. Steps per revolution. 4096 was an approximation; the true figure
       is 4075.77 (gear ratio 63.68395:1, not 64:1). 4096 drifts about
       1.8 deg per revolution. The nail->step maths now uses the exact
       value in integer arithmetic.
    3. Position tracking. currentStep is normalized to one revolution
       when each move finishes, so rounding cannot accumulate over
       thousands of chords, and the saved state file stays small.
    4. A "Run direction test" button in machine settings: the disc
       visits nail 0, 1/4, 1/2, 3/4 and back, naming each one as it
       goes so you can check the nail at the feeder matches.

  ----------------------------------------------------------------------
  WIRING (see the project guide for the full diagram):
    ULN2003 IN1 -> ESP8266 D1 (GPIO5)
    ULN2003 IN2 -> ESP8266 D2 (GPIO4)
    ULN2003 IN3 -> ESP8266 D5 (GPIO14)
    ULN2003 IN4 -> ESP8266 D6 (GPIO12)
    ULN2003 GND -> ESP8266 GND (common ground, required)
    ULN2003 "+" -> external 5V supply (do NOT power the motor from the
                   ESP8266's own 5V/3V3 regulator; share ground only)

    SG90 signal -> ESP8266 D3 (GPIO0)
    SG90 V+     -> external 5V supply (an SG90 can spike well past what
                   the ESP8266's onboard regulator can supply alongside
                   WiFi -- share the same external supply and GND as the
                   ULN2003, not the ESP8266's own 5V/3V3 pins)
    SG90 GND    -> common ground (same net as everything else above)

    LIMIT_SWITCH -> one leg to ESP8266 D7 (GPIO13), the other leg to GND
                    (the internal pull-up is enabled in firmware, so the
                    switch just needs to short the pin to GND when
                    triggered -- no external resistor needed)

    D3/GPIO0 and D7/GPIO13 are deliberately NOT the same pins used for
    the stepper (D1/D2/D5/D6). D7 has no boot-time role at all, so it's
    safe for an input that might be held closed at power-on. D3 does have
    a boot-time role (must read HIGH at reset), but that only matters for
    an INPUT; as an OUTPUT driving a servo, nothing pulls it low before
    the sketch starts, so it boots normally.

  Before flashing:
    1. Set WIFI_SSID / WIFI_PASSWORD below.
    2. Board Manager: install "esp8266" by ESP8266 Community.
    3. Tools > Board: e.g. "NodeMCU 1.0 (ESP-12E Module)".
    4. Tools > Flash Size: pick one with an SPIFFS/LittleFS partition.
    5. Library Manager: no extra libraries needed beyond the ESP8266 core
       (ESP8266WiFi, ESP8266WebServer, LittleFS are all bundled with it).
  ----------------------------------------------------------------------
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <Servo.h>
#include <vector>

// ---------------- User configuration ----------------
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *AP_FALLBACK_SSID = "StringArtCNC";
const char *AP_FALLBACK_PASSWORD = ""; // used only if STA connect fails
const char *HOSTNAME = "stringart";

// Stepper / driver pins (avoid D0/D3/D4/D8: boot-strapping pins)
const uint8_t PIN_IN1 = 5;  // D1
const uint8_t PIN_IN2 = 4;  // D2
const uint8_t PIN_IN3 = 14; // D5
const uint8_t PIN_IN4 = 12; // D6

// Feeder servo (SG90) and home limit switch -- see the wiring notes above
// for why these two specific pins were picked among the remaining ones.
const uint8_t PIN_FEEDER_SERVO = 0;  // D3
const uint8_t PIN_LIMIT_SWITCH = 13; // D7

// 28BYJ-48 via ULN2003, half-step mode.
//
// The commonly quoted 4096 half-steps/rev assumes a 64:1 gearbox. The real
// gear ratio is 63.68395:1, so one output revolution is 64 * 63.68395 =
// 4075.77 half-steps. Using 4096 puts every nail about 0.5% too far round,
// which is ~1.8 deg of error by the time you get back to nail 0 -- enough to
// visibly smear the last chords of a long sequence. We keep the exact value
// scaled by 100 and do the nail->step maths in integers.
const long STEPS_PER_REV      = 4076;    // rounded, used for wrapping
const long STEPS_PER_REV_X100 = 407577L; // 4075.77 * 100, used for the maths

// Which way the disc turns to present a nail to the fixed feeder.
//
// The nails ride on the disc and the feeder does not move, so to bring nail N
// to the feeder the disc must rotate BACKWARDS by N -- nail N starts N steps
// ahead of the feeder and has to travel back. Turning forwards instead
// presents nail (numNails - N): ask for 258 and 102 shows up.
//
//   -1 = disc turns opposite to the nail numbering  (the correct default)
//   +1 = disc turns with the nail numbering
//
// Toggle it live from the web UI ("Reverse rotation direction") -- no reflash
// needed. Re-home after changing it.
int8_t dirSign = -1;

uint16_t stepDelayMs = 2;    // delay between half-steps; lower = faster but can stall
uint16_t numNails = 360;     // must match --nails used in the generator
uint32_t autoAdvanceMs = 4000; // used only in "auto" run mode

// Feeder servo settings (persisted -- see save/loadConfig)
uint8_t feederRestAngle = 0;
uint8_t feederFeedAngle = 90;
uint16_t feederPulseMs = 300;   // how long it dwells at feederFeedAngle before returning
bool feederAutoFeed = false;    // fire a pulse automatically after every "next" advance

// ---------------- Persistent files ----------------
const char *SEQ_FILE = "/sequence.csv";
const char *STATE_FILE = "/state.txt";
const char *CONFIG_FILE = "/config.txt";

// ---------------- Runtime state ----------------
ESP8266WebServer server(80);
std::vector<uint16_t> sequence;
int currentIndex = 0;        // index into `sequence` of the nail we're currently AT
long currentStep = 0;        // absolute half-step position of the disc
long targetStep = 0;
bool stepping = false;
int8_t stepDir = 1;
uint8_t halfStepIdx = 0;
unsigned long lastStepAt = 0;
bool autoRunning = false;
unsigned long lastAutoAdvanceAt = 0;

// Feeder servo (non-blocking: fire-and-return via millis(), like the stepper)
Servo feederServo;
bool feederActive = false;           // mid-pulse, waiting to return to rest
unsigned long feederReturnAt = 0;
bool feederPendingAfterMove = false; // fire once the in-flight stepper move completes

// Homing (non-blocking seek toward the limit switch)
bool homing = false;
bool homeError = false;      // set if the switch never triggered within the safety cap
int8_t homeDir = 1;
long homeStepCount = 0;
long homeStartStep = 0;      // restored on abort so the position tracker isn't corrupted
const long HOME_STEP_CAP = STEPS_PER_REV + STEPS_PER_REV / 4; // 1.25 rev safety limit

// Half-step sequence for a ULN2003-driven unipolar stepper (IN1..IN4)
const uint8_t HALF_STEP_SEQ[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1},
};

void writeCoils(uint8_t idx) {
  digitalWrite(PIN_IN1, HALF_STEP_SEQ[idx][0]);
  digitalWrite(PIN_IN2, HALF_STEP_SEQ[idx][1]);
  digitalWrite(PIN_IN3, HALF_STEP_SEQ[idx][2]);
  digitalWrite(PIN_IN4, HALF_STEP_SEQ[idx][3]);
}

void coilsOff() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  digitalWrite(PIN_IN3, LOW);
  digitalWrite(PIN_IN4, LOW);
}

long normalizeStep(long s) {
  s %= STEPS_PER_REV;
  if (s < 0) s += STEPS_PER_REV;
  return s;
}

// Begin a (non-blocking) move to an absolute step position, shortest direction.
void beginMoveToStep(long target) {
  target = normalizeStep(target);
  long delta = target - currentStep;
  // wrap into (-STEPS_PER_REV/2, STEPS_PER_REV/2]
  while (delta > STEPS_PER_REV / 2) delta -= STEPS_PER_REV;
  while (delta <= -STEPS_PER_REV / 2) delta += STEPS_PER_REV;
  targetStep = currentStep + delta;
  stepDir = (delta >= 0) ? 1 : -1;
  stepping = (delta != 0);
}

// Absolute disc position (in half-steps) that puts `nail` at the feeder.
// Rounded to the nearest half-step, signed by dirSign, wrapped to one rev.
long nailToStep(uint16_t nail) {
  if (numNails == 0) return 0;
  long n = ((long)nail % (long)numNails + (long)numNails) % (long)numNails;
  long s = (n * STEPS_PER_REV_X100 + (long)numNails * 50L) / ((long)numNails * 100L);
  return normalizeStep((long)dirSign * s);
}

// ---- Feeder servo (non-blocking pulse: move to feed angle, return later) --
void startFeederPulse() {
  feederServo.write(feederFeedAngle);
  feederActive = true;
  feederReturnAt = millis() + feederPulseMs;
}

// ---- Homing (non-blocking seek toward the limit switch) -------------------
void startHoming(int8_t dir) {
  homing = true;
  homeError = false;
  homeDir = dir;
  homeStepCount = 0;
  homeStartStep = currentStep;
  stepping = false; // cancel any queued sequence move -- homing takes priority
  lastStepAt = millis();
}

void saveState() {
  File f = LittleFS.open(STATE_FILE, "w");
  if (!f) return;
  f.printf("%d\n%ld\n", currentIndex, currentStep);
  f.close();
}

void loadState() {
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  currentIndex = f.readStringUntil('\n').toInt();
  // normalize: a state file written by older firmware used 4096 steps/rev and
  // could hold an unbounded step count
  currentStep = normalizeStep(f.readStringUntil('\n').toInt());
  f.close();
}

void saveConfig() {
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) return;
  f.printf("%u\n%u\n%lu\n%u\n%u\n%u\n%u\n%d\n",
           numNails, stepDelayMs, (unsigned long)autoAdvanceMs,
           feederRestAngle, feederFeedAngle, feederPulseMs, feederAutoFeed ? 1u : 0u,
           (int)dirSign);
  f.close();
}

// Reads one line and returns `def` (instead of 0) when the line is missing
// or empty, so loading a config file written by an older firmware version
// (fewer lines -- no feeder fields yet) leaves the new fields at whatever
// sane default the caller passes, rather than stomping them to 0.
long readConfigLine(File &f, long def) {
  if (!f.available()) return def;
  String line = f.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return def;
  return line.toInt();
}

void loadConfig() {
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return;
  numNails = (uint16_t)readConfigLine(f, numNails);
  stepDelayMs = (uint16_t)readConfigLine(f, stepDelayMs);
  autoAdvanceMs = (uint32_t)readConfigLine(f, autoAdvanceMs);
  feederRestAngle = (uint8_t)readConfigLine(f, feederRestAngle);
  feederFeedAngle = (uint8_t)readConfigLine(f, feederFeedAngle);
  feederPulseMs = (uint16_t)readConfigLine(f, feederPulseMs);
  feederAutoFeed = readConfigLine(f, feederAutoFeed ? 1 : 0) != 0;
  dirSign = (readConfigLine(f, dirSign) < 0) ? -1 : 1;
  f.close();
  if (numNails == 0) numNails = 200;
  if (stepDelayMs == 0) stepDelayMs = 2;
  if (autoAdvanceMs == 0) autoAdvanceMs = 4000;
}

void loadSequenceFromFile() {
  sequence.clear();
  File f = LittleFS.open(SEQ_FILE, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    sequence.push_back((uint16_t)line.toInt());
  }
  f.close();
}

// Parse an uploaded body of comma/newline/space separated integers into the
// sequence vector and persist it to LittleFS.
void parseAndSaveSequence(const String &body) {
  sequence.clear();
  int start = 0;
  int len = body.length();
  while (start < len) {
    while (start < len && !isDigit(body[start])) start++;
    int end = start;
    while (end < len && isDigit(body[end])) end++;
    if (end > start) {
      sequence.push_back((uint16_t)body.substring(start, end).toInt());
    }
    start = end;
  }
  File f = LittleFS.open(SEQ_FILE, "w");
  if (f) {
    for (uint16_t n : sequence) f.printf("%u\n", n);
    f.close();
  }
  currentIndex = 0;
  saveState();
}

// ---------------- Web UI ----------------
const char INDEX_HTML[] PROGMEM = R"STRINGARTPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>String Art Studio</title>
<style>
  :root{
    --bg:#f4f1ea; --panel:#ffffff; --panel-2:#ece6d7;
    --text:#221d17; --text-dim:#6f6355; --border:#ddd3bd;
    --copper:#a85a1c; --copper-soft:#ecd9c2;
    --signal:#2f5d8a; --signal-soft:#dbe6ee;
    --good:#3f7d4f; --good-soft:#dde8d5;
    --warn:#a3402f; --warn-soft:#f2ddd6;
    --canvas-bg:#ffffff;
    --shadow:0 1px 2px rgba(30,20,10,0.06), 0 6px 20px -14px rgba(30,20,10,0.3);
  }
  @media (prefers-color-scheme: dark){
    :root{
      --bg:#161209; --panel:#1f1a12; --panel-2:#261f15;
      --text:#efe7d8; --text-dim:#a89a80; --border:#3c3221;
      --copper:#e08a3e; --copper-soft:#3a2a15;
      --signal:#7fabd6; --signal-soft:#1d2b38;
      --good:#71bd7d; --good-soft:#1c2e1e;
      --warn:#e08272; --warn-soft:#3a2119;
      --shadow:0 1px 2px rgba(0,0,0,0.4), 0 10px 30px -18px rgba(0,0,0,0.7);
    }
  }
  *{box-sizing:border-box}
  html,body{height:100%}
  body{
    margin:0; background:var(--bg); color:var(--text);
    font-family:"IBM Plex Sans",system-ui,sans-serif; line-height:1.45;
    display:flex; flex-direction:column;
  }
  a{color:var(--signal)}

  .topbar{
    display:flex; align-items:baseline; gap:10px; padding:14px 20px;
    border-bottom:1px solid var(--border); flex:none;
  }
  .topbar h1{font-size:1.05rem; margin:0}
  .topbar .eyebrow{font-size:.72rem; letter-spacing:.06em; text-transform:uppercase; color:var(--copper); font-weight:600}

  .layout{ display:flex; flex:1; min-height:0; }

  .stage{
    flex:1; min-width:0; display:flex; align-items:center; justify-content:center;
    padding:24px; overflow:auto; background:
      radial-gradient(circle at 50% 40%, var(--panel-2) 0%, var(--bg) 70%);
  }
  #canvasWrap{
    background:var(--canvas-bg); border-radius:14px; box-shadow:var(--shadow);
    padding:16px; max-width:100%;
  }
  #artCanvas{ display:block; max-width:100%; height:auto; border-radius:6px; }
  #dropHint{
    position:absolute; color:var(--text-dim); font-size:.9rem; text-align:center;
    pointer-events:none;
  }
  #canvasOuter{ position:relative; display:flex; align-items:center; justify-content:center; max-width:100%; }

  .sidebar{
    width:360px; flex:none; border-left:1px solid var(--border); background:var(--panel);
    padding:20px; overflow-y:auto;
  }

  /* ---- Mobile / narrow viewport ---------------------------------------
     On desktop the stage (image) and sidebar (controls) sit side-by-side,
     each independently scrollable within a viewport-height app shell. On
     phones that nested-scrolling layout backfires: the sidebar's full,
     un-shrinkable content height starves the image area of room, so the
     canvas collapses to a sliver a few pixels tall inside its own tiny
     scrollbox -- which is exactly what makes the selected photo and the
     generated art look "invisible" on a phone. The fix is to stop trying
     to fit everything inside one screen and let the page scroll normally
     instead, like any other mobile web page. */
  @media (max-width:820px){
    html, body{ height:auto; min-height:100%; }
    body{ overflow-x:hidden; }
    .layout{ flex-direction:column; flex:none; height:auto; min-height:0; overflow:visible; }
    .stage{
      flex:none; overflow:visible; padding:16px 16px 8px;
      min-height:min(88vw, 420px);
    }
    #canvasWrap{ width:100%; max-width:480px; margin:0 auto; }
    .sidebar{
      width:auto; flex:none; border-left:none; border-top:1px solid var(--border);
      overflow-y:visible; max-height:none;
    }
  }
  @media (max-width:420px){
    .btn-pair{ flex-direction:column; }
  }

  .section-title{
    font-size:.72rem; letter-spacing:.06em; text-transform:uppercase; color:var(--copper);
    font-weight:700; margin:0 0 12px;
  }

  .field{ margin-bottom:16px; }
  .field label{
    display:flex; justify-content:space-between; align-items:baseline;
    font-size:.78rem; color:var(--text-dim); margin-bottom:5px; font-weight:600;
  }
  .field label .val{ font-family:"IBM Plex Mono",monospace; color:var(--text); font-weight:600; }
  input[type=number], input[type=text]{
    width:100%; padding:8px 10px; border:1px solid var(--border); border-radius:7px;
    background:var(--bg); color:var(--text); font-family:"IBM Plex Mono",monospace; font-size:.9rem;
  }
  input[type=range]{ width:100%; accent-color:var(--copper); }

  .fileBtn{
    display:block; width:100%; text-align:center; padding:10px; border:1px dashed var(--border);
    border-radius:8px; color:var(--text-dim); font-size:.85rem; cursor:pointer; background:var(--bg);
  }
  .fileBtn:hover{ border-color:var(--signal); color:var(--signal); }
  input[type=file]{ display:none; }

  .switch-row{ display:flex; align-items:center; justify-content:space-between; margin-bottom:16px; }
  .switch-row span{ font-size:.85rem; font-weight:600; }
  .switch{ position:relative; width:40px; height:22px; flex:none; }
  .switch input{ opacity:0; width:0; height:0; }
  .slider{
    position:absolute; inset:0; background:var(--border); border-radius:100px; cursor:pointer; transition:.15s;
  }
  .slider::before{
    content:""; position:absolute; width:16px; height:16px; left:3px; top:3px; background:#fff;
    border-radius:50%; transition:.15s;
  }
  .switch input:checked + .slider{ background:var(--copper); }
  .switch input:checked + .slider::before{ transform:translateX(18px); }

  hr{ border:none; border-top:1px solid var(--border); margin:20px 0; }

  button{
    font-family:"IBM Plex Sans",sans-serif; font-weight:600; font-size:.9rem;
    border:none; border-radius:8px; padding:11px 14px; cursor:pointer; width:100%;
  }
  button.primary{ background:var(--copper); color:#fff; margin-bottom:8px; }
  button.primary:disabled{ opacity:.5; cursor:not-allowed; }
  button.secondary{ background:var(--signal-soft); color:var(--signal); margin-bottom:8px; }
  button.secondary:disabled{ opacity:.45; cursor:not-allowed; }
  button.ghost{ background:transparent; border:1px solid var(--border); color:var(--text); }
  button.danger{ background:var(--warn-soft); color:var(--warn); }
  .btn-pair{ display:flex; gap:8px; }
  .btn-pair button{ margin-bottom:8px; }

  .pill{ display:inline-block; font-size:.7rem; font-weight:600; padding:3px 9px; border-radius:100px; }
  .pill.good{ background:var(--good-soft); color:var(--good); }
  .pill.warn{ background:var(--warn-soft); color:var(--warn); }
  .pill.idle{ background:var(--panel-2); color:var(--text-dim); }

  .progress-track{
    height:8px; background:var(--panel-2); border-radius:100px; overflow:hidden; margin:10px 0 6px;
  }
  .progress-fill{ height:100%; background:var(--copper); width:0%; transition:width .1s linear; }
  .progress-label{ font-size:.78rem; color:var(--text-dim); font-family:"IBM Plex Mono",monospace; }

  .msg{ font-size:.82rem; margin-top:8px; padding:8px 10px; border-radius:7px; display:none; }
  .msg.show{ display:block; }
  .msg.ok{ background:var(--good-soft); color:var(--good); }
  .msg.err{ background:var(--warn-soft); color:var(--warn); }

  .stat-line{ font-size:.78rem; color:var(--text-dim); font-family:"IBM Plex Mono",monospace; margin-top:4px; }

  /* Wrap section (indexer) */
  .nail-readout{ font-size:3.2rem; text-align:center; margin:4px 0; font-weight:700; font-family:"IBM Plex Mono",monospace; }
  .idx-sub{ text-align:center; color:var(--text-dim); font-size:.8rem; margin-bottom:10px; }
  progress[id^="idx"]{ width:100%; height:8px; margin-bottom:14px; }
  details{ margin-top:6px; }
  details summary{ cursor:pointer; font-size:.82rem; color:var(--text-dim); font-weight:600; margin-bottom:10px; }
  details .field{ margin-bottom:10px; }

  /* Crop modal */
  #cropOverlay{
    position:fixed; inset:0; background:rgba(10,8,4,.6); display:flex; align-items:center; justify-content:center;
    z-index:1000; padding:20px;
  }
  #cropBox{
    background:var(--panel); border-radius:12px; padding:18px; max-width:640px; width:100%;
    box-shadow:var(--shadow);
  }
  #cropBox h3{ margin:0 0 12px; font-size:1rem; }
  #cropImageWrap{ max-height:60vh; overflow:hidden; background:#000; border-radius:8px; }
  #cropImage{ display:block; max-width:100%; }
  .crop-actions{ display:flex; gap:10px; margin-top:14px; }
  .crop-actions button{ width:auto; flex:1; }
  [hidden]{ display:none !important; }

/* ---- vendored cropper.min.css ---- */
/*!
 * Cropper.js v1.6.1
 * https://fengyuanchen.github.io/cropperjs
 *
 * Copyright 2015-present Chen Fengyuan
 * Released under the MIT license
 *
 * Date: 2023-09-17T03:44:17.565Z
 */.cropper-container{direction:ltr;font-size:0;line-height:0;position:relative;-ms-touch-action:none;touch-action:none;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none}.cropper-container img{backface-visibility:hidden;display:block;height:100%;image-orientation:0deg;max-height:none!important;max-width:none!important;min-height:0!important;min-width:0!important;width:100%}.cropper-canvas,.cropper-crop-box,.cropper-drag-box,.cropper-modal,.cropper-wrap-box{bottom:0;left:0;position:absolute;right:0;top:0}.cropper-canvas,.cropper-wrap-box{overflow:hidden}.cropper-drag-box{background-color:#fff;opacity:0}.cropper-modal{background-color:#000;opacity:.5}.cropper-view-box{display:block;height:100%;outline:1px solid #39f;outline-color:rgba(51,153,255,.75);overflow:hidden;width:100%}.cropper-dashed{border:0 dashed #eee;display:block;opacity:.5;position:absolute}.cropper-dashed.dashed-h{border-bottom-width:1px;border-top-width:1px;height:33.33333%;left:0;top:33.33333%;width:100%}.cropper-dashed.dashed-v{border-left-width:1px;border-right-width:1px;height:100%;left:33.33333%;top:0;width:33.33333%}.cropper-center{display:block;height:0;left:50%;opacity:.75;position:absolute;top:50%;width:0}.cropper-center:after,.cropper-center:before{background-color:#eee;content:" ";display:block;position:absolute}.cropper-center:before{height:1px;left:-3px;top:0;width:7px}.cropper-center:after{height:7px;left:0;top:-3px;width:1px}.cropper-face,.cropper-line,.cropper-point{display:block;height:100%;opacity:.1;position:absolute;width:100%}.cropper-face{background-color:#fff;left:0;top:0}.cropper-line{background-color:#39f}.cropper-line.line-e{cursor:ew-resize;right:-3px;top:0;width:5px}.cropper-line.line-n{cursor:ns-resize;height:5px;left:0;top:-3px}.cropper-line.line-w{cursor:ew-resize;left:-3px;top:0;width:5px}.cropper-line.line-s{bottom:-3px;cursor:ns-resize;height:5px;left:0}.cropper-point{background-color:#39f;height:5px;opacity:.75;width:5px}.cropper-point.point-e{cursor:ew-resize;margin-top:-3px;right:-3px;top:50%}.cropper-point.point-n{cursor:ns-resize;left:50%;margin-left:-3px;top:-3px}.cropper-point.point-w{cursor:ew-resize;left:-3px;margin-top:-3px;top:50%}.cropper-point.point-s{bottom:-3px;cursor:s-resize;left:50%;margin-left:-3px}.cropper-point.point-ne{cursor:nesw-resize;right:-3px;top:-3px}.cropper-point.point-nw{cursor:nwse-resize;left:-3px;top:-3px}.cropper-point.point-sw{bottom:-3px;cursor:nesw-resize;left:-3px}.cropper-point.point-se{bottom:-3px;cursor:nwse-resize;height:20px;opacity:1;right:-3px;width:20px}@media (min-width:768px){.cropper-point.point-se{height:15px;width:15px}}@media (min-width:992px){.cropper-point.point-se{height:10px;width:10px}}@media (min-width:1200px){.cropper-point.point-se{height:5px;opacity:.75;width:5px}}.cropper-point.point-se:before{background-color:#39f;bottom:-50%;content:" ";display:block;height:200%;opacity:0;position:absolute;right:-50%;width:200%}.cropper-invisible{opacity:0}.cropper-bg{background-image:url("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQAQMAAAAlPW0iAAAAA3NCSVQICAjb4U/gAAAABlBMVEXMzMz////TjRV2AAAACXBIWXMAAArrAAAK6wGCiw1aAAAAHHRFWHRTb2Z0d2FyZQBBZG9iZSBGaXJld29ya3MgQ1M26LyyjAAAABFJREFUCJlj+M/AgBVhF/0PAH6/D/HkDxOGAAAAAElFTkSuQmCC")}.cropper-hide{display:block;height:0;position:absolute;width:0}.cropper-hidden{display:none!important}.cropper-move{cursor:move}.cropper-crop{cursor:crosshair}.cropper-disabled .cropper-drag-box,.cropper-disabled .cropper-face,.cropper-disabled .cropper-line,.cropper-disabled .cropper-point{cursor:not-allowed}
</style>
</head>
<body>

<div class="topbar">
  <span class="eyebrow">wooduloveit.com &amp; Thread</span>
  <h1>String Art Studio</h1>
</div>

<div class="layout">
  <div class="stage">
    <div id="canvasOuter">
      <div id="canvasWrap"><canvas id="artCanvas" width="600" height="600"></canvas></div>
      <div id="dropHint">Choose a photo to begin &darr;</div>
    </div>
  </div>

  <div class="sidebar">
    <p class="section-title">1 &middot; Design</p>

    <label class="fileBtn" for="fileInput" id="fileBtnLabel">Choose a photo&hellip;</label>
    <input type="file" id="fileInput" accept="image/*">

    <div class="switch-row" style="margin-top:16px">
      <span>Dark background</span>
      <label class="switch"><input type="checkbox" id="darkMode"><span class="slider"></span></label>
    </div>

    <div class="field">
      <label for="numPins">Pins <span class="val" id="numPinsVal">360</span></label>
      <input type="range" id="numPins" min="60" max="400" step="4" value="360">
    </div>
    <div class="field">
      <label for="numChords">Chords <span class="val" id="numChordsVal">3000</span></label>
      <input type="range" id="numChords" min="200" max="8000" step="50" value="3000">
    </div>
    <div class="field">
      <label for="lineWeight">Line weight <span class="val" id="lineWeightVal">22</span></label>
      <input type="range" id="lineWeight" min="4" max="60" step="1" value="22">
    </div>

    <button class="primary" id="generateBtn" disabled>Generate string art</button>
    <div class="progress-track" id="progressTrack" hidden>
      <div class="progress-fill" id="progressFill"></div>
    </div>
    <div class="progress-label" id="progressLabel" hidden>&nbsp;</div>
    <div class="stat-line" id="statLine" hidden></div>

    <div class="btn-pair" style="margin-top:10px">
      <button class="secondary" id="downloadStepsBtn" disabled>Download steps</button>
      <button class="secondary" id="downloadSvgBtn" disabled>Download SVG</button>
    </div>
    <button class="primary" id="sendBtn" disabled>Send to this machine</button>
    <div class="msg" id="sendMsg"></div>

    <hr>

    <p class="section-title">2 &middot; Wrap</p>
    <div class="nail-readout" id="idxNailNum">&mdash;</div>
    <div class="idx-sub" id="idxProgressText">step -- / --</div>
    <progress id="idxBar" value="0" max="100"></progress>

    <div class="btn-pair">
      <button class="secondary" id="idxPrevBtn">&larr; Prev</button>
      <button class="primary" id="idxNextBtn" style="margin-bottom:0">Next &rarr;</button>
    </div>
    <div class="btn-pair" style="margin-top:8px">
      <button class="secondary" id="idxAutoBtn">Start Auto</button>
      <button class="danger" id="idxHomeBtn">Set Home</button>
    </div>
    <button class="secondary" id="idxFindHomeBtn" style="margin-top:8px">Find Home (limit switch)</button>
    <div class="idx-sub" id="idxSwitchText">limit switch: &mdash;</div>

    <details>
      <summary>Jump to nail # / machine settings</summary>
      <div class="field">
        <label>Jump motor to nail # (doesn't change progress)</label>
        <div class="btn-pair">
          <input id="idxGotoVal" type="number" min="0">
          <button class="ghost" id="idxGotoBtn" style="width:auto;flex:none;padding:8px 16px">Go</button>
        </div>
      </div>
      <div class="field">
        <label>Number of nails</label>
        <input id="idxNumNails" type="number">
      </div>
      <div class="field">
        <label>Step delay (ms/half-step)</label>
        <input id="idxStepDelay" type="number">
      </div>
      <div class="field">
        <label>Auto-advance interval (ms)</label>
        <input id="idxAutoMs" type="number">
      </div>
      <div class="switch-row">
        <span>Reverse rotation direction</span>
        <label class="switch"><input type="checkbox" id="idxReverseDir"><span class="slider"></span></label>
      </div>
      <div class="idx-sub" style="margin-top:0;text-align:left">
        Leave this on unless the test below says otherwise. Re-home after changing it.
      </div>
      <button class="ghost" id="idxDirTestBtn">Run direction test</button>
      <div class="idx-sub" id="idxDirTestText" style="text-align:left">&nbsp;</div>
      <button class="secondary" id="idxSaveConfigBtn">Save machine settings</button>
    </details>

    <hr>

    <p class="section-title">3 &middot; Feed</p>
    <div class="switch-row">
      <span>Auto-feed each step</span>
      <label class="switch"><input type="checkbox" id="feederAutoFeed"><span class="slider"></span></label>
    </div>
    <button class="primary" id="feederFeedBtn">Feed now</button>
    <details>
      <summary>Feeder servo (SG90) settings</summary>
      <div class="field">
        <label>Rest angle (&deg;)</label>
        <input id="feederRestAngle" type="number" min="0" max="180">
      </div>
      <div class="field">
        <label>Feed angle (&deg;)</label>
        <input id="feederFeedAngle" type="number" min="0" max="180">
      </div>
      <div class="field">
        <label>Pulse duration (ms)</label>
        <input id="feederPulseMs" type="number" min="0">
      </div>
      <button class="secondary" id="feederSaveBtn">Save feeder settings</button>
    </details>
  </div>
</div>

<div id="cropOverlay" hidden>
  <div id="cropBox">
    <h3>Frame the artwork</h3>
    <div id="cropImageWrap"><img id="cropImage" alt="Photo to crop"></div>
    <div class="crop-actions">
      <button class="ghost" id="cropCancelBtn">Cancel</button>
      <button class="primary" id="cropConfirmBtn" style="margin-bottom:0">Use this crop</button>
    </div>
  </div>
</div>

<script>
/* ---- vendored cropper.min.js ---- */
/*!
 * Cropper.js v1.6.1
 * https://fengyuanchen.github.io/cropperjs
 *
 * Copyright 2015-present Chen Fengyuan
 * Released under the MIT license
 *
 * Date: 2023-09-17T03:44:19.860Z
 */
!function(t,e){"object"==typeof exports&&"undefined"!=typeof module?module.exports=e():"function"==typeof define&&define.amd?define(e):(t="undefined"!=typeof globalThis?globalThis:t||self).Cropper=e()}(this,function(){"use strict";function C(e,t){var i,a=Object.keys(e);return Object.getOwnPropertySymbols&&(i=Object.getOwnPropertySymbols(e),t&&(i=i.filter(function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable})),a.push.apply(a,i)),a}function S(a){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?C(Object(n),!0).forEach(function(t){var e,i;e=a,i=n[t=t],(t=P(t))in e?Object.defineProperty(e,t,{value:i,enumerable:!0,configurable:!0,writable:!0}):e[t]=i}):Object.getOwnPropertyDescriptors?Object.defineProperties(a,Object.getOwnPropertyDescriptors(n)):C(Object(n)).forEach(function(t){Object.defineProperty(a,t,Object.getOwnPropertyDescriptor(n,t))})}return a}function D(t){return(D="function"==typeof Symbol&&"symbol"==typeof Symbol.iterator?function(t){return typeof t}:function(t){return t&&"function"==typeof Symbol&&t.constructor===Symbol&&t!==Symbol.prototype?"symbol":typeof t})(t)}function j(t,e){for(var i=0;i<e.length;i++){var a=e[i];a.enumerable=a.enumerable||!1,a.configurable=!0,"value"in a&&(a.writable=!0),Object.defineProperty(t,P(a.key),a)}}function A(t){return function(t){if(Array.isArray(t))return a(t)}(t)||function(t){if("undefined"!=typeof Symbol&&null!=t[Symbol.iterator]||null!=t["@@iterator"])return Array.from(t)}(t)||function(t,e){var i;if(t)return"string"==typeof t?a(t,e):"Map"===(i="Object"===(i=Object.prototype.toString.call(t).slice(8,-1))&&t.constructor?t.constructor.name:i)||"Set"===i?Array.from(t):"Arguments"===i||/^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(i)?a(t,e):void 0}(t)||function(){throw new TypeError("Invalid attempt to spread non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.")}()}function a(t,e){(null==e||e>t.length)&&(e=t.length);for(var i=0,a=new Array(e);i<e;i++)a[i]=t[i];return a}function P(t){t=function(t,e){if("object"!=typeof t||null===t)return t;var i=t[Symbol.toPrimitive];if(void 0===i)return("string"===e?String:Number)(t);if("object"!=typeof(i=i.call(t,e||"default")))return i;throw new TypeError("@@toPrimitive must return a primitive value.")}(t,"string");return"symbol"==typeof t?t:String(t)}var t="undefined"!=typeof window&&void 0!==window.document,h=t?window:{},e=!(!t||!h.document.documentElement)&&"ontouchstart"in h.document.documentElement,i=t&&"PointerEvent"in h,c="cropper",I="all",U="crop",q="move",$="zoom",B="e",k="w",O="s",T="n",E="ne",W="nw",H="se",N="sw",Q="".concat(c,"-crop"),K="".concat(c,"-disabled"),L="".concat(c,"-hidden"),Z="".concat(c,"-hide"),G="".concat(c,"-invisible"),n="".concat(c,"-modal"),V="".concat(c,"-move"),d="".concat(c,"Action"),m="".concat(c,"Preview"),F="crop",J="move",_="none",tt="crop",et="cropend",it="cropmove",at="cropstart",nt="dblclick",ot=i?"pointerdown":e?"touchstart":"mousedown",ht=i?"pointermove":e?"touchmove":"mousemove",rt=i?"pointerup pointercancel":e?"touchend touchcancel":"mouseup",st="zoom",ct="image/jpeg",dt=/^e|w|s|n|se|sw|ne|nw|all|crop|move|zoom$/,lt=/^data:/,pt=/^data:image\/jpeg;base64,/,mt=/^img|canvas$/i,ut={viewMode:0,dragMode:F,initialAspectRatio:NaN,aspectRatio:NaN,data:null,preview:"",responsive:!0,restore:!0,checkCrossOrigin:!0,checkOrientation:!0,modal:!0,guides:!0,center:!0,highlight:!0,background:!0,autoCrop:!0,autoCropArea:.8,movable:!0,rotatable:!0,scalable:!0,zoomable:!0,zoomOnTouch:!0,zoomOnWheel:!0,wheelZoomRatio:.1,cropBoxMovable:!0,cropBoxResizable:!0,toggleDragModeOnDblclick:!0,minCanvasWidth:0,minCanvasHeight:0,minCropBoxWidth:0,minCropBoxHeight:0,minContainerWidth:200,minContainerHeight:100,ready:null,cropstart:null,cropmove:null,cropend:null,crop:null,zoom:null},gt=Number.isNaN||h.isNaN;function p(t){return"number"==typeof t&&!gt(t)}function ft(t){return 0<t&&t<1/0}function vt(t){return void 0===t}function o(t){return"object"===D(t)&&null!==t}var wt=Object.prototype.hasOwnProperty;function u(t){if(!o(t))return!1;try{var e=t.constructor,i=e.prototype;return e&&i&&wt.call(i,"isPrototypeOf")}catch(t){return!1}}function l(t){return"function"==typeof t}var bt=Array.prototype.slice;function yt(t){return Array.from?Array.from(t):bt.call(t)}function z(i,a){return i&&l(a)&&(Array.isArray(i)||p(i.length)?yt(i).forEach(function(t,e){a.call(i,t,e,i)}):o(i)&&Object.keys(i).forEach(function(t){a.call(i,i[t],t,i)})),i}var g=Object.assign||function(i){for(var t=arguments.length,e=new Array(1<t?t-1:0),a=1;a<t;a++)e[a-1]=arguments[a];return o(i)&&0<e.length&&e.forEach(function(e){o(e)&&Object.keys(e).forEach(function(t){i[t]=e[t]})}),i},xt=/\.\d*(?:0|9){12}\d*$/;function Y(t,e){e=1<arguments.length&&void 0!==e?e:1e11;return xt.test(t)?Math.round(t*e)/e:t}var Mt=/^width|height|left|top|marginLeft|marginTop$/;function f(t,e){var i=t.style;z(e,function(t,e){Mt.test(e)&&p(t)&&(t="".concat(t,"px")),i[e]=t})}function v(t,e){var i;e&&(p(t.length)?z(t,function(t){v(t,e)}):t.classList?t.classList.add(e):(i=t.className.trim())?i.indexOf(e)<0&&(t.className="".concat(i," ").concat(e)):t.className=e)}function X(t,e){e&&(p(t.length)?z(t,function(t){X(t,e)}):t.classList?t.classList.remove(e):0<=t.className.indexOf(e)&&(t.className=t.className.replace(e,"")))}function r(t,e,i){e&&(p(t.length)?z(t,function(t){r(t,e,i)}):(i?v:X)(t,e))}var Ct=/([a-z\d])([A-Z])/g;function Dt(t){return t.replace(Ct,"$1-$2").toLowerCase()}function Bt(t,e){return o(t[e])?t[e]:t.dataset?t.dataset[e]:t.getAttribute("data-".concat(Dt(e)))}function w(t,e,i){o(i)?t[e]=i:t.dataset?t.dataset[e]=i:t.setAttribute("data-".concat(Dt(e)),i)}var kt,Ot,Tt=/\s\s*/,Et=(Ot=!1,t&&(kt=!1,i=function(){},e=Object.defineProperty({},"once",{get:function(){return Ot=!0,kt},set:function(t){kt=t}}),h.addEventListener("test",i,e),h.removeEventListener("test",i,e)),Ot);function s(i,t,a,e){var n=3<arguments.length&&void 0!==e?e:{},o=a;t.trim().split(Tt).forEach(function(t){var e;Et||(e=i.listeners)&&e[t]&&e[t][a]&&(o=e[t][a],delete e[t][a],0===Object.keys(e[t]).length&&delete e[t],0===Object.keys(e).length)&&delete i.listeners,i.removeEventListener(t,o,n)})}function b(o,t,h,e){var r=3<arguments.length&&void 0!==e?e:{},s=h;t.trim().split(Tt).forEach(function(a){var t,n;r.once&&!Et&&(t=o.listeners,s=function(){delete n[a][h],o.removeEventListener(a,s,r);for(var t=arguments.length,e=new Array(t),i=0;i<t;i++)e[i]=arguments[i];h.apply(o,e)},(n=void 0===t?{}:t)[a]||(n[a]={}),n[a][h]&&o.removeEventListener(a,n[a][h],r),n[a][h]=s,o.listeners=n),o.addEventListener(a,s,r)})}function y(t,e,i){var a;return l(Event)&&l(CustomEvent)?a=new CustomEvent(e,{detail:i,bubbles:!0,cancelable:!0}):(a=document.createEvent("CustomEvent")).initCustomEvent(e,!0,!0,i),t.dispatchEvent(a)}function Wt(t){t=t.getBoundingClientRect();return{left:t.left+(window.pageXOffset-document.documentElement.clientLeft),top:t.top+(window.pageYOffset-document.documentElement.clientTop)}}var Ht=h.location,Nt=/^(\w+:)\/\/([^:/?#]*):?(\d*)/i;function Lt(t){t=t.match(Nt);return null!==t&&(t[1]!==Ht.protocol||t[2]!==Ht.hostname||t[3]!==Ht.port)}function zt(t){var e="timestamp=".concat((new Date).getTime());return t+(-1===t.indexOf("?")?"?":"&")+e}function x(t){var e=t.rotate,i=t.scaleX,a=t.scaleY,n=t.translateX,t=t.translateY,o=[],n=(p(n)&&0!==n&&o.push("translateX(".concat(n,"px)")),p(t)&&0!==t&&o.push("translateY(".concat(t,"px)")),p(e)&&0!==e&&o.push("rotate(".concat(e,"deg)")),p(i)&&1!==i&&o.push("scaleX(".concat(i,")")),p(a)&&1!==a&&o.push("scaleY(".concat(a,")")),o.length?o.join(" "):"none");return{WebkitTransform:n,msTransform:n,transform:n}}function M(t,e){var i=t.pageX,t=t.pageY,a={endX:i,endY:t};return e?a:S({startX:i,startY:t},a)}function R(t,e){var i,a=t.aspectRatio,n=t.height,t=t.width,e=1<arguments.length&&void 0!==e?e:"contain",o=ft(t),h=ft(n);return o&&h?(i=n*a,"contain"===e&&t<i||"cover"===e&&i<t?n=t/a:t=n*a):o?n=t/a:h&&(t=n*a),{width:t,height:n}}var Yt=String.fromCharCode;var Xt=/^data:.*,/;function Rt(t){var e,i,a,n,o,h,r,s=new DataView(t);try{if(255===s.getUint8(0)&&216===s.getUint8(1))for(var c=s.byteLength,d=2;d+1<c;){if(255===s.getUint8(d)&&225===s.getUint8(d+1)){i=d;break}d+=1}if(a=i&&(n=i+10,"Exif"===function(t,e,i){var a="";i+=e;for(var n=e;n<i;n+=1)a+=Yt(t.getUint8(n));return a}(s,i+4,4))&&((r=18761===(o=s.getUint16(n)))||19789===o)&&42===s.getUint16(n+2,r)&&8<=(h=s.getUint32(n+4,r))?n+h:a)for(var l,p=s.getUint16(a,r),m=0;m<p;m+=1)if(l=a+12*m+2,274===s.getUint16(l,r)){l+=8,e=s.getUint16(l,r),s.setUint16(l,1,r);break}}catch(t){e=1}return e}var t={render:function(){this.initContainer(),this.initCanvas(),this.initCropBox(),this.renderCanvas(),this.cropped&&this.renderCropBox()},initContainer:function(){var t=this.element,e=this.options,i=this.container,a=this.cropper,n=Number(e.minContainerWidth),e=Number(e.minContainerHeight),n=(v(a,L),X(t,L),{width:Math.max(i.offsetWidth,0<=n?n:200),height:Math.max(i.offsetHeight,0<=e?e:100)});f(a,{width:(this.containerData=n).width,height:n.height}),v(t,L),X(a,L)},initCanvas:function(){var t=this.containerData,e=this.imageData,i=this.options.viewMode,a=Math.abs(e.rotate)%180==90,n=a?e.naturalHeight:e.naturalWidth,a=a?e.naturalWidth:e.naturalHeight,e=n/a,o=t.width,h=t.height,e=(t.height*e>t.width?3===i?o=t.height*e:h=t.width/e:3===i?h=t.width/e:o=t.height*e,{aspectRatio:e,naturalWidth:n,naturalHeight:a,width:o,height:h});this.canvasData=e,this.limited=1===i||2===i,this.limitCanvas(!0,!0),e.width=Math.min(Math.max(e.width,e.minWidth),e.maxWidth),e.height=Math.min(Math.max(e.height,e.minHeight),e.maxHeight),e.left=(t.width-e.width)/2,e.top=(t.height-e.height)/2,e.oldLeft=e.left,e.oldTop=e.top,this.initialCanvasData=g({},e)},limitCanvas:function(t,e){var i=this.options,a=this.containerData,n=this.canvasData,o=this.cropBoxData,h=i.viewMode,r=n.aspectRatio,s=this.cropped&&o;t&&(t=Number(i.minCanvasWidth)||0,i=Number(i.minCanvasHeight)||0,1<h?(t=Math.max(t,a.width),i=Math.max(i,a.height),3===h&&(t<i*r?t=i*r:i=t/r)):0<h&&(t?t=Math.max(t,s?o.width:0):i?i=Math.max(i,s?o.height:0):s&&((t=o.width)<(i=o.height)*r?t=i*r:i=t/r)),t=(r=R({aspectRatio:r,width:t,height:i})).width,i=r.height,n.minWidth=t,n.minHeight=i,n.maxWidth=1/0,n.maxHeight=1/0),e&&((s?0:1)<h?(r=a.width-n.width,t=a.height-n.height,n.minLeft=Math.min(0,r),n.minTop=Math.min(0,t),n.maxLeft=Math.max(0,r),n.maxTop=Math.max(0,t),s&&this.limited&&(n.minLeft=Math.min(o.left,o.left+(o.width-n.width)),n.minTop=Math.min(o.top,o.top+(o.height-n.height)),n.maxLeft=o.left,n.maxTop=o.top,2===h)&&(n.width>=a.width&&(n.minLeft=Math.min(0,r),n.maxLeft=Math.max(0,r)),n.height>=a.height)&&(n.minTop=Math.min(0,t),n.maxTop=Math.max(0,t))):(n.minLeft=-n.width,n.minTop=-n.height,n.maxLeft=a.width,n.maxTop=a.height))},renderCanvas:function(t,e){var i,a,n,o,h=this.canvasData,r=this.imageData;e&&(e={width:r.naturalWidth*Math.abs(r.scaleX||1),height:r.naturalHeight*Math.abs(r.scaleY||1),degree:r.rotate||0},r=e.width,o=e.height,e=e.degree,i=90==(e=Math.abs(e)%180)?{width:o,height:r}:(a=e%90*Math.PI/180,i=Math.sin(a),n=r*(a=Math.cos(a))+o*i,r=r*i+o*a,90<e?{width:r,height:n}:{width:n,height:r}),a=h.width*((o=i.width)/h.naturalWidth),n=h.height*((e=i.height)/h.naturalHeight),h.left-=(a-h.width)/2,h.top-=(n-h.height)/2,h.width=a,h.height=n,h.aspectRatio=o/e,h.naturalWidth=o,h.naturalHeight=e,this.limitCanvas(!0,!1)),(h.width>h.maxWidth||h.width<h.minWidth)&&(h.left=h.oldLeft),(h.height>h.maxHeight||h.height<h.minHeight)&&(h.top=h.oldTop),h.width=Math.min(Math.max(h.width,h.minWidth),h.maxWidth),h.height=Math.min(Math.max(h.height,h.minHeight),h.maxHeight),this.limitCanvas(!1,!0),h.left=Math.min(Math.max(h.left,h.minLeft),h.maxLeft),h.top=Math.min(Math.max(h.top,h.minTop),h.maxTop),h.oldLeft=h.left,h.oldTop=h.top,f(this.canvas,g({width:h.width,height:h.height},x({translateX:h.left,translateY:h.top}))),this.renderImage(t),this.cropped&&this.limited&&this.limitCropBox(!0,!0)},renderImage:function(t){var e=this.canvasData,i=this.imageData,a=i.naturalWidth*(e.width/e.naturalWidth),n=i.naturalHeight*(e.height/e.naturalHeight);g(i,{width:a,height:n,left:(e.width-a)/2,top:(e.height-n)/2}),f(this.image,g({width:i.width,height:i.height},x(g({translateX:i.left,translateY:i.top},i)))),t&&this.output()},initCropBox:function(){var t=this.options,e=this.canvasData,i=t.aspectRatio||t.initialAspectRatio,t=Number(t.autoCropArea)||.8,a={width:e.width,height:e.height};i&&(e.height*i>e.width?a.height=a.width/i:a.width=a.height*i),this.cropBoxData=a,this.limitCropBox(!0,!0),a.width=Math.min(Math.max(a.width,a.minWidth),a.maxWidth),a.height=Math.min(Math.max(a.height,a.minHeight),a.maxHeight),a.width=Math.max(a.minWidth,a.width*t),a.height=Math.max(a.minHeight,a.height*t),a.left=e.left+(e.width-a.width)/2,a.top=e.top+(e.height-a.height)/2,a.oldLeft=a.left,a.oldTop=a.top,this.initialCropBoxData=g({},a)},limitCropBox:function(t,e){var i,a,n=this.options,o=this.containerData,h=this.canvasData,r=this.cropBoxData,s=this.limited,c=n.aspectRatio;t&&(t=Number(n.minCropBoxWidth)||0,n=Number(n.minCropBoxHeight)||0,i=s?Math.min(o.width,h.width,h.width+h.left,o.width-h.left):o.width,a=s?Math.min(o.height,h.height,h.height+h.top,o.height-h.top):o.height,t=Math.min(t,o.width),n=Math.min(n,o.height),c&&(t&&n?t<n*c?n=t/c:t=n*c:t?n=t/c:n&&(t=n*c),i<a*c?a=i/c:i=a*c),r.minWidth=Math.min(t,i),r.minHeight=Math.min(n,a),r.maxWidth=i,r.maxHeight=a),e&&(s?(r.minLeft=Math.max(0,h.left),r.minTop=Math.max(0,h.top),r.maxLeft=Math.min(o.width,h.left+h.width)-r.width,r.maxTop=Math.min(o.height,h.top+h.height)-r.height):(r.minLeft=0,r.minTop=0,r.maxLeft=o.width-r.width,r.maxTop=o.height-r.height))},renderCropBox:function(){var t=this.options,e=this.containerData,i=this.cropBoxData;(i.width>i.maxWidth||i.width<i.minWidth)&&(i.left=i.oldLeft),(i.height>i.maxHeight||i.height<i.minHeight)&&(i.top=i.oldTop),i.width=Math.min(Math.max(i.width,i.minWidth),i.maxWidth),i.height=Math.min(Math.max(i.height,i.minHeight),i.maxHeight),this.limitCropBox(!1,!0),i.left=Math.min(Math.max(i.left,i.minLeft),i.maxLeft),i.top=Math.min(Math.max(i.top,i.minTop),i.maxTop),i.oldLeft=i.left,i.oldTop=i.top,t.movable&&t.cropBoxMovable&&w(this.face,d,i.width>=e.width&&i.height>=e.height?q:I),f(this.cropBox,g({width:i.width,height:i.height},x({translateX:i.left,translateY:i.top}))),this.cropped&&this.limited&&this.limitCanvas(!0,!0),this.disabled||this.output()},output:function(){this.preview(),y(this.element,tt,this.getData())}},i={initPreview:function(){var t=this.element,i=this.crossOrigin,e=this.options.preview,a=i?this.crossOriginUrl:this.url,n=t.alt||"The image to preview",o=document.createElement("img");i&&(o.crossOrigin=i),o.src=a,o.alt=n,this.viewBox.appendChild(o),this.viewBoxImage=o,e&&("string"==typeof(o=e)?o=t.ownerDocument.querySelectorAll(e):e.querySelector&&(o=[e]),z(this.previews=o,function(t){var e=document.createElement("img");w(t,m,{width:t.offsetWidth,height:t.offsetHeight,html:t.innerHTML}),i&&(e.crossOrigin=i),e.src=a,e.alt=n,e.style.cssText='display:block;width:100%;height:auto;min-width:0!important;min-height:0!important;max-width:none!important;max-height:none!important;image-orientation:0deg!important;"',t.innerHTML="",t.appendChild(e)}))},resetPreview:function(){z(this.previews,function(e){var i=Bt(e,m),i=(f(e,{width:i.width,height:i.height}),e.innerHTML=i.html,e),e=m;if(o(i[e]))try{delete i[e]}catch(t){i[e]=void 0}else if(i.dataset)try{delete i.dataset[e]}catch(t){i.dataset[e]=void 0}else i.removeAttribute("data-".concat(Dt(e)))})},preview:function(){var h=this.imageData,t=this.canvasData,e=this.cropBoxData,r=e.width,s=e.height,c=h.width,d=h.height,l=e.left-t.left-h.left,p=e.top-t.top-h.top;this.cropped&&!this.disabled&&(f(this.viewBoxImage,g({width:c,height:d},x(g({translateX:-l,translateY:-p},h)))),z(this.previews,function(t){var e=Bt(t,m),i=e.width,e=e.height,a=i,n=e,o=1;r&&(n=s*(o=i/r)),s&&e<n&&(a=r*(o=e/s),n=e),f(t,{width:a,height:n}),f(t.getElementsByTagName("img")[0],g({width:c*o,height:d*o},x(g({translateX:-l*o,translateY:-p*o},h))))}))}},e={bind:function(){var t=this.element,e=this.options,i=this.cropper;l(e.cropstart)&&b(t,at,e.cropstart),l(e.cropmove)&&b(t,it,e.cropmove),l(e.cropend)&&b(t,et,e.cropend),l(e.crop)&&b(t,tt,e.crop),l(e.zoom)&&b(t,st,e.zoom),b(i,ot,this.onCropStart=this.cropStart.bind(this)),e.zoomable&&e.zoomOnWheel&&b(i,"wheel",this.onWheel=this.wheel.bind(this),{passive:!1,capture:!0}),e.toggleDragModeOnDblclick&&b(i,nt,this.onDblclick=this.dblclick.bind(this)),b(t.ownerDocument,ht,this.onCropMove=this.cropMove.bind(this)),b(t.ownerDocument,rt,this.onCropEnd=this.cropEnd.bind(this)),e.responsive&&b(window,"resize",this.onResize=this.resize.bind(this))},unbind:function(){var t=this.element,e=this.options,i=this.cropper;l(e.cropstart)&&s(t,at,e.cropstart),l(e.cropmove)&&s(t,it,e.cropmove),l(e.cropend)&&s(t,et,e.cropend),l(e.crop)&&s(t,tt,e.crop),l(e.zoom)&&s(t,st,e.zoom),s(i,ot,this.onCropStart),e.zoomable&&e.zoomOnWheel&&s(i,"wheel",this.onWheel,{passive:!1,capture:!0}),e.toggleDragModeOnDblclick&&s(i,nt,this.onDblclick),s(t.ownerDocument,ht,this.onCropMove),s(t.ownerDocument,rt,this.onCropEnd),e.responsive&&s(window,"resize",this.onResize)}},St={resize:function(){var t,e,i,a,n,o,h;this.disabled||(t=this.options,a=this.container,e=this.containerData,i=a.offsetWidth/e.width,a=a.offsetHeight/e.height,1!=(n=Math.abs(i-1)>Math.abs(a-1)?i:a)&&(t.restore&&(o=this.getCanvasData(),h=this.getCropBoxData()),this.render(),t.restore)&&(this.setCanvasData(z(o,function(t,e){o[e]=t*n})),this.setCropBoxData(z(h,function(t,e){h[e]=t*n}))))},dblclick:function(){var t,e;this.disabled||this.options.dragMode===_||this.setDragMode((t=this.dragBox,e=Q,(t.classList?t.classList.contains(e):-1<t.className.indexOf(e))?J:F))},wheel:function(t){var e=this,i=Number(this.options.wheelZoomRatio)||.1,a=1;this.disabled||(t.preventDefault(),this.wheeling)||(this.wheeling=!0,setTimeout(function(){e.wheeling=!1},50),t.deltaY?a=0<t.deltaY?1:-1:t.wheelDelta?a=-t.wheelDelta/120:t.detail&&(a=0<t.detail?1:-1),this.zoom(-a*i,t))},cropStart:function(t){var e,i=t.buttons,a=t.button;this.disabled||("mousedown"===t.type||"pointerdown"===t.type&&"mouse"===t.pointerType)&&(p(i)&&1!==i||p(a)&&0!==a||t.ctrlKey)||(i=this.options,e=this.pointers,t.changedTouches?z(t.changedTouches,function(t){e[t.identifier]=M(t)}):e[t.pointerId||0]=M(t),a=1<Object.keys(e).length&&i.zoomable&&i.zoomOnTouch?$:Bt(t.target,d),dt.test(a)&&!1!==y(this.element,at,{originalEvent:t,action:a})&&(t.preventDefault(),this.action=a,this.cropping=!1,a===U)&&(this.cropping=!0,v(this.dragBox,n)))},cropMove:function(t){var e,i=this.action;!this.disabled&&i&&(e=this.pointers,t.preventDefault(),!1!==y(this.element,it,{originalEvent:t,action:i}))&&(t.changedTouches?z(t.changedTouches,function(t){g(e[t.identifier]||{},M(t,!0))}):g(e[t.pointerId||0]||{},M(t,!0)),this.change(t))},cropEnd:function(t){var e,i;this.disabled||(e=this.action,i=this.pointers,t.changedTouches?z(t.changedTouches,function(t){delete i[t.identifier]}):delete i[t.pointerId||0],e&&(t.preventDefault(),Object.keys(i).length||(this.action=""),this.cropping&&(this.cropping=!1,r(this.dragBox,n,this.cropped&&this.options.modal)),y(this.element,et,{originalEvent:t,action:e})))}},jt={change:function(t){function e(t){switch(t){case B:f+D.x>y&&(D.x=y-f);break;case k:p+D.x<w&&(D.x=w-p);break;case T:m+D.y<b&&(D.y=b-m);break;case O:v+D.y>x&&(D.y=x-v)}}var i,a,o,n=this.options,h=this.canvasData,r=this.containerData,s=this.cropBoxData,c=this.pointers,d=this.action,l=n.aspectRatio,p=s.left,m=s.top,u=s.width,g=s.height,f=p+u,v=m+g,w=0,b=0,y=r.width,x=r.height,M=!0,C=(!l&&t.shiftKey&&(l=u&&g?u/g:1),this.limited&&(w=s.minLeft,b=s.minTop,y=w+Math.min(r.width,h.width,h.left+h.width),x=b+Math.min(r.height,h.height,h.top+h.height)),c[Object.keys(c)[0]]),D={x:C.endX-C.startX,y:C.endY-C.startY};switch(d){case I:p+=D.x,m+=D.y;break;case B:0<=D.x&&(y<=f||l&&(m<=b||x<=v))?M=!1:(e(B),(u+=D.x)<0&&(d=k,p-=u=-u),l&&(m+=(s.height-(g=u/l))/2));break;case T:D.y<=0&&(m<=b||l&&(p<=w||y<=f))?M=!1:(e(T),g-=D.y,m+=D.y,g<0&&(d=O,m-=g=-g),l&&(p+=(s.width-(u=g*l))/2));break;case k:D.x<=0&&(p<=w||l&&(m<=b||x<=v))?M=!1:(e(k),u-=D.x,p+=D.x,u<0&&(d=B,p-=u=-u),l&&(m+=(s.height-(g=u/l))/2));break;case O:0<=D.y&&(x<=v||l&&(p<=w||y<=f))?M=!1:(e(O),(g+=D.y)<0&&(d=T,m-=g=-g),l&&(p+=(s.width-(u=g*l))/2));break;case E:if(l){if(D.y<=0&&(m<=b||y<=f)){M=!1;break}e(T),g-=D.y,m+=D.y,u=g*l}else e(T),e(B),!(0<=D.x)||f<y?u+=D.x:D.y<=0&&m<=b&&(M=!1),(!(D.y<=0)||b<m)&&(g-=D.y,m+=D.y);u<0&&g<0?(d=N,m-=g=-g,p-=u=-u):u<0?(d=W,p-=u=-u):g<0&&(d=H,m-=g=-g);break;case W:if(l){if(D.y<=0&&(m<=b||p<=w)){M=!1;break}e(T),g-=D.y,m+=D.y,p+=s.width-(u=g*l)}else e(T),e(k),!(D.x<=0)||w<p?(u-=D.x,p+=D.x):D.y<=0&&m<=b&&(M=!1),(!(D.y<=0)||b<m)&&(g-=D.y,m+=D.y);u<0&&g<0?(d=H,m-=g=-g,p-=u=-u):u<0?(d=E,p-=u=-u):g<0&&(d=N,m-=g=-g);break;case N:if(l){if(D.x<=0&&(p<=w||x<=v)){M=!1;break}e(k),u-=D.x,p+=D.x,g=u/l}else e(O),e(k),!(D.x<=0)||w<p?(u-=D.x,p+=D.x):0<=D.y&&x<=v&&(M=!1),(!(0<=D.y)||v<x)&&(g+=D.y);u<0&&g<0?(d=E,m-=g=-g,p-=u=-u):u<0?(d=H,p-=u=-u):g<0&&(d=W,m-=g=-g);break;case H:if(l){if(0<=D.x&&(y<=f||x<=v)){M=!1;break}e(B),g=(u+=D.x)/l}else e(O),e(B),!(0<=D.x)||f<y?u+=D.x:0<=D.y&&x<=v&&(M=!1),(!(0<=D.y)||v<x)&&(g+=D.y);u<0&&g<0?(d=W,m-=g=-g,p-=u=-u):u<0?(d=N,p-=u=-u):g<0&&(d=E,m-=g=-g);break;case q:this.move(D.x,D.y),M=!1;break;case $:this.zoom((a=S({},i=c),o=0,z(i,function(n,t){delete a[t],z(a,function(t){var e=Math.abs(n.startX-t.startX),i=Math.abs(n.startY-t.startY),a=Math.abs(n.endX-t.endX),t=Math.abs(n.endY-t.endY),e=Math.sqrt(e*e+i*i),i=(Math.sqrt(a*a+t*t)-e)/e;Math.abs(i)>Math.abs(o)&&(o=i)})}),o),t),M=!1;break;case U:D.x&&D.y?(i=Wt(this.cropper),p=C.startX-i.left,m=C.startY-i.top,u=s.minWidth,g=s.minHeight,0<D.x?d=0<D.y?H:E:D.x<0&&(p-=u,d=0<D.y?N:W),D.y<0&&(m-=g),this.cropped||(X(this.cropBox,L),this.cropped=!0,this.limited&&this.limitCropBox(!0,!0))):M=!1}M&&(s.width=u,s.height=g,s.left=p,s.top=m,this.action=d,this.renderCropBox()),z(c,function(t){t.startX=t.endX,t.startY=t.endY})}},At={crop:function(){return!this.ready||this.cropped||this.disabled||(this.cropped=!0,this.limitCropBox(!0,!0),this.options.modal&&v(this.dragBox,n),X(this.cropBox,L),this.setCropBoxData(this.initialCropBoxData)),this},reset:function(){return this.ready&&!this.disabled&&(this.imageData=g({},this.initialImageData),this.canvasData=g({},this.initialCanvasData),this.cropBoxData=g({},this.initialCropBoxData),this.renderCanvas(),this.cropped)&&this.renderCropBox(),this},clear:function(){return this.cropped&&!this.disabled&&(g(this.cropBoxData,{left:0,top:0,width:0,height:0}),this.cropped=!1,this.renderCropBox(),this.limitCanvas(!0,!0),this.renderCanvas(),X(this.dragBox,n),v(this.cropBox,L)),this},replace:function(e){var t=1<arguments.length&&void 0!==arguments[1]&&arguments[1];return!this.disabled&&e&&(this.isImg&&(this.element.src=e),t?(this.url=e,this.image.src=e,this.ready&&(this.viewBoxImage.src=e,z(this.previews,function(t){t.getElementsByTagName("img")[0].src=e}))):(this.isImg&&(this.replaced=!0),this.options.data=null,this.uncreate(),this.load(e))),this},enable:function(){return this.ready&&this.disabled&&(this.disabled=!1,X(this.cropper,K)),this},disable:function(){return this.ready&&!this.disabled&&(this.disabled=!0,v(this.cropper,K)),this},destroy:function(){var t=this.element;return t[c]&&(t[c]=void 0,this.isImg&&this.replaced&&(t.src=this.originalUrl),this.uncreate()),this},move:function(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:t,i=this.canvasData,a=i.left,i=i.top;return this.moveTo(vt(t)?t:a+Number(t),vt(e)?e:i+Number(e))},moveTo:function(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:t,i=this.canvasData,a=!1;return t=Number(t),e=Number(e),this.ready&&!this.disabled&&this.options.movable&&(p(t)&&(i.left=t,a=!0),p(e)&&(i.top=e,a=!0),a)&&this.renderCanvas(!0),this},zoom:function(t,e){var i=this.canvasData;return t=Number(t),this.zoomTo(i.width*(t=t<0?1/(1-t):1+t)/i.naturalWidth,null,e)},zoomTo:function(t,e,i){var a,n,o,h=this.options,r=this.canvasData,s=r.width,c=r.height,d=r.naturalWidth,l=r.naturalHeight;if(0<=(t=Number(t))&&this.ready&&!this.disabled&&h.zoomable){h=d*t,l=l*t;if(!1===y(this.element,st,{ratio:t,oldRatio:s/d,originalEvent:i}))return this;i?(t=this.pointers,d=Wt(this.cropper),t=t&&Object.keys(t).length?(o=n=a=0,z(t,function(t){var e=t.startX,t=t.startY;a+=e,n+=t,o+=1}),{pageX:a/=o,pageY:n/=o}):{pageX:i.pageX,pageY:i.pageY},r.left-=(h-s)*((t.pageX-d.left-r.left)/s),r.top-=(l-c)*((t.pageY-d.top-r.top)/c)):u(e)&&p(e.x)&&p(e.y)?(r.left-=(h-s)*((e.x-r.left)/s),r.top-=(l-c)*((e.y-r.top)/c)):(r.left-=(h-s)/2,r.top-=(l-c)/2),r.width=h,r.height=l,this.renderCanvas(!0)}return this},rotate:function(t){return this.rotateTo((this.imageData.rotate||0)+Number(t))},rotateTo:function(t){return p(t=Number(t))&&this.ready&&!this.disabled&&this.options.rotatable&&(this.imageData.rotate=t%360,this.renderCanvas(!0,!0)),this},scaleX:function(t){var e=this.imageData.scaleY;return this.scale(t,p(e)?e:1)},scaleY:function(t){var e=this.imageData.scaleX;return this.scale(p(e)?e:1,t)},scale:function(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:t,i=this.imageData,a=!1;return t=Number(t),e=Number(e),this.ready&&!this.disabled&&this.options.scalable&&(p(t)&&(i.scaleX=t,a=!0),p(e)&&(i.scaleY=e,a=!0),a)&&this.renderCanvas(!0,!0),this},getData:function(){var i,a,t=0<arguments.length&&void 0!==arguments[0]&&arguments[0],e=this.options,n=this.imageData,o=this.canvasData,h=this.cropBoxData;return this.ready&&this.cropped?(i={x:h.left-o.left,y:h.top-o.top,width:h.width,height:h.height},a=n.width/n.naturalWidth,z(i,function(t,e){i[e]=t/a}),t&&(o=Math.round(i.y+i.height),h=Math.round(i.x+i.width),i.x=Math.round(i.x),i.y=Math.round(i.y),i.width=h-i.x,i.height=o-i.y)):i={x:0,y:0,width:0,height:0},e.rotatable&&(i.rotate=n.rotate||0),e.scalable&&(i.scaleX=n.scaleX||1,i.scaleY=n.scaleY||1),i},setData:function(t){var e,i=this.options,a=this.imageData,n=this.canvasData,o={};return this.ready&&!this.disabled&&u(t)&&(e=!1,i.rotatable&&p(t.rotate)&&t.rotate!==a.rotate&&(a.rotate=t.rotate,e=!0),i.scalable&&(p(t.scaleX)&&t.scaleX!==a.scaleX&&(a.scaleX=t.scaleX,e=!0),p(t.scaleY))&&t.scaleY!==a.scaleY&&(a.scaleY=t.scaleY,e=!0),e&&this.renderCanvas(!0,!0),i=a.width/a.naturalWidth,p(t.x)&&(o.left=t.x*i+n.left),p(t.y)&&(o.top=t.y*i+n.top),p(t.width)&&(o.width=t.width*i),p(t.height)&&(o.height=t.height*i),this.setCropBoxData(o)),this},getContainerData:function(){return this.ready?g({},this.containerData):{}},getImageData:function(){return this.sized?g({},this.imageData):{}},getCanvasData:function(){var e=this.canvasData,i={};return this.ready&&z(["left","top","width","height","naturalWidth","naturalHeight"],function(t){i[t]=e[t]}),i},setCanvasData:function(t){var e=this.canvasData,i=e.aspectRatio;return this.ready&&!this.disabled&&u(t)&&(p(t.left)&&(e.left=t.left),p(t.top)&&(e.top=t.top),p(t.width)?(e.width=t.width,e.height=t.width/i):p(t.height)&&(e.height=t.height,e.width=t.height*i),this.renderCanvas(!0)),this},getCropBoxData:function(){var t,e=this.cropBoxData;return(t=this.ready&&this.cropped?{left:e.left,top:e.top,width:e.width,height:e.height}:t)||{}},setCropBoxData:function(t){var e,i,a=this.cropBoxData,n=this.options.aspectRatio;return this.ready&&this.cropped&&!this.disabled&&u(t)&&(p(t.left)&&(a.left=t.left),p(t.top)&&(a.top=t.top),p(t.width)&&t.width!==a.width&&(e=!0,a.width=t.width),p(t.height)&&t.height!==a.height&&(i=!0,a.height=t.height),n&&(e?a.height=a.width/n:i&&(a.width=a.height*n)),this.renderCropBox()),this},getCroppedCanvas:function(){var t,e,i,a,n,o,h,r,s,c,d,l,p,m,u,g,f,v,w,b,y,x,M,C,D,B,k,O=0<arguments.length&&void 0!==arguments[0]?arguments[0]:{};return this.ready&&window.HTMLCanvasElement?(B=this.canvasData,u=this.image,l=this.imageData,a=B,v=O,g=l.aspectRatio,e=l.naturalWidth,n=l.naturalHeight,c=void 0===(c=l.rotate)?0:c,d=void 0===(d=l.scaleX)?1:d,l=void 0===(l=l.scaleY)?1:l,i=a.aspectRatio,r=a.naturalWidth,a=a.naturalHeight,h=void 0===(h=v.fillColor)?"transparent":h,p=void 0===(p=v.imageSmoothingEnabled)||p,m=void 0===(m=v.imageSmoothingQuality)?"low":m,o=void 0===(o=v.maxWidth)?1/0:o,k=void 0===(k=v.maxHeight)?1/0:k,t=void 0===(t=v.minWidth)?0:t,v=void 0===(v=v.minHeight)?0:v,w=document.createElement("canvas"),f=w.getContext("2d"),s=R({aspectRatio:i,width:o,height:k}),i=R({aspectRatio:i,width:t,height:v},"cover"),r=Math.min(s.width,Math.max(i.width,r)),s=Math.min(s.height,Math.max(i.height,a)),i=R({aspectRatio:g,width:o,height:k}),a=R({aspectRatio:g,width:t,height:v},"cover"),o=Math.min(i.width,Math.max(a.width,e)),k=Math.min(i.height,Math.max(a.height,n)),g=[-o/2,-k/2,o,k],w.width=Y(r),w.height=Y(s),f.fillStyle=h,f.fillRect(0,0,r,s),f.save(),f.translate(r/2,s/2),f.rotate(c*Math.PI/180),f.scale(d,l),f.imageSmoothingEnabled=p,f.imageSmoothingQuality=m,f.drawImage.apply(f,[u].concat(A(g.map(function(t){return Math.floor(Y(t))})))),f.restore(),t=w,this.cropped?(e=(v=this.getData(O.rounded)).x,i=v.y,a=v.width,n=v.height,1!=(o=t.width/Math.floor(B.naturalWidth))&&(e*=o,i*=o,a*=o,n*=o),h=R({aspectRatio:k=a/n,width:O.maxWidth||1/0,height:O.maxHeight||1/0}),r=R({aspectRatio:k,width:O.minWidth||0,height:O.minHeight||0},"cover"),c=(s=R({aspectRatio:k,width:O.width||(1!=o?t.width:a),height:O.height||(1!=o?t.height:n)})).width,d=s.height,c=Math.min(h.width,Math.max(r.width,c)),d=Math.min(h.height,Math.max(r.height,d)),p=(l=document.createElement("canvas")).getContext("2d"),l.width=Y(c),l.height=Y(d),p.fillStyle=O.fillColor||"transparent",p.fillRect(0,0,c,d),m=O.imageSmoothingEnabled,u=O.imageSmoothingQuality,p.imageSmoothingEnabled=void 0===m||m,u&&(p.imageSmoothingQuality=u),g=t.width,f=t.height,w=i,(v=e)<=-a||g<v?C=x=b=v=0:v<=0?(x=-v,v=0,C=b=Math.min(g,a+v)):v<=g&&(x=0,C=b=Math.min(a,g-v)),b<=0||w<=-n||f<w?D=M=y=w=0:w<=0?(M=-w,w=0,D=y=Math.min(f,n+w)):w<=f&&(M=0,D=y=Math.min(n,f-w)),B=[v,w,b,y],0<C&&0<D&&B.push(x*(k=c/a),M*k,C*k,D*k),p.drawImage.apply(p,[t].concat(A(B.map(function(t){return Math.floor(Y(t))})))),l):t):null},setAspectRatio:function(t){var e=this.options;return this.disabled||vt(t)||(e.aspectRatio=Math.max(0,t)||NaN,this.ready&&(this.initCropBox(),this.cropped)&&this.renderCropBox()),this},setDragMode:function(t){var e,i,a=this.options,n=this.dragBox,o=this.face;return this.ready&&!this.disabled&&(i=a.movable&&t===J,a.dragMode=t=(e=t===F)||i?t:_,w(n,d,t),r(n,Q,e),r(n,V,i),a.cropBoxMovable||(w(o,d,t),r(o,Q,e),r(o,V,i))),this}},Pt=h.Cropper,It=function(){function n(t){var e=1<arguments.length&&void 0!==arguments[1]?arguments[1]:{},i=this,a=n;if(!(i instanceof a))throw new TypeError("Cannot call a class as a function");if(!t||!mt.test(t.tagName))throw new Error("The first argument is required and must be an <img> or <canvas> element.");this.element=t,this.options=g({},ut,u(e)&&e),this.cropped=!1,this.disabled=!1,this.pointers={},this.ready=!1,this.reloading=!1,this.replaced=!1,this.sized=!1,this.sizing=!1,this.init()}var t,e,i;return t=n,i=[{key:"noConflict",value:function(){return window.Cropper=Pt,n}},{key:"setDefaults",value:function(t){g(ut,u(t)&&t)}}],(e=[{key:"init",value:function(){var t,e=this.element,i=e.tagName.toLowerCase();if(!e[c]){if(e[c]=this,"img"===i){if(this.isImg=!0,t=e.getAttribute("src")||"",!(this.originalUrl=t))return;t=e.src}else"canvas"===i&&window.HTMLCanvasElement&&(t=e.toDataURL());this.load(t)}}},{key:"load",value:function(t){var e,i,a,n,o,h,r=this;t&&(this.url=t,this.imageData={},e=this.element,(i=this.options).rotatable||i.scalable||(i.checkOrientation=!1),i.checkOrientation&&window.ArrayBuffer?lt.test(t)?pt.test(t)?this.read((h=(h=t).replace(Xt,""),a=atob(h),h=new ArrayBuffer(a.length),z(n=new Uint8Array(h),function(t,e){n[e]=a.charCodeAt(e)}),h)):this.clone():(o=new XMLHttpRequest,h=this.clone.bind(this),this.reloading=!0,(this.xhr=o).onabort=h,o.onerror=h,o.ontimeout=h,o.onprogress=function(){o.getResponseHeader("content-type")!==ct&&o.abort()},o.onload=function(){r.read(o.response)},o.onloadend=function(){r.reloading=!1,r.xhr=null},i.checkCrossOrigin&&Lt(t)&&e.crossOrigin&&(t=zt(t)),o.open("GET",t,!0),o.responseType="arraybuffer",o.withCredentials="use-credentials"===e.crossOrigin,o.send()):this.clone())}},{key:"read",value:function(t){var e=this.options,i=this.imageData,a=Rt(t),n=0,o=1,h=1;1<a&&(this.url=function(t,e){for(var i=[],a=new Uint8Array(t);0<a.length;)i.push(Yt.apply(null,yt(a.subarray(0,8192)))),a=a.subarray(8192);return"data:".concat(e,";base64,").concat(btoa(i.join("")))}(t,ct),n=(t=function(t){var e=0,i=1,a=1;switch(t){case 2:i=-1;break;case 3:e=-180;break;case 4:a=-1;break;case 5:e=90,a=-1;break;case 6:e=90;break;case 7:e=90,i=-1;break;case 8:e=-90}return{rotate:e,scaleX:i,scaleY:a}}(a)).rotate,o=t.scaleX,h=t.scaleY),e.rotatable&&(i.rotate=n),e.scalable&&(i.scaleX=o,i.scaleY=h),this.clone()}},{key:"clone",value:function(){var t=this.element,e=this.url,i=t.crossOrigin,a=e,n=(this.options.checkCrossOrigin&&Lt(e)&&(i=i||"anonymous",a=zt(e)),this.crossOrigin=i,this.crossOriginUrl=a,document.createElement("img"));i&&(n.crossOrigin=i),n.src=a||e,n.alt=t.alt||"The image to crop",(this.image=n).onload=this.start.bind(this),n.onerror=this.stop.bind(this),v(n,Z),t.parentNode.insertBefore(n,t.nextSibling)}},{key:"start",value:function(){function t(t,e){g(a.imageData,{naturalWidth:t,naturalHeight:e,aspectRatio:t/e}),a.initialImageData=g({},a.imageData),a.sizing=!1,a.sized=!0,a.build()}var e,i,a=this,n=this.image,o=(n.onload=null,n.onerror=null,this.sizing=!0,h.navigator&&/(?:iPad|iPhone|iPod).*?AppleWebKit/i.test(h.navigator.userAgent));n.naturalWidth&&!o?t(n.naturalWidth,n.naturalHeight):(e=document.createElement("img"),i=document.body||document.documentElement,(this.sizingImage=e).onload=function(){t(e.width,e.height),o||i.removeChild(e)},e.src=n.src,o||(e.style.cssText="left:0;max-height:none!important;max-width:none!important;min-height:0!important;min-width:0!important;opacity:0;position:absolute;top:0;z-index:-1;",i.appendChild(e)))}},{key:"stop",value:function(){var t=this.image;t.onload=null,t.onerror=null,t.parentNode.removeChild(t),this.image=null}},{key:"build",value:function(){var t,e,i,a,n,o,h,r,s;this.sized&&!this.ready&&(t=this.element,e=this.options,i=this.image,a=t.parentNode,(n=document.createElement("div")).innerHTML='<div class="cropper-container" touch-action="none"><div class="cropper-wrap-box"><div class="cropper-canvas"></div></div><div class="cropper-drag-box"></div><div class="cropper-crop-box"><span class="cropper-view-box"></span><span class="cropper-dashed dashed-h"></span><span class="cropper-dashed dashed-v"></span><span class="cropper-center"></span><span class="cropper-face"></span><span class="cropper-line line-e" data-cropper-action="e"></span><span class="cropper-line line-n" data-cropper-action="n"></span><span class="cropper-line line-w" data-cropper-action="w"></span><span class="cropper-line line-s" data-cropper-action="s"></span><span class="cropper-point point-e" data-cropper-action="e"></span><span class="cropper-point point-n" data-cropper-action="n"></span><span class="cropper-point point-w" data-cropper-action="w"></span><span class="cropper-point point-s" data-cropper-action="s"></span><span class="cropper-point point-ne" data-cropper-action="ne"></span><span class="cropper-point point-nw" data-cropper-action="nw"></span><span class="cropper-point point-sw" data-cropper-action="sw"></span><span class="cropper-point point-se" data-cropper-action="se"></span></div></div>',o=(n=n.querySelector(".".concat(c,"-container"))).querySelector(".".concat(c,"-canvas")),h=n.querySelector(".".concat(c,"-drag-box")),s=(r=n.querySelector(".".concat(c,"-crop-box"))).querySelector(".".concat(c,"-face")),this.container=a,this.cropper=n,this.canvas=o,this.dragBox=h,this.cropBox=r,this.viewBox=n.querySelector(".".concat(c,"-view-box")),this.face=s,o.appendChild(i),v(t,L),a.insertBefore(n,t.nextSibling),X(i,Z),this.initPreview(),this.bind(),e.initialAspectRatio=Math.max(0,e.initialAspectRatio)||NaN,e.aspectRatio=Math.max(0,e.aspectRatio)||NaN,e.viewMode=Math.max(0,Math.min(3,Math.round(e.viewMode)))||0,v(r,L),e.guides||v(r.getElementsByClassName("".concat(c,"-dashed")),L),e.center||v(r.getElementsByClassName("".concat(c,"-center")),L),e.background&&v(n,"".concat(c,"-bg")),e.highlight||v(s,G),e.cropBoxMovable&&(v(s,V),w(s,d,I)),e.cropBoxResizable||(v(r.getElementsByClassName("".concat(c,"-line")),L),v(r.getElementsByClassName("".concat(c,"-point")),L)),this.render(),this.ready=!0,this.setDragMode(e.dragMode),e.autoCrop&&this.crop(),this.setData(e.data),l(e.ready)&&b(t,"ready",e.ready,{once:!0}),y(t,"ready"))}},{key:"unbuild",value:function(){var t;this.ready&&(this.ready=!1,this.unbind(),this.resetPreview(),(t=this.cropper.parentNode)&&t.removeChild(this.cropper),X(this.element,L))}},{key:"uncreate",value:function(){this.ready?(this.unbuild(),this.ready=!1,this.cropped=!1):this.sizing?(this.sizingImage.onload=null,this.sizing=!1,this.sized=!1):this.reloading?(this.xhr.onabort=null,this.xhr.abort()):this.image&&this.stop()}}])&&j(t.prototype,e),i&&j(t,i),Object.defineProperty(t,"prototype",{writable:!1}),n}();return g(It.prototype,t,i,e,St,jt,At),It});
</script>
<script>
/* ---- string-art-core.js (shared algorithm, same file used by the standalone client and the CLI's test suite) ---- */
/*
 * String Art Core (browser + Node-testable)
 * ==========================================
 * Pure, DOM-free functions for the greedy nail-and-thread algorithm used by
 * index.html. Kept dependency-free so the exact same file can be loaded as a
 * plain script tag in the browser, or required() from a plain Node test
 * script (see test-core.js), without the two ever drifting apart.
 */
(function (root) {
  "use strict";

  // ---- Pin geometry ----------------------------------------------------

  // Pin 0 at angle 0 (three o'clock), going clockwise in canvas coordinates
  // (y grows downward), evenly spaced. Matches the Python generator so a
  // sequence means the same physical nail regardless of which tool made it.
  function pinPositions(numPins, size) {
    const cx = (size - 1) / 2;
    const cy = (size - 1) / 2;
    const r = size / 2 - 1;
    const pins = new Array(numPins);
    for (let i = 0; i < numPins; i++) {
      const theta = (2 * Math.PI * i) / numPins;
      pins[i] = { x: cx + r * Math.cos(theta), y: cy + r * Math.sin(theta) };
    }
    return pins;
  }

  // ---- Image -> residual/error array ------------------------------------

  // RGBA Uint8ClampedArray -> Float32Array of luma (ITU-R BT.601 weights).
  function toGrayscale(rgba, size) {
    const out = new Float32Array(size * size);
    for (let i = 0, p = 0; i < out.length; i++, p += 4) {
      out[i] = 0.299 * rgba[p] + 0.587 * rgba[p + 1] + 0.114 * rgba[p + 2];
    }
    return out;
  }

  // Zero out any contribution from outside the inscribed circle by pinning
  // it to whichever brightness makes that pixel's later residual 0.
  function maskOutsideCircle(brightness, size, mode) {
    const cx = (size - 1) / 2;
    const cy = (size - 1) / 2;
    const r = size / 2;
    const fill = mode === "dark" ? 0 : 255;
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > r * r) brightness[y * size + x] = fill;
      }
    }
    return brightness;
  }

  // Light mode targets dark regions (error = 255 - brightness); dark mode
  // targets bright regions (error = brightness) -- e.g. a light thread on a
  // dark board.
  function toResidual(brightness, mode) {
    const out = new Float32Array(brightness.length);
    if (mode === "dark") {
      out.set(brightness);
    } else {
      for (let i = 0; i < brightness.length; i++) out[i] = 255 - brightness[i];
    }
    return out;
  }

  // ---- Candidate line precompute ----------------------------------------

  // For every pin pair at least `minSep` indices apart (skips near-duplicate,
  // near-zero-length chords), precompute the flat pixel indices (y*size+x)
  // the connecting line passes through. Keyed "i_j" with i < j.
  function precomputeLines(pins, size, minSep) {
    const n = pins.length;
    const map = new Map();
    for (let i = 0; i < n; i++) {
      for (let j = i + 1; j < n; j++) {
        const sep = Math.min(j - i, n - (j - i));
        if (sep < minSep) continue;
        const p0 = pins[i], p1 = pins[j];
        const dx = p1.x - p0.x, dy = p1.y - p0.y;
        const dist = Math.hypot(dx, dy);
        const steps = Math.max(2, Math.round(dist));
        const idx = new Int32Array(steps);
        let count = 0;
        let lastFlat = -1;
        for (let k = 0; k < steps; k++) {
          const t = k / (steps - 1);
          let x = Math.round(p0.x + t * dx);
          let y = Math.round(p0.y + t * dy);
          if (x < 0) x = 0; else if (x >= size) x = size - 1;
          if (y < 0) y = 0; else if (y >= size) y = size - 1;
          const flat = y * size + x;
          if (flat !== lastFlat) {
            idx[count++] = flat;
            lastFlat = flat;
          }
        }
        map.set(i + "_" + j, count === steps ? idx : idx.subarray(0, count));
      }
    }
    return map;
  }

  function lineKey(a, b) {
    return a < b ? a + "_" + b : b + "_" + a;
  }

  // ---- Greedy generation (chunkable, so the caller can yield to the UI) --

  function createGenerationState(residual, numPins, startPin, recentLimit) {
    return {
      residual,               // Float32Array, mutated in place as lines are "drawn"
      numPins,
      current: startPin,
      sequence: [startPin],
      recent: [],
      recentLimit: Math.max(4, recentLimit),
      done: false,
    };
  }

  // Advances the greedy search by up to `count` more chords. Returns the
  // pin indices newly added to the sequence this call (for incremental
  // rendering) and mutates `state` in place. Sets state.done when no
  // improving chord remains or `maxChords` total has been reached.
  function stepChunk(state, lineMap, weight, count, maxChords) {
    const added = [];
    for (let k = 0; k < count && state.sequence.length - 1 < maxChords; k++) {
      let bestPin = -1, bestScore = -1, bestIdx = null;
      for (let j = 0; j < state.numPins; j++) {
        if (j === state.current || state.recent.indexOf(j) !== -1) continue;
        const idx = lineMap.get(lineKey(state.current, j));
        if (!idx) continue;
        let sum = 0;
        for (let m = 0; m < idx.length; m++) sum += state.residual[idx[m]];
        const score = sum / idx.length;
        if (score > bestScore) { bestScore = score; bestPin = j; bestIdx = idx; }
      }
      if (bestPin === -1 || bestScore <= 0) { state.done = true; break; }

      for (let m = 0; m < bestIdx.length; m++) {
        const v = state.residual[bestIdx[m]] - weight;
        state.residual[bestIdx[m]] = v > 0 ? v : 0;
      }
      state.sequence.push(bestPin);
      added.push(bestPin);
      state.recent.push(bestPin);
      if (state.recent.length > state.recentLimit) state.recent.shift();
      state.current = bestPin;
    }
    if (state.sequence.length - 1 >= maxChords) state.done = true;
    return added;
  }

  // ---- Export helpers ----------------------------------------------------

  function formatStepsText(sequence) {
    let out = "String Art Steps (" + (sequence.length - 1) + " chords, " + sequence.length + " nail stops)\n";
    for (let i = 1; i < sequence.length; i++) {
      out += i + ": From Pin " + sequence[i - 1] + " to Pin " + sequence[i] + "\n";
    }
    return out;
  }

  function formatSequenceText(sequence) {
    return sequence.join("\n") + "\n";
  }

  function svgFromSequence(pins, sequence, size, bgColor, strokeColor, strokeWidth) {
    let svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' + size + " " + size +
      '" width="' + size + '" height="' + size + '">';
    svg += '<rect width="100%" height="100%" fill="' + bgColor + '"/>';
    svg += '<g fill="none" stroke="' + strokeColor + '" stroke-width="' + strokeWidth + '" stroke-linecap="round">';
    for (let i = 1; i < sequence.length; i++) {
      const a = pins[sequence[i - 1]], b = pins[sequence[i]];
      svg += '<line x1="' + a.x.toFixed(2) + '" y1="' + a.y.toFixed(2) +
        '" x2="' + b.x.toFixed(2) + '" y2="' + b.y.toFixed(2) + '"/>';
    }
    svg += "</g></svg>";
    return svg;
  }

  const api = {
    pinPositions,
    toGrayscale,
    maskOutsideCircle,
    toResidual,
    precomputeLines,
    lineKey,
    createGenerationState,
    stepChunk,
    formatStepsText,
    formatSequenceText,
    svgFromSequence,
  };

  if (typeof module !== "undefined" && module.exports) {
    module.exports = api; // Node (tests)
  } else {
    root.StringArtCore = api; // browser <script> tag
  }
})(typeof window !== "undefined" ? window : globalThis);

</script>
<script>
(function () {
  "use strict";
  const C = window.StringArtCore;
  const WORK_SIZE = 600; // internal square working resolution, px

  const canvas = document.getElementById("artCanvas");
  const ctx = canvas.getContext("2d");
  canvas.width = WORK_SIZE;
  canvas.height = WORK_SIZE;

  const fileInput = document.getElementById("fileInput");
  const fileBtnLabel = document.getElementById("fileBtnLabel");
  const dropHint = document.getElementById("dropHint");
  const darkModeToggle = document.getElementById("darkMode");
  const numPinsInput = document.getElementById("numPins");
  const numChordsInput = document.getElementById("numChords");
  const lineWeightInput = document.getElementById("lineWeight");
  const generateBtn = document.getElementById("generateBtn");
  const progressTrack = document.getElementById("progressTrack");
  const progressFill = document.getElementById("progressFill");
  const progressLabel = document.getElementById("progressLabel");
  const statLine = document.getElementById("statLine");
  const downloadStepsBtn = document.getElementById("downloadStepsBtn");
  const downloadSvgBtn = document.getElementById("downloadSvgBtn");
  const sendBtn = document.getElementById("sendBtn");
  const sendMsg = document.getElementById("sendMsg");

  const cropOverlay = document.getElementById("cropOverlay");
  const cropImage = document.getElementById("cropImage");
  const cropCancelBtn = document.getElementById("cropCancelBtn");
  const cropConfirmBtn = document.getElementById("cropConfirmBtn");

  let croppedCanvas = null;
  let pins = null;
  let lastResult = null; // { sequence, numPins }
  let cropper = null;
  let generating = false;

  ["numPins", "numChords", "lineWeight"].forEach((id) => {
    const el = document.getElementById(id);
    const out = document.getElementById(id + "Val");
    el.addEventListener("input", () => { out.textContent = el.value; });
  });

  function bg() { return darkModeToggle.checked ? "#262626" : "#ffffff"; }
  function stroke() { return darkModeToggle.checked ? "rgba(255,255,255,0.55)" : "rgba(0,0,0,0.55)"; }
  function paintBackground() { ctx.fillStyle = bg(); ctx.fillRect(0, 0, canvas.width, canvas.height); }

  darkModeToggle.addEventListener("change", () => {
    if (!croppedCanvas) { paintBackground(); return; }
    if (lastResult) runGeneration();
  });

  // ---- Image select -> crop modal ----------------------------------------

  fileInput.addEventListener("change", () => {
    const f = fileInput.files[0];
    if (!f) return;
    const reader = new FileReader();
    reader.onload = (e) => {
      cropImage.src = e.target.result;
      cropOverlay.hidden = false;
      if (cropper) cropper.destroy();
      cropper = new Cropper(cropImage, { aspectRatio: 1, viewMode: 1, autoCropArea: 1, background: false });
    };
    reader.readAsDataURL(f);
  });

  cropCancelBtn.addEventListener("click", () => {
    cropOverlay.hidden = true;
    if (cropper) { cropper.destroy(); cropper = null; }
  });

  cropConfirmBtn.addEventListener("click", () => {
    const srcCanvas = cropper.getCroppedCanvas({ width: WORK_SIZE, height: WORK_SIZE });
    cropOverlay.hidden = true;
    cropper.destroy(); cropper = null;

    croppedCanvas = srcCanvas;
    fileBtnLabel.textContent = fileInput.files[0] ? fileInput.files[0].name : "Choose a photo…";
    dropHint.hidden = true;

    const size = WORK_SIZE;
    const c = document.createElement("canvas");
    c.width = size; c.height = size;
    const cctx = c.getContext("2d");
    cctx.drawImage(srcCanvas, 0, 0, size, size);
    const imgData = cctx.getImageData(0, 0, size, size);
    const gray = C.toGrayscale(imgData.data, size);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    paintBackground();
    const out = ctx.getImageData(0, 0, size, size);
    for (let i = 0, p = 0; i < gray.length; i++, p += 4) {
      const v = gray[i];
      out.data[p] = v; out.data[p + 1] = v; out.data[p + 2] = v; out.data[p + 3] = 255;
    }
    ctx.putImageData(out, 0, 0);

    generateBtn.disabled = false;
    lastResult = null;
    downloadStepsBtn.disabled = true;
    downloadSvgBtn.disabled = true;
    sendBtn.disabled = true;
  });

  // ---- Generation ---------------------------------------------------------

  function setProgress(pct, label) {
    progressTrack.hidden = false;
    progressLabel.hidden = false;
    progressFill.style.width = Math.max(0, Math.min(100, pct)) + "%";
    progressLabel.textContent = label;
  }

  generateBtn.addEventListener("click", runGeneration);

  function runGeneration() {
    if (generating || !croppedCanvas) return;
    generating = true;
    generateBtn.disabled = true;
    downloadStepsBtn.disabled = true;
    downloadSvgBtn.disabled = true;
    sendBtn.disabled = true;

    const numPins = parseInt(numPinsInput.value, 10);
    const numChords = parseInt(numChordsInput.value, 10);
    const weight = parseFloat(lineWeightInput.value);
    const mode = darkModeToggle.checked ? "dark" : "light";
    const size = WORK_SIZE;

    const c = document.createElement("canvas");
    c.width = size; c.height = size;
    const cctx = c.getContext("2d");
    cctx.drawImage(croppedCanvas, 0, 0, size, size);
    const imgData = cctx.getImageData(0, 0, size, size);

    const brightness = C.toGrayscale(imgData.data, size);
    C.maskOutsideCircle(brightness, size, mode);
    const residual = C.toResidual(brightness, mode);

    pins = C.pinPositions(numPins, size);
    setProgress(0, "Precomputing candidate lines…");

    requestAnimationFrame(() => {
      const minSep = Math.max(2, Math.round(numPins / 20));
      const lineMap = C.precomputeLines(pins, size, minSep);
      const state = C.createGenerationState(residual, numPins, 0, Math.max(4, Math.round(numPins / 20)));

      paintBackground();
      ctx.strokeStyle = stroke();
      ctx.lineWidth = 0.6;

      const CHUNK = 25;
      function tick() {
        const added = C.stepChunk(state, lineMap, weight, CHUNK, numChords);
        ctx.beginPath();
        for (let k = 0; k < added.length; k++) {
          const idx = state.sequence.length - added.length + k;
          const a = pins[state.sequence[idx - 1]], b = pins[state.sequence[idx]];
          ctx.moveTo(a.x, a.y);
          ctx.lineTo(b.x, b.y);
        }
        ctx.stroke();

        const placed = state.sequence.length - 1;
        setProgress((placed / numChords) * 100, placed + " / " + numChords + " chords");

        if (!state.done && placed < numChords) {
          setTimeout(tick, 0);
        } else {
          finishGeneration(state.sequence, numPins);
        }
      }
      tick();
    });
  }

  function finishGeneration(sequence, numPins) {
    generating = false;
    generateBtn.disabled = false;
    lastResult = { sequence, numPins };
    setProgress(100, "Done — " + (sequence.length - 1) + " chords");
    statLine.hidden = false;
    statLine.textContent = "pins: " + numPins + "    chords: " + (sequence.length - 1);
    downloadStepsBtn.disabled = false;
    downloadSvgBtn.disabled = false;
    sendBtn.disabled = false;
  }

  function triggerDownload(filename, content, type) {
    const blob = new Blob([content], { type });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url; a.download = filename;
    document.body.appendChild(a); a.click(); document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  downloadStepsBtn.addEventListener("click", () => {
    if (!lastResult) return;
    triggerDownload("string_art_steps.txt", C.formatStepsText(lastResult.sequence), "text/plain;charset=utf-8");
  });

  downloadSvgBtn.addEventListener("click", () => {
    if (!lastResult) return;
    const svg = C.svgFromSequence(pins, lastResult.sequence, WORK_SIZE, bg(),
      darkModeToggle.checked ? "#ffffff" : "#000000", 0.6);
    triggerDownload("string_art.svg", svg, "image/svg+xml;charset=utf-8");
  });

  // ---- Send to THIS machine (same origin -- no address, no CORS needed) --

  sendBtn.addEventListener("click", async () => {
    sendMsg.className = "msg";
    if (!lastResult) { sendMsg.textContent = "Generate a sequence first."; sendMsg.className = "msg show err"; return; }

    sendBtn.disabled = true;
    sendBtn.textContent = "Sending…";
    try {
      await fetch("/config", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "numNails=" + lastResult.numPins,
      });
      const r = await fetch("/upload", {
        method: "POST",
        headers: { "Content-Type": "text/plain" },
        body: lastResult.sequence.join(","),
      });
      if (!r.ok) throw new Error("HTTP " + r.status);
      sendMsg.textContent = "Sequence loaded — " + lastResult.sequence.length + " nail stops. Use Wrap below to start.";
      sendMsg.className = "msg show ok";
      refreshIndexer();
    } catch (err) {
      sendMsg.textContent = "Could not save the sequence to the machine. Try again.";
      sendMsg.className = "msg show err";
    } finally {
      sendBtn.disabled = false;
      sendBtn.textContent = "Send to this machine";
    }
  });

  // ---- Wrap section (indexer controls, same device, relative fetches) ----

  const idxNailNum = document.getElementById("idxNailNum");
  const idxProgressText = document.getElementById("idxProgressText");
  const idxBar = document.getElementById("idxBar");
  const idxAutoBtn = document.getElementById("idxAutoBtn");
  const idxNumNails = document.getElementById("idxNumNails");
  const idxStepDelay = document.getElementById("idxStepDelay");
  const idxAutoMs = document.getElementById("idxAutoMs");
  const idxFindHomeBtn = document.getElementById("idxFindHomeBtn");
  const idxSwitchText = document.getElementById("idxSwitchText");
  const idxReverseDir = document.getElementById("idxReverseDir");
  const idxDirTestBtn = document.getElementById("idxDirTestBtn");
  const idxDirTestText = document.getElementById("idxDirTestText");
  const feederAutoFeed = document.getElementById("feederAutoFeed");
  const feederFeedBtn = document.getElementById("feederFeedBtn");
  const feederRestAngle = document.getElementById("feederRestAngle");
  const feederFeedAngle = document.getElementById("feederFeedAngle");
  const feederPulseMs = document.getElementById("feederPulseMs");
  const feederSaveBtn = document.getElementById("feederSaveBtn");
  let idxAutoOn = false;

  async function refreshIndexer() {
    try {
      const r = await fetch("/status");
      const j = await r.json();
      idxNailNum.textContent = j.currentNail;
      idxProgressText.textContent = j.homing
        ? "homing…"
        : "step " + j.currentIndex + " / " + j.total + "  (next: nail " + j.nextNail + ")";
      idxBar.value = j.total ? (100 * j.currentIndex / j.total) : 0;
      idxNumNails.value = j.numNails;
      idxStepDelay.value = j.stepDelay;
      idxAutoMs.value = j.autoMs;
      if (document.activeElement !== idxReverseDir) idxReverseDir.checked = (j.dirSign < 0);
      idxAutoOn = j.autoRunning;
      idxAutoBtn.textContent = idxAutoOn ? "Stop Auto" : "Start Auto";
      idxFindHomeBtn.disabled = j.homing;
      idxSwitchText.textContent = "limit switch: " + (j.switchTriggered ? "TRIGGERED" : "open") +
        (j.homing ? "  (homing…)" : "") +
        (j.homeError ? "  — not found last time, check wiring" : "");
      feederAutoFeed.checked = j.feederAutoFeed;
      feederRestAngle.value = j.feederRestAngle;
      feederFeedAngle.value = j.feederFeedAngle;
      feederPulseMs.value = j.feederPulseMs;
    } catch (err) { /* machine momentarily busy mid-step; next poll will catch up */ }
  }

  async function idxAct(cmd, value) {
    let body = "cmd=" + cmd;
    if (value !== undefined) body += "&value=" + value;
    await fetch("/action", { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" }, body });
    refreshIndexer();
  }

  document.getElementById("idxPrevBtn").addEventListener("click", () => idxAct("prev"));
  document.getElementById("idxNextBtn").addEventListener("click", () => idxAct("next"));
  document.getElementById("idxHomeBtn").addEventListener("click", () => idxAct("home"));
  idxAutoBtn.addEventListener("click", () => idxAct(idxAutoOn ? "stop" : "start"));
  idxFindHomeBtn.addEventListener("click", () => idxAct("findhome"));
  feederFeedBtn.addEventListener("click", () => idxAct("feed"));
  document.getElementById("idxGotoBtn").addEventListener("click", () => {
    idxAct("goto", document.getElementById("idxGotoVal").value);
  });
  async function saveMachineConfig() {
    await fetch("/config", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: "numNails=" + idxNumNails.value + "&stepDelay=" + idxStepDelay.value +
        "&autoMs=" + idxAutoMs.value + "&dirSign=" + (idxReverseDir.checked ? -1 : 1),
    });
    refreshIndexer();
  }
  document.getElementById("idxSaveConfigBtn").addEventListener("click", saveMachineConfig);
  idxReverseDir.addEventListener("change", saveMachineConfig);

  // Sends the disc to nail 0, then a quarter, half and three-quarters of the
  // way round. Watch the feeder: the nail arriving must be the one named.
  idxDirTestBtn.addEventListener("click", async () => {
    const n = parseInt(idxNumNails.value, 10) || 200;
    const stops = [0, Math.round(n / 4), Math.round(n / 2), Math.round(3 * n / 4), 0];
    idxDirTestBtn.disabled = true;
    for (const s of stops) {
      idxDirTestText.textContent = "at feeder should be: nail " + s;
      await idxAct("goto", s);
      await new Promise(r => setTimeout(r, 2500));
    }
    idxDirTestText.textContent = "done — if the nails did not match, flip the toggle above";
    idxDirTestBtn.disabled = false;
  });

  async function saveFeederConfig() {
    await fetch("/config", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: "feederRestAngle=" + feederRestAngle.value + "&feederFeedAngle=" + feederFeedAngle.value +
        "&feederPulseMs=" + feederPulseMs.value + "&feederAutoFeed=" + (feederAutoFeed.checked ? 1 : 0),
    });
    refreshIndexer();
  }
  feederAutoFeed.addEventListener("change", saveFeederConfig);
  feederSaveBtn.addEventListener("click", saveFeederConfig);

  setInterval(refreshIndexer, 1000);
  refreshIndexer();
  paintBackground();
})();
</script>
</body>
</html>

)STRINGARTPAGE";

// The machine's own page (served from here) is always same-origin, but the
// browser-based generator (a static page you open separately, or host
// anywhere on your LAN) is a different origin and needs these headers to be
// allowed to fetch() this API directly. All requests it makes (text/plain
// or form-urlencoded, no custom headers) are CORS-simple and shouldn't
// trigger a preflight, but the OPTIONS routes below handle it if a browser
// sends one anyway.
void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleCorsPreflight() {
  sendCorsHeaders();
  server.send(204);
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  uint16_t curNail = sequence.empty() ? 0 : sequence[min((size_t)currentIndex, sequence.size() - 1)];
  uint16_t nextNail = (sequence.empty() || currentIndex + 1 >= (int)sequence.size())
                           ? curNail
                           : sequence[currentIndex + 1];
  String json = "{";
  json += "\"currentIndex\":" + String(currentIndex) + ",";
  json += "\"total\":" + String(sequence.empty() ? 0 : (int)sequence.size() - 1) + ",";
  json += "\"currentNail\":" + String(curNail) + ",";
  json += "\"nextNail\":" + String(nextNail) + ",";
  json += "\"numNails\":" + String(numNails) + ",";
  json += "\"stepDelay\":" + String(stepDelayMs) + ",";
  json += "\"autoMs\":" + String(autoAdvanceMs) + ",";
  json += "\"autoRunning\":" + String(autoRunning ? "true" : "false") + ",";
  json += "\"feederRestAngle\":" + String(feederRestAngle) + ",";
  json += "\"feederFeedAngle\":" + String(feederFeedAngle) + ",";
  json += "\"feederPulseMs\":" + String(feederPulseMs) + ",";
  json += "\"feederAutoFeed\":" + String(feederAutoFeed ? "true" : "false") + ",";
  json += "\"dirSign\":" + String((int)dirSign) + ",";
  json += "\"switchTriggered\":" + String(digitalRead(PIN_LIMIT_SWITCH) == LOW ? "true" : "false") + ",";
  json += "\"homing\":" + String(homing ? "true" : "false") + ",";
  json += "\"homeError\":" + String(homeError ? "true" : "false");
  json += "}";
  sendCorsHeaders();
  server.send(200, "application/json", json);
}

void handleUpload() {
  parseAndSaveSequence(server.arg("plain"));
  sendCorsHeaders();
  server.send(200, "text/plain", "ok");
}

void handleConfig() {
  if (server.hasArg("numNails")) numNails = server.arg("numNails").toInt();
  if (server.hasArg("stepDelay")) stepDelayMs = server.arg("stepDelay").toInt();
  if (server.hasArg("autoMs")) autoAdvanceMs = server.arg("autoMs").toInt();
  if (server.hasArg("dirSign")) dirSign = (server.arg("dirSign").toInt() < 0) ? -1 : 1;
  if (server.hasArg("feederRestAngle")) feederRestAngle = (uint8_t)server.arg("feederRestAngle").toInt();
  if (server.hasArg("feederFeedAngle")) feederFeedAngle = (uint8_t)server.arg("feederFeedAngle").toInt();
  if (server.hasArg("feederPulseMs")) feederPulseMs = (uint16_t)server.arg("feederPulseMs").toInt();
  if (server.hasArg("feederAutoFeed")) feederAutoFeed = server.arg("feederAutoFeed").toInt() != 0;
  saveConfig();
  if (!feederActive) feederServo.write(feederRestAngle); // reflect a new rest angle immediately
  sendCorsHeaders();
  server.send(200, "text/plain", "ok");
}

void handleAction() {
  String cmd = server.arg("cmd");
  if (homing && cmd != "findhome") {
    // Ignore everything else while a homing seek is in flight -- the UI
    // disables these buttons too, but guard here in case of a stale page.
  } else if (cmd == "next") {
    if (!sequence.empty() && currentIndex + 1 < (int)sequence.size()) {
      currentIndex++;
      beginMoveToStep(nailToStep(sequence[currentIndex]));
      feederPendingAfterMove = feederAutoFeed;
      saveState();
    }
  } else if (cmd == "prev") {
    if (!sequence.empty() && currentIndex > 0) {
      currentIndex--;
      beginMoveToStep(nailToStep(sequence[currentIndex]));
      saveState();
    }
  } else if (cmd == "home") {
    // Declare the disc's CURRENT physical position to be nail 0, without moving it.
    currentStep = 0;
    targetStep = 0;
    stepping = false;
    currentIndex = 0;
    saveState();
  } else if (cmd == "findhome") {
    // Physically verified homing: slowly seek until the limit switch trips.
    int8_t dir = (server.hasArg("value") && server.arg("value").toInt() < 0) ? -1 : 1;
    startHoming(dir);
  } else if (cmd == "feed") {
    startFeederPulse();
  } else if (cmd == "goto") {
    long nail = server.arg("value").toInt();
    beginMoveToStep(nailToStep((uint16_t)nail));
  } else if (cmd == "start") {
    autoRunning = true;
    lastAutoAdvanceAt = millis();
  } else if (cmd == "stop") {
    autoRunning = false;
  }
  sendCorsHeaders();
  server.send(200, "text/plain", "ok");
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nCould not join WiFi, starting fallback AP: " + String(AP_FALLBACK_SSID));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_FALLBACK_SSID, AP_FALLBACK_PASSWORD);
    Serial.println("AP IP: " + WiFi.softAPIP().toString());
  }
  if (MDNS.begin(HOSTNAME)) {
    Serial.println("mDNS: http://" + String(HOSTNAME) + ".local/");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  coilsOff();

  pinMode(PIN_LIMIT_SWITCH, INPUT_PULLUP);

  LittleFS.begin();
  loadConfig();
  loadSequenceFromFile();
  loadState();
  if (currentIndex >= (int)sequence.size()) currentIndex = 0;
  targetStep = currentStep; // start stationary at whatever we last saved as "home"

  feederServo.attach(PIN_FEEDER_SERVO);
  feederServo.write(feederRestAngle);

  setupWiFi();

  server.on("/", handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/status", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/upload", HTTP_POST, handleUpload);
  server.on("/upload", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/config", HTTP_OPTIONS, handleCorsPreflight);
  server.on("/action", HTTP_POST, handleAction);
  server.on("/action", HTTP_OPTIONS, handleCorsPreflight);
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
  MDNS.update();

  if (homing) {
    // Slow open-loop seek: step one half-step at a time until the limit
    // switch trips, or we give up after HOME_STEP_CAP half-steps (broken
    // wiring, a switch that never closes, etc.) rather than spin forever.
    if (millis() - lastStepAt >= stepDelayMs) {
      lastStepAt = millis();
      if (digitalRead(PIN_LIMIT_SWITCH) == LOW) {
        currentStep = 0;
        targetStep = 0;
        stepping = false;
        currentIndex = 0;
        saveState();
        homing = false;
        coilsOff();
      } else if (homeStepCount >= HOME_STEP_CAP) {
        homing = false;
        homeError = true;
        currentStep = homeStartStep; // don't leave the position tracker corrupted
        coilsOff();
      } else {
        currentStep += homeDir;
        halfStepIdx = (uint8_t)(((halfStepIdx + (homeDir > 0 ? 1 : -1)) + 8) % 8);
        writeCoils(halfStepIdx);
        homeStepCount++;
      }
    }
  } else {
    // Non-blocking half-step advance toward targetStep
    if (stepping && millis() - lastStepAt >= stepDelayMs) {
      lastStepAt = millis();
      currentStep += stepDir;
      halfStepIdx = (uint8_t)(((halfStepIdx + (stepDir > 0 ? 1 : -1)) + 8) % 8);
      writeCoils(halfStepIdx);
      if (currentStep == targetStep) {
        stepping = false;
        // Snap the tracker back into 0..STEPS_PER_REV-1. Every move is planned
        // from an absolute target, so rounding error cannot accumulate across
        // thousands of chords.
        currentStep = normalizeStep(currentStep);
        targetStep = currentStep;
        coilsOff(); // de-energize coils while idle: less heat, no buzzing, no wasted current
        if (feederPendingAfterMove) {
          feederPendingAfterMove = false;
          startFeederPulse();
        }
      }
    }

    // Optional hands-free auto-advance
    if (autoRunning && !stepping && !sequence.empty() &&
        currentIndex + 1 < (int)sequence.size() &&
        millis() - lastAutoAdvanceAt >= autoAdvanceMs) {
      lastAutoAdvanceAt = millis();
      currentIndex++;
      beginMoveToStep(nailToStep(sequence[currentIndex]));
      feederPendingAfterMove = feederAutoFeed;
      saveState();
      if (currentIndex + 1 >= (int)sequence.size()) autoRunning = false;
    }
  }

  // Feeder servo: non-blocking return-to-rest after a pulse
  if (feederActive && millis() >= feederReturnAt) {
    feederServo.write(feederRestAngle);
    feederActive = false;
  }
}
