"""Run serial protocol and optical checks against the actual connected board."""
import argparse
import json
import time

from irctl import connect, read_event


def response(device, command, expected, timeout=5):
    payload = command if isinstance(command, str) else json.dumps(command)
    device.write((payload + '\n').encode('ascii'))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        event = read_event(device)
        if event is None:
            continue
        print(json.dumps(event), flush=True)
        if event.get('event') == expected:
            return event
        if event.get('event') == 'error' and expected != 'error':
            raise AssertionError(event)
    raise AssertionError(f'Timeout waiting for {expected}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM4')
    args = parser.parse_args()
    with connect(args.port) as device:
        initial = response(device, {'cmd': 'status'}, 'status')
        assert initial['firmware'] == 'atom-u002-usb-ir'
        assert initial['flash_bytes'] == 4194304
        assert initial['tx_pin'] == 26 and initial['rx_pin'] == 32
        response(device, {'cmd': 'ping'}, 'pong')
        response(device, {'cmd': 'help'}, 'help')
        invalid = [
            '{broken',
            {'cmd': 'nonexistent'},
            {'cmd': 'send', 'protocol': 'NEC', 'code': 'not-hex'},
            {'cmd': 'send', 'protocol': 'NEC', 'code': '0x100', 'bits': 8},
            {'cmd': 'send', 'protocol': 'NEC', 'code': '1', 'repeat': 11},
            {'cmd': 'send', 'protocol': 'DAIKIN', 'code': '1'},
            {'cmd': 'raw', 'pulses': []},
            {'cmd': 'raw', 'pulses': [-1, 500]},
            {'cmd': 'raw', 'pulses': [0, 0]},
            {'cmd': 'raw', 'pulses': [100, 100], 'khz': 0},
            {'cmd': 'raw', 'pulses': [65535] * 32},
        ]
        for command in invalid:
            response(device, command, 'error')
        response(device, 'x' * 14001, 'error')
        response(device, {'cmd': 'ping'}, 'pong')
        response(device, {'cmd': 'send', 'protocol': 'NEC',
                          'code': '0xC03F12ED', 'bits': 32}, 'sent')
        optical = response(device, {'cmd': 'selftest'}, 'selftest')
        code = 0xC03F12ED
        pulses = [9000, 4500]
        for bit in range(31, -1, -1):
            pulses.extend([560, 1690 if code & (1 << bit) else 560])
        pulses.append(560)
        response(device, {'cmd': 'raw', 'khz': 38, 'pulses': pulses}, 'sent')
        if optical['optical_loopback']:
            response(device, {'cmd': 'replay'}, 'sent')
        time.sleep(2)
        final = response(device, {'cmd': 'status'}, 'status')
        assert final['uptime_ms'] > initial['uptime_ms']
        assert final['sent'] == initial['sent'] + 3 + int(optical['optical_loopback'])
        assert final['free_heap'] > 50000
        print(json.dumps({'checks': 'passed', 'invalid_commands_rejected': len(invalid) + 1,
                          'optical_loopback': optical['optical_loopback']}))


if __name__ == '__main__':
    main()
