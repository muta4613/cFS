#!/usr/bin/env python3
"""
Generate the scripted GNSS receiver log that ATT_APP replays.

Output is plain NMEA 0183 at 1 Hz: one $GPGGA and one group of $GPGSV
sentences per epoch, every sentence with a valid checksum and CRLF ending.
The receiver sits near Denver (39.74 N, 104.99 W, ~1600 m).

Scripted events (seconds from the start of the log):

    60 - 99    PRN 21 fades on its own (single weak satellite)
   150 - 179   every satellite falls 10-15 dB together (jamming signature)
   230 - 239   no position fix (GGA fix quality 0, most satellites untracked)

The generator is deterministic (fixed random seed), so the committed log
can be regenerated bit-for-bit:

    python3 apps/att_app/ground/make_nmea_log.py \
        --output apps/att_app/data/gnss_sample.nmea
"""
import argparse
import math
import os
import random

# --- Scenario constants ------------------------------------------------------

DURATION_S = 300
START_UTC_S = 18 * 3600  # 18:00:00 UTC

LAT_DEG = 39.74
LON_DEG = -104.99
ALT_M = 1600.0
GEOID_SEP_M = -18.0
POS_JITTER_M = 1.5
METERS_PER_DEG_LAT = 111_320.0

# (PRN, elevation deg, azimuth deg) at the start of the log
SATELLITES = [
    (2, 62, 45),
    (5, 48, 132),
    (7, 71, 301),
    (9, 23, 210),
    (13, 35, 88),
    (15, 15, 322),
    (18, 55, 175),
    (21, 41, 255),
    (26, 19, 60),
    (30, 30, 290),
]

CNO_MIN_DBHZ = 35.0  # at the horizon
CNO_MAX_DBHZ = 48.0  # at zenith
CNO_NOISE_DB = 0.7

WEAK_SAT_PRN = 21
WEAK_SAT_START_S = 60
WEAK_SAT_END_S = 100
WEAK_SAT_DROP_DB = 17.0
WEAK_SAT_RAMP_S = 4

JAM_START_S = 150
JAM_END_S = 180
JAM_DROP_MIN_DB = 10.0
JAM_DROP_MAX_DB = 15.0
JAM_RAMP_S = 2

NOFIX_START_S = 230
NOFIX_END_S = 240
NOFIX_TRACKED_PRNS = (2, 7, 18)  # the only satellites still tracked

MIN_TRACKABLE_CNO_DBHZ = 18
SATS_PER_GSV = 4


def checksum(body):
    """XOR of every character between '$' and '*'."""
    value = 0
    for ch in body:
        value ^= ord(ch)
    return value


def sentence(body):
    return f"${body}*{checksum(body):02X}\r\n"


def to_nmea_angle(deg, is_lat):
    """Decimal degrees -> (ddmm.mmmm or dddmm.mmmm, hemisphere)."""
    hemi = ("N" if deg >= 0 else "S") if is_lat else ("E" if deg >= 0 else "W")
    deg = abs(deg)
    whole = int(deg)
    minutes = (deg - whole) * 60.0
    width = 2 if is_lat else 3
    return f"{whole:0{width}d}{minutes:07.4f}", hemi


def ramp(t, start, end, ramp_s):
    """0..1 envelope: ramps up after start, holds, ramps down into end."""
    if t < start or t >= end:
        return 0.0
    up = min(1.0, (t - start + 1) / ramp_s)
    down = min(1.0, (end - t) / ramp_s)
    return min(up, down)


def nominal_cno(elev_deg):
    frac = math.sin(math.radians(max(elev_deg, 0)))
    return CNO_MIN_DBHZ + (CNO_MAX_DBHZ - CNO_MIN_DBHZ) * frac


def build_epoch(t, rng, jam_depth):
    """Return the list of sentences for second t."""
    utc = START_UTC_S + t
    hhmmss = f"{utc // 3600 % 24:02d}{utc // 60 % 60:02d}{utc % 60:02d}.00"
    no_fix = NOFIX_START_S <= t < NOFIX_END_S

    sats = []  # (prn, elev, azim, cno or None)
    for prn, elev0, azim0 in SATELLITES:
        elev = elev0 + (t / DURATION_S) * (2.0 if prn % 2 else -2.0)
        azim = (azim0 + (t / DURATION_S) * 3.0) % 360.0
        cno = nominal_cno(elev) + rng.gauss(0.0, CNO_NOISE_DB)
        if prn == WEAK_SAT_PRN:
            cno -= WEAK_SAT_DROP_DB * ramp(t, WEAK_SAT_START_S, WEAK_SAT_END_S, WEAK_SAT_RAMP_S)
        cno -= jam_depth[prn] * ramp(t, JAM_START_S, JAM_END_S, JAM_RAMP_S)
        cno_int = int(round(cno))
        if no_fix and prn not in NOFIX_TRACKED_PRNS:
            cno_int = None
        elif cno_int < MIN_TRACKABLE_CNO_DBHZ:
            cno_int = None
        sats.append((prn, int(round(elev)), int(round(azim)), cno_int))

    tracked = sum(1 for s in sats if s[3] is not None)

    if no_fix:
        gga = f"GPGGA,{hhmmss},,,,,0,{tracked:02d},,,M,,M,,"
    else:
        lat = LAT_DEG + rng.gauss(0.0, POS_JITTER_M) / METERS_PER_DEG_LAT
        lon = LON_DEG + rng.gauss(0.0, POS_JITTER_M) / (METERS_PER_DEG_LAT * math.cos(math.radians(LAT_DEG)))
        alt = ALT_M + rng.gauss(0.0, POS_JITTER_M)
        lat_s, ns = to_nmea_angle(lat, True)
        lon_s, ew = to_nmea_angle(lon, False)
        hdop = 0.9 if tracked >= 8 else 1.6
        gga = f"GPGGA,{hhmmss},{lat_s},{ns},{lon_s},{ew},1,{tracked:02d},{hdop:.1f},{alt:.1f},M,{GEOID_SEP_M:.1f},M,,"

    lines = [sentence(gga)]
    total = (len(sats) + SATS_PER_GSV - 1) // SATS_PER_GSV
    for idx in range(total):
        chunk = sats[idx * SATS_PER_GSV : (idx + 1) * SATS_PER_GSV]
        body = f"GPGSV,{total},{idx + 1},{len(sats):02d}"
        for prn, elev, azim, cno in chunk:
            cno_s = "" if cno is None else f"{cno:02d}"
            body += f",{prn:02d},{elev:02d},{azim:03d},{cno_s}"
        lines.append(sentence(body))
    return lines


def generate():
    rng = random.Random(4613)
    jam_depth = {prn: rng.uniform(JAM_DROP_MIN_DB, JAM_DROP_MAX_DB) for prn, _, _ in SATELLITES}
    out = []
    for t in range(DURATION_S):
        out.extend(build_epoch(t, rng, jam_depth))
    return "".join(out)


def main():
    default_out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "gnss_sample.nmea")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default=os.path.normpath(default_out), help="path of the log to write")
    args = parser.parse_args()

    text = generate()
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w", newline="") as fh:
        fh.write(text)
    print(f"wrote {args.output}: {text.count('$GPGGA')} epochs, {text.count(chr(10))} sentences, {len(text)} bytes")


if __name__ == "__main__":
    main()
