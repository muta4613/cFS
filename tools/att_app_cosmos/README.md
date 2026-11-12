# ATT_APP COSMOS plugin

An [OpenC3 COSMOS](https://openc3.com) plugin that turns COSMOS into the ground station for ATT_APP: it sends ATT_APP's commands, decodes its HK and NAV telemetry and cFE event messages, and has a GNSS signal-health screen with red/yellow flags.

## What is in it

| Path | Contents |
|---|---|
| `plugin.txt` | One target (`ATT_APP`) and one UDP interface: commands to CI_LAB on 1234, telemetry from TO_LAB on 2234 |
| `targets/ATT_APP/cmd_tlm/att_app_cmds.txt` | `NOOP`, `RESET_COUNTERS`, `SET_CNO_THRESHOLD`, `RESTART_LOG`, `SEND_HK`, and TO_LAB's `TO_LAB_ENABLE_OUTPUT` |
| `targets/ATT_APP/cmd_tlm/att_app_tlm.txt` | `HK`, `NAV` (with the three health bits as separate items) and `EVENT` (cFE long event messages) |
| `targets/ATT_APP/lib/` | Write protocols that fill in the CCSDS length and the cFS command checksum |
| `targets/ATT_APP/screens/gnss.txt` | Health flags, position, housekeeping, the 12 satellite C/N0 slots, a mean C/N0 graph, and command buttons |
| `test/check_layout.py` | Checks every byte offset against the compiled C structs (see below) |

Colors on the screen come from item states:

| Item | Green | Yellow | Red |
|---|---|---|---|
| `BROADBAND_DROP` | clear | | set (possible interference) |
| `NO_FIX` | clear | | set |
| `LOW_CNO_SAT` | clear | set | |
| `FIX_QUALITY` | GPS, DGPS, RTK… | dead reckoning | no fix |
| `INPUT_STATE` | open | closed | |

## How it was verified

COSMOS decodes packets only from the offsets in the definition files, and a wrong offset shows wrong numbers with no error. `test/check_layout.py` prevents that without needing COSMOS:

1. It compiles a small C program with the flight build's own flags that prints `sizeof`, `offsetof`, message IDs, function codes and health-flag masks from the real headers.
2. It parses the COSMOS files the way COSMOS lays them out and compares packet sizes, all 55 field offsets and sizes, the stream IDs, function codes and health-bit positions.
3. It encodes all five commands from the COSMOS defaults with the plugin's length and checksum rules and compares the bytes with `gnss_monitor.py`, which a live cFS accepts.
4. With `--capture`, it decodes every captured HK and NAV packet with the COSMOS definitions and compares each field with `gnss_monitor.py`'s decoder.

```
python3 tools/att_app_cosmos/test/check_layout.py --capture capture.bin
PASS: 9 packets, 55 fields, 3 health bits, 5 commands byte-for-byte, 335 HK + 335 NAV captured packets decoded
```

It runs in CI on every push. On its first run against a capture it found that NAV shared message ID 0x088A with FM housekeeping (fixed: NAV is now 0x0893).

**Not verified yet:** the plugin has not been installed in a running COSMOS (Docker was not available where it was built). The definitions are checked; the screen layout, buttons and network setup below have not been seen working.

## Running it (WSL Ubuntu)

The simplest network setup runs Docker Engine and cFS in the same WSL distro, so COSMOS reaches cFS at the Docker bridge address `172.17.0.1` and cFS reaches COSMOS at `127.0.0.1`. Those are the plugin's defaults.

1. Install Docker Engine inside WSL Ubuntu (not Docker Desktop), following Docker's "Install Docker Engine on Ubuntu" page, then add yourself to the `docker` group.
2. Get COSMOS:
   ```
   git clone https://github.com/OpenC3/cosmos-project.git ~/cosmos-project
   cd ~/cosmos-project
   ```
3. Let TO_LAB's telemetry into the container: in `compose.yaml`, under the `openc3-operator` service, add
   ```
       ports:
         - "2234:2234/udp"
   ```
4. Start it with `./openc3.sh run` and open http://localhost:2900.
5. Build the plugin gem:
   ```
   cd ~/cFS/tools/att_app_cosmos
   ~/cosmos-project/openc3.sh cli rake build VERSION=1.0.0
   ```
6. In COSMOS, go to Admin → Plugins, install `openc3-cosmos-att-app-1.0.0.gem`, and accept or change the variables (`cfs_host`, `cfs_cmd_port`, `cfs_tlm_port`, `tlm_dest_ip`).
7. Start cFS (`cd ~/cFS/build-native_std/exe/cpu1 && ./core-cpu1`), open Telemetry Screens → ATT_APP → GNSS, and press **Enable telemetry**.

If no telemetry arrives: make sure `gnss_monitor.py` isn't running too, because both bind UDP 2234. Then check that `TO_LAB_ENABLE_OUTPUT` reached cFS (look for a TO_LAB event in the cFS console) and that port 2234 is published (`docker ps`).
