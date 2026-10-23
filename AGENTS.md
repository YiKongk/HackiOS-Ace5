# Repository workflow

- Make local commits when work is ready. Do not push branches, tags, releases or
  source changes to GitHub unless the user explicitly requests publication.
- The initial pre-Wi-Fi public release was authorized on 2026-10-24. It does not
  authorize future pushes.
- Keep Apple system files, SDKs, firmware, DTBs, device backups and raw logs out
  of Git. Do not delete recovery backups as part of cleanup.
- Preserve upstream attribution and per-file licenses. Record which checks ran;
  host checks do not establish device stability or a working feature.
- Scope is OnePlus Ace 5 PKG110 / giulia. Use the dedicated tools/ace5 workflow,
  not the upstream redwood flashing or Wi-Fi paths.
