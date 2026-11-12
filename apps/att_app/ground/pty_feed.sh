#!/usr/bin/env bash
#
# Pretend to be a GNSS receiver on a serial port, for testing ATT_APP's device
# input mode without hardware.
#
# Creates the pseudo-terminal /tmp/gnss_pty and writes the recorded log into it
# one epoch (one GGA plus its GSV sentences) per second, looping forever, the
# way a 1 Hz receiver would.
#
# Usage, before starting cFS built with
# config/examples/device_att_app_internal_cfg_values.h:
#
#     apps/att_app/ground/pty_feed.sh [path/to/log.nmea]
#
# Needs socat (sudo apt install socat). Ctrl+C stops it and removes the pty.

set -euo pipefail

LOG="${1:-$(dirname "$0")/../data/gnss_sample.nmea}"
PTY=/tmp/gnss_pty

command -v socat >/dev/null || { echo "socat is not installed: sudo apt install socat" >&2; exit 1; }
[ -r "$LOG" ] || { echo "cannot read $LOG" >&2; exit 1; }

# socat links the pty's slave side to $PTY and copies its stdin into it.
# raw,echo=0 keeps the line discipline from altering the bytes.
emit_epochs() {
    while true; do
        first=1
        while IFS= read -r line; do
            case "$line" in
                \$G?GGA*)
                    # A new GGA starts the next epoch: wait one second first.
                    [ "$first" -eq 1 ] || sleep 1
                    first=0 ;;
            esac
            printf '%s\n' "$line"
        done < "$LOG"
        sleep 1
    done
}

echo "Feeding $LOG into $PTY at 1 epoch/s. Ctrl+C to stop."
emit_epochs | socat -u - "pty,raw,echo=0,link=$PTY"
