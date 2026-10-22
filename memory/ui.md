# W4 — UI inventory (memory)

Status 2026-10-22. Work is on disk in the main checkout; **the coordinator
commits** (Revision 3: agents do not run git). No oracle used.

## Deliverables

- `spec/ui-inventory.md` — the result: container decode, 71-row inventory,
  RT_DIALOG spec, RCDATA analysis, honest verdict.
- `scripts/uifile_scan.py` — UIFILE header + name extraction; asserts the
  `u32 size @0x0d == len(blob)` claim for all 71.
- `scripts/uifile_inventory.py` — rid → inferred surface + confidence.
- `scripts/uifile_dialog.py` — DLGTEMPLATEEX walker (11/11 exact).
- `scripts/uifile_rcdata.py` — RCDATA classification + 442 string tables.
- `docs/analysis/ui/` — generated TSVs/text: uifile_scan.{txt,tsv},
  uifile_inventory.tsv, dialogs.{txt,tsv}, rcdata_scan.tsv,
  rcdata_strings.tsv (19,792 strings).

## Established (evidence in the spec)

1. UIFILE header: `02 10 | "NUB\0" | 12 | 0x0301 | flag | u32 size | payload`.
   Size field verified 71/71. Rest of header undecoded (61 flag-0, 10 flag-1).
2. Payload = ASCII name pool (`01 <u16 len> name`) and/or `LDXF\0` class
   record, followed by an undecoded binary/opcode stream. 48/71 have an
   LDXF class; 5 embed Lua 5.4 chunks (4750, 4811, 4826, 4838, 4840).
3. **No localised text in UIFILE** — checked all 71 for UTF-16; only Lua
   comments/font names. The identifiers are symbolic keys.
4. 1,973 distinct identifiers recovered. 5 blobs have `DialogView`
   (4774, 4778, 4798, 4826, 4834) but their width/height/title *values* are
   in the undecoded stream.
5. RT_DIALOG = DLGTEMPLATEEX, 11/11 parse exactly, 171 controls, all captions
   and geometry recovered. They are MFC OLE/shell dialogs + Font/Password/Text
   Styles — **not** Project's main dialogs.
6. RT_RCDATA: 442 of 1,071 are indexed UTF-16 string tables (u16 count,
   10-byte records `{u32 id,u32 off,u16 len}`, absolute offsets), 19,792
   strings — menus, buttons, field captions, help, validation messages.
   629 opaque; 412 of those still hold UTF-16 text under other headers.
7. Core views (Task Sheet, Gantt, Usage, Calendar drawing, Network, Timeline)
   are **not resource-defined**: no such identifiers exist in UIFILE.

## Open / next actions

1. Decode the UIFILE value/opcode stream (e.g. 4774 from 0x292). Highest
   value: would give geometry for the DialogViews/galleries.
2. Bind UIFILE symbolic keys to RCDATA ids (find the name→id map / LoadString
   call sites). Without it no caption is specifiable.
3. Classify the 629 opaque RCDATA blobs — start with the 17 × 5,516-byte
   replicas and the `00000000` family; decide menus vs lookup tables.
4. Mine `analysis/res/winproj` RCDATA (112) for the command/menu layer.

## Traps

- `decompiled/*.c` contains no `"NUB"`, `"LDXF"`, `"LuaT"` or
  `"OfficeCenterView"` string — the loader is not findable by string grep.
- `analysis/res/pjintl_strings.tsv` is a naive byte scan; most rows are
  garbage (type 234 especially). Use `rcdata_strings.tsv` instead.
- Blob filenames are `<id>_1033.bin` inside per-type directories.
- Dropbox rewrites files under the checkout: the edit tool intermittently
  reports "file changed since it was read" — re-read and retry.
- Do not run git (Revision 3); leave files on disk.
