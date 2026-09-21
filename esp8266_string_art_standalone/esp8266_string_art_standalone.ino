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
bool wrapBusy();
long stepsPerRev();
long homeStepCap();
bool servoBusy();
void servoGoTo(uint8_t a);
void servoSnapTo(uint8_t a);
void serviceServo();
long autoLeadMag();
long autoSweepMag();
void applyAutoWrapGeometry();
long wrapLeadMag();
long wrapSweepMag();
long wrapSignedLead();
long wrapSignedSweep();
long wrapSweepSteps();
void saveState();
void noteNailDone();
long wrapLeadSteps();
void startWrap(uint16_t nail);
void serviceWrap();
void abortWrap();
void presentNail(uint16_t nail, bool doFeed);
uint16_t previewNail();
long discOffsetFromNail();
void writeCoils(uint8_t idx);
void coilsOff();
long normalizeStep(long s);
void beginMoveToStep(long target);
long nailBaseStep(uint16_t nail);
long nailToStep(uint16_t nail);
void requestFeed(bool settleFirst);
void armFeedAfterMove(bool wantFeed);
void serviceFeeder();
void startHoming(int8_t dir);
void saveState();
void noteNailDone();
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
constexpr long STEPS_PER_REV_X100_DEF =
    MOTOR_INTERNAL_STEPS * MOTOR_STEP_MULT * MOTOR_GEAR_RATIO_X100000 / 1000L;

// Runtime, not constexpr, because the calibration routine measures the real
// figure and writes it back. The compiled value is only the starting point:
// whether your gearbox is 64:1 or the tooth-count 63.68395:1 is something the
// machine can work out for itself over ten revolutions.
long stepsPerRevX100 = STEPS_PER_REV_X100_DEF;
long stepsPerRev() { return (stepsPerRevX100 + 50L) / 100L; }

// Fixed offset between nail numbering and disc position, in half-steps.
// Re-synced from the UI when the nail at the feeder is not the one the
// firmware thinks it is -- which is what missed steps eventually cause.
long nailOffsetSteps = 0;

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

// ---------------- Servo slew ----------------
//
// Servo.write() commands a position and the SG90 slams to it as fast as its
// gearing allows. There is no speed parameter. To move it slowly the angle has
// to be walked in small increments, which is what this does -- non-blocking,
// so the disc and the web server keep running while the arm creeps.
//
// Travel time = (angle to cover / servoSlewDeg) * servoSlewMs.
// At the defaults, a 90 degree swing takes 45 * 20 = 900 ms.
uint8_t servoCurrent = 0;
uint8_t servoTarget = 0;
unsigned long servoNextAt = 0;
uint8_t servoSlewDeg = 2;    // degrees per increment; larger = faster, coarser
uint16_t servoSlewMs = 20;   // ms between increments; larger = slower

bool servoBusy() { return servoCurrent != servoTarget; }

void servoGoTo(uint8_t a) { servoTarget = a; servoNextAt = millis(); }

// Jump without slewing -- used at boot and when the rest angle is edited.
void servoSnapTo(uint8_t a) {
  servoTarget = a;
  servoCurrent = a;
  feederServo.write(a);
}

void serviceServo() {
  if (servoCurrent == servoTarget) return;
  if ((long)(millis() - servoNextAt) < 0) return;
  int diff = (int)servoTarget - (int)servoCurrent;
  int stepDeg = servoSlewDeg < 1 ? 1 : servoSlewDeg;
  if (diff >= -stepDeg && diff <= stepDeg) servoCurrent = servoTarget;
  else servoCurrent = (uint8_t)((int)servoCurrent + (diff > 0 ? stepDeg : -stepDeg));
  feederServo.write(servoCurrent);
  servoNextAt = millis() + servoSlewMs;
}

// ---------------- Wrap cycle ----------------
//
// The tube tip cannot wrap a nail by moving out and back along one line -- that
// encloses nothing. It has to trace a closed loop around the nail, and with
// only one servo axis the disc has to supply the other half of that loop:
//
//   1 APPROACH  disc moves so the tube sits half a nail BEFORE the target
//   2 SETTLE    let the disc stop ringing
//   3 OUT       servo swings the tube outside the nail ring (clear of nails,
//               because it is midway between two of them)
//   4 SWEEP     disc rotates one whole nail pitch while the tube stays out,
//               carrying the thread around the far side of the target nail
//   5 IN        servo brings the tube back inside, again midway between nails
//   6 LAND      disc backs up half a pitch to sit exactly on the target
//
// Steps 3 to 5 are the loop: out on one side, across the back, in on the other.
// That is one wrap. Step 6 does not undo it -- the tube stays inside the ring,
// so it never re-crosses the thread.
//
// This is why the disc and the feeder MUST be allowed to move in the same
// cycle. Earlier versions deliberately kept them apart, which is correct for a
// feeder that only pays out thread and fatal for one that has to wrap.
enum WrapPhase { WRAP_IDLE, WRAP_APPROACH, WRAP_SETTLE, WRAP_SERVO_IN,
                 WRAP_HOLD_IN, WRAP_SWEEP, WRAP_HOLD_SWEEP, WRAP_SERVO_OUT,
                 WRAP_RECOVER, WRAP_LAND };
WrapPhase wrapPhase = WRAP_IDLE;
unsigned long wrapNextAt = 0;
uint16_t wrapTargetNail = 0;
// Which way the disc travelled to reach this nail. The wrap loop is handed off
// this, not off a fixed constant: the tube has to end up on the far side of the
// nail from the incoming thread, and which side that is flips with the
// direction of travel. Signing the loop with a constant makes half the chords
// wrap the wrong way round -- they look like the disc simply carrying on
// through the nail, with no hooking move at all.
int8_t wrapApproachDir = 1;

bool wrapMode = true;      // false = old behaviour, a simple pay-out pulse
// Real step counts, not "0 means work it out". They are filled in from the nail
// count at first boot and whenever the nail count changes, so the UI always has
// concrete numbers to show and to tune from.
uint16_t wrapSteps = 0;    // how far past the nail the disc runs before the servo moves
uint16_t wrapSweep = 0;    // how far the disc carries the thread while the tube is out
int8_t wrapDir = 1;        // which side to approach from; flip if wraps shed

// The two waits that give the thread time to seat. These are the ones that
// matter for whether a wrap holds: the thread needs a moment to fall into
// place after the tube swings, and again after the disc has carried it round.
uint16_t wrapHoldInMs = 400;     // after the tube reaches the feed position
uint16_t wrapHoldSweepMs = 400;  // after the sweep, before the tube comes back

bool wrapBusy() { return wrapPhase != WRAP_IDLE; }

// False when the saved position could not be trusted at boot (power was cut
// mid-move). Cleared by any homing operation.
bool positionKnown = true;
// Set at boot when autoHomeOnBoot is on: home first, then drive back to the
// nail the sequence was left on.
bool resumeAfterHome = false;
// Home automatically at power-up and return to the saved nail. Needs the limit
// switch; without one there is nothing to home against. Persisted.
bool autoHomeOnBoot = false;
// Re-home against the limit switch every N nails, to clear accumulated missed
// steps on a long run. 0 = off. Needs the switch.
uint16_t rehomeEvery = 0;
uint16_t sinceRehome = 0;

// ---------------- Run timing ----------------
// Measured, not predicted. The theoretical cycle time ignores disc travel,
// which varies with how far apart consecutive nails are, and ignores however
// long you actually take between presses in manual mode. An average of real
// nail-to-nail times is the only estimate worth showing.
unsigned long lastNailAt = 0;      // when the previous nail was started
uint32_t avgNailMs = 0;            // exponential moving average of the cycle
uint32_t runElapsedMs = 0;         // accumulated time with auto running
unsigned long elapsedTickAt = 0;

// Folds one completed cycle into the average. Wildly short or long samples are
// dropped: a jog or a pause would otherwise poison the estimate.
void noteNailDone() {
  unsigned long now = millis();
  if (lastNailAt != 0) {
    unsigned long dt = now - lastNailAt;
    // 2 minutes is far longer than any real cycle, so anything above it is a
    // pause rather than a nail and would drag the estimate out badly.
    if (dt > 200 && dt < 120000UL) {
      avgNailMs = (avgNailMs == 0) ? (uint32_t)dt
                                   : (uint32_t)((avgNailMs * 3UL + dt) / 4UL);
    }
  }
  lastNailAt = now;
}

// Steps-per-revolution calibration, driven from the UI in two steps.
bool calRunning = false;
int  calRevs = 0;
long calStartStep = 0;
long calRemaining = 0;

bool feederBusy() { return feederPhase != FEED_IDLE; }

// Homing (non-blocking seek toward the limit switch)
bool homing = false;
bool homeError = false;      // set if the switch never triggered within the safety cap
int8_t homeDir = 1;
long homeStepCount = 0;
long homeStartStep = 0;      // restored on abort so the position tracker isn't corrupted
long homeStepCap() { return stepsPerRev() + stepsPerRev() / 4; } // 1.25 rev safety limit

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
  s %= stepsPerRev();
  if (s < 0) s += stepsPerRev();
  return s;
}

// Begin a (non-blocking) move to an absolute step position, shortest direction.
void beginMoveToStep(long target) {
  target = normalizeStep(target);
  long delta = target - currentStep;
  // wrap into (-stepsPerRev()/2, stepsPerRev()/2]
  while (delta > stepsPerRev() / 2) delta -= stepsPerRev();
  while (delta <= -stepsPerRev() / 2) delta += stepsPerRev();
  targetStep = currentStep + delta;
  stepDir = (delta >= 0) ? 1 : -1;
  stepping = (delta != 0);
}

// Absolute disc position (in half-steps) that puts `nail` at the feeder.
// Rounded to the nearest half-step, signed by dirSign, wrapped to one rev.
// Where nail N sits before the calibration offset is applied.
long nailBaseStep(uint16_t nail) {
  if (numNails == 0) return 0;
  long n = ((long)nail % (long)numNails + (long)numNails) % (long)numNails;
  long s = (n * stepsPerRevX100 + (long)numNails * 50L) / ((long)numNails * 100L);
  return normalizeStep((long)dirSign * s);
}

long nailToStep(uint16_t nail) {
  return normalizeStep(nailBaseStep(nail) + nailOffsetSteps);
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

// Half a nail pitch by default: that puts the ring crossing exactly midway
// between two nails, so the tube passes through a gap rather than into a nail,
// and the swept loop encloses the target nail and nothing else.
// How far past the nail the disc runs before the servo moves, as a magnitude.
// Half a pitch by default: the crossing then falls midway between two nails, so
// the tube goes through a gap instead of into a nail.
long autoLeadMag() {
  if (numNails == 0) return 1;
  long half = (stepsPerRevX100 / (long)numNails / 2 + 50L) / 100L;
  return half < 1 ? 1 : half;
}

// Twice the lead, deliberately, rather than the pitch rounded on its own.
// Rounding the two independently leaves the loop sitting off centre -- at 360
// nails it crossed at +0.53 and -0.44 of a pitch instead of mirroring.
long autoSweepMag() { return autoLeadMag() * 2; }

void applyAutoWrapGeometry() {
  wrapSteps = (uint16_t)autoLeadMag();
  wrapSweep = (uint16_t)autoSweepMag();
}

long wrapLeadMag() {
  if (wrapSteps > 0) return (long)wrapSteps;
  return autoLeadMag();
}

// How far the disc carries the thread round while the tube is out, as a
// magnitude. One whole nail pitch by default, which takes the thread from one
// side of the target nail to the other.
long wrapSweepMag() {
  if (wrapSweep > 0) return (long)wrapSweep;
  return autoSweepMag();
}

// The loop is handed off the direction of travel, with wrapDir as a global flip
// if the thread runs the other way round your tube.
long wrapSignedLead()  { return wrapLeadMag()  * wrapApproachDir * wrapDir; }
long wrapSignedSweep() { return wrapSweepMag() * wrapApproachDir * wrapDir; }

// Backwards-compatible name, used by the status JSON.
long wrapLeadSteps() { return wrapSignedLead(); }
long wrapSweepSteps() { return wrapSignedSweep(); }

void startWrap(uint16_t nail) {
  wrapTargetNail = nail;

  // Work out which way the disc is about to travel, before moving, and hand the
  // loop that way. Overshoot past the nail in the direction of travel, sweep
  // back across it, land on it -- so the tube always passes on the far side
  // from the thread trailing behind, whichever way the chord runs.
  long delta = nailToStep(nail) - currentStep;
  while (delta >  stepsPerRev() / 2) delta -= stepsPerRev();
  while (delta < -stepsPerRev() / 2) delta += stepsPerRev();
  if (delta != 0) wrapApproachDir = (delta > 0) ? 1 : -1;

  wrapPhase = WRAP_APPROACH;
  wrapNextAt = millis();
  // Straight to the approach position from wherever the disc is -- no need to
  // stop on the nail first, so a wrap costs one normal move plus two short ones.
  beginMoveToStep(normalizeStep(nailToStep(nail) + wrapLeadSteps()));
}

void abortWrap() {
  if (wrapPhase == WRAP_IDLE) return;
  wrapPhase = WRAP_IDLE;
  servoGoTo(feederRestAngle);   // never leave the tube parked out over the nails
}

// One phase per call. Each either starts a disc move, starts a servo slew, or
// sets a timer; the guards below then hold everything until that finishes, so
// nothing overlaps except where the cycle deliberately wants it to.
void serviceWrap() {
  if (wrapPhase == WRAP_IDLE) return;
  if (stepping) return;     // disc still travelling
  if (servoBusy()) return;  // arm still slewing
  if ((long)(millis() - wrapNextAt) < 0) return;

  long nailStep = nailToStep(wrapTargetNail);

  switch (wrapPhase) {
    case WRAP_APPROACH:                          // disc has arrived
      wrapPhase = WRAP_SETTLE;
      wrapNextAt = millis() + feederSettleMs;    // let it stop ringing
      break;

    case WRAP_SETTLE:
      servoGoTo(feederFeedAngle);                // tube swings to the feed side
      wrapPhase = WRAP_SERVO_IN;
      break;

    case WRAP_SERVO_IN:                          // slew finished
      wrapPhase = WRAP_HOLD_IN;
      wrapNextAt = millis() + wrapHoldInMs;      // let the thread settle in
      break;

    case WRAP_HOLD_IN:
      // The wrap itself: the disc carries the thread past the nail while the
      // tube stays put.
      beginMoveToStep(normalizeStep(nailStep + wrapSignedLead() - wrapSignedSweep()));
      wrapPhase = WRAP_SWEEP;
      break;

    case WRAP_SWEEP:                             // sweep finished
      wrapPhase = WRAP_HOLD_SWEEP;
      wrapNextAt = millis() + wrapHoldSweepMs;   // let the thread hook properly
      break;

    case WRAP_HOLD_SWEEP:
      servoGoTo(feederRestAngle);                // tube comes back
      wrapPhase = WRAP_SERVO_OUT;
      break;

    case WRAP_SERVO_OUT:                         // slew finished
      wrapPhase = WRAP_RECOVER;
      wrapNextAt = millis() + feederRecoverMs;
      break;

    case WRAP_RECOVER:
      beginMoveToStep(nailStep);                 // sit exactly on the nail
      wrapPhase = WRAP_LAND;
      break;

    case WRAP_LAND:
      wrapPhase = WRAP_IDLE;
      lastAutoAdvanceAt = millis();
      saveState();
      break;

    default:
      wrapPhase = WRAP_IDLE;
  }
}

void serviceFeeder() {
  if (feederPhase == FEED_IDLE) return;
  if (servoBusy()) return;
  if ((long)(millis() - feederNextAt) < 0) return;

  if (feederPhase == FEED_SETTLE) {
    servoGoTo(feederFeedAngle);
    feederPhase = FEED_PULSE;
    feederNextAt = millis() + feederPulseMs;
  } else if (feederPhase == FEED_PULSE) {
    servoGoTo(feederRestAngle);
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
// Single entry point for advancing to a nail. In wrap mode the whole cycle is
// handed to the sequencer; otherwise it falls back to the old move-then-pulse.
void presentNail(uint16_t nail, bool doFeed) {
  if (doFeed && wrapMode) {
    startWrap(nail);
  } else {
    beginMoveToStep(nailToStep(nail));
    armFeedAfterMove(doFeed);
  }
}

// The nail the wrap preview works around: the one the sequence is on, or nail
// 0 if nothing is loaded yet.
uint16_t previewNail() {
  if (!sequence.empty() && currentIndex >= 0 && currentIndex < (int)sequence.size())
    return sequence[currentIndex];
  return 0;
}

// Where the disc sits relative to the preview nail, in steps, shortest way.
long discOffsetFromNail() {
  long d = currentStep - nailToStep(previewNail());
  while (d >  stepsPerRev() / 2) d -= stepsPerRev();
  while (d < -stepsPerRev() / 2) d += stepsPerRev();
  return d;
}

void startHoming(int8_t dir) {
  homing = true;
  homeError = false;
  homeDir = dir;
  homeStepCount = 0;
  homeStartStep = currentStep;
  stepping = false; // cancel any queued sequence move -- homing takes priority
  lastStepAt = millis();
}

// State is written twice per nail: once when a move starts (recording where it
// is headed and that it is in flight) and once when it lands (recording the
// position it actually reached).
//
// Writing only at the start -- which is what earlier versions did -- saved the
// position from BEFORE the move, so a power cut resumed one nail behind where
// the disc physically was. Writing only at the end would leave no trace of an
// interrupted move, and the disc would come back parked somewhere between two
// nails with the firmware convinced it was on one of them.
void saveState() {
  File f = LittleFS.open(STATE_FILE, "w");
  if (!f) return;
  f.printf("%d\n%ld\n%ld\n%d\n%lu\n%lu\n", currentIndex, currentStep, targetStep,
           stepping ? 1 : 0, (unsigned long)runElapsedMs, (unsigned long)avgNailMs);
  f.close();
}

void loadState() {
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  currentIndex = f.readStringUntil('\n').toInt();
  // normalize: a state file from older firmware used 4096 steps/rev and could
  // hold an unbounded step count
  long savedStep = normalizeStep(f.readStringUntil('\n').toInt());
  long savedTarget = normalizeStep(readConfigLine(f, savedStep));
  bool wasMoving = readConfigLine(f, 0) != 0;
  runElapsedMs = (uint32_t)readConfigLine(f, 0);
  avgNailMs = (uint32_t)readConfigLine(f, 0);
  f.close();

  if (wasMoving) {
    // Power went during a move. The disc stopped somewhere between the two
    // positions and there is no way to tell where without a reference. Assume
    // it got there -- moves are short relative to the whole cycle -- but mark
    // the position unverified so the UI asks for a home before trusting it.
    currentStep = savedTarget;
    positionKnown = false;
  } else {
    currentStep = savedStep;
    positionKnown = true;
  }
}

void saveConfig() {
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) return;
  f.printf("%u\n%u\n%lu\n%u\n%u\n%u\n%u\n%d\n%u\n%u\n%u\n%u\n%u\n%d\n%u\n%u\n%u\n%u\n%u\n%ld\n%ld\n%u\n",
           numNails, stepDelayMs, (unsigned long)autoAdvanceMs,
           feederRestAngle, feederFeedAngle, feederPulseMs, feederAutoFeed ? 1u : 0u,
           (int)dirSign, feederSettleMs, feederRecoverMs, autoHomeOnBoot ? 1u : 0u,
           wrapMode ? 1u : 0u, wrapSteps, (int)wrapDir,
           wrapSweep, wrapHoldInMs, wrapHoldSweepMs, servoSlewDeg, servoSlewMs,
           stepsPerRevX100, nailOffsetSteps, rehomeEvery);
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
  autoHomeOnBoot = readConfigLine(f, autoHomeOnBoot ? 1 : 0) != 0;
  wrapMode = readConfigLine(f, wrapMode ? 1 : 0) != 0;
  wrapSteps = (uint16_t)readConfigLine(f, wrapSteps);
  wrapDir = (readConfigLine(f, wrapDir) < 0) ? -1 : 1;
  wrapSweep = (uint16_t)readConfigLine(f, wrapSweep);
  wrapHoldInMs = (uint16_t)readConfigLine(f, wrapHoldInMs);
  wrapHoldSweepMs = (uint16_t)readConfigLine(f, wrapHoldSweepMs);
  servoSlewDeg = (uint8_t)readConfigLine(f, servoSlewDeg);
  servoSlewMs = (uint16_t)readConfigLine(f, servoSlewMs);
  if (servoSlewDeg < 1) servoSlewDeg = 1;
  stepsPerRevX100 = readConfigLine(f, stepsPerRevX100);
  nailOffsetSteps = readConfigLine(f, nailOffsetSteps);
  rehomeEvery = (uint16_t)readConfigLine(f, rehomeEvery);
  if (stepsPerRevX100 < 1000) stepsPerRevX100 = STEPS_PER_REV_X100_DEF;
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
  json += "\"stepsPerRevX100\":" + String(stepsPerRevX100) + ",";
  json += "\"stepsPerRevDefX100\":" + String(STEPS_PER_REV_X100_DEF) + ",";
  json += "\"nailOffsetSteps\":" + String(nailOffsetSteps) + ",";
  json += "\"rehomeEvery\":" + String(rehomeEvery) + ",";
  json += "\"avgNailMs\":" + String(avgNailMs) + ",";
  json += "\"elapsedMs\":" + String(runElapsedMs) + ",";
  json += "\"calRunning\":" + String(calRunning ? "true" : "false") + ",";
  json += "\"hasLimitSwitch\":" + String(PIN_LIMIT_SWITCH >= 0 ? "true" : "false") + ",";
  json += "\"switchTriggered\":" + String(digitalRead(PIN_LIMIT_SWITCH) == LOW ? "true" : "false") + ",";
  json += "\"homing\":" + String(homing ? "true" : "false") + ",";
  json += "\"wrapMode\":" + String(wrapMode ? "true" : "false") + ",";
  json += "\"wrapSteps\":" + String(wrapSteps) + ",";
  json += "\"wrapAutoSteps\":" + String(wrapLeadMag()) + ",";
  json += "\"wrapDir\":" + String((int)wrapDir) + ",";
  json += "\"wrapSweep\":" + String(wrapSweep) + ",";
  json += "\"wrapAutoSweep\":" + String(wrapSweepMag()) + ",";
  json += "\"wrapApproachDir\":" + String((int)wrapApproachDir) + ",";
  json += "\"autoLead\":" + String(autoLeadMag()) + ",";
  json += "\"autoSweep\":" + String(autoSweepMag()) + ",";
  json += "\"servoAngle\":" + String(servoCurrent) + ",";
  json += "\"previewNail\":" + String(previewNail()) + ",";
  json += "\"discOffset\":" + String(discOffsetFromNail()) + ",";
  json += "\"wrapHoldInMs\":" + String(wrapHoldInMs) + ",";
  json += "\"wrapHoldSweepMs\":" + String(wrapHoldSweepMs) + ",";
  json += "\"servoSlewDeg\":" + String(servoSlewDeg) + ",";
  json += "\"servoSlewMs\":" + String(servoSlewMs) + ",";
  json += "\"wrapBusy\":" + String(wrapBusy() ? "true" : "false") + ",";
  json += "\"positionKnown\":" + String(positionKnown ? "true" : "false") + ",";
  json += "\"autoHomeOnBoot\":" + String(autoHomeOnBoot ? "true" : "false") + ",";
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
    wrapMode = true;
    wrapDir = 1;
    wrapHoldInMs = 400;
    wrapHoldSweepMs = 400;
    servoSlewDeg = 2;
    servoSlewMs = 20;
    stepsPerRevX100 = STEPS_PER_REV_X100_DEF;
    nailOffsetSteps = 0;
    rehomeEvery = 0;
    applyAutoWrapGeometry();
    saveConfig();
    if (!feederBusy() && !wrapBusy()) servoSnapTo(feederRestAngle);
    sendCorsHeaders();
    server.send(200, "text/plain", "ok");
    return;
  }
  if (server.hasArg("numNails")) {
    uint16_t was = numNails;
    numNails = server.arg("numNails").toInt();
    // Step counts are meaningless against a different nail pitch, so rebuild
    // them. Changing the nail count invalidates the sequence anyway.
    if (numNails != was) applyAutoWrapGeometry();
  }
  if (server.hasArg("recalcWrap")) applyAutoWrapGeometry();
  if (server.hasArg("stepDelay")) stepDelayMs = server.arg("stepDelay").toInt();
  if (server.hasArg("autoMs")) autoAdvanceMs = server.arg("autoMs").toInt();
  if (server.hasArg("dirSign")) dirSign = (server.arg("dirSign").toInt() < 0) ? -1 : 1;
  if (server.hasArg("feederRestAngle")) feederRestAngle = (uint8_t)server.arg("feederRestAngle").toInt();
  if (server.hasArg("feederFeedAngle")) feederFeedAngle = (uint8_t)server.arg("feederFeedAngle").toInt();
  if (server.hasArg("feederPulseMs")) feederPulseMs = (uint16_t)server.arg("feederPulseMs").toInt();
  if (server.hasArg("feederSettleMs")) feederSettleMs = (uint16_t)server.arg("feederSettleMs").toInt();
  if (server.hasArg("feederRecoverMs")) feederRecoverMs = (uint16_t)server.arg("feederRecoverMs").toInt();
  if (server.hasArg("autoHomeOnBoot")) autoHomeOnBoot = server.arg("autoHomeOnBoot").toInt() != 0;
  if (server.hasArg("wrapMode")) wrapMode = server.arg("wrapMode").toInt() != 0;
  if (server.hasArg("wrapSteps")) wrapSteps = (uint16_t)server.arg("wrapSteps").toInt();
  if (server.hasArg("wrapDir")) wrapDir = (server.arg("wrapDir").toInt() < 0) ? -1 : 1;
  if (server.hasArg("wrapSweep")) wrapSweep = (uint16_t)server.arg("wrapSweep").toInt();
  if (server.hasArg("wrapHoldInMs")) wrapHoldInMs = (uint16_t)server.arg("wrapHoldInMs").toInt();
  if (server.hasArg("wrapHoldSweepMs")) wrapHoldSweepMs = (uint16_t)server.arg("wrapHoldSweepMs").toInt();
  if (server.hasArg("servoSlewDeg")) { servoSlewDeg = (uint8_t)server.arg("servoSlewDeg").toInt(); if (servoSlewDeg < 1) servoSlewDeg = 1; }
  if (server.hasArg("servoSlewMs")) servoSlewMs = (uint16_t)server.arg("servoSlewMs").toInt();
  if (server.hasArg("rehomeEvery")) rehomeEvery = (uint16_t)server.arg("rehomeEvery").toInt();
  if (server.hasArg("feederAutoFeed")) feederAutoFeed = server.arg("feederAutoFeed").toInt() != 0;
  saveConfig();
  if (!feederBusy() && !wrapBusy()) servoSnapTo(feederRestAngle); // show a new rest angle at once
  sendCorsHeaders();
  server.send(200, "text/plain", "ok");
}

void handleAction() {
  String cmd = server.arg("cmd");
  if (homing && cmd != "findhome") {
    // Ignore everything else while a homing seek is in flight -- the UI
    // disables these buttons too, but guard here in case of a stale page.
  } else if ((feederBusy() || wrapBusy()) &&
             (cmd == "next" || cmd == "prev" || cmd == "goto" || cmd == "gotostep")) {
    // A feed is mid-cycle. Starting a move now would drag thread out of the
    // servo's grip; the caller can retry in a few hundred ms.
  } else if (cmd == "next") {
    if (!sequence.empty() && currentIndex + 1 < (int)sequence.size()) {
      noteNailDone();
      currentIndex++;
      presentNail(sequence[currentIndex], feederAutoFeed);
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
    abortWrap();
    // Declare the disc's CURRENT physical position to be nail 0, without moving it.
    currentStep = 0;
    targetStep = 0;
    stepping = false;
    currentIndex = 0;
    runElapsedMs = 0;
    lastNailAt = 0;
    positionKnown = true;   // the operator has told us where zero is
    saveState();
  } else if (cmd == "findhome") {
    abortWrap();
    // Physically verified homing: slowly seek until the limit switch trips.
    int8_t dir = (server.hasArg("value") && server.arg("value").toInt() < 0) ? -1 : 1;
    startHoming(dir);
  } else if (cmd == "gotostep") {
    lastNailAt = 0;
    // Move to a position in the SEQUENCE and take progress with it, so the
    // wrap carries on from there. "goto" moves the disc without touching
    // progress.
    if (!sequence.empty()) {
      int v = server.arg("value").toInt();
      if (v < 0) v = 0;
      if (v >= (int)sequence.size()) v = (int)sequence.size() - 1;
      currentIndex = v;
      beginMoveToStep(nailToStep(sequence[currentIndex]));
      armFeedAfterMove(false);
      saveState();
    }
  } else if (cmd == "jog") {
    // Nudge the disc without touching the nail numbering or progress. Used to
    // line a nail up with the feeder before re-syncing.
    long d = server.arg("value").toInt();
    beginMoveToStep(normalizeStep(currentStep + d));
    saveState();
  } else if (cmd == "setnail") {
    // "The nail at the feeder is actually N." Solves for the offset that makes
    // that true, so every future move lands right. Progress is untouched --
    // this corrects where the disc is, not where you are in the sequence.
    long n = server.arg("value").toInt();
    nailOffsetSteps = normalizeStep(currentStep - nailBaseStep((uint16_t)n));
    saveConfig();
    saveState();
  } else if (cmd == "calmove") {
    // Step one of measuring the real steps-per-revolution: drive a whole
    // number of turns and stop. Whatever nail comes back to the feeder tells
    // us how wrong our figure is.
    calRevs = server.arg("value").toInt();
    if (calRevs < 1) calRevs = 1;
    if (calRevs > 50) calRevs = 50;
    calStartStep = currentStep;
    calRunning = true;
    beginMoveToStep(currentStep);          // land exactly where we are
    calRemaining = calRevs * stepsPerRev();
  } else if (cmd == "calreport") {
    // Step two: you tell it how many nails past (or short of) the start the
    // disc actually finished, and it corrects steps-per-revolution.
    long errNails = server.arg("value").toInt();
    if (calRevs > 0 && numNails > 0 && errNails != 0) {
      // commanded = calRevs turns; actual = calRevs + errNails/numNails turns
      long num = (long)calRevs * (long)numNails * 100L;
      long den = (long)calRevs * (long)numNails + errNails;
      if (den > 0) {
        long fresh = (stepsPerRevX100 * num / 100L) / den;
        if (fresh > stepsPerRevX100 / 2 && fresh < stepsPerRevX100 * 2) {
          stepsPerRevX100 = fresh;
        }
      }
    }
    calRunning = false;
    calRevs = 0;
    saveConfig();
  } else if (cmd == "wrappreview") {
    // Park the disc a signed number of steps from the nail at the feeder, so
    // the ahead and behind positions of the wrap can be set by eye. Paired with
    // "servotest" this walks through a wrap by hand, one move at a time.
    if (!wrapBusy() && !feederBusy() && !autoRunning && !calRunning) {
      long off = server.arg("value").toInt();
      long cap = stepsPerRev() / 4;
      if (off > cap) off = cap;
      if (off < -cap) off = -cap;
      beginMoveToStep(normalizeStep(nailToStep(previewNail()) + off));
    }
  } else if (cmd == "servotest") {
    // Drives the arm straight to an angle so it can be dialled in by eye.
    // Refused while anything else is using the servo.
    if (!wrapBusy() && !feederBusy() && !autoRunning) {
      int a = server.arg("value").toInt();
      if (a < 0) a = 0;
      if (a > 180) a = 180;
      servoGoTo((uint8_t)a);
    }
  } else if (cmd == "wraptest") {
    // One wrap cycle on the nail currently at the feeder, for tuning the
    // overshoot and servo angles without committing to a run.
    if (!sequence.empty() && currentIndex < (int)sequence.size()) {
      startWrap(sequence[currentIndex]);
    }
  } else if (cmd == "feed") {
    requestFeed(false);   // manual press: nothing is moving, no settle needed
  } else if (cmd == "goto") {
    long nail = server.arg("value").toInt();
    beginMoveToStep(nailToStep((uint16_t)nail));
  } else if (cmd == "start") {
    autoRunning = true;
    lastAutoAdvanceAt = millis();
  } else if (cmd == "stop") {
    abortWrap();
    lastNailAt = 0;   // don't fold the pause into the per-nail average
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
  if (wrapSteps == 0 || wrapSweep == 0) applyAutoWrapGeometry();
  loadSequenceFromFile();
  loadState();
  if (currentIndex >= (int)sequence.size()) currentIndex = 0;
  targetStep = currentStep; // start stationary at whatever we last saved as "home"

  Serial.printf("Resumed at step %d of %d, disc at %ld%s\n",
                currentIndex, (int)sequence.size(), currentStep,
                positionKnown ? "" : "  (UNVERIFIED -- power was cut mid-move)");

  // Auto-recovery after a power cut. Only worth doing with a limit switch: it
  // re-establishes an absolute zero, then drives back to the saved nail. The
  // disc is deliberately NOT set running again -- thread may be loose, and
  // starting an unattended machine on power-up is a bad default.
  if (autoHomeOnBoot && !sequence.empty()) {
    resumeAfterHome = true;
    startHoming(1);
  }

  feederServo.attach(PIN_FEEDER_SERVO);
  servoSnapTo(feederRestAngle);

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
    // switch trips, or we give up after homeStepCap() half-steps (broken
    // wiring, a switch that never closes, etc.) rather than spin forever.
    if (millis() - lastStepAt >= stepDelayMs) {
      lastStepAt = millis();
      if (digitalRead(PIN_LIMIT_SWITCH) == LOW) {
        currentStep = 0;
        targetStep = 0;
        stepping = false;
        positionKnown = true;   // the switch is an absolute reference
        homing = false;
        if (resumeAfterHome) {
          // Boot-time recovery: the switch has given us a known zero, so drive
          // back to the nail the sequence was interrupted on. Progress is kept.
          resumeAfterHome = false;
          if (!sequence.empty() && currentIndex < (int)sequence.size()) {
            beginMoveToStep(nailToStep(sequence[currentIndex]));
            armFeedAfterMove(false);
          }
        } else {
          currentIndex = 0;   // a hand-requested home also restarts the piece
        }
        saveState();
        if (!stepping) coilsOff();
      } else if (homeStepCount >= homeStepCap()) {
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
        // Snap the tracker back into 0..stepsPerRev()-1. Every move is planned
        // from an absolute target, so rounding error cannot accumulate across
        // thousands of chords.
        currentStep = normalizeStep(currentStep);
        targetStep = currentStep;
        coilsOff(); // de-energize coils while idle: less heat, no buzzing, no wasted current
        saveState(); // second write: the disc is here, and no move is pending
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
    if (autoRunning && !stepping && !feederBusy() && !wrapBusy() && !calRunning &&
        !sequence.empty() &&
        currentIndex + 1 < (int)sequence.size() &&
        millis() - lastAutoAdvanceAt >= autoAdvanceMs) {
      noteNailDone();
      currentIndex++;
      // Periodic re-home wipes accumulated missed steps. Progress is kept, so
      // it picks straight back up on the same chord.
      if (rehomeEvery > 0 && PIN_LIMIT_SWITCH >= 0 && ++sinceRehome >= rehomeEvery) {
        sinceRehome = 0;
        resumeAfterHome = true;
        startHoming(1);
      } else {
        presentNail(sequence[currentIndex], feederAutoFeed);
      }
      saveState();
      if (currentIndex + 1 >= (int)sequence.size()) autoRunning = false;
    }
  }

  // Feeder servo: advances the settle -> pulse -> recover cycle
  serviceFeeder();
  // Run clock: only ticks while auto is running, so pauses do not inflate it.
  {
    unsigned long now = millis();
    if (autoRunning && elapsedTickAt != 0) runElapsedMs += (uint32_t)(now - elapsedTickAt);
    elapsedTickAt = now;
  }

  // Calibration turns: a long run of whole revolutions, a revolution at a time
  // so each move stays inside one wrap of the step counter.
  if (calRunning && !stepping && calRemaining > 0) {
    long chunk = calRemaining > stepsPerRev() / 2 ? stepsPerRev() / 2 : calRemaining;
    calRemaining -= chunk;
    beginMoveToStep(normalizeStep(currentStep + chunk * dirSign));
  }

  // Wrap sequencer: interleaves disc moves and servo swings
  serviceWrap();
  // Servo slew: walks the arm toward its target one increment at a time
  serviceServo();
}
