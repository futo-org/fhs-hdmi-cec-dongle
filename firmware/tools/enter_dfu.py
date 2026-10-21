#!/usr/bin/env python3
"""Send `dfu` to the dongle's debug console so it reboots into the ROM bootloader.

    enter_dfu.py [port|hwgrep://<regexp>] [baud]
"""

import sys

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "hwgrep://1209:0001"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

try:
    with serial.serial_for_url(port, baud, timeout=1) as s:
        s.write(b"\r\ndfu\r\n")
        s.flush()
except (serial.SerialException, OSError) as e:
    sys.exit("no debug console ({}): bridge the DFU jumper and replug instead".format(e))
