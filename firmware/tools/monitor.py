#!/usr/bin/env python3
"""Serial console for the dongle's USB debug port that survives unplugging it.

pyserial-miniterm does not: its reader thread calls Serial.in_waiting, whose
ioctl raises a bare OSError (ENXIO) once the node is yanked, and miniterm only
catches serial.SerialException -- the thread dies, `alive` stays True, and the
process sits in transmitter_thread.join() forever with the tty in raw mode,
still holding the fd and its flock. Replug then finds the port taken, which
looks like a wedged board and is not one.

Here a disconnect is normal: drop the fd, wait for the port to re-enumerate,
reopen. Discovery is by USB VID:PID, since the node can change name on replug.

    monitor.py [port|hwgrep://<regexp>] [baud]      Ctrl-] quits
"""

import os
import select
import sys
import time

try:
    import termios
    import tty
except ImportError:                 # Windows: falls back to miniterm in main()
    termios = tty = None

import serial
from serial.tools import list_ports

EXIT_CHAR = 0x1D                    # Ctrl-]
RESCAN_S = 0.5                      # device poll while it is missing
SETTLE_S = 1.0                      # let the stale node disappear first


def note(msg):
    sys.stdout.write("-- {} --\n".format(msg))
    sys.stdout.flush()


def resolve(spec):
    """Device node for spec (hwgrep:// regexp or literal path), None if absent."""
    if not spec.startswith("hwgrep://"):
        return spec if os.path.exists(spec) else None
    pattern = spec[len("hwgrep://"):].split("&")[0]
    for port, _desc, _hwid in sorted(list_ports.grep(pattern)):
        return port
    return None


def wait_for(spec):
    said = False
    while True:
        node = resolve(spec)
        if node is not None:
            return node
        if not said:
            note("waiting for {}".format(spec))
            said = True
        time.sleep(RESCAN_S)


def open_port(node, baud):
    """Exclusive (flock) open: a stale monitor cannot split the byte stream."""
    ser = serial.Serial(exclusive=True, timeout=0, write_timeout=1)
    ser.port = node
    ser.baudrate = baud
    ser.open()
    ser.reset_input_buffer()
    return ser


def session(ser):
    """Copy stdin<->port. True if the user quit, False if the device went away."""
    port_fd = ser.fileno()
    tty_fd = sys.stdin.fileno()
    out_fd = sys.stdout.fileno()

    while True:
        try:
            ready, _, _ = select.select([tty_fd, port_fd], [], [])
        except OSError:
            return False

        if port_fd in ready:
            try:
                data = os.read(port_fd, 4096)
            except BlockingIOError:
                data = b"?"           # spurious readiness, not a disconnect
            except OSError:
                return False
            if not data:
                return False          # readable but empty: cable gone
            if data != b"?":
                os.write(out_fd, data)

        if tty_fd in ready:
            try:
                data = os.read(tty_fd, 64)
            except OSError:
                return True
            if not data:
                return True
            quitting = EXIT_CHAR in data
            if quitting:
                data = data[:data.index(EXIT_CHAR)]
            if data:
                try:
                    ser.write(data)
                except (serial.SerialException, OSError):
                    return False
            if quitting:
                return True


def main():
    spec = sys.argv[1] if len(sys.argv) > 1 else os.environ.get(
        "SERIAL_PORT", "hwgrep://3a4c:4663")
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else int(
        os.environ.get("BAUD", "115200"))

    if termios is None:             # Windows: no tty/select console here
        from serial.tools import miniterm
        sys.argv = [sys.argv[0], spec, str(baud), "--exit-char", "29"]
        return miniterm.main()

    tty_fd = sys.stdin.fileno()
    saved = termios.tcgetattr(tty_fd) if os.isatty(tty_fd) else None

    try:
        if saved is not None:
            tty.setcbreak(tty_fd)     # no echo (the firmware echoes), keep Ctrl-C
        while True:
            node = wait_for(spec)
            try:
                ser = open_port(node, baud)
            except (serial.SerialException, OSError) as err:
                note("{}: {}".format(node, err))
                time.sleep(SETTLE_S)
                continue
            note("{} at {}, Ctrl-] quits".format(node, baud))
            quitting = session(ser)
            ser.close()
            if quitting:
                return 0
            note("{} went away".format(node))
            time.sleep(SETTLE_S)
    except KeyboardInterrupt:
        return 0
    finally:
        if saved is not None:
            termios.tcsetattr(tty_fd, termios.TCSADRAIN, saved)


if __name__ == "__main__":
    sys.exit(main())
