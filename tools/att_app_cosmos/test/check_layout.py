#!/usr/bin/env python3
# ATT_APP - GNSS signal-health monitor for NASA cFS
# Licensed under the Apache License, Version 2.0
"""
Check the COSMOS command and telemetry definitions against the flight code.

COSMOS decodes packets purely from the byte offsets in cmd_tlm/*.txt. If one
of those drifts from the C structs, COSMOS shows wrong numbers without any
error. This test catches that, without needing COSMOS or Docker:

  1. Compile a small C program against the real ATT_APP, TO_LAB and cFE
     headers (same flags as the flight build) that prints sizeof, offsetof,
     message IDs, function codes and health-flag masks.
  2. Parse the COSMOS definitions the way COSMOS does (APPEND_* items are laid
     out back to back; ITEM has an explicit bit offset) and compare.
  3. Build every command from its COSMOS defaults, apply the same length and
     checksum rules as the plugin's write protocols, and compare the bytes
     with what ground/gnss_monitor.py sends (which the live runs proved cFS
     accepts).
  4. With --capture FILE (from gnss_monitor.py --capture), decode every
     captured HK and NAV packet with the COSMOS definitions and compare every
     field with gnss_monitor.py's decoder.

Usage, after `make native_std.prep && make native_std.install`:
    python3 tools/att_app_cosmos/test/check_layout.py [--build-dir build-native_std] [--capture cap.bin]

Exit status 0 means every check passed.
"""
import argparse
import json
import math
import os
import shlex
import struct
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PLUGIN = os.path.join(REPO, "tools", "att_app_cosmos")
CMD_TLM_DIR = os.path.join(PLUGIN, "targets", "ATT_APP", "cmd_tlm")
GROUND_DIR = os.path.join(REPO, "apps", "att_app", "ground")

# COSMOS packet -> C type
PACKETS = {
    ("TLM", "HK"): "ATT_APP_HkTlm_t",
    ("TLM", "NAV"): "ATT_APP_NavTlm_t",
    ("TLM", "EVENT"): "CFE_EVS_LongEventTlm_t",
    ("CMD", "NOOP"): "ATT_APP_NoopCmd_t",
    ("CMD", "RESET_COUNTERS"): "ATT_APP_ResetCountersCmd_t",
    ("CMD", "SET_CNO_THRESHOLD"): "ATT_APP_SetCnoThresholdCmd_t",
    ("CMD", "RESTART_LOG"): "ATT_APP_RestartLogCmd_t",
    ("CMD", "SEND_HK"): "ATT_APP_SendHkCmd_t",
    ("CMD", "TO_LAB_ENABLE_OUTPUT"): "TO_LAB_EnableOutputCmd_t",
}

# COSMOS packet -> (C expression for its MID, C expression for its function code or None)
IDS = {
    ("TLM", "HK"): ("ATT_APP_HK_TLM_MID", None),
    ("TLM", "NAV"): ("ATT_APP_NAV_TLM_MID", None),
    ("TLM", "EVENT"): ("CFE_EVS_LONG_EVENT_MSG_MID", None),
    ("CMD", "NOOP"): ("ATT_APP_CMD_MID", "ATT_APP_NOOP_CC"),
    ("CMD", "RESET_COUNTERS"): ("ATT_APP_CMD_MID", "ATT_APP_RESET_COUNTERS_CC"),
    ("CMD", "SET_CNO_THRESHOLD"): ("ATT_APP_CMD_MID", "ATT_APP_SET_CNO_THRESHOLD_CC"),
    ("CMD", "RESTART_LOG"): ("ATT_APP_CMD_MID", "ATT_APP_RESTART_LOG_CC"),
    ("CMD", "SEND_HK"): ("ATT_APP_SEND_HK_MID", None),
    ("CMD", "TO_LAB_ENABLE_OUTPUT"): ("TO_LAB_CMD_MID", "TO_LAB_OUTPUT_ENABLE_CC"),
}

# COSMOS item -> C member (relative to the packet struct). Header items are
# checked as a block: the first payload item's offset must equal offsetof(Payload).
FIELDS = {
    "HK": {
        "COMMAND_COUNTER": "Payload.CommandCounter",
        "COMMAND_ERROR_COUNTER": "Payload.CommandErrorCounter",
        "CNO_THRESHOLD": "Payload.CnoThreshold",
        "INPUT_STATE": "Payload.InputState",
        "WAKEUP_COUNTER": "Payload.WakeupCounter",
        "EPOCH_COUNTER": "Payload.EpochCounter",
        "PARSE_ERROR_COUNTER": "Payload.ParseErrorCounter",
    },
    "NAV": {
        "LATITUDE": "Payload.Latitude",
        "LONGITUDE": "Payload.Longitude",
        "ALTITUDE": "Payload.Altitude",
        "MEAN_CNO": "Payload.MeanCno",
        "EPOCH_COUNTER": "Payload.EpochCounter",
        "PARSE_ERROR_COUNTER": "Payload.ParseErrorCounter",
        "UTC_HOUR": "Payload.UtcHour",
        "UTC_MINUTE": "Payload.UtcMinute",
        "UTC_SECOND": "Payload.UtcSecond",
        "UTC_CENTISECOND": "Payload.UtcCentisecond",
        "FIX_QUALITY": "Payload.FixQuality",
        "SATS_USED": "Payload.SatsUsed",
        "SATS_IN_VIEW": "Payload.SatsInView",
        "HEALTH_FLAGS": "Payload.HealthFlags",
        **{f"SAT{i + 1:02d}_PRN": f"Payload.Sats[{i}].Prn" for i in range(12)},
        **{f"SAT{i + 1:02d}_CNO": f"Payload.Sats[{i}].Cno" for i in range(12)},
    },
    "EVENT": {
        "APP_NAME": "Payload.PacketID.AppName",
        "EVENT_ID": "Payload.PacketID.EventID",
        "EVENT_TYPE": "Payload.PacketID.EventType",
        "SPACECRAFT_ID": "Payload.PacketID.SpacecraftID",
        "PROCESSOR_ID": "Payload.PacketID.ProcessorID",
        "MESSAGE": "Payload.Message",
        "PADDING": "Payload.Padding",
    },
    "SET_CNO_THRESHOLD": {
        "CNO_THRESHOLD": "Payload.CnoThreshold",
        "PADDING": "Payload.Padding",
    },
    "TO_LAB_ENABLE_OUTPUT": {
        "DEST_IP": "Payload.dest_IP",
    },
}

HEALTH_BITS = {
    "NO_FIX": "ATT_APP_HEALTH_FLAG_NO_FIX",
    "LOW_CNO_SAT": "ATT_APP_HEALTH_FLAG_LOW_CNO_SAT",
    "BROADBAND_DROP": "ATT_APP_HEALTH_FLAG_BROADBAND_DROP",
}

C_HEADERS = ["cfe.h", "att_app_msg.h", "att_app_msgids.h", "att_app_fcncodes.h", "att_app_msgdefs.h",
             "to_lab_msg.h", "to_lab_msgids.h", "to_lab_fcncodes.h", "cfe_evs_msg.h", "cfe_evs_msgids.h"]

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)


# --- 1. Facts from the compiled headers ------------------------------------------


def compile_flags(build_dir):
    """Reuse the exact flags the flight build used for att_app.c, plus TO_LAB's includes."""
    with open(os.path.join(build_dir, "native", "default_cpu1", "compile_commands.json")) as f:
        db = json.load(f)

    def flight_entry(src, target):
        # the flight object, not the unit-test copy of the same source file
        return next(e for e in db if e["file"].endswith(src)
                    and f"CMakeFiles/{target}.dir/" in (e.get("output", "") + e.get("command", "")))

    att = flight_entry("apps/att_app/fsw/src/att_app.c", "att_app")
    to_lab = flight_entry("apps/to_lab/fsw/src/to_lab_app.c", "to_lab")
    flags = []
    for entry in (att, to_lab):
        args = entry.get("arguments") or shlex.split(entry["command"])
        flags += [a for a in args if a.startswith(("-I", "-D", "-std", "-m"))]
    return list(dict.fromkeys(flags))


def c_facts(build_dir):
    prog = ["#include <stdio.h>", "#include <stddef.h>"] + [f'#include "{h}"' for h in C_HEADERS]
    prog.append("int main(void) {")
    for (_, pkt), ctype in PACKETS.items():
        prog.append(f'printf("SIZE {pkt} %zu\\n", sizeof({ctype}));')
        prog.append(f'printf("PAYLOAD {pkt} %zu\\n", sizeof({ctype}) > offsetof({ctype}, Payload) ? '
                    f'offsetof({ctype}, Payload) : 0);' if pkt in FIELDS else f'printf("PAYLOAD {pkt} %zu\\n", sizeof({ctype}));')
        for item, member in FIELDS.get(pkt, {}).items():
            prog.append(f'printf("FIELD {pkt} {item} %zu %zu\\n", offsetof({ctype}, {member}), '
                        f'sizeof((({ctype} *)0)->{member}));')
    for (_, pkt), (mid, cc) in IDS.items():
        prog.append(f'printf("MID {pkt} %u\\n", (unsigned)({mid}));')
        if cc:
            prog.append(f'printf("CC {pkt} %u\\n", (unsigned)({cc}));')
    for item, mask in HEALTH_BITS.items():
        prog.append(f'printf("BIT {item} %u\\n", (unsigned)({mask}));')
    prog.append("return 0; }")

    with tempfile.TemporaryDirectory() as tmp:
        src, exe = os.path.join(tmp, "layout.c"), os.path.join(tmp, "layout")
        with open(src, "w") as f:
            f.write("\n".join(prog) + "\n")
        subprocess.run(["cc", *compile_flags(build_dir), src, "-o", exe], check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout

    facts = {"SIZE": {}, "PAYLOAD": {}, "FIELD": {}, "MID": {}, "CC": {}, "BIT": {}}
    for line in out.splitlines():
        kind, *rest = line.split()
        if kind == "FIELD":
            facts["FIELD"][(rest[0], rest[1])] = (int(rest[2]), int(rest[3]))
        else:
            facts[kind][rest[0]] = int(rest[1])
    return facts


# --- 2. Parse the COSMOS definitions -----------------------------------------------


def tokens(line):
    lex = shlex.shlex(line, posix=True)
    lex.whitespace_split = True
    lex.commenters = ""
    return list(lex)


def parse_cosmos(variables):
    """Return {(kind, name): {"endian": ..., "items": [dict], "bits": total}}."""
    packets, cur = {}, None
    for fname in sorted(os.listdir(CMD_TLM_DIR)):
        with open(os.path.join(CMD_TLM_DIR, fname)) as f:
            for raw in f:
                line = raw.strip()
                for var, val in variables.items():
                    line = line.replace(f"<%= {var} %>", val)
                if not line or line.startswith("#"):
                    continue
                t = tokens(line)
                kw = t[0]
                if kw in ("COMMAND", "TELEMETRY"):
                    kind = "CMD" if kw == "COMMAND" else "TLM"
                    cur = {"endian": t[3], "items": [], "bits": 0}
                    packets[(kind, t[2])] = cur
                elif kw in ("APPEND_ITEM", "APPEND_ID_ITEM", "APPEND_PARAMETER", "APPEND_ID_PARAMETER", "ITEM"):
                    explicit = kw == "ITEM"
                    name = t[1]
                    if explicit:
                        offset, size, dtype, rest = int(t[2]), int(t[3]), t[4], t[5:]
                    else:
                        offset, size, dtype, rest = cur["bits"], int(t[2]), t[3], t[4:]
                        cur["bits"] += size
                    item = {"name": name, "offset": offset, "size": size, "type": dtype,
                            "id": "_ID_" in kw, "endian": cur["endian"]}
                    if rest and rest[-1] in ("BIG_ENDIAN", "LITTLE_ENDIAN"):
                        item["endian"] = rest[-1]
                    if kw.endswith("PARAMETER"):
                        # PARAMETER name size type [min max] default "desc"
                        item["default"] = rest[0] if dtype == "STRING" else rest[2]
                    elif kw == "APPEND_ID_ITEM":
                        item["default"] = rest[0]
                    cur["items"].append(item)
    return packets


def as_int(text):
    return int(text, 0)


# --- 3. Encode and decode the way COSMOS would ----------------------------------------


def encode(pkt, values=None):
    """Build a command from its COSMOS definition, then apply the plugin's write protocols."""
    values = values or {}
    buf = bytearray(pkt["bits"] // 8)
    for it in pkt["items"]:
        val = values.get(it["name"], it.get("default"))
        nbytes, start = it["size"] // 8, it["offset"] // 8
        if it["type"] == "STRING":
            raw = val.encode("ascii")[:nbytes]
            buf[start:start + len(raw)] = raw
        else:
            order = "big" if it["endian"] == "BIG_ENDIAN" else "little"
            buf[start:start + nbytes] = as_int(str(val)).to_bytes(nbytes, order)
    # AttAppCcsdsLength, then AttAppCcsdsChecksum
    struct.pack_into(">H", buf, 4, len(buf) - 7)
    buf[7] = 0
    checksum = 0xFF
    for b in buf:
        checksum ^= b
    buf[7] = checksum
    return bytes(buf)


def decode(pkt, data):
    out = {}
    for it in pkt["items"]:
        start, size = it["offset"], it["size"]
        if size % 8 == 0 and start % 8 == 0:
            raw = data[start // 8:(start + size) // 8]
            if it["type"] == "STRING":
                out[it["name"]] = raw.split(b"\x00", 1)[0].decode("ascii", "replace")
            elif it["type"] == "FLOAT":
                fmt = ("<" if it["endian"] == "LITTLE_ENDIAN" else ">") + ("d" if size == 64 else "f")
                out[it["name"]] = struct.unpack(fmt, raw)[0]
            else:
                out[it["name"]] = int.from_bytes(raw, "big" if it["endian"] == "BIG_ENDIAN" else "little")
        else:
            # sub-byte item: offsets count from the MSB of the packet
            byte = data[start // 8]
            out[it["name"]] = (byte >> (7 - (start % 8) - (size - 1))) & ((1 << size) - 1)
    return out


# --- The checks --------------------------------------------------------------------------------


def check_layout(cosmos, facts):
    for key, ctype in PACKETS.items():
        kind, name = key
        pkt = cosmos.get(key)
        check(pkt is not None, f"{kind} {name}: missing from the COSMOS definitions")
        if pkt is None:
            continue
        check(pkt["bits"] == facts["SIZE"][name] * 8,
              f"{name}: COSMOS packet is {pkt['bits'] // 8} bytes, sizeof({ctype}) is {facts['SIZE'][name]}")
        appended = [it for it in pkt["items"] if not (it["name"] in HEALTH_BITS)]
        payload_items = [it for it in appended if it["name"] in FIELDS.get(name, {})]
        if payload_items:
            first = min(it["offset"] for it in payload_items) // 8
            check(first == facts["PAYLOAD"][name],
                  f"{name}: COSMOS payload starts at byte {first}, offsetof(Payload) is {facts['PAYLOAD'][name]}")
        for it in payload_items:
            off, size = facts["FIELD"][(name, it["name"])]
            check(it["offset"] == off * 8 and it["size"] == size * 8,
                  f"{name}.{it['name']}: COSMOS byte {it['offset'] / 8:g} size {it['size'] / 8:g}, C byte {off} size {size}")
        mapped = set(FIELDS.get(name, {}))
        for it in pkt["items"]:
            if it["offset"] >= facts["PAYLOAD"].get(name, 0) * 8 and it["name"] not in mapped \
                    and it["name"] not in HEALTH_BITS and name in FIELDS:
                failures.append(f"{name}.{it['name']}: payload item has no C member mapped in check_layout.py")

        mid_item = next(it for it in pkt["items"] if it["name"] == "CCSDS_STREAMID")
        check(as_int(mid_item["default"]) == facts["MID"][name],
              f"{name}: COSMOS stream ID {mid_item['default']}, C MID 0x{facts['MID'][name]:04X}")
        if kind == "CMD" and name in facts["CC"]:
            fc_item = next(it for it in pkt["items"] if it["name"] == "CCSDS_FC")
            check(as_int(fc_item["default"]) == facts["CC"][name],
                  f"{name}: COSMOS function code {fc_item['default']}, C {facts['CC'][name]}")

    nav = cosmos[("TLM", "NAV")]
    flags_byte = next(it for it in nav["items"] if it["name"] == "HEALTH_FLAGS")["offset"] // 8
    for item, _ in HEALTH_BITS.items():
        mask = facts["BIT"][item]
        bit = int(math.log2(mask))
        want = flags_byte * 8 + 7 - bit
        got = next(it for it in nav["items"] if it["name"] == item)
        check(got["offset"] == want and got["size"] == 1,
              f"NAV.{item}: COSMOS bit {got['offset']}, mask 0x{mask:02X} is bit {want}")


def check_commands(cosmos, tlm_dest_ip):
    sys.path.insert(0, GROUND_DIR)
    import gnss_monitor as gm  # noqa: E402  (ground tool, proven against live cFS)

    cases = [
        ("NOOP", {}, ("noop", [])),
        ("RESET_COUNTERS", {}, ("reset-counters", [])),
        ("RESTART_LOG", {}, ("restart-log", [])),
        ("SET_CNO_THRESHOLD", {"CNO_THRESHOLD": "31"}, ("set-cno-threshold", ["31"])),
        ("TO_LAB_ENABLE_OUTPUT", {}, ("enable-tlm", [])),
    ]
    for name, values, (gm_name, gm_args) in cases:
        ours = encode(cosmos[("CMD", name)], values)
        theirs = gm.build_named_command(gm_name, gm_args, tlm_dest_ip)
        check(ours == theirs, f"command {name}: COSMOS bytes {ours.hex()} != gnss_monitor {theirs.hex()}")
    return gm


def check_capture(cosmos, gm, facts, path):
    with open(path, "rb") as f:
        blob = f.read()
    pos, counts = 0, {"HK": 0, "NAV": 0}
    while pos + 2 <= len(blob):
        (n,) = gm.CAPTURE_LEN.unpack_from(blob, pos)
        data = blob[pos + 2:pos + 2 + n]
        pos += 2 + n
        mid = struct.unpack(">H", data[:2])[0]
        name = {facts["MID"]["HK"]: "HK", facts["MID"]["NAV"]: "NAV"}.get(mid)
        if not name:
            continue
        counts[name] += 1
        pkt = cosmos[("TLM", name)]
        if len(data) != pkt["bits"] // 8:
            # e.g. another app publishing on the same message ID
            failures.append(f"captured {name} #{counts[name]}: {len(data)} bytes, COSMOS expects {pkt['bits'] // 8}")
            continue
        ours = decode(pkt, data)
        ref = gm.decode_hk(data) if name == "HK" else gm.decode_nav(data)
        for item, member in FIELDS[name].items():
            if item[:3] == "SAT" and item[3:5].isdigit():
                idx, part = int(item[3:5]) - 1, item[6:]
                sats = ref["Sats"]
                want = (sats[idx][0 if part == "PRN" else 1] if idx < len(sats) else 0)
            else:
                want = ref[member.split(".")[1]]
            got = ours[item]
            same = math.isclose(got, want, rel_tol=0, abs_tol=1e-9) if isinstance(want, float) else got == want
            check(same, f"captured {name} #{counts[name]} {item}: COSMOS {got}, gnss_monitor {want}")
        if name == "NAV":
            for item, mask_name in HEALTH_BITS.items():
                bit = int(math.log2({"NO_FIX": 1, "LOW_CNO_SAT": 2, "BROADBAND_DROP": 4}[item]))
                check(ours[item] == (ref["HealthFlags"] >> bit) & 1,
                      f"captured NAV #{counts[name]} {item}: COSMOS {ours[item]}, flags 0x{ref['HealthFlags']:02X}")
    check(counts["HK"] > 0 and counts["NAV"] > 0, f"capture {path} has no HK or NAV packets")
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default=os.path.join(REPO, "build-native_std"))
    ap.add_argument("--capture", help="capture file from gnss_monitor.py --capture")
    ap.add_argument("--tlm-dest-ip", default="127.0.0.1", help="tlm_dest_ip plugin variable")
    args = ap.parse_args()

    facts = c_facts(os.path.abspath(args.build_dir))
    cosmos = parse_cosmos({"tlm_dest_ip": args.tlm_dest_ip})
    check_layout(cosmos, facts)
    gm = check_commands(cosmos, args.tlm_dest_ip)
    summary = (f"{len(PACKETS)} packets, {sum(len(v) for v in FIELDS.values())} fields, "
               f"{len(HEALTH_BITS)} health bits, 5 commands byte-for-byte")
    if args.capture:
        counts = check_capture(cosmos, gm, facts, args.capture)
        summary += f", {counts['HK']} HK + {counts['NAV']} NAV captured packets decoded"

    if failures:
        print(f"FAIL: {len(failures)} problem(s)")
        for msg in failures:
            print("  -", msg)
        return 1
    print(f"PASS: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
