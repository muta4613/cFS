# ATT_APP GNSS project: session handoff

Updated 2026-11-12 at the end of session 2. It records what is done and what is left, measured against `FABLE_BRIEF.md`. Delete this file once the project is finished.

Branch: `feature/gnss` on `muta4613/cFS`, branched from `dev` at `3851bb6`. Everything below is pushed.

## Status against the brief

| # | Brief item | Status |
|---|---|---|
| 1 | NMEA log + generator | **Done** |
| 2 | Parser | **Done**, 100% line coverage |
| 3 | Reading the log (OSAL) | **Done**, 100% line coverage, run live |
| 4 | NAV packet, HK additions, `to_lab_sub.c` | **Done**, run live |
| 5 | Health checks | **Done**, 100% line coverage, run live against the whole log |
| 6 | Commands, sample leftovers removed | **Done**, run live. EDS files updated but **not built** (see below) |
| 7 | Unit tests | **Done** for every source file in the non-EDS build |
| 8 | Ground tool | **Done**: `ground/gnss_monitor.py` |
| 9 | Serial input | Code path written and unit tested; **never run against a device or pty**, no example config, not documented |
| 10 | COSMOS plugin | **Not started** |
| 11 | CI | **Not started** |
| 12 | README | **Not started** (still the sample_app text) |
| 13 | DESIGN_NOTES.md | **Not started** |

"Done means" from the brief: items 1 (clean build, tests pass) and 2 (long run with captured output, commands) are met for the log-file path. Items 3–7 (README excerpt, socat, COSMOS, CI, final push of everything) are open.

## Commits on the branch

1. `e3ffbc5` log + generator
2. `7c519f3` parser + tests
3. `4b2e979` WIP: GNSS reader, NAV packet, ground monitor
4. `1e92d3a` tests for the reader and the 1 Hz tick (completes 3)
5. `9738e61` health checks with edge-triggered events
6. `342c7b3` commands, sample_app cleanup, EDS files
7. this handoff update

**History was not rewritten.** Session 2 was asked to replace the WIP commit with a clean one. The tool environment refused the reset + force-push, so commit 4 was added on top instead. To squash 3 and 4 yourself:

```
git rebase -i 3851bb6      # mark 1e92d3a as "fixup" under 4b2e979, reword 4b2e979
git push --force-with-lease origin feature/gnss
```

## Test results (native_std, non-EDS)

| Test runner | Assertions | Source file | Lines | Branches |
|---|---|---|---|---|
| att_app | 22 | `att_app.c` | 100% | 100% |
| att_app_cmds | 100 | `att_app_cmds.c` | 100% | 100% |
| att_app_dispatch | 64 | `att_app_dispatch.c` | 100% | 100% |
| att_app_gnss | 308 | `att_app_gnss.c` | 100% | 100% |
| att_app_health | 999 | `att_app_health.c` | 100% | 96.6% |
| att_app_nmea | 249 | `att_app_nmea.c` | 100% | 99.6% |

1,742 assertions, 0 failures. Build has 0 warnings.

- The untaken branches in `att_app_health.c` are defensive: a missed-epoch counter passing 255, and "no table entry available", which cannot happen while the baseline table is as large as an epoch.
- `att_app_eds_dispatch.c` is only compiled in the EDS build and has no coverage numbers.
- Standalone parser check (`make -C apps/att_app/unit-test/standalone check`): passes under ASan/UBSan, 300 epochs, 76,800 corrupted inputs.
- Coverage was measured with gcov directly on the `coverage-att_app-*-object.dir` objects; `lcov --list` gave nonsense percentages with lcov 2.0 here.

## What exists now

### Flight code (`apps/att_app/fsw/src`)
- `att_app.c`: entry point, main loop, init. Registers an EVS filter so parse-error events stop after the first 8.
- `att_app_dispatch.c`: routes by MID and command code, checks length. Unknown command codes and wrong lengths increment `CommandErrorCounter`.
- `att_app_cmds.c`: NOOP (cc 0), RESET_COUNTERS (cc 1), SET_CNO_THRESHOLD (cc 2), RESTART_LOG (cc 3), the HK request and the 50 Hz wakeup. Every 50th wakeup calls `ATT_APP_GNSS_Tick`.
- `att_app_gnss.c`: opens the input, reads with `OS_TimedRead` (0 ms timeout), splits lines, feeds the assembler, runs the health checks, sends events on flag changes and publishes NAV.
- `att_app_nmea.c`: pure parser and epoch assembler.
- `att_app_health.c`: pure health checks.

### Health logic as built
- One debounce helper (`ATT_APP_HEALTH_Debounce`) for all three flags.
- NO_FIX: fix quality 0. Sets after 1 epoch, clears after 2.
- LOW_CNO_SAT: weakest tracked satellite below the threshold. Clears at threshold + 2 dB.
- BROADBAND_DROP:
  - Sets when the mean is at least 8 dB below its running average **and** at least 75% of satellites with a trusted per-PRN baseline are 6 dB below it or untracked, for 2 epochs.
  - Stays set while the mean is more than 4 dB below baseline; clears after 3 epochs within 4 dB.
  - All baselines are frozen while the set condition is true or the flag is active.
  - A baseline is trusted after 10 epochs; the averaging window is 30 epochs.
- RESTART_LOG clears the health history and keeps the commanded threshold.
- All tuning values are in `fsw/inc/att_app_internal_cfg.h`.

**Known limits of the health logic** (worth stating in DESIGN_NOTES):
- It detects a change, not an absolute level. A receiver switched on under interference learns the low level as normal.
- A drop that starts in the first 10 epochs is not judged.
- A very slow fade (slower than the 30-epoch average) is followed and never flagged.
- If the signal never recovers, BROADBAND_DROP stays set until RESTART_LOG.

### Packets
- HK, MID `0x0889`, 16-byte payload: CommandCounter, CommandErrorCounter, CnoThreshold, InputState (uint8 each); WakeupCounter, EpochCounter, ParseErrorCounter (uint32 each).
- NAV, MID `0x088A`, 64-byte payload: Latitude, Longitude (double); Altitude, MeanCno (float); EpochCounter, ParseErrorCounter (uint32); UtcHour, UtcMinute, UtcSecond, UtcCentisecond, FixQuality, SatsUsed, SatsInView, HealthFlags (uint8); 12 × {Prn, Cno}.
- HealthFlags bits: `0x01` NO_FIX, `0x02` LOW_CNO_SAT, `0x04` BROADBAND_DROP.
- Telemetry header is 16 bytes (6 primary, 6 time, 4 spare). Command header is 8 bytes (6 primary, function code, checksum). Payloads are little-endian on a native build.
- SET_CNO_THRESHOLD payload: uint8 threshold + 3 padding bytes (12-byte packet). Valid range 10–55.

### Events
| ID | Meaning |
|---|---|
| 1–7, 9–11, 13 | From the sample_app base (init, command errors, pipe and subscription errors). 8 and 12 are retired |
| 14–19 | GNSS input: opened, open error, read error, log wrapped, parse error (filtered), device map error |
| 20 / 21 | Fix lost / regained |
| 22 / 23 | Low C/N0 set / cleared |
| 24 / 25 | Possible interference / cleared |
| 26 / 27 | Threshold accepted / rejected |
| 28 | RESTART_LOG |

### Ground
- `ground/gnss_monitor.py`: decodes NAV, HK and cFE long events; `--enable-tlm`, `--capture FILE`, `--duration N`, `--send-cmd noop|reset-counters|set-cno-threshold N|restart-log|bad-length`.
- Capture file format: repeated `<uint16 little-endian length><packet bytes>`, HK and NAV only. This is the input intended for the COSMOS offset test.
- `ground/check_hk.py`: the original script; still works for the first three counters.

## Live run (session 2)

`core-cpu1` ran for about 5.5 minutes with `gnss_monitor.py --enable-tlm --capture`. Result (times are the log's UTC, which starts at 18:00:00):

| Event | Sets | Clears | Notes |
|---|---|---|---|
| LOW_CNO_SAT (PRN 21 alone) | 18:01:03 | 18:01:38 | Threshold had been commanded to 31, so it clears at 33 dB-Hz |
| BROADBAND_DROP | 18:02:32 | 18:03:02 | Event text: "mean C/N0 30.8 dB-Hz is 11.7 dB below baseline, 10 of 10 satellites dropped". LOW_CNO_SAT is also set from 18:02:31 to 18:02:59, correctly: jammed satellites are below the threshold |
| NO_FIX | 18:03:50 | 18:04:01 | 11 NAV packets with NO_FIX, none with BROADBAND_DROP |
| Log wrap | 300 epochs | | Wrap event sent, replay continued |

- Exactly one event per set and one per clear (events 20–25).
- Commands sent during the run:
  - NOOP: CommandCounter 0 → 1.
  - `set-cno-threshold 31`: CommandCounter 1 → 2, HK threshold 30 → 31.
  - `set-cno-threshold 200`: rejected, CommandErrorCounter 0 → 1.
  - `bad-length` (9-byte NOOP): CommandErrorCounter 1 → 2.
  - Unknown command code 9: CommandErrorCounter 2 → 3.
- 330 NAV packets in 330 s, 0 parse errors.
- RESTART_LOG and RESET_COUNTERS were not sent in the live run; they are covered by unit tests only.

The capture and monitor output were left in the session's `/tmp` and are **not** in the repo. Re-create them for the README with:

```
python3 apps/att_app/ground/gnss_monitor.py --enable-tlm --duration 330 --capture /tmp/capture.bin | tee /tmp/monitor.log
```

## Not verified

- **EDS build.** `eds/att_app.xml`, `att_app_eds_dispatch.c` and `config/eds_att_app_interface_cfg_values.h` were rewritten to match the C definitions but never compiled.
  - `sample_defs/eds/cfe-topicids.xml` and `config.xml` contain no ATT_APP entries, so the EDS configuration could not have built this app before either.
  - To build it, the mission EDS needs `ATT_APP_CMD_TOPICID`, `ATT_APP_SEND_HK_TOPICID`, `ATT_APP_WAKEUP_TOPICID`, `ATT_APP_HK_TLM_TOPICID`, `ATT_APP_NAV_TLM_TOPICID` and a `<Package name="ATT_APP"><Define name="MAX_SATS" value="12"/>`. Those files were outside what the brief allows changing.
  - The macro name `EdsParam_ATT_APP_MAX_SATS` is a guess by analogy with the topic ID macros.
- **Serial/device mode.** Unit tested with a scripted read handler only.
- **`make native_std.runtest`** (the whole mission's tests) was started and stopped at the first failure, 68 of 197 tests in: OSAL's `network-api-test` cannot open an IPv6 socket in the cloud sandbox. That test is not ATT_APP's and no ATT_APP file is involved, but it means the full suite was not seen passing here; the six ATT_APP runners were run directly instead. Run it in WSL to confirm.
- **CI.** No workflow run exists for this branch.

## Things found that differ from the brief

- **Build commands.** Use `make native_std.prep && make native_std.install`; tests with `make native_std.runtest`; run from `build-native_std/exe/cpu1`. The single-line `make SIMULATION=native ... prep && make && make install && make test` does not work with this repo's root Makefile, and `make -f simple.mk` fails because `MISSIONCONFIG` is ambiguous.
- **Submodules.** Clone with `--recurse-submodules`; the build needs all of them.
- **Message queue limit.** If `core-cpu1` aborts with `OS_QueueCreate Error`, run `echo 1000 | sudo tee /proc/sys/fs/mqueue/msg_max`. The value resets on reboot. CI will probably need it.
- **Packages used:** `lcov`, `socat`, `jq`, `clang-format` 18. For the EDS prep step: `liblua5.4-dev`, `libexpat1-dev`, `lua5.4`.
- **Stubs** are generated, not hand-written: `perl osal/ut_assert/scripts/generate_stubs.pl apps/att_app/unit-test/stubs apps/att_app/fsw/src/<header>.h`. Re-run it after changing a public prototype.
- **File headers.** New files carry a plain Apache-2.0 header rather than the NASA docket header.

## Remaining work, with what is already known

### Serial input (brief item 9)
- `ATT_APP_PLATFORM_GNSS_INPUT_MODE` selects the mode. In device mode the app maps `GNSS_DEVICE_HOST_DIR` (`/dev`) to the OSAL mount `/gnssdev` with `OS_FileSysAddFixedMap` and opens `GNSS_DEVICE_NAME` (`ttyUSB0`).
- Still needed:
  - an example override header. The platform values come from `att_app_internal_cfg_values.h`; the build looks for an override before falling back to `apps/att_app/config/default_att_app_internal_cfg_values.h`. Where exactly it looks (`sample_defs/` is the likely place) has not been confirmed; `make native_std.prep` prints which file it used.
  - an override must define `ATT_APP_PLATFORM_CFGVAL(x)` for every item, not only the changed ones.
  - the socat pty test: `socat -d -d pty,raw,echo=0,link=/tmp/gnss_pty -`, with host dir `/tmp` and device name `gnss_pty`, feeding the log at 1 Hz.
  - a note that the baud rate must be set with `stty` because OSAL has no termios API.
- In device mode an epoch is published when the *next* GGA arrives, so NAV lags the receiver by up to one epoch.
- A device that stops sending produces no NAV packets and no event. There is no "input stale" flag.

### COSMOS (brief item 10)
- `tools/cfs-cosmos-plugin` (NASA's submodule) already has a CFS target, a UDP interface on 1234/2234, CI_LAB/TO_LAB definitions including `TO_LAB_CMD_ENABLE_OUTPUT`, and sample_app definitions to copy the format from. It cannot be pushed to, so the plan is a standalone plugin in `tools/att_app_cosmos/`.
- The Docker daemon does not run in the cloud session, so the fallback is the Python offset test against a `gnss_monitor.py --capture` file.
- Limits wanted: red on BROADBAND_DROP and NO_FIX, yellow on LOW_CNO_SAT.

### CI (brief item 11)
- The workflows under `apps/att_app/.github/workflows` never run (GitHub only reads the repo-root `.github/workflows`) and point at `nasa/cFS` reusable workflows. Plan: delete them and add one root workflow for `feature/gnss` and `dev` that builds native_std, runs the att_app test runners and `make -C apps/att_app/unit-test/standalone check`.
- The repo root already carries NASA's workflows, which may also trigger on push.

### README and DESIGN_NOTES (brief items 12, 13)
- README needs a trimmed live run; the lines quoted in the session 2 final message are a starting point.
- Design decisions made so far, each with the rejected alternative:
  1. Epoch released on the next GGA, not on the last GSV (multi-constellation receivers send several GSV groups).
  2. Baselines frozen during a drop, not always adapting (an adapting baseline cancels the alarm).
  3. BROADBAND_DROP needs both the mean test and the per-satellite share test, not the mean alone (lost sky versus interference).
  4. Parser and health are pure functions with caller-owned state, not code that calls cFE directly (testable alone, fuzzable under ASan).
  5. Non-blocking `OS_TimedRead` on the 1 Hz tick, not a blocking `OS_read` or a child task (one task must keep answering commands; no shared state to lock).
  6. NAV payload ordered largest alignment first with compile-time size asserts, not packed structs.
  7. Parse-error events limited by an EVS filter while the counter counts all of them.
  8. Input mode is an argument to `ATT_APP_GNSS_Init`, so both modes are tested in one build.

## Suggested order for the next session

1. Serial example config and socat test.
2. COSMOS plugin and offset test.
3. CI workflow; check the Actions run.
4. README (with a fresh captured run) and DESIGN_NOTES.
5. Delete this file.
