# ATT_APP design notes: interview study guide

This is for you, not recruiters. Read it until you can explain every section without looking, then open each source file and check you can find what it describes.

## 1. The 60-second version

> "I built a flight software app on NASA's Core Flight System that monitors GNSS signal health. It reads the receiver's NMEA output, either a recorded log or a live serial port, and publishes position and per-satellite C/N0 as telemetry at 1 Hz. On board, it flags three conditions: lost fix, a single weak satellite, and a broadband C/N0 drop across most satellites at once, which is the signature of jamming. The hard part was telling interference apart from blockage and making sure a jammer can't train the detector to accept it. It's fully unit tested at 100% line coverage, the parser is fuzzed under AddressSanitizer, and there's a COSMOS ground system whose packet definitions are checked automatically against the C structs. That check caught a real bug: my NAV packet was sharing a message ID with another app."

## 2. cFS concepts you need

- **cFE (core Flight Executive):** the services every app uses: Executive Services (ES) starts apps, Software Bus (SB) moves messages, Event Services (EVS) sends text events, Table Services, Time Services.
- **OSAL / PSP:** OSAL hides the operating system (files, queues, tasks), so the same app runs on Linux, VxWorks or RTEMS. PSP hides the hardware platform. ATT_APP only talks to the OS through OSAL (`OS_OpenCreate`, `OS_TimedRead`), which is why it isn't tied to Linux.
- **Software Bus and message IDs (MIDs):** apps never call each other. They publish packets with a message ID and subscribe to the IDs they want. A MID comes from a **topic ID**: commands are `0x1800 | topic`, telemetry `0x0800 | topic`. ATT_APP's topics are 0x87 (commands), 0x88 (HK request), 0x89 (HK), 0x8F (wakeup), 0x93 (NAV).
- **Pipes:** an app's inbox. ATT_APP has one pipe and subscribes it to its command, HK-request and wakeup MIDs. The main loop blocks on that pipe.
- **CCSDS packets:** every message has a 6-byte big-endian primary header (stream ID = MID, sequence, length = total − 7). Commands add a 2-byte secondary header (function code, checksum); telemetry adds a time stamp, padded so the payload starts 8-byte aligned (16-byte header).
- **SCH_LAB:** the lab scheduler. It sends ATT_APP WAKEUP 50 times a second and SEND_HK once a second.
- **CI_LAB / TO_LAB:** command ingest (UDP 1234 in) and telemetry output (UDP 2234 out). TO_LAB only sends what its subscription table lists, and only after an "enable output" command.
- **HK (housekeeping):** a periodic health packet with counters. The command counter and error counter are how operators confirm a command landed.
- **Platform config:** `fsw/inc/att_app_internal_cfg.h` holds every tunable value as `DEFAULT_...`. A mission overrides them by dropping `att_app_internal_cfg_values.h` (or `cpu1_...`) into `sample_defs/`; the build prints "Using file: ..." for each.

## 3. Life of one NAV packet

1. SCH_LAB sends WAKEUP. ATT_APP's main loop wakes on its pipe, dispatch sees the WAKEUP MID, and `ATT_APP_WakeupCmd` bumps `WakeupCounter`. Every 50th one calls `ATT_APP_GNSS_Tick` (1 Hz).
2. `Tick` calls `OS_TimedRead` with a 0 ms timeout, so it takes whatever bytes are waiting and returns immediately. In file mode it reads until one epoch is out; in device mode it drains what the receiver has sent.
3. The bytes are split into lines in a 256-byte buffer. A longer line is thrown away and counted once.
4. Each line goes to the parser: checksum, sentence type, then `ParseGga` or `ParseGsv` into structs. Bad sentences bump `ParseErrorCounter` and send a rate-limited event.
5. The **epoch assembler** collects one GGA plus its GSV sentences. When the *next* GGA arrives, the finished epoch comes out.
6. `ATT_APP_HEALTH_Update` computes mean C/N0 and the weakest satellite, updates the three debounced flags, then updates the baselines (unless frozen). It returns flags plus which ones just set or cleared.
7. `ReportHealthChanges` sends one event per flag edge.
8. `PublishEpoch` fills the NAV payload, time-stamps it and sends it on the bus.
9. TO_LAB is subscribed to NAV's MID, so it sends the packet over UDP 2234 to the ground, where COSMOS or `gnss_monitor.py` decodes it.

## 4. File by file

- **`att_app.c`:** `ATT_APP_Main` runs `Init`, then blocks on `CFE_SB_ReceiveBuffer` (pend forever: SCH_LAB's 50 Hz wakeup guarantees it wakes), handing each message to dispatch. `Init` registers events (including a filter so parse-error events stop after 8), creates the pipe, subscribes and opens the GNSS input. A failing input doesn't stop the app.
- **`att_app_dispatch.c`:** a switch on MID, then on function code for commands. Each command's length is checked against `sizeof` its struct before the handler sees it. Unknown codes and bad lengths count as errors.
- **`att_app_cmds.c`:** small handlers. `SetCnoThreshold` range-checks 10–55. `RestartLog` calls `ATT_APP_GNSS_Restart`. `SendHk` copies counters into HK and sends it.
- **`att_app_gnss.c`:** everything with state and side effects: the open input, the line buffer, the assembler, the health state, events and publishing. It's the glue between pure code and cFE.
- **`att_app_nmea.c`:** pure functions. Input is always (pointer, length), never assumed to end in NUL. Numbers are parsed by hand with length limits; no `sscanf`, `atof` or `strtok`. Empty fields are legal where NMEA allows them (no position without a fix, no C/N0 for an untracked satellite).
- **`att_app_health.c`:** pure functions. `Debounce` is one helper for all three flags (set after N bad epochs, clear after M good). `Average` is a running mean that becomes an exponential moving average once it has 30 samples. Per-satellite baselines live in a small table keyed by PRN, and satellites gone for 30 epochs are dropped.
- **`config/` and `fsw/inc/`:** message layouts (`default_att_app_msgdefs.h`, `msgstruct.h`), topic IDs, function codes, event IDs and platform values.
- **`ground/`:** `make_nmea_log.py` (log generator, fixed seed), `gnss_monitor.py` (ground tool), `pty_feed.sh` (fake serial receiver), `check_hk.py` (original HK script).
- **`unit-test/`:** coverage tests per source file using cFS's UT-Assert framework and generated stubs; `standalone/` builds the parser alone under sanitizers; `check_topic_ids.py`.

## 5. Design decisions, and what was rejected

1. **Baselines freeze during a drop.** *Rejected:* averages that always adapt. Within about 30 s they would follow the jammed level down and the alarm would clear while the jammer was still on. *Cost:* if the receiver starts under jamming, the jammed level becomes "normal."
2. **BROADBAND_DROP needs two tests together:** the mean falls 8 dB *and* 75% of satellites fall 6 dB against their own baselines. *Rejected:* the mean alone (half the sky collapsing moves the mean) or a count alone (the no-fix window loses 7 of 10 satellites but the 3 left are just as strong). Untracked satellites count as dropped, since jamming makes satellites disappear too.
3. **An epoch is released on the next GGA.** *Rejected:* releasing on the last GSV sentence. That's one second faster, but multi-constellation receivers send several GSV groups (GPS, GLONASS, Galileo) per epoch, so you'd publish partial satellite lists. *Cost:* up to one epoch of latency.
4. **Parser and health are pure functions with caller-owned state.** *Rejected:* calling cFE and globals from inside them. Pure code is tested with no stubs and can be fuzzed under ASan with 76,800 corrupted inputs. The caller turns flag changes into events.
5. **Non-blocking `OS_TimedRead` in the main task.** *Rejected:* a blocking `OS_read`, which would stall commands and HK while waiting on the receiver, or a child task, which adds shared state that needs a mutex for no benefit at 1 Hz. One task means no locking at all.
6. **NAV payload ordered largest-alignment first, with compile-time size checks.** *Rejected:* packed structs, which make unaligned accesses (slow or a crash on some flight CPUs) and hide layout problems. The payload is exactly 64 bytes with no implicit padding, and the build fails if that changes.
7. **Edge-triggered events with debouncing.** *Rejected:* an event every epoch while a flag is set, which floods the event log and hides other apps' events. Asymmetric debounce: NO_FIX sets after 1 epoch but clears after 2, so one good epoch in a bad patch doesn't flap the flag (you can see it in the run: fix=1 with NO_FIX still set at 18:04:00).
8. **Parse errors: counter counts all, events stop after 8 (EVS filter).** *Rejected:* an event per bad sentence (a noisy serial line would flood the log) or no events at all (the ground would only find out from a counter).
9. **Input mode is an argument to `ATT_APP_GNSS_Init`,** with the platform value passed in by `Init`. Both file and device paths are tested in one unit-test build instead of needing two builds.
10. **A unique topic ID, enforced by a check.** NAV was first on 0x8A, which File Manager already used for its housekeeping. The build can't catch that, since both are valid IDs; subscribers just get both packets. It showed up as a 28-byte "NAV" packet every 15 s. It moved to 0x93, and `check_topic_ids.py` now fails CI on any collision.
11. **COSMOS definitions are checked against compiled C, not against a hand-written copy.** The test compiles a probe with the flight build's own flags and compares `offsetof`/`sizeof`. A hand-written table could be wrong in the same way the COSMOS file is.

## 6. Known limits (say these before they ask)

- The detector judges change, not absolute level. Starting up while jammed, a drop in the first 10 epochs, a very slow fade, or a jammer that never stops all get past it or leave the flag stuck until `RESTART_LOG`.
- A live receiver that goes silent produces no NAV and no event. The fix is an "input stale" flag after N ticks with no epoch.
- The test data is synthetic. A real log with a real interference event would be the real proof.
- The EDS build and the COSMOS GUI haven't been run.
- Better interference detection uses AGC (automatic gain control) level from the receiver front end. AGC rises when there's more power in the band, and NMEA doesn't carry it. That's what you'd add with a u-blox `UBX-MON-HW` message or an SDR front end.

## 7. Likely interview questions

1. **What is C/N0, and why not SNR?** Carrier-to-noise *density*: signal power over noise power per 1 Hz, in dB-Hz. It doesn't depend on the receiver's bandwidth, so it's comparable across receivers. Open sky is roughly 40–48 dB-Hz; below about 30, tracking gets unreliable.
2. **How does your app tell jamming from a tunnel?** Two tests at once. Jamming lowers almost every satellite by a similar amount, so the mean falls *and* most satellites fall against their own baselines. Blockage removes satellites but the remaining ones stay strong, so the mean doesn't fall.
3. **Why freeze the baseline?** Otherwise the average follows the jammed level down and the detector concludes everything is normal.
4. **What's a false positive you could get?** Antenna damage or a cable fault, which also drops every satellite at once. On board you can't tell that apart from jamming; AGC would help, since a jammer raises in-band power and a broken cable lowers it.
5. **Why does the app read in the main task instead of a separate task?** A non-blocking read at 1 Hz costs nothing, and one task means no shared state and no mutex. Blocking would stop commands and HK.
6. **How is a command validated?** MID routes it to the app, the function code picks the handler, the length must equal `sizeof` that command's struct, and the payload is range-checked. Each failure bumps the error counter and sends an event.
7. **What does a CCSDS header contain?** Stream ID (version, type, secondary-header flag, APID), sequence flags and count, and data length (total − 7). cFS commands add a function code and checksum; telemetry adds a time stamp.
8. **How did you test it?** UT-Assert coverage tests per file with stubbed cFE/OSAL (1,742 assertions, 100% lines), the parser alone under ASan/UBSan with replay and corrupted input, live runs checking every event, and CI that does all of it on every push.
9. **What's the hardest bug you found?** The message ID collision with FM housekeeping. Nothing crashed and nothing failed. I found it by decoding a capture with the COSMOS definitions and seeing 28-byte packets on the NAV ID.
10. **Why not `sscanf`/`atof` in the parser?** They need NUL-terminated strings, depend on locale, and give weak error handling. Flight code also often avoids the C library's heavier parts. A hand-written bounded parser can't read past the buffer and can be tested exhaustively.
11. **What happens with a corrupted sentence?** The checksum fails, the sentence is dropped and counted, an event goes out (rate-limited), and the epoch carries on with the remaining sentences.
12. **How would you port it to real hardware?** OSAL and PSP already abstract the OS. Cross-compile for the target (for example a LEON3 under RTEMS), point the device config at its UART, and set the baud rate in the BSP, because OSAL has no termios API.
13. **Why debounce, and why asymmetric?** To stop flags flapping on a single noisy epoch. Clearing is stricter than setting because declaring "all clear" too early is the worse mistake.
14. **Why is the NAV payload exactly 64 bytes with no padding?** Fields are ordered by alignment (doubles, then floats and uint32s, then bytes) and compile-time asserts lock the size, so the ground definition can't silently drift and there are no unaligned accesses.
15. **What would you do next?** A real recorded log, an input-stale flag, AGC-based detection, and eventually my own correlator on an FPGA so the C/N0 estimates come from my own tracking loops.

## 8. Before an interview

They will ask which parts you wrote and how you built it. Be straight about it. If you used AI tools to write code, say so and show what you verified, decided and fixed yourself. Interviewers respect that far more than a story that comes apart on the second follow-up question. Get to where you can whiteboard sections 3 and 5 from memory and change one threshold, rebuild and show the effect live. Then the project is yours however it was typed.
