# ATOM Lite turntable IR controller

Control a rotating table from a dataset recording script over USB. An M5Stack
ATOM Lite learns one start/stop IR signal, saves it across power cycles, and
replays it through its **onboard emitter**. The external U002 IR module is needed
only while learning.

The signal is a **toggle**. A USB acknowledgement confirms transmission, not
actual motion; establish the table's starting state before recording.

## Build and upload

Windows PowerShell, from this project directory:

```powershell
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\python.exe -m platformio run
.\.venv\Scripts\python.exe -m platformio run --target upload --upload-port COM4
```

Replace COM4 with your assigned port. Close serial monitors before upload or
using the Python client. Application uploads preserve learned NVS data; a full
flash erase or full-image restore does not. Existing original flash backups
remain local under `backups/`; see [hardware notes](docs/hardware.md).

## Learn and use

1. Connect U002 to the Grove socket and power the controller over USB.
2. Hold the ATOM button for 10 seconds until blue blinks, then release it.
3. Briefly press the remote's start/stop button. Solid blue indicates a capture.
4. Short-press the ATOM button to confirm and save. Green flashes indicate success.
5. Power off, remove U002, power on, and point the ATOM at the table.

Normal short button presses toggle on release. A 10-second hold starts learning;
another long hold cancels. Capture and confirmation each time out after 30 seconds.
Amber means no saved signal; green means ready. Details and the physical
acceptance checklist are in [device operation](docs/operation.md).

## Python remote control

For a PC that only uses the controller, install `requirements.txt` instead of
the development dependencies. The same file is reusable and executable:

```powershell
.\.venv\Scripts\python.exe python\turntable_remote.py status
.\.venv\Scripts\python.exe python\turntable_remote.py --port COM4 toggle
```

Add the `python/` directory to your recording application's import path, then:

```python
from turntable_remote import SendResult, TurntableRemote

with TurntableRemote() as remote:  # Optional explicit port="COM4".
    result = remote.send()
    if result == SendResult.UNKNOWN:
        print("Check the table before sending another toggle")
    # Your recording code can continue even when the controller is unavailable.
```

The class manages a persistent serial connection. If no compatible device is
found, it warns once and `send()` quietly returns `UNAVAILABLE`. Other results
are `SENT`, `FAILED` (explicit rejection), and `UNKNOWN` (uncertain outcome).
There are no automatic toggle retries or reconnections. Call `connect()`
explicitly after restoring a known physical state.

See the separate [USB interface and Python integration guide](docs/usb-interface.md)
for protocol 2, all result semantics, discovery behavior, and CLI exit codes.
The original generic IR protocol and tools have been replaced.

## Verification

The automated suite tests firmware timing/storage/USB parsing on the PC and the
Python client with simulated serial devices. First build firmware to fetch its
pinned ArduinoJson headers. A host C++ compiler is also required. On Windows:

```powershell
.\.venv\Scripts\python.exe -m platformio pkg install --global --tool platformio/toolchain-gccmingw32@1.50100.0
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
```

Linux/macOS can use `g++` or `clang++`; `CXX` can select a compiler executable.
Host test binaries are temporary and do not flash or transmit anything.

Read-only checks against a flashed controller:

```powershell
.\.venv\Scripts\python.exe tests\check_device.py --port COM4
```

Add `--toggle` only when you want **one actual IR transmission**. Observe table
motion yourself. The [implementation plan](docs/implementation-plan.md) records
stage completion, actual verification, and hardware checks still needed.

## Project layout

- `src/`: button/state logic, IR, validated signal format, NVS storage, and USB.
- `python/turntable_remote.py`: reusable client and standalone CLI.
- `tests/`: host tests and opt-in hardware smoke check.
- `docs/`: operation, USB integration, hardware history, and implementation plan.
- `backups/`, `logs/`: preserved local artifacts, excluded from Git.
