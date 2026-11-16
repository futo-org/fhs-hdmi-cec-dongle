#!/usr/bin/env python3
"""Reboot the dongle into the ROM USB DFU bootloader with the ENTER_DFU vendor request.

    enter_dfu.py
"""

import sys

import libusb_package
import usb.core
import usb.util

# driver/fhs-cec-proto.h
USB_VID = 0x3A4C
USB_PID = 0x4663
REQ_ENTER_DFU = 0x09

VENDOR_OUT_TO_DEVICE = usb.util.CTRL_OUT | usb.util.CTRL_TYPE_VENDOR | usb.util.CTRL_RECIPIENT_DEVICE

dev = libusb_package.find(idVendor=USB_VID, idProduct=USB_PID)
if dev is None:
    sys.exit("no dongle on USB: bridge the DFU jumper and replug instead")

try:
    dev.ctrl_transfer(VENDOR_OUT_TO_DEVICE, REQ_ENTER_DFU)
except usb.core.USBError as e:
    sys.exit("ENTER_DFU failed ({}): firmware too old? bridge the DFU jumper and replug".format(e))
