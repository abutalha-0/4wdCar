# Windows Setup

This project was built and tested on Linux (see [README.md](README.md)) — this doc covers what's different on Windows.

**Read this first:** the GUI's **Connect** button runs [`connect.sh`](connect.sh), a Linux bash script that uses `rfcomm` (Linux's Bluetooth-to-serial-port tool). That doesn't exist on Windows, so clicking Connect in [`gui.py`](gui.py) will just fail with a "connect.sh failed" error. Windows doesn't need an equivalent step anyway — pairing a Bluetooth device in Windows Settings automatically creates a COM port for it, no separate "connect" action required. So on Windows, use **[`controller.py`](controller.py)** (the terminal-based hold-to-drive controller) instead of the GUI, until/unless the GUI's Connect flow is made cross-platform.

---

## 1. Install prerequisites

- **Python 3** — download from [python.org](https://www.python.org/downloads/). During install, check **"Add python.exe to PATH"**.
  - Unlike Linux, `tkinter` ships with the standard Windows installer — no separate install needed if you do end up using the GUI.
- **Arduino IDE** — download from [arduino.cc](https://www.arduino.cc/en/software).
- **Git** (optional, for cloning) — [git-scm.com](https://git-scm.com/download/win). If you'd rather not install Git, download the repo as a ZIP from GitHub instead (see step 2).

---

## 2. Get the project

**Option A — clone with Git** (open PowerShell or Command Prompt):

```powershell
git clone https://github.com/abutalha-0/4wdCar.git
cd 4wdCar
```

This repo is **private**, so Git will prompt for GitHub credentials — sign in with a browser when prompted, or use a personal access token if asked for a password.

**Option B — download as ZIP:** on the [repo page](https://github.com/abutalha-0/4wdCar), click **Code → Download ZIP**, then extract it.

---

## 3. Upload the Arduino sketch

Same as Linux:

1. Open [`CarControl/CarControl.ino`](CarControl/CarControl.ino) in the Arduino IDE.
2. Install the **LiquidCrystal I2C** library (Tools → Manage Libraries → search "LiquidCrystal I2C" by Frank de Brabander). `Servo`, `SoftwareSerial`, and `Wire` are bundled with the IDE.
3. Select **Tools → Board → Arduino Uno**.
4. Select the correct port under **Tools → Port** — this will be a `COM` port (e.g. `COM3`), shown while the Arduino is connected via USB.
5. Click **Upload**.

Make sure the car is powered per the [README's power requirements](README.md#power) — the Arduino needs its own supply, not the L298N's regulator, regardless of which OS you're driving from.

---

## 4. Pair the HC-05 and find its COM port

Windows doesn't use `rfcomm` — pairing a Bluetooth Serial Port Profile (SPP) device like the HC-05 automatically creates a **virtual COM port** for it.

1. Open **Settings → Bluetooth & devices → Add device → Bluetooth**.
2. Select **HC-05** from the list.
3. When prompted for a PIN, enter **`1234`** or **`0000`** (both are common HC-05 defaults — try one, then the other if it fails).
4. Once paired, go to **Settings → Bluetooth & devices → Devices → More Bluetooth settings** (or search "Bluetooth Settings" in the Start menu) → **COM Ports** tab.
5. Find the row for **HC-05** with direction **Outgoing** — note that port number, e.g. `COM5`. That's the one you need; ignore any "Incoming" row.

If you don't see a COM Ports tab, check **Device Manager → Ports (COM & LPT)** instead — the HC-05 should show up there as a "Standard Serial over Bluetooth link" after pairing.

---

## 5. Python environment

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
```

(If PowerShell blocks the activation script with an execution-policy error, either run `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass` first, or activate via Command Prompt instead with `.venv\Scripts\activate.bat`.)

Install dependencies:

```powershell
pip install pyserial keyboard
```

---

## 6. Point the script at your COM port

[`controller.py`](controller.py) hardcodes the port for Linux:

```python
PORT = "/dev/rfcomm0"
```

Open `controller.py` in a text editor and change that line to the COM port you found in step 4, for example:

```python
PORT = "COM5"
```

(If you want to try the GUI despite the Connect-button limitation above, `gui.py` has the same `PORT = "/dev/rfcomm0"` line near the top — same fix applies, but you'd still need another way to get `self.ser` opened since the Connect button's flow won't complete on Windows without `connect.sh`.)

---

## 7. Drive

```powershell
python controller.py
```

Hold `W`/`A`/`S`/`D` to drive, release to stop, `Q` to quit — same controls as Linux, see the [Controls section in README.md](README.md#controls) for the full command set (speed, servo/debug mode, autonomous mode, horn, RGB).

**If key presses aren't being detected:** the `keyboard` library sometimes needs elevated privileges on Windows to capture global key events. Try right-clicking PowerShell/Command Prompt and choosing **Run as administrator**, then re-run the command from there.

---

## Known limitations on Windows

- **GUI Connect button doesn't work** — see the note at the top. Use `controller.py`.
- **`connect.sh`, `gui.sh` won't run** — they're bash scripts. There is no Windows equivalent needed for the connect step (pairing handles it), but the scripts themselves won't execute without something like Git Bash or WSL.
- Everything else — the Arduino sketch, the serial protocol, `pyserial`'s COM port handling — is identical to Linux once `PORT` is set correctly.
