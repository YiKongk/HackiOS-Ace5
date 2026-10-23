# HackiOS on OnePlus Ace 5

一加 Ace 5 **PKG110 / giulia / Snapdragon 8 Gen 3（SM8650）** 的 HackiOS 研究性移植。
通过临时 UEFI / XNU loader 在 RAM 中启动 iOS 16.5，包含 S3910 触摸适配、Adreno 750 桌面合成、实验性 Metal 支持、原生壁纸修复和单核桌面优化。

**本次公开的是 Wi‑Fi 适配前、经过实机验证的源码基线。Wi‑Fi 尚不可用；这不是可日用 ROM，也没有完整 Metal 兼容或稳定 60 FPS 的承诺。**

本仓库将内核、loader 和用户空间源码放在同一个工作目录，克隆一次即可获得项目自己的源码。Apple 系统文件、SDK、设备固件、DTB、分区备份和预编译镜像均需在本地准备，不随仓库分发。

## 文档入口

| 文档 | 内容 |
| --- | --- |
| [构建与启动教程](docs/BUILD.zh-CN.md) | 依赖、XNU、Mesa/Metal、系统镜像、RAM 启动 |
| [UEFI 项目与完整教程](docs/UEFI.zh-CN.md) | Mu-Silicium 链接、固定版本、giulia 编译与打包、诊断/内核模式 |
| [功能与验证记录](docs/STATUS.zh-CN.md) | 真机结果、测试边界、帧率数据与已知问题 |
| [操作与恢复](docs/RECOVERY.zh-CN.md) | 备份、日志、退出 iOS、USB 丢失、CrashDump |
| [架构与开发](docs/DEVELOPMENT.zh-CN.md) | 源码分层、测试入口、提交规范 |
| [发布范围与清理记录](docs/RELEASE.zh-CN.md) | 基线版本、排除内容、发布检查 |
| [历史适配笔记](docs/ACE5.md) | 早期试验、寄存器和诊断过程；历史阶段不代表当前功能 |
| [署名和许可证](NOTICE.md) | HackiOS、XNU、Mu-Silicium、Mesa 等来源 |

## 本版本实际支持什么

| 功能 | Ace 5 状态 |
| --- | --- |
| 临时 UEFI / XNU 启动、iOS 锁屏和桌面 | 已实机验证，RAM 启动 |
| Synaptics S3910 触摸 | 已验证点击与滑动；用户确认位置准确；长期/多指覆盖仍有限 |
| Adreno 750v2 桌面渲染 | 已验证 OpenGL ES 合成、GPU 缩放/清除/写入面板缓冲 |
| Metal | 已验证部分 compute、绘制、深度格式、CAMetalLayer；不等于完整 API 支持 |
| iOS 16 默认壁纸 | 已验证，保留 CAML 动画，HEIC 图层在本地转 PNG |
| 桌面滑动优化 | 已验证减少单核线程等待；仍有顿挫 |
| CPU | XNU 使用 1 个核心，CPU 0 留给 USB 观察；本版本未验证 SMP |
| Wi‑Fi / 蓝牙 / 蜂窝 / 音频 / 相机 / 正常充电管理 | 未完成 Ace 5 适配 |
| App Store、Apple ID、受保护内容 | 不作为可用功能承诺 |
| 持久化安装、日常数据保留 | 本版本采用 RAM 会话，不提供 Ace 5 刷机安装流程 |

上游 POCO X5 Pro（redwood）的 Wi‑Fi、音频、相机或 8 核测试结果**不能**直接当成 Ace 5 的支持情况。不要把上游 `gpu=1 wifi=1 adsp=1` 启动串套用到本设备。

## 性能与限制

在固定工作负载中，单核桌面优化将面板缓冲更新频率从约 **51.29 次/秒**提升至 **53.35–54.30 次/秒**，超过 25 ms 的间隔比例由 **15.10%** 降到 **9.12–10.61%**。
这是缓冲完成时间统计，不是光学测得的面板刷新率；设置页面同期约有 1–2 次/秒的回退。

一次 10 分钟验证记录了 **4,440 次 GPU 送屏、0 次 CPU 回退、600 帧 Metal 应用绘制零读回错误**。送屏驱动阶段平均约 1.78 ms，不代表整帧只需 1.78 ms。GPU 仍固定在 231 MHz，原生 DPU page flip、DVFS、完整 GPU 隔离/恢复及更广应用兼容仍未完成。

曾出现桌面、Wi‑Fi 页面和飞行模式操作后卡住的报告。补齐 sleepd/configd 解决了一个可重复的系统服务阻塞原因，不能据此认为所有死机都已解决。

## 开始之前

适用机型是已解锁、可运行 root Android 的 **PKG110**；其他 SKU、OnePlus 13R 等相近设备未验证。保留可回到 Android 的启动链和自己的分区备份。

```sh
git clone https://github.com/YiKongk/HackiOS-Ace5.git
cd HackiOS-Ace5
```

接着阅读 [构建教程](docs/BUILD.zh-CN.md) 和 [UEFI 教程](docs/UEFI.zh-CN.md)。源码树中的 `common/setup.sh` 只检查工具，不会再拉取第二个用户空间仓库。本发布没有 `prebuilt/`，需要构建工具链。

这套流程使用 `fastboot boot` 临时启动，默认禁用普通分区写入；loader 仍会使用 **logdump** 保存诊断。它不是“完全没有存储写入”。本次 Ace 5 验证没有刷写系统分区、重新分区或清空用户数据。

## 基线与上游

- 内核和 Ace 5 工具：`86e5296f2e94f0ee6ec2ef00d0950572a49b7095`，桌面线程优化完成后的基线。
- 用户空间：`b10b761ba73bea408841de3ae4d2550d97a0f1e3`。
- XNU：`xnu-8796.121.2`，Darwin 22.5.0 / iOS 16.5。
- UEFI：[Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)，固定 `42fe563791e51bf5500c2e470ed294ca0c94b97c`。
- 起点：[OOSEX GANG HackiOS kernel](https://github.com/oosexgang/hackios-xnu-kernel) 与 [userland](https://github.com/oosexgang/hackios-xnu-userland)。Ace 5 适配不代表上游官方支持。

本仓库是干净的公开源码快照，原研发提交哈希记录于 [RELEASE.json](RELEASE.json)。未公开原私有仓库历史，也未包含后续 WCN7850 Wi‑Fi 诊断、GPU 时间戳、双核或命令环缓存试验。

## 贡献与许可

请先提供机型、Android 固件版本、公开源码提交、启动参数、能否回到 Android 和脱敏后的错误片段。不要把系统镜像、固件、设备序列号、账号或网络密码上传到 issue。

开发默认 **只 commit，不自动 push**；维护者明确要求发布时再同步远端。

XNU 衍生部分保留 [APSL 2.0](LICENSE)，用户空间保留 [MIT](LICENSES/userland-MIT.txt)，其他组件以原文件头和 [NOTICE.md](NOTICE.md) 为准。本项目与 Apple、OnePlus 或 Qualcomm 无官方关联。
