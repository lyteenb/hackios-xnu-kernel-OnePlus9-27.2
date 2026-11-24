<!-- SPDX-License-Identifier: MIT -->
# 复现步骤

本文只描述验证路径，不包含 Apple 资产、厂商固件、设备身份或私有 artifact。Windows 用来做设备
操作和可选 QEMU；XNU/loader/Metal 的 Linux-only 构建放在 GitHub Actions。

## 0. 准备

需要：

- 一台 OnePlus 9 LE2110，能够执行 `fastboot boot`；
- Windows 10/11、PowerShell 7；
- `adb`、`fastboot`；
- Python 3.11+；
- 自己合法持有的 iPhone12,1 iOS 27.2 `24B119` IPSW；
- 与设备匹配的 UEFI loader 输入和私有启动包制作环境；
- 可选：Windows 原生 QEMU，用于真机前验证。

不要把 IPSW、`root.apfs`、`system.apfs`、`DeviceTree.bin`、UEFI、firmware 或完整日志提交到
公开仓库。临时文件应放在仓库外或 `.gitignore` 排除的目录中。

### 是否需要 root / ADB 导出

当前 perf 运行包的**启动阶段不需要 root**。`launch.py` 在 Android 中只使用 `adb devices` 和
`adb reboot bootloader`，之后只用 `fastboot boot`，不执行 `adb shell`、`su` 或分区写入。

但**组包阶段需要区分三类输入**：

| 输入 | 来源 | 当前 perf 配置是否需要 |
| --- | --- | --- |
| `Uefi/SILICIUM_UEFI.fd`、`BootShim.bin`、`device-tree.dtb` | OnePlus 9 的设备/UEFI 私有输入 | 需要；已有同一设备输入时可复用，不必每次重新导出 |
| `Release/system27-prune.apfs`、`root26-darwin.apfs`、`loader.efi` | 合法 IPSW 和私有镜像构建 | 需要；不是从 Android root 目录直接读取 |
| `HardwareSerialSha256` | ADB serial 的 SHA-256 | 需要；只保存哈希，不需要 root |
| GPU/Wi-Fi/ADSP/audio/camera firmware | 本机 Android/vendor 文件 | **当前配置不需要**：`gpu=0`、`wifi=0`、`adsp=0`、`sound=0`、`cam=0` |
| S6SY792 固件 | 触控控制器 | **不需要上传固件**；驱动只做板级 I2C 初始化和扫描/上报命令 |

因此：同一台已准备的 OnePlus 9、已有 UEFI/DTB 输入和合法 IPSW 时，不需要每次 root 导出；
换一台手机或启用 GPU/Wi-Fi/音频等外设时，才需要为该设备补充相应私有输入。

## 1. 在 GitHub Actions 构建公开部分

在仓库页面手动运行：

- `OnePlus 9 Darwin 27.2 public build`（`.github/workflows/lemonade-27.2-ci.yml`）；
- 如需要 Metal 插件，再运行 `OnePlus 9 Darwin 27.2 Metal plugin`
  （`.github/workflows/lemonade-27.2-metal.yml`）。

公开构建会：

1. 安装 LLVM/Clang 和基础工具；
2. 执行 `python3 tools/boards.py check`；
3. 构建 `mach.development.bcm2837`；
4. 构建 `loader.efi`；
5. 生成 SHA-256 清单；
6. 只上传 kernel、loader 和校验和。

Metal workflow 只上传 `HakiosMetal.bundle` 和校验和。不要把私有系统镜像混入这些 artifact。

## 2. 在私有环境制作 Darwin 镜像

这一步必须使用你自己的 Apple 资产，并在私有仓库、私有 runner 或自己的 macOS/Linux 环境中完成。
本项目不提供可直接公开分发的系统镜像。

上游工具的脚本顺序（这些脚本是 POSIX 工具，不应在本机 Windows 上直接运行）：

```sh
# 以下命令只举例说明私有 Linux runner 中的顺序；不要在公开仓库提交输出
bash darwin/tools/unpack.sh "<YOUR_27_2_IPSW>" "<IOS_DIR>"
BUILD=24B119 bash darwin/userland/inj26.sh
bash darwin/userland/root26-darwin.sh
python3 darwin/tools/mkimage26.py --prune "<IOS_DIR>/system27-prune.apfs"
```

实际构建时以当前源码中的脚本参数和 workflow 为准。需要得到：

- `root.apfs`：小型 `/hakios` 用户态镜像；
- `system.apfs`：Darwin 系统/Preboot 相关镜像；
- kernel：公开构建的 `mach.development.bcm2837`；
- `loader.efi`：与设备/loader 输入匹配的 UEFI loader；
- `boot.img`：loader-only 的 Android boot image；
- `mkpayload.py`：打包 kernel 和 root 的工具；
- 可选 `launch.py`：设备校验、启动、停止和返回 Android。

如果你的私有流程使用 GitHub Actions，使用私有仓库/私有 workflow，并确保 artifact 只对授权用户可见。
不要使用公开仓库的 release 或公开 transfer branch 传输 Apple 资产。

## 3. Windows 公开工具

私有输入准备好后，公开仓库里的 Windows 脚本负责组包和 QEMU 检查，不修改 PATH，也不上传资产：

```powershell
.\tools\windows\build_ramboot_package.ps1 `
  -Release C:\path\to\private\release `
  -Kernel C:\path\to\mach.development.bcm2837 `
  -Uefi C:\path\to\oneplus9-uefi `
  -Out C:\path\to\ramboot-package `
  -HardwareSerialSha256 <64-character-sha256>
```

`HardwareSerialSha256` 是 ADB serial 的 SHA-256，不是明文 serial。详细说明见
[`tools/windows/README.md`](../tools/windows/README.md)。组包完成后先运行：

```powershell
python .\launch.py verify
```

QEMU 检查：

```powershell
.\tools\windows\run_ramboot_qemu.ps1 `
  -Release C:\path\to\private\release `
  -Kernel C:\path\to\mach.development.bcm2837 `
  -Qemu C:\path\to\qemu-system-aarch64.exe `
  -QemuShare C:\path\to\qemu\share
```

## 4. 组合并校验发布包

推荐发布包结构：

```text
<package>/
  boot.img
  kernel
  root.apfs
  system.apfs
  mkpayload.py
  launch.py          # 可选，已验证的包可提供
  manifest.json
  SHA256SUMS
  Start.ps1          # 可选
  Return-to-Android.ps1
```

至少对以下文件做 SHA-256：

```powershell
Get-FileHash -Algorithm SHA256 .\boot.img, .\kernel, .\root.apfs, .\system.apfs, .\mkpayload.py
```

如果使用 `launch.py`：

```powershell
python .\launch.py verify
python .\launch.py status
```

`verify` 必须成功。任何文件大小或哈希不匹配都不要继续启动。

## 5. 先做 QEMU 验证（可选但强烈建议）

QEMU 需要自己的 UEFI firmware、QEMU 安装和已组装的镜像。已完成的验证使用 Windows 原生 QEMU、
AArch64 UEFI、ramfb、virtio input 和一条 loader payload。

核心启动参数如下，具体镜像路径按你的包替换：

```text
cpufreq=0 cpus=4 gpu=0 metal=0 wifi=0 adsp=0 ufs=0 ufs_native=0 ufs_write=0 var_store=0
backlight=1 touchscreen=1 buttons=0 battery=0
launchd_unsecure_cache=1
apfs_volume=md0,v=1 apfs_volume_at=/private/preboot
```

验证目标：

1. UEFI 启动 loader；
2. loader 把 kernel/root payload 放入内存；
3. `launchd.darwin` 开始运行；
4. dyld shared cache 能加载；
5. `backboardd` 和 SpringBoard 出现；
6. 画面从 UEFI 文本控制台切换到 Apple 锁屏/SpringBoard；
7. 没有 `panic`、`UEFI EXCEPTION` 或启动链错误。

QEMU 只用于验证启动链，不用于证明 OnePlus 9 的 panel、touch、UFS、Wi-Fi 或电池驱动可用。

## 6. 真机 RAM boot

先确认 Android 和 bootloader：

```powershell
adb devices
adb reboot bootloader
fastboot devices
```

只执行以下启动命令：

```powershell
fastboot boot .\boot.img
```

loader 出现后，按发布包的流程：

1. 上传 `system.apfs` 到 RAM；
2. 让 loader 启动 kernel/root payload；
3. 等待 Darwin、lockdownd/backboardd/SpringBoard 启动；
4. 检查物理触控、显示和 CPU 频率日志；
5. 记录现象后停止运行。

如果发布包提供 `launch.py`，等价流程是：

```powershell
python .\launch.py start
python .\launch.py status
python .\launch.py log
```

## 7. 返回 Android

优先使用发布包提供的方式：

```powershell
python .\launch.py android
```

或者手动重启：

```powershell
fastboot reboot
```

确认：

```powershell
adb wait-for-device
adb shell getprop sys.boot_completed
```

只有返回 `1` 才说明 Android 启动完成。

## 8. 绝对禁止的操作

以下命令不属于本项目，可能破坏分区、bootloader 状态或数据：

```text
fastboot flash ...
fastboot erase ...
fastboot set_active ...
```

不要用刷写分区来修复实验性启动失败。RAM boot 的失败处理是停止、重启回 Android、检查包和日志，
而不是改写设备分区。

## 9. 复现验收清单

- [ ] 公开 Actions 构建成功；
- [ ] `SHA256SUMS` / manifest 校验通过；
- [ ] QEMU 或已知可用的私有 QEMU 环境到达锁屏；
- [ ] 真机只使用 `fastboot boot`；
- [ ] `SpringBoard` 出现；
- [ ] S6SY792 触控初始化并上报触点；
- [ ] CPU 频率日志符合预期；
- [ ] 无 panic、无 UEFI 异常；
- [ ] 返回 Android 且 `sys.boot_completed=1`；
- [ ] 没有提交或公开任何 Apple/vendor/identity 资产。
