<!-- SPDX-License-Identifier: MIT -->
# 已知限制

## 1. A660 Metal 未完成

这是当前最大的功能缺口：

- 27.2 `HakiosMetal` 的设备表只覆盖 A635 `0x06030500`；
- OnePlus 9 的 Adreno 660 是 `0x06060001`；
- 插件会拒绝 A660，CoreAnimation 回退到软件渲染；
- 内核 GPU 服务启动成功不等于 Metal 用户态已完成。

因此：

- 不能把当前状态描述为“GPU 完整加速”；
- 不能把 60 FPS 软件渲染描述为所有界面都能达到；
- A660 需要新的设备表、寄存器/状态验证、shader/资源路径和稳定性测试。

## 2. 性能与热

- 已验证的 60 FPS 来自软件渲染 + `cpufreq=1` + 4 核配置；
- 不同场景、不同温度和不同后台负载可能明显更低；
- 没有完成长时间温控、降频、休眠/唤醒和电池电流验证；
- 高负载时可能过热、掉帧、死机或 USB 失联。

## 3. RAM-only

- 系统镜像和用户态从 RAM 启动；
- UFS 写入、持久化 `/private/var` 和正常关机保存均未验收；
- 重启后设置、会话、安装内容和临时数据不会可靠保留；
- 不能把当前状态当作可长期使用的安装。

## 4. 外设缺口

以下设备路径未完成或未验收：

- QCA6490 Wi-Fi；
- Bluetooth；
- 蜂窝 modem / IPA / QMI；
- ADSP / 音频；
- 摄像头；
- 电池、充电状态和电源管理；
- 按键、传感器、触觉；
- UFS 持久化和分区策略。

任何“能显示 SpringBoard”的结果都不能推导为这些设备可用。

## 5. 设备范围

当前只针对：

- OnePlus 9 LE2110；
- SM8350；
- Samsung `amb655x` 1080 × 2400 面板；
- S6SY792 触控；
- Adreno 660 `0x06060001`。

其他型号、面板批次、触控 IC、固件版本、地区 SKU 或 bootloader 版本没有保证。不同设备可能需要
重新采集设备树和固件，并重新验证 GPIO、I2C、电源和时序。

## 6. 安全边界

HakiOS 的 stand-in 组件会为了启动 Darwin 放宽 Apple 的安全策略。当前状态至少意味着：

- keychain 不具备 Apple 安全隔区的保护；
- 签名、启动链度量和端到端安全模型不等同于 iPhone；
- 不应在运行此系统时保存密码、令牌、私钥或其他敏感数据；
- 不应用它连接不可信网络或作为生产设备。

具体风险以 [../SECURITY.md](../SECURITY.md) 和 [../NOTICE.md](../NOTICE.md) 为准。

## 7. 复现边界

- 公开仓库不包含 Apple IPSW、rootfs、cryptex、system image、厂商固件、UEFI binary 和设备身份；
- 本仓库不包含 Actions workflow；公开内容只提供源码和手动构建说明，不提供可分发的 Apple 用户态镜像；
- 私有镜像构建、设备绑定和发布包校验必须在私有环境完成；
- 没有设备身份或合法 IPSW 的使用者无法仅凭本仓库得到完整启动包；
- 真实硬件验证需要设备、USB 连接、已解锁/可启动的 bootloader 和可回退的 Android 环境。

## 8. 不应执行的命令

以下命令会改变设备状态或写入分区，不属于本项目的验证流程：

```text
fastboot flash ...
fastboot erase ...
fastboot set_active ...
```

当前验证只允许：

```text
fastboot boot boot.img
```

设备异常时优先重启回 Android，不要用刷写分区来“修复”实验性启动。
