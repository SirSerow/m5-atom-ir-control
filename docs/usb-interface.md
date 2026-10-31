# USB interface and Python integration

## Transport and framing

The ATOM Lite uses an FTDI USB-to-UART interface, **115200 baud, 8 data bits, no
parity, 1 stop bit**, no flow control. The serial port is assigned by the OS;
COM4 is only an example. Opening a port may reset the ESP32 with some drivers.

Each request is one UTF-8 JSON object followed by LF (`\n`). CRLF is also
accepted. A maximum of **256 bytes before LF**, including any CR, is accepted.
Embedded NUL, excess input, invalid JSON, trailing junk, or invalid fields produce
`invalid_request`; input recovers after LF. An unfinished line is held until LF,
so a client must always terminate its requests. Empty lines are invalid.

`id` is required and must be an integer from 0 through 2147483647. `cmd` must be
a nonempty string. Use a new ID for each request and allow one outstanding
request per connection. IDs correlate replies; **they are not deduplication
keys**. Sending the same toggle twice can toggle twice.

Responses have `type: "response"`, echoed `id`, and boolean `ok`. Invalid
requests use `id: null` when no valid ID could be recovered. Startup and physical
learning events have `type: "event"` and no request ID. ESP32 ROM boot messages
may be non-JSON. Ignore boot text, events, and responses for other IDs while
waiting for your reply. Firmware responses use CRLF, which JSON line readers
should accept. Response/event lines fit within the supplied client's 4096-byte
receive-line limit.

## Commands (protocol version 2)

### Ping

```json
{"id":1,"cmd":"ping"}
{"type":"response","id":1,"event":"pong","ok":true}
```

### Status

```json
{"id":2,"cmd":"status"}
{"type":"response","id":2,"event":"status","firmware":"atom-turntable-ir","firmware_version":"2.0.0","protocol_version":2,"state":"ready","has_signal":true,"storage_ready":true,"pulses":67,"carrier_khz":38,"sent":0,"uptime_ms":12345,"ok":true}
```

The numbers above are examples. `state` is `not_learned`, `ready`, `learning`,
or `confirm`. `has_signal` refers to the saved signal, including during learning.
`pulses` is the saved signal's timing-entry count, not the candidate's count.
`storage_ready` indicates NVS opened successfully with a valid slot selector;
it does not prove that a saved signal exists. `sent` counts completed IR
transmissions since boot, including manual button actions. `uptime_ms` wraps
after approximately 49.7 days. `carrier_khz` is an assumption, not a measurement.

### Toggle

```json
{"id":3,"cmd":"toggle"}
{"type":"response","id":3,"event":"sent","sent":1,"ok":true}
```

Exactly one saved frame is transmitted through GPIO12. The reply is generated
after transmission completes. **It confirms IR output, not reception or table
motion.** A physical signal can take up to two seconds; use a three-second
request timeout by default, longer if your host environment requires it.

The transmission itself is synchronous; USB requests are serviced sequentially.
Do not pipeline toggles. Learning, button timing, and LED timing use a
nonblocking state machine; waiting for a remote does not block USB service.

### Errors

```json
{"type":"response","id":4,"ok":false,"error":"not_learned"}
```

| Code | Meaning |
| --- | --- |
| `not_learned` | No valid persisted signal; no IR was sent |
| `busy` | Device is learning or awaiting confirmation; no IR was sent |
| `invalid_request` | Invalid JSON, fields, line contents, or line length |
| `unknown_command` | Valid request with an unsupported command |

Protocol 2 replaces the original generic `send`, `raw`, `replay`, `selftest`, and
`help` commands. Old clients must be replaced. Learning, confirmation, and
cancellation are available only on the physical button.

## Unsolicited events

Events include status fields from the status example above:

```json
{"type":"event","event":"capture_ready","firmware":"atom-turntable-ir","firmware_version":"2.0.0","protocol_version":2,"state":"confirm","has_signal":false,"storage_ready":true,"pulses":0,"carrier_khz":38,"sent":0,"uptime_ms":20000}
```

Event names: `ready`, `learning_started`, `capture_ready`, `capture_rejected`,
`signal_saved`, `save_failed`, `learning_cancelled`, `learning_timeout`, `sent`
(manual toggle), and `toggle_rejected`. Where relevant, `reason` is
`invalid_capture`, `storage_error`, `not_learned`, or `busy`. `capture_rejected`
leaves the receiver listening; `save_failed` returns to normal operation with
the previous saved selection. Events are best-effort notifications; query status
after connecting rather than depending on receipt of the startup event.

## Reusable Python class

Install `requirements.txt`. Import from `python/turntable_remote.py` by adding
that directory to your recording application's import path, or copy this single
file into your application. Importing it opens no ports and logs no messages.

```python
from turntable_remote import SendResult, TurntableRemote

with TurntableRemote(port=None) as remote:  # Or port="COM4" / "/dev/ttyUSB0".
    result = remote.send()
    if result == SendResult.SENT:
        print("IR toggle transmitted")
    elif result == SendResult.UNKNOWN:
        print("Check the table and restore a known state before continuing")
    # Run your recording logic here. No automatic toggle is sent on exit.
```

`TurntableRemote(port=None, timeout=3.0)` immediately attempts to connect. It
enumerates FTDI ports (VID 0x0403), opens each, waits 1.5 seconds for possible
boot/reset, and identifies it with a status request. Discovery never sends IR.
Other FTDI devices can be probed and may reset when opened; use an explicit port
to limit discovery. An explicit port also supports other USB serial bridges.
Discovery requires exactly one matching firmware identity and protocol version.
Each candidate can take up to the boot wait plus the request timeout.

| Member | Contract |
| --- | --- |
| `connected` | Whether the client currently owns a usable connection |
| `connected_port` | Selected port, or `None` |
| `connect()` | Close the old connection and explicitly discover/reopen; return boolean |
| `close()` | Release the port; safe to call repeatedly |
| `status()` | Return the status response dictionary, or `None` |
| `send()` | Send one toggle and return `SendResult` |

The serial port stays open between calls. Calls are serialized with a lock; no
background thread is used. The client buffers partial lines, skips boot text and
unrelated messages, correlates IDs, and bounds reads and writes with deadlines.

| SendResult | Meaning and connection behavior |
| --- | --- |
| `SENT` | Firmware acknowledged completed transmission |
| `UNAVAILABLE` | No connected controller; no command written |
| `FAILED` | Explicit firmware rejection; connection remains open |
| `UNKNOWN` | Timeout, partial write, disconnection, or malformed matching response; port closed |

Hardware/communication failures use Python logging warnings and do not raise
through the normal API. A failed connection attempt warns once; repeated sends
while disabled quietly return `UNAVAILABLE`. Each explicit rejected send warns.
Invalid timeout arguments raise `ValueError` because they are programming errors.

An uncertain toggle is **never retried**, and the class does not automatically
reconnect. Check actual motion and restore the intended table state before
calling `connect()` explicitly. Reconnecting does not replay any old command.
Avoid treating enum values as booleans; compare the result explicitly.

## Standalone CLI

From the project root, with runtime dependencies installed:

```powershell
.\.venv\Scripts\python.exe python\turntable_remote.py
.\.venv\Scripts\python.exe python\turntable_remote.py status
.\.venv\Scripts\python.exe python\turntable_remote.py --port COM4 toggle
```

Global options precede `status` or `toggle`: `--port PORT`, `--timeout SECONDS`.
Omitting the command defaults to `toggle`. After connecting, `toggle` waits for
Enter before sending one command; any text, Ctrl+C, or end-of-input cancels without
transmitting. Cancellation exits with code 0. `status` prints JSON immediately
without prompting. `toggle` prints `sent`, `failed`, or `unknown` for an
attempted send. Warnings go to stderr. Exit codes: **0** success, **1** failed or
uncertain outcome, **2** unavailable controller (also argparse usage errors).
Each CLI invocation closes the port on exit. Only one process can own the serial
port, so close serial monitors before using the client or uploading firmware.
