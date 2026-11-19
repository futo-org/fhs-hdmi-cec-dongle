# Changelog

## 0.1.0 (2026-11-19)


### Features

* **firmware:** add enter_dfu vendor request for jumperless usb flashing ([c737f81](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/c737f81a2f3cdb04bc3257b1dcf68f6bbc6f66f8))
* **firmware:** cec usb driver ([a9f166e](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/a9f166e44c295fd68a75e944bd02cfdc7657d79b))
* **firmware:** led on pa1 for the v0.2 board ([33cc4b2](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/33cc4b2800874e9908a3e78e9e7e8ac2a2e65179))
* **firmware:** serve display edid over usb via GET_EDID ([bc2c5fd](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/bc2c5fd19073496e7aac8285e91d04a4bc36a4c2))
* **firmware:** use internal pull-up on cec pin behind isolation fet ([9a034eb](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/9a034eb7412afa666d8e57cbbdb4685695e561b0))
* use futo usb vid:pid 3a4c:4663 ([c4702d3](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/c4702d3df3e1a50124712e348b16cc8dca698372))


### Bug Fixes

* **firmware:** drive status led on pb13 for v0.3 ([0b843b9](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/0b843b9f939f523d3076a34400cee4db3283e560))
* **firmware:** led on pa5 to match the v0.1 board ([3cd5645](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/3cd564505550ad57e6916de0d4d403ba83fe6973))
* **firmware:** map flash before startup init so the app survives dfu leave ([301cea4](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/301cea42d34fa90b128b820d2d57d2a16a262b3e))
* **firmware:** map main flash at boot so the app runs after dfu leave ([f5ad0ef](https://github.com/futo-org/fhs-hdmi-cec-dongle/commit/f5ad0ef9dc5d42df124b46a5790ef4af808f170e))
