# fhs-hdmi-cec

HDMI-CEC on a NUCLEO-F072RB (STM32F072RBT6, Cortex-M0). STM32Cube HAL, vendored in `Drivers/`.

Current state: 48 MHz off the PLL, LED on PA5, a command shell on the ST-LINK VCP, and
**working HDMI-CEC** — send, receive, and enough of a responder that a TV registers the
board as a device and obeys it. Verified against an LG 50QNED75BA.

Wiring and the command sequence are below under [HDMI-CEC bench](#hdmi-cec-bench).
`docs/cec-bringup-plan.md` has the staged plan this was built from.

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
| `Src/`, `Inc/` | Firmware, one concern per module: `main.c` wiring, `board.h` pin map, `system_clock.c`, `led.c`, `uart.c` (bytes), `console.c` (line editing), `shell.c`, `cec.c` (peripheral + protocol), `cec_diag.c` (line instrumentation), `stm32f0xx_it.c`. `Inc/stm32f0xx_hal_conf.h` selects which HAL modules build |
| `Drivers/` | Vendored CMSIS Core + CMSIS device F0 + STM32F0xx HAL driver |
| `cmake/` | Generated build config. Only `files.cmake` is yours to edit |
| `tools/49-stlink.rules` | udev rules for the probe |
| `mise.toml` | toolchain versions **and** tasks: build / load / connect / size / erase / gdbserver / … |
| `docs/` | ST PDFs + `.txt` pdftotext dumps (grep these), plus `cec-bringup-plan.md` |
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

Commands: `help`, `led on|off|blink`, `cec ...`, `diag ...`, `status`, `reset`. Add your own
as a row in the `commands[]` table in `Src/shell.c` plus one handler.

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
| `docs/cec-bringup-plan.md` | Bring-up plan: bench, BOM, wiring, staged firmware | ours, follow it |

Missing: UM1724 (Nucleo-64 board manual) — st.com was unreachable from this machine. Grab from
<https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf>

## HDMI-CEC bench

### What you need

| Item | Note |
|---|---|
| HDMI type A breakout | pads for pins 13, 17, 18. A plug-only board is fine when the TV is the only other device |
| HDMI cable to the TV | micro-to-A if you also want it to reach a Pi |
| 22k or 27k resistor | the bus pull-up; the Nucleo has none |
| 3 jumpers | CEC, GND, +5 V |

### Wiring

Three nets. That is the whole circuit.

| Net | Connects |
|---|---|
| **CEC** | breakout pin 13 — R1 — NUCLEO **PB8** |
| **3V3** | R1 other leg — NUCLEO **3V3** |
| **GND** | breakout pin 17 — NUCLEO **GND** |
| **+5V** | breakout pin 18 — NUCLEO **5V** |

```
   HDMI type A breakout                    NUCLEO-F072RB
   +--------------------+
   |   pin 13   CEC  o--+----------+-----------o PB8   (= Arduino D15 / SCL)
   |                    |          |
   |                    |         .-.
   |                    |         | |  R1  22k
   |                    |         '-'
   |                    |          |
   |                    |          '-----------o 3V3
   |                    |
   |   pin 17   GND  o--+----------------------o GND
   |                    |
   |   pin 18   +5V  o--+----------------------o 5V
   |                    |
   |   pin 19   HPD     |   not connected
   +--------------------+
             |
             |  HDMI cable
             v
            TV
```

**Pin 18 is not optional.** It is the +5 V a source supplies to a sink, and it is how a TV
knows something is plugged into that port. TVs gate CEC per port on it: without it the TV
transmits to you and refuses to listen, which reads as a perfectly clean one-way link and
wastes an afternoon. Symptom: you receive the TV's frames fine, every directed transmit
comes back `tx no ack`, and the ack slot in `diag txtrace` is never pulled down.

The 22k matters too. A floating CEC line is a megohm antenna that picks up mains hum, and
`diag noise` reports thousands of edges a second at roughly 60 Hz structure.

Keep jumpers short, under about 10 cm.

### Bring it up

Check the wiring before talking to anything:

```
diag noise
```

Want `0 edges, 100% high` on a quiet bus, or a few dozen edges with a `longest low` near
3700 us if the TV is chatting. Thousands of edges means the line is floating.

```
diag line
```

Want `pull-up present` **and** `while driving: pin reads low`. The second line is the one
that matters; it proves the board can assert the bus, not just listen to it.

### Talk to the TV

The board allocates its own logical address a second after boot, the way CEC 1.4 §10.2.1
says a Playback Device should: it polls 4, then 8, then 11, and claims the first one nobody
acknowledges, then broadcasts `<Report Physical Address>` so the TV lists it:

```
cec: logical address 4 (playback), physical 1.0.0.0
         -> report physical address
```

`cec addr <n>` still overrides it, `cec addr auto` runs it again, and `status` shows `addr ?`
while it is polling. To check the link by hand:

```
cec send 0              poll the TV -> "tx ok" means it acknowledged
```

`tx ok` on that poll is the milestone: the TV pulled the ack bit down, so the link works
both ways. Then wait a few seconds. The TV interrogates the new device and the board
answers automatically:

```
rx 0->4 83  <Give Physical Address>    -> reply 84
rx 0->4 46  <Give OSD Name>            -> reply 47
rx 0->4 8c  <Give Device Vendor ID>    -> reply 87
rx 0->4 8f  <Give Device Power Status> -> reply 90
```

**Let that finish before sending commands.** A device that has not reported its physical
address is not in the TV's device list, and the TV will acknowledge your commands and then
ignore them. That is Stage 5 in the plan and it is why `cec auto off` is a debugging aid,
not something to leave off.

Now it obeys:

```
cec send 0 36           turn the TV off   <Standby>
cec send 0 04           turn the TV on    <Image View On>
cec send f 36           standby everything on the bus
```

**Wait about 10 seconds after a power command before sending anything else.** A TV stops
acknowledging directed messages while it is powering up or down, so anything sent during
the transition comes back `tx no ack`. That is the TV being busy, not a fault, and retrying
immediately does not help. The give-away that the command *did* land is the TV announcing
its new state unprompted:

```
> cec send 0 04
[  325960] tx ok
[  326853] rx 0->f 90 00   <Report Power Status>    <- it woke
> cec send 0 04
[  329995] tx failed: tx no ack (0x1000)            <- 4 s later, mid-transition
```

This is the single most confusing behaviour on the bench, because a burst of commands makes
a working link look broken. One command, then wait.

Query it:

```
cec send 0 8f           <Give Device Power Status> -> 90 00 = on, 90 01 = standby
cec send 0 9f           <Get CEC Version>          -> 9e 05 = CEC 1.4
cec send 0 46           <Give OSD Name>            -> 47 + ASCII
cec send 0 8c           <Give Device Vendor ID>    -> 87 + 3-byte OUI
```

A full session, from a cold board:

```
> cec addr 4
addr 4
> cec send 0
[  15098] tx ok
[  15453] rx 0->4 83  <Give Physical Address>
         -> reply 84
[  15592] tx ok
[  15681] rx 0->4 46  <Give OSD Name>
         -> reply 47
[  15964] tx ok
> cec send 0 8f
[  71741] tx ok
[  71853] rx 0->4 90 00  <Report Power Status>
> cec send 0 36
[  74676] tx ok
> cec send 0 8f
[  78910] tx ok
[  79022] rx 0->4 90 01  <Report Power Status>
```

### Turn the TV on and switch to this port

```
cec on                  One Touch Play: <Image View On>, then <Active Source> 1.0.0.0
cec off                 <Standby>
cec tv status           <Give Device Power Status> -> "tv is on|standby|turning on|turning off"
```

```
cec vol up [n]          Volume Up key, n times (User Control Pressed 44 41 / Released 45)
cec vol down [n]        Volume Down
cec vol mute            Mute toggle
cec vol status          Give Audio Status -> "audio: volume 34, muted"
```

Volume keys go to whoever owns the volume. When the soundbar broadcasts
`<Set System Audio Mode> on` (`5->f 72 01`, it does after every wake) the keys go to it at
address 5 and `cec vol status` gets a real answer; otherwise they go to the TV, which does
not report a volume level. The command echoes which one it picked.

`cec tv on` and `cec tv off` are the same as `cec on` and `cec off`. Every
`<Report Power Status>` from the TV is decoded the same way, asked for or not, so the
`tv is turning off` line shows up on its own after `cec off`.

`cec on` wakes the TV and makes it switch to the port this cable is in, in one go. What it
does on the bus, CEC 1.4 §13.1: `4->0 04` (Image View On), and once the TV acknowledges
that, `4->f 82 10 00` (Active Source, our physical address). The TV then asks around while
it wakes and the board keeps claiming the screen:

```
cec on
> tx ok                                   <Image View On> acknowledged
         -> active source 1.0.0.0
  tx ok
  rx 0->f 80 00 00 10 00  <Routing Change>          TV moving to 1.0.0.0
         -> active source 1.0.0.0
  rx 0->f 86 10 00        <Set Stream Path>         "device at 1.0.0.0, confirm"
         -> active source 1.0.0.0
  rx 0->f 80 10 00 30 00  <Routing Change>          TV briefly back on its last input
  rx 0->f 80 30 00 10 00  <Routing Change>          ...and onto ours for good
         -> active source 1.0.0.0
```

**Right after `cec off`, the TV refuses.** LG NACKs directed frames while it is still
going down (and, for a while, while coming up). `cec on` and `cec off` retry every second
for up to 30 s and say so:

```
cec on
> [ 176209] tv not ready (no ack), retry 2/30 in 1000 ms
  [ 177300] tv not ready (no ack), retry 3/30 in 1000 ms
  [ 178391] tx ok
           tv accepted it on attempt 3
           -> active source 1.0.0.0
```

A bare `cec send 0 04` does not retry; that is what the raw command is for.

`status` then reports `active source`. The board answers `<Set Stream Path>` and
`<Routing Change>` naming its address, and `<Request Active Source>` while it holds the
screen, so the TV's own input menu and the soundbar's routing traffic keep working. Another
device's `<Active Source>` clears the flag. `cec send 0 04` alone still just wakes the TV.

### Commands

```
cec addr <0-f|none|auto>  own logical address; allocated at boot, this overrides
cec on                    TV on and switched to our port (One Touch Play)
cec off                   TV standby
cec tv status|on|off      power state query, or the two above
cec vol up|down [n]       volume keys; mute; status (audio system only)
cec send <dest> [hex...]  one frame. No payload = a poll
cec auto <on|off>         answer the TV's interrogation (default on)
cec listen <own|all>      print only frames to us and broadcasts (default), or every frame
cec listen reduced        hardware address filter; wedges the receiver, see below
cec tol <std|ext>         rx timing tolerance
cec reset                 re-init the peripheral
cec dump                  registers, transmit/receive state, ISR counters and event log

diag line                 pull-up and drive test
diag noise                1 s activity summary
diag trace                sample the line, print edges with CEC bit hints
diag txtrace <d> [hex]    send and scope our own frame, including the ack slot
diag hold                 drive low 5 s, so you can meter the far end
```

`diag txtrace` is the one that settles arguments. It samples the pin while the peripheral
drives it, so you see your own bit timing against spec (start bit 3700 us, a 0 is 1500 us
low, a 1 is 600 us low, bit period 2400 us) and whether anyone pulls the ack slot down:

```
  low    3691 us  start bit
  low    1494 us  data 0        header 0x40 = initiator 4, destination 0
  low     610 us  data 1
  ...
  low     610 us  data 1        EOM
  low    1513 us  data 0        <- ack: TV holding the line past our 610 us
```

### Known rough edges

- **The bus has a third device on it.** The TV's soundbar (Samsung HW-Q600F, logical
  address 5, Audio System) talks CEC too. The TV interrogates it on every power transition
  and polls it every second while it boots and every 14 s while it sleeps; a sleeping
  soundbar does not answer. `cec listen all` shows the whole conversation. Two hardware
  behaviours came out of that traffic, both fixed in `Src/cec.c`:
  - **Reduced listening mode goes deaf.** With the hardware address filter on (LSTN=0),
    the receiver stopped delivering anything within seconds of every TV power transition:
    no flag, no error, registers all normal, TV still acknowledging our frames and
    answering them, nothing arriving. Only a re-init brought it back. Full listening mode
    (every frame received, filter in software) ran the same cycles indefinitely without
    a fault, so that is now the default. Whatever the silicon does with a directed frame it
    is meant to ignore, it does not do reliably. A watchdog covers the remaining unknowns:
    if the TV acknowledges a question it must answer (`8f`, `8c`, `83`, `46`, `9f`, `91`) and
    nothing at all arrives for 1.5 s, the peripheral is re-initialised and
    `rx watchdog: acked query, no reply` is printed. `cec dump` counts them.
  - **`rx overrun (0x4)` was never an overrun.** In reduced mode, a directed frame for
    someone else that nobody acknowledges (the TV polling the sleeping soundbar) raises
    RXOVR with no byte delivered and RXDR still holding our previous frame's last byte.
    RM0091 defines RXOVR as "RXBR not yet cleared when a new byte is received", which never
    happened: every overrun ever logged had RXBR clear, and the ISR event log proved the
    handler was never late. In full listening mode the same event arrives as RXACKE, which
    is documented. Both are now counted as `foreign nack` and not reported.
- **The interrupt path no longer uses the HAL.** `HAL_CEC_IRQHandler` clears flags with
  `ISR |= bit`, a read-modify-write that also acknowledges any flag raised between the read
  and the write, and its RX cursor is only rewound at RXEND or on an error, so a frame the
  hardware aborts (CECEN=0 does that) leaves it pointing past the buffer. `cec_irq_handler`
  owns the transmit and receive state, clears with plain `ISR = bit` stores, and rescans
  until nothing is pending. `HAL_CEC_Init` and `HAL_CEC_SetDeviceAddress` are still used for
  configuration.
- **The TV tramples the first reply after a wake.** It asks `83` and starts its next frame
  without waiting, on top of our `84`, which shows as `tx rejected by a follower`. Auto
  responses are retried once after 150 ms (`-> retry 84`).
- The physical address is hardcoded to `1.0.0.0` in `Src/cec.c`. There is no EDID reader,
  so it is declared rather than discovered. Change it if the cable moves to another port
- Logical address allocation only knows the Playback set (4, 8, 11) and the physical
  address is declared, not read from EDID, so it is a Playback Device at 1.0.0.0 or nothing


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
- External **27 kΩ pull-up to 3.3 V** — not present on the Nucleo, you must add it.
  22k works fine
- **HDMI pin 18 (+5 V) must be driven.** Not optional, see the bench section below
- Isolate the pin from the bus when the board is unpowered (series MOSFET, Figure 325)
  so you don't drag the HDMI bus down. Not fitted here; the board is always powered
- HDMI connector: CEC = pin 13, ground ref = pin 17 (DDC/CEC ground), +5 V = pin 18
