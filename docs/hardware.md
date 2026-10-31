# Hardware, dependencies, and historical checks

## Current hardware configuration

| Component | Details |
| --- | --- |
| Controller | M5Stack ATOM Lite C008, ESP32-PICO-D4, 4 MB flash |
| PC connection | USB Type-C data cable, FTDI virtual serial port |
| Runtime serial | 115200 baud, 8N1, no flow control |
| IR transmitter | Onboard GPIO12; U002 transmitter is unused |
| Learning receiver | U002 38 kHz demodulator on Grove GPIO32 |
| Button | GPIO39 with the board's pull-up |
| RGB indicator | SK6812 on GPIO27 |
| Wireless/I2C | Not initialized |

Official references: [ATOM Lite pin map and specifications](https://docs.m5stack.com/en/core/Atom-Lite),
[Unit IR wiring and specifications](https://docs.m5stack.com/en/unit/Unit_IR).
See [operation.md](operation.md) for wiring and actual table acceptance checks.
The receiver cannot report a hardware identity or measure the incoming carrier.

Pinned firmware dependencies are Espressif32 6.7.0 (Arduino ESP32 2.0.16),
IRremoteESP8266 2.9.0, ArduinoJson 6.21.5, and Adafruit NeoPixel 1.12.3.
Preferences is supplied by the Arduino ESP32 framework. Storage uses NVS in the
existing default partition layout; no partition migration is required.

References: [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266/tree/v2.9.0),
[Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html),
[NeoPixel SK6812 support](https://github.com/adafruit/Adafruit_NeoPixel/tree/1.12.3).

## Historical results: original generic firmware only

These checks were recorded on **2026-10-31 before the turntable firmware rewrite**.
They do not verify protocol 2, onboard-emitter performance, learning-button
behavior, persistence, or the target remote/table.

- The connected board was identified as ESP32-PICO-D4 revision v1.1, 4 MB flash,
  using the Windows FTDI driver on COM4. That port is not a project requirement.
- The original full flash was backed up and verified before the first upload.
- Generic firmware v1.0.0 built, uploaded, and passed its original protocol tests.
- U002 external-transmitter optical loopback decoded NEC `C03F12ED`, 32 bits,
  with 67 raw timings and no overflow. This was **not the onboard transmitter**.
- The original PC CLI and twelve invalid/oversized-input checks passed.
- Heap was 301428 bytes across those checks, with no reboot.

| Historical communication measurement | Result |
| --- | --- |
| Ping success | 100 / 100 |
| Average round trip | 16.14 ms |
| 95th percentile | 16.64 ms |
| Maximum round trip | 28.01 ms |
| Sequential status throughput | About 9.24 kB/s |

The serial line's theoretical payload capacity is 11520 bytes/s per direction
at 115200 baud with 8N1. The numbers above describe an old workload, not a new
firmware performance guarantee. Original logs remain local under `logs/`.

## Backups and restoring the original firmware

`backups/` and `logs/` remain excluded from Git. Full flash images may contain
credentials. Preserve the existing original backup and its manifest. Before
changing a deployed device, a new full-flash snapshot can be made to a **new**
filename with the pinned development esptool:

```powershell
.\.venv\Scripts\python.exe -m esptool --chip esp32 --port COM4 --baud 115200 read-flash 0 0x400000 backups\before-next-upload.bin
```

Do not reuse a filename holding a backup you still need. To restore the original
full image identified by the existing manifest:

```powershell
$backup = Get-Content backups\manifest.json | ConvertFrom-Json
.\.venv\Scripts\python.exe -m esptool --chip esp32 --port COM4 --baud 115200 write-flash 0 (Join-Path backups $backup.file)
```

Restoring replaces firmware and NVS, including learned signals. Do not erase
flash first. Check the manifest's file, image size, and SHA-256 before restoring.
Normal PlatformIO application uploads preserve NVS; full-image restore or erase
does not. The old RAM-only capture cannot migrate because it was never stored.
