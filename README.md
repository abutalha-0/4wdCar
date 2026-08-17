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

The servo ended up mounted physically reversed relative to how it was bench-tested — commanding a higher angle (via `A` in Servo mode) actually pans the sensor left, and a lower angle pans it right. `CarControl.ino` compensates for this in software (see Controls below) so the on-screen "pan left"/"pan right" behavior matches reality; the raw angle sent to `Servo.write()` is still just 0-180.

---

## Power

- Battery → L298N motor power
- USB → Arduino (during development)
- Arduino GND connected to L298N GND

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

The Arduino also streams live telemetry back over the same serial link while in Servo mode:

- `D:<cm>\n` — ultrasonic distance reading (HC-SR04), sent about every 200ms. `-1` means no echo (out of range/timeout).
- `A:<angle>\n` — current servo angle (0-180), sent whenever it changes.

[`gui.py`](gui.py) parses both and shows them as live "Distance" / "Servo" readouts.

### Known limitation

`Servo` and `SoftwareSerial` both depend on precise AVR interrupt timing, and can occasionally interfere with each other — a Bluetooth command can rarely be dropped mid-receive. `loop()` drains every queued byte each pass to reduce the odds of this, but it isn't fully eliminated. If a command press doesn't register, try again — this is a known hardware/library interaction, not a logic bug.

---

## Setup

Upload [`CarControl/CarControl.ino`](CarControl/CarControl.ino) via the Arduino IDE before connecting Bluetooth (uploading and an active `/dev/rfcomm0` connection conflict).

### GUI (recommended)

```bash
./gui.sh
```

A Tkinter window with a **Connect** button (runs `connect.sh` in the background — no need to run it separately first), drive buttons + WASD/arrow keys, a speed slider, a Car/Servo mode toggle, live Distance/Servo readouts, and a timestamped command log. Needs `python3-tk`:

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
