# UEFI for the Ace 5 public source release

Use the [Ace 5 UEFI guide](../docs/UEFI.zh-CN.md).

Upstream: [Project Silicium / Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium), pinned to `42fe563791e51bf5500c2e470ed294ca0c94b97c`.

This directory retains the upstream source patch and loader hook. The old redwood vendor binaries, device DTB and prebuilt UEFI were removed from this source release. The Ace 5 workflow uses Mu's existing `giulia` platform, `tools/ace5/prepare-uefi.py`, and a DTB read from the owner's device. Do not run the legacy redwood tutorial for this phone.
