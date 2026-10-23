#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
STAGE=${1:-diagnostic}
case "$STAGE" in
  diagnostic) OUT=out/giulia; DEFINES=(-DHACKIOS_DIAGNOSTIC=1) ;;
  kernel) OUT=out/giulia-kernel; DEFINES=() ;;
  *) echo 'Usage: build-loader.sh [diagnostic|kernel]' >&2; exit 2 ;;
esac
mkdir -p "$OUT"
"${CC:-clang}" --target=aarch64-unknown-windows -ffreestanding -fno-builtin \
  -fno-stack-protector -fshort-wchar -mgeneral-regs-only -O1 -Wall -Wextra \
  -Wno-unused-parameter -DHACKIOS_GIULIA=1 ${DEFINES[@]+"${DEFINES[@]}"} \
  -DHACKIOS_STORAGE_WRITES=0 -DPAYLOAD_BASE=0xA8000000ULL \
  -c xnuloader/loader.c -o "$OUT/loader.obj"
"${LLD_LINK:-lld-link}" /subsystem:efi_application /entry:efi_main /nodefaultlib \
  /base:0xB8000000 /timestamp:0 /out:"$OUT/loader.efi" "$OUT/loader.obj"
python3 xnuloader/mkpayload.py -o "$OUT/usb-payload.bin" "loader.efi=$OUT/loader.efi" --usb
python3 - "$OUT" "$STAGE" <<'PY'
import hashlib, json, pathlib, sys
p = pathlib.Path(sys.argv[1])
(p / 'loader-build.json').write_text(json.dumps({
    'stage': sys.argv[2], 'kernel_execution': sys.argv[2] == 'kernel',
    'storage_writes': False,
    'loader_sha256': hashlib.sha256((p / 'loader.efi').read_bytes()).hexdigest(),
    'payload_sha256': hashlib.sha256((p / 'usb-payload.bin').read_bytes()).hexdigest(),
}, indent=2) + '\n')
PY
