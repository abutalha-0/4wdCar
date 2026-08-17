#!/usr/bin/env bash
# Connects to the car's HC-05 module over Bluetooth. See README.md for the MAC address.
set -e

MAC="00:22:12:02:49:0D"
PORT="rfcomm0"
LOG="/tmp/${PORT}-connect.log"

if pgrep -f "rfcomm connect $PORT $MAC" > /dev/null; then
    echo "/dev/$PORT already connected."
    exit 0
fi

sudo rfcomm release "$PORT" 2>/dev/null || true

# Authenticate up front so the backgrounded connect below can use sudo -n
# (a fully detached background process has no terminal to prompt on).
sudo -v

# `rfcomm bind` only creates the device node, it doesn't establish an active
# link. `rfcomm connect` does the real connection but blocks in the
# foreground holding it open, so run it detached in the background.
echo "Connecting /dev/$PORT to $MAC..."
sudo -n rfcomm connect "$PORT" "$MAC" 1 > "$LOG" 2>&1 &
disown

for i in $(seq 1 15); do
    if [ -e "/dev/$PORT" ]; then
        echo "Connected: /dev/$PORT"
        exit 0
    fi
    if ! pgrep -f "rfcomm connect $PORT $MAC" > /dev/null; then
        break
    fi
    sleep 1
done

echo "Failed to connect. Check that the car is powered on and in range."
cat "$LOG" 2>/dev/null
exit 1
