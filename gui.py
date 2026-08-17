#!/usr/bin/env python3
"""Simple button-based GUI to drive the car."""

import os
import subprocess
import threading
import tkinter as tk
from datetime import datetime

import serial

PORT = "/dev/rfcomm0"
BAUD = 9600
CONNECT_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "connect.sh")

# Maps both WASD and arrow keysyms to the same drive command/label.
KEY_MAP = {
    "w": ("w", "Forward"), "up": ("w", "Forward"),
    "a": ("a", "Left"), "left": ("a", "Left"),
    "s": ("s", "Backward"), "down": ("s", "Backward"),
    "d": ("d", "Right"), "right": ("d", "Right"),
}

MODE_COMMANDS = {"car": b"c", "servo": b"g", "auto": b"o"}


class CarGUI:
    def __init__(self, root):
        self.ser = None
        self.root = root
        self.pressed = set()
        self.release_timers = {}
        self.mode = "car"  # "car", "servo", or "auto"
        self.connected = False
        self.drive_widgets = []  # gated by connection state only
        self.manual_widgets = []  # also gated by mode != "auto" (Autonomous drives itself)
        root.title("4WD Car Control")

        top = tk.Frame(root)
        top.pack(padx=10, pady=(10, 0), fill="x")
        self.connect_button = tk.Button(top, text="Connect", command=self.on_connect_click)
        self.connect_button.pack(side="left")
        self.status_label = tk.Label(top, text="Not connected", fg="red")
        self.status_label.pack(side="left", padx=8)
        mode_toggle = tk.Frame(top, bd=1, relief="solid")
        mode_toggle.pack(side="left", padx=8)
        self.mode_toggles = {}
        for mode, label in (("car", "Car"), ("servo", "Servo"), ("auto", "Auto")):
            b = tk.Button(
                mode_toggle, text=label, width=5, state="disabled", bd=0,
                command=lambda m=mode: self.set_mode(m),
            )
            b.pack(side="left")
            self.mode_toggles[mode] = b
            self.drive_widgets.append(b)
        self.default_button_bg = self.mode_toggles["car"].cget("bg")
        self.update_mode_toggle()
        self.distance_label = tk.Label(top, text="Distance: -- cm")
        self.distance_label.pack(side="left", padx=8)
        self.servo_label = tk.Label(top, text="Servo: -- deg")
        self.servo_label.pack(side="left", padx=8)

        pad = tk.Frame(root)
        pad.pack(padx=10, pady=10)

        def make_button(text, cmd, row, col):
            b = tk.Button(pad, text=text, width=8, height=2, state="disabled")
            b.grid(row=row, column=col, padx=4, pady=4)
            b.bind("<ButtonPress-1>", lambda e: self.send(cmd, text))
            b.bind("<ButtonRelease-1>", lambda e: self.send(" ", "STOP"))
            self.manual_widgets.append(b)
            return b

        make_button("Forward", "w", 0, 1)
        make_button("Left", "a", 1, 0)
        make_button("Stop", " ", 1, 1)
        make_button("Right", "d", 1, 2)
        make_button("Backward", "s", 2, 1)

        extras = tk.Frame(root)
        extras.pack(padx=10, fill="x")

        # Horn and RGB work in every mode (Car/Servo/Auto), unlike the drive
        # pad above, so these go in drive_widgets (connection-gated only) not
        # manual_widgets (which also disables under Auto mode).
        self.horn_button = tk.Button(extras, text="Horn", width=8, height=2, state="disabled")
        self.horn_button.pack(side="left")
        self.horn_button.bind("<ButtonPress-1>", lambda e: self.send("h", "Horn ON"))
        self.horn_button.bind("<ButtonRelease-1>", lambda e: self.send("j", "Horn OFF"))
        self.drive_widgets.append(self.horn_button)

        colors = [
            ("Off", "1", "#dddddd"),
            ("Red", "2", "#ff4d4d"),
            ("Green", "3", "#4dff4d"),
            ("Blue", "4", "#4d4dff"),
            ("Yellow", "5", "#ffff4d"),
            ("Cyan", "6", "#4dffff"),
            ("Magenta", "7", "#ff4dff"),
            ("White", "8", "#ffffff"),
        ]
        for label, digit, color in colors:
            b = tk.Button(
                extras, text=label, width=4, bg=color, state="disabled",
                command=lambda d=digit, l=label: self.send(d, f"RGB {l}"),
            )
            b.pack(side="left", padx=2)
            self.drive_widgets.append(b)

        tk.Label(root, text="Speed").pack(anchor="w", padx=10)
        self.speed_scale = tk.Scale(
            root, from_=0, to=255, orient="horizontal",
            command=self.on_speed_change, state="disabled",
        )
        self.speed_scale.set(200)
        self.speed_scale.pack(fill="x", padx=10)
        self.manual_widgets.append(self.speed_scale)

        tk.Label(root, text="Log").pack(anchor="w", padx=10)
        self.log = tk.Text(root, width=50, height=12, state="disabled")
        self.log.pack(padx=10, pady=(0, 10))

        root.bind("<KeyPress>", self.on_key_press)
        root.bind("<KeyRelease>", self.on_key_release)
        root.focus_set()
        root.protocol("WM_DELETE_WINDOW", self.on_close)

    def on_connect_click(self):
        self.connect_button.configure(state="disabled")
        self.status_label.configure(text="Connecting...", fg="orange")
        self.log_message("Running connect.sh (check terminal if it asks for a sudo password)...")
        threading.Thread(target=self.run_connect, daemon=True).start()

    def run_connect(self):
        result = subprocess.run([CONNECT_SCRIPT])
        self.root.after(0, self.finish_connect, result.returncode)

    def finish_connect(self, returncode):
        if returncode != 0:
            self.status_label.configure(text="Connect failed", fg="red")
            self.connect_button.configure(state="normal")
            self.log_message("connect.sh failed. Check that the car is powered on and in range.")
            return

        try:
            self.ser = serial.Serial(PORT, BAUD, timeout=1)
        except serial.SerialException as e:
            self.status_label.configure(text="Connect failed", fg="red")
            self.connect_button.configure(state="normal")
            self.log_message(f"Could not open {PORT}: {e}")
            return

        self.status_label.configure(text="Connected", fg="green")
        self.connect_button.configure(text="Reconnect", state="normal")
        self.connected = True
        for w in self.drive_widgets:
            w.configure(state="normal")
        self.update_manual_widgets_state()
        self.log_message(f"Connected to {PORT}.")

        threading.Thread(target=self.read_loop, daemon=True).start()

    def read_loop(self):
        while True:
            try:
                line = self.ser.readline()
            except (serial.SerialException, OSError):
                return
            if not line:
                continue
            text = line.decode(errors="ignore").strip()
            if text:
                self.root.after(0, self.handle_serial_line, text)

    def handle_serial_line(self, text):
        if text.startswith("D:"):
            try:
                cm = int(text[2:])
            except ValueError:
                return
            self.distance_label.configure(text="Distance: out of range" if cm < 0 else f"Distance: {cm} cm")
        elif text.startswith("A:"):
            try:
                angle = int(text[2:])
            except ValueError:
                return
            self.servo_label.configure(text=f"Servo: {angle} deg")
        else:
            self.log_message(f"received {text!r}")

    def on_key_press(self, event):
        key = event.keysym.lower()
        if key not in KEY_MAP or self.mode == "auto":
            return
        timer = self.release_timers.pop(key, None)
        if timer is not None:
            self.root.after_cancel(timer)
            return  # auto-repeat while held, not a new press
        if key not in self.pressed:
            self.pressed.add(key)
            cmd, label = KEY_MAP[key]
            self.send(cmd, label)

    def on_key_release(self, event):
        key = event.keysym.lower()
        if key not in KEY_MAP:
            return

        def do_release():
            self.release_timers.pop(key, None)
            if key in self.pressed:
                self.pressed.discard(key)
                self.send(" ", "STOP")

        self.release_timers[key] = self.root.after(50, do_release)

    def set_mode(self, mode):
        if self.ser is None or mode == self.mode:
            return
        self.mode = mode
        # Sent 3 times: the Arduino requires a mode-switch byte to arrive
        # 3 times in a row within half a second before it takes effect, so
        # noise-corrupted bytes on the Bluetooth link can't accidentally
        # flip modes mid-drive.
        for _ in range(3):
            self.ser.write(MODE_COMMANDS[mode])
        messages = {
            "car": "Switched back to Car mode.",
            "servo": "Switched to Servo debug mode. W=center, A=pan left, D=pan right.",
            "auto": "Switched to Autonomous mode. Drives itself; manual controls disabled until Car is selected.",
        }
        self.log_message(messages[mode])
        self.update_mode_toggle()
        self.update_manual_widgets_state()

    def update_mode_toggle(self):
        active_bg = "#3a7bd5"
        active_fg = "white"
        for mode, button in self.mode_toggles.items():
            if mode == self.mode:
                button.configure(bg=active_bg, fg=active_fg, relief="sunken")
            else:
                button.configure(bg=self.default_button_bg, fg="black", relief="raised")

    def update_manual_widgets_state(self):
        # Manual drive controls only make sense when connected AND not in
        # Autonomous mode (the Arduino ignores them there anyway, but
        # disabling avoids the confusing "I pressed forward and nothing
        # happened" experience).
        state = "normal" if (self.connected and self.mode != "auto") else "disabled"
        for w in self.manual_widgets:
            w.configure(state=state)

    def on_speed_change(self, value):
        if self.ser is None:
            return
        speed = int(value)
        self.ser.write(b"V")
        self.ser.write(bytes([speed]))
        self.log_message(f"sent Speed={speed} ('V' + byte {speed})")

    def send(self, cmd, label):
        if self.ser is None:
            return
        self.ser.write(cmd.encode())
        self.log_message(f"sent {label!r} ({cmd!r})")

    def log_message(self, text):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log.configure(state="normal")
        self.log.insert("end", f"[{ts}] {text}\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def on_close(self):
        if self.ser is not None:
            self.ser.write(b" ")
            self.ser.close()
        self.root.destroy()


def main():
    root = tk.Tk()
    CarGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
