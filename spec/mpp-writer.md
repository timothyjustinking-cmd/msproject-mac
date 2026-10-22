# `.mpp` writer — container, in-place patching, stream replacement

Third stream of the writer work (W9), 2026-10-22; stage 4 added by W21 the same
day. Companion to [`mpp-container.md`](mpp-container.md) (the wrapper) and
[`mpp-records.md`](mpp-records.md) (the Task record). This file describes what
`core/mppwrite/` does, what it has proved, and the exact evidence for every
claim.

Four stages, in the order they were built, each standing on the previous one:

| stage | claim | code | test |
|---|---|---|---|
| 1 | parse the container and re-emit it **byte-identically** | `cfb_writer.{h,cpp}` | `tests/roundtrip_cfb.cpp` |
| 2 | change one field by rewriting **only** its bytes | `inplace.{h,cpp}` | `tests/inplace_patch.cpp` |
| 3 | replace a whole stream, growing or shrinking it | `cfb_writer.*`, `table.{h,cpp}` | `tests/roundtrip_stream_replace.cpp` |
| 4 | write a row so **every copy** of a date/duration agrees (§10) | `row_writer.{h,cpp}` | `tests/roundtrip_row_write.cpp` |

Checks, measured 2026-10-22 with 42 files in `corpus/probe`: 632 container /
38 patch / 520 stream / 299 row = 1,489, 0 failures, MSVC 19.44
`/W4 /permissive- /utf-8`. (The container test enumerates `corpus/probe`, so its
count grows with the fixture set; earlier revisions quoted 784 for stages 1–3
when the directory held 19 files, which is why that number no longer appears.)

Re-run everything:

```
cmake -S core/mppwrite -B core/mppwrite/build/vs -G "Visual Studio 17 2022" -A x64
cmake --build core/mppwrite/build/vs --config Release      # no --parallel
ctest --test-dir core/mppwrite/build/vs -C Release --output-on-failure
python docs/analysis/writer/cfb_assumptions.py             # container claims
python docs/analysis/writer/validate_written.py            # independent parser
```

**Status: stage 1 verified. Stage 2 verified at container level and opened by MS
Project (§8.1); the patched values for Priority and PercentComplete survive the
open, but patched Start/Duration do not reach Project's model. Stage 3 verified
and **opened by MS Project**: a rebuilt FixedData and an append-only Var2Data
growth both load with the written value visible (§8). One fixture was refused,
and §8 shows it is the test's own scratch in `114/Props`, not the edit path.
Stage 4 is built and self-verified (§10.2), and one fixture is queued for the
oracle (§10.3): `oracle-recalc.mpp`, a whole-project date shift written through
every copy.**

---

## 0. What "byte-identical" is proving

The round trip is not a copy. `CfbDocument` stores **sector placement as data**
(every chain: directory, mini FAT, mini stream, every regular stream) and then
*derives* the FAT, the mini FAT, the DIFAT, the directory's `start`/`size`
fields and the header from those chains. If any derivation is wrong the output
bytes differ from the input and `tests/roundtrip_cfb.cpp` fails. That is why a
passing round trip is evidence of understanding rather than of copying: the file
is rebuilt from the same facts a writer would need to build a new one.

So that this is not merely self-consistent, the written files are also read back
by two independent parsers (§7): `olefile` (Python, unrelated implementation)
and the project's own reader (`core/mpp/reader.cpp`, a different stream's code).

---

## 1. Container facts a writer must reproduce (measured)

Commands: `python docs/analysis/writer/cfb_recon.py <file>` and
`python docs/analysis/writer/cfb_assumptions.py` (all 19 `corpus/probe/*.mpp`
plus both real files).

### 1.1 Shape

| file | bytes | sectors | dir entries (live/empty) | streams | FAT sectors |
|---|---|---|---|---|---|
| `probe/base.mpp` | 237,568 | 463 | 120 (119/1) | 96 | 4 |
| `probe/n1.mpp` | 235,008 | 458 | 120 (119/1) | 96 | 4 |
| `probe/n6.mpp` | 240,640 | 469 | 120 (119/1) | 96 | 4 |
| `project_a.mpp` | 1,567,744 | 3,061 | 136 (134/2) | 109 | 24 |
| `project_b.mpp` | 1,601,536 | 3,127 | 144 (144/0) | 116 | 25 |

Version 3.62 for all five; sector 512, mini sector 64, cutoff 4,096. The file
is exactly `512 + sectors * 512` bytes: there is **no trailing data** after the
last sector (`cfb_recon.py` prints `bytes after last sector: 0`).

### 1.2 The FAT is derivable (verified)

For every file, rebuilding the FAT from the chains — chain link, `ENDOFCHAIN`
(0xFFFFFFFE) at each chain end, `FATSECT` (0xFFFFFFFD) on FAT sectors,
`DIFSECT` (0xFFFFFFFC) on DIFAT sectors, `FREE` (0xFFFFFFFF) everywhere else —
reproduces the stored FAT **entry for entry**. `CfbDocument::check()` performs
that comparison and `tests/roundtrip_cfb.cpp` asserts it per file.

Consequences a writer can rely on:

- FAT entries **beyond** the end of the file are `FREE`, not zero: base.mpp has
  463 sectors but 512 FAT entries; entries 463..511 are `0xFFFFFFFF`.
- `FREE` sectors **inside** the file count is 0 in every corpus file, and every
  sector is reachable from a chain (no orphans). The round-trip test re-asserts
  both: unowned sectors must be all-zero, and there are none.
- Unused DIFAT slots in the header are `FREE` (`0xFFFFFFFF`), not zero.
- Version 3 writes `num_dir_sectors = 0`; the writer preserves that rather than
  "fixing" it to the real directory sector count.

### 1.3 The mini stream is a heap with a *recorded* allocation map (verified)

Mini sectors are **not** handed out in directory order, so the map must be
carried, not recomputed. base.mpp:

```
\x01CompObj          mini sector 29      Props14                mini sector 0
\x05SummaryInformation  24               TBkndTask/Var2Data     174
TBkndTask/FixedData 144                  TBkndTask/VarMeta      173
```

`cfb_assumptions.py` predicted "entry order" and was wrong on every entry; the
writer therefore stores each stream's mini chain verbatim. Total mini stream in
base.mpp: 15,744 bytes in 31 sectors, of which 13,041 bytes are used, so 2,703
bytes of slack sit inside allocated mini sectors — those bytes are preserved
(the writer starts from the parsed mini stream image and overwrites payloads).

### 1.4 Padding is not zero everywhere (verified — do not zero-fill)

- PROJECT_A's `114/TBkndTask/Fixed2Data` is 4,752 bytes in 10 sectors; the final
  sector has **368 padding bytes of which 198 are non-zero**. A writer that
  pads with zeros cannot round-trip PROJECT_A.
- Directory entries have non-zero bytes after the name field in every entry of
  every file (the 64-byte name field is written with garbage past the
  terminator). The writer keeps all 64 name bytes verbatim.
- Empty directory slots are exactly `64 * 0x00`, `name_length = 0`, `type = 0`,
  links `0xFFFFFFFF`, everything else zero.

So the model keeps `Stream::tail` (the bytes after a stream's last byte inside
its final sector) and `mini_stream_tail_`, and writes them back.

### 1.5 Directory entries are fully modelled

All 128 bytes are covered by named fields (64 name bytes, `name_length`, type,
colour, left/right/child, CLSID, state, two timestamps, start, size), so
re-emitting an untouched entry is exact and renaming a stream needs no opaque
blob. Writes are emitted in entry-index order, 8 per sector, which is how every
corpus file is laid out.

### 1.6 Names keep their padding (verified)

Storage names are right-aligned in a 6-character field: the real path is
`"   114/TBkndTask/FixedData"`. `CfbDocument` keeps both the stored path and a
normalised one (spaces stripped per component), and lookups use the normalised
form, so callers write `114/TBkndTask/FixedData`. `tests/roundtrip_cfb.cpp`
asserts the stored form for `base.mpp`.

---

## 2. Stage 1 — round trip, measured

`tests/roundtrip_cfb.cpp`, 310 checks over 21 files (19 probe files plus both
real projects). Per file it asserts:

1. `CfbDocument::check()` is empty (derived FAT == stored FAT, sizes agree,
   no sector claimed twice, no chain past EOF);
2. `to_bytes() == the input bytes` (a failure prints the first differing byte);
3. emit → re-parse → emit is idempotent;
4. the stream set and every stream payload survive the trip;
5. no free sectors inside the file, and the owned-sector count equals the
   sector count of the file;
6. every byte in a sector no structure claims is zero;
7. the container is v3 / 512 / 64 / 4096.

`base.mpp` additionally gets its record-layer anchors re-checked from the
writer's own parser: FixedData is 856 = 48 + 4×202 bytes; row 0 is the project
row (id 0, uid 0); uids 1,2,3; priority 500 in every row; durations 24,000 /
14,400 / 9,600; task A starts 0x3bf212c0 = 2026-01-05 08:00.

Result: **all 21 files round-trip byte-identically.** Nothing in the container
had to be carried as an opaque blob, including `214/`.

---

## 3. Stage 2 — in-place patching

### 3.1 The mapping

Changing one field is a byte-range problem, not a save. `InPlacePatcher`
resolves

```
stream offset -> sector (or mini sector -> mini stream -> root sector) -> file offset
```

and rewrites only those bytes. The file length never changes, no sector moves,
and no metadata stream is touched. `changed_ranges()` reports the exact file
spans that differ from the input, which is what makes the claim falsifiable.

Task-row offsets come from `spec/mpp-records.md` §2 via `mppwrite::task_row`:

```
row byte = 48 + 202 * row + offset
  +78 u16 Priority    +84 u32 Duration      +92 u16 PercentComplete
  +96 u32 Start       +100 u32 Finish
```

**Correction to `mpp-records.md` §2:** PercentComplete is stored in the low
**two** bytes at +92; +94/+95 are zero. `var-pct.mpp` row 1 has
`32 00 00 00` at +92 (50). The writer writes a u16; for 0..100 a u32 write
produces the same bytes, so this is a naming/strictness fix, not a behavioural
one. (`core/mpp/reader.h` already reads a u16.)

### 3.2 Measured results (`tests/inplace_patch.cpp`, 35 checks)

`corpus/probe/base.mpp` in, `core/mppwrite/build/vs/test-out/inplace-patched.mpp`
out, same 237,568 bytes:

| field | change | bytes changed in the file |
|---|---|---|
| Priority (+78) | 500 → 900 | 2 (`f4 01` → `84 03`) |
| PercentComplete (+92) | 0 → 50 | 1 (`00` → `32`) |
| Duration (+84) | 24,000 → 33,600 (7 d) | 2 |
| Start (+96) | 2026-01-05 08:00 → 2026-03-02 08:00 | 2 |
| `114/Props` u32 at 510 | arbitrary | 1..4 (crosses a sector boundary) |

The test pins the priority patch to the *specific* file offset the container
says holds that byte, so a wrong mapping fails rather than merely producing a
different file. After patching it re-parses the result, asserts the container
still re-emits byte-identically, that the sector and directory counts are
unchanged, that **exactly two streams** changed (`114/TBkndTask/FixedData` and
the deliberately patched `114/Props`), and that every other stream — all of
`214/` included — is byte-identical.

Both allocation paths are covered: `TBkndTask/FixedData` is 856 bytes and lives
in the **mini** stream; `114/Props` is 88,690 bytes and lives in **FAT** sectors.

### 3.3 Limits

It cannot change a value's size, so it cannot grow a string, add a record, or
change a stream's length. It also edits bytes without recomputing derived
values: setting Duration does not touch Finish, and setting PercentComplete does
not touch ActualDuration. That is deliberate — stage 2 is "write the byte the
caller asked for" — but it means a patched file can be internally inconsistent.
MS Project recalculates on open, and §8 asks the oracle to prove that.

---

## 4. Stage 3 — stream replacement

### 4.1 What the writer does with a replaced stream

`CfbDocument::replace_stream(path, bytes)`:

- **same sector count** as the current chain → reuse those sectors. The file
  changes only in the payload bytes (plus nothing else), which is exactly the
  in-place patch result. `tests/roundtrip_stream_replace.cpp` asserts the two
  paths produce **identical files**.
- **otherwise** → release the old chain (the sectors become `FREE` *inside* the
  file, which is legal) and append fresh sectors at the end of the sector
  space. Growing the mini stream extends the root entry's payload and, when it
  crosses a sector boundary, its chain; the root entry's `size` is updated.
- a stream that crosses the 4,096-byte cutoff moves between the mini stream and
  FAT sectors, because readers choose the path by size (`core/mpp/cfb.cpp` does
  exactly that, and so does `olefile`).
- the allocation is **append-only**: freed sectors are not reused. That is a
  deliberate simplification — a free-list is a save-path optimisation, not a
  correctness requirement, and it keeps every other byte of the file in place.

### 4.2 What must stay consistent (the list this stage exists to document)

Replacing `114/TBkndTask/FixedData` or growing `114/TBkndTask/Var2Data` is only
safe if all of the following hold. `mppwrite::check_table(doc)` checks the ones
marked ✓ and returns the first disagreement:

| invariant | where | checked |
|---|---|---|
| `FixedData` = 48 + 202 × rows | `TBkndTask/FixedData` | ✓ |
| FixedMeta header +0x0c == FixedData byte size | `TBkndTask/FixedMeta` | ✓ |
| FixedMeta header +0x08 == number of 47-byte entries | `TBkndTask/FixedMeta` | ✓ |
| FixedMeta entry *i* (i ≥ 3) +0x04 == 48 + 202·(i−3) | `TBkndTask/FixedMeta` | by construction (row count is invariant) |
| VarMeta header +0x14 == Var2Data byte size | `TBkndTask/VarMeta` | ✓ |
| every VarMeta offset addresses a block that fits in Var2Data | `TBkndTask/VarMeta` | ✓ |
| VarMeta `mid` groups match the row UniqueIDs | both | unchanged by design (only offsets move) |
| Fixed2Data = 64 × (rows + 3), Fixed2Meta = 16 + 96 × (rows + 3) | `TBkndTask` | not applicable: row count is invariant |
| directory entry `size`/`start` of the replaced stream | directory | handled by `replace_stream` |
| root entry `size` == mini stream payload size | directory | handled: growing the mini stream updates it |
| FAT / mini FAT links | container | derived on every emit |
| `214/` | untouched | asserted per case |

**Row-count changes are refused**, not silently mishandled: adding a task row
would also require a FixedMeta entry (47 bytes, with its opaque 16-byte identity
blob and u16 field count), a Fixed2Data record (64 bytes), a Fixed2Meta entry
(96 bytes), a VarMeta `mid` group and renumbered row ids. `store_task_fixed_data`
throws with that list.

**Adding a VarMeta entry is refused** for the same reason: it moves the 4-byte
trailer that terminates VarMeta. Growing an *existing* value is supported
because the append-only path leaves every other offset alone.

### 4.3 The append-only variable store

`replace_var_block(doc, table, field_id, mid, payload)`:

1. append `u32 length || payload` to `Var2Data`;
2. rewrite the `offset` of every VarMeta entry with that `(field_id, mid)` to the
   new block;
3. set the Var2Data size word in the VarMeta header (+0x14).

Nothing else moves, so no other record needs rewriting. The old block stays
behind as unreferenced bytes — which real files already contain, so a reader
must tolerate it (`mpp-records.md` §5.3), and the project's reader does.

### 4.4 Measured results (`tests/roundtrip_stream_replace.cpp`, 439 checks)

Source `corpus/probe/base.mpp` (237,568 B), written into
`core/mppwrite/build/vs/test-out/`:

| case | change | file size | streams changed |
|---|---|---|---|
| A | FixedData rebuilt with task C priority 900 (same size) | 237,568 (**2 bytes differ**) | `114/TBkndTask/FixedData` |
| B | task B name `Task B` → `client-text-025 (rev B)` | 238,080 (+512) | `Var2Data`, `VarMeta` |
| C | task A name → 4,200 characters (forces Var2Data past 4,096 B) | 246,784 (+9,216) | `Var2Data`, `VarMeta` |
| D | PROJECT_A `114/Props` 90,082 → 102,082 B (needs two more FAT sectors) | 1,671,168 (+103,424) | `114/Props` |
| E | PROJECT_A `114/Props` 90,082 → 100 B (moves into the mini stream, 176 FAT sectors freed) | 1,567,744 (+0) | `114/Props` |

- **A** is byte-for-byte identical to the in-place patch of the same field, and
  differs from the input in exactly 2 bytes.
- **B**: Var2Data 464 → 552 bytes (+4 length prefix + 2·43 + 2 NUL bytes);
  VarMeta keeps its size (no entry added); FixedData unchanged; the new data is
  an exact extension of the old (append-only asserted).
  The mini stream grew from 15,744 to 16,320 bytes, which pushed the root
  entry's chain from 31 to 32 sectors — +512 bytes in the file, and the root
  `size` field had to move with it (this was a real bug the test caught: the
  first version left the root size stale and the re-read truncated the stream).
- **C**: Var2Data 464 → 8,870 bytes, above the cutoff, so it leaves the mini
  stream and is addressed in whole 512-byte FAT sectors (18 of them, +9,216
  bytes). The old mini sectors are released in the mini FAT.
- In every case: container self-check empty, re-emission idempotent,
  `check_table` empty, `FixedData` untouched where only a name changed, and all
  `214/` streams byte-identical.

- **D** and **E** run on `corpus/project_a.mpp` (1,567,744 B, 3,061
  sectors) because its FAT is nearly full: 24 FAT sectors address 24 × 128 =
  3,072 sectors. Growing `114/Props` by 12,000 bytes (90,082 → 102,082 = 176 →
  200 sectors) appends the new 200-sector chain at ids 3,061..3,260 (the old 176
  sectors stay behind as free sectors). The resulting 3,263 sectors need
  `ceil(3263 / 128) = 26` FAT sectors, so the writer **appends two FAT sectors**
  (ids 3,261, 3,262), grows the FAT array from 3,072 to 3,328 entries, and
  updates `num_fat_sectors` in the header and the DIFAT slot list. The re-read
  parses and re-emits identically, which is the only evidence yet that our FAT
  growth is right; MS Project has not seen such a file.
  Case **E** shrinks the same stream to 100 bytes: it moves into the mini stream
  (which absorbs it without growing the root chain, so the file size does not
  change) and its **176 FAT sectors are released**, so the emitted file contains
  free sectors inside it — legal in the format, and something no corpus file
  exhibits. Re-parsing that file, re-emitting it and reading the shrunk payload
  back all succeed, and all `214/` streams are still identical.

C's 4,200-character name is not something MS Project would write (its own limit
is 255 characters). It is a container-level test that reaches the mini→FAT
migration without a multi-megabyte fixture; it is **not** a claim that such a
file opens in Project.

---

## 5. Not implemented, deliberately

1. **Adding or deleting a directory entry** (creating/removing a stream). Needs
   a red-black tree insert (colour, left/right/child links), possibly a new
   directory sector, and a new FAT allocation for it.
2. **Adding or deleting a link, assignment or resource row**, or a new variable
   field. Task rows are implemented (§11); the other tables are not, so a delete
   that would orphan a link/assignment is refused by default
   (`DeleteOptions::allow_dangling`).
3. **Repairing damage.** PROJECT_A's `FixedMeta` declares 87 entries but has 88, its
   `VarMeta` declares 1,076 but has 1,080, and row 67 (uid 10856) has no
   metadata at all. The writer carries all of that through unchanged; repairing
   it is a separate decision with oracle consequences. One consequence: the
   structural writer refuses PROJECT_A (`add_task_row`/`delete_task_row`) because its
   size identity is broken.
4. **Recomputing derived fields.** Writing Duration does not update Finish or
   the project summary row; writing PercentComplete does not update
   ActualDuration. The scheduling engine owns that, and MS Project recalculates
   on open (§8).
5. **Free-sector reuse.** Freed sectors stay allocated-but-free inside the file.

---

## 6. Date and duration encoding (used by both stages)

Verified against the fixtures and the two real files:

```
date     = (day << 16) | (seconds_since_midnight / 6)
           day 0 = 1983-12-31   (15346 = 2026-01-05; 15636 = 2026-10-22)
           tick  = 6 s          (4800 ticks = one 8-hour day)
duration = ticks (4800 = 1 d)
```

`0x3bf212c0` = day 15,346, 4,800 ticks = 2026-01-05 08:00.
`0x3d141482` = day 15,636, 5,250 ticks = 2026-10-22 08:45 (base.mpp's save time).

`mppwrite::pack_date_ymd(2026, 3, 2, 8*3600)` = **`0x3c2a12c0`** (day 15402,
4800 ticks); the tests round-trip it through the file and format it back to
`2026-03-02 08:00`. An earlier revision of this line said `0x3c2e12c0`, which is
2026-03-06 — the code and the tests were right, the prose was wrong. Cross-check:
Project's own save of the W3-O1 fixture gives 2026-10-19 = day 15633 =
`0x3d1112c0` at 08:00 and `0x3d1127d8` at 17:00, and 2026-03-02 is 15633 - 231 =
15402 = `0x3c2a`.

**Inference (not settled):** date-only project fields appear to be stored in UTC
while task working times are local wall time (`mpp-records.md` §3). The writer
does no timezone conversion; it writes the caller's pack. A DST fixture is still
needed.

---

## 7. Verification, including independent parsers

1. **MSVC build + ctest** (§ top): 3 tests, 784 checks
   (310 container / 35 patch / 439 stream).
   `ctest` runs them from the repository root so `corpus/` paths resolve.

2. **`docs/analysis/writer/cfb_assumptions.py`** — states each assumption a
   re-emitter depends on (reachability, FAT derivability, zero padding except
   where recorded, mini-FAT consistency, directory packing, empty slots) and
   reports per file. It also records the one assumption that was **wrong**: mini
   sectors are not allocated in directory order.

3. **`docs/analysis/writer/validate_written.py`** — re-reads every written
   fixture with **`olefile`** (a different implementation, unaware of this
   project) and compares each stream against the source. Output:

   ```
   inplace-patched.mpp      237568 B  changed ['114/Props', '114/TBkndTask/FixedData']  OK
   replace-fatgrow.mpp     1671168 B  changed ['114/Props']                               OK
   replace-fixeddata.mpp    237568 B  changed ['114/TBkndTask/FixedData']                 OK
   replace-grow.mpp         246784 B  changed ['114/TBkndTask/Var2Data', '...VarMeta']    OK
   replace-name.mpp         238080 B  changed ['114/TBkndTask/Var2Data', '...VarMeta']    OK
   replace-shrink.mpp      1567744 B  changed ['114/Props']                               OK
   6/6 clean (parser: olefile 0.47)
   ```

4. **The project's own reader** (`core/mpp/reader.cpp`, written by another
   stream) opens the written files with **0 warnings** and reports the written
   values:

   ```
   inplace-patched.mpp: uid 1 prio 900 pct 50 dur 33600 start 2026-03-02 08:00
   replace-fixeddata.mpp: uid 3 prio 900
   replace-name.mpp: uid 2 name 'client-text-025 (rev B)'
   ```

   Reproduce with a scratch driver (not committed):
   `cl /Icore\mpp /Icore\mppwrite <driver>.cpp core\mpp\reader.cpp core\mpp\cfb.cpp`

---

## 8. What MS Project actually did with these files (measured 2026-10-22)

The experiment ran. Reproduce with:

```
python scripts/oracle_writer.py patch|rebuild|name|base
python scripts/oracle_writer.py diff
python scripts/oracle_patch_variants.py make|open
```

Results: `docs/analysis/oracle/writer-{patch,rebuild,name,base}.json`,
`writer-diff.json`, `patch-variants.json`. The bisect fixtures are single-field
edits applied to `base.mpp` by locating its 856-byte FixedData payload verbatim
in the file (the same byte-range primitive `InPlacePatcher` uses).

| file | edit | Project's response |
|---|---|---|
| `oracle-rebuild.mpp` | `replace_stream`: FixedData rebuilt, task C priority 900 | **opens**; task C shows Priority 900 |
| `oracle-name.mpp` | append-only Var2Data + VarMeta repoint, 55-char name | **opens**; the full name is shown |
| `oracle-patch.mpp` | in-place patch of priority / percent / duration / start **plus** a scratch u32 in `114/Props` | **refused**: `FileOpen` returns, no document is open (`ActiveProject` null), `CalculateProject`/`FileSaveAs` answer "method is not available in this situation", and no window is visible |
| `pv-priority`, `pv-percent`, `pv-percent-consistent`, `pv-duration`, `pv-duration-consistent`, `pv-start-96-only`, `pv-start-all-copies`, `pv-all-four`, `pv-duration-both-tables` | each in-place edit alone | **all open** |
| `pv-props-510.mpp` | only the arbitrary u32 at `114/Props`+510 | **refused** |
| `base.mpp` -> `resaved-base.mpp` | open + Save As, no edit | saves; 25 streams rewritten (§8.2) |

So the refusal is caused by the test's deliberate scratch at `114/Props` offset
510, **not** by the FixedData edits: the same four edits without the scratch
(`pv-all-four.mpp`) open. `114/Props` is validated content — the "arbitrary"
patch in §3.2 was not harmless, and the test's claim is only that the *container*
still round-trips, which it does.

### 8.1 What survives the open — and what Project recalculates

Read back through COM immediately after `FileOpen` and again after
`CalculateProject` (identical both times, so this is load-time state, not a
button press):

| written | Project shows |
|---|---|
| Priority 900 | **900** (FixedData +78 is authoritative) |
| PercentComplete 50 | **50** (FixedData +92) |
| Name `client-text-025 (rev B)` | **the full name** (Var2Data) |
| Duration 33600 ticks (7 d) | **2400 minutes = 5 d** — the value the file had before the patch |
| Start `0x3c2a12c0` (2026-03-02) | the schedule's own date, not the patched one |

Two consequences, both important and both measured rather than inferred:

1. **Dates and durations in the fixed row are not what Project uses on load.**
   The row carries a second copy: `114/TBkndTask/Fixed2Data`, whose entry
   `row + 3` holds a packed start at +46, a packed finish at +54 and a duration
   at +58 (base.mpp entry 4 = row 1: `20 1c f6 3b` / `c0 5d 00 00` = 24000).
   Patching FixedData +84 and Fixed2Data +58 together still did not change
   Project's reported duration (`pv-duration-both-tables.mpp`), so the
   authority is somewhere else again, or Project recomputes the value on load.
   **Open:** which store wins, and whether Project "repairs" rows whose copies
   disagree. Until that is settled, the writer must not claim that patching a
   date or a duration changes the schedule; it should write a *consistent* file
   and let the engine own the arithmetic (roadmap stage 4).
2. **A date written by the port must be written consistently in every copy.**
   The one-change fixture `var-duration` changes the same 4-byte duration at
   FixedData +84 *and* Fixed2Data +58, and the same date at FixedData +96 and
   Fixed2Data +46 — three copies of start/finish exist in a row (§2) and two in
   Fixed2Data. A single-copy patch leaves the row self-inconsistent.

### 8.2 Save As rewrites `214/` — the container question is settled

`python scripts/oracle_writer.py diff` on `base.mpp` (237,568 B) -> 
`resaved-base.mpp` (239,104 B; 96 -> 96 streams):

| stream | bytes | note |
|---|---|---|
| `214/CV_iew/Var2Data` | 100,178 -> 100,178, 103 bytes differ at 42,613+ | view state rewritten |
| `214/CEdl/Var2Data` | 474 -> 702 | |
| `214/CFilter/{FixedData,FixedMeta,VarMeta,Var2Data}` | 230 -> 460 for FixedData, new blocks | the filter UI state grew |
| `214/CGrouping/FixedData`, `214/CTable/FixedData`, `214/CV_iew/FixedData`, `214/Props` | 10-15 bytes each | |
| `114/TBkndTask/Fixed2Data` | 39 bytes at +192 (the project row's entry) | |
| `114/TBkndCal/Fixed2Data`, `114/TBkndAssn/*`, `114/TBkndRsc/FixedMeta`, `114/Props`, `Props14`, `SummaryInformation` | small | |

No stream is added or removed, but **`214/` is not inert**: Project's own save
rewrites view, filter and grouping state (12 of the 25 changed streams are
under `214/`: `CV_iew` Var2Data+FixedData, `CEdl` Var2Data+FixedData+VarMeta,
`CFilter` FixedData+FixedMeta+VarMeta+Var2Data, `CTable/FixedData`,
`CGrouping/FixedData`, `214/Props`), and even rebuilds the empty assignment
table's metadata. Carrying `214/` opaquely is therefore fine for a *pass-through*
edit that Project will re-save, and **not** a viable strategy for a real save
path (M2). Budget for regenerating view state.

Two smaller measured facts:

* Project's Save As normalises the file shape: +1,536 bytes for base.mpp, all
  of `214/` present, `DocumentSummaryInformation` unchanged in this case.
* Project **recalculates on open**: the dates it reports come from the schedule,
  not from the stored task copies. A file whose rows are internally consistent
  but whose dates are stale opens with the recalculated dates, which is what
  makes "patch the bytes, let Project fix it" unsafe as a design.

### 8.3 The original questions, answered

| question | answer |
|---|---|
| Does Project accept a rebuilt container? | **Yes** — `oracle-rebuild.mpp` opens with the rebuilt value. `replace_stream` is sound, including the freed-sector-within-file case. |
| Does Project accept an append-only Var2Data growth and a VarMeta repoint? | **Yes** — the 55-character name is shown. The 4,096-byte migration was not exercised by an in-range filename, so that sub-question stays open. |
| Does Project accept a byte-patched fixed row? | **Yes at the container level**, and Priority/PercentComplete take effect; a patched Start/Duration does not (see 8.1). |
| Does Project rewrite `214/` on every save? | **Yes** — 8 streams under `214/` change (8.2). |
| Is `114/Props` safe to scratch? | **No** — a single u32 at +510 makes the file unopenable. |

---

## 9. Open questions

| question | what would settle it |
|---|---|
| Does Project accept a container we rebuilt? | §8, `oracle-rebuild.mpp` |
| Does Project accept a byte-patched fixed row (recalculating derived fields)? | §8, `oracle-patch.mpp` |
| Is the 4,096-byte mini-stream boundary the same for Project's reader? | not asked yet: case C is out-of-range content, case E moves a stream *below* the cutoff. A valid-length name-growth fixture that crosses 4,096 would settle it |
| Does Project rewrite `214/` on every save? | §8 item 5 |
| Which bytes of the 16-byte FixedMeta entry blob are identity vs save metadata? | a save from a *different* Project build, or a delete/restore row fixture |
| Are date-only fields really UTC? | two fixtures either side of a DST boundary |
| Does the FAT tolerate free sectors inside the file, and a 25th FAT sector appended past the old FAT? | not yet asked of Project: cases D and E exercise both at container level. A fixture that shrinks `114/Props` in `base.mpp` would answer it in one open |
| Is the `0x80000000 | uid` VarMeta family safe to leave alone? | a baseline fixture (BaselineSave is not exposed by this build) |

---

## 10. Stage 4 — consistent row writes, the oracle fixture, and `214/`

Added 2026-10-22 (W21). Code: `core/mppwrite/row_writer.{h,cpp}`; test:
`tests/roundtrip_row_write.cpp` (299 checks). Everything below is either measured
on the files named or labelled **inference**.

### 10.0 Which copies Project believes — measured 2026-10-22 (W26 + coordinator)

The single-variable fixtures (`tests/roundtrip_authority.cpp`,
`core/mppwrite/build/vs/test-out/w26-*.mpp`, opened read-only by the
coordinator; `docs/analysis/oracle/w26-open*.json`) answer the "copy authority"
question for a **manually scheduled** task, and it is not a single word:

* `+96/+100`, `+104/+108`, `+112/+116` changed alone: Project's model is
  identical to the baseline (Start/Finish/Early/Late unchanged). `+84/+88`
  alone is also ignored.
* Fixed2Data changed alone DOES move the model: `+54` alone moves the reported
  Finish; `+46/+50` alone also move the Finish (Project takes the later of the
  changed Fixed2Data words), while Start stays at the project start.
* Project's own save after `Task.Start = 2026-01-12 08:00` on that manual task
  (`corpus/probe/manual-start-edited.mpp`) writes the new dates to `+104/+108`
  and `+112/+116` and to Fixed2Data `+50/+54`, and leaves `+96/+100` and
  Fixed2Data `+46` at their old values. Reopening it reports Start 2026-01-12 /
  Finish 2026-01-16.

So `+104/+108` are the **effective Start/Finish** the UI reports; `+96/+100`
are a **calculated-schedule copy** (equal to `+104/+108` for auto-scheduled
files, which is why the reader's `+96` read matches PROJECT_A's export);
`+112/+116` are **LateStart/LateFinish**; Fixed2Data `+50/+54` track
Start/Finish and `+46` is an early/project-start copy. W21's "consistent write"
changed every FixedData and Fixed2Data date word together and Project still
ignored it; the single-word fixtures show that is not because one particular
store wins — the loader only trusts a row that looks like Project's own save.
The manual-task Start store is therefore still **open**: the next experiment is
a whole-row diff of `manual-start-edited.mpp` against `base.mpp` (including
whether a Var2Data block changed), not another single FixedData word.

### 10.1 Why a "consistent row writer" and not a field setter

§8.1 measured that a patched Start/Duration is recalculated away on open, and
that Project's own save of `base.mpp` (`corpus/probe/resaved-base.mpp`) keeps
the row's date copies exactly as they were. The copies, re-measured by diffing
`base.mpp` against the one-change fixtures:

| value | copies | evidence |
|---|---|---|
| Start | FixedData +96, +104, +112 | `var-projstart.mpp` row 0 moves all three; `var-duration.mpp` row 2 moves +112 |
| Finish | FixedData +100, +108, +116 | same fixtures |
| Duration | FixedData +84, Fixed2Data record `row+3` +58 | `var-duration.mpp` row 2: +84 14400→24000 **and** rec 5 +58 14400→24000 |
| Actual/remaining duration | +80, +88 (`actual + remaining == duration`) | `var-pct.mpp`: 50 % of 24,000 → +80 12000, +88 12000 |
| TotalSlack | +74, +190 (equal in every measured row) | §2; `var-duration.mpp` row 2 −9600 in both |
| StartSlack | +186 (equals +74 on task rows; differs on `w3-o1.mpp`'s project row: 0 vs 4800) | §2 + W18 |
| Fixed2Data Start | +46, +50 (equal to each other and to FixedData +104 on every measured task row) | `var-projstart.mpp` rec 4–6; base rows 1–3 |
| Fixed2Data Finish | +54 (equals FixedData +108 on every measured task row) | `var-duration.mpp` rec 5 +54 moves with +108 |

The copies are **not all the same number**, and §10.0 identifies them: `base.mpp`'s
manual task A stores `2026-01-05 08:00` at +96 (calculated schedule) but
`2026-01-04 13:00` at +104/+112 (effective Start = the fixture's project start,
which the naive-datetime bug set to 13:00 — this is **not** a UTC frame), and
`2026-01-09 17:00` at +100 but `2026-01-09 12:00` at +108/+116. `w3-o1.mpp`
(auto-scheduled) stores +104 == +96 and +112 = the late start, which is why the
reader's +96 read and the UI agree there. So the writer must not compute one
copy from another, and must know which pair is effective before claiming a
date change survived.

`TaskValues` therefore carries all six FixedData dates plus the Fixed2Data
copies, and `write_task_row`:

1. refuses a `TaskValues` with any date copy missing (0 / 0xFFFFFFFF) — there is
   no API for "just +96";
2. refuses a change that moves exactly one member of a family (start =
   {+96,+104,+112,Fixed2 +46/+50}, finish = {+100,+108,+116,Fixed2 +54},
   duration = {+84, Fixed2 +58}) while its siblings stay — the shape of edit §8.1
   measured Project ignoring;
3. keeps a copy a copy: a Fixed2Data word (or +186) that equalled its source
   before and that the caller did not override follows the new source value, so
   read → change one date → write moves it too;
4. refuses `actual + remaining != duration`, percent > 100, and a Fixed2Data
   size that is not `64 * (rows + 3)`;
5. writes FixedData through `store_task_fixed_data` (FixedMeta's size word stays
   in step) and Fixed2Data through `replace_stream` (same size, so the sectors
   are reused and the file length does not change);
6. re-reads the row through the container and throws if any value did not take;
7. returns warnings (not errors) for legal-but-suspicious values: a start after
   its finish in any frame, or a Fixed2Data duration that disagrees with +84.

`recalculate_write_path(doc, rows)` takes `PlannedRow { uid, TaskValues }` — plain
numbers, no `core/plan` or `core/mpp` dependency — writes each row, then requires
`check_table` to be empty (FixedMeta/Fixed2Meta size words, VarMeta's Var2Data
size, every block inside Var2Data).

The stage-2 setters that wrote a single copy (`TaskTable::set_start`,
`set_finish`, `set_duration`, `set_remaining_duration`) were **removed** so the
inconsistent edit cannot be expressed through the table API; `TaskTable::set_u32`
remains as the documented raw escape hatch, and `tests/inplace_patch.cpp` now
asserts that `write_task_row` refuses the same +96-only patch the byte patcher
performs.

### 10.2 What the test proves (`tests/roundtrip_row_write.cpp`, 299 checks)

On `corpus/probe/base.mpp`:

* **anchors** — `read_task_values` returns task A as +96 `0x3bf212c0`
  (2026-01-05 08:00), +100 `0x3bf627d8` (2026-01-09 17:00), +104 `0x3bf11e78`
  (2026-01-04 13:00), +108 `0x3bf61c20` (2026-01-09 12:00), duration 24,000,
  slack 0/0, Fixed2Data copies equal to +104/+108/+84.
* **all copies change together** — task A becomes 2026-03-02 08:00 → 2026-03-10
  17:00, project frame 2026-03-01 13:00 → 2026-03-10 12:00, duration 33,600.
  `read_task_values` reads every copy back; the raw bytes at +74/+84/+88/+96/+100/
  +104/+108/+112/+116/+186/+190 and Fixed2Data +46/+50/+54/+58 are asserted
  individually; the untouched row 2 is byte-identical; exactly **two** streams
  changed (`FixedData`, `Fixed2Data`); all `214/` streams are unchanged; the file
  size is unchanged; the container self-check and `check_table` are empty and the
  emitted file re-emits byte-identically; the saved file reloads to the same
  values.
* **refusals** — a partial `TaskValues` throws and names the missing copies
  (+104 among them); a full `TaskValues` with only +96 changed throws
  "single-copy start write refused"; a duration whose actual+remaining do not add
  up throws; in all three cases the document bytes are unchanged.
* **recalculation** — `recalculate_write_path` shifts all four rows (project row
  included) +14 days through all six copies. Read back, every copy moved exactly
  14 days, durations and slack are unchanged, +74/+186/+190 still hold 9600 on
  row 2, `check_table` is empty, two streams changed.

Independent readers, not our writer: `olefile` re-reads
`oracle-recalc.mpp` and prints the same rows (below), and `core/mpp/reader.cpp`
(a different stream's code, validated against COM) loads it with **0 warnings**
and reports project start 2026-01-18 13:00, finish 2026-01-27 12:00, A 7d
2026-01-19 08:00→2026-01-27 17:00, B 3d 01-19→01-21, C 2d 01-19→01-20, slack
0/19200/24000.

### 10.3 The oracle fixture: `core/mppwrite/build/vs/test-out/oracle-recalc.mpp`

**Superseded note (2026-10-22, later the same day).** The oracle run recorded
below was read from a stale build of this fixture (the saved file is 40 s newer
and its row 1 Start is `2026-01-18 13:00`); the JSON at
`docs/analysis/oracle/oracle-recalc.json` therefore describes the *recalculation*
(Project recomputed Duration from the dates it held) and not the fixture as it
now exists. The authority answer for manual tasks is §10.0 and the W26
single-variable fixtures; do not quote the "8160/6240/5760" run as a result for
this file.

Built by the test from `corpus/probe/base.mpp`; same 237,568 bytes, same stream
set, only `114/TBkndTask/FixedData` and `114/TBkndTask/Fixed2Data` differ.
Exactly what changed, per row (packed dates shown as they decode; "frame" =
+104/+108 and Fixed2Data +46/+50/+54):

| row | before | after |
|---|---|---|
| project (uid 0) | start 2026-01-04 13:00, finish 2026-01-09 12:00 (+96/+104/+112 and +100/+108/+116) | start 2026-01-18 13:00, finish 2026-01-27 12:00 in all three pairs |
| A (uid 1) | 5 d, 2026-01-05 08:00 → 2026-01-09 17:00, +108 2026-01-09 12:00, late 01-04 13:00 / 01-09 12:00, slack 0 | **7 d (33,600 ticks)**, 2026-01-19 08:00 → **2026-01-27 17:00**, +108 2026-01-27 12:00, late 01-18 13:00 / 01-27 12:00, slack 0 |
| B (uid 2) | 3 d, 01-05 → 01-07 17:00, slack 9,600 | 3 d, 2026-01-19 08:00 → 2026-01-21 17:00, +108 01-21 12:00, late 01-22 13:00 / 01-27 12:00, slack **19,200** |
| C (uid 3) | 2 d, 01-05 → 01-06 17:00, slack 14,400 | 2 d, 2026-01-19 08:00 → 2026-01-20 17:00, +108 01-20 12:00, late 01-23 13:00 / 01-27 12:00, slack **24,000** |

So: the project start moves forward exactly two weeks (Monday 2026-01-05 →
Monday 2026-01-19 local), task A grows from 5 to 7 working days, and B/C keep
their durations. Every date is written in all its copies, and TotalSlack is
written at +74/+190 and StartSlack at +186.

What the oracle run should report after `FileOpen` + `CalculateProject`:

```
project      Start 2026-01-19 (Monday, local)   Finish 2026-01-27
A  7 d   Start 2026-01-19 08:00   Finish 2026-01-27 17:00   TotalSlack 0
B  3 d   Start 2026-01-19 08:00   Finish 2026-01-21 17:00   TotalSlack 4 d
C  2 d   Start 2026-01-19 08:00   Finish 2026-01-20 17:00   TotalSlack 5 d
```

Three caveats, stated so the result is not over-read:

* The fixture is **internally consistent by construction**, which is the point
  (it is the edit §8.1 says should survive), but it cannot by itself say *which*
  copy Project reads: any of them now yields the same answer. An inconsistent
  fixture is the separate experiment, and §8.1 already has those
  (`pv-duration-both-tables` etc.).
* The project row's **duration is left at its stored 21,600 ticks (4.5 d)**. The
  rule that produces 21,600 for a 5-working-day project in `base.mpp` is not
  understood (spec/mpp-records.md §4 calls it an inference), so writing a new
  summary duration would be inventing. Project recomputes the summary row on
  load; if it shows 7 d that is the recalculation, not our value.
* +112 (late start) is written with the calendar-day convention `project start
  day + slack working days`, which reproduces base.mpp exactly but is
  **inference** for a slack that crosses a weekend (B and C here). It is a
  derived field; Project recalculates it, and Start/Finish/Duration do not depend
  on it.

While preparing this, a pre-existing fixture bug surfaced (not changed here,
`scripts/` is another stream's): `scripts/oracle_patch_variants.py` sets
`DATE_0302 = 0x3C2E12C0`, which is **2026-03-06**, not 2026-03-02 (§6). So the
`pv-start-*` fixtures on disk patched 2026-03-06; that does not change the §8
conclusion (Project reported the schedule's date either way), but the fixture
date should be fixed before anyone quotes it.

### 10.4 Decision: `214/` is not regenerated

**We do not attempt to regenerate `214/`, and this is a decision, not an
omission.** The evidence is §8.2, measured on Project's own Save As of the
untouched fixture: 25 of 96 streams are rewritten, and 12 of those 25 are under
`214/` — `CV_iew/Var2Data` (103 bytes differ inside a 100,178-byte stream),
`CEdl/Var2Data` and `CEdl`'s FixedData/VarMeta, all four `CFilter` streams (its
FixedData doubles, 230 → 460), `CGrouping/FixedData`, `CTable/FixedData`,
`CV_iew/FixedData` and `214/Props`. No stream is added or removed, so the shape is
stable, but the *content* is view/filter/grouping state that Project owns and
rewrites. There is no measured byte layout for any of it — we cannot write a
`214/` stream that is known to mean what we intend.

Therefore:

1. The writer carries `214/` through **byte-identically**, and every stage-3 and
   stage-4 test asserts that (per stream, not just "some bytes"). This is safe
   for a pass-through edit because the writer never adds or removes a row or a
   stream, so the row ids and table shapes the view state refers to still exist.
2. If a written file's view state is stale (a Gantt bar drawn from a cached
   date, say), Project's next Save As replaces it from its own model. That is the
   acceptable part: **Project repairs the view state; we must not guess it.**
3. It is **not** a viable strategy for a real save path (M2): a save that
   creates rows or tables would leave `214/` referring to records that no longer
   exist, and no amount of pass-through fixes that. Regenerating view state is a
   prerequisite for the save path, and it needs its own oracle fixtures (delete a
   row, add a task, change the view) before a single byte is written.
4. `214/` is also **validated content**: §8 records that a scratch u32 at
   `114/Props`+510 (a FAT-chained, not `214/`, stream) makes the file
   unopenable, which is why no test writes to a stream it does not understand.
   `214/` gets the same treatment — read, carry, never invent.

### 10.5 A correction found while adding stage 4: VarMeta was parsed 8 bytes off

`table.cpp` used to read `VarMeta` as a 32-byte header followed by
`(field, mid, offset)` entries and a 4-byte trailer, and to call field
`0x0b400564` "Task.Name". Both are wrong; the measured layout is a **24-byte
header followed by `(uid, offset, key)` entries and nothing after them**, and
Task.Name is key `0x0b40000e`:

* sizes: base `528 = 24 + 42*12`, PROJECT_A `12,996 = 24 + 1,081*12`, PROJECT_B
  `17,268 = 24 + 1,437*12`. With a 32-byte header and a 4-byte trailer none of
  those is a whole number of entries.
* the entry sequence in `base.mpp` decodes as `(0, 76, 0x0b40000e)` → the block
  that reads "base" (the project title), then `(0, 0, 0x0b4000a4)`, `(0, 12,
  0x0b4000a5)`, … Every task row starts with `(uid, offset, 0x0b40000e)` →
  "Task A"/"Task B"/"Task C".
* key `0x0b400564` exists once per row and holds 4 bytes (`ff ff ff ff`), i.e. it
  is a different field; it was being mistaken for the name.
* `core/mpp/reader.cpp` already parsed the 24-byte form and reproduces COM's
  names, which is why the earlier tests still passed: the 8-byte shift pairs
  entry *i*'s key with entry *i+1*'s uid/offset, and in `base.mpp` the last key
  of row *k* is immediately followed by the name entry of row *k+1*, so renaming
  uid 1 or 2 happened to repoint the right block. Renaming **uid 3 (the last
  row)** could not work at all: there is no following entry.

Fixed in `table.h`/`table.cpp`, with `tests/roundtrip_stream_replace.cpp` case B2
now renaming the last row and asserting, with an independent parse of VarMeta in
the test, that the `(uid 3, 0x0b40000e)` entry points at the appended block;
`read_task_name(doc, 0)` is asserted to return the project title "base".
`check_table` also checks Fixed2Data's size (`64 * (rows + 3)`) and Fixed2Meta's
size word, because the writer writes Fixed2Data.

**Open:** whether `0x0b400564` is a date or a duration (4 bytes, `ff ff ff ff` in
every row of `base.mpp`) is still unknown; it is carried through untouched.

### 10.6 Still open after stage 4

| question | what would settle it |
|---|---|
| Do written dates survive a `CalculateProject` when every copy agrees? | the oracle run on `oracle-recalc.mpp` (§10.3) |
| Which copy does Project read for Start/Duration? | an *inconsistent* fixture per copy (the §8.1 `pv-*` family), read back through COM |
| What is Fixed2Data +46/+50/+54/+58, semantically? | they move with the project dates in `var-projstart.mpp` and with the row's finish/duration in `var-duration.mpp`, but `w3-o1.mpp` stores 0 there. A baseline or a delete/restore row fixture |
| Does the project row's duration have a rule? | a project with a known summary duration (e.g. one task, one day) saved by Project |
| What is +112's convention for weekend-crossing slack? | a Project-saved fixture with fractional/weekend slack, compared with the file's +112 |
| Is it safe to write a file whose project row duration is stale? | the same oracle run: if Project shows the recalculated summary and keeps our dates, yes |

### 10.7 The working save path: pin the edit with SNET (measured 2026-10-22)

`w29-snet.mpp` (from `w3-o1.mpp`, uid 1) changes exactly two fields: row `+64`
u16 `0x0000 -> 0x0004` (ConstraintType SNET) and `+66` u32 `0xffffffff ->
0x3d1312c0` (2026-10-21 08:00). Every date/duration byte is unchanged. Project
opens it read-only with uid 1 Start **2026-10-21 08:00**, Finish 17:00, Early the
same, Late 10-26, slack 1440; the project recalculates to start 10-20 (the
earliest remaining task). So **a date edit can be stored as a real SNET
constraint**, and Project's own recalculation puts the task on that date.

The alternative — writing the seven date words Project's own save produces on a
manual task (`+104/+108`, `+112/+116`, `+174`, Fixed2Data `+50/+54`;
`w29-manual.mpp`) — moves Finish to 01-16 and duration to 5040 but leaves Start
at 01-04 13:00. Manual scheduling still needs the remaining save-state
differences (Fixed2Meta `+88`, dropping the StartText/FinishText VarMeta
entries) or a different model; do not claim manual-date writes survive.

**Semantics to state in the product:** a written date becomes a
start-no-earlier-than constraint. Project itself does that when a date is typed
into an auto-scheduled task (W3-O4 measured the ASAP -> SNET migration), so it
is a defensible edit model; a manual-scheduling write path is a separate,
unproved feature.

**Supersedes** the first three rows of §10.6 (the dates question, the copy
question, and Fixed2Data's semantics) with the measured split in §10.0 and this
section.

A second candidate that also sets Fixed2Meta entry `+88` to 0 and removes uid
1's StartText/FinishText VarMeta entries (`w29-manual2.mpp`) still reopens with
Start 2026-01-04 13:00 (Finish and Duration move). Manual scheduling therefore
remains open; `pin_task_start` (SNET) is the documented, measured save path.

§10.8 (wave 9) settles the manual half: the mode bit is `Fixed2Meta` entry byte
`+8` bit `0x80`, and clearing it is what makes the SNET pin authoritative.

### 10.8 The manual mode bit, and the save path that finally sticks (measured 2026-10-22)

Three fixtures built from `corpus/probe/base.mpp` (three tasks, all `Manual=True`
in the file) and opened read-only in Project; Project recalculates on open, and
`Project.Task.Manual` is read back. Each fixture differs from `base.mpp` only in
the bytes listed.

| fixture | bytes changed vs `base.mpp` | `Manual` after open | con/date | Start | Finish | Duration |
|---|---|---|---|---|---|---|
| `base.mpp` | — | True | 0 / NA | 2026-01-04 13:00 | 2026-01-09 12:00 | 2400 |
| `w37-snet-save.mpp` | row 1 `+64`=4, `+66`=SNET date (5 B) | **True** | **4 / 2026-10-21 08:00** | **2026-01-04 13:00** | 2026-01-09 12:00 | 2400 |
| `w37-snet-auto.mpp` | the same 5 B **plus** `Fixed2Meta` entry 4 byte `+8` `0xff->0x7f` (1 B) | **False** | 4 / 2026-10-21 08:00 | **2026-10-21 08:00** | 2026-10-27 17:00 | 2400 |

Facts (measured, `python scripts/oracle_task_fields.py <fixture>`, JSON under
`docs/analysis/oracle/task-fields-*.json`):

1. **`Fixed2Meta` entry byte `+8` bit `0x80` is the manual/auto mode switch.**
   Clearing it alone makes Project report `Manual=False` and schedule the row
   from its constraint. `core/mpp`'s `kF2MetaFlagManual` was an inference; it is
   now measured on two fixtures that differ only in that bit.
2. **SNET on a `Manual=True` row is stored but ignored.** Project keeps
   type 4 and the exact date in the file's model and still places Start at the
   manual date. This is why W29's manual fixtures never moved: the constraint
   was never the missing piece.
3. **SNET + the bit cleared is honoured.** Start lands on the pin, Duration is
   untouched (2400), Finish is recomputed through the task calendar (5 working
   days from Wed 2026-10-21 -> Tue 2026-10-27 17:00), and the project recalculates
   around it. The other two rows, left manual, do not move.

The supported date write is therefore: `save_date_edits` = **SNET pin at
`+64/+66` + clear the row's `Fixed2Meta` mode bit**, in one all-or-nothing
operation, followed by a re-read from disk (`core/mppwrite/save.h`).

**Product consequence, state it plainly:** the row becomes auto-scheduled. A
manual task's date cannot be changed while keeping manual scheduling with any
write path we have measured. `save_date_edits` warns on every row it converts.

**Limits (inference, unmeasured):** SNET is *no earlier than*, so for a row with
an FS predecessor Project places it at `max(predecessor finish, pin)`; an edit
earlier than the dependency allows will not be reproduced exactly. The
last-writer-wins assumption also applies to any `214/` cache (see §10.4). What
would prove or disprove the linked case: one fixture with an FS pair whose
successor start is pinned before its predecessor's finish, opened read-only.

## 11. Task row add and delete (wave 10, measured and Project-confirmed)

API: `core/mppwrite/add_delete.h` — `add_task_row` / `delete_task_row`. Full
measurements and the fixture diff: `docs/analysis/oracle/w10-oracle.md` §1.

### Add

A task add appends, it does not insert: `FixedData` +202 B (row at
`48 + 202*n`, `id = uid = new`), `FixedMeta` +47 B (new entry `+0 = 0`,
`+2 = 8`, `+4` = the row's FixedData offset) with header `entry_count` and
`fixed_data_size` updated, `Fixed2Data` +64 B (record at index `row + 3`,
`+16` double = record ordinal + 1, `+0` a fresh identity, `+24` the project
row's identity for a top-level task), `Fixed2Meta` +96 B (new entry, flags byte
`+8 = 0x7f` auto / `0xff` manual) with its header count/size, `VarMeta` +8
entries (`0x0b40000e` Name, `0x0b4000a4/a5/a8/a9/aa`, `0x0b400563/564`) with
header count and `var2_size`, and the eight Var2Data blocks. Project compacts
Var2Data on its own save and re-lays out block offsets; our writer appends the
blocks and points the new entries at them, which is self-consistent because
every block is length-prefixed and every VarMeta entry carries its offset.

The emitted row is a clone of the last live task row so every byte is a legal
value for this file; the writer clears the constraint (`+64/+66`) and start
anchor (`+174`), and `TaskValues` merges caller values through
`write_task_row` (all six date copies must be supplied together). A new task is
auto-scheduled by default, so stored dates alone do not survive Project's open
recalculation — the SNET path in §10.7 still governs date edits.

### Delete

Delete is a tombstone; no stream changes size:

1. `FixedMeta` entry `+0 = 0x02`, `+2 = 0`;
2. **`FixedMeta` header word +4 |= 0x02** (Project's own saves write 4 → 6).
   This is the gate: with the entry byte alone Project reopens the deleted row
   as a live task (measured on the first W45 fixtures); with the header bit,
   Project honours it. PROJECT_B's real file also carries `+4 = 6`;
3. later live rows' ID (`FixedData +0`) renumber down; Project recomputes IDs on
   open anyway;
4. the dead uid's VarMeta entries are removed and its Var2Data blocks are left
   orphaned. Project instead keeps the entries and re-keys them onto the next
   uid; both shapes reopen correctly — the cleaner removal is ours.

### Proof and limits

`mppwrite_roundtrip_add_delete` 493 checks and `mpp_tombstone_tests` 47 checks;
the coordinator opened every emitted fixture in Project read-only: adds reopen
as 4/5/4 tasks (priority preserved, percent/dates recalculated because the row
is auto-scheduled) and deletes as 2/2/3/4/4. `214/` is never written; link and
assignment rows cannot be added or removed, so a delete naming a live link or
assignment is refused unless `DeleteOptions::allow_dangling`; PROJECT_A cannot be
edited structurally because its `FixedMeta` size identity is already broken.
