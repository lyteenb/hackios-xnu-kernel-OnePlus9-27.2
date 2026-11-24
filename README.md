<!-- SPDX-License-Identifier: MIT -->
# HakiOS · OnePlus 9 / Darwin 27.2

本仓库是 [HakiOS](https://github.com/oosexgang/hakios-xnu-kernel) 面向一加 9（OnePlus 9
LE2110 / `lemonade` / Qualcomm SM8350）的 Darwin 27.2（iOS 27.2、build `24B119`）源码快照与
公开集成说明。它保存可编译的 XNU/HakiOS 源码、OnePlus 9 设备包、公开构建配置和文档，不保存
Apple 的 IPSW、rootfs、cryptex、系统镜像、厂商固件或设备身份数据。

> **这是开发验证项目，不是可日常使用的 iPhone 替代系统，也不是可以刷写的 ROM。**
> 验证路径只使用 RAM boot；任何 `fastboot flash`、`erase`、`set_active` 都不是本项目的操作。

本项目参考的两个主要上游：

- [oosexgang/hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel)：HakiOS 内核、
  Darwin 用户态、loader、驱动框架和许可证/通知体系的来源。
- [b1ackm4x/hackios-xnu-kernel](https://github.com/b1ackm4x/hackios-xnu-kernel)：此前一加 9 的
  iOS 16.5 集成经验、硬件事实和验证方法；其二进制、固件和发布包不随本仓库分发。

另外，[lyteenb/hackios-xnu-kernel](https://github.com/lyteenb/hackios-xnu-kernel) 是 b1ackm4x 上游在
Windows 下的复现和验证参考，不计入这两个上游。

源码快照对应 `hakios-27.2-lemonade` 分支的提交 `8a61f288`。本仓库只整理公开可提交的内容；
上游仓库仍是这些代码的权威来源。

## 当前状态

截至 2026-11-24，已完成并在本机验证：

| 项目 | 状态 |
| --- | --- |
| QEMU | 使用 24B119 系统镜像启动到真实 Darwin 锁屏 |
| OnePlus 9 真机 | RAM boot 到达 SpringBoard |
| 触控 | S6SY792 物理触控可用：`scan 0x62`、`report 0x10`、1080 × 2400、10 点 |
| CPU 频率 | 两组 CPU 频率分别达到 2419 MHz / 2841 MHz |
| 软件渲染 | 稳定窗口记录到 `301 frames / 5002 ms`（约 60 FPS） |
| GPU 内核侧 | Adreno 660 内核服务启动到 `THE 3D CORE WRITES OUR MEMORY ON COMMAND` |
| A660 Metal | **未完成**；27.2 `HakiosMetal` 仍只接受 A635 `0x06030500`，拒绝 A660 `0x06060001` |

这里区分四种不同的验证结论：**能编译**、**通过主机测试**、**QEMU 能启动**、**真机能启动并可用**。
本仓库不会把前一种结论写成后一种结论。

## 目标平台

| 项目 | 目标 |
| --- | --- |
| 手机 | OnePlus 9 LE2110（`lemonade`） |
| SoC | Qualcomm SM8350 / Snapdragon 888 |
| 面板 | `qcom,mdss_dsi_samsung_amb655x_dsc_cmd`，1080 × 2400 |
| GPU | Adreno 660，chip-id `0x06060001` |
| Darwin 身份 | `iPhone12,1`，iOS 27.2，build `24B119` |
| 启动方式 | `fastboot boot` + loader/USB RAM 传输，仅 RAM |
| 宿主机 | Windows 10/11 + PowerShell 7；Linux 构建放在 GitHub Actions |
| 可选验证 | Windows 原生 QEMU；不要求本机 WSL 或 Linux |

设备必须已经处于可以执行 `fastboot boot` 的状态，并且用户必须自行确认 bootloader、USB 驱动、
ADB 授权和数据备份。这里不包含绕过厂商限制或设备安全的步骤。

## 已验证 / 未完成

### 已验证

- Darwin 27.2 的 dyld shared cache、Apple `launchd`、`backboardd` 和 `SpringBoard` 能达到。
- OnePlus 9 的 display/backlight 路径能输出 SpringBoard 画面。
- S6SY792 触控能初始化并上报物理触点。
- EPSS CPU 频率配置能提高两组 CPU 频率。
- 在 GPU/Metal 关闭、CPU 频率开启时，软件渲染 UI 达到约 60 FPS。
- Adreno 660 内核 GPU 路径能启动；但用户态 Metal 兼容层尚未覆盖 A660。

### 未完成或未验收

- Adreno 660 的 Metal 用户态表、shader 路径和完整 CoreAnimation 加速。
- Wi-Fi、Bluetooth、蜂窝、ADSP、音频、摄像头、传感器、电池、按键和充电状态。
- UFS 持久化写入、可保存设置、睡眠/唤醒、温控和长期稳定性。
- 所有“日常使用”场景，包括通话、联网、相机、应用商店和长时间运行。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| `arch/arm64/boot/` | loader、UEFI 启动支持和设备包加载 |
| `arch/arm64/boot/dts/lemonade/` | OnePlus 9 的 `board.dts`、`soc.dtsi` 和设备说明 |
| `drivers/board/` | 面向 HakiOS/XNU 的设备驱动；`s6sy792.c` 为 OnePlus 9 触控驱动 |
| `kernel/` | Apple XNU 的组装、补丁、覆盖层和构建脚本 |
| `darwin/`, `userland/`, `components/` | Darwin 用户态、镜像工具和兼容组件 |
| `tools/`, `workbench/` | 主机工具和无需手机的测试 |
| `tools/windows/` | Windows 组包、RAM boot 启动器和 QEMU 验证脚本 |
| `Documentation/` | 上游架构、移植、QEMU、驱动和故障排查文档 |
| `docs/` | 本 fork 的平台、状态、限制、复现和致谢说明 |
| `.github/workflows/` | 仅在 GitHub Actions 运行的公开 kernel/loader/Metal 构建 |

公开仓库故意不包含私有系统镜像构建 workflow，也不包含任何 Apple 或 OnePlus 资产。

## 构建

### GitHub Actions（推荐）

1. 在仓库页面打开 **Actions**。
2. 手动运行 `OnePlus 9 Darwin 27.2 public build`，即
   `.github/workflows/lemonade-27.2-ci.yml`。
3. 该 workflow 在 `ubuntu-24.04` runner 上安装 LLVM 和基础工具，执行设备包检查、XNU 构建和
   通用 UEFI loader 构建，只上传内核与 `loader.efi`。
4. Metal 插件可单独运行 `OnePlus 9 Darwin 27.2 Metal plugin`，它只构建并上传
   `HakiosMetal.bundle` 和校验和。

Action 只应产出可公开的源代码构建物；不要把 IPSW、rootfs、cryptex、系统镜像、原机固件、
设备序列号或个人日志上传到公开 artifact。

### 本机 Windows

Windows 用于设备连接、校验、打包和 QEMU 启动验证，不作为 Linux 构建环境。需要准备：

- PowerShell 7；
- `adb` 和 `fastboot`，且位于当前进程可见的工具目录；
- Python 3.11+（仅用于启动/校验脚本）；
- 可选：QEMU、UEFI firmware 和 `qemu-system-aarch64`；
- 用户自行合法持有的对应 IPSW 和已解包的 Apple 系统资产。

本机不要使用 WSL、MSYS 或 Git Bash 作为构建 shell；Linux-only 的 XNU/loader 构建交给 GitHub
Actions。仓库不会修改系统或用户 PATH，工具路径应在单个 PowerShell 会话中设置。

## 复现步骤

完整步骤见 [docs/REPRODUCE.md](docs/REPRODUCE.md)。最小流程如下：

1. 在 GitHub Actions 构建 kernel 和 loader。
2. 在自己的私有环境/私有 workflow 中解包自己合法持有的 `iPhone12,1` 27.2 `24B119` IPSW，
   生成 `root.apfs` 与 `system.apfs`；这些文件不得提交或公开上传。
3. 按 [Windows 组包脚本](tools/windows/README.md) 将 kernel、root/system 镜像、loader 和设备启动配置组合，并用 SHA-256 清单校验。
4. 先在 QEMU 验证锁屏/启动链，再连接 OnePlus 9。
5. 真机只执行：

   ```powershell
   adb devices
   adb reboot bootloader
   fastboot devices
   fastboot boot boot.img
   ```

6. 结束后返回 Android：

   ```powershell
   # 如果发布包包含启动器
   python .\launch.py android
   # 或者手动重启回 Android
   fastboot reboot
   ```

7. 若启动器提供 `verify` / `status` / `log`，先运行 `verify`，再启动；不要跳过哈希校验。

**绝对不要**在 OnePlus 9 上执行 `fastboot flash`、`fastboot erase`、`fastboot set_active` 或写入
任何 Android 分区。RAM boot 失败时优先重启回 Android，而不是刷写分区。

## 已知限制

- A660 Metal 尚未接通；目前是可用的软件渲染 UI，不是完整 GPU 加速。
- 60 FPS 是软件渲染 + CPU 频率配置的结果，不代表所有应用、所有场景都能达到 60 FPS。
- 没有持久化 `/private/var` 和 UFS 写入；重启后设置与会话不保留。
- 触控、显示和 CPU 已验证，不代表 Wi-Fi、音频、相机、传感器、电池或蓝牙同样可用。
- 系统身份、锁屏、激活和应用生态仍受 Apple 用户态、签名、网络和服务限制。
- 当前仅面向一台已适配的 LE2110；其他 OnePlus 9 型号、地区版本和面板/触控变体不保证兼容。
- 真实硬件验证不能由 QEMU 结果替代。

更完整的边界见 [docs/LIMITATIONS.md](docs/LIMITATIONS.md)。

## 安全与法律

- 只使用你有权使用的 IPSW、系统和固件。
- 不提交 Apple 文件、厂商固件、设备树、UEFI 驱动、序列号、ECID、Wi-Fi 密码或私有日志。
- 这是实验性内核，可能造成死机、USB 失联、数据损坏或无法启动；请先备份并保留 Android 回退路径。
- 项目不提供绕过签名、激活或设备安全的承诺；不得把本项目用于未经授权的设备或网络。
- 发现安全问题请按 [SECURITY.md](SECURITY.md) 私下报告，不要在公开 issue 中贴敏感日志或设备身份。

## 许可证

本仓库不是单一许可证仓库，沿用上游 HakiOS 的分层方式：

| 内容 | 许可证 |
| --- | --- |
| 新增工具、测试、设备包格式、公开文档和 `build.config.*` | MIT，见 [LICENSES/preferred/MIT](LICENSES/preferred/MIT) |
| 对 Apple XNU 的覆盖/补丁、HakiOS 衍生代码、内核驱动覆盖层 | APSL-2.0，见 [LICENSES/other/APSL-2.0](LICENSES/other/APSL-2.0) |
| `components/` 等第三方文件 | 保留文件自己的许可证和 SPDX 标识 |
| `fs/apfs/libdeflate/` | MIT |
| `fs/apfs/libzbitmap/` | GPL-2.0+ OR MIT（本项目按 MIT 使用） |

完整来源、第三方通知和本 fork 的改动说明见 [NOTICE.md](NOTICE.md)。根目录 [LICENSE](LICENSE)
只是许可证索引，不是对全部文件的单一重新授权。

## 致谢

感谢 [OOSEX GANG / HakiOS](https://github.com/oosexgang/hakios-xnu-kernel) 构建并公开 XNU、loader、
驱动和 Darwin 用户态框架；感谢 [b1ackm4x/hackios-xnu-kernel](https://github.com/b1ackm4x/hackios-xnu-kernel)
提供一加 9 的 16.5 集成和硬件验证基础。第三方来源、Linux/FreeBSD/Mesa 等参考和许可证细节见
[NOTICE.md](NOTICE.md) 与 [docs/ACKNOWLEDGEMENTS.md](docs/ACKNOWLEDGEMENTS.md)。

## 上游关系

本仓库是面向 OnePlus 9 的整理/集成快照，不是 Apple 官方项目，也不是对上游历史的替代。需要跟进
上游通用修复时，应优先查看 `oosexgang/hackios-xnu-kernel`；需要复核早期 OnePlus 9 的 16.5 结果时，
参考 `b1ackm4x/hackios-xnu-kernel`。`lyteenb/hackios-xnu-kernel` 只是 b1ackm4x 上游在 Windows
下的复现和验证参考，不是本项目的第三个上游。本仓库只保留完成 27.2 RAM boot 验证所需的
OnePlus 9 配置和公开说明。
