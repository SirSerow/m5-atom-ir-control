import contextlib
import io
import json
from pathlib import Path
import runpy
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
import turntable_remote as module
from turntable_remote import SendResult, TurntableRemote


def status_reply(request, **changes):
    result = {"type": "response", "id": request["id"], "ok": True,
              "event": "status", "firmware": "atom-turntable-ir",
              "protocol_version": 2, "has_signal": True, "state": "ready"}
    result.update(changes)
    return result


def sent_reply(request, **changes):
    result = {"type": "response", "id": request["id"], "ok": True,
              "event": "sent", "sent": 1}
    result.update(changes)
    return result


class FakeDevice:
    """A byte-stream device: writes enqueue replies; reads may fragment them."""

    def __init__(self, factory, **settings):
        self.factory = factory
        self.settings = settings
        self.port = None
        self.timeout = settings["timeout"]
        self.is_open = False
        self.closed = False
        self.pending = bytearray()
        self.requests = []

    @property
    def config(self):
        return self.factory.configs[self.port]

    def open(self):
        if self.port not in self.factory.configs:
            raise serial.SerialException("missing")
        if self.config.get("open_error"):
            raise serial.SerialException("port busy")
        self.is_open = True
        # Opening must set these before toggling the physical port.
        assert self.dtr is False and self.rts is False

    def close(self):
        self.closed = True
        self.is_open = False

    def reset_input_buffer(self):
        self.pending.clear()

    def write(self, payload):
        request = json.loads(payload)
        self.requests.append(request)
        if request["cmd"] == "toggle" and self.config.get("write_error"):
            raise serial.SerialTimeoutException("write timed out")
        if request["cmd"] == "toggle" and self.config.get("short_write"):
            return len(payload) - 1
        callback = self.config.get("respond")
        replies = (callback(request) if callback else
                   [status_reply(request) if request["cmd"] == "status"
                    else sent_reply(request)])
        for reply in replies:
            if isinstance(reply, bytes):
                self.pending.extend(reply)
            else:
                self.pending.extend(json.dumps(reply).encode("ascii") + b"\n")
        return len(payload)

    def read(self, count):
        if (self.requests[-1]["cmd"] == "toggle" and
                self.config.get("read_error")):
            raise serial.SerialException("USB disconnected")
        count = min(count, self.config.get("chunk_size", count))
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result


class FakeFactory:
    def __init__(self, configs):
        self.configs = configs
        self.devices = []

    def __call__(self, **settings):
        device = FakeDevice(self, **settings)
        self.devices.append(device)
        return device

    @property
    def toggles(self):
        return [request for device in self.devices for request in device.requests
                if request["cmd"] == "toggle"]


class RemoteTests(unittest.TestCase):
    def setUp(self):
        self.configs = {"COM4": {}}
        self.factory = FakeFactory(self.configs)
        self.ports = [SimpleNamespace(device="COM4", vid=0x0403)]
        self.patches = [patch.object(module.serial, "Serial", self.factory),
                        patch.object(module.list_ports, "comports", lambda: self.ports),
                        patch.object(module.time, "sleep")]
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)

    def remote(self, **kwargs):
        remote = TurntableRemote(timeout=0.02, **kwargs)
        self.addCleanup(remote.close)
        return remote

    def test_discovery_retains_connection_and_uses_identity_only(self):
        remote = self.remote()
        self.assertTrue(remote.connected)
        self.assertEqual(remote.connected_port, "COM4")
        device = self.factory.devices[0]
        self.assertEqual([r["cmd"] for r in device.requests], ["status"])
        self.assertEqual(remote.send(), SendResult.SENT)
        self.assertEqual(remote.send(), SendResult.SENT)
        self.assertIsNotNone(remote.status())
        self.assertEqual(len(self.factory.devices), 1)
        ids = [r["id"] for r in device.requests]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(len(self.factory.toggles), 2)

    def test_no_device_warns_once_and_sends_are_noops(self):
        self.ports.clear()
        with self.assertLogs(module._LOGGER, level="WARNING") as logs:
            remote = self.remote()
        self.assertEqual(len(logs.output), 1)
        with self.assertNoLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNAVAILABLE)
            self.assertEqual(remote.send(), SendResult.UNAVAILABLE)
            self.assertIsNone(remote.status())
        self.assertFalse(self.factory.devices)

    def test_multiple_matches_are_closed_without_toggle(self):
        self.configs["COM5"] = {}
        self.ports.append(SimpleNamespace(device="COM5", vid=0x0403))
        with self.assertLogs(module._LOGGER, level="WARNING") as logs:
            remote = self.remote()
        self.assertIn("multiple", logs.output[0])
        self.assertFalse(remote.connected)
        self.assertTrue(all(d.closed for d in self.factory.devices))
        self.assertFalse(self.factory.toggles)

    def test_explicit_port_can_use_non_ftdi_bridge(self):
        self.ports.clear()
        self.assertTrue(self.remote(port="COM4").connected)

    def test_non_ftdi_ports_are_not_probed(self):
        self.ports.append(SimpleNamespace(device="COM8", vid=0x1234))
        self.assertTrue(self.remote().connected)
        self.assertEqual([d.port for d in self.factory.devices], ["COM4"])

    def test_unrelated_and_busy_ports_do_not_hide_one_valid_controller(self):
        self.configs["COM3"] = {"open_error": True}
        self.configs["COM5"] = {"respond": lambda r: [status_reply(r, firmware="other")]}
        self.ports.extend([SimpleNamespace(device="COM3", vid=0x0403),
                           SimpleNamespace(device="COM5", vid=0x0403)])
        self.assertEqual(self.remote().connected_port, "COM4")
        self.assertTrue(all(d.closed for d in self.factory.devices if d.port != "COM4"))

    def test_incompatible_firmware_warns_and_disables(self):
        self.configs["COM4"]["respond"] = lambda r: [status_reply(r, protocol_version=1)]
        with self.assertLogs(module._LOGGER, level="WARNING"):
            remote = self.remote()
        self.assertFalse(remote.connected)
        self.assertTrue(self.factory.devices[0].closed)
        self.assertFalse(self.factory.toggles)

    def test_not_learned_controller_can_connect(self):
        self.configs["COM4"]["respond"] = lambda r: [status_reply(r, state="not_learned", has_signal=False)]
        self.assertTrue(self.remote().connected)

    def test_fragmented_reads_and_unrelated_lines(self):
        self.configs["COM4"]["chunk_size"] = 3
        remote = self.remote()
        def respond(request):
            return [b"ROM boot text\n{broken\n[]\n\xff\n",
                    {"type": "event", "event": "sent"},
                    sent_reply(request, id=request["id"] + 100),
                    sent_reply(request, id=True), sent_reply(request)]
        self.configs["COM4"]["respond"] = respond
        self.assertEqual(remote.send(), SendResult.SENT)
        self.assertEqual(len(self.factory.toggles), 1)

    def test_oversized_noise_recovers_at_newline(self):
        remote = self.remote()
        self.configs["COM4"]["respond"] = lambda r: [b"x" * 6000 + b"\n", sent_reply(r)]
        self.assertEqual(remote.send(), SendResult.SENT)

    def test_explicit_rejection_is_failed_and_keeps_connection(self):
        remote = self.remote()
        self.configs["COM4"]["respond"] = lambda r: [
            {"type": "response", "id": r["id"], "ok": False, "error": "not_learned"}]
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.FAILED)
        self.assertTrue(remote.connected)
        self.assertEqual(len(self.factory.toggles), 1)

    def test_timeout_unknown_and_no_retry_or_automatic_reconnect(self):
        remote = self.remote()
        self.configs["COM4"]["respond"] = lambda r: []
        with self.assertLogs(module._LOGGER, level="WARNING") as logs:
            self.assertEqual(remote.send(), SendResult.UNKNOWN)
        self.assertEqual(len(logs.output), 1)
        self.assertFalse(remote.connected)
        with self.assertNoLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNAVAILABLE)
        self.assertEqual(len(self.factory.toggles), 1)
        self.assertEqual(len(self.factory.devices), 1)

    def test_usb_disconnect_is_unknown_and_closes_port(self):
        remote = self.remote()
        self.configs["COM4"]["read_error"] = True
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNKNOWN)
        self.assertTrue(self.factory.devices[0].closed)
        self.assertEqual(len(self.factory.toggles), 1)

    def test_write_timeout_never_retries(self):
        remote = self.remote()
        self.configs["COM4"]["write_error"] = True
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNKNOWN)
        self.assertEqual(len(self.factory.toggles), 1)

    def test_short_write_never_retries(self):
        remote = self.remote()
        self.configs["COM4"]["short_write"] = True
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNKNOWN)
        self.assertEqual(len(self.factory.toggles), 1)

    def test_matching_malformed_reply_invalidates_connection(self):
        remote = self.remote()
        self.configs["COM4"]["respond"] = lambda r: [sent_reply(r, ok="yes")]
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNKNOWN)
        self.assertFalse(remote.connected)

    def test_wrong_success_event_is_not_accepted(self):
        remote = self.remote()
        self.configs["COM4"]["respond"] = lambda r: [sent_reply(r, event="pong")]
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNKNOWN)

    def test_manual_reconnection_does_not_resend_old_toggle(self):
        remote = self.remote()
        self.configs["COM4"]["respond"] = lambda r: []
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(remote.send(), SendResult.UNKNOWN)
        del self.configs["COM4"]["respond"]
        self.assertTrue(remote.connect())
        self.assertEqual(len(self.factory.toggles), 1)
        self.assertEqual(remote.send(), SendResult.SENT)
        self.assertEqual(len(self.factory.toggles), 2)

    def test_status_failure_disables_connection(self):
        remote = self.remote()
        self.configs["COM4"]["respond"] = lambda r: []
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertIsNone(remote.status())
        self.assertEqual(remote.send(), SendResult.UNAVAILABLE)
        self.assertFalse(self.factory.toggles)

    def test_context_manager_closes_even_on_application_exception(self):
        remote = self.remote()
        with self.assertRaises(RuntimeError):
            with remote:
                raise RuntimeError("recorder failure")
        self.assertFalse(remote.connected)
        self.assertEqual(remote.send(), SendResult.UNAVAILABLE)

    def test_import_has_no_device_side_effects(self):
        before = len(self.factory.devices)
        runpy.run_path(str(Path(module.__file__)))
        self.assertEqual(len(self.factory.devices), before)

    def test_invalid_timeouts_do_not_open_ports(self):
        for value in (0, -1, float("nan"), float("inf")):
            with self.subTest(value=value), self.assertRaises(ValueError):
                TurntableRemote(timeout=value)
        self.assertFalse(self.factory.devices)

    def test_cli_status_and_toggle(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(module.main(["--port", "COM4", "status"]), 0)
        self.assertEqual(json.loads(output.getvalue())["firmware"], "atom-turntable-ir")
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(module.main(["toggle"]), 0)
        self.assertEqual(output.getvalue().strip(), "sent")
        self.assertEqual(len(self.factory.toggles), 1)

    def test_cli_unavailable_exit_code(self):
        self.ports.clear()
        with self.assertLogs(module._LOGGER, level="WARNING"):
            self.assertEqual(module.main(["toggle"]), 2)

    def test_cli_failure_and_unknown_exit_codes(self):
        class CliRemote:
            connected = True
            result = SendResult.FAILED
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def send(self): return self.result
            def status(self): return None
        instance = CliRemote()
        with patch.object(module, "TurntableRemote", return_value=instance):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(module.main(["toggle"]), 1)
                instance.result = SendResult.UNKNOWN
                self.assertEqual(module.main(["toggle"]), 1)
                self.assertEqual(module.main(["status"]), 1)


if __name__ == "__main__":
    unittest.main()
