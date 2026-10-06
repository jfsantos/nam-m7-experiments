#!/usr/bin/env python3
"""Flash an experiment to a Daisy Seed over USB DFU and print its report.

    daisy.py flash-run build/01-specialize.bin --id 01-specialize
    daisy.py flash build/01-specialize.bin
    daisy.py run [--id 01-specialize]

Needs dfu-util and pyserial (pip install pyserial). The experiments are
BOOT_QSPI apps: the Daisy bootloader must be in the board's internal flash.

Getting into DFU mode: if an experiment (or any firmware that understands it)
is running, this script sends 'D' over USB serial and the firmware reboots
into the bootloader. Otherwise press RESET, then BOOT while the LED pulses.
"""
import argparse
import glob
import subprocess
import sys
import time

QSPI_ADDRESS = '0x90040000'
DFU_ID = '0483:df11'


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def dfu_devices():
    r = subprocess.run(['dfu-util', '-l'], capture_output=True, text=True)
    return [l for l in r.stdout.splitlines() if l.startswith('Found DFU') and DFU_ID in l]


def serial_ports():
    try:
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports() if p.vid == 0x0483 and p.pid != 0xDF11]
    except ImportError:
        ports = []
    return ports or sorted(glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*'))


def open_port(port):
    import serial
    s = serial.Serial(port, baudrate=115200, timeout=0.2)
    try:
        s.dtr = True  # some CDC stacks only send once the host asserts DTR
    except OSError:
        pass
    return s


def enter_dfu(args):
    if dfu_devices():
        return
    for port in ([args.port] if args.port else serial_ports()):
        log(f'asking the firmware on {port} to reboot into the bootloader')
        try:
            s = open_port(port)
            time.sleep(0.2)
            s.write(b'D')
            s.flush()
            time.sleep(0.3)
            s.close()
        except Exception as e:  # noqa: BLE001 - the port vanishes as the board reboots
            log(f'  ({e})')
        for _ in range(40):
            if dfu_devices():
                return
            time.sleep(0.25)
    log('No DFU device. On the Daisy: press RESET, then press BOOT while the LED pulses.')
    log('(No Daisy bootloader yet? Hold BOOT, press RESET, then run "make program-boot" '
        'in any experiment folder.)')
    deadline = time.time() + args.dfu_wait
    while time.time() < deadline:
        if dfu_devices():
            return
        time.sleep(0.5)
    raise SystemExit('timed out waiting for the DFU device')


def flash(args, binary):
    enter_dfu(args)
    devs = dfu_devices()
    if any('0x08000000' in d for d in devs) and not any('0x90' in d for d in devs):
        raise SystemExit('The DFU device is the STM32 ROM bootloader, not the Daisy bootloader.\n'
                         'Install the Daisy bootloader once with "make program-boot", or press RESET.')
    log(f'flashing {binary}')
    r = subprocess.run(['dfu-util', '-a', '0', '-s', f'{QSPI_ADDRESS}:leave', '-D', binary, '-d', f',{DFU_ID}'],
                       capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode != 0 and 'Download done' not in out and 'File downloaded successfully' not in out:
        sys.stderr.write(out[-3000:])
        raise SystemExit('dfu-util failed')


def connect(args):
    """Wait for an experiment's serial port; returns (port, info)."""
    deadline = time.time() + args.boot_wait
    last = None
    while time.time() < deadline:
        for port in ([args.port] if args.port else serial_ports()):
            try:
                s = open_port(port)
            except Exception as e:  # noqa: BLE001 - not enumerated yet
                last = e
                continue
            info = {}
            t_end = time.time() + 3
            s.write(b'I')
            while time.time() < t_end:
                line = s.readline().decode(errors='replace').strip()
                if not line:
                    s.write(b'I')
                    continue
                if line.startswith('EXP_INFO'):
                    info = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
                elif line == 'EXP_READY':
                    break
            if info and (not args.id or info.get('id') == args.id):
                return s, info
            s.close()
            last = f'{port} runs {info.get("id")}, expected {args.id}' if info else f'{port}: no answer'
        time.sleep(0.5)
    raise SystemExit(f'no experiment answered on USB serial ({last})')


def run(args):
    s, info = connect(args)
    log(f'{info.get("id")} on {s.port}, {int(info.get("hz", 0)) / 1e6:.0f} MHz')
    s.reset_input_buffer()
    s.write(b'R')
    deadline = time.time() + args.run_timeout
    while time.time() < deadline:
        line = s.readline().decode(errors='replace').rstrip('\r\n')
        if line == 'EXP_DONE':
            return
        if line:
            print(line, flush=True)
    raise SystemExit('timed out waiting for the report')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=['flash', 'run', 'flash-run'])
    ap.add_argument('binary', nargs='?')
    ap.add_argument('--id', help='expected experiment id (refuse to run another one)')
    ap.add_argument('--port', help='USB serial port (default: autodetect)')
    ap.add_argument('--dfu-wait', type=float, default=60)
    ap.add_argument('--boot-wait', type=float, default=20)
    ap.add_argument('--run-timeout', type=float, default=300)
    args = ap.parse_args()
    if args.command in ('flash', 'flash-run'):
        if not args.binary:
            ap.error('flash needs a .bin file')
        flash(args, args.binary)
    if args.command in ('run', 'flash-run'):
        try:
            import serial  # noqa: F401
        except ImportError:
            raise SystemExit('pyserial is missing: pip install pyserial')
        run(args)


if __name__ == '__main__':
    main()
