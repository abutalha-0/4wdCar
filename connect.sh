#!/usr/bin/env bash
# Binds /dev/rfcomm0 to the car's HC-05 module. See README.md for the MAC address.
set -e

MAC="00:22:12:02:49:0D"
PORT="rfcomm0"

if [ -e "/dev/$PORT" ]; then
    echo "/dev/$PORT already bound."
    exit 0
fi

echo "Binding /dev/$PORT to $MAC..."
sudo rfcomm bind "$PORT" "$MAC"
echo "Connected: /dev/$PORT"
