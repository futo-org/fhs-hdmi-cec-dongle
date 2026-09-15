# fhs-hdmi-cec

HDMI-CEC on a NUCLEO-F072RB (STM32F072RBT6, Cortex-M0). STM32Cube HAL, vendored in `Drivers/`.

Current state: 48 MHz off the PLL, LED on PA5, and a command shell on the ST-LINK VCP.
CEC not started yet.

## Quick start

    mise install            # one-time: fetch the pinned toolchain
    mise run build          # configure + build
    mise run load           # build, flash, reset
    mise run connect        # list probes + read DBGMCU_IDCODE, read-only
    mise run verify         # flash contents vs built ELF
    mise tasks              # full list

`PRESET=Release mise run load` for the Release preset.
Artifacts land in `build/<preset>/fhs-hdmi-controller.elf` + `.map`.

## One-time: ST-LINK permissions

Probe enumerates (`0483:374b`) but has no udev rule → `DEV_CONNECT_ERR`.

    sudo cp tools/49-stlink.rules /etc/udev/rules.d/
    sudo udevadm control --reload && sudo udevadm trigger
    # then replug the board

## Layout

| Path | What |
|---|---|
| `Src/`, `Inc/` | Firmware, one concern per module: `main.c` wiring, `board.h` pin map, `system_clock.c`, `led.c`, `uart.c` (bytes), `console.c` (line editing), `shell.c`, `stm32f0xx_it.c`. `Inc/stm32f0xx_hal_conf.h` selects which HAL modules build |
| `Drivers/` | Vendored CMSIS Core + CMSIS device F0 + STM32F0xx HAL driver |
| `cmake/` | Generated build config. Only `files.cmake` is yours to edit |
| `tools/49-stlink.rules` | udev rules for the probe |
| `mise.toml` | toolchain versions **and** tasks: build / load / connect / size / erase / gdbserver / … |
| `docs/` | ST PDFs + `.txt` pdftotext dumps (grep these) |
| `ref/` | Reference CEC code, not compiled |

## Serial console

USART2 on PA2/PA3 → ST-LINK VCP → `/dev/ttyACM0`, **115200 8N1**. `printf` goes out here
(`syscall.c` already routes `_write`/`_read` to `__io_putchar`/`__io_getchar`, which
`Src/uart.c` defines), and typed lines come back through `console_take_line()`.

    mise run monitor          # minicom at 115200
    # or, scripted -- keep ONE fd open for the whole session:
    stty -F /dev/ttyACM0 115200 raw -echo -hupcl
    exec 3<>/dev/ttyACM0
    cat <&3 &
    sleep 3                   # the open toggles DTR/RTS; let the VCP settle
    printf 'status\r' >&3

Commands: `help`, `led on|off|blink`, `status`, `reset`. Add your own as a row in the
`commands[]` table in `Src/shell.c` plus one handler.

The RX ISR only pushes bytes into a ring buffer — echo and line editing happen in
`Src/console.c` from the main loop. Echoing inside the ISR blocks for a full character
time at 115200 and drops the next incoming byte (`status` arrives as `statu`).

Re-opening `/dev/ttyACM0` per command (`printf ... > /dev/ttyACM0` in a loop) toggles
DTR/RTS on the ST-LINK VCP each time and drops bytes in flight — output comes back with
chunks missing. It looks exactly like a firmware bug and is not one. Hold one fd.

The terminal has no local echo (`raw -echo`); the firmware echoes. `/dev/ttyACM0` is
world-readable thanks to the udev rule above, so dialout group membership is not needed.

## HAL

Vendored rather than added through the extension's pack manager, so the build is
self-contained and reproducible from a clone:

    Drivers/CMSIS/Core/Include                      # ARM CMSIS core (Cortex-M0)
    Drivers/CMSIS/Device/ST/STM32F0xx               # device headers + system_stm32f0xx.c
    Drivers/STM32F0xx_HAL_Driver                    # full HAL, Inc/ + Src/

Enabled modules are listed in `Inc/stm32f0xx_hal_conf.h`: HAL, CEC, CORTEX, DMA, EXTI,
FLASH, GPIO, PWR, RCC, UART. Enabling another one there also needs its `.c` added to the
`target_sources` list in `CMakeLists.txt`. HAL sources compile with `-w`; the project's
own `-Wall -Wextra -Wpedantic` still applies to `Src/`.

Defines: `STM32F072xB`, `USE_HAL_DRIVER`.

## Toolchain

Everything is pinned in `mise.toml` and installed by mise. No STM32Cube VS Code bundles,
no system packages, nothing to put on PATH by hand:

    [tools]
    cmake             = "4.4.3"
    ninja             = "1.13.2"
    gcc-arm-none-eabi = "13.3.Rel1"
    "ubi:probe-rs/probe-rs" = { version = "latest", exe = "probe-rs" }

    mise install      # one command, from a fresh clone

Flashing is `probe-rs`, not STM32CubeProgrammer. Chip id is `STM32F072RB` (`$CHIP` in
`mise.toml`); `probe-rs chip list` has the rest. Raw equivalents of the tasks:

    cmake --preset Debug
    cmake --build --preset Debug
    probe-rs download --chip STM32F072RB build/Debug/fhs-hdmi-controller.elf
    probe-rs reset --chip STM32F072RB

**`arm-none-eabi-gdb` needs one system package.** The Arm toolchain links against
ncurses 5, which Fedora 43 does not ship by default:

    arm-none-eabi-gdb: error while loading shared libraries: libncursesw.so.5
    sudo dnf install ncurses-compat-libs

Only `mise run gdb` needs it; build and flash do not.

Still from the STM32Cube extension, both editor-side only, neither needed to build or
flash: the clangd language server (`cube starm-clangd`, pointed at the mise gcc via
`.vscode/settings.json`) and the `F5` ST-Link debug adapter. To drop the latter too,
run `mise run gdbserver` (probe-rs, port 1337) and attach with `mise run gdb`.

## In VS Code

- Build: `Ctrl+Shift+B` (`cmake: build Debug` task — configure + build)
- Debug+flash: `F5` → "Debug (ST-Link, Debug preset)" — builds, flashes, halts at `main`
- Peripheral registers: STM32Cube registers view (SVD wired in `launch.json`)
- The attach config only connects to an `ST-LINK_gdbserver` you already started (port 61234,
  `mise run gdbserver`); it does not launch one.
- Build/debug go through `.vscode/tasks.json`, which just calls `mise run …`, **not** the
  CMake Tools extension. CMake Tools keeps its active preset in workspace state with no settings.json
  equivalent in v1.24, so a fresh clone gives "No configure preset is active". Pick one from
  the status bar if you want its UI; nothing here depends on it.

## Gotchas hit already

- `openGdbConsole: true` → `pty.create_pty is not a function`. The extension's bundled
  `pty.node` is packaged as a path string, not a native binding, so the client-terminal gdb
  path is broken in v1.4.0. Leave it off.
- `deviceName` must be a **substring** of a CMSIS pack `Dname` — `STM32F072RB`, not
  `STM32F072RBTx`.
- `Target no device found` / `DEV_CONNECT_ERR` on a second run: the previous session's
  `ST-LINK_gdbserver` still owns the probe, or the ST-LINK re-enumerated. `serverShared: true`
  and `automaticallyKillServer: true` are set to limit this. If it still happens:
  `mise run kill-probe`, then press the black RESET button (or replug).
- F072 has HSI48, so the HAL defines `RCC_PLLSOURCE_HSI` as **HSI/PREDIV**, not HSI/2
  (`stm32f0xx_hal_rcc_ex.h`, under `#if defined(RCC_HSI48_SUPPORT)`). With `PREDIV_DIV1` and
  `PLL_MUL12` that is 96 MHz — double the part max, and nothing asserts. `SystemClock_Config`
  uses `PREDIV_DIV2` to land on 48 MHz.

### ST-LINK firmware

The gdbserver reports "ST-LINK firmware update available" — the on-board probe is at
**V2J33M25** (2018-era), a known cause of intermittent "no device found". Upgrade (close
VS Code debug sessions first):

    ~/.local/share/stm32cube/bundles/adoptium-jre/21.0.8+9.st.2/bin/java \
      -jar ~/.local/share/stm32cube/bundles/stlink-upgrader/3.17.11+st.1/STLinkUpgrade.jar

---

# HDMI-CEC reference

## Documents

| File | What | CEC part |
|---|---|---|
| `docs/RM0091-rev10.pdf` | Reference manual STM32F0x1/F0x2/F0x8 (Rev 10) | **§31 HDMI-CEC controller, p.892-910**; register code examples **§A.13, p.966** |
| `docs/DS-STM32F072.pdf` | STM32F072x8/xB datasheet | §3.x CEC overview; AF tables 14/15; memory map |
| `docs/AN4066-hdmi-cec-network-stm32f0.pdf` | AN4066 "Developing an HDMI-CEC network using an STM32F0xx MCU" (32p) | whole doc: HW, protocol, sw stack |
| `docs/ST-training-HDMI-CEC.pdf` | ST peripheral training slides on the CEC block | quick overview |
| `docs/STSW-STM32134_cec_firmware.zip` | AN4066 companion firmware (StdPeriph V1.0.0) | unzipped to `ref/stsw-stm32134/` |

Missing: UM1724 (Nucleo-64 board manual) — st.com was unreachable from this machine. Grab from
<https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf>

## Reference code (`ref/`, not compiled)

- `ref/stm32f0xx_hal_cec.c/.h` — Cube HAL CEC driver
- `ref/cube-example/` — STM32CubeF0 `CEC_DataExchange` (STM32072B_EVAL, same die).
  Upstream also has `CEC_ListenMode`, `CEC_MultiAddress`
- `ref/stsw-stm32134/STM32F0xx_AN4066_FW_V1.0.0/Project/STM32F0xx_CEC/`

## Chip facts

- Peripheral base `0x4000_7800`, APB1. Clock enable: `RCC_APB1ENR.CECEN` (bit 30)
- Kernel clock 32 kHz: `RCC_CFGR3.CECSW` — 0 = HSI/244 (default), 1 = LSE.
  Runs in Stop mode (wakeup on header)
- NVIC **position 30, `CEC_CAN_IRQn`** — shared with CAN
- Registers: `CEC_CR`, `CEC_CFGR`, `CEC_TXDR`, `CEC_RXDR`, `CEC_ISR`, `CEC_IER`.
  `CEC_CFGR` and `CEC_IER` are writable only when `CECEN=0`
- CEC v1.4, own-address filter (OAR), listen mode, SFT auto/fixed,
  BRE/SBPE/LBPE/TXERR/ARBLST detection + auto retry
- Pins: **PB8 = AF0**, **PB10 = AF0**, PA5 = AF1
  - Nucleo-64: PB8 = Arduino **D15** (I2C1_SCL), PB10 = Arduino **D6**.
    PA5 = D13 = user LED, already taken by the blink example.
  - Check the PB8/PB9 solder-bridge config in UM1724 before wiring.

## Hardware needed (RM0091 §31.3.1)

- CEC is a single bidirectional open-drain line → GPIO as **AF open-drain, no pull**
- External **27 kΩ pull-up to 3.3 V** — not present on the Nucleo, you must add it
- Isolate the pin from the bus when the board is unpowered (series MOSFET, Figure 325)
  so you don't drag the HDMI bus down
- HDMI connector: CEC = pin 13, ground ref = pin 17 (DDC/CEC ground)
