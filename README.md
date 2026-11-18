# FHS CEC Dongle

The source repository for the FHS CEC Dongle. It gives Linux machines without CEC hardware an HDMI-CEC adapter: plug it in and the `fhs-cec` driver exposes it as `/dev/cecN` through the kernel's CEC framework.

| Path | What |
|---|---|
| `firmware/` | STM32F072 firmware |
| `driver/` | Linux kernel driver, DKMS package |
| `hardware/` | PCB and case |

## Releases

[release-please](https://github.com/googleapis/release-please) runs three release trains from conventional commits on `master`, each with its own release PR and tag:

| Train | Path | Tag | Assets |
|---|---|---|---|
| hardware | `hardware/` | `hardware-vX.Y.Z` | PCB and panel production files, STEP, case, programming package |
| firmware | `firmware/` | `firmware-vX.Y.Z` | `fhs-cec-firmware.hex`, `fhs-cec-firmware.bin` |
| driver | `driver/` | `driver-vX.Y.Z` | `fhs-cec-<version>.tar.gz` DKMS tarball |
