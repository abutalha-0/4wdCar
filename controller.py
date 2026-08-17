#!/usr/bin/env python3
"""Keyboard-driven controller for the 4WD car over serial (Bluetooth or USB).

Hold W/A/S/D to drive, release to stop. Q quits.
"""

import sys
import time

import serial
import keyboard

PORT = "/dev/rfcomm0"
BAUD = 9600

DRIVE_KEYS = {"w", "a", "s", "d"}


def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except serial.SerialException as e:
        print(f"Could not open {PORT}: {e}")
        print("Run ./connect.sh first.")
        sys.exit(1)

    time.sleep(2)  # let the Arduino reset after the serial connection opens
    print(f"Connected to {PORT} at {BAUD} baud.")
    print("W/A/S/D to drive (hold to move, release to stop), Q to quit.")

    pressed = set()

    def on_press(event):
        key = event.name
        if key in DRIVE_KEYS and key not in pressed:
            pressed.add(key)
            ser.write(key.encode())

    def on_release(event):
        key = event.name
        if key in DRIVE_KEYS and key in pressed:
            pressed.discard(key)
            ser.write(b" ")

    keyboard.on_press(on_press)
    keyboard.on_release(on_release)

    keyboard.wait("q")

    ser.write(b" ")
    ser.close()
    print("Disconnected.")


if __name__ == "__main__":
    main()
