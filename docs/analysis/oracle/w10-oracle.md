# Wave-10 oracle results — add/delete, formulas, usage, preferences

2026-10-22, coordinator-only (COM to MS Project; agents cannot drive it). Four
synthetic fixtures under `corpus/probe/` (git-ignored), four committed JSONs
under `docs/analysis/oracle/`, and two reusable tools:

```
python scripts/oracle_w10.py adddelete|formulas|usage|prefs|fieldvalues|analyze <file>
python scripts/mpp_diff.py before.mpp after.mpp [--only TBkndTask] [--json out]
```

`mpp_diff` decodes Task rows (202 B), FixedMeta (47 B), Fixed2Data (64 B,
192-byte special header), Fixed2Meta (96 B) and the 24-byte-header VarMeta; it
also handles Rsc (172 B), Assn (110 B, meta 34) and Cons (20 B, meta 10).

## 1. Task add and delete — W45's design (measured)

Fixture flow (`oracle_w10.py adddelete`): 5 tasks T1..T5, 2 d each, T1→T2 FS,
one Work resource R1 at 100 % assigned to T2. Save `-base`; `Tasks.Add` T6, save
`-add`; delete T6, save `-back`; delete T4, save `-del`. Project reopens them as
**5 / 6 / 5 / 4** tasks (the last two are the point).

### Add one row: every table grows at the end

| stream | size | structural change |
|---|---|---|
| `TBkndTask/FixedData` | 1260 → 1462 (+202) | new row at index 6, `id=uid=6`; every existing row byte-identical |
| `TBkndTask/FixedMeta` | 439 → 486 (+47) | header `entry_count 9→10`, `fixed_data_size 1260→1462`; new entry offset 1260 (row 6), `+0=00`, `+2=08` |
| `TBkndTask/Fixed2Data` | 576 → 640 (+64) | 192-byte special header unchanged; new record at index 6, `+16` double = **7.0** (record ordinal+1), `+0` id0 new, `+24` id1 = **the project row's id0** (the parent of a new top-level task; corrected from "previous record's id0" after W45 dumped the record) |
| `TBkndTask/Fixed2Meta` | 880 → 976 (+96) | header `count 9→10`, `size 576→640`; new entry 9 at offset 576, flags byte `+8 = 0x7f` |
| `TBkndTask/VarMeta` | 636 → 732 (+96) | header `count 51→59`, `var2_size 590→704`; **8 new entries for uid 6**: `0x0b40000e` Name, `0x0b4000a4/a5/a8/a9/aa` (the five overtime fields), `0x0b400563`, `0x0b400564`; existing 51 entries unchanged in `(uid,key)` and order |
| `TBkndTask/Var2Data` | 590 → 704 (+114) | **not append-only**: prefix 76 B, suffix 481 B; Project replaces 33 B at 76 with 147 B and re-lays out the block store |

Var2Data rewrite measured by entry offsets: uid 0's name block moved 76 → 704
and entry `(0, 0x0b4004e4)` moved 112 → 590. A writer that appends the new
row's blocks and updates only the new entries is *simpler and self-consistent*;
Project must accept it because every block is length-prefixed and every entry
carries its own offset. Do not try to reproduce Project's compaction.

**Container size is not content size.** Stream bytes go 217,265 → 218,809
(+1,544), but the file goes 245,760 → 360,448 (+114,688): Project's save
reallocates CFB sectors. Our `replace_stream` need only grow by what it writes.

### Delete is a tombstone, never a shrink

Deleted row: `FixedMeta` entry **`+0 = 0x02`** and `+2 = 0`; the FixedData row,
its Fixed2Data record and (usually) its VarMeta entries stay. Reopen ignores it.
- Add-then-delete T6 (`-back`): 7 rows and 10 FixedMeta entries remain; uid 6
  entry `+0 0x00→0x02`, `+2 8→0`, 8 VarMeta entries still present; Project
  reports 5 tasks.
- Delete middle T4 (`-del`): row count stays 7; uid 4 entry `+0 0x00→0x02`,
  `+2 8→0`; uid 4's VarMeta entries are gone; **uid 5's ID renumbers 5→4**
  (uid unchanged) and its VarMeta group gains the deleted row's entries
  (16 entries under uid 5). Project's re-keying is a quirk, not a requirement.

**The gate is the FixedMeta *header* word +4, not the entry byte alone.**
Project's own delete writes `+4 0x00000004 → 0x00000006` (bit 0x02) in addition
to the entry `+0/+2`. Measured the hard way on W45's first delete fixtures: with
entry `+0=2` but header `+4=4`, Project reopened the tombstoned row as a **live
task** (3 tasks instead of 2). Two variants that only added `+4 |= 0x02` — one
keeping W45's VarMeta removal, one keeping Project's retained entries — both
reopened as 2 tasks. So a writer must set the header bit; whether it removes the
dead uid's VarMeta entries is free (W45 removes them).

### The reader gap this exposed (must be fixed with W45)

`core/mpp/reader.cpp` computes `live_row[row] = !table.usable || has_var_group`.
Project's liveness is the FixedMeta tombstone flag. Measured with
`build\vs-w9\tools\recalc\Release\recalc.exe corpus\probe\w10-adddel-back.mpp`:

| file | Project | our reader | why |
|---|---|---|---|
| `-back` (delete T6) | 5 | **6** | uid 6 has VarMeta, entry `+0=2` |
| `-del` (delete T4 then T6) | 4 | **5** | uid 6 phantom; uid 4 correctly skipped (no VarMeta) |
| PROJECT_A / PROJECT_B | 83 / 73 | 83 / 73 | no tombstones in either file, so this changes nothing there |

Rule to implement: a row is live iff it has a FixedMeta entry **with `+0 == 0`**
and a VarMeta group. W45 owns the one-predicate reader change plus its tests.

### W45 design constraints

1. Add: append one Task row, one FixedMeta entry, one Fixed2Data record, one
   Fixed2Meta entry, 8 VarMeta entries; update every header count/size word;
   append the Var2Data blocks and point the new entries at them.
2. Delete: set `FixedMeta +0=2, +2=0`; renumber later rows' ID (`FixedData +0`);
   remove the deleted row's VarMeta entries (cleaner than Project's re-key) and
   leave all table sizes unchanged; rows without a VarMeta group are already
   ignored.
3. Never write `214/`; Project rewrites twelve of its streams itself on save.
4. The `+3` byte of Fixed2Data id0/id1 changes on every Project save (a
   generation stamp); ours need not copy that churn.
5. Verify by re-reading from disk, and the coordinator will open the emitted
   fixtures in Project.

## 2. Custom fields and formulas — W44's ground truth

### Formula definitions live in `114/Props`, not VarMeta

`CustomFieldSetFormula(FieldID, Formula)` returns **True** and
`CustomFieldGetFormula` reads the text back. The saved file stores it in
`114/Props` as UTF-16 with field references rewritten to `[MSPJ<decimal>]`
tokens. Measured at `corpus/probe/w10-formulas.mpp` +2938..:

| source formula | stored text |
|---|---|
| `[Duration]*2` | `[MSPJ188744967]*2` |
| `[% Complete]>50,1,0` | `[MSPJ188743712]>50,1,0` |
| `[Name] & "-" & [Duration]` | `[MSPJ<Name>] & "-" & [MSPJ188744967]` |

`188744967` = `pjTaskDurationText`, `188743712` = `pjTaskPercentComplete`. The
Props blocks are separated by `\r\x00`, `ff ff ff ff` and per-field format bytes
(`5c`/`4c`/`67`…); W44 does **not** need to rewrite Props — it needs to know
formulas are there and that `[Duration]` resolves to the *text* field token.

### Project does not evaluate formulas into COM in this session

Measured, repeatedly: `Number1 := "1+1"` returns True, stores `1+1`, and
`Task.GetField`/`Task.Number1` still read **0**. A fresh open of the saved file
also reads 0. The saved file contains **no** custom-field VarMeta entries, so
values are computed by the UI, not persisted by a headless save. Consequence:
W44's evaluator semantics must be documented as inference; the *storage* and
*field ids* below are measured.

### Direct custom-field values: storage (measured)

`oracle_w10.py fieldvalues` set one value per field on T1 and saved
`corpus/probe/w10-fieldvalues.mpp`. VarMeta entries for uid 1:

| field | id | block | encoding |
|---|---|---|---|
| Text1 | `0x0b400033` | 12 B | UTF-16LE `"alpha"` + NUL |
| Number1 | `0x0b400057` | 8 B | little-endian f64 `42.5` |
| Number2 | `0x0b400058` | 8 B | f64 `3.25` |
| Duration1 | `0x0b400067` | 4 B | u32 `4800` = **file ticks** (1 d) |
| Date1 | `0x0b400109` | 4 B | packed date `0x3d1812c0` = 2026-10-26 08:00 |
| Flag1 | `0x0b400048` | — | **not in VarMeta**: Task `FixedMeta` entry byte **+24 bit 0x02** (measured below) |

COM formatting read back by `GetField`: `"42.5"`, `"3.25"`,
`"Mon 26/10/26"`, `"Yes"`, `"alpha"`, `"1 day"`. COM's `Task.Duration1`
attribute is `480` (minutes) while the file stores `4800` ticks — the usual
×10 COM/file difference.

**Flag1 location (measured by `oracle_w10.py flagdiff`).** Two files differing
only in T1's `Flag1` (False then True, same session): `Task/FixedMeta` entry 4
(row 1) byte **+24 `0x00` → `0x02`**; `Task/Fixed2Meta` entry 4 bytes **+31
and +61 `0x00` → `0x80`** (mirror bits); Var2Data bytes 76/100/102 also change
but the block layouts are identical. So boolean custom fields are bit-packed in
the FixedMeta entry tail from +24 (Flag1 = bit 0x02), not stored in VarMeta.
Reading flags therefore needs the FixedMeta entry, not the VarMeta store.

## 3. Time-phased usage — W43's answer: computed, not stored

`oracle_w10.py usage` saved three states: one flat assignment (T1/R1 3 d), one
back-loaded contour (T2/R2 3 d, `WorkContour=1`), one with 8 h actual work
(T3/R3). `TimeScaleData` daily buckets (COM returns wall-clock bucket edges;
the call itself is UTC-aware and the edges are offset — do not reuse them as
dates):

| fixture | work buckets | actual buckets |
|---|---|---|
| flat | 960, 480 | (empty) |
| back-loaded | 240, 1200 | — |
| actual | 960, 480 | 480, (empty) |

Stream diffs (`mpp_diff.py` flat→contour, contour→actual):

- **No stream grows by a per-day array.** The only growth is one assignment
  row (110 B), its FixedMeta entry (34 B), Fixed2Data record (48 B),
  Fixed2Meta entry (53 B) and its VarMeta/Var2Data blocks.
- Assignment totals are in `FixedData`: Units f64 at +12 (×10000), Work f64 at
  +20 (minutes×1000), Start/Finish/Resume packed dates at +52/+56/+60.
- The **work contour is a step list in assignment VarMeta**:
  `0x0f400031` planned (flat = 72 B, back-loaded = **212 B**, actual-case
  = 100 B) and `0x0f400032` actual work (56 B, only on the actual row).
- Per-assignment VarMeta also carries `0x0f400010`/`0x0f400020` baseline work/
  cost, `0x0f400092`/`0x0f400093` baseline start/finish, `0x0f40027a` Created,
  and internal `0x0f400299`/`0x0f40029b` 16-byte blocks.

**W43 conclusion:** build period buckets from assignment Work + Units + contour
(and the calendar); there is no timephased stream to parse. A flat spread is
correct for every contour-less assignment; decode `0x0f400031` only if
back-loaded/front-loaded shaping is wanted, and mark it inference.

## 4. Preferences — W46's persistence question

`oracle_w10.py prefs` saved `-base`, then added a Number1 column to the Entry
table (`TableEdit("Entry", True, False, False, None, "Number1", ..., 10, 0,
False, True)`), then changed that table's date format with the same call plus
`DateFormat=5`. Both calls returned True.

Measured diffs (all 96 streams, only these changed):

| comparison | streams |
|---|---|
| base → column added | `114/Props` −2 B, `214/Props`, `Props14` +192 B |
| column → date format 5 | `114/Props` +2 B, `214/Props`, `Props14` +2 B |

So **the table's date format is a property of the table definition and is
written into `114/Props`** (with Project's own `214/`/`Props14` churn). It is
not a global option. There is no exposed setter for the *global* date format,
duration format or Gantt timescale: `Application.DateFormat`/`DurationFormat`
are conversion functions, `Application.Timescale`/`Options*` are dialog
methods, and `Application.Tables` is a method returning a bool on this build
(`pywin32` quirk; `dir()` on the COM object raises "Not implemented").

**W46 conclusion:** persist app preferences app-side (NSUserDefaults / a
plist). A per-project table date format could be written through `114/Props`
only after W43/W45 own a Props decoder — do not fake it this wave. Working-day
shading is purely ours. Timescale is our view state.

### Trap for anyone re-running these

`FileSaveAs(Name="x.xml", FormatID="MSProject.xml")` silently wrote a **binary
.mpp** in the formula session (238,080 B, OLE header), unlike W9's export.
`FileSaveAs`'s Boolean return was not checked. Check the first bytes before
parsing an export, and do not trust a `.xml` suffix.
