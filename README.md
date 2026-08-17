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

Mount the servo at the front of the chassis with the HC-SR04 on its horn. (Not mounted yet as of writing — bench-tested first.)

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

Single-character commands over serial (USB or Bluetooth), 9600 baud. Letters are case-insensitive (`o` and `O` both work).

| Key | Action |
|-------|--------|
| W | Forward |
| A | Left |
| S | Backward |
| D | Right |
| Space | Stop (works in any mode, including Autonomous) |
| Q | Quit controller (host-side only, `controller.py`) |

### Modes

| Key | Mode | Behavior |
|-----|------|----------|
| N | Normal | Default driving speed |
| T | Turbo | Max speed |
| P | Precision | Reduced speed, for tight maneuvering |
| O | Autonomous | Ignores W/A/S/D; drives itself at a fixed ~55% speed. On obstacle (<20cm ahead, median-of-3 filtered): backs up briefly, checks left first and commits to a ~90° left turn if clear, otherwise checks right and turns there, otherwise backs off further and spins ~180° to try a different direction (won't just give up in a corner) |

### Speed

Send `V` followed immediately by one raw byte (0-255) to set an exact speed, independent of mode — e.g. mode presets pick a starting speed, but a `V` command overrides it until the next mode switch. Autonomous mode ignores `V` entirely and always drives at its own fixed speed (~140/255, ~55%).

### Known limitation

`Servo` and `SoftwareSerial` both depend on precise AVR interrupt timing, and can occasionally interfere with each other — a Bluetooth command can rarely be dropped mid-receive. `loop()` drains every queued byte each pass to reduce the odds of this, but it isn't fully eliminated. If a mode/command press doesn't register, try again — this is a known hardware/library interaction, not a logic bug.

---

## Setup

Connect Bluetooth

```bash
./connect.sh
```

Run controller

```bash
./run.sh
```

Disconnect

```bash
./disconnect.sh
```

Or use the GUI instead of the scripts above — Connect/Disconnect/Run/Stop buttons, with a log panel:

```bash
./gui.sh
```

The GUI needs `python3-tk` (one-time system install) and uses `pkexec` to prompt for root graphically:

```bash
sudo apt install python3-tk
```

Or use the web UI — same Connect/Disconnect, plus speed, mode, and drive controls:

```bash
./web.sh
```

Then open `http://localhost:8765`. This runs [`bridge.py`](bridge.py), a small local server that serves the page from [`web/`](web/) and relays its commands to the Arduino over `/dev/rfcomm0` (same `pkexec`/rfcomm approach as the GUI — no need to run `connect.sh` separately, the page's Connect button does it). It doesn't use Web Bluetooth, since the HC-05 is classic Bluetooth (SPP), not BLE, which browsers can't talk to directly.

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

- Keep `connect.sh` running while driving.
- Upload Arduino sketches before starting the Bluetooth connection.
- Disconnect the Bluetooth session before uploading new sketches.
