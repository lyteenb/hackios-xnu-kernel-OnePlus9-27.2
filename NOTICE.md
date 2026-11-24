# Licences and third-party notices

## Repository provenance

This repository is a public, source-only integration snapshot for the OnePlus 9 Darwin 27.2 work.
It is derived from:

- `oosexgang/hakios-xnu-kernel` — the HakiOS kernel, loader, Darwin user-space tooling, device
  package format, tests, documentation and licence structure.
- `b1ackm4x/hackios-xnu-kernel` — the earlier OnePlus 9 iOS 16.5 integration and the hardware facts
  used to prepare this tree.

`lyteenb/hackios-xnu-kernel` is a Windows reproduction of the `b1ackm4x/hackios-xnu-kernel` tree and
was used as a Windows/validation reference. It is not a third source upstream, and its firmware,
system images and release packages are not distributed here.

The snapshot corresponds to `hakios-27.2-lemonade` commit `8a61f288`. The upstream repositories
remain authoritative for their respective code and history. Nothing in this notice changes the
licence attached to an individual file.

## This fork's work

- `arch/arm64/boot/dts/lemonade/` is the OnePlus 9 device package. Its data was read from the
  phone's Android device tree and earlier OnePlus 9 bring-up; it is distributed under MIT, as the
  upstream device-package format and board data are. The data itself is not Apple code.
- `drivers/board/s6sy792.c` is the OnePlus 9 S6SY792 touch driver. It is part of the kernel overlay
  and is distributed under APSL-2.0. It contains no vendor firmware.
- `arch/arm64/configs/phone_27_2_perf_defconfig`, the public OnePlus 9 workflows, and the OnePlus 9
  documentation are MIT.
- Changes to Apple's XNU files, the HakiOS kernel overlay, loader and Darwin user space remain under
  the licence of the file they modify, normally APSL-2.0.
- The public documentation in `docs/` describes only public facts. It does not grant rights to any
  Apple, Qualcomm or OnePlus file that a user obtains separately.

## Upstream HakiOS work

- `kernel/overlay/`, `kernel/patches/` and the parts of the kernel's tree kept at the top
  (`arch/arm64/kernel/`, `arch/arm64/include/`, `arch/arm64/configs/xnu/`, `crypto/`, `fs/`,
  `security/`; `kernel/lib.sh` lists them) modify and extend Apple's XNU, published under the Apple
  Public Source License 2.0, and are distributed under the same licence
  (`LICENSES/other/APSL-2.0`).
- `userland/` is HakiOS with our changes; it keeps HakiOS's licence
  (`LICENSES/other/APSL-2.0`). So do `arch/arm64/boot/` and `kernel/deps/`, which were HakiOS's
  `xnuloader/` and `xnu-deps/`.
- `components/`: each file keeps the licence of where it came from. What came from `userland/` and
  `darwin/` keeps HakiOS's licence; `components/metal/plugin/` and files with an
  `SPDX-License-Identifier` say their own.
- `tools/`, `workbench/`, `arch/arm64/boot/dts/`, `bench`, `build.config.*` of this repository and the
  documentation written for it are MIT (`LICENSES/preferred/MIT`).

## Files inside the kernel overlay that keep their own licence

- `fs/apfs/libdeflate/` (the tree's `bsd/miscfs/apfs/libdeflate/`), `apfs_deflate.c`: libdeflate,
  MIT (Eric Biggers; Google LLC).
- `fs/apfs/libzbitmap/`, `apfs_zbitmap.c`: libzbitmap, GPL-2.0+ OR MIT, taken under MIT
  (Corellium LLC).

## Linux and other reference implementations

Our drivers (`bsd/dev/board/*.c`, `BoardGPU.cpp`, ...) name the Linux drivers whose register layouts
and bring-up order they follow (msm/adreno, qcom clocks and PHYs, cs35l41, spi-geni, ...). They are
written for xnu, not copied. The optional Wi-Fi build script (`kernel/lx/build.sh`) fetches Linux's
ath11k (BSD-3-Clause-Clear, with an ISC header) and FreeBSD's LinuxKPI headers (BSD-2-Clause).
Those fetched sources are not in this repository. The current configuration does not link an
external Wi-Fi object.

The Bluetooth controller's firmware is sent as Linux's Qualcomm Bluetooth driver (`btqca.c`) sends
it; `board/bt_qca.h` is written from what that driver does, not from its code.

## Other ports of Darwin to phones

What other ports found about Darwin's own side is credited where it is used, in the comment beside
the code and in the commit: the OnePlus Nord 2 (MediaTek) port of iOS, `frxncyyy/ios-denniz`,
branch `nord2-port-20261116` (its kernel pieces under APSL-2.0, with its own credits in
`LICENSES-AND-CREDITS.md`), and through it HakiOS's MediaTek port. The upstream `NOTICE.md` lists
the exact files and ideas taken from that work.

## Excluded from this repository

Firmware and binaries of others are not repository content. `tools/check-publishable.sh` looks for
them: Apple's Darwin files and shaders, Qualcomm/OnePlus firmware (ADSP, GPU, Wi-Fi, Bluetooth,
touch), device trees, prebuilt UEFI drivers, and private device identities. Apple IPSW, rootfs,
cryptex, system images and device-bound loader outputs are never committed here.
