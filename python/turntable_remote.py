"""Persistent USB connection to an ATOM Lite turntable controller.

Import TurntableRemote and SendResult from this file, or run it directly:
    python turntable_remote.py --port COM4 status
    python turntable_remote.py --port COM4 toggle

A SENT result confirms IR transmission, not table motion. Never automatically
retry UNKNOWN: the first transmission may already have toggled the table.
"""

import argparse
from enum import Enum
import json
import logging
import math
import threading
import time

import serial
from serial.tools import list_ports


_LOGGER = logging.getLogger(__name__)
_FIRMWARE = "atom-turntable-ir"
_PROTOCOL_VERSION = 2
_MAX_LINE = 4096


class SendResult(str, Enum):
    SENT = "sent"
    UNAVAILABLE = "unavailable"
    FAILED = "failed"
    UNKNOWN = "unknown"


class _ExchangeError(Exception):
    """Internal transport/protocol failure; converted to a warning by the API."""


class TurntableRemote:
    """Own one connection, discovering a compatible FTDI device if port is None.

    Construction attempts to connect. Hardware errors warn and leave the object
    usable in disabled mode; invalid timeout arguments raise ValueError.
    Calls are serialized by a lock; no background thread or automatic retry is
    used. Call connect() explicitly to recover after a connection failure.
    """

    def __init__(self, port=None, timeout=3.0):
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout must be finite and positive")
        self.port = port
        self.timeout = float(timeout)
        self._device = None
        self._buffer = bytearray()
        self._discarding = False
        self._next_id = 1
        self._lock = threading.Lock()
        self.connect()

    @property
    def connected(self):
        return self._device is not None

    @property
    def connected_port(self):
        device = self._device
        return device.port if device is not None else None

    def _close(self):
        device, self._device = self._device, None
        self._buffer.clear()
        self._discarding = False
        self._close_device(device)

    @staticmethod
    def _close_device(device):
        if device is not None:
            try:
                device.close()
            except (OSError, serial.SerialException):
                pass

    def close(self):
        """Release the port. Subsequent sends return UNAVAILABLE."""
        with self._lock:
            self._close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def _open(self, port):
        device = serial.Serial(
            port=None, baudrate=115200, bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
            timeout=min(0.05, self.timeout), write_timeout=self.timeout,
            xonxoff=False, rtscts=False, dsrdtr=False,
        )
        try:
            # Set these before open to reduce ESP32 resets. Some drivers still
            # reset it, so allow boot to finish before a read-only handshake.
            device.dtr = False
            device.rts = False
            device.port = port
            device.open()
            time.sleep(1.5)
            device.reset_input_buffer()
            return device
        except (OSError, serial.SerialException, ValueError):
            self._close_device(device)
            raise

    def connect(self):
        """Reconnect explicitly; return False and warn once if discovery fails.

        Automatic discovery probes FTDI ports with status, never toggle. When
        multiple compatible controllers respond, select one with an explicit
        port rather than choosing an arbitrary table.
        """
        with self._lock:
            self._close()
            matches = []
            last_error = "no compatible FTDI controller found"
            try:
                ports = ([self.port] if self.port is not None else
                         sorted({p.device for p in list_ports.comports()
                                 if p.vid == 0x0403}))
                for port in ports:
                    device = None
                    try:
                        device = self._open(port)
                        self._buffer.clear()
                        self._discarding = False
                        reply = self._exchange(device, "status")
                        if not (reply["ok"] and
                                reply.get("firmware") == _FIRMWARE and
                                type(reply.get("protocol_version")) is int and
                                reply["protocol_version"] == _PROTOCOL_VERSION and
                                type(reply.get("has_signal")) is bool and
                                reply.get("state") in
                                ("ready", "not_learned", "learning", "confirm")):
                            raise _ExchangeError("incompatible firmware identity/version")
                        matches.append(device)
                        device = None  # Ownership moved into matches.
                    except (OSError, serial.SerialException, ValueError,
                            _ExchangeError) as exc:
                        last_error = f"{port}: {exc}"
                    finally:
                        self._close_device(device)
                if len(matches) == 1:
                    self._device = matches.pop()
                    self._buffer.clear()
                    self._discarding = False
                    return True
                if len(matches) > 1:
                    last_error = "multiple compatible controllers; specify port explicitly"
            except (OSError, serial.SerialException, ValueError) as exc:
                last_error = str(exc)
            finally:
                for device in matches:
                    self._close_device(device)
            _LOGGER.warning("Turntable controller unavailable: %s", last_error)
            return False

    def _read_message(self, device, deadline):
        while time.monotonic() < deadline:
            newline = self._buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self._buffer[:newline])
                del self._buffer[:newline + 1]
                if self._discarding:
                    self._discarding = False
                    continue
                if len(line) > _MAX_LINE:
                    continue
                try:
                    message = json.loads(line)
                except (ValueError, UnicodeDecodeError):
                    continue  # ROM boot text and unrelated noise are not replies.
                if isinstance(message, dict):
                    return message
                continue
            if len(self._buffer) > _MAX_LINE:
                self._buffer.clear()
                self._discarding = True
            device.timeout = min(0.05, max(0.001, deadline - time.monotonic()))
            self._buffer.extend(device.read(256))
        raise _ExchangeError("response timed out")

    def _exchange(self, device, command):
        request_id = self._next_id
        self._next_id = 1 if request_id == 0x7FFFFFFF else request_id + 1
        payload = (json.dumps({"id": request_id, "cmd": command},
                              separators=(",", ":")) + "\n").encode("ascii")
        deadline = time.monotonic() + self.timeout
        if device.write(payload) != len(payload):
            raise _ExchangeError("incomplete command write")
        # No flush or retransmit: a timed-out write can already have reached IR.
        while time.monotonic() < deadline:
            message = self._read_message(device, deadline)
            if (message.get("type") != "response" or
                    type(message.get("id")) is not int or
                    message["id"] != request_id):
                continue
            if type(message.get("ok")) is not bool:
                raise _ExchangeError("malformed response")
            expected = {"status": "status", "ping": "pong", "toggle": "sent"}[command]
            if message["ok"] and message.get("event") != expected:
                raise _ExchangeError("unexpected response event")
            if not message["ok"] and not isinstance(message.get("error"), str):
                raise _ExchangeError("missing firmware error code")
            return message
        raise _ExchangeError("response timed out")

    def status(self):
        """Return the status response, or None when unavailable or unsuccessful."""
        with self._lock:
            if self._device is None:
                return None
            try:
                reply = self._exchange(self._device, "status")
                if reply["ok"]:
                    return reply
                _LOGGER.warning("Turntable status rejected: %s", reply["error"])
            except (OSError, serial.SerialException, ValueError, _ExchangeError) as exc:
                self._close()
                _LOGGER.warning("Turntable connection lost: %s", exc)
            return None

    def send(self):
        """Transmit one toggle; return SendResult and never retry automatically."""
        with self._lock:
            if self._device is None:
                return SendResult.UNAVAILABLE
            try:
                reply = self._exchange(self._device, "toggle")
                if reply["ok"]:
                    return SendResult.SENT
                _LOGGER.warning("Turntable toggle rejected: %s", reply["error"])
                return SendResult.FAILED
            except (OSError, serial.SerialException, ValueError, _ExchangeError) as exc:
                self._close()
                _LOGGER.warning("Turntable toggle outcome unknown: %s; not retried", exc)
                return SendResult.UNKNOWN


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Explicit serial port; otherwise discover FTDI devices")
    parser.add_argument("--timeout", type=float, default=3.0,
                        help="Request deadline in seconds (default: 3)")
    parser.add_argument("command", choices=("status", "toggle"))
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    with TurntableRemote(port=args.port, timeout=args.timeout) as remote:
        if not remote.connected:
            return 2
        if args.command == "status":
            reply = remote.status()
            if reply is None:
                return 1
            print(json.dumps(reply, indent=2))
            return 0
        result = remote.send()
        print(result.value)
        return {SendResult.SENT: 0, SendResult.UNAVAILABLE: 2}.get(result, 1)


if __name__ == "__main__":
    raise SystemExit(main())
