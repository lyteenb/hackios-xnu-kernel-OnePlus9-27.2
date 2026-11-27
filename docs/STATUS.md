<!-- SPDX-License-Identifier: MIT -->
# OnePlus 9 Darwin 27.2 状态

## 联合 RAM 启动

2026-11-27，在 OnePlus 9 LE2110 上以 USB RAM boot 启动 iPhone12,1 的 24B119。
本次配置同时开启 GPU、Metal、触控、按键和电池，保留显示、背光、CPU 频率和 Preboot 挂载路径。
Wi-Fi、蓝牙、ADSP、音频、摄像头、触觉、传感器、温控及 UFS 持久化写入均沿用关闭配置。

| 项目 | 实机观察 |
| --- | --- |
| 启动链 | loader 报告运行，Darwin launchd、backboardd 和 SpringBoard 启动，并取得 1080 × 2400 framebuffer。 |
| Adreno 660 | sysmem Metal 设备打开。显示日志记录了多次非零 GPU 提交，首轮联合验证持续到提交 2150。 |
| 触控 | S6SY792 完成初始化，报告 1080 × 2400、10 触点。收到了 down、move 和 up。屏幕边缘、多点和手势行为待逐项验收。 |
| 按键 | PMIC 驱动以 16 ms 间隔轮询。电源键、音量上和音量下均记录到成对的物理按下和释放事件，锁屏和调音量等界面行为待单独验收。 |
| 电量 | BQ28Z610 能提供有效 SOC、电压和状态。需要带对应采样路径的 loader，启用电量诊断后，SpringBoard 的 IOPS 返回一个电源来源和有效百分比。 |
| 回退 | 通过 USB 返回 Android，ADB 的 `sys.boot_completed` 为 `1`，飞行模式、Wi-Fi 与蓝牙仍为离线状态。 |

在较早的一次尝试中，GPU 和输入驱动启动后，旧 loader 的电池缓存没有有效样本。
采用包含 BQ28Z610 支持的 loader 后，联合启动读到有效数据。
该轮采样未见 GPU 故障或 SpringBoard/backboardd 异常退出。随后启用 SpringBoard 电量诊断的复测
出现一次 SpringBoard SIGABRT，进程重启后读取到电量，自动输入与 GPU 提交也继续出现。
真实充电、电量变化和设备休眠仍需在专门测试中验证。

## 其他已验证路径

- QEMU 已到达 Darwin 27.2 锁屏。真机软件渲染对照在四核及 EPSS 配置下曾记录约 60 FPS 的窗口样本。
- 实验性 A660 路径曾通过缓冲区复制、计算、清屏和 sysmem 三角形探针。调整每个 GPU 核心的
  shader 私有内存分配后，短时渲染观察未出现此前的 SMMU translation fault。
- 持续触控压力测试出现过 SpringBoard 和 backboardd 的 SIGBUS，配对软件渲染对照完成了同一输入序列。
  GMEM、完整 CoreAnimation 覆盖、长时间互动及其稳定性仍在验证。

[复现步骤](REPRODUCE.md)列出了配置、私有输入、组包命令与验收条件。[已知限制](LIMITATIONS.md)说明剩余硬件和使用范围。
