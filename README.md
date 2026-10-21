# FHS CEC Dongle

The source repository for the FHS CEC Dongle. It gives Linux machines without CEC hardware an HDMI-CEC adapter: plug it in and the `fhs-cec` driver exposes it as `/dev/cecN` through the kernel's CEC framework.

| Path | What |
|---|---|
| `firmware/` | STM32F072 firmware |
| `driver/` | Linux kernel driver, DKMS package |
| `hardware/` | PCB and case |
