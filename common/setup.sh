#!/bin/bash
# Public Ace 5 snapshot: kernel and userland already share this Git tree.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f RELEASE.json ] && [ -f userland/build.sh ] || { echo 'Incomplete source checkout' >&2; exit 1; }
echo 'Ace 5 sources are bundled; setup does not fetch or update repositories.'
missing=0
for tool in python3 git curl clang lld-link llvm-objcopy make nasm adb fastboot; do
    if ! command -v "$tool" >/dev/null 2>&1; then echo "Missing build/host tool: $tool"; missing=1; fi
done
[ -d "${THEOS:-$HOME/theos}/sdks/iPhoneOS16.5.sdk" ] || echo 'Set THEOS to your local toolchain with iPhoneOS16.5.sdk.'
echo 'Read docs/BUILD.zh-CN.md and docs/UEFI.zh-CN.md for the separate build/USB hosts.'
exit "$missing"
