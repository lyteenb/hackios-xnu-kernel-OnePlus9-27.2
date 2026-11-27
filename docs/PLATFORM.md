<!-- SPDX-License-Identifier: MIT -->
# 平台与构建环境

## 目标硬件

| 项目 | 值 |
| --- | --- |
| 设备 | OnePlus 9 LE2110（代号 `lemonade`） |
| SoC | Qualcomm SM8350 / Snapdragon 888 |
| CPU | 8 核，已验证配置只启用 4 核 |
| GPU | Adreno 660，chip-id `0x06060001` |
| 面板 | Samsung `amb655x`，1080 × 2400 |
| 触控 | Samsung S6SY792，I2C 地址 `0x48` |
| 实体键 | PMK8350 的电源键和 RESIN 音量上，PM8350 GPIO6 音量下 |
| 电池 | BQ28Z610，UEFI loader 采样并向 Darwin 共享 |
| UEFI payload | `SILICIUM_UEFI.fd`，base `0x9FD00000`，size `0x200000`，boot header v3 |
| Darwin 身份 | `iPhone12,1`，Darwin 27.2 / iOS 27.2，build `24B119` |

当前结论只针对 LE2110 和该面板/触控/GPU 组合。其他 OnePlus 9 型号、地区版本、面板批次、
触控控制器或固件版本可能存在未记录差异。

## Windows 宿主机

建议环境：

| 组件 | 用途 | 备注 |
| --- | --- | --- |
| Windows 10/11 x64 | 设备连接、打包、QEMU 启动 | 不要求 Linux/WSL |
| PowerShell 7+ | 所有本地命令 | 不依赖 `.cmd` 或 Bash |
| Python 3.11+ | 启动器、校验、镜像工具 | 只在需要时使用 |
| `adb` | Android 侧连接、重启到 bootloader | 需要设备授权 |
| `fastboot` | 启动 bootloader 和 RAM loader | 仅使用 `fastboot boot` |
| QEMU（可选） | 在真机前验证启动链 | `qemu-system-aarch64` |
| 仓库脚本 | 组包、启动和 QEMU | [tools/windows/README.md](../tools/windows/README.md) |
| UEFI firmware（可选） | QEMU 的 loader 启动 | 用户自行准备，不入库 |

本仓库不修改系统或用户 `PATH`。如果工具没有安装在系统目录，可以在当前 PowerShell 会话中临时设置：

```powershell
$env:Path = "C:\path\to\platform-tools;C:\path\to\qemu;$env:Path"
```

这个设置只对当前会话有效。不要把仓库路径、用户目录或私钥写入提交。

## 构建

本仓库不包含 GitHub Actions workflow，也不要求使用 GitHub Actions。公开部分按当前环境的工具链手动构建：

```sh
python3 tools/boards.py check
BUILD_CONFIG=build.config.arm64.llvm kernel/build.sh -j4
arch/arm64/boot/build.sh
```

Metal 插件按 `components/metal/plugin/` 的构建脚本手动运行。Windows 只负责设备连接、校验、打包和 QEMU；Linux-only 的 XNU/loader 构建放到你的本地或外部构建环境。

以下输入需要自行准备：

- 合法持有的 iOS 27.2 `24B119` IPSW；
- 从 IPSW 生成 Darwin root/system/Preboot 镜像的私有流程；
- OnePlus 9 的 UEFI、固件和设备身份数据；
- 真机连接、ADB 授权和 fastboot 操作。

私有 Apple 资产不应通过公开 artifact 传递。无论使用本地环境还是外部构建环境，都要在发布前执行脱敏和 `SHA-256` 校验。
## QEMU

QEMU 验证需要：

- `qemu-system-aarch64`；
- AArch64 UEFI firmware（如 QEMU 的 `QEMU_EFI.fd`）；
- 一个已组装的 kernel；
- 一个包含 `root.apfs` / `system.apfs` 的测试包；
- 与目标 Darwin 版本匹配的启动参数。

QEMU 只能证明“启动链和用户态在该虚拟环境中可达”，不能证明 OnePlus 9 的 panel、touch、GPU、
UFS、Wi-Fi、电源或 USB 路径可用。

## 真机前检查

```powershell
adb devices
fastboot devices
```

确认（请按自己的手机准备 UEFI/DTB/firmware/serial hash）：

1. `adb devices` 中目标手机为 `device`，不是 `unauthorized`；
2. 手机能进入 bootloader，`fastboot devices` 能看到设备；
3. Android 数据已备份；
4. 已准备返回 Android 的方法；
5. 发布包的 SHA-256 全部通过；
6. 当前操作只使用 `fastboot boot`，不包含任何写分区命令。

## 不支持的环境

- 本机 WSL、MSYS、Git Bash 或 Linux 构建链；
- 在 Windows 上直接运行上游 POSIX `bench` 脚本；
- 任何 `fastboot flash`、`fastboot erase`、`fastboot set_active`；
- 在公开仓库/公开 artifact 中传递 Apple 镜像、厂商固件或设备身份；
- 把 QEMU 或软件渲染的成功描述成完整 GPU/日常可用。
