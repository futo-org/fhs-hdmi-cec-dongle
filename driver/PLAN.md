# Linux CEC driver plan

Goal: plugging in the dongle gives `/dev/cecN`, already configured, with no userspace setup. `cec-ctl`, libcec and `cec-follower` work unchanged.

Scope decisions:

- **Deliverable: a DKMS package.** Not upstreamed. Installable on any distro, FHS included. FHS image integration is handled separately (notes at the end).
- **Driver-controlled only.** The dongle does nothing on the CEC bus until the driver enables it. The debug serial shell is a compile-time option.
- **TinyUSB** for the firmware USB stack.
- **VID:PID:** `0x3a4c:0x4663`, defined once in `fhs-cec-proto.h`.

## What Linux gives us

CEC framework, `drivers/media/cec/core`, `CONFIG_CEC_CORE`. Mainline since 4.10.

The driver fills in `struct cec_adap_ops` and calls `cec_allocate_adapter()` + `cec_register_adapter()`. The core then provides:

- `/dev/cecN`, the `CEC_*` ioctls, debugfs status
- logical address allocation (polling, claiming, re-claiming on PA change)
- retries: `adap_transmit()` gets an `attempts` count. If the driver reports one attempt with `cec_transmit_attempt_done(adap, CEC_TX_STATUS_NACK)`, the core requeues it. Firmware only has to do one attempt per request.
- replies to core messages on its own: Get CEC Version, Give Physical Address, Give Device Vendor ID, Give OSD Name, Give Features
- topology tracking from Report Physical Address
- RC passthrough: User Control Pressed/Released → input device (`CEC_CAP_RC`)
- physical address: `cec_s_phys_addr()` / `cec_s_phys_addr_from_edid()`, or userspace sets it when `CEC_CAP_PHYS_ADDR` is set

What the driver implements:

| op | does |
|---|---|
| `adap_enable(bool)` | enter/leave host mode on the device |
| `adap_log_addr(la)` | add LA to the hardware ack mask; `CEC_LOG_ADDR_INVALID` = clear all |
| `adap_transmit(attempts, sft, msg)` | send one frame, async |
| `adap_monitor_all_enable(bool)` | forward foreign frames too (`CEC_CAP_MONITOR_ALL`) |
| `adap_status` | debugfs dump (counters from the device) |

Back to the core:

- `cec_transmit_attempt_done(adap, status)`: OK / NACK / ARB_LOST / LOW_DRIVE / ERROR
- `cec_received_msg(adap, msg)`

Both take `adap->lock` (a mutex), so they cannot run in a USB completion handler. Pulse-Eight, RainShadow and Extron all hand off to a workqueue. We do the same.

## Reliability findings

Most "Linux CEC is broken" reports come from individual drivers or from userspace, not the core. The core does have real bugs, all in rare paths. The design below works around each one.

### Core bugs (`drivers/media/cec/core`)

| Bug | Fixed in | In FHS kernel (7.1.4)? |
|---|---|---|
| CVE-2024-23848: use-after-free in `cec_queue_msg_fh`, no lock on filehandle release | 6.8-era | fixed (7.1.4) |
| syzbot: task hung in `cec_claim_log_addrs` (lock dropped and re-taken) | 2024 | fixed |
| A signal interrupting a blocking `CEC_TRANSMIT` races the kthread and frees a pending delayed work (ODEBUG warning, UAF) | 2026; the backport failed to apply to some stable trees | **missing in 7.1.4** (still `cancel_delayed_work_sync`). A userspace client killed mid-transmit triggers it |
| LA claim gives up if a poll returns `TIMEOUT` or `ABORTED`. The adapter stays unconfigured with no signal to userspace | v7.3 (also adds `CEC_LOG_ADDRS_FL_CONFIG_FAILED`) | **missing in 7.1.4** (still has the `ABORTED \| TIMEOUT` bail-out) |

### Core behaviour the driver must respect (read from `cec-adap.c`)

- **Transmit deadline 2.1 s** (`xfer_timeout_ms`). If no `cec_transmit_attempt_done()` arrives by then, the core marks the message `TIMEOUT` and starts the next one. A late TX_DONE gets credited to the *next* message. → Firmware always finishes within ~1 s, and the driver drops TX_DONE events whose `seq` doesn't match.
- **Never let a transmit reach `TIMEOUT`.** On kernels before v7.3 (the FHS kernel and every current distro kernel) one timeout during LA claim leaves the adapter unconfigured. A firmware-side stuck TX reports `ERROR`, which the core retries.
- **Unconfigured transmit.** When the PA is invalid, the core still sends a poll or `<Image View On>` to the TV from LA 15, but only if `CEC_CAP_NEEDS_HPD` is not set. That's how displays that drop HPD in standby get woken. → Don't set `NEEDS_HPD`. Firmware must transmit with an empty ack mask.
- Queues: 18 TX per adapter, 54 RX per filehandle. Overflow returns `-EBUSY` or drops the oldest. No action needed.

### Ecosystem reports

- **Physical address is the #1 failure.**
  - Pi 5 vc4 stays at `f.f.f.f` despite a valid EDID ([raspberrypi/linux#7485](https://github.com/raspberrypi/linux/issues/7485)). Our firmware docs already hit this.
  - Kodi CEC broke on Pi kernel 5.10 ([#4148](https://github.com/raspberrypi/linux/issues/4148)).
  - libcec `CEC_TRANSMIT` fails with errno 64 = `ENONET` = adapter unconfigured because the PA is invalid ([libcec#592](https://github.com/Pulse-Eight/libcec/issues/592)).
- **libcec never sets the PA.** Its Linux backend (`LinuxCECAdapterCommunication.cpp`) only reads it with `CEC_ADAP_G_PHYS_ADDR`, warns if invalid, and never calls `CEC_ADAP_S_PHYS_ADDR`. → The PA must come from the device or the driver.
- **libcec claims exclusive follower** (`CEC_MODE_EXCL_FOLLOWER_PASSTHRU`) and supports only one LA. A second client wanting follower mode gets `EBUSY`. → One daemon owns `/dev/cecN` per machine.
- **TVs drop HPD in standby** (kernel admin guide). Drivers that invalidate the PA on HPD low lose their LA, and the TV can't address them.
- **Microcontroller TVs violate timing.** The kernel guide suggests sending `<Standby>` and `<Active Source>` broadcasts twice, and `<Image View On>` first, before anything else. This is policy in the userspace daemon, not the driver.
- **Serial adapters (Pulse-Eight etc.)** need `inputattach` plus systemd/udev glue. That's another reason for vendor USB.

### What the design does about it

1. The PA comes from the device (EDID, else persisted in flash) or a module param. The adapter is configured before any client opens it.
2. HPD low does not invalidate the PA. HPD and 5V go to userspace as pin events.
3. Firmware TX budget is 1 s, then `ERROR`. Seq numbers on every transmit.
4. Distro kernels lack the two newest core fixes, so item 3 is what keeps the driver safe. Backporting them is an option for kernels we build ourselves (FHS notes at the end).
5. Firmware fault injection, to test the timeout and seq paths against the real core.

## Do we encode packets ourselves?

- **CEC bit encoding: no.** The STM32 CEC peripheral already handles start bits, timing, EOM/ACK and arbitration. The kernel hands us whole frames: header + opcode + operands, ≤16 bytes (`struct cec_msg`).
- **Host↔device transport: yes.** Linux defines no wire format between the driver and an adapter. Every in-tree adapter has its own. We need a small binary protocol that carries frames, TX results and config commands. Spec below.

## Transport: serial vs others

The production board puts the STM32F072CB on USB-C directly (PA11/PA12, crystal-less USB FS). There is no USB-UART bridge, so any transport means a USB stack in firmware, serial included.

| Option | Kernel side | Detection | Verdict |
|---|---|---|---|
| CDC-ACM serial + serio driver (Pulse-Eight / RainShadow / Extron model) | `serio_driver`, needs a new `SERIO_*` proto ID in uapi `serio.h` | none in the kernel. A udev rule on VID:PID starts `inputattach --<proto> /dev/ttyACMx` as a daemon to hold the tty open | no |
| USB HID + `hid_driver` | binds over hid-generic by VID:PID | automatic | workable. Fixed-size reports, no gain over vendor class |
| **USB vendor class + `usb_driver`** | native USB driver | automatic: `MODULE_DEVICE_TABLE(usb)` → modalias → modprobe on plug | **recommended** |

Why not serial:

- Every in-tree serial CEC driver (checked: pulse8, rainshadow, extron) is a serio driver. It needs a userspace helper and a uapi ID we can't allocate out of tree.
- The kernel can't tell our `ttyACM` apart from any other. Detection would live in udev + systemd, not the driver.
- Byte stream: needs framing (COBS/SLIP), resync and a checksum. USB transfers are already framed and CRC-checked.
- The ST-LINK VCP problems in the firmware README (DTR toggle, stuck reader) come from tty semantics. A vendor interface doesn't have them.

Why vendor class:

- The kernel binds by VID:PID + interface class. No helper, no port discovery. `probe()` runs, registers the adapter with the USB interface as its parent, and `/dev/cecN` appears.
- Release firmware has a single vendor interface. Debug firmware (`FHS_CEC_DEBUG_SHELL`) adds a CDC-ACM interface 1 with the bench shell, and `cdc_acm` binds that one. The driver matches only interface 0, so it works the same with either build.

## Wire protocol (vendor interface)

Two channels, nothing else:

- **Host → device: vendor control requests on EP0.** Synchronous `usb_control_msg()` with a timeout. The USB stack reports delivery, so no ack/seq handling for config.
- **Device → host: one interrupt IN endpoint, 64 B.** One event per transfer. The driver keeps one URB always submitted.

Control requests (`bmRequestType` = vendor | interface):

| bRequest | dir | wValue / data | purpose |
|---|---|---|---|
| `GET_INFO` | IN | → proto version, fw version, caps bits, persisted PA | probe-time handshake, refuse unknown major |
| `SET_PHYS_ADDR` | OUT | wValue = PA | persist PA in device flash |
| `SET_ENABLE` | OUT | wValue 0/1 | host mode on/off |
| `SET_LOG_ADDRS` | OUT | wValue = 16-bit LA ack mask | `adap_log_addr` |
| `SET_MONITOR_ALL` | OUT | wValue 0/1 | `adap_monitor_all_enable` |
| `TRANSMIT` | OUT | wValue = seq; data = frame (1..16 B) | `adap_transmit`, returns at once |
| `GET_STATUS` | IN | → counters | `adap_status` debugfs |

wIndex is always the interface number (USB routes interface requests by it). The kernel's signal free time is not sent: CFGR is writable only with the peripheral disabled, which aborts reception, and the hardware already applies the spec's 3/5/7 bit periods.

Interrupt IN events, byte 0 = type:

| type | payload | driver action |
|---|---|---|
| `EV_TX_DONE` | seq, status (OK/NACK/ARB_LOST/LOW_DRIVE/ERROR) | `cec_transmit_attempt_done()` |
| `EV_RX` | len, frame bytes | `cec_received_msg()` |
| `EV_PHYS_ADDR` (not yet defined) | u16 PA or `0xffff` | `cec_s_phys_addr()` (EDID phase) |

- The core keeps at most one transmit in flight, but it moves on after its 2.1 s timeout. `seq` is required: without it, a late TX_DONE gets credited to the next message. The driver drops any TX_DONE whose seq ≠ the current one.
- HPD and 5V events are out: this board revision does not wire HDMI_HPD or HDMI_5V to the MCU.
- The constants live in one header, `driver/fhs-cec-proto.h`, which the firmware includes too (plain C, fixed-width types, no kernel headers). That includes `FHS_CEC_USB_VID` / `FHS_CEC_USB_PID`. One source of truth.

## Detection / binding

1. Firmware enumerates with our VID:PID: interface 0 class `0xFF`, plus CDC-ACM interface 1 in debug builds.
2. Driver table: `USB_DEVICE_AND_INTERFACE_INFO(VID, PID, 0xFF, sub, proto)`. Matching on interface keeps `cdc_acm` on interface 1.
3. DKMS installs the module and runs `depmod` → udev autoloads it on plug. `probe()`: `GET_INFO` → allocate adapter → submit IN URB → `cec_register_adapter(adap, &intf->dev)` → `cec_s_phys_addr()`.
4. `disconnect()`: kill URB, flush work, `cec_unregister_adapter()`. In-flight TX completes with `CEC_TX_STATUS_ERROR`.
5. Suspend/resume: kill/resubmit URB. Polish phase.

## Physical address

The core won't claim an LA without a valid PA, and libcec never sets one. The adapter must come up with a valid PA and no userspace step.

PA source, highest priority first:

1. `cec-ctl --phys-addr` at runtime (`CEC_CAP_PHYS_ADDR` stays set in every phase)
2. `phys_addr=` module param
3. firmware `EV_PHYS_ADDR` from EDID
4. firmware PA persisted in flash (`SET_PHYS_ADDR` writes it). Factory default `1.0.0.0`.

- **Initial:** sources 1, 2 and 4. Probe reads the persisted PA from `GET_INFO`.
- **EDID phase:** the board routes DDC SCL/SDA to the MCU (PB6/PB7). HPD is not wired, so the firmware has to find the EDID read some other way, e.g. by watching DDC traffic. It then sends `EV_PHYS_ADDR`.
  - The PA changes only when a new EDID read gives a different one (TVs drop HPD in standby).
  - Risk: DDC is shared with the source GPU (two masters). Firmware should read after the source finishes, retry on arbitration loss, or just sniff the source's own EDID read. Hardware/firmware problem, flagged here.

## Firmware changes this requires

- USB device stack: TinyUSB (MIT, F072 crystal-less USB, vendor + CDC classes).
- **Driver-controlled only.** The autonomous behaviour goes away: LA allocation, `respond()`, the 30× power retries and the `cec on/off/vol` logic. The kernel core and the userspace daemon on the host (system-agent on FHS) own all of that now.
  - After power-up: CEC peripheral initialised, ack mask empty, receive path off. The dongle is invisible on the bus until `SET_ENABLE=1`.
  - On `SET_ENABLE=0` or USB reset/disconnect: back to that idle state. A host that disappears never leaves the dongle holding an LA.
- The dongle is a dumb adapter:
  - LA mask → `CEC_CFGR.OAR` (already supports a mask)
  - one TX attempt per `TRANSMIT`, result as `EV_TX_DONE`. Hardware ARB retry is fine and reports as OK/ARB_LOST.
  - hard 1 s budget per TX, then `ERROR`. Never leave the core to hit its 2.1 s timeout.
  - a new `TRANSMIT` aborts any in-flight one, with no TX_DONE for the old seq
  - transmits with an empty ack mask (initiator 15) are allowed: unconfigured `<Image View On>` / poll
  - RX: own + broadcast frames, all frames if monitor-all. Full listening mode stays on (reduced mode wedges, per README).
  - the `cec_recover()` safety nets stay, but are silent and report stuck TX as `ERROR`.
- **Debug shell behind `#define FHS_CEC_DEBUG_SHELL`** (CMake option, on in the Debug preset, off in Release):
  - adds the CDC-ACM interface and keeps `console.c` / `shell.c`, frame printing, `cec dump` and `diag ...`
  - read-only by default: it prints what the driver does and never sends on its own, except `diag` commands and `cec send`, which are for the bench
  - fault injection for driver testing: `fault nack`, `fault late-txdone`, `fault drop-rx`, `fault wedge-tx`
  - Release builds compile all of it out. No text path exists on the vendor interface in either build.
- USART2 printf stays available behind the same define for SWD/UART bench work.
- PA persisted in flash (one page), written by `SET_PHYS_ADDR`.
- Today's code mapped to the new shape:
  - `cec.c` peripheral/ISR/recover: kept
  - responder, allocator, TV/volume helpers: deleted
  - `cec_diag.c`, `shell.c`, `console.c`: debug only
  - new `usb.c` (TinyUSB glue, descriptors) and `host.c` (control requests ↔ `cec.c`)

## Deliverable: DKMS package

One source tree, built by DKMS on any distro, FHS included. FHS integration is out of scope here; notes for it are at the end.

### Layout

```
driver/
  PLAN.md
  README.md           install, verify, uninstall, troubleshooting
  fhs-cec-proto.h     shared with firmware (includes FHS_CEC_USB_VID / _PID)
  fhs-cec.c           usb_driver + cec_adap_ops + event work
  Kbuild              obj-m := fhs-cec.o
  Makefile            make -C $(KDIR) M=$(CURDIR); KDIR defaults to /lib/modules/$(uname -r)/build
  dkms.conf
  70-fhs-cec.rules    /dev/cec-fhs symlink, group/mode
  VERSION             single source for the module version, dkms.conf and the tarball name
```

- Language: C. Kernel modules have no Go option.
- One file, about 400–500 lines, modelled on `pulse8-cec.c` minus the serio parts.
- Concurrency: IN URB completion copies the event into a small `kfifo` and calls `schedule_work()`. The work item calls the `cec_*` API. `adap_*` ops run under `adap->lock` in process context, so `usb_control_msg()` (sleeping) is fine there.

### `dkms.conf`

```
PACKAGE_NAME="fhs-cec"
PACKAGE_VERSION="<from VERSION>"
BUILT_MODULE_NAME[0]="fhs-cec"
DEST_MODULE_LOCATION[0]="/updates/dkms"   # ignored by DKMS 3.x, kept for 2.x
MAKE[0]="make KDIR=${kernel_source_dir}"
CLEAN="make clean"
AUTOINSTALL="yes"                         # rebuild on every kernel update
BUILD_EXCLUSIVE_CONFIG="CONFIG_USB CONFIG_CEC_CORE"
```

- `BUILD_EXCLUSIVE_CONFIG` makes DKMS skip kernels without USB or CEC core with a clear message, instead of failing the build.
- Fedora, Debian/Ubuntu and Arch all ship `CONFIG_CEC_CORE` (as a module or built in) because their GPU drivers and the Pulse-Eight driver select it. Our module only depends on `cec.ko`.

### Release artifact

`mise run driver:dkms` → `fhs-cec-<ver>.tar.gz` containing `dkms.conf`, sources, `VERSION`, the udev rule and README. Install:

```
sudo tar -xzf fhs-cec-<ver>.tar.gz -C /usr/src
sudo dkms install fhs-cec/<ver>
sudo install -m 644 /usr/src/fhs-cec-<ver>/70-fhs-cec.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

Plug in → `/dev/cec-fhs` appears, already configured with the PA stored in the dongle. Verify with `cec-ctl -d /dev/cec-fhs --playback` and `cec-ctl --to 0 --image-view-on`.

- Attached to GitHub releases of this repo. Distro packages (COPR/PPA/AUR) are an open question.
- Public release waits for the real VID:PID. Internal tarballs with the test ID are fine.

### Secure Boot

DKMS signs the module with the machine's MOK key. Ubuntu/Debian set that up automatically. Fedora and Arch need a one-time `mokutil --import` and enrollment at reboot. The README covers it, since "module not loading" on a Secure Boot machine is the most likely support issue.

### Kernel range and CI

- Supported: 6.1 LTS through mainline. The APIs used (CEC adapter, USB core, kfifo, workqueue) have been stable across that range. Any `LINUX_VERSION_CODE` guard lives in one compat header, and only when CI demands it.
- CI (GitHub Actions), compile-only:
  - `make KDIR=...` against headers for 6.1, 6.6, 6.12 and latest stable/mainline
  - `dkms build` inside Fedora, Debian and Arch containers. This exercises `dkms.conf` itself, not just the Makefile
  - warnings are errors
- Distro kernels lack the two core fixes (see Reliability findings). The firmware's 1 s TX budget and the driver's seq filtering keep the driver off those paths. Listed as a known limitation in the README.

### mise tasks

- `driver:build`: builds against the running kernel
- `driver:load` / `driver:unload`: insmod/rmmod for iteration without DKMS
- `driver:dkms`: build the release tarball
- `driver:dkms-install` / `driver:dkms-remove`: install the local tree through DKMS, to test the real install path

## Phases

Status: phases 1–3 written on `feat/cec-usb-driver`, untested on hardware. The userspace pyusb script in phase 1 was skipped.

1. **Protocol header + firmware vendor interface.**
   - TinyUSB vendor interface, plus CDC shell under `FHS_CEC_DEBUG_SHELL`
   - remove the autonomous responder/allocator; idle until `SET_ENABLE`
   - the control requests and events above
   - Test from userspace with a ~100-line pyusb/libusb script before any kernel code.
2. **Driver MVP.** probe/disconnect, enable, log_addr, transmit, rx, seq filtering, PA from `GET_INFO` / module param, `CEC_CAP_PHYS_ADDR`.
   Verify on a dev box with no PA step: plug in, `cec-ctl -d /dev/cec0 --playback`, then `cec-ctl --to 0 --image-view-on`, `--standby`, `--give-device-power-status`.
3. **DKMS packaging.** `dkms.conf`, `VERSION`, udev rule, README, tarball task, CI build matrix. From here on, all testing installs through DKMS.
4. **Compliance + soak.**
   - `cec-follower` + `cec-compliance -A` against the LG on the bench. Fix what it reports.
   - libcec: `cec-client -t p` with no prior `cec-ctl`. Must configure on its own.
   - `cec-ctl --stress-test-power-cycle` overnight. Zero `transmit timed out` in dmesg.
   - Fault injection: each `fault` mode, plus USB unplug mid-TX and `kill -9` of a client blocked in `CEC_TRANSMIT`. No hang, no WARN, adapter recovers.
   - Run on Fedora and Ubuntu installs at least.
5. **EDID PA.** Firmware reads EDID over DDC, `EV_PHYS_ADDR`.
6. **Polish.** monitor-all, debugfs status, suspend/resume. Optional: `CEC_CAP_MONITOR_PIN` via the existing `cec_diag` edge capture.
7. **Public release.** Swap in the real VID:PID, tag, publish the tarball.

## Notes for FHS integration (owned outside this plan)

Found while researching, kept here so they aren't lost:

- bootc `/usr` is read-only, so run DKMS in a Dockerfile build stage against the locked kernel's `kernel-devel` (`dkms install -k $KVER`), then copy the `.ko` into the image and `depmod`. Same shape as `build-nvidia-kmod.sh` / `setup-nvidia.sh`.
- The FHS kernel 7.1.4 lacks both CEC core fixes. Candidates for `os/kernel/patches/`: "media: cec/core: drop ABORTED/TIMEOUT check" (v7.3) and the 2026 delayed-work fix in `cec_transmit_msg_fh()`.
- The owner of `/dev/cecN` should take `CEC_MODE_INITIATOR | CEC_MODE_FOLLOWER`, claim one Playback LA, and own the retry policy: "TV not ready" retries, and sending `<Standby>` / `<Active Source>` twice.
- `system_agent_t` is permissive today. It needs an allow rule for the CEC device before it goes enforcing.

## Open questions

1. DKMS distribution: release tarball only, or also packages (Fedora COPR / Ubuntu PPA / AUR)?
2. License: driver and `fhs-cec-proto.h` are written as GPL-2.0 OR MIT (`MODULE_LICENSE("Dual MIT/GPL")`), pending FUTO legal.

