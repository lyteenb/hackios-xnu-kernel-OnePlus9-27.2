<!-- SPDX-License-Identifier: MIT -->
# UEFI port pin

This directory records the Mu-Silicium revision expected by the OnePlus 9 UEFI port. It contains no
UEFI binary, vendor driver, device tree or firmware.

The external UEFI input used by `tools/windows/build_ramboot_package.ps1` must provide:

- `SILICIUM_UEFI.fd`
- `BootShim.bin`
- `device-tree.dtb`

Those files are private device inputs and must be prepared for the phone in use. See
[`Documentation/porting.md`](../../../../Documentation/porting.md) and
[`tools/windows/README.md`](../../../../tools/windows/README.md).
