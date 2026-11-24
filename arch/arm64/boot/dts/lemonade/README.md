<!-- SPDX-License-Identifier: MIT -->
# OnePlus 9 LE2110 (`lemonade`)

| | |
| --- | --- |
| Device | OnePlus 9 LE2110 |
| SoC | Qualcomm SM8350 / Snapdragon 888 |
| Panel | `qcom,mdss_dsi_samsung_amb655x_dsc_cmd`, 1080 x 2400 |
| GPU | Adreno 660, chip-id `0x06060001`, 1536 KiB GMEM |
| Touch | Samsung S6SY792, I2C address `0x48` |
| UEFI | `SILICIUM_UEFI.fd`, base `0x9FD00000`, size `0x200000`, boot header v3 |
| State | Darwin 27.2 RAM boot reached SpringBoard; touch and CPU-frequency profile verified |

## State

This package is the OnePlus 9 port of the upstream HakiOS Darwin 26/27 device format. The hardware
values are derived from the phone's Android device tree and the earlier OnePlus 9 iOS 16.5
bring-up. Darwin 27.2 has since been booted from RAM on the phone, with SpringBoard, S6SY792 touch
and an EPSS CPU-frequency profile observed.

The GPU kernel path starts, but A660 Metal user space is not complete. The 27.2 HakiosMetal tables
are A635-only, so CoreAnimation currently falls back to software rendering. See
[`docs/STATUS.md`](../../../../../docs/STATUS.md) and
[`docs/LIMITATIONS.md`](../../../../../docs/LIMITATIONS.md).

## Hardware data present

- `cpufreq-epss`: SM8350 EPSS, groups 4 + 3 + 1 cores.
- `ddr-rpmh`: RPMh command DB and apps RSC.
- `display-scan`: DSI0, MDP INTF1 and the SM8350 GCC/DISPCC clock offsets.
- `gpu-a660`: Adreno 660 blocks, GMEM size, fuse location, speed table and debug mux.
- `touch-s6sy792`: QUPv3 SE4 I2C pins, interrupt/reset, supplies and coordinates.

## Known gaps

- Remaining A660 Metal tables and user-space rendering paths.
- QCA6490 Wi-Fi, Bluetooth, ADSP, audio, camera, battery, buttons and sensors.
- UFS persistence, thermal control and long-run stability.
- The UEFI firmware and the phone's Android device tree are private inputs and are not committed
  here.

## Build

The public workflow builds only the kernel and a generic loader:

```sh
python3 tools/boards.py check
BUILD_CONFIG=build.config.arm64.llvm kernel/build.sh -j4
arch/arm64/boot/build.sh
```

The workflow must not upload Apple system files, IPSW contents, the phone's firmware, its device
identity or private logs.
