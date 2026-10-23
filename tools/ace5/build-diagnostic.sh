#!/bin/bash
# Build on a Linux host with Mu-Silicium dependencies installed. No device operations.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD
MU=${1:?Usage: tools/ace5/build-diagnostic.sh /absolute/path/to/Mu-Silicium /absolute/path/to/device.dtb}
DTB=${2:?Device DTB is required}
python3 tools/ace5/prepare-uefi.py "$MU"
tools/ace5/build-loader.sh
(cd "$MU" && python3 build_uefi.py -d giulia -m 0 -r RELEASE)
python3 tools/ace5/pack-boot.py --mu "$MU" --dtb "$DTB" --output out/giulia/hackios-giulia-diagnostic.img
