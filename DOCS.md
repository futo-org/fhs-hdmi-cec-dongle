# HDMI-CEC on Nucleo-F072RB — doc index

All PDFs in `docs/`, `.txt` = pdftotext dumps (grep these). Code refs in `ref/`.

## Primary

| File | What | CEC part |
|---|---|---|
| `docs/RM0091-rev10.pdf` | Reference manual STM32F0x1/F0x2/F0x8 (Rev 10) | **§31 HDMI-CEC controller, p.892-910**; register code examples **§A.13, p.966** |
| `docs/DS-STM32F072.pdf` | STM32F072x8/xB datasheet | §3.x CEC overview; AF tables 14/15; memory map |
| `docs/AN4066-hdmi-cec-network-stm32f0.pdf` | AN4066 "Developing an HDMI-CEC network using an STM32F0xx MCU" (32p) | whole doc: HW, protocol, sw stack |
| `docs/ST-training-HDMI-CEC.pdf` | ST peripheral training slides on CEC block | quick overview |
| `docs/STSW-STM32134_cec_firmware.zip` | AN4066 companion firmware (StdPeriph, V1.0.0) | unzipped to `ref/stsw-stm32134/` |

Missing: UM1724 (Nucleo-64 board manual) — st.com unreachable from here, grab from https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf

## Code

- `ref/stm32f0xx_hal_cec.c/.h` — Cube HAL CEC driver
- `ref/cube-example/` — STM32CubeF0 `CEC_DataExchange` example (STM32072B_EVAL, same die as F072RB). Also exist upstream: `CEC_ListenMode`, `CEC_MultiAddress`
- `ref/stsw-stm32134/STM32F0xx_AN4066_FW_V1.0.0/Project/STM32F0xx_CEC/`

## Chip facts (F072RB)

- Peripheral base `0x4000_7800`, APB1. Clock enable: `RCC_APB1ENR.CECEN` (bit 30)
- Kernel clock 32 kHz: `RCC_CFGR3.CECSW` — 0 = HSI/244 (default), 1 = LSE. Runs in Stop mode (wakeup on header)
- NVIC: **position 30, `CEC_CAN_IRQn`** — shared with CAN
- Registers: `CEC_CR`, `CEC_CFGR`, `CEC_TXDR`, `CEC_RXDR`, `CEC_ISR`, `CEC_IER`. `CEC_CFGR` and `CEC_IER` writable only when `CECEN=0`
- Features: CEC v1.4, own-address filter (OAR), listen mode, SFT auto/fixed, BRE/SBPE/LBPE/TXERR/ARBLST detection + auto retry
- Pins (AF): **PB8 = AF0**, **PB10 = AF0**, PA5 = AF1
  - On Nucleo-64: PB8 = Arduino **D15** (I2C1_SCL), PB10 = Arduino **D6**. PA5 = D13 = user LED LD2, don't use.
  - Verify solder-bridge config for PB8/PB9 in UM1724.

## Hardware needed (RM0091 §31.3.1)

- CEC line is single bidirectional open-drain wire → configure GPIO as **AF open-drain, no pull**
- External **27 kΩ pull-up to 3.3 V** (not on the Nucleo, must add)
- Isolate pin from bus when board unpowered (series MOSFET, see Figure 325) so you don't drag the HDMI bus down
- HDMI connector: CEC = pin 13, GND ref = pin 17 (DDC/CEC ground)
