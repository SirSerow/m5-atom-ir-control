# ATOM Lite Turntable IR Controller: implementation plan

Approved design and implementation stages, recorded on 2026-10-31. Checkboxes
separate software delivery from physical acceptance. This document is updated
with actual results; historical generic-firmware checks are in `hardware.md`.

## Goal and design

Replace the turntable remote during dataset recording. The ATOM Lite stores one
learned start/stop signal and transmits it through its onboard IR emitter when
requested over USB or by a short button press.

The PC interface exposes **toggle**, since the device has no feedback about
actual table motion. A successful response means IR transmission completed.

Learning requires the external U002 receiver; normal operation does not. GPIO12
is the onboard transmitter, GPIO32 receives from U002, GPIO39 reads the button,
and GPIO27 controls the indicator.
[Hardware reference](https://docs.m5stack.com/en/core/Atom-Lite).

## Stage 1: save the plan and reorganize the project

- [x] Save a separate staged implementation plan and checklist.
- [x] Keep the PlatformIO root and organize by the main project purpose.
- [x] Split firmware into simple, commented modules by responsibility.
- [x] Replace generic IR tools with the reusable turntable client and updated
  hardware smoke checks.
- [x] Preserve local ignored backups/logs and historical hardware notes.
- [x] Retain existing pinned dependencies; add Adafruit NeoPixel 1.12.3.
- [x] Remove required COM4 configuration and support explicit ports.
- [x] Build the reorganized firmware and document the new commands.

Target structure:

```text
README.md                     Purpose, quick start, build and upload
platformio.ini                Board and pinned firmware dependencies
requirements.txt              Python runtime dependency: pyserial
requirements-dev.txt          Development dependencies
src/                          Small firmware modules
python/turntable_remote.py    Reusable class and standalone CLI
tests/                        Automated tests and opt-in hardware smoke checks
docs/
  implementation-plan.md      Stages and acceptance checklist
  operation.md                Wiring, learning, button and LED behavior
  usb-interface.md            Complete integration contract
  hardware.md                 Hardware references and historical checks
```

Firmware responsibilities are application/button state, IR capture/transmit,
persistent storage, and USB protocol. Use straightforward functions, descriptive
names, and comments explaining timing, hardware assumptions, and failures.

**Acceptance:** firmware builds and documented commands use the new structure.

## Stage 2: validate the actual remote and onboard transmitter

- [ ] Capture several separate short presses of the actual start/stop remote.
- [ ] Verify start and stop use the same toggle action.
- [ ] Verify each captured action produces exactly one toggle.
- [ ] Verify onboard transmission at the intended mounting position.
- [ ] Verify repeat frames do not cause extra toggles.
- [ ] Observe ten deliberate transmissions producing exactly ten toggles.

V1 stores raw timings at an assumed 38 kHz, with a 1024-entry receiver buffer and
two-second transmit limit. The receiver cannot measure carrier frequency. Reject
incomplete, overflowed, repeat-only, or invalid captures. Use one complete
non-repeat frame without added repetitions. If the target needs multiple frames
or another carrier, revise the policy before using it for recording.

**Acceptance:** ten observed toggles, no misses or extras.

**Current limitation:** the actual remote cannot be pressed or table motion
observed by the development tools. Software implementation continued using the
approved single-frame policy; this stage remains a deployment acceptance gate,
not a claimed success. The full manual checklist is in `operation.md`.

## Stage 3: learning, storage, and device operation

- [x] Implement nonblocking timing with `millis()` and 30 ms debounce.
- [x] Normal short press toggles on release; long holds never also transmit.
- [x] A continuous 10-second hold enters learning once and requires release.
- [x] Capture the first valid non-repeat frame and freeze the RAM candidate.
- [x] Short press confirms and saves without transmitting.
- [x] A 10-second hold during learning/confirmation cancels.
- [x] Apply separate 30-second capture and confirmation timeouts.
- [x] Preserve the old signal on cancellation, timeout, or failed verification.
- [x] Enable reception only during learning; do not detect module presence.
- [x] Validate and persist one logical signal in NVS.
- [x] Reject toggles while learning or without a valid saved signal.
- [ ] Learn on hardware, power-cycle, remove U002, and successfully toggle.

| State | Behavior | Indicator |
| --- | --- | --- |
| No saved signal | USB works; toggles return `not_learned` | Steady amber |
| Ready | Short press transmits on release | Steady green |
| Learning | Wait for a complete remote frame | Blinking blue |
| Confirmation | Keep candidate in RAM | Steady blue |
| Success/error | Brief feedback, then return | Green/red |

The NVS format contains a version and checksum; validate lengths, timings,
duration, version, and checksum on load. Confirmed saves are verified before
promotion. The implementation uses two physical blob slots and an atomic
active-slot selector for one logical saved signal: write the inactive slot,
read and validate it, then commit the selector. Interrupted saves select either
the prior signal or a fully verified replacement.

Boot, connection, timeout, and save operations never transmit IR. Missing U002
causes a learning timeout. Preferences retains data across reset and power loss.
[Storage reference](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html).

**Acceptance:** saved signal operates after power cycling with U002 removed.

## Stage 4: USB contract and reusable Python client

- [x] Use newline-delimited JSON at 115200 baud, 8N1, no flow control.
- [x] Expose only `ping`, `status`, and `toggle`; learning remains button-only.
- [x] Require request IDs, echo IDs, and distinguish responses from events.
- [x] Include identity, protocol version, learning state, and signal availability.
- [x] Acknowledge toggle only after transmission completes.
- [x] Return stable `not_learned`, `busy`, `invalid_request`, `unknown_command` errors.
- [x] Bound input to 256 bytes and recover at newline after invalid input.
- [x] Introduce protocol version 2 and replace the old generic interface.
- [x] Implement persistent connection ownership and explicit reconnect/close.
- [x] Discover by read-only firmware handshakes among FTDI ports.
- [x] Warn and disable when zero or multiple controllers match.
- [x] Buffer partial input, match IDs, tolerate startup text, enforce deadlines.
- [x] Keep one outstanding request, with no automatic toggle retry/reconnect.
- [x] Support import without I/O and standalone status/toggle CLI testing.

Public class: `TurntableRemote(port=None, timeout=3.0)` with `connect()`, `close()`,
`status()`, `send()`, context-manager support, and connection properties. The
constructor attempts connection. Missing hardware warns and leaves sends as
no-ops; failures do not interrupt the recording application.

| Result | Meaning |
| --- | --- |
| `SENT` | Firmware confirmed completed transmission |
| `UNAVAILABLE` | No controller; no command sent |
| `FAILED` | Firmware explicitly rejected the command |
| `UNKNOWN` | Transmission outcome uncertain after timeout/connection loss |

Warn once per connection failure, avoiding repeated warnings for disabled sends.
Never replay a pending command after reconnecting. CLI global options include
`--port`; subcommands are `status` and `toggle`. Exit 0 indicates success, 1 a
failure/unknown outcome, and 2 unavailable hardware.

**Acceptance:** the class can be imported into another script, keeps it running
without hardware, and distinguishes successful/rejected/uncertain sends.

## Stage 5: documentation and verification

- [x] Document operation, wiring, positioning, learning, LEDs, and persistence.
- [x] Provide separate USB contract and Python integration documentation.
- [x] Clearly separate historical measurements from rewrite validation.
- [x] Run host firmware tests and Python client tests.
- [x] Verify the original ignored backup against its SHA-256 manifest.
- [x] Upload and verify the new firmware on the connected ATOM.
- [x] Run read-only USB smoke checks against that firmware.
- [ ] Complete actual remote/table acceptance and record observed results.

Automated scenarios include button debounce, long holds, release handling,
confirmation/cancellation/timeouts, millis rollover, capture rejection, persistent
record validation, write/readback/commit failures, fragmented/malformed/oversized
USB lines and recovery, discovery with zero/one/multiple devices, wrong IDs,
partial reads, disconnection/timeouts, one toggle write per send, CLI exit codes,
and absence of import-time I/O.

Hardware scenarios include persistence after power loss, operation without
U002, relearning, missing-receiver timeout, and repeated real-table toggling.
The opt-in smoke script is read-only by default; `--toggle` requests one actual
transmission and still requires a person to verify motion.

## Verification record (2026-10-31)

- **Host tests:** `python -m unittest discover -s tests -v` passed all 26 tests.
  The native C++ test executes actual firmware state, signal, storage, and USB
  parser code against pinned ArduinoJson, with warnings treated as errors.
- **Firmware build:** `python -m platformio run` succeeded for ATOM Lite.
  RAM: 28848 / 327680 bytes; flash: 379533 / 1310720 bytes.
- **Backup:** existing original full image is 4194304 bytes and its SHA-256
  matches the existing manifest. The image and manifest were not modified.
- **Upload:** protocol-2 firmware uploaded to the connected ESP32-PICO-D4 on
  COM4 at 115200 baud; esptool verified written-image hashes and reset the board.
- **USB on hardware:** explicit-port smoke checks and automatic-discovery CLI
  status passed. Ping, status, unknown-command handling, fragmented requests,
  exact 256-byte input, overflow recovery, and 14 malformed/invalid requests
  passed against the board. No IR was transmitted (`sent: 0`). Result summary
  is saved locally in ignored `logs/turntable-v2-usb-check.json`.
- **Current device state:** `not_learned`, `has_signal: false`,
  `storage_ready: true`. The remote still needs to be learned physically.
- **Actual IR/table tests:** pending. No learned target-remote signal or
  physical table-motion success is claimed.

## Assumptions and boundaries

- One turntable and one saved toggle signal; Python 3.11+.
- The recording application/operator establishes the initial table state.
- No motion sensing, rotation counting, speed control, Wi-Fi, or automatic stop
  on disconnect.
- Integration into the actual recording application is outside this project;
  the reusable Python class is the integration deliverable.
