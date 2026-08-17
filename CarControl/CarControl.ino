// 4WD car control via keyboard (Bluetooth serial), see README.md for wiring.
// Car mode:        W = forward, S = backward, A = left, D = right, Space = stop
// Servo mode:       A = pan left, D = pan right, W = center, entered/exited via 'g'/'c'
// Autonomous mode:  entered with 'o', exited (like Servo mode) with 'c'.
//                    Drives itself; W/A/S/D/V have no effect until 'c' is sent.

#include <SoftwareSerial.h>
#include <Servo.h>

const int BT_RX = 7; // HC-05 TXD
const int BT_TX = 8; // HC-05 RXD (through voltage divider)
SoftwareSerial bt(BT_RX, BT_TX);

const int IN1 = 2;  // Left side
const int IN2 = 3;  // Left side
const int IN3 = 4;  // Right side
const int IN4 = 5;  // Right side
const int ENA = 6;  // Left side enable (PWM)
const int ENB = 11; // Right side enable (PWM)

const int SERVO_PIN = 9;
const int SERVO_STEP = 10; // degrees per keypress
Servo panServo;
int panAngle = 90; // 0-180, centered
bool servoMode = false; // toggled by the GUI's Debug button ('g'/'c')

const int TRIG_PIN = 12;
const int ECHO_PIN = 10;
const unsigned long DISTANCE_INTERVAL_MS = 200;
unsigned long lastDistanceSend = 0;

// pulseIn() BLOCKS until it sees an echo or hits this timeout, and while it
// blocks SoftwareSerial can't reliably receive - so a long timeout directly
// costs us dropped/corrupted Bluetooth bytes. 12ms is ~2m of range, far
// beyond the 20-30cm thresholds that actually matter here, and cuts the
// worst-case blocking to a third of the old 30ms (~5m).
const unsigned long ECHO_TIMEOUT_US = 12000;

int speed = 200; // 0-255, changed via 'V' + one raw byte
bool awaitingSpeedByte = false;

// --- Mode-switch debounce -----------------------------------------------
// Movement bytes (w/a/s/d/space) are low-stakes if one gets corrupted by
// noise on the SoftwareSerial link - you just lose one drive command
// briefly. Mode-switch bytes ('g'/'c'/'o') are not: a single glitched byte
// that happens to land on 'g' or 'c' can silently switch modes mid-drive -
// including silently exiting Autonomous mode, which looks exactly like the
// car "freezing" since nothing else runs updateAutonomous() anymore. This
// requires the *same* command to arrive MODE_CONFIRM_COUNT times in a row,
// each within MODE_CONFIRM_WINDOW_MS of the last, before it takes effect
// (the GUI sends each mode command that many times back-to-back). Two
// matching corrupted bytes in a row is already unlikely; three drops the
// odds further still.
char pendingModeCmd = 0;
unsigned long pendingModeCmdAt = 0;
int pendingModeCmdCount = 0;
const unsigned long MODE_CONFIRM_WINDOW_MS = 500;
const int MODE_CONFIRM_COUNT = 3;

bool confirmModeCommand(char cmd) {
  unsigned long now = millis();
  if (pendingModeCmd == cmd && (now - pendingModeCmdAt) <= MODE_CONFIRM_WINDOW_MS) {
    pendingModeCmdCount++;
  } else {
    pendingModeCmdCount = 1;
  }
  pendingModeCmd = cmd;
  pendingModeCmdAt = now;
  if (pendingModeCmdCount >= MODE_CONFIRM_COUNT) {
    pendingModeCmdCount = 0; // consumed - next byte starts a fresh confirmation
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Autonomous mode
//
// Design goal: loop() must NEVER block (no delay()-based waiting), because
// it also has to keep draining incoming Bluetooth bytes every pass so you
// can send 'c' at any moment and immediately regain manual control. So
// instead of writing this as a single function with delay() calls in a row,
// it's a state machine: each pass through loop() we ask "has enough time
// passed to finish what we're currently doing?" and if so, move to the next
// state. See updateAutonomous() near the bottom of the file for the actual
// step logic - this section is just the tunable numbers and the state list.
// ---------------------------------------------------------------------------

bool autonomousMode = false; // toggled by 'o' (enter) / 'c' (exit, shared with Servo mode)

// --- Tunable behavior constants ---------------------------------------
// These are best-guess starting points; expect to adjust them once you see
// the car actually run (e.g. if it stops too late, raise OBSTACLE_STOP_CM
// or lower AUTO_SPEED; if it "flickers" between stopped/driving right at
// the boundary, widen the gap between the STOP and CLEAR thresholds).

const int OBSTACLE_STOP_CM = 20;  // stop and start avoiding once closer than this
const int OBSTACLE_CLEAR_CM = 30; // must read *clearer* than this before resuming forward.
                                   // This gap between STOP (20) and CLEAR (30) is
                                   // "hysteresis" - without it, a reading that hovers
                                   // right around one single threshold would make the
                                   // car flicker: stop, think it's clear, lurch
                                   // forward, stop again, forever, right at that spot.
const int AUTO_SPEED = 150;       // 0-255. Deliberately moderate (not Turbo) - the
                                   // faster the car is going, the more distance it
                                   // needs to actually stop within OBSTACLE_STOP_CM
                                   // after deciding to. V (speed command) is ignored
                                   // while autonomous, since it always uses this value.

const unsigned long DISTANCE_CHECK_INTERVAL_MS = 150; // how often to re-ping the sensor while driving forward
const unsigned long SETTLE_PAUSE_MS = 150; // brief full-stop pause before reversing a motor
                                            // direction (forward<->backward, or before a
                                            // turn). Snapping straight into the opposite
                                            // direction is jerky and can sag the supply
                                            // voltage enough to glitch the Bluetooth link
                                            // mid-command - a short pause avoids that.
const unsigned long BACKUP_DURATION_MS = 400;     // how long to reverse away from an obstacle
const unsigned long SCAN_PAN_SETTLE_MS = 300;     // time to let the servo *physically* finish
                                                   // swinging before trusting the reading -
                                                   // reading too early measures whatever the
                                                   // sensor was pointed at mid-swing, not left/right.
const unsigned long TURN_STEP_DURATION_MS = 400;  // one "attempt" worth of turning
const int MAX_TURN_ATTEMPTS = 4;                  // bounded retries per obstacle - see AUTO_VERIFY
const unsigned long FULL_SPIN_DURATION_MS = 1200; // used once small turns have been retried
                                                   // MAX_TURN_ATTEMPTS times and still haven't
                                                   // cleared the obstacle (e.g. a tight corner)

// Must match the Servo mode 'a'/'d' direction above - a higher angle points left.
const int AUTO_LOOK_LEFT_ANGLE = 180;
const int AUTO_LOOK_RIGHT_ANGLE = 0;
const int AUTO_LOOK_CENTER_ANGLE = 90;

// --- State machine ------------------------------------------------------
enum AutoState {
  AUTO_DRIVE,        // driving forward, periodically checking for obstacles
  AUTO_STOP_PAUSE,   // just stopped after finding an obstacle; brief pause before reversing
  AUTO_REVERSE,      // backing away from the obstacle
  AUTO_REVERSE_PAUSE,// just stopped backing up; brief pause before panning the servo
  AUTO_LOOK_LEFT,    // servo swinging/settling to look left, then reads distance
  AUTO_LOOK_RIGHT,   // servo swinging/settling to look right, then reads distance
  AUTO_DECIDE,       // instant: compare left vs right, pick a direction, recenter servo
  AUTO_TURN_PAUSE,   // brief pause before turning (avoids an abrupt direction flip)
  AUTO_TURN,         // pivoting toward the chosen direction
  AUTO_VERIFY_PAUSE, // just stopped turning; brief pause before re-checking
  AUTO_VERIFY,       // re-check distance dead ahead - loops back to another turn if still blocked
};

AutoState autoState = AUTO_DRIVE;
unsigned long autoStateStartedAt = 0; // millis() timestamp when we entered the current state
unsigned long autoLastDistanceCheck = 0; // separate timer just for the AUTO_DRIVE polling rate
unsigned long autoLastTelemetrySend = 0; // throttles how often AUTO_DRIVE reports D: over Bluetooth

long autoLeftDistance = 0;
long autoRightDistance = 0;
int autoTurnDirection = 0;  // -1 = turning left, +1 = turning right
int autoTurnAttempts = 0;   // retries so far on the *current* obstacle (reset when a new one is found)

void leftForward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
}

void leftBackward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
}

void leftStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
}

void rightForward() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void rightBackward() {
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void rightStop() {
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void moveForward() {
  leftForward();
  rightForward();
}

void moveBackward() {
  leftBackward();
  rightBackward();
}

void turnLeft() {
  leftForward();
  rightBackward();
}

void turnRight() {
  leftBackward();
  rightForward();
}

void stopCar() {
  leftStop();
  rightStop();
}

// Returns distance in cm, or -1 if no echo was received (out of range/timeout).
long readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (duration == 0) return -1;
  return duration / 58;
}

// Ultrasonic sensors occasionally return garbage from a single ping (echo
// bounce, timing glitch). Taking 3 quick readings and using the middle
// (median) value throws out a single bad sample without needing an average
// (which a wild outlier can still skew badly). This is what autonomous mode
// uses instead of raw readDistanceCM() - a false "obstacle!" from noise, or
// a false "clear!" mid-turn, is exactly the kind of thing that makes an
// autonomous car behave erratically.
long readDistanceFiltered() {
  long readings[3];
  for (int i = 0; i < 3; i++) {
    long d = readDistanceCM();
    // Treat "no echo" as "nothing in range" (i.e. clearly not an obstacle),
    // not as "very close" - otherwise a failed reading would look like a
    // wall right in front of the sensor and trigger a false stop.
    readings[i] = (d < 0) ? 999 : d;
  }
  // Tiny 3-element sort (not worth pulling in a sort() for 3 numbers).
  for (int i = 0; i < 2; i++) {
    for (int j = i + 1; j < 3; j++) {
      if (readings[j] < readings[i]) {
        long tmp = readings[i];
        readings[i] = readings[j];
        readings[j] = tmp;
      }
    }
  }
  return readings[1]; // the middle element after sorting = the median
}

// Human-readable state names for the STATE: debug telemetry below - makes it
// possible to see exactly how far the state machine actually got just by
// watching the GUI log, instead of guessing from the car's physical motion.
//
// Takes a plain int (not AutoState) because the Arduino IDE auto-generates
// function prototypes and inserts them above the enum's own definition -
// using the enum type here would break that auto-generated prototype.
const char* autoStateName(int s) {
  switch (s) {
    case AUTO_DRIVE: return "DRIVE";
    case AUTO_STOP_PAUSE: return "STOP_PAUSE";
    case AUTO_REVERSE: return "REVERSE";
    case AUTO_REVERSE_PAUSE: return "REVERSE_PAUSE";
    case AUTO_LOOK_LEFT: return "LOOK_LEFT";
    case AUTO_LOOK_RIGHT: return "LOOK_RIGHT";
    case AUTO_DECIDE: return "DECIDE";
    case AUTO_TURN_PAUSE: return "TURN_PAUSE";
    case AUTO_TURN: return "TURN";
    case AUTO_VERIFY_PAUSE: return "VERIFY_PAUSE";
    case AUTO_VERIFY: return "VERIFY";
    default: return "?";
  }
}

// Switches the state machine to newState and records when we entered it, so
// the per-state logic in updateAutonomous() can measure "how long have I
// been in this state" via millis() - autoStateStartedAt. Takes a plain int
// for the same auto-prototype reason as autoStateName() above.
void autoEnterState(int newState) {
  autoState = (AutoState)newState;
  autoStateStartedAt = millis();
  bt.print("STATE:"); bt.println(autoStateName(newState));
}

void enterAutonomousMode() {
  servoMode = false; // Autonomous and Servo are mutually exclusive
  autonomousMode = true;
  analogWrite(ENA, AUTO_SPEED); // fixed speed - ignores whatever 'speed' (V command) was set to
  analogWrite(ENB, AUTO_SPEED);
  if (!panServo.attached()) panServo.attach(SERVO_PIN);
  panServo.write(AUTO_LOOK_CENTER_ANGLE);
  bt.print("A:"); bt.println(AUTO_LOOK_CENTER_ANGLE);
  autoTurnAttempts = 0;
  autoLastDistanceCheck = millis();
  moveForward();
  autoEnterState(AUTO_DRIVE);
}

// Shared by both the 'c' (return to Car mode) and 'g' (enter Servo mode)
// commands - either way, autonomous driving needs to stop cleanly and hand
// the motor speed back to the user's manual setting before anything else happens.
void exitAutonomousMode() {
  if (!autonomousMode) return;
  autonomousMode = false;
  bt.println("EXIT_AUTO"); // one-off marker so we can see in the log whether
                            // this ever fires unexpectedly (e.g. from a
                            // corrupted byte pair on the Bluetooth link)
  stopCar();
  analogWrite(ENA, speed); // restore the manually-set speed (V command)
  analogWrite(ENB, speed);
}

// Advances the autonomous state machine by (at most) one step. Called once
// per loop() pass whenever autonomousMode is true. Every state either:
//   (a) does something instantly and moves straight to the next state, or
//   (b) starts a timed action (drive, reverse, turn, servo pan) and then
//       just waits - checking "has enough time passed?" - until it's done.
// Because this returns immediately either way, loop() stays free to keep
// draining Bluetooth bytes every pass, so 'c' always works instantly even
// mid-maneuver.
void updateAutonomous() {
  unsigned long now = millis();
  unsigned long elapsed = now - autoStateStartedAt; // time spent in the current state so far

  switch (autoState) {

    case AUTO_DRIVE:
      // Don't ping the sensor every single loop() pass (pulseIn() blocks
      // for up to 30ms while waiting for an echo) - just often enough to
      // react promptly, per DISTANCE_CHECK_INTERVAL_MS.
      if (now - autoLastDistanceCheck >= DISTANCE_CHECK_INTERVAL_MS) {
        autoLastDistanceCheck = now;
        long distance = readDistanceFiltered();
        // Only send this over Bluetooth once a second (not every 150ms
        // check) - sending it constantly while the motors are running
        // floods SoftwareSerial and seems to be corrupting bytes in
        // transit (motor PWM interrupts vs. SoftwareSerial's precise bit
        // timing). The obstacle check itself still runs at the full rate
        // below - this only throttles how often we *report* it.
        if (now - autoLastTelemetrySend >= 1000) {
          autoLastTelemetrySend = now;
          bt.print("D:"); bt.println(distance);
        }

        if (distance < OBSTACLE_STOP_CM) {
          stopCar();
          autoTurnAttempts = 0; // this is a brand new obstacle - fresh retry budget
          autoEnterState(AUTO_STOP_PAUSE);
        }
      }
      break;

    case AUTO_STOP_PAUSE:
      // See SETTLE_PAUSE_MS above: let the car actually finish stopping
      // before slamming it into reverse.
      if (elapsed >= SETTLE_PAUSE_MS) {
        moveBackward();
        autoEnterState(AUTO_REVERSE);
      }
      break;

    case AUTO_REVERSE:
      if (elapsed >= BACKUP_DURATION_MS) {
        stopCar();
        autoEnterState(AUTO_REVERSE_PAUSE);
      }
      break;

    case AUTO_REVERSE_PAUSE:
      if (elapsed >= SETTLE_PAUSE_MS) {
        panServo.write(AUTO_LOOK_LEFT_ANGLE);
        bt.print("A:"); bt.println(AUTO_LOOK_LEFT_ANGLE);
        autoEnterState(AUTO_LOOK_LEFT);
      }
      break;

    case AUTO_LOOK_LEFT:
      // Wait for the servo to physically finish swinging before trusting
      // the reading - see SCAN_PAN_SETTLE_MS above.
      if (elapsed >= SCAN_PAN_SETTLE_MS) {
        autoLeftDistance = readDistanceFiltered();
        bt.print("D:"); bt.println(autoLeftDistance);
        panServo.write(AUTO_LOOK_RIGHT_ANGLE);
        bt.print("A:"); bt.println(AUTO_LOOK_RIGHT_ANGLE);
        autoEnterState(AUTO_LOOK_RIGHT);
      }
      break;

    case AUTO_LOOK_RIGHT:
      if (elapsed >= SCAN_PAN_SETTLE_MS) {
        autoRightDistance = readDistanceFiltered();
        bt.print("D:"); bt.println(autoRightDistance);
        autoEnterState(AUTO_DECIDE);
      }
      break;

    case AUTO_DECIDE:
      // Pick whichever side currently has more room. This doesn't have to
      // be a perfect decision - AUTO_VERIFY below is what actually catches
      // "that wasn't enough room after all" and retries, rather than this
      // single left/right comparison needing to be correct every time.
      autoTurnDirection = (autoLeftDistance >= autoRightDistance) ? -1 : 1;
      panServo.write(AUTO_LOOK_CENTER_ANGLE); // recenter so the sensor faces forward again
      bt.print("A:"); bt.println(AUTO_LOOK_CENTER_ANGLE);
      autoEnterState(AUTO_TURN_PAUSE);
      break;

    case AUTO_TURN_PAUSE:
      if (elapsed >= SETTLE_PAUSE_MS) {
        autoTurnAttempts++;
        if (autoTurnDirection < 0) turnLeft(); else turnRight();
        autoEnterState(AUTO_TURN);
      }
      break;

    case AUTO_TURN: {
      // The first MAX_TURN_ATTEMPTS retries are short, incremental turns.
      // If those still haven't cleared the obstacle (checked in
      // AUTO_VERIFY below), later attempts use a much longer full-spin
      // duration instead - handles a tight corner where lots of small
      // turns in the same spot would otherwise retry forever.
      unsigned long turnDuration =
          (autoTurnAttempts > MAX_TURN_ATTEMPTS) ? FULL_SPIN_DURATION_MS : TURN_STEP_DURATION_MS;
      if (elapsed >= turnDuration) {
        stopCar();
        autoEnterState(AUTO_VERIFY_PAUSE);
      }
      break;
    }

    case AUTO_VERIFY_PAUSE:
      if (elapsed >= SETTLE_PAUSE_MS) {
        autoEnterState(AUTO_VERIFY);
      }
      break;

    case AUTO_VERIFY: {
      // This check is the actual fix for "detects an obstacle but still
      // drives into it": never assume a turn worked. Look again before
      // committing back to driving forward.
      long distance = readDistanceFiltered();
      bt.print("D:"); bt.println(distance);
      if (distance >= OBSTACLE_CLEAR_CM) {
        // Genuinely clear now (OBSTACLE_CLEAR_CM, not OBSTACLE_STOP_CM -
        // see the hysteresis note above).
        moveForward();
        autoEnterState(AUTO_DRIVE);
      } else {
        // Still blocked - turn a bit more the same direction and check
        // again. autoTurnAttempts (incremented in AUTO_TURN_PAUSE) is what
        // eventually escalates this into a full spin instead of retrying
        // forever in place.
        autoEnterState(AUTO_TURN_PAUSE);
      }
      break;
    }
  }
}

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  analogWrite(ENA, speed);
  analogWrite(ENB, speed);
  stopCar();

  // Not attached here on purpose - Car mode is the default at boot, and the
  // servo's background PWM-refresh interrupt would otherwise run the whole
  // time even though nothing's using it, fighting with Bluetooth reception.
  // It gets attached on demand when entering Servo or Autonomous mode.

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  bt.begin(9600);

  // Diagnostic: setup() only runs on power-up or reset. If "BOOT" ever shows
  // up in the GUI log *while you're driving*, the Arduino is resetting
  // mid-run - almost always a brown-out from motor current sagging the
  // supply, not a software bug. That would also explain autonomous mode
  // silently stopping: a reset lands back in Car mode by default.
  bt.println("BOOT");
}

void loop() {
  while (bt.available() > 0) {
    char c = bt.read();

    if (awaitingSpeedByte) {
      awaitingSpeedByte = false;
      // Autonomous mode always drives at its own fixed AUTO_SPEED (see the
      // constant above) - a V command received while it's active is
      // consumed (so the byte doesn't leak into the next command) but
      // otherwise ignored.
      if (!autonomousMode) {
        speed = (uint8_t)c;
        analogWrite(ENA, speed);
        analogWrite(ENB, speed);
      }
      continue;
    }

    if (c == 'o' || c == 'O') {
      if (confirmModeCommand('o')) {
        enterAutonomousMode();
      }
      continue;
    }
    if (c == 'g' || c == 'G') {
      if (confirmModeCommand('g')) {
        exitAutonomousMode(); // no-op if it wasn't running
        stopCar();
        servoMode = true;
        // Servo's background PWM-refresh interrupt only exists once
        // attach()ed - only turn it on while something is actually using it.
        if (!panServo.attached()) panServo.attach(SERVO_PIN);
        panServo.write(panAngle);
        bt.print("A:"); bt.println(panAngle);
      }
      continue;
    }
    if (c == 'c' || c == 'C') {
      if (confirmModeCommand('c')) {
        exitAutonomousMode(); // no-op if it wasn't running
        servoMode = false;
        // Detach so the Servo library's interrupt isn't running in the
        // background while driving manually - it can fight with
        // SoftwareSerial's Bluetooth reception timing (see README).
        panServo.detach();
      }
      continue;
    }

    if (autonomousMode) {
      // Autonomous is driving the motors itself via updateAutonomous()
      // below - ignore any other byte (W/A/S/D etc.) until 'c' is sent.
      continue;
    }

    if (servoMode) {
      switch (c) {
        // 'a' (pan left) increases the angle, 'd' (pan right) decreases it.
        // If the servo gets remounted again, flip the +/- here to match.
        case 'a': case 'A':
          panAngle = min(180, panAngle + SERVO_STEP);
          panServo.write(panAngle);
          bt.print("A:"); bt.println(panAngle);
          break;
        case 'd': case 'D':
          panAngle = max(0, panAngle - SERVO_STEP);
          panServo.write(panAngle);
          bt.print("A:"); bt.println(panAngle);
          break;
        case 'w': case 'W':
          panAngle = 90;
          panServo.write(panAngle);
          bt.print("A:"); bt.println(panAngle);
          break;
        default:
          break;
      }
      continue;
    }

    switch (c) {
      case 'w': case 'W':
        moveForward();
        break;
      case 's': case 'S':
        moveBackward();
        break;
      case 'a': case 'A':
        turnLeft();
        break;
      case 'd': case 'D':
        turnRight();
        break;
      case ' ':
        stopCar();
        break;
      case 'v': case 'V':
        awaitingSpeedByte = true;
        break;
      default:
        break;
    }
  }

  // Car and Servo mode both get this periodic readout; Autonomous mode
  // sends its own D: telemetry at the points that matter in
  // updateAutonomous(), so it's excluded here to avoid two independent
  // timers both pinging the sensor.
  if (!autonomousMode && millis() - lastDistanceSend >= DISTANCE_INTERVAL_MS) {
    lastDistanceSend = millis();
    long cm = readDistanceCM();
    bt.print("D:");
    bt.println(cm);
  }

  if (autonomousMode) {
    updateAutonomous();
  }
}
