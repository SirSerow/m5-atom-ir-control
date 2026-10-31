# ATOM Lite USB IR controller

USB serial firmware and PC tools for learning and transmitting infrared
remote-control signals with an M5Stack ATOM Lite controller and Unit IR module.
The PC sends JSON commands over USB and receives decoded IR events and raw
timings. Captured signals can be saved to a file and transmitted again.

## Controller: M5Stack ATOM Lite (C008)

Hardware specifications from the [official ATOM Lite documentation](https://docs.m5stack.com/en/core/Atom-Lite):

| Parameter | Value |
| --- | --- |
| Manufacturer / model | M5Stack ATOM Lite |
| Product code / SKU | C008 |
| System on chip | Espressif ESP32-PICO-D4 |
| Processor | Dual-core, 32-bit Xtensa LX6, up to 240 MHz |
| SRAM | 520 KB |
| Flash memory | 4 MB |
| Power input | 5 V; manufacturer lists 500 mA |
| PC interface | USB Type-C; FTDI virtual serial port |
| Expansion connector | Grove / HY2.0-4P, with GPIO26 and GPIO32 |
| Wireless capability | 2.4 GHz Wi-Fi; unused by this firmware |
| Onboard RGB LED | SK6812 3535, GPIO27 |
| Onboard button | GPIO39 |
| Onboard IR transmitter | GPIO12; this project uses the external U002 transmitter |
| I2C pins | SDA GPIO25, SCL GPIO21; unused by this firmware |
| Dimensions | 24.0 × 24.0 × 9.5 mm |
| Weight | 5.5 g |
| Operating temperature | 0–40 °C |

The connected controller was identified as an ESP32-PICO-D4, revision v1.1,
with 4 MB flash. Its USB serial port on the development PC is COM4; port
assignment can differ on another PC.

## Infrared module: M5Stack Unit IR (U002)

Hardware specifications from the [official Unit IR documentation](https://docs.m5stack.com/en/unit/Unit_IR):

| Parameter | Value |
| --- | --- |
| Manufacturer / model | M5Stack Unit IR |
| Product code / SKU | U002 |
| Functions | Infrared transmission and reception |
| Emitter wavelength | 940 nm, invisible infrared |
| Receiver carrier frequency | 38 kHz, with hardware demodulation |
| Rated operating range | Up to 5 m; depends on positioning and surroundings |
| Interface | Grove / HY2.0-4P, separate IR_TX and IR_RX signals |
| Supply connection | 5 V and GND through the Grove cable |
| Protocol example | NEC; additional decoding/encoding is handled by firmware |
| Dimensions | 32.0 × 24.0 × 8.0 mm |
| Weight | 4.6 g |
| Mounting | Two LEGO-compatible mounting holes |

### Wiring

Connect the U002 to the ATOM Lite's Grove socket with the four-wire cable.
The mapping below combines the two official pin tables linked above.

| Cable color | ATOM Lite connection | U002 connection |
| --- | --- | --- |
| Black | GND | GND |
| Red | 5 V | 5 V |
| Yellow | GPIO26 | IR_TX |
| White | GPIO32 | IR_RX |

The U002 uses GPIO signals and has no discoverable device ID. Optical tests
verify its transmit and receive paths. Wi-Fi and I2C are not initialized by
this firmware.

## Communication parameters

| Parameter | Project setting |
| --- | --- |
| Transport | USB-to-UART serial |
| Default development port | COM4 |
| Runtime serial baud rate | 115200 baud |
| Serial format | 8 data bits, no parity, 1 stop bit (8N1) |
| Flow control | None |
| Firmware upload baud rate | 115200 baud |
| Application protocol | One JSON object per line, terminated by a newline |
| Default IR transmit carrier | 38 kHz |
| Firmware-configurable IR transmit carrier | 30–60 kHz; receiver remains tuned to 38 kHz |
| Receive buffer / frame timeout | 1024 entries / 50 ms |

Communication measurements on this setup, taken on 2026-10-31:

| Check | Result |
| --- | --- |
| Ping responses | 100 successful out of 100, no failures |
| Average command round-trip time | 16.14 ms |
| 95th-percentile round-trip time | 16.64 ms |
| Maximum round-trip time | 28.01 ms |
| Response throughput during 20 sequential status queries | About 9.24 kB/s |
| Reboots during the communication test | None |

These timings include USB and command-processing latency. The 8N1 line
capacity at 115200 baud is 11520 bytes/s in each direction; measured status
throughput is a workload result, not a maximum-speed benchmark.

## Verified device status (2026-10-31)

- Connected through the Windows FTDI driver on COM4.
- ESP32-PICO-D4 revision v1.1, 4 MB flash.
- Original full flash backed up and verified against the device before upload.
- Firmware v1.0.0 built, flashed, and verified successfully.
- On-device checks passed: ping/status/help, scalar NEC send, raw send, replay,
  and rejection of 12 invalid or oversized commands.
- Optical loopback passed: U002 transmitted and decoded NEC `C03F12ED`, 32 bits,
  67 raw pulses, with no capture overflow or truncation.
- Free heap remained 301428 bytes across the protocol checks, with no reboot.
- PC CLI status, ping, saved-capture transmission, and replay passed.

Run the hardware checks again with:

```powershell
.\.venv\Scripts\python.exe tools\check_device.py
```

This emits test IR signals. Results from the initial checks are saved locally
in `logs/device-check.log`; build/upload and backup verification logs are in
the same folder.

## Build and flash (Windows PowerShell)

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\python.exe -m platformio run
.\.venv\Scripts\python.exe -m platformio run --target upload
```

The default port is COM4. Override uploads with `--upload-port COMx`.
Close serial monitors before uploading. This device was reliable at
115200 baud; faster baud rates failed during the initial checks.

## Use from the PC

```powershell
.\.venv\Scripts\python.exe tools\irctl.py status
.\.venv\Scripts\python.exe tools\irctl.py ping
.\.venv\Scripts\python.exe tools\irctl.py listen --seconds 30 --save capture.json
.\.venv\Scripts\python.exe tools\irctl.py raw capture.json
.\.venv\Scripts\python.exe tools\irctl.py send NEC 0x20DF10EF --bits 32
.\.venv\Scripts\python.exe tools\irctl.py selftest
```

Point a remote at the U002 while listening. `--save` writes the most recent
complete, non-repeat frame. Use `raw capture.json` to reproduce it. The
example NEC code is illustrative: use a code learned from your own remote.
The CLI accepts `--port COMx` before the command.

Self-test transmits a test NEC frame (0xC03F12ED) and listens for that exact
frame on the U002 receiver. Optical feedback may require a reflective surface
near the unit. A successful result verifies both optical paths; a failed
result alone does not establish a hardware fault. CLI exit codes: 0 success,
1 command/connection error, 2 no optical loopback during self-test.

## Serial protocol

One JSON object per line; commands end with a newline. Responses and unsolicited
receive events also occupy one line. ESP32 ROM boot text can appear on reset;
ignore lines that are not JSON. Supported commands:

```json
{"cmd":"ping"}
{"cmd":"status"}
{"cmd":"help"}
{"cmd":"send","protocol":"NEC","code":"0x20DF10EF","bits":32,"repeat":0}
{"cmd":"raw","khz":38,"pulses":[9000,4500,560,560,560,1690,560]}
{"cmd":"replay"}
{"cmd":"selftest"}
```

`send` supports scalar protocols available in IRremoteESP8266, with at most
64 bits and 0-10 repeats. State-based A/C messages can be captured and sent
as raw timings. `raw` accepts at most 1024 alternating mark/space durations
in microseconds, starting with a mark; each duration is 0-65535 us, and total
duration is 1 us to two seconds. A zero duration preserves phase when a long
interval is split into 16-bit entries. Carrier is 30-60 kHz (default 38).
The U002 receiver is tuned to 38 kHz and cannot measure incoming carrier
frequency; captured `khz:38` is an assumption.

Every `received` event includes protocol, hex code, bit count, repeat flag,
overflow/truncation flags, and raw timing pulses. `replay` sends the last
non-repeat frame stored in RAM, defaulting to 38 kHz; incomplete captures are rejected.
RAM captures disappear on reset. Prefer a saved capture for repeatable use.
Reception is paused during normal transmission, so it does not learn its
own output. Self-test deliberately keeps reception enabled.

Receive buffer: 1024 entries; end-of-frame timeout: 50 ms. Long gaps can split
some remotes' multi-frame messages. Check overflow/truncation before saving
or replaying complex A/C messages. There is no persistent on-device code store.

## Original firmware backup

The full original flash is saved locally under `backups/` and excluded from
Git because flash images may include credentials. The `logs/` folder contains
local diagnostics. Restore the original full image with:

```powershell
$backup = Get-Content backups\manifest.json | ConvertFrom-Json
.\.venv\Scripts\python.exe -m esptool --chip esp32 --port COM4 --baud 115200 write-flash 0 (Join-Path backups $backup.file)
```

Restoring overwrites this firmware and flash settings. Do not run an erase
command first. Check `backups/manifest.json` for the backup size and SHA-256.

## Hardware references

- [ATOM Lite C008 and Grove pin mapping](https://docs.m5stack.com/en/core/Atom-Lite)
- [U002 IR pin mapping](https://docs.m5stack.com/en/unit/Unit_IR)
- [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266)
