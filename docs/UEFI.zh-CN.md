# Ace 5 的 UEFI 项目与教程

## 1. 使用哪个项目

- 项目主页：[Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)。
- 本次固定版本：[42fe563791e51bf5500c2e470ed294ca0c94b97c](https://github.com/Project-Silicium/Mu-Silicium/tree/42fe563791e51bf5500c2e470ed294ca0c94b97c)。
- 对应的上游[编译说明](https://github.com/Project-Silicium/Mu-Silicium/blob/42fe563791e51bf5500c2e470ed294ca0c94b97c/Building.md)。
- giulia 平台源码：[Platforms/OnePlus/giuliaPkg](https://github.com/Project-Silicium/Mu-Silicium/tree/42fe563791e51bf5500c2e470ed294ca0c94b97c/Platforms/OnePlus/giuliaPkg)。
- 项目通用指南：[Project-Silicium/Guides](https://github.com/Project-Silicium/Guides)。

Mu-Silicium 提供 giulia 的 UEFI 平台、内存图、显示和 USB 环境。本仓库的 `xnuloader` 是被 UEFI 启动的程序，负责接收 RAM 镜像、构造 XNU 启动参数、启动/观察内核及取回日志。UEFI 本身不是 iOS，也不会单独提供 GPU、触摸或 Wi‑Fi 的 iOS 驱动。

启动关系：Android bootloader → `fastboot boot` → Mu-Silicium UEFI → XNU loader → XNU → iOS 用户空间。

## 2. 机型、目录与主机

仅验证 OnePlus Ace 5 **PKG110 / giulia / SM8650**。本项目与上游的 redwood 是两个平台，不能混用 DTB、UEFI FD 或引导地址。

已使用的构建环境为 Apple Silicon Mac 上的 **Colima Linux ARM64**、LLVM 18、Python 3.11+。USB 操作在能够直接访问手机的主机运行。其他 Linux 环境可能可构建，但本发布不把它们列为已验证组合。

示例目录：

```text
workspace/
  HackiOS-Ace5/       # 本源码
  Mu-Silicium/       # 下述固定版本与子模块
  theos/             # XNU/用户空间工具链；UEFI 单独不依赖它
```

建议无空格路径；准备充足磁盘空间，Mu 及子模块约数 GB，完整系统和构建缓存另计。

## 3. Linux 编译依赖

Ubuntu 类系统示例（版本号和可用软件包以实际发行版为准）：

```sh
sudo apt-get update
sudo apt-get install git build-essential clang-18 lld-18 llvm-18 nasm \
  python3 python3-venv python3-pip uuid-dev gcc-aarch64-linux-gnu \
  device-tree-compiler acpica-tools
export PATH="/usr/lib/llvm-18/bin:$PATH"
```

也可参考上游 `setup_env.sh`，但保留下面固定的源码版本，不用 `build_uefi.py -u` 自动升级。

从本仓库根目录执行：

```sh
export ACE5_WORK="$PWD"
git clone --no-checkout https://github.com/Project-Silicium/Mu-Silicium.git ../Mu-Silicium
git -C ../Mu-Silicium checkout --detach 42fe563791e51bf5500c2e470ed294ca0c94b97c
git -C ../Mu-Silicium submodule update --init --recursive
export ACE5_MU="$(cd ../Mu-Silicium && pwd)"
python3 -m venv local/mu-venv
. local/mu-venv/bin/activate
python3 -m pip install -r "$ACE5_MU/pip-requirements.txt"
```

Mu 的子模块包含独立维护的设备二进制依赖；它们由 Mu 的依赖流程取得，不是本仓库的源码或本仓库重新分发的固件。保留它们自己的来源和许可证。

**仅 Linux ARM64** 继续运行本项目的原生 BaseTools 辅助脚本：

```sh
python3 tools/ace5/prepare-linux-arm64.py "$ACE5_MU"
export CLANGPDB_AARCH64_PREFIX=aarch64-linux-gnu-
```

该脚本编译固定 Mu 子模块中的 BaseTools，登记本机 NASM/BaseTools 的路径及来源，拒绝非 Linux ARM64 或错误 Mu 提交。不要把 ARM64 的工具覆盖步骤套到 x86-64 主机。

## 4. 从自己的手机取得 DTB

在 USB 主机上，Android 启动完成、ADB 已授权、root 可用后：

```sh
export ACE5_SERIAL='替换为 adb devices 显示的序列号'
adb -s "$ACE5_SERIAL" shell getprop ro.product.model
adb -s "$ACE5_SERIAL" shell su -c id
mkdir -p local/device
adb -s "$ACE5_SERIAL" exec-out su -c 'cat /sys/firmware/fdt' > local/device/device.dtb
python3 tools/ace5/inspect-dtb.py local/device/device.dtb
```

机型必须是 PKG110。保留该 DTB 与 Android 固件版本记录，不从别人的机型复制。若该节点不可读或输出为空，停止打包并检查 root/固件差异。

编译主机和 USB 主机不在同一文件系统时，手动复制这个本地 DTB。不要把 DTB 或分区备份提交到 Git。

## 5. 先构建诊断模式

回到 Linux 构建主机和本仓库根目录，使用本机绝对路径：

```sh
tools/ace5/build-diagnostic.sh "$ACE5_MU" "$ACE5_WORK/local/device/device.dtb"
```

脚本按顺序执行：

1. `prepare-uefi.py` 校验固定提交并应用 `uefi/mu-silicium.patch`。
2. 安装 `XnuLoader.c/.h` 启动钩子，将 payload 地址设为 `0xA8000000`，修正 giulia 配置里的型号拼写。
3. 编译禁止执行 XNU 的诊断 loader。
4. `build_uefi.py -d giulia -m 0 -r RELEASE` 编译 UEFI FD。
5. 把 BootShim、FD、loader payload 与自己的 DTB 打包成 Android v4 boot image。

产物：

- `out/giulia/hackios-giulia-diagnostic.img`
- 同名 `.json`：镜像与 payload SHA-256、DTB 哈希、内存地址、阶段和写入策略。
- Mu 内的 `Build/giuliaPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd`。

本设备的 FD 基址为 `0xA7100000`、大小 `0x300000`，payload 为 `0xA8000000`。打包器会校验这些值；不要为了绕过报错而改成 redwood 的布局。

## 6. 构建可执行 XNU 的模式

复用上一步生成的 UEFI FD：

```sh
tools/ace5/build-loader.sh kernel
python3 tools/ace5/pack-boot.py --mu "$ACE5_MU" \
  --dtb "$ACE5_WORK/local/device/device.dtb" --stage kernel \
  --output out/giulia-kernel/hackios-giulia-kernel.img
```

产物为 `out/giulia-kernel/hackios-giulia-kernel.img` 和同名 `.json`。
诊断模式会拒绝执行内核；内核模式也仍禁用普通分区写入。两种模式都允许 loader 使用 **logdump** 保存诊断，不应将其描述为完全不写存储。

## 7. 临时启动与确认

把对应 `.img` 和 `.json` 放到 USB 主机上本仓库的相同 `out/` 路径。先做自己的备份，见[恢复文档](RECOVERY.zh-CN.md)。

```sh
# 可选：仅诊断 loader
python3 tools/ace5/phone.py --serial "$ACE5_SERIAL" enter --stage diagnostic
fastboot -s xnuloader getvar product
fastboot -s xnuloader getvar platform
fastboot -s xnuloader getvar build-stage
fastboot -s xnuloader getvar storage-writes
python3 tools/ace5/phone.py --serial "$ACE5_SERIAL" reboot
```

应分别看到 `xnuloader`、`giulia`、`diagnostic`、`disabled`。
随后需要 XNU 时，从 Android 按同样方式 `enter --stage kernel`，不要在 loader 已运行时重复 `enter`。

`phone.py enter` 会核对机型、bootloader 的 `product=pineapple` / `unlocked=yes`、镜像 SHA-256、阶段和 loader 身份，再执行临时 RAM 启动。它没有 `flash`、`erase`、改 slot 或重新分区操作。

UEFI/loader 成功后，继续[构建与启动教程](BUILD.zh-CN.md)上传系统 RAM 镜像并启动 XNU。没有系统镜像时，loader 可以正常运行，但这不证明 iOS 已启动。

## 8. 退出与故障定位

- 有限 iOS 会话结束先回到 loader，再用 `phone.py reboot` 回 Android。
- 启动 XNU 时使用 `--bootcpu 1 --live-log`，让 CPU 0 保留 USB 观察能力。
- Qualcomm CrashDump / `05c6:900e` 且 ADB、fastboot 均不可用时，已用过的恢复方法是长按电源键＋音量上键约 15 秒，等待 Android；按键恢复不是刷机。
- 不把 EDL 工具、刷写 xbl/uefi 或重复刷 boot 当成这份教程的恢复步骤。
- UEFI 构建成功、manifest 通过、XNU 自测通过、iOS 桌面可用是不同里程碑，分别验证。

源码自带 `uefi/apply.sh` / redwood 平台文件属于上游参考；Ace 5 使用本文专用流程。其他机型的预编译 UEFI、DTB 和 vendor 二进制已从本公开版清理。
