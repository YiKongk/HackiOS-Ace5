> Historical bring-up notes through the pre-Wi-Fi baseline. For current release instructions and verified limitations, start with [the Chinese guide](../README.md). Earlier sections describe intermediate states, not the final release. Referenced raw logs and local images are intentionally not distributed.

# OnePlus Ace 5 / giulia bring-up

This is an experimental **Ace 5 port**, not a complete hardware adaptation.
Adreno 750 now executes OpenGL ES and an experimental Metal/AIR subset, including
verified compute, color/index/depth drawing and full-frame CAMetalLayer readback.
The BGRA desktop path now also supports A750 hardware scaling/copy into the panel
framebuffer; see the GPU display validation below. This removes CPU pixel scaling
from that path, without implying complete Metal API support or sustained 60 FPS.
The repeated Settings/Wi-Fi stall was reproduced by omitting sleepd/configd and
resolved in a matched trial by restoring their launchd jobs. This addresses
navigation into the Wi-Fi settings page, not wireless networking support.
A 600-second run completed 600 Metal application frames without readback errors,
then entered Wi-Fi, returned and scrolled Settings successfully. Broader
application compatibility and sustained interactive performance remain unproven.
Subsequent interactive sessions still reported stalls, including after an airplane
mode change; that investigation was deferred at the user's request to prioritize
GPU display acceleration. The service-omission trial identified one reproducible
cause, not every possible cause of a frozen interface.
Temporary UEFI/loader boot, USB diagnostics, XNU's early initialization and
single-core operation are verified on the phone. The first full iOS RAM boot reached
SpringBoard: the user saw the desktop, and the recovered log records completion
of shell launch and continuing framebuffer swaps. The S3910 touch driver now
passes UEFI input capture and XNU initialization. A 180-second run delivered
physical touches to backboardd, and the returned framebuffer shows the Phone app.
The user confirmed normal touch response and accurate positions. Physical
multi-touch and extended operation still need separate verification. A later
sustained run froze on the desktop. The new separate-core observer reproduced
that symptom and retained the logs: touch reports, background processes and
framebuffer submissions continued. Those earlier trials did not establish stable
desktop interaction; the subsequent service fix and controlled tests are below.
USB recovery failed during the first full-system run; the later 180-second touch
trial returned its full log and framebuffer successfully.

There are separate diagnostic and kernel-capable images. XNU execution is
compiled out of the diagnostic image; ordinary partition writes are disabled in
both. Neither image contains Apple userland files or flashes itself.

## Hardware evidence

Read from the connected PKG110 running `PKG110_16.0.10.500(CN01)` on 2026-10-22:

| Item | Value |
| --- | --- |
| Board | `OP5D2BL1`, DT model ends in `giulia` |
| SoC | SM8650 / pineapple |
| Android | 16, Linux 6.1.141, KernelSU root |
| Boot state at collection | unlocked, slot `_a` |
| GIC distributor / redistributor | `0x17100000` / `0x17180000`; explicit RD stride `0x40000` |
| GPU | Adreno 750v2, chip ID `0x43051401`, compatible `qcom,adreno-gpu-gen7-9-1` |
| CPU affinity | 0x000–0x700, stride 0x100; little cores 0 and 1 |
| Framebuffer reservation | `0xD5100000`, size `0x02B00000` |
| Display | 1264 × 2780, stride 5056 bytes (GOP and USB framebuffer verified) |
| UEFI FD | `0xA7100000`, size `0x00300000` |
| Payload | `0xA8000000`, 64 MiB inside Mu's reserved Kernel region |
| Active touch node | Synaptics S3910 HBP SPI (`synaptics,tcm-spi-hbp`), IRQ GPIO 162, reset GPIO 161 |
| Android boot image | header v4, per Mu giulia config |

Mu revision: `42fe563791e51bf5500c2e470ed294ca0c94b97c`.
The original HackiOS patch applies cleanly to that revision. The giulia-specific
preparation changes the payload address and corrects the upstream SKU typo.
The loader is linked at `0xB8000000`, with normal UEFI relocation if occupied.

## Verified runtime milestones (2026-10-22)

- Stock bootloader: `product=pineapple`, unlocked, slot `a`; `fastboot boot` works.
- UEFI build completed and its post-build validation accepted 49 PE images.
- Diagnostic loader: EL1, MIDR `0x410fd801`, timer 19.2 MHz, image at `0xB8000000`.
- USB reported `product=xnuloader`, screen `1264 2780 5056`.
- Loader wrote 4096 bytes to logdump and read back an exact match.
- Diagnostic `oem run` was rejected as intended.
- Loader reboot returned to Android with the original model, slot and KernelSU root.
- Kernel self-test returned `selftest-ok`; checkpoints 5 (MMU enabled) and 21
  (early initialization, before machine startup) both returned successfully.
- XNU claimed a 6 GiB RAM window at `0x8C0000000` and completed a 20-second
  trial with the small root image, returning 21,822 bytes of kernel log.
- The 2,492,203,008-byte system image uploaded to RAM successfully in 78 seconds.
- First full-system attempt: `cpus=1`, 120-second return timer, hardware extras
  disabled. User reported the desktop. USB failed while waiting for the result;
  reconnection did not restore the host connection.
- After the user's physical restart, Android, slot `_a` and root were verified.
  Boot, init_boot, vendor_boot, dtbo and vbmeta in slot A still match their backups.
- Recovered logdump contains a complete 501,536-byte kernel log: all 36 initial
  userland checks passed, SpringBoard finished launching, and framebuffer swaps
  continued at approximately 10 fps near the end of the run. The return timer
  expired and XNU returned to the loader. The missing host response is therefore
  a USB recovery problem, not evidence that XNU failed to return.
- The loader's new platform/stage/write-policy queries and `phone.py` self-test
  were exercised successfully. An experimental run on physical CPU 1 returned
  an unclassified abort (`fault_code=0x10`) during interrupt setup. This was fixed
  by passing the original DT's explicit redistributor stride (`0x40000`).
  `GICR_TYPER` on this device does not advertise VLPI frames, so inferring the
  stride from that register alone is insufficient. Physical CPU 1 now runs XNU
  and the full RAM system while CPU 0 services USB. Host stop and live log
  retrieval were exercised during the reproduced desktop freeze.

The first short-run kernel log was accidentally replaced by the loader's idle
`oem log` response before retrieval. Future runs fetch `get_staged` directly after
the result; do not issue `oem log` between completion and retrieval.

## Permissions and backups

The user explicitly permits loader writes to **logdump**. On 2026-10-22 the user
also authorized persistent system flashing and data deletion **if it substantially
accelerates adaptation**. Touch and GPU/Metal experiments have continued through
RAM boot, which supports the required tests and preserves Android as a reference.
No persistent system image, formatting, repartitioning or slot change has been used.
A/B boot-related partition backups and logdump are local, outside the Git trees.
Each image was compared with the phone's SHA-256. They are not uploaded to GitHub.

The diagnostic loader rejects `oem run` and `oem selftest`. Its normal block-write
path is disabled at compile time; logdump writes are separate and remain enabled.
Mu uses emulated NVRAM (`PcdEmuVariableNvModeEnable=TRUE`). The proprietary firmware
drivers are not fully auditable, so "no fastboot flash" is not a proof that every
firmware operation is read-only.

## Build

Use Linux with Mu-Silicium's build dependencies, Python 3.11+, clang, lld, llvm,
nasm and GNU tools. Clone Mu at the revision above, including its submodules.
Install its pinned Python requirements in a virtual environment and activate it.

The tested build host is Linux ARM64 in Colima on an Apple Silicon Mac, using
clang/lld/llvm 18. Install the Linux packages above plus `uuid-dev`,
`gcc-aarch64-linux-gnu` and `device-tree-compiler`. Native ARM64 BaseTools can be
built from the pinned Mu submodule with:

```sh
python3 tools/ace5/prepare-linux-arm64.py /absolute/path/to/Mu-Silicium
export CLANGPDB_AARCH64_PREFIX=aarch64-linux-gnu-
```

That helper registers local BaseTools/system NASM overrides and records their
provenance. Other external dependencies are downloaded and verified by Mu's
dependency manager. Put LLVM's `bin` directory and the Python venv on `PATH`.

```sh
tools/ace5/build-diagnostic.sh /absolute/path/to/Mu-Silicium /absolute/path/to/device.dtb
```

Outputs: `out/giulia/hackios-giulia-diagnostic.img` and a JSON manifest with SHA-256,
DTB hash, memory layout, revision, and the explicit diagnostic-only status.
`common/build.sh giulia` and `common/run.sh giulia` intentionally refuse to use the
redwood boot chain. `mkxnuimg.py` also refuses non-redwood targets; use the dedicated
packer, which requires the actual giulia FD and matching DTB.

After the UEFI FD is built, a kernel-capable loader can reuse it:

```sh
tools/ace5/build-loader.sh kernel
python3 tools/ace5/pack-boot.py --mu /absolute/path/to/Mu-Silicium \
  --dtb /absolute/path/to/device.dtb --stage kernel \
  --output out/giulia-kernel/hackios-giulia-kernel.img
```

The stage, loader/payload hashes and image hash are recorded in the manifest.
The helper rejects a stage mismatch. Loader PE timestamps are fixed for repeatable
builds with the same toolchain and UEFI FD.

## Host controls

`tools/ace5/phone.py` uses an explicit original device serial, verifies the image
hash and loader platform, and contains no flash/erase/slot-changing operation.
It requires the new loader's `platform`, `build-stage` and `storage-writes`
queries. The first successful trial used an earlier build without those queries.

```sh
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL enter --stage kernel
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL selftest
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL ramdisk
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL run --seconds 20
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL reboot
```

To keep iOS on screen until a physical restart, use `run --seconds 0`. That command
returns after the loader accepts the launch; it does not claim the desktop has
finished starting. Touch is disabled unless `--touch --kernel PATH` is supplied.
USB cannot be serviced while XNU
owns the boot core.

`--bootcpu 1` now works with the updated loader and kernel: XNU runs on the
second physical little core, and CPU 0 services USB. Add `--live-log` with a
finite run to save a log snapshot every ten seconds. `oem live-log` refuses an
idle kernel without overwriting the final staged log; `oem stop` ends the run.
On CPU 0 the host waits before querying the result to avoid a long
unanswered USB transfer. If USB remains absent, a physical power + volume-up
restart returns to Android because the boot chain was not flashed.

## First runtime validation

Prefer a temporary boot, if this bootloader supports it. Check the exact device
and image hash before invoking any boot command. If temporary boot is unsupported,
stop and request confirmation with the exact partition, image hash, and restoration
procedure. A diagnostic image in a persistent boot partition can return to itself
after reboot; do not flash it without a recovery procedure.

Once the loader is reachable: get its `product` and `screen`, request `oem memmap`
and `oem log`, fetch the staged log, then return to Android. If USB fails, the
permitted logdump partition can be read back from Android for diagnosis.

## Still required for iOS on the phone

- Investigate why USB recovery failed on the initial run. The 180-second touch
  trial returned a full log and a host-side screenshot successfully.
- Keep using the separate observer core to investigate desktop stalls. CPU 1
  interrupt initialization is fixed; SMP remains unvalidated.
- Validate the other cores and GIC redistributor layout before enabling SMP.
- Validate S3910 multi-touch, positional accuracy and extended operation; port
  buttons separately. The stock tree contains disabled Goodix alternatives.
- Port GPU (Adreno 750), Wi-Fi, audio and power separately. Redwood drivers and
  firmware must not be enabled just because both devices use Qualcomm hardware.
- Userland images are built and independently readable with `7zz t` (45,809 files
  in the system image); QEMU execution has not been performed.
- Validate SpringBoard stability and basic interaction from RAM before considering
  any persistent storage layout.

No Apple files, device backups, downloaded firmware or raw device tree are committed.

## S3910 touch development

The active device is S3910PA0B0-15.2, TouchComm v1, firmware build 4425728.
Android's cached configuration reports 20224 × 44480; after hardware reset the
controller reports maximum coordinates 12639 × 27799. The driver queries the
actual application information instead of assuming Android's coordinate scale.

The initial transport uses mode-0, MSB-first bit-banged SPI: MISO 48, MOSI 49,
clock 50, CS 51, reset 161, active-low IRQ 162, AVDD enable 108. GPIO registers
start at TLMM + 0x100000 (physical 0x0f100000). The 1.8 V rail is `ldob4` from
command DB at 0x81c60000; the verified resource address is 0x41c00. RPMh DRV2 at
0x17a20000 uses v3 registers; an idle unarmed active TCS is selected and restored.

UEFI testing verified power, reset, identification, application information,
LBP mode selection, volatile report configuration and readback. User input
produced 115 reports in one capture and 271 in another, with no transport errors.
235 retained records, including finger lifts, passed the same coordinate decoder
with AddressSanitizer and UndefinedBehaviorSanitizer. A locally built kernel passed its return-path self-test and a 20-second full
userland trial (36 initial checks, zero failures). Its S3910 driver identified
the chip and enabled reports, with no communication errors. Physical multi-touch
still needs explicit verification. The user confirmed normal touch response and
accurate positions in iOS. A subsequent
180-second run reached SpringBoard, delivered touches to backboardd, recorded
at least 310 reports with zero bad reports, and returned normally with a
665,455-byte kernel log and a framebuffer showing the opened Phone app. The
36 initial userland checks passed again. This confirms the basic event path;
it does not establish modem/phone-call functionality.

Diagnostic commands, only in the giulia loader while no kernel runs:

```sh
fastboot -s xnuloader oem touchstate   # registers only
fastboot -s xnuloader oem touchprobe   # power/reset; identify; enable LBP reports
fastboot -s xnuloader oem touchwatch   # finite capture; slide while it runs
fastboot -s xnuloader get_staged touch.log
```

The report configuration and dynamic mode changes are volatile. No touch
firmware download, erase, or configuration-commit command is used. Neither the
old Redwood driver nor the new driver is implicitly enabled. The new driver
requires `ace5_touch=1`; the host helper supplies it with `run --touch --kernel`.

Local XNU builds use Linux ARM64 (Colima), clang 18, flex, bison, autoconf,
automake, libncurses-dev, the Theos ARM64 cctools toolchain release
`test-210562a`, and Theos SDKs commit `0222fd5413cf4b9af096f37b4621afa2688572f7`.
The macOS 13.3 Kernel.framework headers archive SHA-256 is
`518e35eae6039b3f64e8025f4525c1c43786cc5cf39459d609852faf091e34be`.
Set THEOS to the local toolchain/SDK directory, run `xnu-deps/setup.sh`, then
`xnu-build.sh -j2 build` for the linked kernel. The default `all` target also
packages external kext symbol sets, which currently fails on missing DriverKit
exports; those packages are not used by this static RAM-boot kernel. The syscall generator uses literal cast parentheses for
portability to Ubuntu's mawk.

Hardware/protocol references: OnePlusOSS
`android_kernel_modules_and_devicetree_oneplus_sm8650` commit
`453d8875f815bf9dfad9035982861226a4823a6c`, `synaptics_hbp`; and the matching
`android_kernel_oneplus_sm8650` branch `oneplus/sm8650_b_16.0.0_ace_5`,
`drivers/pinctrl/qcom/pinctrl-pineapple.c` and `drivers/soc/qcom/rpmh-rsc.c`.

The tested touch kernel SHA-256 is
`a2e410a76292ef093d09bb1ceadc9e10d838331d237fa4e11f852d1615369faf`.
For a sustained RAM trial after loading the kernel-capable loader and system image:

```sh
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL run --seconds 0 \
  --touch --kernel out/giulia-kernel/mach.ace5-s3910
```

Use a bounded `--seconds 180` run to collect a log and screenshot automatically.
The last displayed frame remains after a bounded run ends; it no longer accepts
input until another kernel run is started. A physical restart returns to Android.


## Adreno 750 development (2026-10-22)

The Ace 5 driver is `xnu/files/bsd/dev/giulia_adreno.c`. It recognizes the giulia
GIC physical addresses in the loader's DT and blocks the legacy A642L path even
if `gpu=1` or `adreno=N` is accidentally supplied. **GPU acceleration is not
implemented yet.** The existing RedwoodGPU service and Mesa submission path
still contain A6xx assumptions; setting a new chip ID alone is not a port.

The following opt-in probes run with `gpu=0` and restore their owned enable bits:

| `--gpu-probe` | Operation | Evidence |
| --- | --- | --- |
| 1 | Read GCC GPU clocks | GPU AHB/memnoc/DVM controls accessible |
| 2 | Read GPUCC roots, branches and power controls | CX/GX initially collapsed; roots at XO |
| 3 | Enable and restore CX power only | HW_CTRL `0x80000202` acknowledges on; off confirmed |
| 4 | Temporarily enable CX XO clocks; read GMU/SMMU | GMU reset=1; SMMU IDR0=`0x4c017e18`, IDR1=`0x40000016` |
| 5 | Write/read/restore two words in ITCM and DTCM | Both patterns and restored contents verified; CX off=1 |
| 6 | Read SMMU stream table and PLL1 | IDR2=`0x5511`; 24 idle SMRs; 36-bit PA, 4K pages |
| 7 | Parse/stage original GMU firmware and allocate backing memory | 15 blocks; 138 pages in 6 tables audited; TCM readback and restoration pass |
| 8 | Configure SID5 → context bank 2 and tear down | Register readback, TLB sync, stream removal, disabled context and cleared TTBRs pass |
| 9 | Enable PLL1 and operating clock roots | PLL locks; GMU/HUB roots configured for 260/200 MHz; restored to XO after probe |
| 10 | Invoke vendor GPU SCM init, seed CX timer and boot CM3 | FW_INIT_RESULT=`0x100`; no SMMU fault; reset and GBIF drain acknowledged |
| 11 | Start HFI and query GMU warmboot support | Matching GET_VALUE ACK: `0x1040c,0x10030c,1,0` |
| 12 | Send the device-specific performance and bandwidth tables | 13 GX levels, 3 GMU levels and 10 DDR levels acknowledged |
| 13 | Configure RSC/PDC, enable HWSCHED, start core firmware | CORE_FW_START ACK; RSC sleep completes |
| 14 | Start scheduler and service dynamic allocations | 4 bounded scratch, profiling, command and ring buffers acknowledged |
| 15 | Vote stock minimum frequency and request GX via OOB | 231 MHz vote ACK; GX power/clock status 0; RBBM reads; slumber returns status 3 |
| 16 | GPU SID0/CB0, separate page tables and SQE/AQE backing | Register readback and teardown pass; GPU DMA is not yet proven |
| 17 | Experimental Gen7 core registers and SQE CP_ME_INIT | Unverified; initial attempt entered Qualcomm CrashDump; tracing added |

Stages 3–17 require physical CPU 1 and a finite return timer in both the host tool
and kernel. Every wait has a timeout. GX stays collapsed through stage 14; stage
15 briefly powers it at the lowest stock operating point. Stage 17 attempts CP
initialization; no rendering commands are submitted. Firmware is reset and DDR traffic drained before mappings are
removed and memory freed. Stage 15 also completes PREPARE_SLUMBER and RSC sleep,
and checks GX has collapsed before teardown. The 90-second stage-5
run returned normally, captured eight live snapshots,
passed all 36 initial userland checks and reached SpringBoard. Local evidence:
`logs/giulia/20261022-221839-run.log`. Kernel SHA-256:
`713e18285849b3cb9fc834e384a9a409537d3888a253a6ccfeb25ed12d862dea`.

The counter read in stage 4 stays zero while CM3 is in reset. Stage 10 seeds the
gen7-9-x CX timer; reading its seed register is not a measurement of a live timer.
Firmware execution is established by a fresh ready response and HFI ACK.

`giulia_gmu_fw.h` checks lengths, address regions, alignment, metadata ordering
and overlapping payloads before any firmware writes. The host regression test
runs with AddressSanitizer/UBSan and includes 10,000 mutated inputs. Firmware,
the custom APFS image and captured logs remain local and are never committed.

SMMU details matter on this board: CB0/1 are reserved for GPU clients by the
OnePlus driver, so this probe owns idle CB2. SID5 has mask zero. TCR2 has fixed
read-one bits and SCTLR.ASIDPNE reads as zero in this AArch64 context. Replaying
an uninitialized CBAR reset value triggered a synchronous abort during early
testing; teardown now follows the disabled-context model and never restores
that unsupported configuration. MMIO stores use an ordinary source register
to avoid the documented Qualcomm hypervisor WZR emulation issue.

IDR0.ATOSNS is set, so ATS1PR translation tests are unavailable. The software
page-table audit and register readback alone do not prove DMA; the later
successful firmware boot and HFI exchange do. FSR=`0x400` is the address-format
field, with no error bits in `0xc00001fe` set. FAR is undefined without a fault.

```sh
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL run --seconds 90 \
  --bootcpu 1 --gpu-probe 5 --live-log --kernel /absolute/path/to/custom-kernel
python3 tools/ace5/inspect-adreno750-fw.py /local/firmware > /local/firmware-manifest.json
cc -fsanitize=address,undefined -g tools/ace5/test-gmu-fw.c -o /tmp/test-gmu-fw
/tmp/test-gmu-fw /local/firmware/gmu_gen70900.bin
```

To run the firmware probe, copy the small `userland/root` tree to a private
local directory, place the device's `gmu_gen70900.bin` in its `fw/` subdirectory,
then build a separate small APFS image (preserve the baseline image):

```sh
python3 tools/apfs/mkapfs.py -z --plain fw -o /local/root-gmu.apfs redwood=/local/root-gmu
python3 tools/ace5/phone.py --serial ORIGINAL_SERIAL run --seconds 90 \
  --bootcpu 1 --gpu-probe 11 --live-log --touch \
  --kernel /absolute/path/to/custom-kernel --root-image /local/root-gmu.apfs
```

The host reports failure for kernel panic, a failed probe, or a missing completion
marker for the requested stage, even if the loader itself returned normally.
Stage-10 evidence: `logs/giulia/20261022-225227-run.log`. First successful HFI
round trip: `logs/giulia/20261022-225643-run.log`.
The integrated 90-second run, `logs/giulia/20261022-225914-run.log`, repeated
all eleven probe steps, reached SpringBoard, and recorded 23 touch reports
with zero malformed reports. The GMU probe ends before normal userland activity;
this is not 90 seconds of GPU rendering or proof that the desktop stall is fixed.
Stage-11 kernel SHA-256: `33418117a3d0022770b0420eb91e59b8be2974bf7203abab56ebf4d14b525930`.
Its 20-second smoke run passed all stages and 36 userland checks:
`logs/giulia/20261022-230229-run.log`. Local firmware root-image SHA-256:
`0f43929601a91ea4a4ebce4abb2e04f2d8584ea815a928cb589fc4e95658484e`.

Use the full RAM image for a userland run. A probe without the resident system
image can finish its hardware steps and then panic because the small root has no
`/sbin/launchd`; this is not a passing iOS boot.

The extracted firmware remains local: `gen70900_aqe.fw`, `gen70900_sqe.fw`,
`gen70900_zap.mbn`, `gmu_gen70900.bin`. The GMU image has 15 blocks, core version
`0x40020014`, power version `0x40020010`, and HFI version word `0x20060001`.
The inspector validates sizes, alignment, address ranges and overlapping data.
Remaining work includes proving GPU DMA, signed zap/SQE/AQE startup,
command submission, the Mesa kernel interface and sustained desktop stability.
GMU runs at 260 MHz with the bootloader's shared CX/MX votes retained. Stage 15
uses a stock 231 MHz GPU vote; it is not a general frequency-scaling policy.
No GPU rendering service is exposed yet.

The power tables are specific to this PKG110 bin, based on the live Android KGSL
frequency list, device tree, and cmd-db ARC/BCM data. HFI generic ACKs carry status
in word 2; GET_VALUE replies carry data in word 2 and status in word 3. The host
HFI regression test covers 256 ring wrap positions, both ACK formats, malformed
packets, wrong sequences, secure-memory rejection and representative ARC/BCM
vectors, under AddressSanitizer/UBSan.

Cold-boot stage 15 passed in `logs/giulia/20261022-233840-run.log`. Repeating it
without a hardware reboot exposed retained RSC sleep state: DCVS failed with
FW_INIT_RESULT `0x900` and debug text `GmuPwrGxBwVote`. The implementation now
checks the retained sleep bit and performs the vendor wake request/ACK/busy
handshake before releasing CM3. This is distinct from the previously reported
desktop stall; that stall remains unresolved. The corrected kernel passed two
consecutive 20-second runs (`20261022-234329-run.log` and
`20261022-234423-run.log`); the second records the wake handshake and again
completes all 15 stages, 36 base checks and 80 surface checks. Tested kernel
SHA-256: `080f9545a254ad51807e6850571c3910f235238a48e34b3fe6354ac703ad08a9`.

Mesa 25.0.7 was built locally with `softpipe,freedreno` and `AIR=1`, including
`libOSMesa.8.dylib`, `libredwoodair.dylib` and `air_run`. Its device database has
chip ID `0x43051401` (FD750). These builds are not proof of working device-side
GPU acceleration, and have not replaced the baseline userland image.

Stage 16 passed on 2026-10-22: `logs/giulia/20261022-235515-run.log`, kernel
SHA-256 `2280e39429d0804d0d42edcbb90c9d0b970b4aaeba43c9882a67110f76cea9d0`.
GPU SID0 uses CB0 and ASID0, separate from GMU SID5/CB2. Only HFI buffers marked
GFX-accessible are imported, with GPU privileged/write flags preserved. SQE and
AQE backing excludes each firmware's first version DWORD, following KGSL.
Teardown checks GX is off and DDR traffic drained before freeing those mappings.
The local `root-gpu.apfs` adds SQE/AQE/ZAP to the previous GMU root; SHA-256
`f06777891212a5b80c81519ec7c43815126c7089f191819e8328abae08924b6b`.

Enabling AQE additionally requests a 0x37d000-byte nonsecure context record,
then a secure record with flags 0x1007. The driver rejects secure allocations;
it must not satisfy them with normal RAM. Evidence is in the local runs
`20261023-000038-run.log` and `20261023-000206-run.log`. AQE remains disabled
until protected allocation and the secure SID2 domain are implemented. CB1 is
reserved for nonsecure LPAC SID1; it is not the secure GPU domain. The subsequent SQE-only
stage-17 attempt, kernel SHA-256
`a2c90779ef5ac691ccb187955a022e3a85a83e98f0b23f308d2c7e2b077a0115`,
entered Qualcomm USB CrashDump (05c6:900e) before the first live snapshot.
This is a new bring-up failure, distinct from the earlier desktop stall.

The trace build supports `--gpu-step-delay 200 --live-interval 0.1
--live-log` with a sufficiently long finite run (for example 120 seconds).
It pauses before new core register accesses so USB can capture the last
operation.
The host and kernel reject a return timer too short for the inserted delays.
GPU diagnostic lines now go directly to `loader_log_line` as well as `printf`:
the older live snapshots only contained userspace console text, because the
kernel printf buffer was appended at return. A delay alone would not have
preserved the new register trace.

On 2026-10-23, trace kernel SHA-256
`d2769979144f5a7d71078a6314d16650b748324a16a6087351b4a1fdac251cf8`
produced 205 successful live snapshots and a partially transferred 206th before
entering USB CrashDump again. The final local log is
`logs/giulia/20261023-125222-live-206.log`. It passes core register configuration,
the clock-gating handshake, power-list reads and SQE/AQE base writes. The last
captured line is the delay immediately before the first raw CP_ME_INIT submission.
There is no captured HFI send/ACK for that submission, so this bounds the failure
to the submission path but does not prove firmware accepted or executed it.

The next diagnostic build separately traces queue access, payload publication,
doorbell write and notification completion. Publication is traced before changing
the write index because firmware may poll the queue without a doorbell. During
raw-command waits, it also checks owned GPU CB0 and global translation faults and
records syndromes, fault address and TTBR0 before aborting. It does not change
translation fault policy or authenticate ZAP. Host transport tests include raw
commands at 256 wrap positions and rejection of an ACK accompanied by a simulated
GPU translation fault. The follow-up kernel (SHA-256
`0e914b756e9a0383916d8b323e5cad5dd2f8e236cd0f0e4191100365a850406b`)
saved 218 live snapshots in the `20261023-131408` run before CrashDump. The
last log proves the raw queue payload and write index were published. It ends
at the pre-doorbell breadcrumb; there is no notification-complete line or ACK.
Firmware may consume a published command before the doorbell, so this does not
identify the exact faulting instruction or prove CP execution.

The opt-in `--gpu-direct` / `ace5_gpu_direct=1` diagnostic skips HWSCHED feature
enable and uses a host CP ring, following the A750 path in Linux v6.17 and the
user-suggested UniXNU A740 implementation. Stage 16 maps SID0 and SID1 to owned
CB0, SQE plus a 32 KiB ring, a RPTR/fence page and a scratch page; AQE stays off.
Stage 17 programs the ring registers, starts SQE and requires two GPU-written
completion fences plus a CP_MEM_WRITE result of `0xa7500001`. RPTR advance alone
is not success. The probe waits at most 200 ms per fence and halts SQE before
slumber. These are diagnostics, not a persistent rendering backend.

The A750 clock/protection configuration remains from the pinned OnePlus source.
CP_ME_INIT retains its vendor bit 11 to disable DMS, for which no resources are
provided. GPU ring/scratch/SQE mappings are noncacheable with explicit CPU cache
maintenance. The ring is refused if it is not fresh, and all register setup is
read back. ASan/UBSan host tests exercise the actual direct-ring code against a
fake GPU, including missing fences, missing data writes, translation/hardware
faults, register mismatch and stale RPTR. Device results are recorded separately.

Direct-mode stage 16 passed in a 25-second RAM run on 2026-10-23, log
`logs/giulia/20261023-132635-run.log`, kernel SHA-256
`961a9d60bc0d866287fb9a23b20bbc1ec1c502e0a4e382a44211b05c4e6dbc89`.
CORE_FW_START and START returned successfully with zero scheduler allocations.
SID0+1/CB0 setup, GX power, slumber, TLB teardown and CX-off all completed.
This stage intentionally does not start SQE or establish GPU DMA.

Direct-mode stage 17 also passed: `logs/giulia/20261023-132854-run.log`, same
kernel SHA-256. SQE started, RPTR/shadow advanced to 15 and 24, fences 1 and 2
were written by the GPU, and CP_MEM_WRITE produced `0xa7500001` in scratch.
CP hardware/interrupt/protection status were zero; CB0 FSR remained its nonfault
reset value `0x400`. Slumber and teardown completed. This is real CP/GPU DMA
evidence, not yet rendering or a fix for the earlier desktop stall.

`giulia_zap_fw.h` validates the local signed ELF/MDT image. Its ASan/UBSan test
passes the real image, every truncated length and 10,000 header mutations.
`giulia_zap_start.h` implements the next opt-in stages: stage 18 performs PAS13
INIT_IMAGE, MEM_SETUP, reserved-memory copy/readback, and AUTH_AND_RESET; stage
19 sends CP_SET_SECURE_MODE(0), then requires another GPU memory write and fence.
The metadata is checked for physical contiguity. Only the live-DT ZAP carveout
at `0x9b09a000` is written, after successful PAS initialization/setup; its
temporary CPU mapping is removed before authentication. SQE is halted and PAS
SHUTDOWN must succeed before normal GPU teardown. Interrupted SCM calls preserve
the Qualcomm x6 session; bounded BUSY retries are supported. An incomplete
WAITQ/interrupted operation stops the run without freeing possibly owned memory.
Host tests exercise metadata packing, reserved-range bounds, call ordering,
unmapping before authentication and failures at each PAS step. Direct-ring tests
also reject a mode switch after failed authentication and a missing nonsecure
DMA result. Neither firmware nor raw device logs are committed.

ZAP stage 18 passed in `logs/giulia/20261023-133520-run.log`; stage 19
also passed in `logs/giulia/20261023-133645-run.log`, kernel SHA-256
`28283007e1303ef6b1fd8708c28c1bcf52041fc426acec3d6242b0f48c449960`.
All four PAS operations returned status/result zero. After the secure-mode
command, fences 3 and 4 retired at RPTR 31 and 40; GPU scratch[1] became
`0xa7500019`. No CP or translation faults were reported, and both runs
completed full shutdown. These are repeated RAM runs without a physical reboot.

Stage 20 adds an A7xx submission builder and an owned indirect command buffer.
It resets context synchronization state, waits for SRAM initialization, flushes
and invalidates GPU caches, and joins a BV fence before the BR completion.
Both CP threads wrote independent values (`0xa75000b1`, `0xa75000b2`), with
BV/ring fence 5 and RPTR 112, in `logs/giulia/20261023-134117-run.log`. Kernel
SHA-256: `f6626ab7d3c9fdd9bad514eda1b192f4d5877b8db8fabb276c3d58af58a566ef`.
No CP or SMMU fault was recorded; the 75-second run returned and cleaned up.
ASan/UBSan tests decode 1..128-IB submissions, check packet parity and bounds,
and reject completion if either thread fails to write its expected result.
Actual shader rendering and persistent userspace submission remain unverified.

### Persistent A750 and Mesa rendering

The opt-in `--gpu-probe 20 --gpu-direct --gpu-keep` now retains the validated
GPU setup and publishes the existing RedwoodGPU userspace service. A sleeping
owner thread keeps the bring-up resources alive; ordered stop drains commands,
halts SQE, shuts down PAS13, sleeps GMU and releases mappings/clocks. The shared
IOKit buffer/IOSurface interface delegates hardware operations to the A750
backend, so A642L recovery, panic and temperature-register paths are not used.
Dynamic 4 KiB GPU mappings cover `0x70000000..0xff000000`, with explicit cache
maintenance and TLB synchronization. Ring space waits are bounded, and a detected
GPU failure stops new submissions. There is no A750 reset/recovery implementation.

The 60-second run `logs/giulia/20261023-135626-run.log` verified **actual FD750
shader rendering**, kernel SHA-256
`049e9c960dd225f05539384b4aa783177e3ef1ed13cea3a21ea11c9508926ec7`.
Mesa 25.0.7 reported OpenGL ES 3.2 and FD750. `gl_test 0 256 256` retired user
fences 6 and 7, drew in 52 ms and read back in 3 ms, with GL error zero. The
background, center and three vertex samples matched the triangle; 17,086 of
65,536 pixels were covered (26%, as expected). Full GPU shutdown also succeeded.
The root image SHA-256 was
`d103ca24a3bd7de3072277b5a3ffcad084568bc86b1149928b28a03bc083a673`.
This supersedes the earlier unverified-rendering status; it does not by itself
establish desktop stability.

The live-backend ASan/UBSan test checks mapping permissions and bounds, TLB
commits, all 8,192 ring wrap positions, a full-ring timeout and fence wrap.
Rendering currently uses the fixed 231 MHz operating point, serialized shared
address space and a context-state reset per submission. AQE, protected contexts,
preemption, per-client address spaces, DVFS and A750 recovery are not implemented.
Metal/AIR was not included in this initial triangle root; subsequent tests are described below.

Build the Mesa components on the configured Linux ARM64 builder, after building
Mesa with the userland repository's `tools/mk-mesa.sh`:

```sh
THEOS=/path/to/theos tools/ace5/build-gpu-userland.sh /path/to/components
```

Create a new root directory from the firmware-bearing `root-gpu` baseline,
preserving its symlinks. On the macOS host (the Colima shared mount cannot create
all these symlinks), copy `components/libOSMesa.8.dylib` and
`components/libredwood-gl.dylib` into its `lib/`, and `components/gl_test` into
its `bin/`. The existing injection library loads the EAGL bridge when RedwoodGPU
is present. The bridge redirects CoreAnimation's OpenGL ES contexts to Mesa.
Use this `etc/rc.jobs` in the small root for a readback test alongside the desktop:

```text
/redwood/bin/hello_dyn A750 accelerated desktop
/redwood/bin/svc_test
/redwood/bin/rwtouch wait 10
/redwood/bin/gl_test 0 256 256
/redwood/bin/hello_dyn A750 desktop render check finished
```

Build on the host and check on Linux (requires zlib and OpenSSL development
headers for the independent APFS reader):

```sh
python3 tools/apfs/mkapfs.py -z --plain fw -o /path/to/root-desktop.apfs redwood=/path/to/root-desktop
tools/apfs/check-image.sh /path/to/root-desktop.apfs redwood=/path/to/root-desktop
```

Use a finite RAM run with live logs for device validation:

```sh
python3 tools/ace5/phone.py --serial DEVICE_SERIAL run --seconds 300 --bootcpu 1 \
  --gpu-probe 20 --gpu-direct --gpu-keep --touch --live-log --live-interval 1 \
  --kernel /path/to/mach.ace5-adreno750-desktop --root-image /path/to/root-desktop.apfs
```

Neither firmware, Apple binaries, root images nor raw device logs are committed.

The first interactive desktop trial used kernel SHA-256
`d2eb13d7c6155277e12b4d29311cfd3bf4e0810f42ab946bcc45eba89c2414b5`
and root SHA-256
`eb22b63f7a0d45580eea420686cc543ba824f36a13ea3b915f7e363b2cc42708`.
CoreAnimation created a Mesa FD750 context, imported IOSurfaces and rendered
hundreds of frames. The concurrent triangle readback again passed. At the user's
request the 300-second trial was stopped after approximately 207 seconds and
restarted; log `logs/giulia/20261023-140428-run.log`. The final GPU fence 821
retired, CP/SMMU fault status was clear, and full teardown succeeded.
**The user reported severe lag; this is not a successful stability/performance
validation.** Several frame records exceed 100 ms. The final screen was
`Setting Language…`; the relation of that state to the previous desktop stall is
not established. `--render-trace` enables per-frame timing, GL calls exceeding
20 ms for follow-up trials. It no longer enables the very high-volume SpringBoard
main-loop trace (`touch_lat=1`), which can fill the live-log buffer by itself. GPU workload-time statistics are
currently unavailable on this backend (the common report shows zero samples),
so they must not be read as zero GPU activity.


### Metal and UI retry-loop diagnosis (2026-10-23)

The shared A750 backend also runs the existing RedwoodMetal/AIR path. The first
90-second Metal run (`20261023-142133-run.log`) completed `MetalFaceMask_InitMinMax` and `clearFragment20`. The fragment
output matched the software reference: `2458616957 2930221209 3401825461 3873429713`.
The later full suite `20261023-143209-run.log` used standard system Metal discovery
through `RedwoodMetal.bundle`, rather than registering a second device manually.
Nine bounded compute kernels completed with status 4; indexed drawing, textured
vertex/fragment drawing and Depth32Float, Depth16Unorm and Depth32Float_Stencil8
readback/compare tests executed on FD750. Stencil8 alone was refused. This is an
experimental Metal subset: MetalFX, MSAA/resolve, binary archives and general
application compatibility are not established.

Two harness defects were exposed: the index test judged pixels on the shared
edge rather than the two triangle interiors, and `metal_caps` queried linear
texture alignment for a depth format, which Metal explicitly rejects. The tests
now sample `(0,3)` and `(3,0)` and query only legal color formats. A separate
`triangle_app --layer` test uses `CAMetalLayer.nextDrawable` and checks every pixel
through both IOSurface and Metal readback: two 1125 x 2436 frames, 2,740,500 pixels
per frame, with zero incorrect pixels. Presentation in an ordinary UIKit
application was tested separately below.

The repeated desktop/Wi-Fi stall was accompanied by SpringBoard's main thread in
`Sleep.framework`'s `HKSPXPCConnectionProvider._retryPendingMessages` and failed
lookups of `com.apple.sleepd.sleepserver`. Both `sleepd` and `configd` were absent
from the small root's job list although their binaries were already in the
system image. Adding their stock job definitions eliminated the sleepd lookup
loop in the 150-second run `20261023-142744-run.log`; late CPU samples were 1%
busy. The previous tracing run was continuously 100% busy, although its extra
main-loop logging is a confounder. A subsequent 180-second automated run
`20261023-143531-run.log` opened Settings, entered Wi-Fi, went back and scrolled.
Screenshots at `20261023-143445-screen.png` and `20261023-143532-screen.png` show
the Wi-Fi page and the returned, scrolled Settings list. This fixes a reproduced
service omission; sustained interactive performance still requires validation.

A matched control (`20261023-144554-run.log`) removed only those two job files,
using the same gestures, kernel and logging flags as the fixed run. It reproduced
the stuck highlighted Wi-Fi row and sustained 100% CPU usage. The fixed image
entered Wi-Fi and returned to Settings. This rules out the earlier extra logging
as the sole explanation for the improvement.

`--gpu-cached` selects the new experimental `gpu_cached=2` mode. It cleans cached
buffers before submission, waits for completion, and invalidates ordinary buffers
before returning to Mesa. Waiting/invalidation at CPU_PREP alone is insufficient:
Mesa can resolve a non-shared buffer's fence without entering the kernel. This
mode serializes ownership deliberately and is **not the default**. The legacy
`gpu_cached=1` experiment does not have this guarantee.

The paired 150-second runs `20261023-143851-run.log` (write-combined) and
`20261023-144209-run.log` (cached/synchronous) used identical test roots. All nine
compute dispatches, color/index/depth checks and the two CAMetalLayer frames
completed again. For a 1080 x 2400 BGRA8 texture, median readback was 195.8 ms vs
5.0 ms; a separate untouched-frame read was 193.3 ms vs 1.0 ms. Upload remained
slow (136.9 vs 132.1 ms). Sampled desktop drawing increased from about 1.3 ms to
6.3 ms, so the default stays write-combined. These are diagnostic measurements,
not end-to-end game or desktop frame rates. The harness's frame-rate extrapolation
also assumes a synchronous commit; its observed nonzero wait time makes that
extrapolation unsuitable for performance claims. A750 hardware workload counters
remain unavailable. Kernel SHA-256 for the opt-in cache implementation:
`8388395ad559794649f343fb8704a9627b48ac6cd767382f1054f1eb65387c7f`.

Reproduce the local userland build and pack the small root without altering the
firmware-bearing baseline or the 2.5 GB system image:

```sh
THEOS=/path/to/theos tools/ace5/build-gpu-userland.sh /path/to/components --metal
python3 tools/ace5/prepare-gpu-root.py --base /path/to/root-gpu \
  --components /path/to/components --ios-root ios165/rootfs \
  --output /path/to/root-metal --metal
python3 tools/apfs/mkapfs.py -z --plain fw -o /path/to/root-metal.apfs redwood=/path/to/root-metal
tools/apfs/check-image.sh /path/to/root-metal.apfs redwood=/path/to/root-metal
```

The build checks the AIR source hash to avoid mixing internal ABIs. The preparer
preserves symlinks, installs Mesa/Metal components, copies the two required local
Apple shader libraries, and adds sleepd/configd jobs (and PosterBoard when
`--wallpaper` is selected). No downloads or device
writes occur. The existing system image must contain the
`/System/Library/Extensions/RedwoodMetal.bundle -> /redwood/lib/RedwoodMetal.bundle`
link (verified on this device). Pass `--metal` alongside `--gpu-keep` to allow the
IOKit discovery properties; the jobs recipe activates them with `metal_props on`.
The desktop window server continues to use OpenGL ES. `--thread-trace Preferences`
or `--thread-trace SpringBoard` captures kernel-side user stacks every five
seconds without the main-loop log flood.

For a real UIKit application, install `RedwoodTriangle.app` as a normal directory
inside the RAM system image, following `userland/build.sh`. The temporary bindfs
union approach was rejected: its POSIX directory check saw both Triangle and
Preferences, but LaunchServices did not register the stock applications. A later
screenshot showed only Triangle and waiting widgets, and the Settings URL was
refused. The directory check was insufficient; neither that URL refusal nor
clicks at the usual Settings icon position established UI navigation. The union
helper and its entitlement were removed.

`prepare-gpu-system.py` stages a new minimal system tree from the existing local
iOS extraction, stage0 and signed Triangle binary. It preserves the stock apps,
uses hard links for unchanged source files, and excludes symbolication caches
from the dyld payload. It refuses workspaces with optional audio/bootstrap/app
content rather than silently dropping those additions. Use `userland/build.sh`
for such workspaces. The original system image is not overwritten:

```sh
python3 tools/ace5/prepare-gpu-system.py --ios ios165 \
  --stage0 userland/obj/stage0 --triangle /path/to/components/triangle_app \
  --output /path/to/system-metal
python3 tools/apfs/mkapfs.py -z -o /path/to/system-metal.apfs \
  System=/path/to/system-metal,manifest=ios165/system.manifest
tools/apfs/check-image.sh /path/to/system-metal.apfs \
  System=/path/to/system-metal,manifest=ios165/system.manifest
python3 tools/ace5/phone.py --serial DEVICE_SERIAL ramdisk \
  --image /path/to/system-metal.apfs
```

The small-root preparer's `--app` option includes only the signed application
launch helper. It neither mounts over `/Applications` nor installs the bundle
there. `rc.jobs.metalapp` and `rc.jobs.metalstress` require the normal bundle in
the uploaded system image.

The corrected local system image is 2,492,227,584 bytes, SHA-256
`566c29eaa11d8e38860d8ba0dc4f4f996a29294561d6deb044a1f5f834dc9b47`.
The independent APFS checker read back all 62,359 entries with matching contents;
the staged tree contains 174 normal application directories and 43 dyld cache
parts. Its RAM upload took 78.410 seconds. The original `userland/system.apfs`
remains SHA-256
`8d27af4e95bb4dcb887d4034058b63fc298fc7125222b4599e3509a46c53cc0f`.
Returning to Android loses the RAM upload; upload the chosen system image again
before the next iOS run.

In `20261023-145230-run.log`, SpringBoard launched RedwoodTriangle as uid 501,
Metal discovered the A750 device normally, and repeated drawable presents ran
without a reported failure. `20261023-145231-screen.png` shows its orange/blue
triangle filling the physical panel. This application's drawable is 375 x 812;
the separate bare-layer test covers 1125 x 2436. Its timer deliberately draws at
2 Hz to test drawable-pool reuse, so neither the frame count nor that screenshot
is an application performance benchmark.

The complete AppleMetalGLRenderer fragment sweep contains 271 shaders: 263
finished, six multisample textures were unsupported, and two depth-writing
shaders lacked a depth attachment in this sweep's render pass. The separate
depth tests above cover depth attachments. `tools/air/metalcheck.py`, with a
native Linux ARM64 `tools/air/mk-host.sh` build of the same AIR source, compared
258 of the GPU results with softpipe: all agreed. Of the five drawn but
uncompared cases, one has no color output, three write attachment 3 outside the
harness's bound attachments 0–2, and one is refused by softpipe. Eight other
cases never drew. These are not 271 successful comparisons.

On the compute suite, four results strongly agreed with softpipe, one wrote
nothing on either backend, and four could not be checked because softpipe
refused their NIR. All nine completed on the A750. These comparisons share the
AIR frontend and cannot detect errors common to it. Separately,
`tools/air/cpuref.py --all --draw` on AppleMetalGLRenderer ran the shader modules
through LLVM independently: 255 agreed, zero differed, and 19 were outside that
harness (the library has 274 total functions, including vertices). The 255
comparable modules contain plain LLVM IR and no AIR intrinsics; all 255 were
also drawn by softpipe with matching pixels. This does not establish arbitrary
shader or game compatibility.

`tools/ace5/check-metal-log.py RUN.log` checks the bounded suite's positive
readbacks. `--stress` checks the distinct draw count, mobile application launch,
600 successful application frames and completion markers for
`rc.jobs.metalstress`; screenshots are still needed to establish UI navigation.
The stress recipe checks the Settings URL both before and after the application,
then uses touch gestures to return and scroll. A successful command marker alone
is insufficient; the corresponding screenshots must show the intended pages.

The 600-second run `20261023-150836-run.log` completed all 600 application frames
with zero failures, and GPU fence 3038 retired before clean power-down. It used
the original bilinear display path; its final Settings URL request was refused,
so it validates the long application run, not that navigation step.

The corrected normal-installation run `20261023-154423-run.log` used the rebuilt
kernel, the new system image and nearest-neighbor display scaling for 600 seconds.
All 15 stress-log checks passed: 263 distinct fragment draws, both full-resolution
bare-layer frames, and 600 UIKit application frames with zero readback failures.
SpringBoard accepted the Settings URL both before and after the Metal application.
`20261023-154157-screen.png` shows the post-application Wi-Fi page;
`20261023-154424-screen.png` shows the returned and scrolled Settings list.
The final GPU fence was 4648/4648, and the GPU powered down cleanly. This run
validates the normal application installation and that specific navigation
sequence; it does not establish general application compatibility or an FPS target.

### Display scaling cost and the fast option

Hardware rendering did not remove the CPU display scaler. The iOS display is
1125 x 2436 and the Ace 5 panel is 1264 x 2780. On this single-core configuration,
the CPU's bilinear scaling and framebuffer write dominated otherwise short GPU
draws. `phone.py --fb-filter nearest` selects the existing nearest-neighbor
scaler. It does not lower the iOS render resolution, and it trades edge
smoothness for speed. Omitting the option retains bilinear; `--fb-filter bilinear`
selects it explicitly.

The same Settings root, touch recipe and logging settings produced these
180-second trials. Each value weights the driver's reported window averages by
the number of displayed frames in that window; it is not a whole-system FPS.

| Display path | Average time in display driver | Evidence |
| --- | ---: | --- |
| Bilinear | 40.405 ms | `20261023-143531-run.log` |
| Bilinear with cached row staging | 41.847 ms | `20261023-151151-run.log` |
| Nearest neighbor | 13.777 ms | `20261023-151636-run.log` |

The cached-row experiment did not help and its source change was reverted.
Nearest neighbor reduced the measured display work by 66%; the final scrolling
window's median interval fell from 51 to 34 ms and its reported 95th percentile
from at least 65 to 42 ms. Other windows still stall longer, and neither these
measurements nor the 2 Hz Metal application establish sustained 60 FPS.
`20261023-151637-screen.png` shows the returned and scrolled Settings page. Text
remains legible, with the expected difference in edge filtering. Native GPU
scaling was still missing at this stage; the later display work below replaces it.

For an interactive root without automatic gestures or benchmark launches, use
`--metal --jobs tools/ace5/rc.jobs.desktop` with the root preparer. It waits
60 seconds for the validated OpenGL ES window server startup, then enables Metal
discovery for subsequently launched applications. The normal Triangle bundle
is optional for this recipe. A locally prepared interactive root can be booted with:

```sh
python3 tools/ace5/phone.py --serial DEVICE_SERIAL run --seconds 600 \
  --bootcpu 1 --gpu-probe 20 --gpu-direct --gpu-keep --metal \
  --fb-filter nearest --touch --live-log \
  --kernel /absolute/path/to/mach.ace5-adreno750-metal \
  --root-image /absolute/path/to/root-metal-daily.apfs
```

The rebuilt kernel for this configuration has SHA-256
`4f86be680e53ceb23ea42c2611a147d97ab775ce4859d0c511b3242f9bbf420e`.
The loader remains the guarded RAM-boot image. The original system image is
preserved; the additional system image with Triangle is uploaded only to RAM.
GPU buffer caching remains opt-in; the command above uses the default
write-combined buffers.

The ordinary interactive root, SHA-256
`ea895e5351310872291ba6164d958b05d991e6848ba2ca0849b31fc69d9f44ef`,
passed a 120-second startup check in `20261023-154655-run.log`. The desktop used
FD750, the Metal bundle was discovered, fence 845/845 retired and teardown was
clean. `20261023-154656-screen.png` shows the stock application icons including
Settings; the Weather and Calendar widgets remain blank. This recipe runs no
automatic app launches or touch gestures.

### GPU display scaling and clearing (2026-10-23)

`--gpu-display --fb-filter bilinear` sends the final BGRA IOSurface through the
A750 2D engine directly into the firmware panel framebuffer. CoreAnimation still
uses its accelerated OpenGL ES compositor through Mesa FD750; Metal applications
submit CAMetalLayer surfaces into that compositor. The framebuffer presenter waits
for rendering, then submits the hardware scaler and waits for its fence before
reusing the command buffer. It no longer calls the CPU pixel scaler on success.
The same engine clears the panel when the bottom display layer is removed.

`RedwoodGPUDisplay.h` owns a serialized command buffer and eight retained surface
mappings, separately from ordinary client submissions. It only accepts the A750v2
chip ID and bounded, linear, 64-byte-aligned BGRA resources. Unsupported layouts
retain the CPU fallback and log that event. Panel mapping comes from the kernel's
existing framebuffer; no physical address is accepted from the diagnostic client.
The command sequence follows Mesa 25.0.7's A7xx 2D blitter, including the A7xx
source pitch encoding and A750 CCU layout. Power-off precedes resource release.

The SMMU walker now caches the existing L3 table pointer for each 2 MiB GPU VA
region. Previously every mapped 4 KiB page searched all allocated tables and
translated each table address again. The 16 KiB pointer cache lasts as long as
the address space; unmapping still clears and flushes PTEs and synchronizes the
TLB. In the desktop trial, active IOSurface import windows averaged 1.3–1.7 ms per
surface, compared with 65–131 ms in the user's earlier session. These are
different workloads, not an isolated benchmark of identical imports.

The 180-second desktop trial (`20261023-195224-run.log`) recorded at least 1,080
GPU display layers and zero CPU fallbacks. All seven copy/scale readbacks passed;
fence 7,272/7,272 retired and teardown completed. The Settings screenshot
`20261023-195146-screen.png` shows correct channel order, geometry and text.
The display driver's frame-weighted mean was **1.764 ms** across its reporting
windows, versus the earlier **40.405 ms** CPU bilinear path and **13.777 ms** CPU
nearest-neighbor path. These figures measure display-driver work, not end-to-end
latency or FPS; idle gaps and CPU application work remain outside that comparison.

The 600-second GPU display/Metal run (`20261023-200417-run.log`) then passed all
27 log checks: eight display readbacks, 263 completed fragment shader draws,
full-frame drawable verification, 600 UIKit Metal frames without errors, and
Settings navigation before and after the application. It recorded at least
4,440 hardware display layers, zero CPU display/clear fallbacks, zero CPU frame
readback copies in the compositor, and fence 27,777/27,777 at clean shutdown.
The display driver's weighted mean was 1.779 ms. Screenshots
`20261023-195923-screen.png` and `20261023-200320-screen.png` show the Metal
application and the scrolled Settings page after it. The 2 Hz test app cadence
is deliberate and is not a throughput benchmark.

Validated kernel SHA-256:
`220914fe9df32c20f37d5c469b09de0cf35939058772462aedd6d9c8517e9489`.
Stress root SHA-256:
`158d8bf2223e592a4b7fd830825a5c9452d719e8e67b96876393a7dfc27b6b40`.

`display_test` independently checks the entire destination image and row padding
for identity, 2x nearest/bilinear, cropped nearest/bilinear, native panel-ratio
nearest/bilinear, and cropped black clear. Untouched pixels must retain their
sentinel value. Nearest sampling uses the observed eight-bit fractional coordinate
quantization and exact colors; bilinear permits at most one level per channel.
The test does not write its reference image into the GPU destination.
The final interactive boot (`20261023-200452-live-008.log`) repeated all eight
cases with odd-width 63x47 cropped destinations and exact checks of untouched
pixels and row padding; all passed. Its root hash is
`384ee60a32547fd4badac5e98545c4cf75fb96295f83b04dfeaa75ab5c22a6fa`.

Build the diagnostic with `tools/ace5/build-gpu-userland.sh`; include it using
`prepare-gpu-root.py --display-test`. For the sustained suite, also use `--metal
--app --jobs tools/ace5/rc.jobs.gpudisplay` and the normal system image containing
Triangle, then run:

```sh
python3 tools/ace5/phone.py --serial DEVICE_SERIAL run --seconds 600 \
  --bootcpu 1 --gpu-probe 20 --gpu-direct --gpu-keep --metal \
  --gpu-display --fb-filter bilinear --touch --live-log \
  --kernel /absolute/path/to/mach.ace5-adreno750-display-clear \
  --root-image /absolute/path/to/root-display-stress.apfs
python3 tools/ace5/check-metal-log.py --gpu-display /path/to/returned-run.log
```

An ordinary `rc.jobs.desktop` root is compatible with the new kernel and GPU
display flag; the diagnostic is only required for the test recipe. This remains
a RAM boot. GPU frequency is still fixed at 231 MHz, XNU uses one little CPU core,
and the panel still scans the firmware framebuffer rather than a page-flipped
native display queue. General planar/video formats, clipped off-panel rectangles,
MSAA and full Metal compatibility are not established by these BGRA UI tests.

### Stock wallpaper on the single-core RAM boot (2026-10-23)

The minimal daily root omitted `com.apple.PosterBoard`, which is supplied by
iOS under `System/Library/LaunchAngels`. SpringBoard logged both a missing
`com.apple.posterboardservices.services` endpoint and "Could not find job with
label com.apple.PosterBoard". The wallpaper assets themselves were present.
Explicit job selection in `userland/mkjobs.py` now also looks in LaunchAngels;
`prepare-gpu-root.py --wallpaper` adds just this job.

Adding PosterBoard alone restored the stock blue gradient in
`20261023-201813-screen.png`, but CollectionsPoster could not decode its HEIC
image layers without `com.apple.coremedia.decompressionsession`. Enabling
mediaserverd exposed a synchronous NanoRegistry dependency: its main thread
waited in `ADASPreferenceStore initNPSDomain` / `NRRegistryClient
syncGrabRegistryWithReadBlock`, while SpringBoard waited in
`FigSystemControllerRemoteCreate`. Adding nanoregistryd cleared that wait.
The decoder then ran in `VCPHEVC.videocodec` / `VTTileDecompressionSessionDecodeTile`,
while wallpaper extensions repeatedly exceeded launchd's five-second check-in
limit. This did not establish a working general HEIC or hardware-video path.

`prepare-wallpaper-system.py` instead stages the existing system tree with the
15 stock iOS 16 wallpaper image layers converted to PNG on macOS. It retains the
original HEIC files, validates dimensions, alpha presence and color profile, and
changes only image references in the stock CAML documents. Layer geometry,
filters and animations remain intact. It replaces edited hard-link entries
before writing, preserving the original system tree. The default wallpaper can
then load without starting the media/audio/watch services solely to decode it.
This is image preparation, not HEVC hardware acceleration or a replacement of
the GPU compositor. Other wallpaper collections are not converted.

```sh
python3 tools/ace5/prepare-wallpaper-system.py \
  --base /path/to/system-metal-normal --output /path/to/system-wallpaper-png
python3 tools/apfs/mkapfs.py -z -o /path/to/system-wallpaper-png.apfs \
  System=/path/to/system-wallpaper-png,manifest=ios165/system.manifest
tools/apfs/check-image.sh /path/to/system-wallpaper-png.apfs \
  System=/path/to/system-wallpaper-png,manifest=ios165/system.manifest
# Prepare a fresh small root using --wallpaper and the usual GPU options.
# Upload the new system image to the guarded loader's RAM, then run that root.
```

Validation: all 15 decoded layers matched the original HEIC images exactly when
rendered to sRGB RGBA (maximum channel difference zero). Original HEIC hashes
were unchanged, and parsed CAML differed only in image source suffixes. The new
system passed apfsck and readback comparison of all 62,374 entries. Its SHA-256 is
`cc31bc2db1028e440f2a0eff82620cf6c7614e5f140865c6a6330fd17d631864`.

The 210-second device run `20261023-204406-run.log` passed all eight GPU scaler
readbacks, completed lock/unlock navigation, and recorded at least 2,520 GPU
display layers with zero CPU display/clear fallbacks. The compositor drew in
place with FD750, and fence 11,837/11,837 retired before clean GPU shutdown.
CollectionsPoster checked in after approximately 0.94 and 2.45 seconds for its
two instances; neither hit the five-second timeout. Screenshots
`20261023-204256-screen.png` (lock screen) and `20261023-204345-screen.png`
(application switcher) show the complete wallpaper and its blurred backdrop.
The daily root uses no automated gestures and has SHA-256
`92ead5a074e46771ea684f475f0b95ea0eceea83e7019a2d233f8a82e421f94b`.
Its fresh boot also shows the complete home wallpaper in
`20261023-204650-screen.png`, after all eight display readbacks passed again.
This changes only the RAM system/root images; no system partition was flashed.

### Desktop command submission on one CPU (2026-10-23)

The accelerated desktop still stuttered more than Settings scrolling. During
repeated home-page swipes, CPU 1 was typically 92–96% busy. A backboardd sample
resolved to `st_glFlush -> tc_flush -> _tc_sync -> util_queue_fence_wait`, while
Mesa's separate `gdrv` thread was runnable or constructing A7xx draws. Even a
no-op GEM_MADVISE call occasionally measured tens of milliseconds of wall time.
These observations implicate CPU scheduling/command generation, not just GPU
execution time. The original 231 MHz operating point was not changed.

`prepare-gpu-root.py --compositor-threading inline` sets `GALLIUM_THREAD=false`
only in backboardd's launch job. Mesa generates the compositor's GPU commands
on the calling thread, avoiding the extra driver-thread handoff on the current
single-CPU boot. GPU execution remains asynchronous and uses the same A750
driver, hardware scaler, full-resolution IOSurfaces, wallpaper and shaders.
Metal application processes retain their existing configuration. Use
`--compositor-threading auto` to remove the override; omitting the option
preserves the base root's policy.

The paired 220-second runs used `rc.jobs.uiperf`: warm-up, 24 alternating home
swipes, Settings scrolling, eight scaler readbacks, 271 fragment-shader inputs
and two full-resolution Metal layer frames. Every recipe command stays within
init's 63-argument limit. Root trees differed only in the one environment value;
kernel, Mesa/Metal libraries and all trace flags were identical. Timing snapshots
must be recovered before analysis because the loader's console tail rolls over.

| Scene / run | Compositor frames/s | Compositor intervals >25 ms | Panel-buffer updates/s | Panel-buffer intervals >25 ms |
| --- | ---: | ---: | ---: | ---: |
| Desktop, default threading (`211235` snapshots) | 51.33 | 18.22% | 51.29 | 15.10% |
| Desktop, inline (`210814` snapshots) | 54.29 | 8.77% | 54.30 | 9.12% |
| Desktop, inline repeat (`211656` snapshots) | 53.34 | 9.89% | 53.35 | 10.61% |
| Settings, default threading | 55.07 | 8.77% | 55.06 | 9.01% |
| Settings, inline | 54.20 | 10.60% | 54.18 | 10.76% |
| Settings, inline repeat | 53.32 | 12.57% | 53.30 | 12.58% |

These are moving-scene measurements, including the fixed pauses between swipes,
not whole-boot averages. Panel-buffer completion is measured after the hardware
blit; it is not native DPU page flipping or an optical measurement of the scan.
The principal improvement is fewer long desktop frames. Settings showed a small
regression (about 1–2 updates/s, with a higher long-frame fraction), so this is a
desktop-focused tradeoff rather than an across-the-board speedup. No run
establishes a sustained 60 fps. More CPU cores and native display integration
remain separate work.

The first inline run (`20261023-211156-run.log`) passed all eight pixel readbacks,
263 distinct fragment draws (the existing two pipeline and six texture exclusions
remained), and both Metal layer frames without failures. At least 4,320 display
layers used the GPU with no CPU fallback or compositor frame copies. Fence
27,584/27,584 retired and the GPU shut down cleanly. Cached read-only command
buffers and forced sysmem rendering were also tried, showed no stable improvement,
and were not included in the daily configuration.
The repeat (`20261023-212038-run.log`) passed the same checks, displayed at least
4,080 GPU layers without fallback, and drained fence 26,100/26,100 before clean
shutdown.

```sh
# Add to the usual prepare-gpu-root.py invocation:
#   --compositor-threading inline --jobs tools/ace5/rc.jobs.uiperf
# Add to phone.py run for diagnostic comparisons:
#   --render-trace --frame-timing --live-log
python3 tools/ace5/summarize-ui-log.py --snapshots \
  --merged-output /path/to/recovered.log logs/giulia/RUN-live-*.log
python3 tools/frames.py /path/to/recovered.log
```

The summarizer requires exact overlap between snapshots, complete consecutive
frame records and both scene markers. It rejects missing evidence instead of
reporting a frame rate from a partial console tail. Firmware, raw images and
device logs stay local; this remains a RAM-only change.

The daily small root retains the original no-gesture recipe and differs from the
previous wallpaper daily root only in backboardd's environment. Its SHA-256 is
`824e49850cbde94d29c87ba684f5deb1aecd1ccf086717a74a0cca9a48ff46dc`.
Use the validated display kernel `mach.ace5-adreno750-display-clear` and the
existing `system-wallpaper-png.apfs`; neither needs replacement for this policy.

Reference sources:

- [UniXNU native Adreno overview](https://github.com/UniXNU/UniXNU/blob/245aca1a9ec5b730b1cad80fade295800d2981f1/docs/drivers/native-adreno.md), with [NativeAdreno](https://github.com/UniXNU/NativeAdreno/tree/b686b9054f57d393c80195b82568a09f8ca106a2), [sheng GPU bring-up](https://github.com/UniXNU/UniXNU-sheng/blob/251b291adf941b9953fe3ae66a808b9b626e164d/package/GpuPowerProbe.cpp), [AdrenoFirmware](https://github.com/UniXNU/AdrenoFirmware/tree/40fdb8ea87606612ac6f1e80bbb49955d0ce4051), and [ArmSmmu4K](https://github.com/UniXNU/ArmSmmu4K/tree/4d0d6598b1a36aeec3de01091893fa6660e49388). A740 results do not establish working A750 acceleration.
- [Linux v6.17 A7xx CP initialization](https://github.com/torvalds/linux/blob/v6.17/drivers/gpu/drm/msm/adreno/a6xx_gpu.c) and [SM8650 GPU SID0/SID1 bindings](https://github.com/torvalds/linux/blob/v6.17/arch/arm64/boot/dts/qcom/sm8650.dtsi).
- [OnePlus graphics kernel](https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8650/tree/453d8875f815bf9dfad9035982861226a4823a6c/vendor/qcom/opensource/graphics-kernel), especially `adreno-gpulist.h`, `adreno_gen7_gmu.c`, `adreno_gen7_hwsched.c`, `gen7_reg.h`.
- [OnePlus pineapple GPUCC](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8650/blob/oneplus/sm8650_b_16.0.0_ace_5/drivers/clk/qcom/gpucc-pineapple.c) and the matching GCC/GDSC source.
- [OnePlus SMMU implementation](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8650/blob/oneplus/sm8650_b_16.0.0_ace_5/drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c), including context allocation and DT ACTLR configuration.
- [Linux SMMU register programming](https://github.com/torvalds/linux/blob/v6.1/drivers/iommu/arm/arm-smmu/arm-smmu.c), including the Qualcomm MMIO store quirk and ATOSNS handling.
- [Linux GIC redistributor traversal](https://github.com/torvalds/linux/blob/master/drivers/irqchip/irq-gic-v3.c), which honors an explicit DT stride before inferring it from VLPIS.
- [Linux MDT loader](https://github.com/torvalds/linux/blob/v6.6/drivers/soc/qcom/mdt_loader.c), for packed ELF/hash metadata and relocatable signed images.

### Earlier reproduced desktop stall (2026-10-22)

The user reported the frozen desktop during a CPU 1 run with `ace5_touch=1`,
`gpu=0`, and a 600-second limit. Live USB snapshots were saved. A host stop ended
the run early and returned 547,994 kernel-log bytes successfully. Local evidence:
`logs/giulia/20261022-221318-desktop-freeze.log` and
`logs/giulia/20261022-221317-screen.png`.

The S3910 continued reporting (112 reports, zero bad reports in the recovered
log), backboardd received touch events, other processes emitted logs, and the
framebuffer driver reported approximately 24 submissions/second, roughly 39 ms
per driver operation. The screenshot's desktop clock remained at 10:11 while
later logs reached 22:13. This narrows the symptom to desktop/render/event
progress rather than a total kernel halt. That initial capture did not establish
a root cause or prove that GPU acceleration alone would fix it; the later matched
service-omission experiment and navigation results are documented above.
