"""USB CLI for the ATOM Lite + U002 JSON firmware."""
import argparse
import json
from pathlib import Path
import sys
import time

import serial


def connect(port):
    device = serial.Serial(port=None, baudrate=115200, timeout=0.2,
                           write_timeout=3)
    device.dtr = False
    device.rts = False
    device.port = port
    device.open()
    # Some USB drivers reset the ESP32 on open. Let boot output finish.
    time.sleep(1.5)
    device.reset_input_buffer()
    return device


def read_event(device):
    line = device.readline()
    try:
        result = json.loads(line)
        return result if isinstance(result, dict) else None
    except (ValueError, UnicodeDecodeError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM4')
    parser.add_argument('--timeout', type=float, default=5)
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('ping', 'status', 'help', 'replay', 'selftest'):
        sub.add_parser(name)
    listen = sub.add_parser('listen')
    listen.add_argument('--seconds', type=float, default=30)
    listen.add_argument('--save', type=Path, help='Save the latest complete capture as JSON')
    send = sub.add_parser('send')
    send.add_argument('protocol')
    send.add_argument('code', help='Hex code, e.g. 0x20DF10EF')
    send.add_argument('--bits', type=int)
    send.add_argument('--repeat', type=int, default=0)
    raw = sub.add_parser('raw')
    raw.add_argument('file', type=Path, help='Capture JSON or JSON list of microsecond durations')
    raw.add_argument('--khz', type=int, default=38)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')

    request = {'cmd': args.command}
    if args.command == 'send':
        request.update(protocol=args.protocol.upper(), code=args.code, repeat=args.repeat)
        if args.bits is not None:
            request['bits'] = args.bits
    elif args.command == 'raw':
        capture = json.loads(args.file.read_text(encoding='utf-8'))
        pulses = capture.get('pulses') if isinstance(capture, dict) else capture
        if not isinstance(pulses, list):
            parser.error('Capture must contain a pulses array')
        request.update(pulses=pulses, khz=args.khz)

    with connect(args.port) as device:
        if args.command == 'listen':
            if args.seconds <= 0:
                parser.error('--seconds must be positive')
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                event = read_event(device)
                if event is None:
                    continue
                print(json.dumps(event), flush=True)
                if (args.save and event.get('event') == 'received'
                        and not event.get('truncated') and not event.get('repeat')):
                    args.save.write_text(json.dumps(event, indent=2) + '\n', encoding='utf-8')
            return 0

        device.write((json.dumps(request, separators=(',', ':')) + '\n').encode('ascii'))
        expected = {'ping': 'pong', 'send': 'sent', 'raw': 'sent',
                    'replay': 'sent'}.get(args.command, args.command)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            event = read_event(device)
            if event is None:
                continue
            print(json.dumps(event), flush=True)
            if event.get('event') == 'error':
                return 1
            if event.get('event') == expected:
                if args.command == 'selftest' and not event.get('optical_loopback'):
                    return 2
                return 0
        print('Timed out waiting for firmware response', file=sys.stderr)
        return 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (serial.SerialException, OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
