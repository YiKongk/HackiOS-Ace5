# 2026-10-24：Wi‑Fi 适配前源码公开版

## 版本选择

这是新的源码快照仓库，不是把原私有开发仓库及其全部历史改成公开。它合并项目内核、loader、设备工具和用户空间的源码，保留来源与许可证。

| 组成 | 原研发基线 |
| --- | --- |
| 内核 / Ace 5 / loader | `86e5296f2e94f0ee6ec2ef00d0950572a49b7095` |
| 用户空间 / Mesa 接口 / Metal | `b10b761ba73bea408841de3ae4d2550d97a0f1e3` |
| Apple XNU | `c54f35ca767986246321eb901baf8f5ff7923f6a`，tag `xnu-8796.121.2` |
| Mu-Silicium | `42fe563791e51bf5500c2e470ed294ca0c94b97c` |

选择的是触摸、A750/Metal、GPU 送屏、默认壁纸和单核桌面优化完成后的已验证基线。后续 WCN7850 / PCIe Wi‑Fi 诊断、GPU 时间戳、双核和命令环缓存试验未纳入。

源码里仍有上游 redwood 的 Wi‑Fi 代码和系统服务补齐，这不代表 Ace 5 Wi‑Fi 可用。修复 Wi‑Fi 设置页某个服务等待问题，也不等同于适配无线硬件。

## 清理范围

从两套研发基线合并前的文件清单中，排除 **364 个文件、31,014,583 字节**，主要是：

- 预编译内核、用户程序、动态库与镜像产物。
- 其他机型的 UEFI vendor 二进制和 DTB。
- vendor FocalTech 固件字节数组，以及旧机型文档截图。
- 原有双仓库 README/.gitignore，替换为单仓库的入口和规则。

研发环境中的 Apple 文件、SDK、GPU 固件、分区备份、原始设备日志、临时试验和构建缓存不进入新仓库。恢复备份仍在本地保留。以上是公开文件集的清理，不会删除原私有开发历史。

## 发布整理产生的代码差异

- `common/setup.sh` 改为工具检查，`common/ugit` 转发至同一个 Git 仓库，避免重新拉取另一套用户空间。
- `common/devices/giulia.conf` 和通用构建提示指向 Ace 5 专用教程，保留阻止通用 flash 流程的限制。
- 删除与 Ace 5 无关的 `redwood_focaltech_fw.h`；`redwood_goodix.c` 在本地没有该固件时提前拒绝旧 FocalTech 探测。Ace 5 S3910 路径不依赖它。
- 加入日常手动使用的 `rc.jobs.daily`、中文教程、许可证来源说明及源码检查脚本。

这些整理没有引入后续硬件试验。发布后如需改进，默认仅本地 commit，明确要求发布时才 push。

## 本次发布检查

在公开工作树上完成：

- 固定 Apple XNU 重新应用补丁，使用 LLVM 18 / Linux ARM64 完整构建 `build` 目标成功；包括上述去除 FocalTech 固件后的源文件。
- diagnostic 和 kernel 两种 Ace 5 loader 从源码编译成功。
- 固定 Mu 干净检出上的钩子应用与重复运行检查；使用已有同版本 UEFI FD、自己的 DTB 和新 loader 打包检查成功。
- EAGL、Metal 组件从公开源码编译检查；使用原有同基线 Mesa 构建缓存和本地 Theos SDK。
- 8 组触摸、GMU、HFI、CP、live queue、提交、ZAP 解析/启动主机测试，启用 ASan/UBSan；GMU/ZAP 的真实本地固件样本及截断/变异检查通过。
- 所有跟踪 Python 文件语法、shell 脚本语法、Markdown 本地链接、源码路径及常见凭据模式检查。

复用了本地 SDK/依赖库及 Mesa 缓存；本次没有从空环境重下载全部 Apple 文件、重编译完整 Mesa/Mu 或重新做手机上的 10 分钟运行。因此[功能状态](STATUS.zh-CN.md)中的真机数据明确归属先前基线，不把本次编译当成又一次实机验证。

源码检查方法：

```sh
python3 scripts/check-public-source.py
```

检查只读取 Git 跟踪的源码，检查器不能保证发现所有敏感信息。上游 patch 的上下文空白及原文件格式保留，不为消除整仓 whitespace 警告修改补丁语义；发布新增文档/脚本另做格式检查。

## 获取与后续工作

公开入口：[YiKongk/HackiOS-Ace5](https://github.com/YiKongk/HackiOS-Ace5)。

[UEFI 项目链接与教程](UEFI.zh-CN.md)包括固定 Mu 版本、主机依赖、自己的 DTB、诊断/内核模式、打包和恢复。完整系统构建见 [BUILD.zh-CN.md](BUILD.zh-CN.md)。此版本只发布源码，没有包含 iOS 或厂商固件的 GitHub Release 附件。
