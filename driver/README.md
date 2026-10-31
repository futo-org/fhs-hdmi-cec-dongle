# fhs-cec

Linux driver for the FHS HDMI-CEC dongle. Plug it in and it shows up as `/dev/cecN` (symlink `/dev/cec-fhs`), driven by the kernel CEC framework. `cec-ctl`, libcec and `cec-follower` work unchanged.

Needs kernel 6.1 or newer with `CONFIG_USB` and `CONFIG_CEC_CORE` (every mainstream distro kernel has both).

## Install

Build tools, kernel headers and DKMS first:

| Distro | Packages |
|---|---|
| Fedora | `dkms kernel-devel` |
| Debian / Ubuntu | `dkms linux-headers-$(uname -r)` |
| Arch | `dkms linux-headers` |

Then:

```
sudo tar -xzf fhs-cec-<ver>.tar.gz -C /usr/src
sudo dkms install fhs-cec/<ver>
sudo install -m 644 /usr/src/fhs-cec-<ver>/70-fhs-cec.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

DKMS rebuilds the module on every kernel update.

## Verify

Plug the dongle in, then:

```
cec-ctl -d /dev/cec-fhs --playback          # claim a Playback address
cec-ctl -d /dev/cec-fhs --to 0 --image-view-on
cec-ctl -d /dev/cec-fhs --to 0 --give-device-power-status
```

`cec-ctl` is in the `v4l-utils` package.

## Physical address

The driver sets the HDMI physical address at plug-in, so libcec works with no setup. It reads it from the display's EDID on boards that support it, else uses the one stored in the dongle's flash (factory default `1.0.0.0`). EDID is read once, at plug-in.

Change the stored value (survives replug and reboot):

```
echo 2.0.0.0 | sudo tee /sys/bus/usb/drivers/fhs-cec/*/stored_phys_addr
```

Overrides, highest priority first:

- `cec-ctl -d /dev/cec-fhs --phys-addr 2.0.0.0`: until replug
- `phys_addr=0x2000` module parameter: ignores EDID and the stored value

## Secure Boot

DKMS signs the module with the machine's MOK key.

- Debian / Ubuntu: set up automatically on first DKMS install. Enroll the key at the blue MOK screen on the next boot.
- Fedora / Arch: `sudo mokutil --import /var/lib/dkms/mok.pub`, reboot, enroll.

`modprobe: ERROR: could not insert 'fhs_cec': Key was rejected by service` means the key is not enrolled.

## Uninstall

```
sudo dkms remove fhs-cec/<ver> --all
sudo rm -r /usr/src/fhs-cec-<ver> /etc/udev/rules.d/70-fhs-cec.rules
```

## Known limitations

- Kernels before 7.3 stop claiming a logical address if one poll times out. The dongle reports every transmit within 1 s, so the driver never hits that timeout.
- Killing a process while it is blocked in `CEC_TRANSMIT` can trip a use-after-free in the CEC core on kernels without the 2026 delayed-work fix. Not specific to this driver.

## Development

```
mise run build        # against the running kernel
mise run load         # insmod, rmmod with: mise run unload
mise run dkms         # release tarball
mise run docker-build # compile test in Debian and Fedora containers
```
