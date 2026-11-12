#!/usr/bin/env python3
"""
Ground monitor for ATT_APP: decodes its telemetry and sends its commands.

With cFS running (cd build-native_std/exe/cpu1 && ./core-cpu1):

  Watch telemetry (asks TO_LAB to start sending, then prints one line per second):
      python3 apps/att_app/ground/gnss_monitor.py --enable-tlm

  Send a command from a second terminal (the monitor keeps running in the first):
      python3 apps/att_app/ground/gnss_monitor.py --send-cmd noop
      python3 apps/att_app/ground/gnss_monitor.py --send-cmd reset-counters
      python3 apps/att_app/ground/gnss_monitor.py --send-cmd set-cno-threshold 35
      python3 apps/att_app/ground/gnss_monitor.py --send-cmd restart-log

Each NAV line shows: UTC time of the fix, fix quality, satellites used / in
view, mean C/N0, the health flags that are set, and per-satellite PRN:C/N0.
A housekeeping line is printed whenever a counter other than the wakeup and
epoch counts changes, and cFS event messages are printed as they arrive.

Only the Python standard library is used.
"""
import argparse
import socket
import struct
import sys
import time

# --- Network defaults (cFS lab apps on cpu1) ----------------------------------

CFS_HOST = "127.0.0.1"
CI_LAB_CMD_PORT = 1234  # CI_LAB listens here for commands
TO_LAB_TLM_PORT = 2234  # TO_LAB sends telemetry here once enabled

# --- Message IDs (native_std build: command base 0x1800, telemetry base 0x0800) -

ATT_APP_CMD_MID = 0x1887
ATT_APP_HK_TLM_MID = 0x0889
ATT_APP_NAV_TLM_MID = 0x088A
TO_LAB_CMD_MID = 0x1880
CFE_EVS_LONG_EVENT_MSG_MID = 0x0808

# --- Command function codes ---------------------------------------------------

ATT_APP_NOOP_CC = 0
ATT_APP_RESET_COUNTERS_CC = 1
ATT_APP_SET_CNO_THRESHOLD_CC = 2
ATT_APP_RESTART_LOG_CC = 3
TO_LAB_OUTPUT_ENABLE_CC = 6

# --- Packet layout --------------------------------------------------------------
#
# The CCSDS primary header is always big-endian. Everything after it is in the
# byte order of the flight processor, little-endian for a native x86/ARM build.
#
# Command:    6 byte primary header, 1 byte function code, 1 byte checksum, payload
# Telemetry:  6 byte primary header, 6 byte time stamp, 4 spare bytes, payload

CCSDS_PRI_HDR = struct.Struct(">HHH")  # stream ID, sequence, length
CCSDS_PRI_HDR_LEN = CCSDS_PRI_HDR.size
CCSDS_LENGTH_OFFSET = 7  # length field = total packet length - 7
CCSDS_SEQ_UNSEGMENTED = 0xC000
CMD_HDR_LEN = 8
CMD_FCN_CODE_OFFSET = 6
CMD_CHECKSUM_OFFSET = 7
CMD_CHECKSUM_INIT = 0xFF
TLM_HDR_LEN = 16

# ATT_APP_HkTlm_Payload_t
HK_PAYLOAD = struct.Struct("<BBBBIII")

# ATT_APP_NavTlm_Payload_t, fixed part; followed by MAX_SATS x {uint8 Prn, uint8 Cno}
NAV_FIXED = struct.Struct("<ddffIIBBBBBBBB")
NAV_MAX_SATS = 12
NAV_SAT = struct.Struct("<BB")
NAV_PAYLOAD_LEN = NAV_FIXED.size + NAV_MAX_SATS * NAV_SAT.size

# CFE_EVS_LongEventTlm_Payload_t: AppName[20], EventID, EventType, SpacecraftID, ProcessorID, Message[122]
EVS_PACKET_ID = struct.Struct("<20sHHII")
EVS_MESSAGE_LEN = 122
EVS_EVENT_TYPES = {1: "DEBUG", 2: "INFO", 3: "ERROR", 4: "CRITICAL"}

# SET_CNO_THRESHOLD payload: uint8 threshold + 3 padding bytes
SET_CNO_THRESHOLD_PAYLOAD = struct.Struct("<B3x")

# TO_LAB OUTPUT_ENABLE payload: char dest_IP[16]
TO_LAB_DEST_IP_LEN = 16

HEALTH_FLAGS = (
    (0x01, "NO_FIX"),
    (0x02, "LOW_CNO_SAT"),
    (0x04, "BROADBAND_DROP"),
)

INPUT_STATES = {0: "CLOSED", 1: "OPEN"}

RECV_BUF_SIZE = 4096
RECV_TIMEOUT_S = 0.5
CAPTURE_LEN = struct.Struct("<H")  # each captured packet is stored as length + bytes


def build_command(mid, fcn_code, payload=b""):
    """Return a complete cFS command packet with a valid checksum."""
    total_len = CMD_HDR_LEN + len(payload)
    pkt = bytearray(CCSDS_PRI_HDR.pack(mid, CCSDS_SEQ_UNSEGMENTED, total_len - CCSDS_LENGTH_OFFSET))
    pkt += bytes([fcn_code, 0]) + payload
    checksum = CMD_CHECKSUM_INIT
    for byte in pkt:
        checksum ^= byte
    pkt[CMD_CHECKSUM_OFFSET] = checksum
    return bytes(pkt)


def build_named_command(name, args, tlm_dest_ip):
    """Translate a --send-cmd name (plus arguments) into a command packet."""
    if name == "noop":
        return build_command(ATT_APP_CMD_MID, ATT_APP_NOOP_CC)
    if name == "reset-counters":
        return build_command(ATT_APP_CMD_MID, ATT_APP_RESET_COUNTERS_CC)
    if name == "restart-log":
        return build_command(ATT_APP_CMD_MID, ATT_APP_RESTART_LOG_CC)
    if name == "set-cno-threshold":
        if len(args) != 1:
            raise ValueError("set-cno-threshold needs one argument: the threshold in dB-Hz (0-255)")
        value = int(args[0], 0)
        if not 0 <= value <= 255:
            raise ValueError("threshold must fit in a uint8 (0-255); the app range-checks it further")
        return build_command(ATT_APP_CMD_MID, ATT_APP_SET_CNO_THRESHOLD_CC, SET_CNO_THRESHOLD_PAYLOAD.pack(value))
    if name == "bad-length":
        # A NOOP with one byte too many: the app must reject it and count a command error
        return build_command(ATT_APP_CMD_MID, ATT_APP_NOOP_CC, b"\x00")
    if name == "enable-tlm":
        dest = tlm_dest_ip.encode("ascii")[: TO_LAB_DEST_IP_LEN - 1].ljust(TO_LAB_DEST_IP_LEN, b"\x00")
        return build_command(TO_LAB_CMD_MID, TO_LAB_OUTPUT_ENABLE_CC, dest)
    raise ValueError(f"unknown command '{name}'")


COMMAND_NAMES = ("noop", "reset-counters", "set-cno-threshold", "restart-log", "bad-length", "enable-tlm")


def send_command(packet, host, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(packet, (host, port))
    finally:
        sock.close()


def decode_hk(data):
    """Decode an ATT_APP housekeeping packet into a dict, or None if too short."""
    if len(data) < TLM_HDR_LEN + HK_PAYLOAD.size:
        return None
    cmd, err, threshold, input_state, wakeups, epochs, parse_errors = HK_PAYLOAD.unpack_from(data, TLM_HDR_LEN)
    return {
        "CommandCounter": cmd,
        "CommandErrorCounter": err,
        "CnoThreshold": threshold,
        "InputState": input_state,
        "WakeupCounter": wakeups,
        "EpochCounter": epochs,
        "ParseErrorCounter": parse_errors,
    }


def decode_nav(data):
    """Decode an ATT_APP NAV packet into a dict, or None if too short."""
    if len(data) < TLM_HDR_LEN + NAV_PAYLOAD_LEN:
        return None
    (lat, lon, alt, mean_cno, epochs, parse_errors, hour, minute, second, centisec, fix, used, in_view, flags) = (
        NAV_FIXED.unpack_from(data, TLM_HDR_LEN)
    )
    sats = []
    offset = TLM_HDR_LEN + NAV_FIXED.size
    for idx in range(NAV_MAX_SATS):
        prn, cno = NAV_SAT.unpack_from(data, offset + idx * NAV_SAT.size)
        if prn != 0:
            sats.append((prn, cno))
    return {
        "Latitude": lat,
        "Longitude": lon,
        "Altitude": alt,
        "MeanCno": mean_cno,
        "EpochCounter": epochs,
        "ParseErrorCounter": parse_errors,
        "UtcHour": hour,
        "UtcMinute": minute,
        "UtcSecond": second,
        "UtcCentisecond": centisec,
        "FixQuality": fix,
        "SatsUsed": used,
        "SatsInView": in_view,
        "HealthFlags": flags,
        "Sats": sats,
    }


def decode_event(data):
    """Decode a cFE long event message into a dict, or None if too short."""
    if len(data) < TLM_HDR_LEN + EVS_PACKET_ID.size + EVS_MESSAGE_LEN:
        return None
    app, event_id, event_type, _scid, _cpu = EVS_PACKET_ID.unpack_from(data, TLM_HDR_LEN)
    start = TLM_HDR_LEN + EVS_PACKET_ID.size
    text = data[start : start + EVS_MESSAGE_LEN].split(b"\x00", 1)[0].decode("ascii", "replace")
    return {
        "App": app.split(b"\x00", 1)[0].decode("ascii", "replace"),
        "EventID": event_id,
        "EventType": EVS_EVENT_TYPES.get(event_type, str(event_type)),
        "Message": text,
    }


def flag_names(flags):
    names = [name for bit, name in HEALTH_FLAGS if flags & bit]
    return ",".join(names) if names else "none"


def format_nav(nav):
    sats = " ".join(f"{prn:02d}:{cno:02d}" if cno else f"{prn:02d}:--" for prn, cno in nav["Sats"])
    if nav["FixQuality"]:
        pos = f"{nav['Latitude']:9.5f} {nav['Longitude']:10.5f} {nav['Altitude']:6.1f}m"
    else:
        pos = "   (no position)             "
    return (
        f"NAV {nav['UtcHour']:02d}:{nav['UtcMinute']:02d}:{nav['UtcSecond']:02d} "
        f"fix={nav['FixQuality']} used={nav['SatsUsed']:2d} view={nav['SatsInView']:2d} {pos} "
        f"meanCN0={nav['MeanCno']:4.1f} flags={flag_names(nav['HealthFlags'])} | {sats}"
    )


def format_hk(hk):
    return (
        f"HK  cmd={hk['CommandCounter']} err={hk['CommandErrorCounter']} "
        f"threshold={hk['CnoThreshold']}dB-Hz input={INPUT_STATES.get(hk['InputState'], hk['InputState'])} "
        f"epochs={hk['EpochCounter']} parseErrors={hk['ParseErrorCounter']} wakeups={hk['WakeupCounter']}"
    )


def hk_key(hk):
    """The part of HK worth a new line when it changes (not the free-running counters)."""
    return (hk["CommandCounter"], hk["CommandErrorCounter"], hk["CnoThreshold"], hk["InputState"], hk["ParseErrorCounter"])


def monitor(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.tlm_port))
    sock.settimeout(RECV_TIMEOUT_S)

    if args.enable_tlm:
        send_command(build_named_command("enable-tlm", [], args.tlm_dest), args.host, args.cmd_port)

    print(f"Listening on UDP {args.bind}:{args.tlm_port} for ATT_APP telemetry. Ctrl+C to stop.", flush=True)

    capture = open(args.capture, "wb") if args.capture else None
    deadline = time.monotonic() + args.duration if args.duration else None
    last_hk_key = None

    try:
        while deadline is None or time.monotonic() < deadline:
            try:
                data, _ = sock.recvfrom(RECV_BUF_SIZE)
            except socket.timeout:
                continue
            if len(data) < CCSDS_PRI_HDR_LEN:
                continue
            mid = CCSDS_PRI_HDR.unpack_from(data)[0]

            if capture and mid in (ATT_APP_HK_TLM_MID, ATT_APP_NAV_TLM_MID):
                capture.write(CAPTURE_LEN.pack(len(data)) + data)

            if mid == ATT_APP_NAV_TLM_MID:
                nav = decode_nav(data)
                if nav:
                    print(format_nav(nav), flush=True)
            elif mid == ATT_APP_HK_TLM_MID:
                hk = decode_hk(data)
                if hk and (args.all_hk or hk_key(hk) != last_hk_key):
                    print(format_hk(hk), flush=True)
                    last_hk_key = hk_key(hk)
            elif mid == CFE_EVS_LONG_EVENT_MSG_MID:
                event = decode_event(data)
                if event and (args.all_events or event["App"] == "ATT_APP"):
                    print(f"EVT {event['App']} {event['EventID']} {event['EventType']}: {event['Message']}", flush=True)
    finally:
        if capture:
            capture.close()
        sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=CFS_HOST, help="address cFS (CI_LAB) is reachable at")
    parser.add_argument("--cmd-port", type=int, default=CI_LAB_CMD_PORT, help="CI_LAB command port")
    parser.add_argument("--tlm-port", type=int, default=TO_LAB_TLM_PORT, help="UDP port to listen on for telemetry")
    parser.add_argument("--bind", default="0.0.0.0", help="local address to listen on")
    parser.add_argument("--tlm-dest", default=CFS_HOST, help="address TO_LAB should send telemetry to (--enable-tlm)")
    parser.add_argument("--enable-tlm", action="store_true", help="send TO_LAB OUTPUT_ENABLE before listening")
    parser.add_argument(
        "--send-cmd",
        nargs="+",
        metavar=("NAME", "ARG"),
        help="send one command and exit: " + ", ".join(COMMAND_NAMES),
    )
    parser.add_argument("--duration", type=float, default=0, help="stop after this many seconds (default: run forever)")
    parser.add_argument("--capture", help="also save raw HK and NAV packets to this file (length-prefixed)")
    parser.add_argument("--all-events", action="store_true", help="print events from every app, not only ATT_APP")
    parser.add_argument("--all-hk", action="store_true", help="print every housekeeping packet, not only changes")
    args = parser.parse_args()

    if args.send_cmd:
        try:
            packet = build_named_command(args.send_cmd[0], args.send_cmd[1:], args.tlm_dest)
        except ValueError as err:
            parser.error(str(err))
        send_command(packet, args.host, args.cmd_port)
        print(f"sent {args.send_cmd[0]} ({len(packet)} bytes: {packet.hex()}) to {args.host}:{args.cmd_port}")
        return 0

    try:
        monitor(args)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
