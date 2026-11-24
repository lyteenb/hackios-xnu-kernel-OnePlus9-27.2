<!-- SPDX-License-Identifier: MIT -->
# 项目说明

## 这是什么

`hackios-xnu-kernel-OnePlus9-27.2` 是 HakiOS 的 OnePlus 9 Darwin 27.2 集成快照。它的目标不是
“运行一个同名版本号”，而是把以下部分组成一条可以验证的启动链：

1. Apple XNU 的公开源码组装与 HakiOS 覆盖层；
2. HakiOS loader、UEFI 启动支持和 USB RAM 传输；
3. OnePlus 9 的 board/SoC 数据、显示、触控、CPU 频率和 GPU 内核侧配置；
4. Darwin 27.2 用户态镜像构建工具；
5. QEMU 验证和 OnePlus 9 真机 RAM boot 验证。

仓库只保存可公开的源码、配置和文档。Apple IPSW、系统镜像、cryptex、dyld shared cache、设备树、
厂商固件、UEFI 二进制、设备身份和个人日志必须由使用者在本地或私有构建环境中准备。

## 与两个上游的关系

### `oosexgang/hackios-xnu-kernel`

这是主要的源码上游。它提供：

- Apple XNU 的组装/补丁方式；
- HakiOS loader 与通用驱动框架；
- Darwin 26/27 用户态构建工具；
- 设备包格式、测试、文档和 MIT/APSL-2.0 分层许可证协议；
- Renoir 等参考设备的 bring-up 记录。

本仓库的源码快照来自 `hakios-27.2-lemonade` 分支提交 `8a61f288`。上游仍是通用代码的权威来源；
本仓库只负责保存 OnePlus 9 的集成结果和公开复现说明。

### `b1ackm4x/hackios-xnu-kernel`

这是此前一加 9 的 iOS 16.5 集成树，提供 OnePlus 9 的硬件事实和验证经验，包括面板、触控、
GPU、USB、loader 和发布包流程。它帮助确认了：

- `qcom,mdss_dsi_samsung_amb655x_dsc_cmd` / 1080 × 2400 面板；
- S6SY792 的 I2C 地址和 GPIO 接线；
- Adreno 660 的 chip-id、GMEM 和频率数据；
- OnePlus 9 的 fastboot/loader 操作路径；
- 只 RAM boot、不写 Android 分区的测试边界。

16.5 的驱动和二进制不能直接证明可在 XNU 12377 / Darwin 27.2 下工作；27.2 的每个结论都在本仓库
或验证日志中重新给出。

### Windows 复现（非上游）

`lyteenb/hackios-xnu-kernel` 是 `b1ackm4x/hackios-xnu-kernel` 在 Windows 下的复现和验证参考。它不是第三个
源码上游；本仓库仅在 Windows 环境、USB/fastboot 操作和验证流程上参考它。它的固件、系统镜像和发布包
不随本仓库分发。

## 仓库内容

| 内容 | 是否提交 | 说明 |
| --- | --- | --- |
| 内核/loader 源码 | 是 | 公开源码快照，许可证按文件区分 |
| OnePlus 9 board/SoC 配置 | 是 | 只包含从设备树读取的事实和必要参数 |
| 公开 workflow | **否** | 本仓库不包含 GitHub Actions；构建在本地或外部环境手动完成 |
| Apple IPSW / rootfs / cryptex | **否** | 由使用者自行取得并保存在私有环境 |
| vendor firmware / UEFI binary | **否** | 不提交、不公开分发 |
| 设备序列号 / ECID / 设备身份 | **否** | 发布包也不能包含可反推身份的数据 |
| 真机日志 | **否** | 只提交脱敏后的结论 |
| 私有镜像构建流程 | **否** | 防止误把 Apple 资产上传到公开仓库 |

## 公开与私有的边界

公开仓库可以保存：

- 源码和补丁；
- 设备包中的地址、总线、GPIO、面板时序等事实数据；
- 构建脚本和本地检查命令；
- 脱敏后的状态、限制、复现方法和校验方法。

公开仓库不得保存：

- 从 IPSW 解包出的任何文件；
- `root.apfs`、`system.apfs`、`boot.img` 等包含 Apple 文件或厂商二进制的发布物；
- `DeviceTree.bin`、`identity.bin`、UEFI 驱动和厂商 firmware；
- 真机完整日志、序列号、ECID、MAC、Wi-Fi 名称或密码；
- “私有 release”下载地址、run id、传输分支或 token。

## 版本与分支

- 当前源码快照：`hakios-27.2-lemonade` 的 `8a61f288`。
- 本仓库的 `main` 用于公开整理，不声称与上游历史线性对应。
- 上游通用修复应先在 `oosexgang/hackios-xnu-kernel` 复核，再决定是否 cherry-pick。
- OnePlus 9 的 16.5 硬件事实和早期集成应与 `b1ackm4x/hackios-xnu-kernel` 对照。
- `lyteenb/hackios-xnu-kernel` 是 Windows 复现，不是上游来源。
- 任何包含 Apple 资产的构建流程应留在私有环境或私有流程中，不进入本仓库。
