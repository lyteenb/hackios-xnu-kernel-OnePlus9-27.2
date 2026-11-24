<!-- SPDX-License-Identifier: MIT -->
# 致谢与来源

本项目的代码和知识来自多个上游。这里的“致谢”不是对第三方代码的重新授权；每个文件的许可证和
SPDX 标识仍然有效。

## 主要上游

### OOSEX GANG / HakiOS

- 仓库：<https://github.com/oosexgang/hackios-xnu-kernel>
- 贡献：XNU 组装与补丁框架、HakiOS loader、设备包格式、驱动框架、Darwin 26/27 用户态工具、
  QEMU 工作台、文档和许可证/通知体系。
- 本仓库的通用代码、目录结构和大量文档直接来自该上游。

### b1ackm4x / 一加 9 早期集成

- 仓库：<https://github.com/b1ackm4x/hackios-xnu-kernel>
- 贡献：OnePlus 9 的 16.5 实机经验、面板/触控/GPU 硬件事实、USB loader 操作和 RAM 启动验证方法。
- 本仓库的 27.2 工作重新验证了这些事实；16.5 的固件、系统镜像和发布包没有复制到本仓库。

### Windows 复现（非上游）

- 仓库：<https://github.com/lyteenb/hackios-xnu-kernel>
- 这是 `b1ackm4x/hackios-xnu-kernel` 在 Windows 下的复现和验证参考，不属于两个源码上游。
- 本仓库只把它作为 Windows 环境和验证路径的参考；它的固件、系统镜像和发布包同样不随本仓库分发。

## 其他来源

上游 `NOTICE.md` 记录了更多参考和许可证，包括：

- `frxncyyy/ios-denniz` 的 OnePlus Nord 2 移植经验；
- Linux `msm/adreno`、Qcom clock/PHY、`btqca` 等驱动所记录的寄存器和初始化顺序；
- FreeBSD LinuxKPI 头文件；
- `libdeflate`（Eric Biggers / Google LLC，MIT）；
- `libzbitmap`（Corellium LLC，GPL-2.0+ OR MIT，本项目按 MIT 使用）；
- HakiOS 的 `components/`、Mesa/AIR 相关组件及其各自的许可证和 SPDX 标识。

请以 [../NOTICE.md](../NOTICE.md)、`components/upstream.dts` 和源文件头部的 SPDX 标识为准。这里的
简短致谢不能替代完整通知。

## OnePlus 9 验证贡献

本 fork 的公开整理包括：

- `arch/arm64/boot/dts/lemonade/` board/SoC 数据；
- `drivers/board/s6sy792.c` 触控驱动；
- `phone_27_2_perf_defconfig` 和公开 CI；
- 27.2 Preboot/APFS 启动链适配；
- Adreno 660 内核侧启动、A660 Metal 限制记录；
- Windows + GitHub Actions 的无 WSL 复现说明；
- 本 `docs/` 下的平台、状态、限制、复现和致谢文档。

这些新增文件按各自 SPDX 标识授权；对 Apple XNU/HakiOS 代码的修改仍按其原许可证处理。
