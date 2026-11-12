# ATT_APP GNSS project: session handoff

Written 2026-11-12, when the first working session was stopped early. It records what is done, what is half done, and what has not been started, measured against `FABLE_BRIEF.md`. Delete this file once the project is finished.

Branch: `feature/gnss` on `muta4613/cFS`, branched from `dev` at `3851bb6`. Everything below is pushed.

## Status against the brief

| # | Brief item | Status |
|---|---|---|
| 1 | NMEA log + generator | **Done**, committed |
| 2 | Parser | **Done**, committed, 100% line coverage |
| 3 | Reading the log (OSAL) | **Code written and run live; no unit tests yet** (WIP commit) |
| 4 | NAV packet, HK additions, `to_lab_sub.c` | **Code written and run live; no unit tests yet** (WIP commit) |
| 5 | Health checks | **Not started** (design below) |
| 6 | Commands, remove sample leftovers, EDS | **Not started** |
| 7 | Unit tests | Parser done. GNSS reader, health, commands outstanding |
| 8 | Ground tool | `gnss_monitor.py` written and run live (WIP commit). The two new commands it sends do not exist in the app yet |
| 9 | Serial input | Reader is written for both modes; **device mode never run**, no socat test, no example config |
| 10 | COSMOS plugin | **Not started** |
| 11 | CI | **Not started** |
| 12 | README | **Not started** (still the sample_app text) |
| 13 | DESIGN_NOTES.md | **Not started** |

None of the "Done means" items is complete. No 6-minute run was made, and no GitHub Actions run was checked.

## Commits on the branch

1. `e3ffbc5` log + generator
2. `7c519f3` parser + tests
3. WIP commit: GNSS reader, NAV packet, ground monitor (see "Half done")

## What is done

### Log and generator
- `ground/make_nmea_log.py` is deterministic (fixed seed) and writes `data/gnss_sample.nmea`: 300 epochs at 1 Hz, 1200 sentences, CRLF endings, valid checksums, 10 satellites.
- Scripted events, in seconds from the start:
  - 60–99: PRN 21 fades about 17 dB by itself (to roughly 27 dB-Hz).
  - 150–179: every satellite drops 10–15 dB.
  - 230–239: no fix. GGA quality is 0 with empty position fields, and 7 of 10 satellites have an empty C/N0 field.
- `data/.gitattributes` stops git rewriting the CRLF endings.
- `CMakeLists.txt` installs the log to each target's `cf/`, so the app opens `/cf/gnss_sample.nmea`.

### Parser (`fsw/src/att_app_nmea.c/.h`)
- Pure functions: no cFE or OSAL calls, no globals. Inputs are (pointer, length) and are never assumed NUL-terminated.
- Public functions: `ATT_APP_NMEA_VerifyChecksum`, `GetSentenceType`, `ParseGga`, `ParseGsv`, and the epoch assembler (`AssemblerInit`, `AssemblerFeed`, `AssemblerFlush`).
- An epoch is released when the **next GGA** arrives (or on flush at end of file). This was chosen over "release on the last GSV" because multi-constellation receivers send several GSV groups per epoch.
- GGA with fix quality 0 may have empty position fields. GSV may have empty elevation, azimuth and C/N0; an empty C/N0 is stored as 0, meaning "in view, not tracked".
- Number parsing is hand-written and length-bounded (no `strtok`, `sscanf` or `atof`).
- `ATT_APP_MISSION_MAX_SATS` (12) was added to `fsw/inc/att_app_interface_cfg.h`.
- Tests:
  - `unit-test/coveragetest/coveragetest_att_app_nmea.c`: 249 assertions pass; 100% lines, 99.6% branches.
  - `unit-test/standalone/`: `make check` builds the parser alone under ASan/UBSan, replays the log (300 epochs, 10 without fix, 0 rejected) and feeds 76,800 corrupted sentences through every entry point. Passes. Needs only gcc and the source tree.
- `unit-test/stubs/att_app_nmea_stubs.c` was generated with `osal/ut_assert/scripts/generate_stubs.pl`.

## Half done (in the WIP commit)

It builds with no warnings and the pre-existing tests pass. It was run live once for about 12 seconds: NAV decoded correctly at 1 Hz in `gnss_monitor.py` (position, 10 satellites, mean C/N0 about 43).

- `fsw/src/att_app_gnss.c/.h`
  - `ATT_APP_GNSS_Init`, `ATT_APP_GNSS_Restart`, `ATT_APP_GNSS_Tick`.
  - Uses `OS_OpenCreate`, `OS_TimedRead` with a 0 ms timeout, `OS_lseek` and `OS_close`.
  - `OS_TimedRead` was used instead of `OS_read` so one call serves both a file and a serial device without blocking. On POSIX OSAL every file is opened non-blocking and goes through `select`, so for a regular file the two behave the same.
  - File mode publishes one epoch per tick and wraps at end of file. Device mode drains whatever has arrived.
  - A missing file raises an error event and the app keeps running. An empty file closes the input instead of wrapping every second.
  - A line longer than the 256-byte buffer is discarded and counted once.
  - `HealthFlags` is always 0 for now.
- NAV packet
  - Topic 0x8A, MID `0x088A`.
  - `ATT_APP_NavTlm_Payload_t` is 64 bytes with no implicit padding, checked by compile-time asserts.
  - Field order: Latitude, Longitude (double); Altitude, MeanCno (float); EpochCounter, ParseErrorCounter (uint32); UtcHour, UtcMinute, UtcSecond, UtcCentisecond, FixQuality, SatsUsed, SatsInView, HealthFlags (uint8); then 12 × {Prn, Cno}.
- HK payload is now 16 bytes: CommandCounter, CommandErrorCounter, CnoThreshold, InputState, WakeupCounter, EpochCounter, ParseErrorCounter. `CnoThreshold` is not filled in yet.
- `ATT_APP_WakeupCmd` calls the tick every 50th wakeup (`ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH`).
- `att_app.c` registers an EVS filter so parse-error events stop after the first 8.
- New event IDs 14–19 and new platform config items are in `att_app_eventids.h` and `att_app_internal_cfg.h`.
- `sample_defs/tables/to_lab_sub.c` subscribes to `ATT_APP_NAV_TLM_MID`.
- `ground/gnss_monitor.py`
  - Decodes NAV, HK and cFE long events.
  - `--enable-tlm`, `--send-cmd`, `--duration`, `--capture`.
  - It already knows `set-cno-threshold` (cc 2) and `restart-log` (cc 3), but the app still has PROCESS and DISPLAY_PARAM on those codes.

**Known gaps in the WIP commit**
- `coveragetest_att_app_gnss.c` is an empty placeholder, so `att_app_gnss.c` has 0% coverage.
- `coveragetest_att_app_cmds.c` does not cover the new tick branch (cmds is at 96% lines).
- `check_hk.py` still documents the old HK layout; the first 8 payload bytes it reads are unchanged in position, so it still works.
- `eds/att_app.xml` was already stale on `dev` (no WAKEUP, no WakeupCounter) and is further behind now.

## Not started, with the design that was planned

### Health checks
Planned as `fsw/src/att_app_health.c/.h`, pure like the parser. It returns flags plus raised/cleared bits and the caller sends the events.
- One debounce helper for all three flags (set after N consecutive epochs, clear after M).
- NO_FIX: fix quality 0.
- LOW_CNO_SAT: any tracked satellite below the threshold; clears when all are at or above threshold + 2 dB.
- BROADBAND_DROP:
  - Mean C/N0 at least 8 dB below a slow running average, **and** at least 75% of satellites that have a per-PRN baseline are at least 6 dB below it. Untracked or missing satellites count as dropped.
  - Set after 2 epochs. Clear when the mean is within 4 dB of baseline for 3 epochs.
  - **Freeze the baselines while the condition is true**, otherwise the average follows the jammer down.
- The no-fix window in the log is built so that it must *not* trip BROADBAND_DROP: 70% of satellites are missing but the mean of the 3 that remain is unchanged.
- Bit definitions already exist in `default_att_app_msgdefs.h` (`ATT_APP_HEALTH_FLAG_*`).

### Commands
- Remove PROCESS, DISPLAY_PARAM, the example table (`fsw/tables/`, the `tbl` config headers, `att_app_utils.c` with its test and stub), the sample_lib dependency, and `STRING_VAL_LEN`.
- Add SET_CNO_THRESHOLD (cc 2, payload `uint8` + 3 padding bytes, range-checked) and RESTART_LOG (cc 3).
- Count a command error on an invalid command code as well.
- Update `eds/att_app.xml` and `att_app_eds_dispatch.c`.

### Serial input
- The reader takes `ATT_APP_PLATFORM_GNSS_INPUT_MODE`. In device mode it maps `GNSS_DEVICE_HOST_DIR` (`/dev`) to the OSAL mount `/gnssdev` with `OS_FileSysAddFixedMap` and opens `GNSS_DEVICE_NAME`.
- Still needed:
  - an example override header, to be copied to `sample_defs/att_app_internal_cfg_values.h` (where the build looks for overrides has not been confirmed);
  - the socat pty test;
  - a note that the baud rate must be set with `stty` because OSAL has no termios API.

### COSMOS
- `tools/cfs-cosmos-plugin` (NASA's submodule) already has a CFS target, a UDP interface on 1234/2234, CI_LAB/TO_LAB definitions including `TO_LAB_CMD_ENABLE_OUTPUT`, and sample_app definitions to copy the format from.
- It cannot be pushed to, so the plan was a standalone plugin in `tools/att_app_cosmos/`.
- The Docker daemon does not run in the cloud session (the client is installed, there is no daemon), so the fallback is the Python offset test against packets captured with `gnss_monitor.py --capture`.

### CI, README, DESIGN_NOTES
- The workflows under `apps/att_app/.github/workflows` never run (GitHub only reads the repo-root `.github/workflows`) and they point at `nasa/cFS` reusable workflows. Plan: delete them and add one root workflow for `feature/gnss` and `dev`.

## Things found that differ from the brief

- **Build commands.**
  - `make SIMULATION=native ENABLE_UNIT_TESTS=true prep && make && make install && make test` does not work in this repo. The root `Makefile` is the multi-config one, and that line fails at `native_eds.prep`.
  - What works, and what the repo README documents:
    ```
    make native_std.prep
    make native_std.install
    make native_std.runtest
    cd build-native_std/exe/cpu1 && ./core-cpu1
    ```
  - The output directory is `build-native_std/`, not `build/`.
  - `make -f simple.mk` fails too: `MISSIONCONFIG` is ambiguous because both `sample_defs` and `simple_defs` exist.
- **Submodules.** The build needs all of them (`targets.cmake` lists cf, hs, md, cs, fm, lc, sc, ds, hk, sbn), so clone with `--recurse-submodules`.
- **Message queue limit.**
  - `core-cpu1` aborted at startup with `OS_QueueCreate Error. errno = 22` until this was run:
    ```
    echo 1000 | sudo tee /proc/sys/fs/mqueue/msg_max
    ```
  - If you see that error in WSL, run the same line. CI will probably need it too.
- **Packages installed in the session:** `lcov`, `socat`, `liblua5.4-dev`, `libexpat1-dev`, `lua5.4`. The last three let `make native_eds.prep` succeed; the EDS build itself was not run.
- **File headers.** New files carry a plain Apache-2.0 header rather than the NASA docket header, since they are not NASA's work.

## How to see what exists so far (WSL)

```
git fetch origin && git checkout feature/gnss
git submodule update --init --recursive
make native_std.prep && make native_std.install

# terminal 1
cd build-native_std/exe/cpu1 && ./core-cpu1

# terminal 2, from the repo root
python3 apps/att_app/ground/gnss_monitor.py --enable-tlm

# parser tests
cd build-native_std/exe/cpu1 && ./coverage-att_app-att_app_nmea-testrunner
make -C apps/att_app/unit-test/standalone check
```

## Suggested order for the next session

1. Tests for `att_app_gnss.c` and the tick branch in cmds, then turn the WIP commit into a clean one.
2. Health module and tests; wire it into `ATT_APP_GNSS_PublishEpoch` with edge-triggered events.
3. Commands, sample_app cleanup, EDS.
4. 6-minute run with capture; serial test with socat.
5. COSMOS plugin and offset test.
6. CI, README, DESIGN_NOTES.
