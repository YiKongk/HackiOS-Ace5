#!/bin/bash
# Run on Linux ARM64 with THEOS set. Builds local Mesa/EAGL components for
# an Ace 5 root image; does not fetch firmware or alter an existing image.
set -eo pipefail
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ] || { [ "$#" = 2 ] && [ "$2" != --metal ]; }; then
    echo "Usage: THEOS=/path/to/theos $0 OUTPUT_DIRECTORY [--metal]" >&2
    exit 2
fi
mkdir -p "$1"
OUTPUT=$(cd "$1" && pwd)
cd "$(dirname "$0")/../.."
source ./xnu-env.sh
set -u
MESA=$XNUDEPS/build/mesa/out
SDK=$THEOS/sdks/iPhoneOS16.5.sdk
for INPUT in "$MESA/libOSMesa.8.dylib" "$MESA/gl.xml" "$MESA/exported-gl.txt" \
    userland/mkeagl.py userland/src/eagl.c userland/src/gl_osmesa.c userland/src/gl_test.c; do
    [ -f "$INPUT" ] || { echo "Missing build input: $INPUT" >&2; exit 1; }
done
mkdir -p "$OUTPUT/include"
python3 userland/mkeagl.py userland/src/eagl_slots.txt "$MESA/gl.xml" \
    "$MESA/exported-gl.txt" userland/src/eagl.c > "$OUTPUT/include/eagl_gen.h"
xnu-clang -arch arm64 -isysroot "$SDK" -O1 -dynamiclib \
    -install_name /redwood/lib/libredwood-gl.dylib -I"$OUTPUT/include" \
    -framework CoreFoundation -framework IOKit -framework IOSurface -lobjc \
    "$MESA/libOSMesa.8.dylib" -o "$OUTPUT/libredwood-gl.dylib" \
    userland/src/eagl.c userland/src/gl_osmesa.c
xnu-clang -arch arm64 -isysroot "$SDK" -O1 \
    -framework CoreFoundation -framework IOKit "$MESA/libOSMesa.8.dylib" \
    -o "$OUTPUT/gl_test" userland/src/gl_test.c
xnu-clang -arch arm64 -isysroot "$SDK" -O2 \
    -framework CoreFoundation -framework IOKit -framework IOSurface \
    -o "$OUTPUT/display_test" tools/ace5/display-test.c
cp "$MESA/libOSMesa.8.dylib" "$OUTPUT/libOSMesa.8.dylib"
sha256sum "$OUTPUT/libOSMesa.8.dylib" "$OUTPUT/libredwood-gl.dylib" "$OUTPUT/gl_test" "$OUTPUT/display_test"
if [ "${2:-}" = --metal ]; then
    # Driver and AIR library share an internal struct ABI. Do not silently package
    # an older compiler library after an edit to tools/mesa/air/.
    [ "$(cat "$MESA/built-from-air")" = "$(cat tools/mesa/air/* | sha256sum)" ] || {
        echo "AIR library is stale: rebuild with AIR=1 tools/mk-mesa.sh first" >&2
        exit 1
    }
    METAL_FLAGS=(-arch arm64 -isysroot "$SDK" -O1 -framework Foundation -framework IOKit -framework Metal -lobjc)
    xnu-clang "${METAL_FLAGS[@]}" -dynamiclib -install_name /redwood/lib/libredwoodmetal.dylib \
        -o "$OUTPUT/libredwoodmetal.dylib" userland/src/redwoodmetal.m userland/src/redwooddrawable.m
    mkdir -p "$OUTPUT/RedwoodMetal.bundle"
    xnu-clang "${METAL_FLAGS[@]}" -bundle -o "$OUTPUT/RedwoodMetal.bundle/RedwoodMetal" \
        userland/src/redwoodmetal.m userland/src/redwooddrawable.m
    cp userland/src/metal_plugin.plist "$OUTPUT/RedwoodMetal.bundle/Info.plist"
    xnu-clang "${METAL_FLAGS[@]}" -o "$OUTPUT/metal_drv_test" userland/src/metal_drv_test.m
    xnu-clang "${METAL_FLAGS[@]}" -framework MetalFX -o "$OUTPUT/metal_caps" userland/src/metal_caps.m
    xnu-clang "${METAL_FLAGS[@]}" -framework UIKit -framework QuartzCore -framework IOSurface \
        -o "$OUTPUT/triangle_app" userland/src/triangle_app.m
    xnu-clang -arch arm64 -isysroot "$SDK" -O1 -framework CoreFoundation -framework IOKit -lsqlite3 \
        -o "$OUTPUT/battery_test" userland/src/battery_test.c
    SIGNER=${LDID:-$THEOS/toolchain/linux/iphone/bin/ldid}
    "$SIGNER" -Suserland/src/triangle_app.entitlements "$OUTPUT/triangle_app"
    "$SIGNER" -Suserland/src/battery_test.entitlements "$OUTPUT/battery_test"
    xnu-clang -arch arm64 -isysroot "$SDK" -O1 -framework CoreFoundation -framework IOKit \
        -o "$OUTPUT/metal_props" userland/src/metal_props.c
    cp "$MESA/libredwoodair.dylib" "$OUTPUT/libredwoodair.dylib"
    sha256sum "$OUTPUT/libredwoodmetal.dylib" "$OUTPUT/libredwoodair.dylib" \
        "$OUTPUT/RedwoodMetal.bundle/RedwoodMetal" "$OUTPUT/metal_drv_test" \
        "$OUTPUT/metal_caps" "$OUTPUT/metal_props" "$OUTPUT/triangle_app"
fi
