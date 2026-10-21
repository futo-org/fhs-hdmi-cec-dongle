# CEC implementation, in one page

Terse by design. Newcomer read. Details live in code comments + README.

## Files

| File | Role |
|---|---|
| `Inc/board.h` | pin/IRQ map: PB8 AF0, PB9 pull-up, `CEC_CAN_IRQn` prio 3, `USB_IRQn` prio 2 |
| `Inc/cec.h` `Src/cec.c` | peripheral, ISR, tx/rx, ack mask, watchdogs. No policy |
| `Inc/adapter.h` `Src/adapter.c` | driver protocol: requests -> `cec.c`, results/frames -> event queue -> USB |
| `Src/usb.c` | TinyUSB: descriptors, CEC interface class driver, event endpoint, CDC stdio |
| `../driver/fhs-cec-proto.h` | wire protocol, shared with the kernel driver |
| `Src/cec_log.c` | frame decoder for the console. Debug only |
| `Src/cec_diag.c` | bench instruments: pin borrowed from the peripheral, edge capture. Debug only |
| `Src/shell.c` | `cec`, `fault`, `diag`, `status`. Debug only |
| `ref/stm32f0xx_hal_cec.c` | HAL, config only. **Its IRQ handler is NOT used** (see quirks) |

## Data flow

```
host  -- SET_* / TRANSMIT (EP0) -->  adapter_control  -->  cec_set_ack_mask / cec_transmit
bus   -- ISR: rx_buf[] -> RXEND -> rx_queue[8] -->  cec_take_rx  --> adapter_task --+
ISR TXEND/err -> cec_task -> result ------------->  cec_take_tx_result -----------+
                                                       event queue[16] -> EP 0x81 -> host
```

* ISR never prints. `cec_task()` runs in the main loop.
* TX single buffered. A new `cec_transmit` abandons the old frame with no result.
* Every other abort (re-init, ack mask change, diag suspend) reports `CEC_TX_ERROR`.

## Transmit contract

* One result per accepted frame, within `CEC_TX_TIMEOUT_MS` (1 s).
* `TXEND` -> OK. `TXACKE` -> NACK (broadcast: a follower rejected). `TXERR` -> LOW_DRIVE. `TXUDR` -> ERROR.
* `ARBLST`: hardware retries on its own. If the 1 s budget runs out after one, the result is ARB_LOST.
* The kernel core times out at 2.1 s. On kernels before v7.3, one timeout during logical address
  claiming leaves the adapter unconfigured, so the 1 s budget is not negotiable.

## ISR (`cec_irq_handler`)

* Loop `while (ISR & IER)`: RXBR (read RXDR first), RXEND, rx errors, ARBLST, TXBR, TXEND, tx errors.
* Clears with **plain store** `ISR = bit`. Never `|=`.
* RXBR gap > 60 ms with bytes pending = stale frame, reset.
* All rx errors abort the frame -> `rx_len = 0` (BRESTP=1 so BRE aborts too).

## Peripheral quirks found (all reproduced, all worked around)

1. **Reduced listen mode goes deaf.** Fix: full listening always, filter in software (`adapter.c`).
2. **RXOVR ≠ overrun.** Foreign unacked frame. Counted as foreign nack, silent.
3. **HAL `__HAL_CEC_CLEAR_FLAG` is `ISR |= bit`.** Own ISR.
4. **HAL `HAL_CEC_SetDeviceAddress` never clears the old address.** `cec_set_ack_mask` writes OAR itself.
5. **CECEN=0 aborts rx/tx, ISR flags survive.** `cec_rearm()` after every disable/enable.
6. Back-to-back CECEN toggles in the ISR do nothing (32 kHz domain). Re-init from the main loop.

## Safety nets (`cec_task`)

| Net | Trigger | Action |
|---|---|---|
| tx timeout | tx pending > 1 s | re-init, result ARB_LOST or ERROR |
| reply watchdog | directed query (8f 8c 83 46 9f 91) acked, no frame at all in 1.5 s | re-init |
| rx stuck | ≥4 overruns AND no frame 2 s | re-init |

## Timing facts

Start bit 3.7 ms low / 4.5 ms. Bit 2.4 ms: `0` = 1.5 ms low, `1` = 0.6 ms low. Byte (10 bits)
≈ 24 ms. Header = initiator<<4 | dest. Broadcast dest 0xF, its ack inverted (low = reject).
TXBR at 4th bit of byte, next byte due within 6 bits.

## Glossary

| Term | Meaning |
|---|---|
| LA | logical address 0..15. 0 TV, 4/8/11 playback, 5 audio system, 15 broadcast / unregistered |
| PA | physical address a.b.c.d = HDMI port path. TV 0.0.0.0 |
| ack mask | LAs the hardware acknowledges, `CFGR.OAR`, one bit per LA 0..14 |
| LSTN | listen mode bit in `CFGR`. 1 = receive every frame (we use) |
| SFT | signal free time, idle bits before a start bit. Hardware picks 3/5/7 per the spec |
| CECEN | peripheral enable, `CR` bit 0. 0 aborts rx+tx, keeps `ISR` |
| TXSOM / TXEOM | tx start / end of message, `CR` |
| RXBR / RXEND | rx byte received / last byte received |
| TXBR / TXEND | tx byte request / last byte acked |
| ARBLST | lost arbitration to a lower initiator. Hardware retries |
| TXACKE / RXACKE | missing ack, tx / rx side |
| TXERR | line low while we drove high. Reported as LOW_DRIVE |
| TXUDR | byte not written in time after TXBR |
| BRE / SBPE / LBPE | bit rising / short bit period / long bit period error |
| poll | header-only frame. "Anyone at this LA?" |
| seq | per-transmit number from the driver, echoed in `EV_TX_DONE` |
| rearm | `cec_rearm()`: reset byte counts + clear all ISR flags after any CECEN 0/1 |
| recover | `cec_recover()`: `HAL_CEC_Init` + rearm, keeps ack mask and tolerance |
| CRS | clock recovery system: trims HSI48 against USB SOF |
