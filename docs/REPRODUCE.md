<!-- SPDX-License-Identifier: MIT -->
# 复现步骤

目标是 OnePlus 9 LE2110（`lemonade`、SM8350）和 iPhone12,1 的 Darwin 27.2 / iOS 27.2（`24B119`）。
流程用 USB RAM boot。把 Apple 与设备输入、生成物、设备绑定信息和原始日志保存在仓库外。

## 1. 准备输入与环境

| 输入 | 用途 |
| --- | --- |
| 自己持有的 iPhone12,1 `24B119` IPSW | 解出系统、Cryptex、dyld 缓存和生成 loader 身份所需的 Apple 设备树 |
| iPhoneOS 16.5 SDK、LLVM 20、`ldid` | 在 Linux 编译 kernel、loader 和 arm64e 用户态 |
| `SILICIUM_UEFI.fd`、`BootShim.bin`、`device-tree.dtb` | 与 LE2110 匹配的 UEFI、启动垫片和设备树，参见 [设备移植说明](../Documentation/porting.md) |
| 自己设备对应的六个 A660 固件文件 | `a660_gmu.bin`、`a660_sqe.fw`、`a660_zap.mdt`、`a660_zap.b00`、`a660_zap.b01`、`a660_zap.b02` |
| Windows PowerShell 7、Python 3.11+、Android platform-tools | 组包、校验、通过 USB 启动和回到 Android |

Linux 构建依赖见 [构建说明](../Documentation/building.md#linux)。可先运行
`scripts/setup-ubuntu.sh --check kernel loader` 检查基础软件，再按 [Metal 构建说明](../Documentation/metal-plugin.md)
准备 Mesa 构建依赖。签名工具需支持 `ldid -Cadhoc`，供 Darwin 27 的 `stage0` 使用。解包还需要 `ipsw`、`7z` 和 `unzip`，详见 [解包脚本](../darwin/tools/unpack.sh)。
使用可执行 `fastboot boot` 的手机，并提前完成 Windows USB 驱动安装和 Android ADB 授权。
触控通过板级 I2C 初始化。

## 2. 在 Linux 构建

在仓库根目录的 Bash 中执行。先设置以下路径，使用自己的私有工作目录、IPSW、SDK 和签名工具。
内核构建会按固定版本准备依赖。可以把自己取得的 macOS 13.3 SDK 置于 `$XNUDEPS/MacOSX13.3.sdk`，提供 DriverKit 头文件。

```sh
export PRIVATE_WORK=/path/to/private-work
export IPSW=/path/to/iPhone12,1_27.2_24B119_Restore.ipsw
export DARWIN_SDK=/path/to/iPhoneOS16.5.sdk
export LDID=/path/to/ldid
export XNU_CLANG_DIR=/usr/lib/llvm-20/bin
export PATH="$XNU_CLANG_DIR:$PATH"
export BUILD_CONFIG=build.config.arm64.llvm
export IOS="$PRIVATE_WORK/ios27" BOARD=lemonade
export OBJ="$PRIVATE_WORK/obj27"
export PRIVATE_RELEASE="$PRIVATE_WORK/release"
export XNU_SRC="$PRIVATE_WORK/xnu-12377-hakios"
export XNUDEPS="$PRIVATE_WORK/xnu-deps"

mkdir -p "$PRIVATE_RELEASE"
python3 tools/boards.py check
source kernel/env.sh
kernel/build.sh -j4
bash darwin/tools/unpack.sh "$IPSW" "$IOS"
IDENTITY="$IOS/DeviceTree.bin" \
  LOADER_CC="$XNU_CLANG_DIR/clang" LOADER_LINK="$XNU_CLANG_DIR/lld-link" \
  arch/arm64/boot/build.sh
BUILD=24B119 bash darwin/userland/inj26.sh
bash darwin/userland/root26-darwin.sh
```

`inj26.sh` 构建 `pacstrip` 和签名后的 Darwin 插件，`root26-darwin.sh` 生成小型根镜像、
`stage0` 和 `$IOS/jobs26`。组包用的 loader 应包含本仓库的 LE2110 BQ28Z610 采样支持。

按 [Darwin 27 的缓存步骤](../Documentation/darwin26-boot.md#darwin-27) 处理对应的
dyld 缓存及 FIPS 校验记录。用该节的 24B119 示例或自己同一构建的日志设置
`FIPS_STORED`（`In variable`）和 `FIPS_COMPUTED`（`MAC generated`）。
若选择另一套缓存或 pacstrip，使用那套产物自己的记录。

```sh
(
  shopt -s nullglob
  arch/arm64/tools/pacstrip cache "$IOS/dyld-strip" \
    "$IOS/dyld/dyld_shared_cache_arm64e" "$IOS"/dyld/dyld_shared_cache_arm64e.[0-9]*
)
python3 darwin/tools/fips26.py "$IOS/dyld-strip" "$FIPS_STORED" "$FIPS_COMPUTED"
METAL= LAUNCHDAEMONS="$IOS/jobs26" WORK="$IOS/image-tree-prune" \
  python3 darwin/tools/mkimage26.py --prune "$IOS/system27-prune.apfs"

METAL=1 LAUNCHDAEMONS="$IOS/jobs26" WORK="$IOS/image-tree-metal-donor" \
  python3 darwin/tools/mkimage26.py --prune --metal "$IOS/system27-metal-donor.apfs"
```

第二个镜像供资源工具读取与系统同一 build 的 Metal 库。只将最终修补后的 `system.apfs` 交给 USB，
它必须小于 4 GiB。

单独构建 A660 用的 Metal 插件，使小型根镜像保持为覆盖前的状态。
`metal.sh` 生成带签名和 chained fixups 的 arm64e Mach-O。

```sh
make -C components/metal/plugin test
OBJ="$PRIVATE_WORK/metal-obj" bash darwin/userland/metal.sh
mkdir -p "$PRIVATE_RELEASE/HakiosMetal.bundle"
cp "$PRIVATE_WORK/metal-obj/HakiosMetal" "$PRIVATE_RELEASE/HakiosMetal.bundle/"
cp components/metal/plugin/plugin/HakiosMetal.plist "$PRIVATE_RELEASE/HakiosMetal.bundle/Info.plist"
cp "$XNU_SRC/BUILD/obj/mach.development.bcm2837" "$PRIVATE_RELEASE/kernel"
cp arch/arm64/boot/loader.efi "$PRIVATE_RELEASE/"
cp "$IOS/root26-darwin.apfs" "$IOS/system27-prune.apfs" \
   "$IOS/system27-metal-donor.apfs" "$PRIVATE_RELEASE/"
```

## 3. 在 Windows 组包

把上述私有 `release` 目录交给 Windows。在公开仓库根目录启动 PowerShell 7，
将路径换成自己的目录。手机暂时保持在 Android，`adb devices` 应只列出这一台已授权设备。

```powershell
$PrivateRelease = 'D:\private\release'
$DeviceUefi = 'D:\private\oneplus9-uefi'
$GpuFirmware = 'D:\private\oneplus9-a660-firmware'
$PackageParent = 'D:\private\ramboot'
$Kernel = Join-Path $PrivateRelease kernel
$MetalBundle = Join-Path $PrivateRelease HakiosMetal.bundle
$MetalResourceDonor = Join-Path $PrivateRelease system27-metal-donor.apfs
$BasePackage = Join-Path $PackageParent base
$GpuPackage = Join-Path $PackageParent a660
$FinalPackage = Join-Path $PackageParent final

$Serial = (adb get-serialno | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or !$Serial -or $Serial -eq 'unknown') { throw 'ADB target unavailable' }
$DeviceSerialHash = [Convert]::ToHexString(
  [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Serial))
).ToLowerInvariant()

.\tools\windows\build_ramboot_package.ps1 `
  -Release $PrivateRelease -Kernel $Kernel -Uefi $DeviceUefi `
  -Out $BasePackage -HardwareSerialSha256 $DeviceSerialHash `
  -Defconfig phone_27_2_a660_base

python .\tools\apfs\prepare-a660-ramboot.py `
  --base $BasePackage --firmware $GpuFirmware --metal-bundle $MetalBundle `
  --output $GpuPackage

python .\tools\apfs\prepare-a660-springboard-resources.py `
  --base $GpuPackage --donor $MetalResourceDonor --output $FinalPackage
```

`a660` 和 `final` 两个输出目录应尚未创建，且三个目录位于同一文件系统。
覆盖工具会硬链接未修改的文件。设备序列号及其哈希只用于本机和私有包。

进入最终包目录后校验并核对实际启动参数。

```powershell
Set-Location $FinalPackage
.\Start-iOS.cmd verify
(Get-Content .\manifest.json -Raw | ConvertFrom-Json).boot_arguments
```

确认 `gpu=1 metal=1 metal_procs=SpringBoard touchscreen=1 buttons=1 battery=1`，以及
`wifi=0 bt=0 adsp=0 sound=0 cam=0 haptic=0 sensors=0 thermal=0 ufs=0 ufs_native=0 ufs_write=0 var_store=0`。
配置保留背光、CPU 频率和 Preboot 挂载参数，蜂窝功能保持关闭。
需要软件渲染对照时，可以用 `phone_27_2_perf` 单独组包。

## 4. 启动和验收

确认 Android 飞行模式开启、Wi-Fi 和蓝牙关闭，然后双击最终私有包中的 `Start-iOS.cmd`。
终端也可运行下列命令。

```powershell
.\Start-iOS.cmd start
.\Start-iOS.cmd status
python .\launch.py log
```

启动器先校验包和设备，再临时启动 loader 并逐个上传系统镜像与 kernel/root payload。
按 [状态页](STATUS.md) 记录证据。

- loader 报告 `running`，随后出现 `launchd.darwin`、`backboardd`、SpringBoard 和可见画面。
- A660 sysmem 设备打开，后续日志出现已完成的 GPU 提交或 framebuffer 的非零提交编号。
- S6SY792 报告 `physical touch ready`。点按和拖动时核对坐标与 down/up 事件。
- PMIC 驱动轮询三个按键。逐个短按电源、音量上和音量下，核对按下与释放事件。
- BQ28Z610 给出有效 SOC 和电压。需要检查 SpringBoard 的电源来源时，在启动前给私有包的
  `boot_arguments` 加入 `battery_probe=1`，再查 IOPS 日志。
- 检查内核 panic、SMMU fault 和 SpringBoard/backboardd 异常退出。

结束后返回 Android 并检查启动完成。

```powershell
.\Start-iOS.cmd android
adb shell getprop sys.boot_completed
```

期望 `sys.boot_completed` 为 `1`。如需在真机前检查启动链，见
[Windows QEMU 命令](../tools/windows/README.md#qemu-check)。真机外设单独验收。
