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


class CarGUI:
    def __init__(self, root):
        self.ser = None
        self.root = root
        self.pressed = set()
        self.release_timers = {}
        root.title("4WD Car Control")

        top = tk.Frame(root)
        top.pack(padx=10, pady=(10, 0), fill="x")
        self.connect_button = tk.Button(top, text="Connect", command=self.on_connect_click)
        self.connect_button.pack(side="left")
        self.status_label = tk.Label(top, text="Not connected", fg="red")
        self.status_label.pack(side="left", padx=8)

        pad = tk.Frame(root)
        pad.pack(padx=10, pady=10)

        def make_button(text, cmd, row, col):
            b = tk.Button(pad, text=text, width=8, height=2, state="disabled")
            b.grid(row=row, column=col, padx=4, pady=4)
            b.bind("<ButtonPress-1>", lambda e: self.send(cmd, text))
            b.bind("<ButtonRelease-1>", lambda e: self.send(" ", "STOP"))
            self.drive_widgets.append(b)
            return b

        self.drive_widgets = []
        make_button("Forward", "w", 0, 1)
        make_button("Left", "a", 1, 0)
        make_button("Stop", " ", 1, 1)
        make_button("Right", "d", 1, 2)
        make_button("Backward", "s", 2, 1)

        tk.Label(root, text="Speed").pack(anchor="w", padx=10)
        self.speed_scale = tk.Scale(
            root, from_=0, to=255, orient="horizontal",
            command=self.on_speed_change, state="disabled",
        )
        self.speed_scale.set(200)
        self.speed_scale.pack(fill="x", padx=10)
        self.drive_widgets.append(self.speed_scale)

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
        for w in self.drive_widgets:
            w.configure(state="normal")
        self.log_message(f"Connected to {PORT}.")

    def on_key_press(self, event):
        key = event.keysym.lower()
        if key not in KEY_MAP:
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
