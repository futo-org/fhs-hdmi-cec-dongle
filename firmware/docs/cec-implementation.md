# CEC implementation, in one page

Terse by design. Newcomer read. Details live in code comments + README "HDMI-CEC bench".

## Files

| File | Role |
|---|---|
| `Inc/board.h` | pin/IRQ map: PB8 AF0, `CEC_CAN_IRQn` prio 3 |
| `Inc/cec.h` `Src/cec.c` | everything CEC: init, ISR, tx/rx, responder, address alloc, power/volume, watchdogs |
| `Inc/cec_diag.h` `Src/cec_diag.c` | bench instruments: pin borrowed from peripheral, edge capture. Fault finding only |
| `Src/shell.c` | `cec ...` `diag ...` commands, `status` line |
| `Src/stm32f0xx_it.c` | `CEC_CAN_IRQHandler -> cec_irq_handler()` |
| `ref/stm32f0xx_hal_cec.c` | HAL, config only. **Its IRQ handler is NOT used** (see quirks) |

## Hardware

* STM32F072 CEC peripheral, PB8 open drain, no internal pull. Kernel clock HSI/244 = 32.787 kHz, set in `cec_msp_init`.
* Bus: 22k..27k pull-up to 3V3 (Nucleo has none). HDMI pin 13 CEC, 17 GND, **18 +5V must be driven** (TV gates CEC per port on it).
* Bench bus = 3 devices: LG TV addr 0, Samsung soundbar addr 5 (Audio System), us addr 4.

## Identity (hardcoded, `cec.c` top)

* `CEC_PHYS_ADDR 0x1000` = 1.0.0.0 (HDMI 1). No EDID reader. Change if cable moves.
* `CEC_DEVICE_TYPE 4` Playback. OSD name "FUTO Home". Vendor ID 0.

## Data flow

```
bus -> ISR: rx_buf[] byte by byte -> RXEND -> rx_queue[8] (SPSC) -> cec_task(): print, respond()
cec_send(): tx_buf[] + header -> TXDR/TXSOM -> ISR feeds TXBR -> TXEND/err -> cec_task() prints
```

* ISR never prints. `cec_task()` runs in main loop, one rx frame per pass.
* TX single buffered. `cec_send()` returns 0 if busy. Protocol callers retry from `cec_task`.
* Shell frames (`cec on|off|tv status|vol status|send`) go through `cec_send_user()`: sends now, or parks one frame (`uq_*`) that `cec_task` sends first thing when the slot frees. Before this a command typed while we answered the TV's `Give Device Vendor ID` (10 to 15 s after every wake) was dropped as "busy".

## ISR (`cec_irq_handler`)

* Loop `while (ISR & IER)`: handle RXBR (read RXDR first), RXEND, rx errors, ARBLST, TXBR, TXEND, tx errors.
* Clears with **plain store** `ISR = bit`. Never `|=`.
* RXBR gap > 60 ms with bytes pending = stale frame, reset (`rx_resync` counter).
* All rx errors abort frame -> `rx_len = 0` (BRESTP=1 so BRE aborts too).
* Event ring `ev_log[32]` + overrun ring `ovr_log[4]`, dumped by `cec dump`.

## Peripheral quirks found (all reproduced, all worked around)

1. **Reduced listen mode goes deaf.** LSTN=0 + TV↔soundbar traffic -> after TV power transition receiver delivers nothing, no flag, regs normal, until re-init. Fix: **full listening always** (`CEC_FULL_LISTENING_MODE`), address filter in software.
2. **RXOVR ≠ overrun.** Fires for someone else's unacked directed frame (TV polling sleeping soundbar). `rx_len 0`, RXBR clear, RXDR = previous frame's last byte. In full mode same event = RXACKE. Both counted as `foreign nack`, silent.
3. **HAL `__HAL_CEC_CLEAR_FLAG` is `ISR |= bit`**: read/modify/write acks flags raised in between. HAL rx cursor not rewound on abort. Hence own ISR.
4. **CECEN=0 aborts rx/tx**, ISR flags survive. `cec_rx_rearm()` after every disable/enable path (init, set address, listen/tol change, diag suspend/resume).
5. Back-to-back CECEN toggle in ISR does nothing (32 kHz domain). Re-init = `HAL_CEC_Init` from main loop.

## Safety nets (in `cec_task`)

| Net | Trigger | Action |
|---|---|---|
| reply watchdog | our directed query (8f 8c 83 46 9f 91) acked, no frame at all in 1.5 s | `cec_recover()`, print `rx watchdog` |
| tx timeout | tx pending > 1 s (ARBLST wedge) | `cec_recover()` |
| rx stuck | ≥4 real overruns AND no frame 2 s | `cec_recover()`. Should never fire now |
| reply retry | auto-response NACKed/TXERR (TV tramples our 84 after wake) | resend once after 150 ms |

`cec_recover()` = `HAL_CEC_Init` (keeps addr, tolerance, listen mode) + rearm.

## Logical address (CEC 1.4 §10.2.1)

* Boot +1 s: poll 4, 8, 0xB from that address. Acked = taken, next. NACK = free, claim via `cec_set_address`, then broadcast `84 10 00 04` Report Physical Address.
* `cec addr <n>` overrides, cancels alloc. `cec addr auto` reruns. `status` shows `addr ?` while polling.
* `cec_set_address` also updates `Init.OwnAddress` (HAL leaves it stale).

## Responder (`respond()`, `cec auto on|off`)

Directed to us: `83->84` phys addr (bcast), `8C->87` vendor (bcast), `46->47` OSD name, `8F->90 00`, `9F->9E 05` (v1.4).
Broadcast: `85` Request Active Source -> `82` if we hold screen. `86` Set Stream Path / `80` Routing Change naming 1.0.0.0 -> `82`. Someone else's `82` -> clear `active_source`.
Tracked: `72`/`7E` System Audio Mode -> `sys_audio_mode`. `90` from TV closes `cec tv status`. Decoding is in the console line itself.

## TV control

| Cmd | Bus | Notes |
|---|---|---|
| `cec on` | `4->0 04`, on ack `4->f 82 10 00` | One Touch Play. NACK -> retry 1 s, ≤30x (LG refuses mid transition) |
| `cec off` | `4->0 36` | same retry |
| `cec tv status` | `4->0 8F` -> `90 xx` | decoded `Report Power Status: on` etc. 2 s no answer -> warning |
| `cec vol up\|down [n]` | `44 41\|42` + `45`, n times | target = 5 if `sys_audio_mode` else 0 |
| `cec vol mute` | `44 43` + `45` | toggle |
| `cec vol status` | `71` -> `7A` | audio system only |
| `cec send <d> [hex]` | raw, no retry | poll = header only |

Wait ~10 s after power commands, ~35 s for full LG boot.

## Console format

```
[  41.073] RX [04 83]
              TV    -> us     83                 Give Physical Address
[  41.080] TX [4f 84 10 00 04]  reply ok
              us    -> all    84 10 00 04        Report Physical Address 1.0.0.0 Playback
[  41.138]   ! rx errors x3: bit rising (0x8)
[  42.000]   i logical address 4 (us), physical 1.0.0.0
[  43.000]   ~ TV not ready (no ack), retry 2/30 in 1000 ms
```

`[s.ms]`, `from -> to` by LA name (`TV` `us` `Audio` `Play2` `all`), payload hex, meaning w/ decoded operands (PA, power state, key, volume, OSD text, vendor OUI, abort reason). Own frames end with result: `ok` / `reply ok` / `NO ACK, nobody answered` / `REJECTED by a follower` / `FAILED, collision on the line`. `RX [..]`/`TX [..]` line = wire bytes incl. header + tx result; indented line = meaning. `cec raw off` folds them. Marks: `i` info, `~` retry step, `!` warning. Code: `print_line()`, `print_meaning()`, `note()` in `cec.c`.

## Console filter

HW receives all. `cec listen own` (default) prints ours + broadcasts. `cec listen all` prints everything (bus sniffer). `cec listen reduced` = HW filter, **for experiments only, wedges**.

## Diag (`cec_diag.c`)

`diag line` pull-up + drive test. `diag noise` 1 s edge stats (floating line = mains hum). `diag trace` edges w/ bit hints. `diag txtrace` scope own frame incl. ack slot. `diag hold` drive low 5 s. All suspend the peripheral (`cec_bus_suspend/resume`).

## Timing facts

Start bit 3.7 ms low / 4.5 ms. Bit 2.4 ms: `0` = 1.5 ms low, `1` = 0.6 ms low. Byte (10 bits) ≈ 24 ms. Header = initiator<<4 | dest. Broadcast dest 0xF, its ack inverted (low = reject). TXBR at 4th bit of byte, next byte due within 6 bits.

## Not done

* Physical address from EDID. Only Playback address set. No CEC over host protocol yet (see README production note: UART + RainShadow or framed protocol).
* Pi 5 as CEC partner never worked (vc4 reports f.f.f.f), TV used instead.

## Debug workflow

1. `status` -> `cec ready, addr 4, active source, rx N, tx N, err N`.
2. `cec dump` -> regs, tx/rx state, counters (`foreign nack`, `watchdog`, `late flags`), last 32 ISR events.
3. `cec listen all` -> watch the whole bus.
4. `diag txtrace 0 8f` -> is anyone acking us.
5. One reader on `/dev/ttyACM0`. Two readers = garbled.

## Glossary

Register bits are STM32 CEC (`CEC_ISR` / `CEC_IER` / `CEC_CR` / `CEC_CFGR`, RM0091 §31).

| Term | Meaning |
|---|---|
| CEC | Consumer Electronics Control. HDMI single-wire bus, pin 13 |
| LA | logical address 0..15. 0 TV, 4/8/11 playback, 5 audio system, 15 broadcast/unregistered |
| PA | physical address a.b.c.d = HDMI port path. TV 0.0.0.0, our HDMI 1 = 1.0.0.0 |
| OAR | Own Address Register, `CFGR` bitmask of LAs the hardware acks for |
| LSTN | Listen mode bit in `CFGR`. 1 = receive every frame (we use), 0 = reduced, hardware drops foreign frames (wedges) |
| SFT | Signal Free Time, idle bits before a start bit. Hardware default, `CEC_DEFAULT_SFT` |
| SFTOPT | SFT starts on TXSOM (we use) vs on bus event |
| RXTOL | rx timing tolerance, standard or extended (`cec tol`) |
| BRESTP | stop reception on Bit Rising Error. Set, so every rx error aborts the frame |
| BREGEN / LBPEGEN / BRDNOGEN | generate error bit on the line for BRE / LBPE / broadcast. All off |
| CECEN | peripheral enable, `CR` bit 0. 0 aborts rx+tx, clears `CR`, keeps `ISR` |
| TXSOM | Tx Start Of Message, `CR`. Set after header in TXDR. Hardware clears at end |
| TXEOM | Tx End Of Message, `CR`. Set before writing last byte |
| TXDR / RXDR | tx / rx data register, one byte |
| RXBR | Rx Byte Received. Read RXDR, clear |
| RXEND | Rx End of message, set with the last byte's RXBR (EOM=1) |
| RXOVR | Rx Overrun. Documented: new byte while RXBR still set. Actual on this bench: foreign unacked frame (quirk 2) |
| BRE | Bit Rising Error, rising edge at wrong time |
| SBPE / LBPE | Short / Long Bit Period Error |
| RXACKE | Rx missing ack. Full mode: foreign frame nobody acked. Counted as foreign nack |
| ARBLST | Arbitration Lost, lower LA won the start bit. Hardware retries our tx itself |
| TXBR | Tx Byte Request, at 4th bit of current byte. Next byte due within 6 bits |
| TXEND | Tx End, last byte acked |
| TXUDR | Tx Underrun, byte not written in time after TXBR |
| TXERR | Tx Error, line low when we drove high (collision) |
| TXACKE | Tx missing ack. Directed: follower absent/refusing. Broadcast: someone rejected (inverted ack) |
| EOM | End Of Message bit after each byte. 1 = last byte |
| ACK | ack bit after each byte. Directed: follower pulls low = ack. Broadcast: low = reject |
| Initiator / Follower | sender / addressed device of one frame |
| Header | byte 0 = initiator<<4 \| dest |
| Poll / Ping | header-only frame, EOM=1. "Anyone at this LA?" |
| Opcode | byte 1. e.g. `04` Image View On, `36` Standby, `8F` Give Power Status, `90` Report Power Status, `82` Active Source, `86` Set Stream Path, `80` Routing Change, `44/45` User Control Pressed/Released, `72` Set System Audio Mode, `7A` Report Audio Status |
| UCP / UCR | User Control Pressed / Released (`44` / `45`) |
| UI command | key code inside UCP. `41` vol up, `42` vol down, `43` mute |
| OTP | One Touch Play: `04` then `82`, TV wakes and switches to our PA |
| Active Source | device holding the screen, claims with `82 <PA>` |
| System Audio Mode | audio system owns volume, announced by `72 01`. Keys go to LA 5 |
| SimpLink | LG's name for CEC |
| ARC / eARC | Audio Return Channel, how the soundbar hangs on the TV. Why LA 5 is on the bus |
| HPD | Hot Plug Detect, HDMI pin 19. Not wired |
| DDC | Display Data Channel, HDMI pins 15/16 (I2C, EDID). Pin 17 = DDC/CEC ground |
| EDID | display identity block. Source of PA in real devices. We hardcode |
| HAL | STM32Cube HAL, `ref/stm32f0xx_hal_cec.c`. Config only here |
| ISR | interrupt service routine, also the name of the CEC status register. Context tells |
| IER | Interrupt Enable Register, bit per ISR flag |
| NVIC | Cortex-M interrupt controller. CEC IRQ 30, prio 3, shared with CAN |
| MSP | HAL "MCU Support Package" init: clocks, pins, NVIC (`cec_msp_init`) |
| AF / AF_OD | GPIO alternate function / open drain. PB8 AF0 OD |
| HSI | 8 MHz internal RC, /244 = CEC kernel clock |
| RCC | Reset and Clock Control |
| RM0091 | STM32F0 reference manual, `docs/rm0091-rev10.pdf` |
| AN4066 | ST app note, CEC on STM32F0, `docs/` |
| SPSC | single producer single consumer ring: ISR writes head, main reads tail, no locks |
| rearm | `cec_rx_rearm()`: reset byte counts + clear all ISR flags after any CECEN 0/1 |
| recover / re-init | `cec_recover()`: `HAL_CEC_Init` + rearm, keeps LA |
| foreign nack | someone else's directed frame nobody acked. Counter, not error |
| watchdog | reply watchdog: acked query, no frame 1.5 s -> re-init |
| rx stuck | legacy net: ≥4 real overruns + 2 s silence -> re-init |
| ST-LINK / VCP | Nucleo debugger + its virtual COM port = our console on USART2 |
| probe-rs | flasher/debugger CLI used by `mise run load` |
| SysTick | Cortex-M tick timer, 1 ms HAL tick, µs stamps in diag |
| IDR | GPIO Input Data Register, lets diag read PB8 while peripheral owns it |
