# Arduino 4WD Bluetooth Car

## Hardware

### Arduino Uno

### Motors

4WD chassis with two motors per side, wired in parallel per side (not independently addressable):

- **Left side**: L1 (front) + L2 (back), both connected to L298N OUT1 (red) / OUT2 (black)
- **Right side**: R1 (front) + R2 (back), both connected to L298N OUT3 (red) / OUT4 (black)

Confirmed by testing: for both sides, `IN1 = LOW, IN2 = HIGH` drives that side forward (`HIGH, LOW` drives it backward). This is the opposite of the L298N's usual documented convention — specific to how these motors/wires ended up connected on this chassis.

### L298N

| L298N | Arduino | Notes |
|-------|----------|-------|
| IN1 | D2 | Left side |
| IN2 | D3 | Left side |
| IN3 | D4 | Right side |
| IN4 | D5 | Right side |
| ENA | D6 | Left side enable (PWM, Timer0) |
| ENB | D11 | Right side enable (PWM, Timer2) |
| OUT1 (red) | — | Left motors (L1 + L2) |
| OUT2 (black) | — | Left motors (L1 + L2) |
| OUT3 (red) | — | Right motors (R1 + R2) |
| OUT4 (black) | — | Right motors (R1 + R2) |

`ENA`/`ENB` are on D6/D11 instead of the more common D9/D10 — the Servo library (used for the ultrasonic scan servo) owns Timer1, which drives PWM on D9/D10. Using those pins for motor speed would silently break once a servo is attached.

### HC-05

| HC-05 | Arduino |
|--------|----------|
| VCC | 5V |
| GND | GND |
| TXD | D7 |
| RXD | D8 (through 1k/2k voltage divider) |

### HC-SR04 (ultrasonic distance sensor)

| HC-SR04 | Arduino |
|---------|----------|
| VCC | 5V |
| GND | GND |
| Trig | D12 |
| Echo | D10 (Uno is 5V logic — no divider needed, unlike the HC-05) |

Echo is deliberately not on D13 — that pin is hard-wired to the Uno's built-in LED, which adds a small parasitic load and distorted `pulseIn()` timing during testing.

### Servo (scans the ultrasonic sensor left/center/right)

| Servo wire | Connects to |
|---|---|
| Signal (orange/yellow) | D9 |
| VCC (red) | **Separate 5V supply** — not the Arduino's 5V pin, to avoid brownouts. Use the L298N's onboard 5V regulator output (if its jumper is enabled and rated for the extra load) or a dedicated UBEC. |
| GND (brown/black) | Common ground with everything else |

If the servo jitters while holding a position, it's most likely noise from the L298N's onboard regulator, not a wiring fault — add a decoupling capacitor (~470µF electrolytic + ~0.1µF ceramic, in parallel) across the servo's VCC/GND, as close to the servo leads as possible.

Mounted at the front of the chassis with the HC-SR04 on its horn.

With the servo as currently mounted, a **higher** angle points the sensor **left** and a lower angle points it right. `CarControl.ino` encodes this in one place per mode — the `'a'`/`'d'` cases in Servo mode, and `AUTO_LOOK_LEFT_ANGLE`/`AUTO_LOOK_RIGHT_ANGLE` for Autonomous. If the servo is ever remounted and left/right come out swapped, flip both together (they're commented as such) so manual panning and autonomous scanning stay consistent.

---

## Power

- Battery (2× 3.7V Li-ion, ~7.4V) → L298N motor power
- **Arduino → its own separate supply** (USB, powerbank, or a dedicated regulator) — see below
- All grounds common: Arduino GND ↔ L298N GND ↔ battery GND

### The Arduino must not be powered from the L298N's 5V regulator

This one cost a long debugging session, so it's worth stating plainly: **do not feed the Arduino's 5V pin from the L298N's onboard 5V output.**

That regulator is a small linear regulator meant only to power the L298N's own logic — a few tens of mA. Running the Arduino + HC-05 + HC-SR04 + servo from it is far beyond what it can hold up. When the motors draw current the battery sags, the regulator loses its input headroom, its 5V output collapses, and **the Arduino brown-out resets**.

A reset is easy to mistake for a software bug, because it doesn't look like a crash:

- `setup()` re-runs, so the sketch lands back in Car mode with the motors stopped
- Autonomous mode silently stops driving — but *without* going through `exitAutonomousMode()`, so no clean exit is reported
- Periodic distance telemetry starts up again (it only runs outside Autonomous mode), so the GUI looks partly alive
- Symptoms cluster around the moments of peak current draw: starting the motors, or the servo swinging

If any of that shows up, check for a `BOOT` line in the GUI log. `setup()` prints it once, so **`BOOT` appearing mid-run means the board reset** — that's a power problem, not a logic problem.

The servo has the same requirement for the same reason (see the Servo wiring table above). If you can't separate the supplies, decoupling capacitors across the 5V rail help, but a genuinely separate supply for the Arduino is the real fix.

---

## Bluetooth

MAC Address

```
00:22:12:02:49:0D
```

Serial Port

```
/dev/rfcomm0
```

Baud Rate

```
9600
```

---

## Controls

Single-character commands over serial (Bluetooth via `/dev/rfcomm0`, or USB), 9600 baud. Letters are case-insensitive (`w` and `W` both work). Implemented in [`CarControl/CarControl.ino`](CarControl/CarControl.ino).

### Car mode (default)

| Key | Action |
|-------|--------|
| W | Forward |
| A | Left |
| S | Backward |
| D | Right |
| Space | Stop |

### Speed

Send `V` followed immediately by one raw byte (0-255) to set the drive speed (PWM duty cycle on `ENA`/`ENB`). Stays in effect until the next `V` command.

### Servo/debug mode

| Key | Action |
|-------|--------|
| G | Enter Servo mode (stops the car first) |
| C | Return to Car mode |

While in Servo mode, `W`/`A`/`S`/`D` control the pan servo instead of driving:

| Key | Action |
|-------|--------|
| A | Pan left (steps the servo angle) |
| D | Pan right (steps the servo angle) |
| W | Center (90°) |

### Autonomous mode

| Key | Action |
|-------|--------|
| O | Enter Autonomous mode |
| C | Return to Car mode (works at any point, including mid-maneuver) |

Drives itself and avoids obstacles. `W`/`A`/`S`/`D` and `V` are ignored while it's active — send `C` to take back control. It always drives at its own fixed speed (`AUTO_SPEED`), independent of whatever `V` was set to.

The avoidance sequence, on finding something closer than `OBSTACLE_STOP_CM`:

1. Stop, pause briefly to settle
2. Reverse away from the obstacle
3. Pan the servo left, read distance; pan right, read distance
4. Pick whichever side has more room, recenter the servo
5. Pivot-turn toward that side
6. **Verify** — re-read distance ahead. If still blocked, turn further and check again; otherwise resume driving

Step 6 is the important one. Turning for a fixed guessed duration and *assuming* it worked is the usual reason a car detects an obstacle but still drives into it. Retries are bounded — after several short turns fail, it escalates to a longer spin so it can get out of a tight corner instead of retrying forever in place.

Other details that keep it from behaving erratically:

- **Median-of-3 distance filtering**, with "no echo" treated as *clear* rather than *close*, so a single noisy ping can't trigger a false stop.
- **Hysteresis**: stop below `OBSTACLE_STOP_CM` (20cm), but don't resume until the path reads clearer than `OBSTACLE_CLEAR_CM` (30cm). A single shared threshold makes the car flicker between stopped and driving.
- **Settle pauses** before every direction reversal, instead of snapping straight from forward to reverse.
- Implemented as a **non-blocking `millis()` state machine**, never `delay()` — `loop()` keeps draining incoming bytes every pass, so `C` interrupts instantly.

All the tunables (`OBSTACLE_STOP_CM`, `AUTO_SPEED`, backup/turn/scan durations, retry count) are named constants at the top of [`CarControl/CarControl.ino`](CarControl/CarControl.ino), with comments on what to adjust for which symptom.

### Telemetry

The Arduino streams these back over the same serial link:

- `D:<cm>\n` — ultrasonic distance. `-1` means no echo (out of range/timeout). Sent about every 200ms in Car and Servo mode; throttled in Autonomous mode, which sends its own readings at the points that matter.
- `A:<angle>\n` — current servo angle (0-180), whenever it changes.
- `STATE:<name>\n` — Autonomous state machine transitions (`DRIVE`, `REVERSE`, `LOOK_LEFT`, …). Diagnostic.
- `BOOT\n` — printed once by `setup()`. **Mid-run means the board reset** — see Power.
- `EXIT_AUTO\n` — Autonomous mode exited cleanly.

[`gui.py`](gui.py) parses `D:` and `A:` into live readouts and logs the rest.

### Known limitations

`Servo` and `SoftwareSerial` both depend on precise AVR interrupt timing and interfere with each other, and motor PWM adds electrical noise on top — Bluetooth bytes can occasionally arrive corrupted or dropped. Several things reduce this:

- The servo is **attached on demand** (only in Servo and Autonomous mode) rather than at boot, so its background PWM-refresh interrupt isn't running while driving manually.
- `pulseIn()` uses a **12ms timeout** (~2m, well beyond the thresholds that matter) instead of 30ms, cutting how long the sketch is blocked and unable to receive.
- Mode switches (`G`/`C`/`O`) require the **same byte 3 times in a row within 500ms** before taking effect, so a corrupted byte can't silently flip modes mid-drive. `gui.py` sends each mode command three times for this reason.

It isn't fully eliminated. If a command doesn't register, press again — that's the hardware/library interaction, not a logic bug.

---

## Setup

Upload [`CarControl/CarControl.ino`](CarControl/CarControl.ino) via the Arduino IDE before connecting Bluetooth (uploading and an active `/dev/rfcomm0` connection conflict).

### GUI (recommended)

```bash
./gui.sh
```

A Tkinter window with a **Connect** button (runs `connect.sh` in the background — no need to run it separately first), drive buttons + WASD/arrow keys, a speed slider, a **Car / Servo / Auto** mode toggle, live Distance/Servo readouts, and a timestamped command log. Manual controls grey out while Auto is active. Needs `python3-tk`:

```bash
sudo apt install python3-tk
```

`sudo` may prompt for a password the first time you click Connect — that prompt appears in whichever terminal you launched `gui.py` from.

### Terminal / scripted control

Connect Bluetooth (backgrounds an `rfcomm connect`, polls until `/dev/rfcomm0` is live):

```bash
./connect.sh
```

Then drive with hold-to-move keyboard control:

```bash
python3 controller.py
```

W/A/S/D to drive (hold to move, release to stop), Q to quit. The `keyboard` module needs root or your user in the `input` group to capture keys on Linux.

---

## Python Environment

Activate

```bash
source .venv/bin/activate
```

Install dependencies

```bash
pip install pyserial keyboard
```

---

## Notes

- `connect.sh` backgrounds the Bluetooth connection process; leave it (or the GUI) running while driving.
- Upload Arduino sketches before starting the Bluetooth connection.
- Kill the active `rfcomm connect` process (`sudo pkill -f "rfcomm connect rfcomm0"`) before uploading new sketches — the serial port can't be shared between them.
