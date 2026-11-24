<!-- SPDX-License-Identifier: MIT -->
# OnePlus 9 Darwin 27.2 状态

本页只记录已经公开核实的状态；不包含设备序列号、ECID、私有 run id、私有 artifact 地址或日志文件路径。

## 结论

OnePlus 9 LE2110 已经通过 RAM boot 启动到 Darwin 27.2 的 SpringBoard，显示和物理触控可用，
CPU 频率配置生效。GPU 的内核路径能启动，但 Adreno 660 的 Metal 用户态表尚未移植，因此当前 UI
主要由软件渲染。

这是**开发验证状态**，不是“完整 iOS 27.2 已移植完成”。

## 已验证

| 子系统 | 结果 | 证据/范围 |
| --- | --- | --- |
| QEMU 启动链 | 通过 | 到达 Darwin 27.2 锁屏；ramfb 从 UEFI 文本控制台切换到 Apple 画面 |
| Preboot/APFS | 通过 | stage0 在 handoff 前挂载 Preboot；使用 `apfs_volume=md0,v=1` 和 `apfs_volume_at=/private/preboot` |
| Apple launchd | 通过 | `launchd.darwin` 启动，dyld shared cache 被加载 |
| SpringBoard | 通过 | 真机 RAM boot 到达 SpringBoard；`SpringBoard` 进程出现 |
| 面板/显示 | 通过 | 1080 × 2400 SpringBoard 画面持续刷新 |
| 背光 | 通过 | `backlight=1` 的验证配置可用 |
| 触控 | 通过 | S6SY792：identity 读取成功、`scan 0x62`、`report 0x10`、10 点、1080 × 2400 |
| CPU 频率 | 通过 | 两组 CPU 频率达到 2419 MHz / 2841 MHz（`cpufreq=1`） |
| 软件渲染 | 通过 | 稳定窗口 `301 frames / 5002 ms`，约 60 FPS |
| GPU 内核侧 | 部分通过 | Adreno 660 内核服务启动到 `THE 3D CORE WRITES OUR MEMORY ON COMMAND` |

## 验证配置

目前的可用软件渲染验证配置是：

```text
cpus=4
cpufreq=1
gpu=0
metal=0
wifi=0
adsp=0
sound=0
cam=0
haptic=0
sensors=0
backlight=1
touchscreen=1
buttons=0
battery=0
ufs=0
ufs_native=0
ufs_write=0
var_store=0
launchd_unsecure_cache=1
apfs_volume=md0,v=1
apfs_volume_at=/private/preboot
```

这不是日常配置。它刻意关闭未验证的无线、音频、摄像头、传感器、电池和持久化写入路径。

## 未完成

| 项目 | 当前状态 |
| --- | --- |
| A660 Metal | `HakiosMetal` 27.2 表只识别 A635 `0x06030500`，A660 `0x06060001` 被拒绝 |
| CoreAnimation 加速 | 仍走软件路径，未完成 A660 Metal 全链路 |
| Wi-Fi | 未接通 QCA6490 / 27.2 用户态 |
| Bluetooth | 未接通 |
| 蜂窝 | 未接通；不作为可日常通信设备 |
| ADSP / 音频 | 未接通 |
| 摄像头 | 未接通 |
| 电池/充电状态 | 未验收 |
| 按键/传感器 | 未验收 |
| UFS 持久化 | 关闭；没有可靠的掉电保存 |
| 睡眠/唤醒 | 未验证 |
| 温控/长期稳定性 | 未验证 |
| 其他 OnePlus 9 变体 | 未验证 |

## 能证明什么，不能证明什么

可以证明：

- 这条源码快照能构建对应的 kernel/loader；
- 该 RAM 启动链在 QEMU 和 LE2110 上各到达过预期用户态阶段；
- 当前软件渲染 profile 能让 SpringBoard 显示和触控达到可观察的约 60 FPS。

不能证明：

- A660 GPU/Metal 已正常工作；
- Wi-Fi、蓝牙、音频、相机、电池、UFS 或蜂窝可用；
- 系统可稳定运行任意时长；
- 所有 iOS 应用、激活、商店、网络服务或签名路径都可用；
- QEMU 结果可直接替代真机结果；
- 这个项目可以安全地用于日常主力机。
