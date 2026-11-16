# fhs-hdmi-cec firmware

Firmware for the FHS CEC dongle (`hardware/pcb`, STM32F072CBU6, Cortex-M0). STM32Cube HAL and
TinyUSB, both vendored in `Drivers/`.

The dongle is a USB HDMI-CEC adapter for the Linux kernel's CEC framework. The `fhs-cec` driver
(`../driver`) enables it, sets its logical addresses and sends frames; the firmware moves frames
between the bus and USB and reports the result of every transmit. It does nothing on the bus
until the driver enables it. The kernel and userspace own all CEC policy: address allocation,
replies, routing, retries.

The wire protocol is `../driver/fhs-cec-proto.h`, included by both sides.

## Quick start

    mise install            # one-time: fetch the pinned toolchain
    mise run build          # configure + build (Debug preset)
    mise run load           # build, flash over SWD, reset
    mise run flash          # build, flash over USB DFU
    mise run monitor        # debug console over USB (Debug builds only)
    mise tasks              # full list

`PRESET=Release mise run load` for the Release preset. Artifacts land in
`build/<preset>/fhs-hdmi-controller.elf` + `.map`.

Flashing is SWD (`SWDIO`/`SWCLK`/`NRST` test points) with an ST-LINK and `probe-rs`, chip
`STM32F072CB`. The `DFU` jumper (BOOT0) starts the ROM USB DFU bootloader instead.

`mise run flash` needs no probe and no jumper: it sends the `ENTER_DFU` vendor request, the
running firmware (Debug or Release) resets into the ROM bootloader, and `dfu-util` writes the
image. A blank chip has no firmware to answer, so it needs the jumper.

On Linux the ST-LINK needs the udev rule once:

    sudo cp tools/49-stlink.rules /etc/udev/rules.d/
    sudo udevadm control --reload && sudo udevadm trigger

## Presets

| Preset | `FHS_CEC_DEBUG_SHELL` | USB interfaces | Size |
|---|---|---|---|
| Debug (`-O0`) | ON | CEC adapter + CDC-ACM debug console | ~101 KiB |
| Release (`-Os`) | OFF | CEC adapter only | ~17 KiB |

The driver binds the CEC interface in both. Release compiles out the console, frame decoder,
line diagnostics and fault injection.

`FHS_CEC_EDID` (default ON) builds the DDC EDID reader. Turn it off for boards without DDC wired
to the MCU: `EDID=OFF mise run build`. The driver then falls back to the stored address.

## Layout

| Path | What |
|---|---|
| `Src/main.c` | wiring only |
| `Inc/board.h` | pin map |
| `Src/cec.c` | CEC peripheral, ISR, transmit/receive, receiver watchdogs |
| `Src/adapter.c` | the driver protocol: vendor requests in, events out, fault injection |
| `Src/usb.c` | TinyUSB glue, descriptors, CEC interface class driver, CDC stdio |
| `Src/settings.c` | physical address persisted in the last flash page |
| `Src/system_clock.c` | 48 MHz from HSI48, CRS trimmed against USB SOF |
| `Src/led.c` | status LED |
| `Src/cec_log.c`, `shell.c`, `console.c`, `cec_diag.c` | debug console only |
| `Inc/tusb_config.h` | TinyUSB options |
| `Drivers/` | CMSIS, STM32F0xx HAL, TinyUSB 0.21.0 (device core, CDC, FSDEV port) |
| `cmake/` | generated build config. Only `files.cmake` is yours to edit |
| `tools/` | ST-LINK udev rule, `monitor.py` |
| `docs/` | ST PDFs + `.txt` pdftotext dumps (grep these) |
| `ref/` | reference CEC code, not compiled |

## Hardware

| Net | MCU | Notes |
|---|---|---|
| CEC_MCU | PB8, AF0 | through 100R and Q1 to HDMI pin 13, open drain, internal pull-up. Q1 cuts the pin off the bus when unpowered |
| USB_DM / USB_DP | PA11 / PA12 | USB-C, internal D+ pull-up |
| LED_DRV | PB13 | active high. On while the driver has the adapter enabled |
| HPD_MCU | PA0 | HDMI HPD through U5 Schmitt buffer, unused by firmware |
| UART_TX / UART_RX | PA2 / PA3 | test points, unused by firmware |
| DDC_SCL_MCU / DDC_SDA_MCU | PB6 / PB7 | EDID (`FHS_CEC_EDID`) |
| BOOT0 | | `DFU` jumper |

HDMI 5V is not wired to the MCU on this revision.

No crystal. SYSCLK and the USB clock are HSI48; CRS trims it against the host's 1 kHz SOF. HSI
(8 MHz) stays on for the CEC kernel clock, HSI/244 = 32.787 kHz.

## Behaviour

- **Idle until enabled.** Ack mask empty, nothing forwarded. USB reset or `SET_ENABLE 0` returns to idle.
- **One result per transmit, always.** Every accepted frame produces exactly one `EV_TX_DONE`
  within 1 s (`CEC_TX_TIMEOUT_MS`), checked against `FHS_CEC_TX_BUDGET_MS` at compile time. The
  kernel core gives up after 2.1 s, and on current kernels one timeout during address claiming
  leaves the adapter unconfigured. A wedged transmit is re-initialised and reported as `ARB_LOST`
  (if arbitration was lost) or `ERROR`.
- **One attempt per frame.** The kernel core retries NACKs. Arbitration retries are the
  hardware's own.
- **A new `TRANSMIT` abandons the old one** without a result. The driver drops stale results by
  sequence number.
- **Signal free time is the hardware's.** The STM32 applies the spec values (3/5/7 bit periods)
  itself; the kernel's value is ignored.
- **Receive filter in software.** Full listening mode always (reduced mode wedges, see below).
  Forwarded: broadcast, frames to an acked address, everything with monitor-all.
- **Physical address** is stored in flash (`SET_PHYS_ADDR`), default `1.0.0.0`. The driver reads
  it at probe, after trying the display's EDID (`GET_EDID`).
- **EDID** is read over DDC on request, one block per `GET_EDID`. The read blocks the main loop
  for ~240 ms, so the driver only asks at probe.

## Debug console

Debug builds only. CDC-ACM on the same USB cable, `mise run monitor`. The banner prints when a
terminal opens the port.

```
cec send <dest> [hex...]  one frame, from the host's address, else from 15. No payload = poll
cec listen <own|all>      print frames to us + broadcasts (default), or every frame
cec raw <on|off>          raw wire bytes before each frame (default on)
cec tol <std|ext>         rx timing tolerance
cec reset                 re-init the peripheral
cec dump                  registers, counters, last 32 ISR events
fault <none|nack|late|drop|wedge>   break the next host exchange, one shot
diag line|noise|trace|txtrace|hold  line instrumentation, peripheral suspended
status                    firmware, USB, adapter, ack mask, stored address, counters
```

Every frame, both directions, from the host or the console:

```
[  15.453] RX [04 83]
              TV    -> us     83                 Give Physical Address
[  15.592] TX [4f 84 10 00 04]  ok
              us    -> all    84 10 00 04        Report Physical Address 1.0.0.0 Playback
```

`us` is any address in the ack mask. Results: `ok`, `NO ACK`, `REJECTED by a follower`
(broadcast), `ARBITRATION LOST`, `FAILED, collision on the line`, `FAILED`. A tag in brackets
marks frames that did not come from the host (`local`, `fault`).

A local `cec send` abandons a host frame in flight. Bench only.

`fault` exercises the driver's error paths against the real kernel core:

| fault | effect | driver should |
|---|---|---|
| `nack` | next host frame reports NACK, never sent | let the core retry |
| `late` | next host result arrives after 2.5 s | core times out; driver drops the late result by seq |
| `drop` | next received frame not forwarded | nothing |
| `wedge` | next host result never sent | core times out, next transmit works |

`diag txtrace` settles "is anyone acking us": it samples the pin while the peripheral drives it.
Start bit 3700 us low, a 0 is 1500 us low, a 1 is 600 us, bit period 2400 us:

```
  low    3691 us  start bit
  low    1494 us  data 0        header 0x40 = initiator 4, destination 0
  low    1513 us  data 0        <- ack: TV holding the line past our 610 us
```

`diag noise` on a quiet bus wants `0 edges, 100% high`; thousands of edges means the line is
floating. `diag line` wants `pull-up present` and `while driving: pin reads low`.

## Peripheral quirks (all reproduced, all worked around in `cec.c`)

1. **Reduced listen mode goes deaf.** LSTN=0 with TV and soundbar traffic: after a TV power
   transition the receiver delivers nothing, no flag, registers normal, until re-init. Full
   listening always, address filter in software.
2. **RXOVR is not an overrun.** It fired for someone else's unacked directed frame (TV polling a
   sleeping soundbar). In full mode the same event is RXACKE. Counted as foreign nack, silent.
3. **HAL `__HAL_CEC_CLEAR_FLAG` is `ISR |= bit`**: read-modify-write acks flags raised in
   between. Own ISR, plain `ISR = bit` stores.
4. **HAL `HAL_CEC_SetDeviceAddress` ORs the new address into CFGR** without clearing the old one.
   `cec_set_ack_mask` writes the OAR field itself.
5. **CECEN=0 aborts rx/tx; ISR flags survive.** Every disable/enable path re-arms, and a frame it
   aborted reports `ERROR`.
6. Back-to-back CECEN toggles in the ISR do nothing (32 kHz domain). Re-init is `HAL_CEC_Init`
   from the main loop.

Safety nets in `cec_task()`:

| Net | Trigger | Action |
|---|---|---|
| tx timeout | transmit pending > 1 s | re-init, result `ARB_LOST` or `ERROR` |
| reply watchdog | our directed query (8f 8c 83 46 9f 91) acked, no frame at all in 1.5 s | re-init |
| rx stuck | ≥4 overruns and no frame for 2 s | re-init |

Re-inits count as `recoveries` in `GET_STATUS` (the driver's debugfs status) and `cec dump`.

## Bus behaviour worth knowing (LG 50QNED75BA bench)

For whoever writes the userspace side:

- **HDMI pin 18 (+5V) must be driven.** TVs gate CEC per port on it: without it the TV transmits
  to you and NACKs everything you send.
- **A device that has not reported its physical address is ignored.** The TV acks its commands
  and does nothing.
- **Wait ~10 s after a power command.** The TV NACKs directed frames mid-transition; a burst of
  commands makes a working link look broken. Full LG boot is ~35 s.
- **The TV tramples the first reply after a wake**: it sends its next frame on top of our
  `<Report Physical Address>`, which reads as a rejected broadcast. Retry once after ~150 ms.
- **One Touch Play** is `<Image View On>` to the TV, then `<Active Source>` once that is acked.
- **Volume keys** go to the audio system (LA 5) while it has System Audio Mode on (it broadcasts
  `72 01` after every wake), otherwise to the TV.
- The soundbar (Samsung HW-Q600F, LA 5) is polled by the TV every second while booting and
  every 14 s while asleep.

## Chip facts

- CEC base `0x4000_7800`, APB1, `RCC_APB1ENR.CECEN` (bit 30). NVIC 30 `CEC_CAN_IRQn`, shared with CAN.
- CEC kernel clock: `RCC_CFGR3.CECSW` 0 = HSI/244, 1 = LSE.
- `CEC_CFGR` and `CEC_IER` writable only with `CECEN=0`.
- USB: NVIC 31 `USB_IRQn`, 1 KiB packet memory, internal D+ pull-up (`USB_BCDR.DPPU`).
- Flash: 128 KiB, 2 KiB pages. The last page is `SETTINGS` in the linker script.

## Reference documents

| File | What |
|---|---|
| `docs/RM0091-rev10.pdf` | reference manual. §31 HDMI-CEC p.892-910, §A.13 code examples p.966 |
| `docs/DS-STM32F072.pdf` | datasheet: AF tables 14/15, memory map |
| `docs/AN4066-hdmi-cec-network-stm32f0.pdf` | ST app note: CEC on STM32F0 |
| `docs/ST-training-HDMI-CEC.pdf` | ST training slides on the CEC block |
| `docs/STSW-STM32134_cec_firmware.zip` | AN4066 firmware, unzipped in `ref/stsw-stm32134/` |
| `docs/cec-bringup-plan.md` | the original Nucleo bring-up plan |
| `docs/cec-implementation.md` | one-page map of the CEC code |
