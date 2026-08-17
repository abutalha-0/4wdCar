#!/usr/bin/env bash
# Launches the button-based car control GUI. Run ./connect.sh first.
set -e

python3 "$(dirname "$0")/gui.py"
