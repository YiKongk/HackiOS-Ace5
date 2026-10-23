# 操作、日志与恢复

本版本采用临时 RAM 启动，保留 Android 启动链。普通分区写入在 Ace 5 loader 构建中关闭，但 **logdump 诊断写入仍然存在**。不要将 `storage-writes=disabled` 解读为零存储写入。

## 首次操作前保存本机备份

Android 已启动、ADB 已授权、root 正常时，在 USB 主机运行：

```sh
export ACE5_SERIAL='自己的 Android 序列号'
python3 tools/ace5/backup.py --serial "$ACE5_SERIAL" \
  --output local/backups/ace5-before-first-boot
```

脚本限定 PKG110，只读取两槽 boot、init_boot、vendor_boot、dtbo、vbmeta 系列、recovery、xbl、xbl_config、uefi 共 22 个分区，逐项比较远端和本地 SHA-256。已有完成 manifest 会拒绝覆盖。中断时只有 partial manifest；它不代表完整备份。不同固件若缺少分区应先核对实际布局，不能跳过报错后宣称完成。

这是启动链备份，不是用户数据或所有校准分区的全量备份。自行另存需要的数据与原厂恢复材料；把备份复制到源码目录外的可靠存储。备份、序列号和设备信息不应上传 GitHub。

## 正常退出

`phone.py run --seconds 600` 是有限会话。时间到后 XNU 返回 loader，脚本读取内核日志和 framebuffer。保持 USB 连接，等待脚本结束，不同时运行另一条 fastboot 命令。

```sh
python3 tools/ace5/phone.py --serial "$ACE5_SERIAL" reboot
```

该命令请求回到 Android。再次物理启动 UEFI 后，之前的系统 RAM 镜像已不存在，必须重新上传。

## 卡住但 USB 仍在

先保留屏幕和线缆，记录卡住前操作及时间。启用 `--bootcpu 1 --live-log` 时，CPU 0 可持续采集内核日志；iOS 界面停止更新不必然等于 loader USB 已停止。

脚本保存到 `logs/giulia/`。提交 issue 前摘取必要错误片段并脱敏，不上传整个日志。对于滚动快照，可按工具帮助合并，避免把相同内容重复统计：

```sh
python3 tools/ace5/summarize-ui-log.py --help
python3 tools/ace5/summarize-ui-log.py --snapshots \
  --merged-output local/recovered.log logs/giulia/运行前缀-live-*.log
```

不要在内核返回后、保存 staged 日志前主动调用 `fastboot oem log`：它可能把待取回的内核日志替换成 loader 自己的日志。优先让 `phone.py run` 完成既定取回流程。

## USB 完全消失或 Qualcomm CrashDump

曾出现设备只以 Qualcomm `05c6:900e` 枚举、ADB/fastboot 均无法控制的情况。此前使用过的恢复方法：保持有电，长按 **电源键＋音量上键约 15 秒** 强制重启，等待回到 Android，然后确认 `adb devices` 和 root。

按键无效、反复 CrashDump 或不能回 Android 时停止新试验，核对供电/连接与设备原厂恢复方案。本公开教程不提供 EDL 刷写、不要求刷 xbl/uefi、不重新分区，也不把其他机型的镜像作为修复办法。

## 哪些命令不属于本教程

仓库保留上游 redwood 的研究源码，因此能找到 `common/flash.sh`、分区工具、通用 `xl` 等。它们的存在不代表可以在 Ace 5 上执行。Ace 5 的已验证入口是 `tools/ace5/phone.py` 配合对应的 manifest 和 PKG110 检查。

任何将来持久化安装，都需要独立设计、完整验证和明确的设备操作授权；不属于这个 Wi‑Fi 前源码快照。
