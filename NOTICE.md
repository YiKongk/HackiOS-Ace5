# 来源、署名与许可证

此仓库是 OnePlus Ace 5 的研究性适配快照，不是 Apple、OnePlus、Qualcomm 或以下上游的官方发行版。保留文件原有版权与许可证；根目录 LICENSE 不会把第三方文件重新许可为单一协议。

## 来源与范围

| 来源 | 本仓库中的相关内容 | 许可 / 说明 |
| --- | --- | --- |
| [oosexgang/hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel) | XNU 补丁、loader、内核构建和工具；加上 Ace 5 修改 | 上游根许可 [APSL 2.0](LICENSE)，各文件头优先 |
| [oosexgang/hackios-xnu-userland](https://github.com/oosexgang/hackios-xnu-userland) | `userland/`、`ios165/`、APFS/AIR/Metal 等用户空间工具和文档 | 保留 [MIT 原文](LICENSES/userland-MIT.txt)，文件内其他声明保留 |
| [Apple XNU](https://github.com/apple-oss-distributions/xnu/tree/xnu-8796.121.2) | 构建时取得的 `xnu-8796.121.2`，本仓库提供补丁/新增文件 | APSL 2.0 及各文件原有条款 |
| [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) | `uefi/` 平台修改和 XnuLoader 钩子 | [Mu 顶层 BSD-2-Clause](LICENSES/Mu-Silicium-BSD-2-Clause.txt)；XnuLoader.c 等标明 [BSD-2-Clause-Patent](LICENSES/BSD-2-Clause-Patent.txt)，其他目录按其声明 |
| [Mesa 25.0.7](https://archive.mesa3d.org/mesa-25.0.7.tar.xz) | `tools/mesa/` 补丁、Freedreno 接口和 AIR 支持；Mesa 本体构建时下载 | Mesa 各文件原许可；AIR 源码保留 Darwin on redwood contributors 的 MIT 标记 |
| [UniXNU/NativeAdreno](https://github.com/UniXNU/NativeAdreno/tree/b686b9054f57d393c80195b82568a09f8ca106a2) | CP 队列初始化、缓存/fence 顺序参考，见 `giulia_gpu_direct.h`、`giulia_gpu_submit.h` | [NativeAdreno MIT](LICENSES/NativeAdreno-MIT.txt)；A740 参考并非 A750 的验证结果 |
| [OnePlus SM8650 graphics-kernel](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8650/tree/453d8875f815bf9dfad9035982861226a4823a6c/vendor/qcom/opensource/graphics-kernel) | `giulia_gpu_regs.h` 的寄存器/表及协议参考 | 文件保留 Qualcomm / Linux Foundation 版权与 GPL-2.0-only 标记；[协议原文](LICENSES/GPL-2.0-only.txt) |
| libdeflate | `xnu/files/bsd/miscfs/apfs/libdeflate/` | 随目录保留 [COPYING](xnu/files/bsd/miscfs/apfs/libdeflate/COPYING) |
| [linux-apfs/apfsprogs](https://github.com/linux-apfs/apfsprogs/tree/1bd1c1c2140d13d1b6d2e03379316572bee70aa3) | APFS 布局参考与构建时取得的检查器 | GPL-2.0；检查器源码由其自身仓库提供 |

更多来源和固定版本见 [历史适配笔记](docs/ACE5.md)末尾，包括 Linux MSM 驱动、UniXNU-sheng、AdrenoFirmware 和 ArmSmmu4K。Theos/cctools/SDK 是本地构建依赖，不作为本仓库资产上传。

## 发布修改与历史

公开内容基于内核 `86e5296f2e94f0ee6ec2ef00d0950572a49b7095` 和用户空间 `b10b761ba73bea408841de3ae4d2550d97a0f1e3`。这些是原研发仓库的标识，公开仓库使用新的快照历史，并不要求读者能访问原私有提交。

2026-10-24 的发布整理加入中文文档和单仓库 setup/ugit；排除了预编译文件、固件、设备照片和后续试验。`redwood_goodix.c` 增加缺少 vendor FocalTech 固件时拒绝该旧路径的检查；Ace 5 使用独立 S3910 驱动。详见[发布记录](docs/RELEASE.zh-CN.md)。

本仓库不分发 Apple 系统文件、SDK、厂商固件、DTB、分区镜像、密钥或设备备份。混合来源源码的各自许可继续有效；本次只发布源码，不提供一个统一重新许可的二进制系统。
