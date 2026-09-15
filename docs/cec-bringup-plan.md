# HDMI-CEC bring-up plan

Target: a two-node CEC bus between the NUCLEO-F072RB and a Raspberry Pi 5, driven from the
serial shell on one side and `cec-ctl` on the other.

Status when this was written: `HAL_CEC_MODULE_ENABLED` is set and `stm32f0xx_hal_cec.c`
compiles in, but there is no CEC code in `Src/`, no CEC pins in `Inc/board.h`, and
`CEC_CAN_IRQHandler` is still the weak default from `startup_stm32f0x2xx.S`.

> **Outcome.** Stages 1 to 5 are done and verified against an LG 50QNED75BA. The working
> wiring and command sequence live in the README under "HDMI-CEC bench"; use that, not the
> §2 table below, which is kept for the record. Two corrections this plan got wrong:
> **HDMI pin 18 (+5 V) must be driven**, and the diagnostics in `Src/cec_diag.c` (`diag
> line`, `noise`, `trace`, `txtrace`, `hold`) are what actually found both faults. Stage 3
> merged into Stage 4 because transmit needs an initiator address, and the CEC kernel clock
> ended up in `cec_msp_init` rather than `system_clock.c` so each module owns its own
> peripheral clocks. Two later findings changed the design: the bench is a three-device
> bus (the TV's soundbar sits at logical address 5), and against that traffic the
> hardware's reduced listening mode goes deaf while `rx overrun` turns out to be the
> silicon's way of reporting someone else's unanswered frame. Full listening is now the
> default, the interrupt path no longer uses the HAL, and a reply watchdog re-inits the
> receiver if the TV acknowledges a question and nothing comes back. README, "Known rough
> edges", has the evidence.

## 0. Bench

| Role | What |
|---|---|
| MCU | NUCLEO-F072RB, ST-LINK VCP on `/dev/ttyACM0`, 115200 8N1 |
| Peer | `pi-cec-emulator.local` (10.10.48.84), Pi 5, kernel 6.18, `cec-ctl` from v4l-utils |
| Pi adapters | `/dev/cec0` = HDMI-A-1, `/dev/cec1` = HDMI-A-2, driver `vc4_hdmi` |

Pi adapter capabilities read back as `0x31e`. Decoded:

| Cap | Set | Consequence |
|---|---|---|
| `PHYS_ADDR` | no | userspace **cannot** force the physical address, it comes from EDID |
| `LOG_ADDRS` | yes | logical address claiming works |
| `TRANSMIT` | yes | can send |
| `PASSTHROUGH` | yes | raw frames, no kernel auto-replies |
| `MONITOR_ALL` | no | `--monitor` sees broadcast plus frames addressed to the Pi, nothing else |
| `NEEDS_HPD` | no | CEC keeps running if HPD drops |
| `CONNECTOR_INFO`, `REPLY_VENDOR_ID` | yes | |

Both Pi connectors currently read `disconnected` with physical address `f.f.f.f`. Until the
Pi reads an EDID carrying a CEC physical address it will not claim a logical address and
will not transmit. That is the hardware gate on this whole plan.

## 1. Hardware to gather

| # | Item | Notes |
|---|---|---|
| 1 | micro HDMI (type D) male to HDMI type A female adapter or cable | Pi 5 port HDMI0, nearest the USB-C jack |
| 2 | HDMI type A **pass-through** breakout, male one side, female the other, pins on 0.1" headers or screw terminals | must be pass-through, a plug-only breakout terminates the chain and leaves nowhere for the EDID source |
| 3 | HDMI EDID emulator ("dummy plug"), 1080p or 4K | supplies the EDID the Pi needs; pick one whose EDID carries an HDMI VSDB with a physical address |
| 4 | 27k 1/4 W resistor | pull-up to 3V3 per AN4066 §2, only fit it if the line does not already idle high |
| 5 | Jumper wires, multimeter | |
| 6 | *Optional* logic analyser or scope, 1 ch | CEC start bit is 3.7 ms low, data bits 2.4 ms / 1.5 ms low for 0 / 1 |
| 7 | *Later, not for the bench* small N-channel MOSFET for unpowered isolation | RM0091 Figure 325, see §2 |

Fallbacks if the pass-through breakout is hard to source: a real monitor or TV at the end of
the chain instead of the dummy plug, or a cheap HDMI female-to-female coupler cracked open
and soldered to pins 13 and 17.

Chain: **Pi HDMI0 → micro-to-A adapter → pass-through breakout → dummy plug**, tap at the
breakout.

## 2. Wiring

| HDMI type A pin | Signal | Goes to |
|---|---|---|
| 13 | CEC | NUCLEO **PB8** |
| 17 | DDC/CEC ground | NUCLEO **GND** |
| 18 | +5 V | NUCLEO **5V** — required, see §7 |

The candidate pins are already tabulated under "Chip facts" in the README (PB8 = AF0,
PB10 = AF0, PA5 = AF1; datasheet `docs/ds.txt`, AF tables, confirms all three). The choice
for this bring-up:

| Pin | AF | Verdict |
|---|---|---|
| **PB8** | AF0 | chosen. Free, Arduino D15 on CN5, also on morpho CN10 |
| PB10 | AF0 | spare, Arduino D6, if PB8 ever goes to I2C1 |
| PA5 | AF1 | taken by LD2 (`BOARD_LED_PIN`) |

**Check the PB8/PB9 solder-bridge configuration in UM1724 before wiring.** That manual is
not in `docs/` yet; the README records where to fetch it.

PB8 is 5 V tolerant, so no level shifting. The HDMI2C1-5DIJ ESD part in AN4066 is only
needed for power-off-state HDMI 1.4 compliance; skip it on the bench. Likewise the series
MOSFET that isolates the pin while the board is unpowered (RM0091 Figure 325): not needed
while the NUCLEO is always on, but required before this touches a real HDMI bus, otherwise
an unpowered board drags the CEC line down for every other device on it.

## 3. Hardware acceptance, before any firmware

Run in order. Do not connect PB8 until step 3 passes.

1. Assemble the chain with the STM32 **not** connected. On the Pi:

       ssh pi-cec-emulator.local
       cat /sys/class/drm/card1-HDMI-A-1/status      # expect: connected
       cec-ctl -d /dev/cec0 --show-topology          # expect a real Physical Address, not f.f.f.f

   If it is still `f.f.f.f`, the dummy plug's EDID has no HDMI VSDB. Software fallback:
   drop an EDID blob in `/lib/firmware/edid/` and add
   `video=HDMI-A-1:1920x1080@60e drm.edid_firmware=HDMI-A-1:edid/custom.bin`
   to `/boot/firmware/cmdline.txt`, then reboot.

2. Continuity check: breakout pad labelled CEC really is HDMI pin 13, and the ground pad is
   pin 17.

3. Multimeter, CEC pad to ground, chain powered: expect roughly 3.3 V idle. If it floats or
   sits near 0 V, fit the 27k from the CEC line to NUCLEO 3V3.

4. Connect PB8 and GND. Re-read the idle level. Still ~3.3 V, i.e. the STM32 is not holding
   the line low with a stray pull-down.

## 4. Address map

Neither side does real logical address allocation during bring-up; both are pinned.

| Node | Role | Logical | Physical |
|---|---|---|---|
| Pi 5 | Playback 1 | 4 | whatever the EDID gives, typically 1.0.0.0 |
| STM32 | Playback 2 | 8 | 2.0.0.0, a compile-time constant |

The STM32 has no upstream EDID of its own, so its physical address cannot be discovered and
is simply declared. Proper allocation (poll your own address, walk to the next free one) is
out of scope here; note it as future work.

## 5. Firmware stages

One commit per stage. Each stage ends with a build, a flash, and the listed check.

### Stage 1: clock, pin, peripheral init, no traffic

* `Inc/board.h`: `BOARD_CEC_PORT` GPIOB, `BOARD_CEC_PIN` GPIO_PIN_8, `BOARD_CEC_AF`
  GPIO_AF0_CEC, `BOARD_CEC_IRQn` CEC_CAN_IRQn, `BOARD_CEC_CLK_ENABLE()`,
  `BOARD_CEC_PORT_CLK_ENABLE()`.
* `Src/system_clock.c`: the CEC peripheral runs off its own slow clock, not PCLK. Add a
  `HAL_RCCEx_PeriphCLKConfig` call with `RCC_PERIPHCLK_CEC` and `RCC_CECCLKSOURCE_HSI`
  (HSI/244). `stm32f0xx_hal_rcc_ex.c` is already in the build.
* `Inc/cec.h` + `Src/cec.c`: a file-static `CEC_HandleTypeDef`, `cec_init(void)`, and
  `HAL_CEC_MspInit` doing GPIO `GPIO_MODE_AF_OD` / `GPIO_NOPULL` / AF0 plus the NVIC enable.
  Cortex-M0 priorities are 0..3, so use 3, below the console UART.
  Init fields: `SignalFreeTime` = `CEC_DEFAULT_SFT` (hardware-chosen),
  `Tolerance` = `CEC_STANDARD_TOLERANCE`, `BRERxStop` = `CEC_NO_RX_STOP_ON_BRE`,
  `BREErrorBitGen` / `LBPEErrorBitGen` = no generation,
  `BroadcastMsgNoErrorBitGen` = `CEC_BROADCASTERROR_NO_ERRORBIT_GENERATION`,
  `SignalFreeTimeOption` = `CEC_SFT_START_ON_TXSOM`,
  `ListenMode` = `CEC_REDUCED_LISTENING_MODE`,
  `OwnAddress` = `CEC_OWN_ADDRESS_NONE`, `RxBuffer` = the module's frame buffer.
* `Src/stm32f0xx_it.c`: `CEC_CAN_IRQHandler` calling into `cec.c`, same shape as the
  existing `USART2_IRQHandler`.
* `cmake/files.cmake`: add `Src/cec.c`.
* `Src/main.c`: `cec_init()` after `uart_init()`.

Check: builds clean under `-Wall -Wextra -Wpedantic`, boots, LED still blinks, `status`
still responds, and the CEC line still idles at 3.3 V.

### Stage 2: receive

* Frame buffer of 16 bytes handed to `Init.RxBuffer`; this HAL keeps RX armed in the ISR and
  calls `HAL_CEC_RxCpltCallback(hcec, size)` per frame.
* A small ring of received frames, say 8 entries of 17 bytes plus a length. Filled in the
  callback, drained by `cec_task()` from the main loop.
* **No `printf` from the ISR.** The same reasoning as the UART RX path in `Src/console.c`:
  blocking inside an interrupt at 115200 drops the next byte. Formatting happens in
  `cec_task()`.
* Print as `rx 0f:84:10:00:04` plus a decoded opcode name where one is known.
* `Src/main.c`: `cec_task()` in the loop next to `led_task()`.

Check: on the Pi, `cec-ctl -d /dev/cec0 --to 0xf --standby` (broadcast, so it lands even
with no own address set) shows up on the STM32 console.

### Stage 3: logical address and acknowledge

* `HAL_CEC_SetDeviceAddress(&hcec, CEC_OWN_ADDRESS_8)`.
* Shell: `cec addr <0..15>`.

Check: `cec-ctl -d /dev/cec0 --to 8 --give-device-power-status` is acknowledged; `cec-ctl`
does not report a NACK.

### Stage 4: transmit

* `cec_send(uint8_t dest, const uint8_t *payload, size_t len)` over `HAL_CEC_Transmit_IT`.
  It returns `HAL_BUSY` while a frame is in flight, so surface that rather than spinning.
* `HAL_CEC_TxCpltCallback` sets a flag; the main loop reports `tx ok`.
* Shell: `cec send <dest> <hex bytes>`, e.g. `cec send 4 8f`.

Check: `cec-ctl -d /dev/cec0 --monitor` on the Pi shows the frame.

### Stage 5: minimal responder

Handle, in `cec_task()`, never in the ISR:

| Incoming | Reply |
|---|---|
| `0x83` Give Physical Address | `0x84` Report Physical Address, broadcast, 2.0.0.0 + device type |
| `0x8C` Give Device Vendor ID | `0x87` Device Vendor ID |
| `0x46` Give OSD Name | `0x47` Set OSD Name |
| `0x8F` Give Device Power Status | `0x90` Report Power Status |
| poll (header only, no opcode) | acknowledge only, handled in hardware |

Check: `cec-ctl -d /dev/cec0 -S` lists the STM32 in the topology with its OSD name.

### Stage 6: errors and diagnostics

* `HAL_CEC_ErrorCallback` incrementing per-cause counters from `HAL_CEC_GetError()`:
  `RXOVR`, `BRE`, `SBPE`, `LBPE`, `RXACKE`, `ARBLST`, `TXUDR`, `TXERR`, `TXACKE`.
* Shell: `cec stats`, and `cec listen on|off` to flip between
  `CEC_FULL_LISTENING_MODE` and `CEC_REDUCED_LISTENING_MODE`.

Full listening mode makes the STM32 the bus sniffer. Worth having, because the Pi's vc4
adapter lacks `MONITOR_ALL` and can never sniff frames addressed elsewhere.

## 6. Shell surface when done

Added as rows in the `commands[]` table in `Src/shell.c`, one handler each, matching the
existing `led` pattern:

    cec addr <0..15>          set own logical address
    cec send <dest> <hex...>  transmit one frame
    cec listen on|off         full vs reduced listening
    cec stats                 error counters

## 7. Risks and known traps

* **Pi physical address stays `f.f.f.f`.** Everything else is blocked behind it. Resolve at
  §3 step 1 before touching firmware.
* **The Pi cannot sniff the bus.** No `MONITOR_ALL`. Use the STM32 in full listening mode
  when you need to see everything.
* **Arbitration.** Leave `SignalFreeTime` on the hardware default. A short fixed SFT on a
  two-node bus turns into collisions that look like random transmit failures.
* **PB8 doubles as Arduino D15 / I2C1_SCL.** Do not enable I2C1 on the same pin later, and
  check its solder-bridge configuration in UM1724 before wiring.
* **Unpowered board pulls the bus down.** Harmless on a two-node bench, not acceptable on a
  real HDMI bus. See the isolation note in §2.
* **HDMI +5 V (pin 18) must be driven.** This plan originally said to leave it open, and
  that cost most of a day. Pin 18 is the +5 V a source supplies to a sink, and TVs gate CEC
  per port on it. Without it the TV transmits to you and refuses to listen: receive works
  perfectly, every directed transmit returns `tx no ack`, and the ack slot is never pulled
  down. It looks exactly like a firmware bug and is not one.
* **An unregistered device is ignored.** The TV acknowledges commands from a device that
  never reported its physical address, then does nothing with them. Let the Stage 5
  responder answer the interrogation before expecting `<Standby>` to work.
* **`HAL_CEC_Transmit_IT` returns `HAL_BUSY`** mid-frame. Report it, do not retry in a tight
  loop.
* **ISR discipline.** Formatting and `printf` belong in `cec_task()`. The console already
  learned this lesson once; see the Serial console section of the README.
* **Logical address 8 is assumed free.** If the Pi is reconfigured away from `--playback`,
  recheck the map in §4.
* **Do not run reduced listening mode.** With a third device on the bus the receiver stops
  delivering frames after a TV power transition, silently, until re-initialised. Full
  listening mode with the address filter in software ran the same cycles without a fault.
* **`RXOVR` does not mean what RM0091 says.** Every one logged had RXBR already clear. In
  reduced mode it fires for a directed frame to another address that nobody acknowledged;
  in full mode that case arrives as RXACKE. Treat both as bus activity, not error.
* **Do not clear `CEC_ISR` with `|=`.** The HAL does; it acknowledges any flag raised
  between the read and the write. Plain `ISR = bit` stores only.

## 8. References

* The **HDMI-CEC reference** section of the README: document map, chip facts, register and
  NVIC details. Read that first; this plan does not repeat it.
* `docs/RM0091-rev10.pdf` §31, p.892-910, and the register examples in §A.13, p.966. Grep
  `docs/rm0091-rev10.txt`.
* `docs/AN4066-hdmi-cec-network-stm32f0.pdf` and `docs/an4066.txt` (grep the txt). §2 covers
  the HDMI connector pinout and the 27k pull-up.
* `docs/DS-STM32F072.pdf` / `docs/ds.txt` for the AF tables.
* `ref/cube-example/` : ST's CEC_DataExchange example, `HAL_CEC_MspInit` in
  `Src/stm32f0xx_hal_msp.c` is the model for Stage 1.
* `ref/stsw-stm32134/` : the AN4066 firmware package, StdPeriph era, useful for the protocol
  layer rather than the register setup.
* UM1724, the Nucleo-64 board manual, for the CN5/CN10 pin positions and the solder
  bridges. Not vendored yet; fetch link is in the README.
