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
| State | Darwin 27.2 RAM boot reaches SpringBoard with the experimental A660 profile |

## State

This package is the OnePlus 9 port of the upstream HakiOS Darwin 26/27 device format. The hardware
values are derived from the phone's Android device tree and the earlier OnePlus 9 iOS 16.5
bring-up. Darwin 27.2 has since been booted from RAM on the phone, with SpringBoard, S6SY792 touch
and an EPSS CPU-frequency profile observed.

A660 uses the experimental linear sysmem path. The board also describes the three PMIC keys and BQ28Z610 gauge. For the current evidence and acceptance scope, see [status](../../../../../docs/STATUS.md).

## Hardware data present

- `cpufreq-epss`: SM8350 EPSS, groups 4 + 3 + 1 cores.
- `ddr-rpmh`: RPMh command DB and apps RSC.
- `display-scan`: DSI0, MDP INTF1 and the SM8350 GCC/DISPCC clock offsets.
- `gpu-a660`: Adreno 660 blocks, GMEM size, fuse location, speed table and debug mux.
- `touch-s6sy792`: QUPv3 SE4 I2C pins, interrupt/reset, supplies and coordinates.

## Known gaps

- GMEM coverage, additional render paths and sustained A660 UI validation.
- QCA6490 Wi-Fi, Bluetooth, ADSP, audio, camera, charging and sensors.
- UFS persistence, thermal control and long-run stability.
- The UEFI firmware and the phone's Android device tree are private inputs and are not committed
  here.

## Build

Build the kernel and generic loader in a prepared build environment.

```sh
python3 tools/boards.py check
BUILD_CONFIG=build.config.arm64.llvm kernel/build.sh -j4
arch/arm64/boot/build.sh
```

For a device-bound loader and the offline A660 package, follow [the reproduction steps](../../../../../docs/REPRODUCE.md).
