# fhs-cec web

Browser bench for the dongle over WebUSB. Sends any CEC opcode, runs multi-step procedures
(address allocation, One Touch Play, bus scan, remote keys) and logs the bus.

    mise run dev        # http://localhost:5173 in Chrome or Edge

- Speaks `../driver/fhs-cec-proto.h` directly; `src/lib/usb/proto.ts` mirrors it.
- Linux: the `fhs-cec` kernel driver holds the interface. `sudo rmmod fhs_cec` first, and give
  your user access to `3a4c:4663`.
- Allocate an address before sending. Until then frames go out as Unregistered (15).
- Auto-reply answers the requests a TV sends to keep a source listed. Turn it off to watch raw
  traffic.
