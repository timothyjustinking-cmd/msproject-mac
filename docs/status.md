# Reverse-engineering status — where the analysis actually is

_Written 2026-10-22, after inventorying every artifact in the workspace._

## 1. What exists

| Area | Artifact | Scale | State |
|---|---|---|---|
| Binary loaded | `ghidra-project/WINPROJ.gpr` — WINPROJ.EXE, PE32+ x86-64, ImageBase 0x140000000 | 770 MB project | complete |
| Decompile | `decompiled/part_0000..0027.c` | 2,586,873 lines, 54,450 functions, 8 failures | complete, **pre-naming** |
| Structural meta | `analysis/meta/*.tsv` | 55,978 functions, 230,233 call edges, 89,486 vtable slots, 2,060 classes, 16,819 strings, 4,365 imports | complete |
| Type library | `analysis/typelib.tsv` | 111 dispatch interfaces, 4,609 methods, 183 enums, 5,884 enum values, 3,477 field IDs | complete |
| Naming | `analysis/named_functions_p1.tsv` | 51,257 assigned labels | complete, mostly placeholders |
| Field model | `analysis/field_metadata_all.tsv` + 7 per-type files | 3,207 fields / 8 record types, 0xA8-byte descriptor stride | **strongest result** |
| Runtime validation | `analysis/com_validation.tsv` | 838/838 COM reads matched | strong |
| Resources | `analysis/res/pjintl` (1,298), `analysis/res/winproj` (2,320) | blobs + `index.tsv` | dumped, not decoded |

One session of work: 2026-10-20 13:49 → 2026-10-21 00:39 (≈11 h). Nothing since.

## 2. What is solid

- **Field dispatch chain** fully decoded: COM `IBkndTaskDispatch` → `TBkndTaskElem` → `TTaskTable` → `TBkndPropContainer` → `vtable[158]` dispatcher, with `PropInfoData` entries on a 0xA8 stride (type code, storage class, flags, slot index, getter1/getter2 pointers).
- **Complete field inventory**: 3,207 fields — Task 1,444, Resource 883, Assignment 777, Links 30, OutlineCode 16, Calendar 14, Project 9, Task34 34 — 100% extracted, ~55% carrying COM property names.
- **Class structure**: 2,060 RTTI classes, 89,486 vtable slots, 56 modules (scheduling.tasks 11.3k, backend.core 10.5k, scheduling.resources 9.3k, backend.serialization 6.5k, drawing/OART 3.9k).
- **Runtime storage** proven by full-memory diff: three distinct locations (getter record, flat buffer, packed block); assignment flat buffer mapped to 156 bytes, validated 14/14 against COM.

## 3. The naming caveat

"98.2% of functions named" is label coverage, not understanding:

| Confidence | Count | Nature |
|---|---|---|
| ≥90 | 3,698 | matched COM interface/vtable names — reliable |
| 80–89 | 476 | RTTI/string-derived — reliable |
| 65–79 | 1,312 | string-diagnostic — useful |
| ≤55 | 45,771 | `func_N` / `related_N` / `local_N` placeholders |

Roughly **5,500 labels carry information**. The corpus is a map, not a readable program.

## 4. Not started

1. **The `.mpp` file format.** Only `analysis/test_project.mpp` (238 KB, generated via COM as a probe). No container/stream map, no record layout, no parser, no corpus.
2. **Scheduling semantics.** No CPM, calendar arithmetic, constraint handling, duration units, contours, splits, or leveling. Only class *names* (`TBkndRecalc`, `TColFormulaRecalc`).
3. **Computed/formula fields** — storage class 10, 61 Task fields with per-field computation functions.
4. **UI definition** — 71 `UIFILE` blobs and 11 dialogs dumped but undecoded.
5. **Interchange** — MSPDI/XML and the ODBC/SQL layer (67 offsets) only partly mapped.
6. **Port code** — nothing yet.

## 5. Workspace issues

- **Ghidra project inside Dropbox** (770 MB of locked binary files). Dropbox syncs every change, and the root's explicit `Everyone` deny prevents the sandbox from provisioning a write grant, which breaks shell tooling here. If it ever becomes painful, `D:\dev\re\ghidra` (not C:) is the place for it.
- **Decompiled C predates the naming pass.** `ImportNames.java` was never followed by a re-run of `ExportAllC.java`, so all 2.59 M lines still say `FUN_140001000`. ~10 minutes of compute to fix, and it makes the corpus navigable.
- **Numbers disagree between summaries.** `architecture_summary.txt` says 7 offsets / 102 fields in §5 and 11 / 149 in §9; `phase2c_improved_summary.txt` reports "high confidence: 0". Re-derive from the TSVs before quoting.
- **155 one-off `phase2d_*.py` scripts** with no runner or manifest. Promote the ~10 that matter into a pipeline; archive the rest.

## 6. What the RE still owes the port

| Question the port will ask | Where the answer must come from |
|---|---|
| How is an `.mpp` file laid out? | New work — differential file diffs via COM |
| What exactly does recalculation do? | Decompiled `TBkndRecalc` + golden-output capture |
| How are durations/work/costs encoded? | Mostly answered by the descriptor table |
| What does each of the 1,444 task fields *mean*? | Descriptor table + COM naming (838 done) |
| What is the UI supposed to show? | `UIFILE` decode + observed behaviour |

---

**Update 2026-10-22:** container mapped against a two-file real corpus - see spec/mpp-container.md. Var-meta entries are keyed by PjField id; FixedData row stride and FixedMeta layout still open.
