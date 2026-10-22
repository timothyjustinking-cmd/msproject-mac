# Handoff — W4 UI inventory

2026-10-22. Branch: none of my own (Revision 3, coordinator commits from the
main checkout). Files are on disk under
`D:\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile`.

## Done

- `spec/ui-inventory.md` — container decode, 71-resource inventory with
  confidence, full DLGTEMPLATEEX spec of the 11 dialogs, RCDATA analysis,
  blunt coverage verdict.
- `scripts/uifile_scan.py`, `uifile_inventory.py`, `uifile_dialog.py`,
  `uifile_rcdata.py` — all re-runnable, read-only.
- `docs/analysis/ui/` — uifile_scan.{txt,tsv}, uifile_inventory.tsv,
  dialogs.{txt,tsv}, rcdata_scan.tsv, rcdata_strings.tsv (19,792 strings).
- `memory/ui.md` — current state.

Headline results: 442 RCDATA string tables / 19,792 keyed strings; 11 dialogs
decoded exactly; 71 UIFILEs yield 1,973 symbolic identifiers but **no text and
no geometry**; the core views are not resource-defined.

## Not done

- UIFILE value/opcode stream (the part after the name pool) — undecoded.
  Without it there is no modern-UI geometry.
- No proof that UIFILE identifier keys resolve to RCDATA/RT_STRING ids.
- 629 opaque RCDATA blobs unclassified; menus/toolbars not proven.
- `analysis/res/winproj` (1,829 bitmaps, 112 RCDATA) not examined.
- No oracle observation at all (by design; COM hangs confined).

## Next three concrete actions

1. Reverse the UIFILE value stream using 4774 (1,564 B): name pool ends at
   0x292, code from there. Cross-check against 4778 / 4806 (same DialogView
   shape) to identify operand widths and find where `DefaultWidth`'s value
   lives.
2. Search the decompiled C for the loader with non-string anchors: the
   resource id constants 4724/4750/4774 and VERSIONINFO "PJINTL", then trace
   the `LoadResource(RT_RCDATA, ...)` consumer. Goal: the name→id binding.
3. Cluster the 629 opaque RCDATA by (size, signature) and dump UTF-16 text
   for each cluster; start with the 17 identical 5,516-byte blobs (ids
   219–235).

## Traps

- `decompiled/*.c` has no `"NUB"`, `"LDXF"`, `"LuaT"` strings; do not chase
  string greps for the loader.
- `analysis/res/pjintl_strings.tsv` is a garbage-laden byte scan — use the
  generated `rcdata_strings.tsv`.
- UIFILE text is ASCII identifiers only; do not claim labels from it.
- Dropbox touches files: the edit tool may need a re-read before retrying.
- Revision 3: **do not run git**; the coordinator commits.
