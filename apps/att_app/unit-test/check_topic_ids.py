#!/usr/bin/env python3
# ATT_APP - GNSS signal-health monitor for NASA cFS
# Licensed under the Apache License, Version 2.0
"""
Fail if any ATT_APP topic ID is also used by another app in the mission.

cFS turns a topic ID into a message ID (0x18xx for commands, 0x08xx for
telemetry). Two apps with the same topic ID end up publishing on the same
message ID, and nothing in the build catches it: subscribers simply receive
both packets. ATT_APP's NAV packet originally used 0x8A, the same as FM
housekeeping, and the only symptom was a 28-byte "NAV" packet every 15 s.

This scans every DEFAULT_*_TOPICID in apps/, cfe/ and libs/ and is
deliberately conservative: a number ATT_APP uses must not appear anywhere
else, command or telemetry.

Usage: python3 apps/att_app/unit-test/check_topic_ids.py
"""
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PATTERN = re.compile(r"#define\s+DEFAULT_(\w+?)_TOPICID\s+(0x[0-9A-Fa-f]+|\d+)")


def scan():
    found = {}
    for top in ("apps", "cfe", "libs"):
        for root, dirs, files in os.walk(os.path.join(REPO, top)):
            dirs[:] = [d for d in dirs if not d.startswith((".", "build"))]
            for name in files:
                if not name.endswith(".h"):
                    continue
                path = os.path.join(root, name)
                with open(path, errors="replace") as f:
                    for m in PATTERN.finditer(f.read()):
                        found.setdefault(int(m.group(2), 0), []).append((m.group(1), os.path.relpath(path, REPO)))
    return found


def main():
    found = scan()
    ours = {tid: uses for tid, uses in found.items() if any(p.startswith("apps/att_app/") for _, p in uses)}
    problems = []
    for tid, uses in sorted(ours.items()):
        others = [(n, p) for n, p in uses if not p.startswith("apps/att_app/")]
        for name, path in others:
            mine = ", ".join(n for n, p in uses if p.startswith("apps/att_app/"))
            problems.append(f"topic 0x{tid:02X}: ATT_APP {mine} collides with {name} ({path})")
    if problems:
        print("FAIL")
        for p in problems:
            print("  -", p)
        return 1
    print(f"PASS: {len(ours)} ATT_APP topic IDs, none shared with the {len(found) - len(ours)} other IDs in the mission")
    return 0


if __name__ == "__main__":
    sys.exit(main())
