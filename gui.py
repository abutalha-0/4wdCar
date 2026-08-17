#!/usr/bin/env python3
"""Simple button-based GUI to drive the car. Run ./connect.sh first."""

import sys
import time
import tkinter as tk
from datetime import datetime

import serial

PORT = "/dev/rfcomm0"
BAUD = 9600


class CarGUI:
    def __init__(self, root, ser):
        self.ser = ser
        root.title("4WD Car Control")

        pad = tk.Frame(root)
        pad.pack(padx=10, pady=10)

        def make_button(text, cmd, row, col):
            b = tk.Button(pad, text=text, width=8, height=2)
            b.grid(row=row, column=col, padx=4, pady=4)
            b.bind("<ButtonPress-1>", lambda e: self.send(cmd, text))
            b.bind("<ButtonRelease-1>", lambda e: self.send(" ", "STOP"))
            return b

        make_button("Forward", "w", 0, 1)
        make_button("Left", "a", 1, 0)
        make_button("Stop", " ", 1, 1)
        make_button("Right", "d", 1, 2)
        make_button("Backward", "s", 2, 1)

        tk.Label(root, text="Log").pack(anchor="w", padx=10)
        self.log = tk.Text(root, width=50, height=12, state="disabled")
        self.log.pack(padx=10, pady=(0, 10))

    def send(self, cmd, label):
        self.ser.write(cmd.encode())
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log.configure(state="normal")
        self.log.insert("end", f"[{ts}] sent {label!r} ({cmd!r})\n")
        self.log.see("end")
        self.log.configure(state="disabled")


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"Could not open {PORT}: {e}")
        print("Run ./connect.sh first.")
        sys.exit(1)

    time.sleep(2)  # let the Arduino reset after the serial connection opens

    root = tk.Tk()
    CarGUI(root, ser)
    root.mainloop()

    ser.write(b" ")
    ser.close()


if __name__ == "__main__":
    main()
