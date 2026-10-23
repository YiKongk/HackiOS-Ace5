# 架构与开发

## 代码分层

| 路径 | 作用 |
| --- | --- |
| `uefi/`、`tools/ace5/prepare-uefi.py` | 固定 Mu-Silicium 基线及启动钩子 |
| `xnuloader/` | EFI loader、USB 命令、RAM 镜像和 XNU 启动/返回 |
| `xnu/redwood.patch`、`xnu/files/` | Apple XNU 补丁和新增文件，包含 `giulia_*` 硬件适配 |
| `xnu/files/bsd/dev/giulia_s3910.c` | Ace 5 S3910 触摸传输与报告 |
| `xnu/files/bsd/dev/giulia_gpu*` | A750 初始化、提交、寄存器和 GMU/SMMU 辅助代码 |
| `tools/mesa/`、`tools/mk-mesa.sh` | Mesa Freedreno、Darwin 接口、AIR 编译与兼容补丁 |
| `userland/src/` | EAGL、Metal、系统服务适配、init 和测试程序 |
| `tools/apfs/` | 只读 APFS 镜像生成与独立读取检查 |
| `tools/ace5/` | 机型检查、有限会话、组装、像素/日志验证 |

公开仓库是单 Git 工作树。`common/ugit` 仅转发至同一个仓库，不再创建隐藏的第二套用户空间历史。`common/setup.sh` 只做工具检查；在没有 USB 工具的纯编译机报缺 adb/fastboot 不等于编译器损坏。

`docs/ACE5.md` 是历史过程记录。其他英文文档主要继承自 redwood 上游，用于理解实现；本文、README 与中文构建教程才定义本公开版的 Ace 5 流程。历史日志路径和已移除的预编译资源并非发布附件。

## 主机端驱动检查

从仓库根目录执行。这些测试模拟 MMIO/队列或检查解析器，不会访问手机：

```sh
mkdir -p local/tests
for name in s3910-report gmu-fw gmu-hfi gpu-direct gpu-live gpu-submit; do
  cc -std=c11 -fsanitize=address,undefined -g "tools/ace5/test-$name.c" \
    -o "local/tests/test-$name" || exit 1
  "local/tests/test-$name" || exit 1
done
```

有自己提取的固件时，再验证真实样本及截断/变异边界：

```sh
local/tests/test-gmu-fw fw-gpu/gmu_gen70900.bin
cc -std=c11 -fsanitize=address,undefined -g tools/ace5/test-zap-fw.c \
  -o local/tests/test-zap-fw
local/tests/test-zap-fw fw-gpu/gen70900_zap.mbn
cc -std=c11 -fsanitize=address,undefined -g tools/ace5/test-zap-start.c \
  -o local/tests/test-zap-start
local/tests/test-zap-start fw-gpu/gen70900_zap.mbn
```

真实硬件协议、缓存一致性、时序和热状态仍必须实机验证。不要把 host PASS 写成“GPU 完整适配”。

## 验证顺序

1. 解析器/队列测试与源码编译。
2. 机型、DTB、固定 UEFI 地址及镜像 SHA-256。
3. loader 身份与 XNU selftest。
4. 有限 RAM 会话中的 fence、读回、错误路径和返回日志。
5. 完整 GPU/Metal recipe，再做人工 UI 操作和更长稳定性试验。

自动 UI recipe 会主动触摸和打开应用；不要与人工操作同时做帧率对照。每次保留本地的源码提交、工具链、输入固件哈希和启动参数，用时间戳区分输出。

## 提交与发布

默认只在本地 commit。**未经用户明确要求，不 push 分支、标签、release 或源码。** 2026-10-24 初次开源授权不延伸到日后开发。

提交前检查 `git diff --check`、变更内容和适当测试。不要用 `git add -f` 绕过固件/镜像忽略规则。Apple 文件、SDK、固件、DTB、日志、分区备份和本地绝对路径留在本机。清理仅针对可再生输出，保留恢复备份。

新增适配应注明来源与许可证，保持文件头；更新功能状态时写清“编译通过”“主机测试通过”“实机验证”的区别。不要自动把新的私有试验合入这个已公布的基线。
