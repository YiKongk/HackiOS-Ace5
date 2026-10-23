# 从源码构建并在 Ace 5 上启动

本教程对应 [RELEASE.json](../RELEASE.json) 的 Wi‑Fi 适配前版本，只适用于 PKG110。首次配置会下载外部依赖；之后手机的 iOS 会话放在 RAM 中。请先读[功能边界](STATUS.zh-CN.md)和[恢复说明](RECOVERY.zh-CN.md)。

## 1. 准备环境和目录

验证使用 Linux ARM64 / LLVM 18 编译 XNU、loader 和用户空间；Mac 负责 USB 以及 `sips` 壁纸转换。Apple Silicon 上可使用 Colima Linux ARM64，并共享源码目录。USB 命令必须在 `adb devices` / `fastboot devices` 能看到手机的主机执行，不要求把 USB 转交虚拟机。

建议预留至少 30 GB 空闲空间；Apple 文件、Mu 子模块和构建缓存比本仓库大很多。所有命令从本仓库根目录执行，明确标注的主机切换除外。不要用符号链接把旧的 iOS 解包目录当新构建目录：提取和字体处理会修改文件。

Linux 的基本依赖示例：

```sh
sudo apt-get update
sudo apt-get install build-essential git curl xz-utils clang-18 lld-18 llvm-18 \
  flex bison m4 perl autoconf automake libtool pkg-config libncurses-dev \
  python3 python3-venv python3-pip meson ninja-build nasm
export PATH="/usr/lib/llvm-18/bin:$PATH"
mkdir -p local
```

另外需要 **7-Zip 23 或更新版**，且命令名为 `7z`，旧 p7zip 不能完成 APFS 提取；以及 Python `fonttools`、`Pillow`、`lzfse`（图像/系统工具按实际脚本使用）。通过本地 venv 安装，不修改系统 Python：

```sh
python3 -m venv local/image-venv
. local/image-venv/bin/activate
python3 -m pip install fonttools Pillow lzfse
```

## 2. Theos 工具链与 SDK

本项目使用 Theos 的目录布局和 Mach-O 工具，不要求运行 Theos 的自动更新安装器。已用版本：

- Linux ARM64 cctools：[iOSToolchain-aarch64.tar.xz，test-210562a](https://github.com/L1ghtmann/llvm-project/releases/tag/test-210562a)。
- SDK 来源：[theos/sdks，0222fd5413cf4b9af096f37b4621afa2688572f7](https://github.com/theos/sdks/tree/0222fd5413cf4b9af096f37b4621afa2688572f7)，使用 `iPhoneOS16.5.sdk`。
- 编译器使用本机 LLVM 18，链接器和 `ldid` 使用上述 Linux ARM64 工具链。

新目录示例（已有目录不要覆盖）：

```sh
export THEOS="$PWD/local/theos"
mkdir -p "$THEOS/toolchain"
curl -fL --retry 3 -o local/ios-toolchain-arm64.tar.xz \
  https://github.com/L1ghtmann/llvm-project/releases/download/test-210562a/iOSToolchain-aarch64.tar.xz
tar -xJf local/ios-toolchain-arm64.tar.xz -C "$THEOS/toolchain"
git clone --no-checkout https://github.com/theos/sdks.git "$THEOS/sdks"
git -C "$THEOS/sdks" checkout --detach 0222fd5413cf4b9af096f37b4621afa2688572f7
test -x "$THEOS/toolchain/linux/iphone/bin/ld"
test -x "$THEOS/toolchain/linux/iphone/bin/ldid"
test -d "$THEOS/sdks/iPhoneOS16.5.sdk/usr/include"
```

SDK 和 Apple 文件不随本仓库分发；自行确认所用外部材料的适用条款。`xnu-deps/setup.sh` 还会下载开源构建依赖及 macOS 13.3 SDK 的 Kernel.framework 头文件。版本和下载地址在脚本中可查。

## 3. 应用并构建 XNU

```sh
xnu-deps/setup.sh --stage1
xnu/apply.sh
xnu-deps/setup.sh --stage2
./xnu-build.sh -j2 build
mkdir -p out/giulia-kernel
cp xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837 \
  out/giulia-kernel/mach.ace5-public
```

`apply.sh` 获取 Apple `xnu-8796.121.2`，校验基础提交 `c54f35ca767986246321eb901baf8f5ff7923f6a`，应用补丁并复制驱动。现有目标目录会被拒绝，避免覆盖其他试验。`BCM2837` 是沿用的 XNU 构建配置名，不代表设备是树莓派。

如果上述产物路径不存在，先确认构建以状态 0 结束并检查实际输出；不要使用旧镜像冒充新构建。本教程使用 `build` 目标，不用 `-k` 掩盖错误，也不构建 Ace 5 尚未适配的 ath11k 对象。

## 4. UEFI 和 DTB

按[UEFI 完整教程](UEFI.zh-CN.md)准备自己的 DTB，构建 Mu-Silicium 和 kernel-stage loader，得到：

```text
out/giulia-kernel/hackios-giulia-kernel.img
out/giulia-kernel/hackios-giulia-kernel.json
```

UEFI、loader、XNU 是三个产物；只重新编译内核时通常不用重新编译 Mu。

## 5. 取得本机 GPU 固件

从自己的 Android vendor 固件提取以下四个文件，放在仓库根目录的 **`fw-gpu/`**：

| 文件 | 已验证样本大小 |
| --- | ---: |
| `gen70900_aqe.fw` | 33,176 字节 |
| `gen70900_sqe.fw` | 85,364 字节 |
| `gen70900_zap.mbn` | 12,088 字节 |
| `gmu_gen70900.bin` | 78,588 字节 |

在 Android 中用 root 查找真实路径，不假定不同固件版本目录相同：

```sh
adb -s "$ACE5_SERIAL" shell su -c \
  'find /vendor/firmware /vendor/firmware_mnt /odm/firmware -type f 2>/dev/null' \
  > local/device/firmware-paths.txt
```

对每个已找到的实际路径，用 `adb exec-out su -c 'cat 实际路径' > fw-gpu/文件名` 读取；保持文件名不变。不要用其他 GPU 型号的固件替代。大小相同也不证明版本兼容，检查结构和哈希：

```sh
python3 tools/ace5/inspect-adreno750-fw.py fw-gpu
```

这些文件只在本地使用。`userland/build.sh` 将它们复制到小 root 的 `fw/`；打包时保持这一目录不压缩。无需获取 Wi‑Fi 或音频固件。

## 6. iOS 本地提取、Mesa 和用户空间

`ios165/fetch.sh` 从 Apple CDN 按需读取 iOS 16.5 / 20F66 / iPhone X 的 IPSW 成员，不从本仓库下载系统镜像。需要稳定网络和支持 APFS 的 7-Zip。

```sh
bash ios165/fetch.sh
AIR=1 DRIVERS=softpipe,freedreno tools/mk-mesa.sh
GL=1 PREBUILT= bash userland/build.sh
tools/ace5/build-gpu-userland.sh local/components --metal
```

Mesa 固定为 25.0.7，下载 SHA-256 由脚本验证。`AIR=1` 构建 AIR→NIR→ir3 路径；`build-gpu-userland.sh` 校验 AIR 库与源码的对应关系。不要混用旧 AIR 库和新 Metal 驱动。Mesa 产物不保证逐字节重现，哈希用于识别文件，不能替代源码版本检查。

这里不设置 `REST=1`、`FLASHBOOT=1`、`ADSP=1` 或 `JB=1`。基础 root 使用默认的有限服务集合，下一步补 sleepd、configd 和 PosterBoard。本版本没有预编译兜底文件，缺编译器时应修复环境。

生成新的小 root 和系统目录：

```sh
python3 tools/ace5/prepare-gpu-root.py \
  --base userland/root --components local/components --ios-root ios165/rootfs \
  --output local/root-daily --metal --app --display-test --wallpaper \
  --compositor-threading inline --jobs tools/ace5/rc.jobs.daily
python3 tools/ace5/prepare-gpu-system.py --ios ios165 \
  --stage0 userland/obj/stage0 --triangle local/components/triangle_app \
  --output local/system-metal
```

这些脚本拒绝覆盖已有输出。`inline` 只改变 backboardd 的 Mesa 命令生成线程策略，不会关闭硬件 GPU。`--app` 加入应用启动测试工具；Triangle 应用放在系统镜像中。

### 壁纸转换：在 macOS 上执行

保持同一源码和本地目录可见，在具有 `sips` 的 macOS 主机执行：

```sh
python3 tools/ace5/prepare-wallpaper-system.py \
  --base local/system-metal --output local/system-wallpaper
```

它创建硬链接副本，在写入前断开需要修改的链接，把默认 iOS 16 壁纸 HEIC 图层转成 PNG，并修正 CAML 中的资源名；保留原 CAML 动画。曾经的黑色背景来自壁纸图层解码/服务链缺失，不能仅凭 GPU 初始化成功解决。此流程不需要把 mediaserverd 及音频依赖全部启动。

### 生成和检查 APFS：回 Linux 构建环境

```sh
python3 tools/apfs/mkapfs.py -z --plain fw -o local/root-daily.apfs \
  redwood=local/root-daily
tools/apfs/check-image.sh local/root-daily.apfs redwood=local/root-daily
python3 tools/apfs/mkapfs.py -z -o local/system-wallpaper.apfs \
  System=local/system-wallpaper,manifest=ios165/system.manifest
tools/apfs/check-image.sh local/system-wallpaper.apfs \
  System=local/system-wallpaper,manifest=ios165/system.manifest
```

检查同时使用 apfsck 和移植内核的 APFS 读取代码，对照输入目录读取文件。小 root 需符合 `phone.py` 的 32 MiB 限制；系统 RAM 镜像约 2.5 GB。检查失败时不要启动该镜像。

## 7. USB 主机上启动

将镜像、对应 UEFI `.json` 和内核放到 USB 主机的上述相同路径。`ACE5_SERIAL` 是自己 Android / bootloader 的序列号，loader 的 fastboot 身份固定为 `xnuloader`。

先按[恢复文档](RECOVERY.zh-CN.md)备份。以下操作仅在当前为 Android 时执行 `enter`：

```sh
export ACE5_SERIAL='自己的序列号'
python3 tools/ace5/phone.py --serial "$ACE5_SERIAL" enter --stage kernel
python3 tools/ace5/phone.py --serial "$ACE5_SERIAL" selftest \
  --kernel out/giulia-kernel/mach.ace5-public
python3 tools/ace5/phone.py --serial "$ACE5_SERIAL" ramdisk \
  --image local/system-wallpaper.apfs
python3 tools/ace5/phone.py --serial "$ACE5_SERIAL" run \
  --seconds 600 --bootcpu 1 --gpu-probe 20 --gpu-direct --gpu-keep \
  --metal --gpu-display --fb-filter bilinear --touch \
  --live-log --live-interval 10 \
  --kernel out/giulia-kernel/mach.ace5-public --root-image local/root-daily.apfs
```

系统上传约 80 秒是此前设备的观测值，实际时间受 USB 影响。会话约 10 分钟后返回 loader 并读取日志/画面；小 root 可以换，系统 RAM 镜像在 loader 会话中可复用。物理重启后必须重新上传系统。

日常 recipe 等待触摸初始化并做送屏读回，不会自动反复滑动桌面。若改为自动压力测试，应使用新的 root 输出目录和 `rc.jobs.gpudisplay`，避免与人工交互同时进行。

启动命令没有 `wifi=1` 或 SMP 参数。不要添加后续开发分支的 `--gpu-profile`、`--gpu-ring-range`、`--cpus`、`--wifi-probe`，此公开基线不包含它们。

## 8. 确认硬件路径而不只看“启动成功”

完整日志应有 CoreAnimation 的 FD750 上下文、GPU 送屏计数与 fence 前进、display_test 的像素读回结果。只看到 `Metal` 设备名称不说明所有 UI 已硬件执行。

对 `rc.jobs.gpudisplay` 对应的完整压力测试日志：

```sh
python3 tools/ace5/check-metal-log.py --gpu-display local/gpudisplay-run.log
```

这个检查器要求规定测试完成证据，不适用于 `rc.jobs.daily` 的短自检日志。手工滑动仍需观察：硬件送屏成功不代表 SpringBoard 的 CPU 工作、帧调度、显示控制器和所有应用都没有瓶颈。
