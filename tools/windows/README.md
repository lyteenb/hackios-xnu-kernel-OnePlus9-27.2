<!-- SPDX-License-Identifier: MIT -->
# Windows RAM boot tools

These scripts make the Windows path explicit without changing the system or user `PATH`, and without
placing Apple/vendor assets in the repository.

## Package builder

Given a private directory containing `system27-prune.apfs`, `root26-darwin.apfs` and `loader.efi`,
a built kernel, and the phone's UEFI build, run:

```powershell
.\tools\windows\build_ramboot_package.ps1 `
  -Release C:\path\to\private\release `
  -Kernel C:\path\to\mach.development.bcm2837 `
  -Uefi C:\path\to\oneplus9-uefi `
  -Out C:\path\to\ramboot-package `
  -HardwareSerialSha256 <64-character-sha256>
```

The manifest stores only the SHA-256 of the ADB serial, never the serial itself. Compute it in the
same PowerShell session if needed:

```powershell
$serial = (adb -s <device-serial> get-serialno).Trim()
$bytes = [Text.Encoding]::UTF8.GetBytes($serial)
[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
```

The builder calls the repository's `tools/image.py`, `mkpayload.py` and `mkboot.py`, then writes
`boot.img`, `kernel`, `root.apfs`, `system.apfs`, `launch.py`, `manifest.json`, `SHA256SUMS`, and
Windows start/return helpers.

Prepare and verify the UEFI/DTB directory and serial hash for your phone; do not reuse device-specific inputs from another device.

The builder itself does not run `su`. Obtaining `device-tree.dtb` or vendor firmware from a phone may
require root/ADB depending on how the private UEFI input is prepared. The current `phone_27_2_perf` profile
does not need GPU, Wi-Fi, ADSP, audio or camera firmware; S6SY792 touch needs no firmware upload. Even so,
prepare and hash-check the current device's UEFI/DTB before building `boot.img`.

## QEMU check

After producing the private release directory and kernel:

```powershell
.\tools\windows\run_ramboot_qemu.ps1 `
  -Release C:\path\to\private\release `
  -Kernel C:\path\to\mach.development.bcm2837 `
  -Qemu C:\path\to\qemu-system-aarch64.exe `
  -QemuShare C:\path\to\qemu\share `
  -Seconds 1200
```

The script uses AArch64 UEFI, ramfb, virtio input and the RAM payload. It prints the serial log and
returns failure unless SpringBoard is observed.

## Package controls

After the private package is built:

```powershell
python .\launch.py verify
python .\launch.py status
python .\launch.py start
python .\launch.py log
python .\launch.py android
```

Only `fastboot boot` is used. The scripts do not flash, erase or activate a partition.

The scripts require the user's legally obtained IPSW-derived images, OnePlus 9 UEFI/firmware and
device-specific inputs. Do not commit or publish those files.
