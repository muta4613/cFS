#!/usr/bin/env python3
"""
Listen for ATT_APP housekeeping telemetry from TO_LAB and print it.

Usage (with cFS already running):
  1. Enable TO_LAB telemetry output (once per cFS run):
       cd ~/cFS/tools/cFS-GroundSystem/Subsystems/cmdUtil
       ./cmdUtil --pktid=0x1880 --cmdcode=6 --string="16:127.0.0.1"
  2. Run this script:
       python3 ~/cFS/apps/att_app/ground/check_hk.py

Each line shows the counters from one housekeeping packet (sent once per
second). WakeupCounter should climb by ~50 between lines if the 50 Hz
scheduler wakeup is arriving.
"""
import socket
import struct

TLM_PORT = 2234          # TO_LAB default telemetry port for cpu1
ATT_HK_TLM_MID = 0x0889  # 0x0800 telemetry base + topic 0x89

# Packet layout (native_std build, little-endian payload):
#   bytes 0-5   CCSDS primary header (big-endian, MID in bytes 0-1)
#   bytes 6-11  time stamp
#   bytes 12-15 padding
#   byte  16    CommandCounter
#   byte  17    CommandErrorCounter
#   bytes 18-19 Spare
#   bytes 20-23 WakeupCounter (uint32)


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", TLM_PORT))
    print(f"Listening on UDP {TLM_PORT} for ATT_APP HK (MID 0x{ATT_HK_TLM_MID:04X}). Ctrl+C to stop.")
    last = None
    while True:
        data, _ = sock.recvfrom(4096)
        if len(data) < 24 or struct.unpack(">H", data[0:2])[0] != ATT_HK_TLM_MID:
            continue
        cmd, err = data[16], data[17]
        wakeups = struct.unpack("<I", data[20:24])[0]
        delta = "" if last is None else f"  (+{wakeups - last} since last packet)"
        print(f"CommandCounter={cmd}  CommandErrorCounter={err}  WakeupCounter={wakeups}{delta}")
        last = wakeups


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
