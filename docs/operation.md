# Device operation

The controller stores one IR start/stop frame for one turntable. The remote's
button must toggle rotation using the same signal in both states. Establish the
initial table state yourself: neither USB nor the indicator can confirm motion.

## Hardware and placement

Use an M5Stack ATOM Lite (C008) with a USB data cable. Point its onboard IR
emitter at the table's receiver and verify the intended position experimentally.
The U002 module's published range does not describe the onboard emitter's range.

Connect M5Stack Unit IR (U002) to the Grove socket **for learning only**:

| Cable | ATOM Lite | U002 |
| --- | --- | --- |
| Black | GND | GND |
| Red | 5 V | 5 V |
| Yellow | GPIO26 | IR_TX, unused |
| White | GPIO32 | IR_RX |

Use GPIO12 for transmission even while U002 is connected. Reception is disabled
outside learning; the controller does not probe for an attached receiver.
Disconnect USB power before attaching or removing the Grove cable, then power
the controller again. Saved signals survive the power cycle.

## Learn or replace the signal

1. Connect U002, power the ATOM, and point the remote at U002.
2. Hold the ATOM button continuously for about 10 seconds, until blue blinks.
3. Release the ATOM button. Briefly press the remote's start/stop button once.
4. Solid blue means a valid candidate was captured. Further remote input is
   ignored; the existing saved signal has not changed yet.
5. Short-press and release the ATOM button to save the candidate. Green flashes
   confirm a verified save. This confirmation does **not** transmit IR.
6. Disconnect power, remove U002, and reconnect power. Green indicates a valid
   saved signal. Short-press the ATOM button to test one toggle.

There is a 30-second timeout waiting for a capture and a separate 30-second
timeout waiting for confirmation. Hold the ATOM button for 10 seconds during
either stage to cancel. Release after cancellation before another action.
Cancellation and timeout preserve the old saved signal. A button held when
capture arrives cannot confirm that capture on release; make a fresh press.
A short press while still waiting for IR has no effect.

An absent receiver produces a learning timeout. A rejected repeat-only,
overflowed, incomplete, or invalid frame leaves the device listening until
timeout. A steady blue candidate can be the wrong remote button; cancel and
learn again if unsure.

## Indicator and normal use

| Indication | Meaning |
| --- | --- |
| Steady amber | No valid saved signal; toggles are rejected |
| Steady green | Saved signal ready |
| Blinking blue | Waiting for a remote frame |
| Steady blue | Waiting for a short button press to confirm |
| Brief green flashes | Save or transmission completed |
| Brief red | Save failure, learning timeout, or rejected manual toggle |

A normal short press sends exactly one frame **on release**. A long press enters
learning and does not also toggle. USB toggles are rejected throughout learning
and confirmation. Status and ping remain available. USB-request errors are
returned on USB; they do not all flash the indicator.

Opening or closing a USB connection, booting, saving, cancellation, and timeouts
never transmit IR. Disconnecting USB does not stop a running table.

## Persistence and limitations

Raw timings are stored in ESP32 NVS with a format version, duration, and CRC32.
A save writes an inactive record, reads and validates it, then commits the
active-slot selector. Unverified candidates are never selected. A reset during
save retains either the old signal or the fully verified new signal. The two
physical slots implement one logical saved signal; there is no profile menu.
Corrupt or unsupported active records are treated as not learned. A red save
failure preserves the previous selection; try learning again and inspect USB
events if it repeats. If the selector itself is corrupt, `storage_ready` is
false and service/reflashing with an intentional NVS reset may be necessary.

V1 uses one complete raw frame at an **assumed 38 kHz**, no added repeats, a
1024-entry receiver buffer, and a 50 ms frame-ending gap. Total transmit duration
must be at most two seconds. Long timing intervals are split into 16-bit entries
with zero-length opposite-phase separators. The demodulating U002 cannot measure
carrier frequency. Some remotes send multiple frames, change payloads between
presses, or use another carrier; they require a revised capture policy.

## Real-table acceptance checklist

These checks require the actual remote, table, and a person observing motion.
No synthetic test or USB acknowledgement substitutes for them.

- [ ] Capture separate short presses when starting and stopping. Confirm that
  each learned frame performs the same toggle action.
- [ ] At the intended mounting position, make ten deliberate transmissions
  spaced at least one second apart. Observe exactly ten toggles.
- [ ] Hold the remote button during learning; confirm only one frame is saved
  and replay produces only one toggle. If it toggles more than once, do not use
  this capture policy for recording.
- [ ] Learn and confirm, power-cycle, disconnect U002, and toggle successfully.
- [ ] Replace a learned signal; verify the replacement after a power cycle.
- [ ] Cancel and time out during both learning stages; verify the old signal
  still works, including after a power cycle.
- [ ] Enter learning without U002; verify timeout and continued USB operation.
- [ ] Remove USB during a running table test; verify the recording script warns
  without crashing, and manually restore a known table state before reconnecting.

Record results and positioning in [implementation-plan.md](implementation-plan.md).
