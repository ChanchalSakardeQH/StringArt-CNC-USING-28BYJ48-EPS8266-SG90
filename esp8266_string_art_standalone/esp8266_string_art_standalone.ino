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

// ---------------------------------------------------------------------------
// Explicit prototypes.
//
// Arduino normally generates these for you by scanning the .ino. Declaring
// them by hand costs nothing and means the build no longer depends on that
// scan succeeding -- which is the other half of the fix that moved the web
// page into web_page.h.
// ---------------------------------------------------------------------------
bool feederBusy();
void writeCoils(uint8_t idx);
void coilsOff();
long normalizeStep(long s);
void beginMoveToStep(long target);
long nailToStep(uint16_t nail);
void requestFeed(bool settleFirst);
void armFeedAfterMove(bool wantFeed);
void serviceFeeder();
void startHoming(int8_t dir);
void saveState();
void loadState();
void saveConfig();
long readConfigLine(File &f, long def);
void loadConfig();
void loadSequenceFromFile();
void parseAndSaveSequence(const String &body);
void sendCorsHeaders();
void handleCorsPreflight();
void handleRoot();
void handleStatus();
void handleUpload();
void handleConfig();
void handleAction();
void setupWiFi();

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

// ---------------- Motor profile ----------------
//
// Everything downstream -- the nail->step maths, the status JSON, and the
// base-template designer in the web page -- derives from these three numbers.
// Change them here only; nothing else hardcodes a step count.
//
//   28BYJ-48, full-step mode : 32 x 64      = 2048 steps/rev
//   28BYJ-48, half-step mode : 32 x 2 x 64  = 4096 steps/rev
//
constexpr long MOTOR_INTERNAL_STEPS = 32;   // full steps per internal motor rev
constexpr bool MOTOR_HALF_STEP = true;      // this firmware drives half-steps
constexpr long MOTOR_STEP_MULT = MOTOR_HALF_STEP ? 2 : 1;

// Gear reduction, scaled by 100000 so it stays integer maths.
//
//   6400000 = 64.00000:1  -> 2048 full / 4096 half steps per rev
//             This is the figure printed on the motor and the one the base
//             designer is set up around. Nail counts that divide evenly into
//             4096 (64, 128, 256...) then land exactly on a step boundary.
//
//   6368395 = 63.68395:1  -> 2037.89 full / 4075.77 half steps per rev
//             The ratio you get by multiplying the actual gear teeth
//             (32/9 x 22/11 x 26/9 x 31/10). Many 28BYJ-48 units measure
//             this rather than a clean 64:1, in which case commanding a
//             full revolution with 4096 under-rotates by about 1.8 deg.
//
// To find out which one your motor is: home the disc, mark the frame, then
// send it to nail 0 ten times via "goto" with numNails set to 1 (ten full
// revolutions). If the mark comes back true, keep 6400000. If it has crept
// round by roughly 18 deg, switch to 6368395 and re-home.
constexpr long MOTOR_GEAR_RATIO_X100000 = 6400000L;

// Derived: steps per output-shaft revolution, x100 to keep the fractional
// part without floating point. 32 * 2 * 6400000 / 1000 = 409600 (=4096.00).
constexpr long STEPS_PER_REV_X100 =
    MOTOR_INTERNAL_STEPS * MOTOR_STEP_MULT * MOTOR_GEAR_RATIO_X100000 / 1000L;
constexpr long STEPS_PER_REV = (STEPS_PER_REV_X100 + 50L) / 100L;

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

// ---------------- Adjustable defaults ----------------
// These are what the machine boots with before /config.txt is read, and what
// "Restore defaults" in Advanced settings writes back. Change them here if a
// different starting point suits your build better.
constexpr uint16_t DEF_NUM_NAILS       = 200;
constexpr uint16_t DEF_STEP_DELAY_MS   = 2;
constexpr uint32_t DEF_AUTO_MS         = 4000;
constexpr uint8_t  DEF_FEEDER_REST     = 0;
constexpr uint8_t  DEF_FEEDER_FEED     = 90;
constexpr uint16_t DEF_FEEDER_PULSE_MS = 300;
// The disc is still ringing the instant a move ends, and the thread needs a
// moment to stop swinging before the feeder grabs it. Feeding into that
// snatches the thread and gives uneven lengths. 800 ms is a deliberately
// generous starting point -- it is easy to wind down once you can see the
// motion, and much less obvious when it is too short.
constexpr uint16_t DEF_FEEDER_SETTLE_MS  = 800;  // after the nail arrives, before feeding
constexpr uint16_t DEF_FEEDER_RECOVER_MS = 400;  // after the servo rests, before moving on

uint16_t stepDelayMs = DEF_STEP_DELAY_MS; // between half-steps; lower = faster but can stall
uint16_t numNails = DEF_NUM_NAILS;    // must match --nails used in the generator
uint32_t autoAdvanceMs = DEF_AUTO_MS; // used only in "auto" run mode

// Feeder servo settings (persisted -- see save/loadConfig)
uint8_t feederRestAngle = DEF_FEEDER_REST;
uint8_t feederFeedAngle = DEF_FEEDER_FEED;
uint16_t feederPulseMs = DEF_FEEDER_PULSE_MS;
uint16_t feederSettleMs = DEF_FEEDER_SETTLE_MS;
uint16_t feederRecoverMs = DEF_FEEDER_RECOVER_MS;
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
// Feeder servo. A full feed is four phases, all non-blocking:
//
//   SETTLE  nail has arrived; let the disc and thread stop moving
//   PULSE   servo at feed angle, paying out thread
//   RECOVER servo back at rest; let the thread settle before the disc moves
//   IDLE    done -- the indexer is free to advance
//
// Auto-advance waits for IDLE, so a move never starts on top of a feed.
enum FeederPhase { FEED_IDLE, FEED_SETTLE, FEED_PULSE, FEED_RECOVER };
FeederPhase feederPhase = FEED_IDLE;
unsigned long feederNextAt = 0;
bool feederPendingAfterMove = false; // fire once the in-flight stepper move completes

bool feederBusy() { return feederPhase != FEED_IDLE; }

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

// ---- Feeder servo (non-blocking, phased -- see FeederPhase above) ---------
//
// settleFirst is true when the feed follows a disc move, false for a manual
// "Feed now" press (nothing is moving, so there is nothing to settle).
void requestFeed(bool settleFirst) {
  feederPhase = FEED_SETTLE;
  feederNextAt = millis() + (settleFirst ? feederSettleMs : 0);
}

// Queue the feed that follows a disc move. beginMoveToStep() leaves stepping
// false when the move works out to zero steps -- a sequence that repeats a
// nail, or two nails that round to the same step -- and then there is no
// move-completion event to hang the feed off. Fire it here instead, otherwise
// auto mode silently skips the feed and stalls waiting for a dwell that never
// gets restarted.
void armFeedAfterMove(bool wantFeed) {
  feederPendingAfterMove = wantFeed && stepping;
  if (stepping) return;
  if (wantFeed) requestFeed(true);
  else lastAutoAdvanceAt = millis();
}

void serviceFeeder() {
  if (feederPhase == FEED_IDLE) return;
  if ((long)(millis() - feederNextAt) < 0) return;

  if (feederPhase == FEED_SETTLE) {
    feederServo.write(feederFeedAngle);
    feederPhase = FEED_PULSE;
    feederNextAt = millis() + feederPulseMs;
  } else if (feederPhase == FEED_PULSE) {
    feederServo.write(feederRestAngle);
    feederPhase = FEED_RECOVER;
    feederNextAt = millis() + feederRecoverMs;
  } else {
    feederPhase = FEED_IDLE;
    // Auto-advance dwell is measured from here, so the interval means "pause
    // after the thread is fed", not "pause that overlaps the feed".
    lastAutoAdvanceAt = millis();
  }
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
  f.printf("%u\n%u\n%lu\n%u\n%u\n%u\n%u\n%d\n%u\n%u\n",
           numNails, stepDelayMs, (unsigned long)autoAdvanceMs,
           feederRestAngle, feederFeedAngle, feederPulseMs, feederAutoFeed ? 1u : 0u,
           (int)dirSign, feederSettleMs, feederRecoverMs);
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
  feederSettleMs = (uint16_t)readConfigLine(f, feederSettleMs);
  feederRecoverMs = (uint16_t)readConfigLine(f, feederRecoverMs);
  f.close();
  if (numNails == 0) numNails = DEF_NUM_NAILS;
  if (stepDelayMs == 0) stepDelayMs = DEF_STEP_DELAY_MS;
  if (autoAdvanceMs == 0) autoAdvanceMs = DEF_AUTO_MS;
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
#include "web_page.h"   // INDEX_HTML -- see the note at the top of that file


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
  json += "\"feederSettleMs\":" + String(feederSettleMs) + ",";
  json += "\"feederRecoverMs\":" + String(feederRecoverMs) + ",";
  json += "\"feederBusy\":" + String(feederBusy() ? "true" : "false") + ",";
  json += "\"defaults\":{";
  json +=   "\"numNails\":" + String(DEF_NUM_NAILS) + ",";
  json +=   "\"stepDelay\":" + String(DEF_STEP_DELAY_MS) + ",";
  json +=   "\"autoMs\":" + String(DEF_AUTO_MS) + ",";
  json +=   "\"feederRestAngle\":" + String(DEF_FEEDER_REST) + ",";
  json +=   "\"feederFeedAngle\":" + String(DEF_FEEDER_FEED) + ",";
  json +=   "\"feederPulseMs\":" + String(DEF_FEEDER_PULSE_MS) + ",";
  json +=   "\"feederSettleMs\":" + String(DEF_FEEDER_SETTLE_MS) + ",";
  json +=   "\"feederRecoverMs\":" + String(DEF_FEEDER_RECOVER_MS);
  json += "},";
  json += "\"feederAutoFeed\":" + String(feederAutoFeed ? "true" : "false") + ",";
  json += "\"dirSign\":" + String((int)dirSign) + ",";
  json += "\"internalSteps\":" + String(MOTOR_INTERNAL_STEPS) + ",";
  json += "\"halfStep\":" + String(MOTOR_HALF_STEP ? "true" : "false") + ",";
  json += "\"gearRatioX100000\":" + String(MOTOR_GEAR_RATIO_X100000) + ",";
  json += "\"stepsPerRevX100\":" + String(STEPS_PER_REV_X100) + ",";
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
  // "reset=1" restores every adjustable setting to the compiled-in defaults
  // above. Done server-side so the page never has to carry its own copy of
  // the default values and drift out of step with the firmware.
  if (server.hasArg("reset")) {
    numNails = DEF_NUM_NAILS;
    stepDelayMs = DEF_STEP_DELAY_MS;
    autoAdvanceMs = DEF_AUTO_MS;
    feederRestAngle = DEF_FEEDER_REST;
    feederFeedAngle = DEF_FEEDER_FEED;
    feederPulseMs = DEF_FEEDER_PULSE_MS;
    feederSettleMs = DEF_FEEDER_SETTLE_MS;
    feederRecoverMs = DEF_FEEDER_RECOVER_MS;
    saveConfig();
    if (!feederBusy()) feederServo.write(feederRestAngle);
    sendCorsHeaders();
    server.send(200, "text/plain", "ok");
    return;
  }
  if (server.hasArg("numNails")) numNails = server.arg("numNails").toInt();
  if (server.hasArg("stepDelay")) stepDelayMs = server.arg("stepDelay").toInt();
  if (server.hasArg("autoMs")) autoAdvanceMs = server.arg("autoMs").toInt();
  if (server.hasArg("dirSign")) dirSign = (server.arg("dirSign").toInt() < 0) ? -1 : 1;
  if (server.hasArg("feederRestAngle")) feederRestAngle = (uint8_t)server.arg("feederRestAngle").toInt();
  if (server.hasArg("feederFeedAngle")) feederFeedAngle = (uint8_t)server.arg("feederFeedAngle").toInt();
  if (server.hasArg("feederPulseMs")) feederPulseMs = (uint16_t)server.arg("feederPulseMs").toInt();
  if (server.hasArg("feederSettleMs")) feederSettleMs = (uint16_t)server.arg("feederSettleMs").toInt();
  if (server.hasArg("feederRecoverMs")) feederRecoverMs = (uint16_t)server.arg("feederRecoverMs").toInt();
  if (server.hasArg("feederAutoFeed")) feederAutoFeed = server.arg("feederAutoFeed").toInt() != 0;
  saveConfig();
  if (!feederBusy()) feederServo.write(feederRestAngle); // reflect a new rest angle immediately
  sendCorsHeaders();
  server.send(200, "text/plain", "ok");
}

void handleAction() {
  String cmd = server.arg("cmd");
  if (homing && cmd != "findhome") {
    // Ignore everything else while a homing seek is in flight -- the UI
    // disables these buttons too, but guard here in case of a stale page.
  } else if (feederBusy() && (cmd == "next" || cmd == "prev" || cmd == "goto")) {
    // A feed is mid-cycle. Starting a move now would drag thread out of the
    // servo's grip; the caller can retry in a few hundred ms.
  } else if (cmd == "next") {
    if (!sequence.empty() && currentIndex + 1 < (int)sequence.size()) {
      currentIndex++;
      beginMoveToStep(nailToStep(sequence[currentIndex]));
      armFeedAfterMove(feederAutoFeed);
      saveState();
    }
  } else if (cmd == "prev") {
    if (!sequence.empty() && currentIndex > 0) {
      currentIndex--;
      beginMoveToStep(nailToStep(sequence[currentIndex]));
      armFeedAfterMove(false);
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
    requestFeed(false);   // manual press: nothing is moving, no settle needed
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
          requestFeed(true);   // settle before feeding: the nail just arrived
        } else {
          lastAutoAdvanceAt = millis();
        }
      }
    }

    // Optional hands-free auto-advance. Waits for the feeder as well as the
    // stepper, so the disc never starts turning with thread still being fed.
    if (autoRunning && !stepping && !feederBusy() && !sequence.empty() &&
        currentIndex + 1 < (int)sequence.size() &&
        millis() - lastAutoAdvanceAt >= autoAdvanceMs) {
      currentIndex++;
      beginMoveToStep(nailToStep(sequence[currentIndex]));
      armFeedAfterMove(feederAutoFeed);
      saveState();
      if (currentIndex + 1 >= (int)sequence.size()) autoRunning = false;
    }
  }

  // Feeder servo: advances the settle -> pulse -> recover cycle
  serviceFeeder();
}
