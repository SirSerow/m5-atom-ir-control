"""Opt-in hardware smoke check. IR is sent only with --toggle (one attempt)."""

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from turntable_remote import SendResult, TurntableRemote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--toggle", action="store_true",
                        help="Transmit exactly one toggle; observe the actual table yourself")
    args = parser.parse_args()
    with TurntableRemote(port=args.port) as remote:
        if not remote.connected:
            return 2
        before = remote.status()
        after = remote.status()
        if before is None or after is None:
            return 1
        checks = (
            before["firmware"] == "atom-turntable-ir",
            before["protocol_version"] == 2,
            before["state"] in ("ready", "not_learned", "learning", "confirm"),
            type(before["has_signal"]) is bool,
            before["sent"] == after["sent"],
        )
        print(json.dumps({"status": after, "read_only_checks_passed": all(checks)}, indent=2))
        if not all(checks):
            return 1
        if args.toggle:
            result = remote.send()  # Deliberately no retry or toggle loop.
            print(json.dumps({"toggle_result": result.value,
                              "table_motion_verified": False}))
            return 0 if result == SendResult.SENT else 1
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
