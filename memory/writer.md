# memory/writer.md — the `.mpp` writer (W9, then W21 stage 4, then W26)

Last updated 2026-10-22. If you are picking this up cold, read this file, then
`spec/mpp-writer.md` (especially §8 measured oracle results and §10 stage 4), then
`docs/handoff/writer.md`. Do not re-derive the container or the copy map; both are
measured.

## Where things are

| path | what |
|---|---|
| `core/mppwrite/cfb_writer.{h,cpp}` | CFB parse + re-emit + `replace_stream` |
| `core/mppwrite/inplace.{h,cpp}` | `InPlacePatcher`: stream byte -> file byte, no rebuild |
| `core/mppwrite/table.{h,cpp}` | Task fixed rows, packed dates, append-only Var2Data, `check_table` |
| `core/mppwrite/row_writer.{h,cpp}` | **stage 4**: `TaskValues`, `read_task_values`, `write_task_row`, `recalculate_write_path` — writes every copy |
| `tests/roundtrip_cfb.cpp` | stage 1; check count scales with `corpus/probe` (688 with 46 `.mpp` files, 2026-10-22 14:13) |
| `tests/inplace_patch.cpp` | stage 2, 38 checks, incl. the stage-4 refusal cross-check |
| `tests/roundtrip_stream_replace.cpp` | stage 3, 520 checks, incl. case B2 (last-row rename, VarMeta layout) |
| `tests/roundtrip_row_write.cpp` | stage 4, 299 checks |
| `tests/roundtrip_authority.cpp` | **W26**: 9 single-variable copy-authority fixtures + manifest, 1045 checks |
| `spec/mpp-writer.md` | the write-up: facts, evidence, oracle results, §10 stage 4 + `214/` decision |
| `docs/analysis/writer/*.py` | container recon, assumption checks, olefile validation (W9's, unchanged) |

Build/test (MSVC, from the repo root; `--parallel` returns 1 with 0 errors):

```
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul && cmake -S core\mppwrite -B core\mppwrite\build\vs -G `"Visual Studio 17 2022`" -A x64"
cmd /c "call `"$vc`" >nul && cmake --build core\mppwrite\build\vs --config Release"
ctest --test-dir core\mppwrite\build\vs -C Release --output-on-failure
python docs/analysis/writer/validate_written.py
```

`core/mppwrite` **is** in the top-level build now (`foreach(stream_subdir mpp
sched mppwrite ...)` in `CMakeLists.txt`), so `ctest` from the root builds and
runs all five mppwrite tests. `ctest` is not on the plain PowerShell PATH here;
run it from the `vcvars64.bat` shell as above. Written fixtures land in
`core/mppwrite/build/vs/test-out/` (build tree, never `corpus/`).

## Verified (all re-runnable)

1. **Byte-identical container round trip for 21 files** (19 `corpus/probe/*.mpp`
   plus PROJECT_A and PROJECT_B). FAT/mini FAT/DIFAT/directory/header are *derived* from
   stored chains; `CfbDocument::check()` compares derived against stored FAT.
2. **In-place patching** of single bytes, both allocation paths (mini stream and
   FAT sectors), file length unchanged.
3. **Stream replacement**: same-size (byte-identical to in-place), mini-stream
   growth, mini→FAT migration, FAT growth on PROJECT_A, shrink-to-mini.
4. **Stage 4**: a row written with all copies changed reads back with every copy
   (`read_task_values` + raw byte asserts at +74/+84/+88/+96/+100/+104/+108/
   +112/+116/+186/+190 and Fixed2Data +46/+50/+54/+58); only FixedData and
   Fixed2Data change; `214/` untouched; `check_table` empty; container re-emits
   byte-identically; refusals fire for partial and single-copy writes.
   `tests/roundtrip_row_write.cpp`, 299 checks.
5. **Two independent readers agree on the written files**: `olefile` (Python) and
   `core/mpp/reader.cpp` (another stream, 0 warnings).
6. **W26 copy-authority fixtures** (`tests/roundtrip_authority.cpp`, 821 checks):
   seven files built from `base.mpp`, each changing exactly one copy family of
   row 1 (uid 1). The test proves single-variable-ness from the emitted bytes:
   every differing byte of the whole file must lie inside a targeted 4-byte
   word, every targeted word must have changed, and every other stream (214/
   included) is byte-identical. An independent `olefile` pass re-checked all
   nine: 9/9 clean, diff-byte counts 0/2/6/6/9/6/3/4/4 (scratch scripts
   `core/mppwrite/build/vs/w26_check.py` and `w26_rows.py`; build tree, not
   committed -- the C++ test is the re-runnable proof).

## Stage-4 facts (do not rediscover)

- A row's date lives in **three** FixedData pairs (+96/+100, +104/+108,
  +112/+116) and **four** Fixed2Data words (rec `row+3`: +46/+50 start, +54
  finish, +58 duration). Duration also lives at +84 and +88 (+80 actual).
  Slack at +74, +186, +190.
- **For a `Manual=True` task the model ignores almost all of that.** base.mpp
  uid 1 is Manual=True/ConstraintType=0: changing +96/+100, +104/+108,
  +112/+116 or +84/+88 leaves Project's reported Start/Finish/Duration/Slack
  unchanged (W26 oracle, 2026-10-22); only Fixed2Data +54 (the finish copy)
  takes effect. "Write every copy consistently" is therefore necessary but not
  *effective* -- see the W26 section.
- The pairs are **different fields, not different frames**: +96/+100 =
  Start/Finish, +104/+108 = EarlyStart/EarlyFinish, +112/+116 =
  LateStart/LateFinish (measured by the coordinator via COM on `w3-o1.mpp`, W26;
  see the W26 section). base.mpp task A stores Start 2026-01-05 08:00 / Finish
  2026-01-09 17:00, Early 2026-01-04 13:00 / 2026-01-09 12:00, Late = Early
  (zero slack). All six are **local wall clock**; the old "UTC-encoded frame"
  note here was wrong. `w3-o1` stores Early == Start. Never compute one copy from
  another: `read_task_values` returns the six values; change and write.
- `write_task_row` refuses: any missing date copy; a change touching exactly one
  member of a copy family; actual+remaining != duration; percent > 100;
  Fixed2Data size != 64*(rows+3); a read-back mismatch. Warnings for start after
  finish and for Fixed2Data duration != +84.
- Changing `duration` on a row read from the file needs
  `remaining_duration = kInherit` (and `fixed2_duration = kInherit` if the stored
  Fixed2Data word was not already a copy of duration), otherwise the family check
  refuses — deliberately.
- The project row (row 0) is **not** written to Fixed2Data: its date/duration
  words are 0xFFFFFFFF in base.mpp and resaved-base.mpp. Its duration (21,600 for
  a 5-day project) has no known rule and is left alone.
- **`214/` is not regenerated** (decision + evidence in spec §10.4): Project's own
  Save As rewrites 12 of its streams; there is no measured layout. Pass-through is
  acceptable only until a real save path exists.

## W26 (2026-10-22): the copy-authority fixtures

**The field names are measured now** (coordinator, COM read of
`corpus/probe/w3-o1.mpp` read-only; all six words matched six reported fields
exactly, uid 3 the clincher):

```
+96 Start       +100 Finish
+104 EarlyStart +108 EarlyFinish
+112 LateStart  +116 LateFinish
```

Project's COM date properties are **local wall clock**; pywin32's naive->UTC
conversion affects the *input* direction only. So the earlier "UTC-encoded
frame" reading of base.mpp's +104/+108 was wrong: 13:00/12:00 are wall-clock
times. base.mpp is internally odd -- its EarlyStart 2026-01-04 13:00 *precedes*
its Start 2026-01-05 08:00, and its project start is 2026-01-04 13:00 -- which
is why the authority question needs Project.

**Fixtures** (`core/mppwrite/build/vs/test-out/`, git-ignored, regenerated by
`mppwrite_roundtrip_authority.exe`; source `corpus/probe/base.mpp`, row 1 =
uid 1; intended new schedule Start 2026-01-12 08:00 -> Finish 2026-01-16 17:00,
still 5 working days / 24,000 ticks):

| file | change (packed u32, old -> new) | file bytes differing | what it tests |
|---|---|---|---|
| `w26-base.mpp` | none | 0 | baseline |
| `w26-96.mpp` | +96 0x3bf212c0->0x3bf912c0, +100 0x3bf627d8->0x3bfd27d8 | 2 | Start/Finish authority |
| `w26-104.mpp` | +104 0x3bf11e78->0x3bf912c0, +108 0x3bf61c20->0x3bfd27d8 | 6 | EarlyStart/EarlyFinish read? |
| `w26-112.mpp` | +112 0x3bf11e78->0x3bf912c0, +116 0x3bf61c20->0x3bfd27d8 | 6 | LateStart/LateFinish read? |
| `w26-fixed2.mpp` | Fixed2Data +46/+50 0x3bf11e78->0x3bf912c0, +54 0x3bf61c20->0x3bfd27d8 | 9 | is Fixed2Data read? |
| `w26-frame.mpp` | +104/+112 0x3bf11e78->0x3bf81e78, +108/+116 0x3bf61c20->0x3bfd1c20 | 4 | all Early/Late copies, base's own -19 h / -5 h deltas |
| `w26-dur84.mpp` | +84/+88 24,000->33,600 ticks | 4 | Duration authority |

Each variant changes **one copy family only**; the other 15 watched row words are
byte-identical to base.mpp, and so is every other stream (214/ included). The
test proves that from the emitted bytes: it diffs the whole file against
`base.mpp` and requires every differing byte to fall inside a targeted 4-byte
word. The new dates are exactly the base dates + 7 days (`+7<<16` on the packed
value), so duration, slack and the working-day shape are untouched.
`w26-manifest.json` carries, per file: role/note, FNV-1a 64, and every changed
word with stream offset, **file offset**, old/new u32 hex and the exact changed
file-byte offsets. `w26-frame.mpp` is the brief's "UTC-translated" F variant,
renamed because the COM measurement shows there is no UTC frame: it applies
base.mpp's measured +96->+104 (-19 h) and +100->+108 (-5 h) deltas to the new
dates and writes all four Early/Late copies.

**Oracle result, 2026-10-22** (`docs/analysis/oracle/w26-open.json`; read-only
open, Project's own load-time recalculation; reported strings are wall clock).
uid 1 is **Manual=True, ConstraintType=0 (ASAP)**:

| file | uid 1 model |
|---|---|
| `w26-base` | Start 2026-01-04 13:00, Finish 2026-01-09 12:00, Early = Late = same, Dur 2400 min (5 d), Slack 0 |
| `w26-96`, `w26-104`, `w26-112`, `w26-frame`, `w26-dur84` | **identical to base** |
| `w26-fixed2` | Start 01-04 13:00 (**unchanged**), Finish **01-16 17:00**, Dur **5040 min (10.5 d)**, Early/Late = (01-04 13:00, 01-16 17:00); project finish **01-16 17:00**, project dur 4800; B/C TotalSlack 960->3600 and 1440->4080 |

So for a **manually scheduled** task: +96/+100, +104/+108, +112/+116 and +84/+88
are all ignored/recomputed on load; **Fixed2Data +54 (the finish copy) is read**
and moves the finish, the task duration, the project finish and the successors'
slack. Start did not move even in `w26-fixed2`, where +46/+50 had also been
changed to 2026-01-12 08:00.

`w26-f2start.mpp` and `w26-f2finish.mpp` were added for that anomaly: E1 changes
only +46/+50 (Start should move iff the start words are read); E2 changes only
+54 (should reproduce E's finish movement on its own). Their results are the
open item.

**What the writer must do for a manual task (from this result):** write
Fixed2Data +54. Writing +96/+100, +104/+108, +112/+116, +84/+88 is, on this
evidence, ineffective on load. The manual task's Start store is *not*
+96/+100/+104/+108/+112/+116 and (pending E1) not +46/+50 either; candidates are
another Fixed2Data word we have not identified, a Var2Data block, or a
project-start fallback. Until it is found, a written file can move a manual
task's finish but not its start.

**The W21 "consistent write" is internally consistent but not effective.** It
made every copy agree, yet for a manual task Project reads almost none of them.
Only
Fixed2Data +54 reaches the model, and in `oracle-recalc.mpp` that word held the
frame finish, so the observed Finish 2026-01-27 12:00 came from +54, not from
+108. This also explains why the `pv-*` single-copy patches of +96/+104/+112 did
nothing.

**Raw fields vs model.** `Task.GetField(Duration/Start/...)` returns the
*stored* value as a date-only string ("Mon 5/01/26"), while `Task.Duration` and
`Task.Start` are the model's recomputed values: `w26-dur84` raw Duration
"7 days" vs model 5 days; `w26-fixed2` raw "5 days" vs model 10.5 days. Do not
use GetField to predict what the model will show.

**Prior oracle run, corrected.** `docs/analysis/oracle/oracle-recalc.json`
reports Start 2026-01-04 13:00, Finish 2026-01-27 12:00 and Duration
8160/6240/5760 minutes. That Start is the manual-task model value (also base's),
so it identifies no stored copy; the durations are exactly the inclusive
working-day counts between the reported model Start/Finish (17/13/12 days). The
fixture on disk differs and has an mtime 40 s after the JSON, so the run used an
earlier build of `oracle-recalc.mpp`: treat it as evidence about the
recalculation only.

## Corrections made (2026-10-22, W21)

- **VarMeta was parsed 8 bytes off** in `table.cpp`: the real layout is a 24-byte
  header plus `(uid, offset, key)` entries, nothing after them (base 528 = 24 +
  42*12; PROJECT_A 12,996 = 24 + 1,081*12; PROJECT_B 17,268 = 24 + 1,437*12). The old
  32-byte-header/4-byte-trailer reading worked by accident for uid 1/2 renames and
  could not rename the last row at all. `kTaskNameFieldId` is **0x0b40000e**
  (0x0b400564 is a different 4-byte field). `tests/roundtrip_stream_replace.cpp`
  case B2 covers it.
- `TaskTable::set_start/set_finish/set_duration/set_remaining_duration` were
  **removed** (single-copy writers). `set_u32` remains the raw escape hatch.
- `check_table` now also checks Fixed2Data = 64*(rows+3) and Fixed2Meta's size
  word.
- `memory/writer.md` used to say `core/mppwrite` was not in the top-level build;
  it is (checked `CMakeLists.txt` line 115 on 2026-10-22).

## Correction (2026-10-22, W26): the copy names

- **+96/+100 are Start/Finish; +104/+108 are EarlyStart/EarlyFinish; +112/+116
  are LateStart/LateFinish.** Measured, not inferred (COM read of `w3-o1.mpp`,
  all six fields matched the six words). The earlier reading in this file and in
  spec §10.1 -- that +104/+112 are "the same dates in a UTC-encoded frame" -- is
  wrong; the values are local wall clock. `TaskValues::project_start` /
  `project_finish` are therefore stale names for +104/+108 (= Early*); they were
  left as-is because `row_writer.h` is shared, but a successor should rename
  them. Caveat from the W26 oracle run: that mapping is for the *stored fields*
  on an auto-scheduled file (w3-o1, Manual=False); on a Manual=True task the
  model recomputes Start/Finish and does not read +96/+100 at load either.

## Open / blocked (as of 2026-10-22)

- **Oracle result landed for the seven original W26 files** (2026-10-22,
  `docs/analysis/oracle/w26-open.json`): B/C/D/F/G are indistinguishable from
  baseline; E moved Finish/Duration/project finish/successor slack but not
  Start. **Pending:** `w26-f2start.mpp` and `w26-f2finish.mpp` (added after that
  run) separate "Fixed2Data start words are read" from "ignored" and check that
  +54 alone reproduces E's finish movement. Manifest:
  `core/mppwrite/build/vs/test-out/w26-manifest.json`.
- **The manual task's Start store is unknown** and is the main open writer
  question: it is not +96/+100, +104/+108 or +112/+116, and (pending E1) not
  Fixed2Data +46/+50. Candidates: another Fixed2Data word (its 64-byte record
  has words we have not identified), a Var2Data block, or a project-start
  fallback. A writer cannot set a manual task's start until it is found.
  `w26_scan_copies.py` in the build tree lists every u32 equal to a packed date
  by stream/record offset if a successor wants the candidate list.
- Which copy Project reads for a *finish* is now measured: Fixed2Data +54.
  +84/+88 and the three FixedData date pairs are recomputed on load for a manual
  task; `GetField()` still exposes the stored words as date-only strings, so it
  must not be used to predict the model.
- `0x0b400564` (4 bytes, `ff ff ff ff` per row) is unidentified.
- `scripts/oracle_patch_variants.py` writes `DATE_0302 = 0x3C2E12C0` = 2026-03-06,
  not 2026-03-02. Not mine to change; the `pv-start-*` fixtures carry 03-06.
- Not implemented, deliberately: adding/removing directory entries, repairing
  PROJECT_A's damaged metadata, free-sector reuse, regenerating `214/`. Reasons in spec
  §5 and §10.4. Row add and delete DID land in W45 (`core/mppwrite/add_delete.h`,
  section at the end of this file); growing VarMeta's entry array is now part of
  `add_task_row`, not of `table.cpp`.

## Traps that cost time here

- The build intermittently fails with `LNK1104`/`MSB6003` on a `.tlog` in
  `core/mppwrite/build/vs` — the folder is under Dropbox and a sync/AV pass holds
  the file for a second. Retry the build; nothing is wrong with the code. Do not
  delete the build tree.
- `ctest --output-on-failure` hides the test's stdout on success; run
  `core\mppwrite\build\vs\Release\mppwrite_roundtrip_row.exe` directly to see the
  oracle-fixture summary it prints.
- MSVC `/W4` is clean; clang/GCC are stricter about unused locals and missing
  initializers, so initialise every struct member and every local.
- MSVC `/W4` *does* warn C4189 ("local variable is initialized but not
  referenced") for a range-for binding whose value is not read, and `/WX` turns
  it into an error. It caught that in `roundtrip_authority.cpp`; do not bind a
  loop variable you do not use.

## Next three actions

1. **Use `pin_task_start`** (W29, oracle-verified) as the writer's "set a date"
   operation: a real SNET constraint at +64/+66, Project recalculates on open and
   the date is honoured. Wire it into the save path instead of the FixedData
   copies. It does not preserve manual scheduling.
2. Get the `w29-manual2.mpp` oracle result (see the W29 section at the end). If
   Start still does not move, close the manual search: manual scheduling keeps
   its open status and SNET is the documented path.
3. Prerequisite for a real save path: add a directory entry (create a stream),
   which is also what regenerating `214/` would need.

## Coordinator oracle addendum (E1/E2 + Project's own manual save)

_Added by the coordinator after W26 finished; this supersedes the "Start store
unknown" wording above._

**E1/E2 result.** `w26-f2start.mpp` (Fixed2Data +46/+50 only -> 01-12 08:00,
+54 left 01-09 12:00) makes Project report **Finish 01-12 08:00**, Duration
2640 (5.5 d), Start unchanged 01-04 13:00. `w26-f2finish.mpp` (+54 only ->
01-16 17:00) reports **Finish 01-16 17:00**, Duration 5040 (10.5 d), Start
unchanged. So the Fixed2Data words are not a clean start/finish pair: Project's
reported Finish is the later of the changed F2 words in both cases, and the
Start never moves.

**Project's own edit (decisive).** Opened `base.mpp`, set `Task.Start` =
2026-01-12 08:00 on the manual task, CalculateProject, Save As
`corpus/probe/manual-start-edited.mpp`; Project reports Start 01-12 08:00,
Finish 01-16 17:00. What Project wrote in that save:
  * `+104/+108` -> 01-12 08:00 / 01-16 17:00  (effective Start/Finish)
  * `+112/+116` -> 01-12 08:00 / 01-16 17:00  (late = start here, slack 0)
  * Fixed2Data `+50/+54` -> 01-12 08:00 / 01-16 17:00
  * it LEFT `+96/+100` at 01-05 08:00 / 01-09 17:00 and Fixed2Data `+46` at
    01-04 13:00.
So `+96/+100` are a **calculated-schedule copy**, not the effective dates, and
`+104/+108` are the **effective Start/Finish** the UI reports. For auto-scheduled
files the two pairs are equal (w3-o1), which is why the reader's `+96` read
matches Project's PROJECT_A export; for a manual task they differ. `+46` is an
early/project-start copy that Project did not update.

**Why fixing only +104 did not move the Start (w26-104).** None of the individual
single-word fixtures moves Start, yet a Project-saved file with the same new
dates does. The row must look like Project's own save (the whole `+104/+108`,
`+112/+116`, F2 `+50/+54` set, and possibly a Var2 block) before Project trusts
it. That is a cross-copy consistency/source rule, not one field. The reference
to diff is `corpus/probe/manual-start-edited.mpp` vs `base.mpp`: every changed
byte in the task table is a candidate, and the 214/ rewrites in that save are
Project's normal view-state churn.

**Writer consequence.** Writing the six FixedData date words is not sufficient
for a manual task (W21's consistent write is internally consistent but not
effective). Two viable paths: (a) write a genuine `SNET` constraint with the
date, which pins an auto task (measured W3-O4) and is what typing a date into
an auto task does; or (b) reproduce Project's manual-save encoding exactly,
starting from the `manual-start-edited.mpp` diff. Do not claim dates survive
until one of those is opened and verified.

## W29 (2026-10-22): the manual-task write path and the SNET fixture

**ORACLE RESULTS (2026-10-22, coordinator; both files opened read-only in
Project): SNET works, the seven-word manual write does not move Start.**
`w29-manual2.mpp` is the last cheap manual candidate and was pending when this
was written. Details below.

### What was added (all in `core/mppwrite/`, tests in `tests/`)

- `table.h`: `task_row::kConstraintType` (+64, u16), `kConstraintDate` (+66,
  u32 packed date), `kStartAnchor` (+174, u32 packed date).
- `TaskValues` / `read_task_values` now carry `constraint_type`,
  `constraint_date` and `start_anchor`; `write_task_row` writes them
  (`kInherit` keeps the stored value). All five pre-W29 tests stayed green.
- **`write_manual_task_row(doc, row, ManualTaskWrite)`** -- the Project-shaped
  manual write: `+104/+108` = effective Start/Finish, `+112/+116` = Late*
  (`kInherit` copies start/finish, the zero-slack case), `+174` = Start,
  Fixed2Data `+50/+54`. `+96/+100`, `+84/+88`, Fixed2Data `+46/+58` and the
  constraint are left alone and are *verified* unchanged by a re-read; the
  function throws if any of them (or any effective word) reads back wrong. Row 0
  is refused. **Measured ineffective for Start (see oracle results); keep it for
  diagnostics / Finish experiments, not as a save path.**
- **`read_task_constraint` / `write_task_constraint`** -- +64/+66 only; refuses
  a non-zero type without a valid date *and* a valid date with type 0 (ASAP
  stores no date); verifies the whole rest of the row is unchanged.
- **`pin_task_start(doc, row, date)`** -- the supported "write a date" operation
  (oracle-verified 2026-10-22): write `date` as a real SNET constraint
  (ConstraintType = 4, ConstraintDate = `date`) at +64/+66 and nothing else.
  Project recalculates the row on open and the date is honoured; this does not
  preserve manual scheduling (the row is scheduled by the constraint, and an
  unconstrained task floats to the project start).
- **`tests/roundtrip_manual_start.cpp`** (target `mppwrite_roundtrip_manual`,
  **446 checks**; suite 6/6). Emits `w29-manual.mpp`, `w29-snet.mpp`,
  `w29-manual2.mpp` and `w29-manifest.json` under
  `core/mppwrite/build/vs/test-out/`. The manifest is per-word (stream offset,
  row offset, file offset, old/new, changed bytes).

### The fixtures

| file | source | bytes | FNV-1a 64 |
|---|---|---|---|
| `test-out/w29-manual.mpp` | `corpus/probe/base.mpp` | 237,568 | `0xbd66103027d02b88` |
| `test-out/w29-snet.mpp` | `corpus/probe/w3-o1.mpp` | 237,568 | `0x12dedd307d29264c` |
| `test-out/w29-manual2.mpp` | `corpus/probe/base.mpp` | 237,568 | `0x3235f109041f4ef8` |

`w29-manual.mpp` -- exactly Project's own word set ("deliverable 1"):

| stream | row off | file bytes | old -> new |
|---|---|---|---|
| FixedData | +104 | 129890-129892 | `3bf11e78` (01-04 13:00) -> `3bf912c0` (01-12 08:00) |
| FixedData | +108 | 129894-129896 | `3bf61c20` (01-09 12:00) -> `3bfd27d8` (01-16 17:00) |
| FixedData | +112 | 129898-129900 | `3bf11e78` -> `3bf912c0` (late = start, slack 0) |
| FixedData | +116 | 129902-129904 | `3bf61c20` -> `3bfd27d8` |
| FixedData | +174 | 129960-129962 | `3bf11e78` -> `3bf912c0` (start anchor) |
| Fixed2Data | +50 | 127666-127668 | `3bf11e78` -> `3bf912c0` |
| Fixed2Data | +54 | 127670-127672 | `3bf61c20` -> `3bfd27d8` |

21 changed bytes total; the only differing streams are FixedData and
Fixed2Data; `+96/+100` stay 01-05 08:00/01-09 17:00, `+84/+88` stay 24000,
Fixed2Data `+46` stays 01-04 13:00 and `+58` stays 24000.

`w29-snet.mpp` -- deliverable 2, from w3-o1 row 1 (auto, 1 d Mon 10-19):

| stream | row off | file bytes | old -> new |
|---|---|---|---|
| FixedData | +64 u16 | 129658 | `0000` -> `0004` (ConstraintType = SNET) |
| FixedData | +66 | 129660-129663 | `ffffffff` -> `3d1312c0` (2026-10-21 08:00) |

5 changed bytes; only FixedData differs; every date word and Fixed2Data word is
byte-identical. w3-o1's own uid 2/3/4 already store type 4 + a date at
+64/+66, so the encoding is copied from Project's file.

`w29-manual2.mpp` -- the last cheap manual candidate: the same seven date words
as `w29-manual.mpp` plus the two other semantic differences Project's own save
made, and nothing else:
- Fixed2Meta entry row+3 +88: `u32 0x0000000c -> 0x00000000` (1 byte);
- uid 1's VarMeta entries `0x0b400505` (StartText) and `0x0b400506`
  (FinishText) removed, declared entry count 42 -> 40, stream size still 528 B
  (the last 24 B are the stale tail of the old entry array), Var2Data untouched.

Confirming olefile diff of manual2 vs manual: only `Fixed2Meta` (1 byte) and
`VarMeta` (header count + the shifted entry array) differ; FixedData,
Fixed2Data and Var2Data are byte-identical; the row still reads the intended
dates. Project's own save additionally rewrites the project-row title block
(mid0 Name -> offset 464, `0x0b4004e4` block 16 -> 12 B, Var2Data 464 -> 508 B)
and 214/Props/metadata; those are file-title/save churn, not task date stores,
so manual2 deliberately keeps them out.

### Oracle results 2026-10-22

- **`w29-snet.mpp`: SUCCESS.** uid 1 reports Start 2026-10-21 08:00, Finish
  17:00, Early 10-21/17:00, Late 10-26/17:00, slack 1440; the project
  recalculated to start 2026-10-20 08:00 (earliest remaining task). Writing only
  +64/+66 makes the date stick through Project's own recalculation -- a real
  SNET constraint is the working save path.
- **`w29-manual.mpp`: FAILED to move Start.** Project reports uid 1 Start
  2026-01-04 13:00 (unchanged), Finish 2026-01-16 17:00, Duration 5040
  (10.5 d), project finish 01-16. The seven words Project itself wrote move
  Finish but not Start -- the same shape as W26's `w26-fixed2`. `+174` is not
  the manual Start authority by itself.
- **`w29-manual2.mpp`: pending** when this was written. If Project still reports
  Start 2026-01-04 13:00, stop: manual scheduling keeps its open status and SNET
  is the documented path.

### Measured facts found while building the fixtures

- A packed-date word moved by +7 days differs in **3** bytes, not 4: the day's
  high byte (the packed value's top byte) is unchanged. Assert bytes, not words.
- **+172 is a u16 flag** (1 on these rows); a u32 read at +172 overlaps +174 and
  "changes" when only +174 moves. The test asserts the u16. `+176` is the top
  half of +174's packed date, so writing +174 covers it.
- base.mpp `+174` = 01-04 13:00 on rows 1..3 (all Manual=True, project-start
  bound); w3-o1 auto rows store `0xFFFFFFFF` there.
- The Project save dropped uid-1's VarMeta entries `0x0b400505` and
  `0x0b400506` (StartText/FinishText) and rewrote VarMeta's header entry count
  42 -> 40, while **Var2Data is byte-identical** except for an appended
  project-title block (464 -> 508 B). It also changed Fixed2Meta entry row+3
  byte +88 (`u32 0x0c -> 0x00`) and churned row-identity bytes in Fixed2Data
  (+0..+5, +24..+29), FixedMeta/VarMeta and `114/Props` / the summary streams.
  `w29-manual2.mpp` now adds the Fixed2Meta +88 and the StartText/FinishText
  entry drop; the identity/214/Props churn is still deliberately not written.
- **Correction to the coordinator addendum above**: `0x0b400564` did *not*
  grow from 8 B to 20 B. The quoted 20-byte run
  `04000000ffffffff020000000000020000000000` is three consecutive Var2Data
  blocks -- 0x0b400564 (len 4) + 0x0b400505 (len 2) + 0x0b400506 (len 2) -- and
  it sits at Var2Data 178 in *both* files byte-identically. What Project removed
  is the two *VarMeta entries* (StartText/FinishText), not any block bytes.

### Verified vs inferred

Verified by the oracle: SNET at +64/+66 pins the date through Project's
recalculation (w29-snet). Verified locally and re-runnable: the emitted bytes
contain exactly the target words and nothing else; container check and
`check_table` are empty; reload is equal; rows other than 1 are untouched; all
`214/` streams are byte-identical; fixture hashes match the manifest.
Still open: whether the manual seven-word set plus the two extra edits
(`w29-manual2.mpp`) is enough for Project to move a Manual=True task's Start.
If it is not, the manual Start authority remains unknown and the documented save
path is `pin_task_start` (SNET).

### If manual2 fails

Stop the manual search: report the failure to the coordinator and keep manual
scheduling's open status. Do **not** rewrite the 0x0b400564 block (its bytes
already match Project's save) and do not chase 214/Props or the row-identity
churn -- those are save metadata, not date stores.



## Coordinator oracle: manual2 (final)

`w29-manual2.mpp` (the seven date words PLUS Fixed2Meta entry +88 = 0 and the
StartText/FinishText VarMeta entries removed) still reopens with uid 1 Start
2026-01-04 13:00, Finish 2026-01-16 17:00, Duration 5040. **Manual date writes
remain open.** The supported save path is `pin_task_start` (SNET + date at
+64/+66), which Project honours (`w29-snet.mpp`: uid 1 on 2026-10-21 08:00-17:00).

## W37 (2026-10-22): the save flow API, its tests, and the manual/auto oracle split

### ORACLE RESULT (coordinator, MS Project, read-only open) -- final for this wave

**SNET is honoured for an AUTO-SCHEDULED row, STORED-BUT-IGNORED for a
Manual=True row, and HONOURED for a manual row whose Fixed2Meta mode bit is
cleared (the row becomes auto-scheduled).**

- `w29-snet.mpp` (from `w3-o1.mpp`, auto): uid 1 reopened Start 2026-10-21
  08:00, Finish 17:00, Late 10-26, slack 1440. Project recalculated the project
  start to the earliest remaining task. SNET is the working date store.
- `w37-snet-save.mpp` (W37; from `base.mpp`, all three tasks Manual=True): uid 1
  reopened with **ConstraintType 4 and ConstraintDate 2026-10-21 08:00 read
  back exactly, Start unchanged 2026-01-04 13:00**, Finish 2026-01-09 12:00,
  Duration 2400, Early/Late unchanged, slack 0. So Project reads and keeps the
  constraint and still schedules the manual row at its own start. A manual
  row's date is not made effective by writing SNET.
- `w37-snet-auto.mpp` (W37, **MEASURED SUCCESS**): the same SNET plus the row's
  Fixed2Meta per-row flags byte bit 0x80 cleared (0xff -> 0x7f). Project
  reopened it with uid 1 **Manual=False**, Start **2026-10-21 08:00**, Finish
  2026-10-27 17:00, Duration 2400, project finish 10-27; the other two rows stay
  Manual=True at their own dates. So **Fixed2Meta entry +8 bit 0x80 is the
  manual/auto mode bit** (the reader's `manually_scheduled`; that inference is
  now measured), and **"SNET + clear the bit" is the save path that makes a date
  stick on a manual row too**. Manual scheduling itself is NOT preserved: the
  row becomes auto-scheduled.
- Same bytes, same hash: `--write-fixture-auto` now emits this file through
  `save_date_edits`, which is exactly the file the coordinator opened
  (FNV `0xc917b3380e0d660c`). `--write-fixture` is the negative control only
  (direct `pin_task_start`, manual bit kept, FNV `0xfabd15409e013b8c`).

### What was added (files)

| path | what |
|---|---|
| `core/mppwrite/save.h` | **new**: `DateEdit{uid, start}`, `SaveResult{rows_written, warnings}`, `save_date_edits`, `save_plan` -- the exact contract W38 codes against; top comment carries the oracle verdict above |
| `core/mppwrite/save.cpp` | **new**: validate -> copy -> apply -> prove only intended bytes moved -> save -> re-open from disk -> re-read (`read_task_constraint` + `read_task_values`) -> receipt in `SaveResult::warnings`; throws on any mismatch |
| `tests/roundtrip_save_flow.cpp` | **new**: corpus-free (tests/data/synthetic-base.mpp) + corpus (corpus/probe/base.mpp) + 7 refusal cases + `--write-fixture` / `--write-fixture-auto` modes |
| `core/mppwrite/CMakeLists.txt` | `save.cpp` in the library; ctest `mppwrite_save_flow`; `MPPWRITE_TEST_DATA` injected into every writer test; `MPPWRITE_PROBE_DIR` may now be overridden (used to prove the no-corpus path) |

### Behaviour (re-runnable)

- `save_date_edits`: refuses empty list, uid 0 / row 0, unknown uid, duplicate
  uid, invalid packed date, missing output directory, and a row with no FixedMeta
  entry (Project treats such a row as deleted, so the pin would be dead). Applies
  `pin_task_start` to a COPY of the doc; for a row whose Fixed2Meta flags byte has
  bit 0x80 set it ALSO clears that bit (the measured manual -> auto conversion,
  so the date sticks). Verifies FixedData changed only in the 6-byte constraint
  window (+64/+66) of the edited rows, Fixed2Meta only at those flag bytes, and
  no other stream at all; saves; re-opens; requires the reread bytes to equal
  what was written, `check()` and `check_table()` clean, the constraint +
  `read_task_values` to match, and a converted row's flag byte to read back
  clear. On success `doc` is replaced by the written document.
- Warning text for a converted row: "uid N: was manual; written as SNET and
  converted to auto-scheduled so Project honours the date (manual scheduling is
  not preserved, measured 2026-10-22)". An unreadable Fixed2Meta flag gets a
  "scheduling mode unknown" warning instead (pin written, not converted).
- `save_plan`: pre-resolves every planned row with `resolve_task_values` (the
  same refusals `write_task_row` has) before touching the file; uid 0 is legal
  (the project row); writes via `recalculate_write_path` on a copy; verifies only
  FixedData/Fixed2Data changed and only inside the planned rows; saves and
  re-opens/re-reads every row. Empty list is legal (writes unchanged, 0 rows).
- `SaveResult::warnings` carries the writer's genuine warnings plus an
  informational "re-read from disk: ..." receipt per edit (one summary line for a
  plan). This is the only reporting channel the fixed struct has; document it in
  any UI that shows warnings.

### Verification (exact, 2026-10-22)

Standalone (the tree that works even when another stream is mid-write):
```
cmake -S core\mppwrite -B build\vs-w37-mppwrite -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\vs-w37-mppwrite --config Release
ctest --test-dir build\vs-w37-mppwrite -C Release --output-on-failure -R "mppwrite|roundtrip"
```
Canonical top-level (run at 16:40 once core/render and core/mpp landed):
```
cmake -S . -B build\vs-w37 -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\vs-w37 --config Release --target mppwrite_save_flow mppwrite_roundtrip_authority mppwrite_roundtrip_cfb mppwrite_inplace_patch mppwrite_roundtrip_stream mppwrite_roundtrip_row mppwrite_roundtrip_manual
ctest --test-dir build\vs-w37 -C Release --output-on-failure -R "mppwrite|roundtrip"
```
Both give **7/7 passed** (six existing writer tests + `mppwrite_save_flow`), no
regression. `mppwrite_save_flow.exe` prints **101 checks, 0 failures** with corpus
present, **76 checks, 0 failures** with `MPPWRITE_PROBE_DIR` pointed at a
non-existent path (corpus case SKIPs; the no-corpus run is
`build\vs-w37-nocorpus\Release\mppwrite_save_flow.exe`).

Independent parser pass (olefile 0.47, scratch script in `build\`, not committed):
`w37-snet-save.mpp` differs from base.mpp in FixedData only, bytes
[314, 316, 317, 318, 319] 00 ff ff ff ff -> 04 c0 12 13 3d, and its Fixed2Meta
flags byte at 408 stays 0xff; `w37-snet-auto.mpp` is the same plus Fixed2Meta
offset 408 ff -> 7f and nothing else (7 checks, 0 failures).

Fixtures (git-ignored, regenerated by the test binary):
| file | source | bytes | FNV-1a 64 | change |
|---|---|---|---|---|
| `corpus/probe/w37-snet-save.mpp` | `corpus/probe/base.mpp` | 237,568 | `0xfabd15409e013b8c` | negative control: FixedData row 1 +64/+66 = SNET 2026-10-21 08:00, manual bit 0xff kept (direct `pin_task_start`) |
| `corpus/probe/w37-snet-auto.mpp` | `corpus/probe/base.mpp` | 237,568 | `0xc917b3380e0d660c` | save-flow fixture: that plus Fixed2Meta entry 4 +8 bit 0x80 cleared (0xff -> 0x7f), through `save_date_edits` |

Commands: `...\mppwrite_save_flow.exe --write-fixture` (negative control) and
`--write-fixture-auto` (the save flow's own fixture; byte-identical to the file
the coordinator opened successfully).

### Manual-scheduling verdict: **pin + convert works; manual scheduling itself is not preserved**

Measured: a date edit written as SNET alone reopens at the row's own start for a
Manual=True row (`w37-snet-save.mpp`), while the same edit plus a cleared
Fixed2Meta mode bit reopens with `Manual=False`, Start on the SNET date and Finish
recalculated (`w37-snet-auto.mpp`). `save_date_edits` therefore always does
"pin + clear the mode bit" for a manual row and says so in a warning. This makes
a date stick on every task type, at the cost of converting the row to
auto-scheduled. What is still NOT proved: that a *manually scheduled* row (mode
bit set) can have its Start moved while staying manual -- the seven-word set
(W29 manual/manual2) and SNET alone all failed; no further candidate is known.
Nobody needs that for the app's save path anymore.

### Open / next three actions

1. Unmeasured, flagged in save.h: SNET is "no earlier than", so a linked successor
   may reopen at max(predecessor finish, pin) rather than at the pin. Build
   `w37-snet-link.mpp` (auto FS pair, pin the successor's start BEFORE its
   predecessor's finish) to measure it; until then do not claim exact-date writes
   for linked rows.
2. W38/writer glue: `save_plan` for "write the recalculated plan";
   `save_date_edits` for a single Start edit, surfacing `SaveResult::warnings`
   (the conversion warning + the re-read receipt) in the UI. If the app must
   preserve manual scheduling, it needs a different feature, not a writer tweak.
3. When the coordinator merges: note that the canonical `build\vs-w37` writer
   ctest now matches 7 tests (`-R "mppwrite|roundtrip"`), not 8; the other wave-9
   suites live under different names.

### Traps added this wave

- Top-level configure fails while ANOTHER stream is mid-write (`core/render`
  missing report.cpp/render_test_main.cpp, then `core/mpp` missing
  tests/mpp_perf_large.cpp). That is a transient of the shared tree, not a bug
  here: retry, or verify the standalone `core\mppwrite` build meanwhile. Do not
  edit another stream's CMakeLists to work around it. Both landed by ~16:40.
- `Fixed2Meta` entries are paired with `FixedMeta` by ENTRY INDEX, not array
  position: locate the entry through FixedMeta's +4 FixedData-offset word. Entry
  4 is row 1 in base.mpp; flags at entry +8 (stream offset 408 for row 1). Bit
  0x80 is now MEASURED as the manual/auto mode bit (w37-snet-auto open).
- A manual row is converted, not refused, on a date edit: the warning is how a
  caller learns that the row changed scheduling mode. Do not turn it into a
  refusal without checking the app's edit model first.
- The conversion must be part of the same "only intended bytes changed" check:
  Fixed2Meta's flag byte is an allowed change (exactly the edited manual rows),
  and a re-read must show it clear. If Fixed2Meta is unreadable the row is pinned
  without conversion and warned about.
- `MPPWRITE_PROBE_DIR` is now a cache-overridable variable so the no-corpus path
  can be exercised on a machine that has corpus/; CI does not need it.
- Two fixture modes, two purposes: `--write-fixture` (negative control, direct
  `pin_task_start`) must NOT be switched to `save_date_edits`, or the evidence
  for "SNET alone is ignored on a manual row" stops being regenerable.

## W45 (2026-10-22): row add and delete, plus the reader's tombstone fix

**Status: ORACLE-CONFIRMED 2026-10-22 (coordinator, read-only Project open).**
Add is accepted (4 / 5 / 4 tasks for add / add2 / add-values; priority 600
survived, percent re-read as 0 and dates recalculated because the new row is auto
— expected). Delete was NOT honoured by the first fixture set; the gate was found
and fixed: **FixedMeta header word +4 must be OR-ed with 0x02** ("this table
contains deleted rows"). Both delete variants re-opened with Project as 2 tasks
(durations 2400 + 960), so the per-entry tombstone alone is not enough and the
retained-VarMeta churn is not needed. The shipped delete path sets the header bit
and keeps the cleaner VarMeta-removal shape (variant A).

### What landed (files)

| path | what |
|---|---|
| `core/mppwrite/add_delete.h` | **new**: `NewTaskRow`, `AddedRow`, `DeleteOptions`, `DeletedRow`, `add_task_row`, `delete_task_row`. The header carries the measured design, the clone semantics and every policy. |
| `core/mppwrite/add_delete.cpp` | **new**: validation, append/tombstone, save, re-open from disk, re-read every written fact. |
| `tests/roundtrip_add_delete.cpp` | **new** (`mppwrite_roundtrip_add_delete`): **493 checks, 0 failures** with the corpus present; synthetic-only cases always run. Emits 11 fixtures (9 behaviours + the two delete variants) + `w45-manifest.json` (see below). |
| `core/mpp/reader.cpp` | **liveness fix** (the one permitted cross-lane change): the Task predicate now requires a FixedMeta entry with `+0 == 0` AND a VarMeta group. PROJECT_A/PROJECT_B stay 83/73 (`mpp_reader_tests`: 663 checks). |
| `tests/mpp_tombstone_tests.cpp` | **new** (`mpp_tombstone`): 47 checks. Control = committed `tests/data/synthetic-base.mpp`; Project's `w10-adddel-back/del` and the writer's own fixtures are used when present. |
| `core/mpp/CMakeLists.txt` | additive: the `mpp_tombstone_tests` target and ctest registration. |
| `core/mppwrite/CMakeLists.txt` | `add_delete.cpp`; the new test; links `core_mpp` and defines `MPPWRITE_HAVE_READER` only when the top-level build provides the reader; `MPPWRITE_FIXTURE_MIRROR`. |

### Measured design actually written (do not re-derive)

Add appends one row: FixedData +202 at `48+202*n` with `id = uid = new`; FixedMeta
+47 with `+0=0`, `+2=8` (the row's variable-field count), `+4` = new row offset,
header `+8` count +1 and `+12` = new FixedData size; Fixed2Data +64 at record
`row+3` with `+16` f64 = `row+1`, a fresh 16-byte identity and `+24` = the project
row's identity; Fixed2Meta +96 with `+4` = record offset, `+8` = `0x7f` (auto) or
`0xff` (manual), header counts/sizes bumped; VarMeta +96 = 8 entries in the order
Name, `0x0b4000a4/a5/a8/a9/aa`, `0x0b400563`, `0x0b400564`, with header `+8`
declared count +8 and `+0x14` = new Var2Data size; Var2Data gets the 8 payload
blocks appended at the end. Project compacts Var2Data on its own add; we do not.

Correction to the oracle prose, found by dumping `w10-adddel-add.mpp`: the new
Fixed2Data record's `+24` is the **project row's** identity (record 3), not "the
previous record's id0" -- the two are the same only when the previous record is
the project row, which it is not for a 6-row file. Implemented as "parent =
project row", which is also what the reader's outline walk expects for a
top-level task.

Delete sets FixedMeta entry `+0 = 0x02`, `+2 = 0` AND the FixedMeta HEADER word
`+4 |= 0x02`; keeps every stream's byte size; renumbers later live rows'
`FixedData +0` down by one; removes the deleted uid's VarMeta entries (both `uid`
and `0x80000000|uid`) and leaves its Var2Data blocks orphaned;
Fixed2Data/Fixed2Meta/Var2Data must read back byte-identical.

The header word is the gate, measured twice: Project's own files carry
`+4 = 0x00000004` with no tombstone (base, w10-adddel-base/add, PROJECT_A,
synthetic-base) and `0x00000006` as soon as a row is deleted (w10-adddel-back/-del,
and PROJECT_B with its 12 deleted slots); and the coordinator's read-only open of the
first W45 delete fixtures showed Project **still listing the deleted task** until
the bit was set. Project's own delete also keeps the deleted uid's VarMeta entries
and only decrements the declared count at VarMeta `+8` (back: 59 entries, declared
51; del: 59 entries, declared 43). That retention is NOT needed: both
`w45-del-middle-a.mpp` (+4=6, entries removed, declared 31) and
`w45-del-middle-b.mpp` (+4=6, all 42 entries kept, declared 31) reopen as 2 tasks.
The shipped path is A (removal) because it leaves VarMeta internally consistent.

### API semantics (ours, documented in add_delete.h)

- `NewTaskRow::name` is required; everything else is optional and merged onto the
  cloned last-live-task row through `TaskValues`' `kInherit` sentinels.
- uid: 0 = allocate `(largest uid in the table, tombstones included) + 1`; an
  explicit uid must be non-zero and unused. A tombstoned uid is never reused.
- ID: `(largest live task ID) + 1`. In an append-after-delete file this can equal
  the tombstone's raw ID; Project's own `-del` file does exactly that (uid 4
  tombstone and uid 5 both carry raw ID 4), so it is deliberate.
- The new row clones the last live task's 202 bytes, then clears the constraint
  (`+64=0`, `+66=NA`) and start anchor (`+174=NA`). Progress/duration/dates are
  cloned; supply `TaskValues` to override. Scheduling mode defaults to auto
  (`0x7f`), which is what Project's measured `Tasks.Add` row was even in a file
  whose other rows were manual.
- `TaskValues` overrides go through `write_task_row`, so moving a date means
  supplying all six date copies; one-member changes are refused by that function.
- The new Fixed2Data record's has-children flag (byte +42 bit 0x40) is cleared;
  the rest of the template's flags byte is kept.
- Delete refuses uid 0, an unknown uid, a row with no FixedMeta entry, an
  already-tombstoned row, a row with no VarMeta group, and a broken table size
  identity. By default it also refuses a delete that leaves a live link or
  assignment naming the uid; `DeleteOptions::allow_dangling = true` proceeds and
  reports the orphaned rows in `DeletedRow::warnings`. Rationale: the writer
  cannot remove link/assignment rows and a link is scheduling-visible. Real files
  tolerate orphan assignments (PROJECT_B: 10 of 73 name deleted task uids).

### Verification (exact, re-runnable)

```
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S . -B build\vs-w45 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build\vs-w45 --config Release --target mppwrite_roundtrip_add_delete mpp_tombstone_tests mpp_reader_tests mppwrite_save_flow plan_recalc_tests'
.\build\vs-w45\core\mppwrite\Release\mppwrite_roundtrip_add_delete.exe   # 493 checks, 0 failures
.\build\vs-w45\core\mpp\Release\mpp_tombstone_tests.exe                  # PASS: 47 checks
.\build\vs-w45\core\mpp\Release\mpp_reader_tests.exe                     # PASS: 663 checks, PROJECT_A 83 / PROJECT_B 73
.\build\vs-w45\core\plan\Release\plan_recalc_tests.exe                   # M2 332/332, 231 checks
ctest --test-dir build\vs-w45 -C Release --output-on-failure -R "mppwrite|mpp_reader|tombstone|plan_recalc"   # 11/11
```

`plan_recalc_tests` rewrites `tests/data/project_a_recalc_golden.tsv` on every run (its
own design); it produced the same 332/332 all-match table.

### Fixtures (git-ignored; regenerated by the test binary)

| file | bytes | FNV-1a 64 | meaning |
|---|---|---|---|
| `w45-add.mpp` | 242,176 | `0xd8229d8a0434b41d` | synthetic-base + uid 4 (clone of uid 3), auto — Project: 4 tasks |
| `w45-add2.mpp` | 246,272 | `0x657a6a8ebab4a82a` | + uid 4 and uid 5 — Project: 5 tasks |
| `w45-add-values.mpp` | 242,176 | `0x7d04658a9660c4ca` | + uid 4 with all six date copies +7 d, priority 600, 25 % — Project: 4 tasks, priority kept, percent re-read 0 (auto row) |
| `w45-del-middle.mpp` | 237,568 | `0x36f46490082c5f3d` | tombstone uid 2 (middle), +4=6, 11 VarMeta entries removed, uid 3 ID 3->2 — Project: 2 tasks |
| `w45-del-middle-a.mpp` | 237,568 | `0x36f46490082c5f3d` | variant A (byte-identical to `w45-del-middle.mpp`) — Project: 2 tasks |
| `w45-del-middle-b.mpp` | 238,080 | `0x0694ff478c8d124e` | variant B: +4=6, all 42 VarMeta entries kept, declared 31 — Project: 2 tasks |
| `w45-del-last.mpp` | 237,568 | `0x12aa98789821ce9e` | tombstone uid 3 (last), +4=6 — Project: 2 tasks |
| `w45-add-before-delete.mpp` | 242,176 | `0x7657ad730b3d5bd8` | intermediate: add uid 4 |
| `w45-add-back.mpp` | 242,176 | `0x2f0e78a63d20e533` | add uid 4 then delete it, +4=6 — Project: 3 tasks |
| `w45-append-after-delete.mpp` | 246,272 | `0xa6810cdae03347a1` | add 4, delete 4, append 5, +4=6 — Project: 4 tasks (uids 1,2,3,5) |
| `w45-del-probe-middle.mpp` | 240,128 | `0xb1c267c5d1a09b5f` | from `corpus/probe/w10-adddel-base.mpp`: delete uid 2, +4=6, `allow_dangling` — Project: 4 tasks |

Paths: emitted to `build\vs-w45\core\mppwrite\test-out\` and mirrored to
`core\mppwrite\build\vs-w45\test-out\` (the brief's path). A `w45-manifest.json`
in both lists size + FNV per file.

### Verified vs inferred

Verified locally/independently: the emitted fixtures' only differing streams are
the six Task table streams (add) or FixedData/FixedMeta/VarMeta (delete); every
`214/` stream is byte-identical; all size/count words agree (`FixedMeta +4` is
asserted: 4 untouched on add, 6 after every delete); `check()` and
`check_table()` are clean; the file re-emits byte-identically; the writer's own
re-open and the independent `core/mpp` reader both return the intended
counts/uids/IDs; `olefile` 0.47 reproduces the stream diff and the tombstones.

Verified by the oracle (coordinator, read-only Project open, 2026-10-22):
add / add2 / add-values reopen as 4 / 5 / 4 tasks (priority 600 survived; the
auto row's percent re-reads as 0 and the dates recalculate, as expected);
`w45-del-middle-a` and `w45-del-middle-b` both reopen as 2 tasks with durations
2400 + 960; all five delete fixtures now carry `FixedMeta +4 = 6`.

Still inferred: the clone semantics for a row added to a non-fresh file
(progress, milestone blob, whether the new row should be auto) and the fact that
our deterministic Fixed2Data identity and cloned FixedMeta blob are accepted
(measured only insofar as Project reopened the fixtures without complaint).

### Open items / next actions

1. **Re-open the five regenerated delete fixtures** (`w45-del-middle`,
   `w45-del-last`, `w45-add-back`, `w45-append-after-delete`,
   `w45-del-probe-middle`): they now carry `FixedMeta +4 = 6`; expected Project
   counts are 2, 2, 3, 4, 4. `w45-del-middle-b.mpp` is the retained-entry control
   and is not needed by the ship path.
2. Add-side discriminators are no longer needed (accepted). If a future file is
   rejected, attack it in this order: the deterministic Fixed2Data identity at
   +0, the +24 parent identity, the cloned FixedMeta blob, then the uncompacted
   Var2Data layout.
3. Spec refresh is the coordinator's: `spec/mpp-writer.md` §5 item 2 ("adding a
   table row") and §10.4 item 3 now describe W45's `add_delete.h` instead of a
   non-feature; this file and `docs/handoff/w45.md` carry the design meanwhile.
4. `delete_task_row` does not remove link/assignment rows; a future stream that
   can tombstone those should take over the `allow_dangling` policy.

### Traps added this wave

- **Dropbox mtime churn on every edit.** The file-observation policy rejected a
  second `edit` on the same file almost every time ("file changed since it was
  read") even with no other writer. Workaround: re-read immediately before each
  edit, one edit per read.
- A built `release` exe path from an older wave is not reused: W45's targets live
  under `build\vs-w45`, and ctest fails "Not Run" for any writer test target not
  built in that directory. Build all six writer targets before running the
  filtered ctest.
- `mpp_reader_tests` prints PROJECT_A/PROJECT_B counts first; use them as the gate
  (83/73), not the check total.


## W48 (2026-10-22): the delete cascade, custom-field values, linked-pin contract

**Status: cascade and Flag1 PASS the Project gate; custom-field values are
byte-identical to Project's own definition tables but the confirming re-open is
pending (two earlier opens showed zeros).** The writer tombstones dependent
link/assignment rows on a task delete, and writes Text/Number/Date/Duration/Flag1
custom-field values together with their `Props` field registrations. The
linked-pin clamp is documented and exposed as a predicate; scheduling semantics
are unchanged. `core/mpp` gained one additive read API (custom fields) and was
otherwise frozen for W48.

**Project gate so far (coordinator, read-only):**
- `w48-del-cascade.mpp` **PASS** — 2 tasks (uids 1,3), uid 1 unchanged, uid 3 at
  project start, no link/assignment naming uid 2.
- `w48-flag-on/off` **PASS** — uid 2 Flag1 Yes / all No; only the FixedMeta
  bit + Fixed2Meta mirror matter for the flag.
- Text1/Number1/Number2/Duration1/Date1: **FAILED twice** — first because the
  VarMeta entries were appended outside uid 1's group, then because the field
  definitions were not registered in `TBkndTask/Props`.  Both causes are fixed
  and both Props streams match Project's own file byte-for-byte.  The third
  measured difference (FixedMeta tail presence bits + the internal Duration
  marker, below) is also implemented, and `w48-fields.mpp`'s uid 1 row now matches
  Project's own six-field save (`corpus/probe/w11-fields-base.mpp`) entry for
  entry and byte for byte on VarMeta order, tail bytes and `+2`; re-open pending.


### Measured rules implemented (do not re-derive)

**Delete cascade** (`docs/analysis/oracle/w11-oracle.md` §2, fixture
`w11-linkdelete-post.mpp`). Project's own delete of T2 in a T1->T2->T3 file with
one T2 assignment leaves every dependent row in place and tombstones it:

| table | what Project writes |
|---|---|
| `TBkndCons/FixedMeta` | header `+4 4->6`; BOTH links (1->2 and 2->3) entry `+0 00->02`, `+2 ->0`; FixedData byte-identical |
| `TBkndAssn/FixedMeta` | header `+4 4->6`; the T2/R1 entry `+0->02`, `+2->0`; FixedData byte-identical (survivors' cached values recalculated, not copied) |
| `TBkndTask/FixedMeta` | header `+4 4->6`; the task's entry `+0->02`, `+2->0` |

Selection: a Link row is selected when predecessor (+4) OR successor (+8) is the
uid; an Assignment row when task uid (+4) is. Rows whose entry is already a
tombstone are left alone. No stream shrinks. Our `w48-del-cascade.mpp` has
**byte-identical `TBkndCons/FixedData` and `TBkndCons/FixedMeta` to Project's
`w11-linkdelete-post.mpp`** (checked directly); `TBkndAssn/FixedMeta` differs
only at the two surviving entries' `+2` field-count byte (Project's 2->3 save
churn), and `TBkndAssn/FixedData` differs only in Project's recalculated cached
values.

**Custom-field storage** (`w10-oracle.md` §2, `w10-fieldvalues.mpp`):
Text = VarMeta block UTF-16LE+NUL; Number = 8-byte f64; Date = 4-byte packed u32;
Duration = 4-byte u32 file ticks (COM minutes x 10); Flag1 = Task FixedMeta entry
byte **+24 bit 0x02** (not VarMeta), with Project's own Fixed2Meta mirror at entry
bytes **+31/+61 bit 0x80** (flag-off -> flag-on diff). FixedMeta entry `+2` is the
row's exact VarMeta entry count (verified on synthetic-base: 11 entries/11, and
on w10-fieldvalues: 14/14), so adding a field entry increments `+2` by one; a flag
write does not.

**Field ids** (PjTask, `analysis/field_id_map.tsv`; one table duplicated in
`core/mpp/field_value.h` and `core/mppwrite/field_values.{h,cpp}` because the
writer does not link the reader; the field-values test cross-checks all 100 ids):

```
Text1..6  0x0b400033 + 3*(n-1)      Text7..10  0x0b400042 + (n-6)
Text11..30 0x0b40013d + (n-11)
Flag1..10 0x0b400048 + (n-1)        Flag11..20 0x0b400124 + (n-11)
Number1..5 0x0b400057 + (n-1)       Number6..20 0x0b40012e + (n-6)
Duration1..3 0x0b400067 + (n-1)     Duration4..10 0x0b400113 + (n-4)
Cost1..3 0x0b40006a + (n-1)         Cost4..10 0x0b400102 + (n-4)
Date1..10 0x0b400109 + (n-1)
```

Watch the Text family: **Text1..6 stride 3, then Text7..10 contiguous** (0x43,
0x44, 0x45, 0x46) because Flag1 starts at 0x48. The first cut assumed stride 3
through Text10 and collided with Flag1/2/3; the id round-trip test caught it.

**VarMeta group placement (measured, W48 gate #1).** Project only honours a
custom-field entry that sits inside its uid's CONTIGUOUS group; entries appended
after other uids' groups are ignored (the file reopened with every value empty).
The array is sorted by (uid, secondary family, **field key ascending**), and our
`table.h::var_entry_insert_offset`/`insert_var_entry` already produce that order
(verified against `w11-fields-base.mpp`): uid 1 becomes Name, Text1(0x33),
Number1(0x57), Number2(0x58), Duration1(0x67), a4..aa, 0xb7, Date1(0x109), 4e5,
StartText, FinishText, 563, 564.  `add_task_row` uses the same helper, so a
mid-range caller-supplied uid is inserted in uid order rather than at the end.

**The field registry: `114/TBkndTask/Props` + `114/TBkndAssn/Props` (measured,
W48 gate #2).** A VarMeta value is not enough: Project shows a custom field only
when its definition is registered in the Task table's Props, mirrored in the
Assignment table's Props. Project's own file has Task/Props 614 B = 86 + 6*88
and Assn/Props 616 = 88 + 6*88; committed synthetic-base has the preambles
(86 / 88) only. Records are 88 bytes copied from that file (Task offsets
86,174,262,350,438,526; Assn 88,176,264,352,440,528). The preamble words differ
per table and are the whole trick:

| word | Task/Props (86 B preamble) | Assn/Props (88 B preamble) |
|---|---|---|
| +0, +4 | u32 size-4 | u32 size-4 |
| total 44+88n | u16 **+30** (high half of u32 +28) | u32 **+32** |
| record count | u32 **+78** | u32 **+80** |
| count*88 | u32 **+82** | u32 **+84** |

Everything else in the preamble is unchanged. The coordinator's first recipe
(+78/+82 for both, +28 u32 for both) is the Task layout only; Assn differs, and
using it leaves Project ignoring the definitions. Both of our emitted Props
streams are now **byte-identical** to Project's own file, which the field test
asserts directly when corpus/ is present.

**FixedMeta tail presence bits + the Duration marker (measured, W48 gate #3, on
`w11-fields-base.mpp` and the single-field probes).** Even with VarMeta and Props
byte-perfect, Project shows a custom field as empty unless the row's FixedMeta
entry tail says the field has a value:

| field | entry tail |
|---|---|
| Text1 0x0b400033 | `+21 |= 0x20` |
| Flag1 0x0b400048 | `+24 |= 0x02` (the bool itself; False clears it) |
| Number1 0x0b400057 | `+25 |= 0x08` |
| Number2 0x0b400058 | `+25 |= 0x10` |
| Duration1 0x0b400067 | `+26 |= 0x01`, `+27 |= 0x20`, plus VarMeta key 0x0b4000b7 |
| Date1 0x0b400109 | `+29 |= 0x40` |

The six-field tail is `+21=0x20, +24=0x02, +25=0x18, +26=0x01, +27=0x20,
+29=0x40`.  The Duration marker's block is 2 bytes `07 00`; it is a normal
VarMeta entry at key 0x0b4000b7, inserted in the same ascending-key order (it
lands after the overtime entries, before Date1/4e5).  The entry's `+2` u16 is the
row's exact VarMeta entry count INCLUDING the marker (base 11 -> 17 with the six
fields) and is recomputed from the array after the write.  `w48-fields.mpp`'s uid
1 VarMeta order, tail bytes and `+2` all equal `w11-fields-base.mpp`'s.


### What landed (files)

| path | what |
|---|---|
| `core/mppwrite/add_delete.h/.cpp` | the cascade: `DeletedRow::link_rows_tombstoned` / `assignment_rows_tombstoned`, per-table entry tombstoning, header flag in each table, byte-level verification from the re-read file. `DeleteOptions::allow_dangling` now only covers a **pre-existing** dead link endpoint. |
| `core/mppwrite/field_values.h/.cpp` | **new**: `FieldKind`, `FieldValue`, `FieldEdit`, `FieldWriteResult`, `save_field_values`, `field_id_for`, `field_kind_for_id`. Inserts/repoints VarMeta entries inside the uid group, sets each field's FixedMeta tail presence bit (+ `+2` = entry count), adds the Duration marker 0x0b4000b7 (07 00), writes the six-field `TBkndTask/Props` + `TBkndAssn/Props` registrations, sets Flag1 + mirror, refuses Cost/Flag2..20 and any field without a measured Props record. |
| `core/mppwrite/table.h/.cpp` | `var_entry_insert_offset` / `insert_var_entry`: Project's measured VarMeta order (uid ascending, key ascending inside a uid, secondary family after the plain uid). Used by `save_field_values` AND `add_task_row`. |
| `core/mppwrite/save.h/.cpp` | linked-pin contract measured (w11 §1) replacing the old "unmeasured" note; `bool pin_would_be_clamped(pinned, dependency_allowed_start)` (pure predicate, no scheduling change). |
| `core/mpp/field_value.h` | **new**, header-only read side: `FieldKind`, `CustomFieldValue`, id table, `custom_field_storage_measured`. |
| `core/mpp/reader.h/.cpp` | additive: `Task::custom_fields` + `custom_field()`/`has_custom_field()`, `Document::custom_field(uid,id)`; `fixed_meta_coverage` gained one optional `row_custom_flag_byte` out-param (Flag1's +24 byte). Liveness untouched; PROJECT_A 83 / PROJECT_B 73 / synthetic 3 unchanged. |
| `tests/roundtrip_add_delete.cpp` | updated to the default cascade (no `allow_dangling` for normal deletes), new corpus-free `make_link_source` (synthetic-base's Cons table is empty), cascade cases + `w48-del-cascade*.mpp`. |
| `tests/roundtrip_field_values.cpp` | **new** (`mppwrite_roundtrip_field_values`): encode -> write -> re-read for every measured kind, rewrite with repointed entries, flag set/clear, 12 refusals, id-table cross-check with core/mpp, `pin_would_be_clamped` cases. |
| `tests/mpp_field_value_tests.cpp` | **new** (`mpp_field_value`): the read API against `w10-fieldvalues.mpp`, `w10-flag-off/on` and the committed synthetic control; 100-id round trip. |
| `core/mpp/CMakeLists.txt`, `core/mppwrite/CMakeLists.txt` | additive test targets; the fixture mirror moved to `core/mppwrite/build/vs-w48/test-out`. |

### API semantics (ours, documented in the headers)

- `delete_task_row(doc, uid, path)` now cascades by default; no options needed for
  a normal file. `allow_dangling` is only for a surviving live link whose OTHER
  endpoint already names a dead uid (damaged file); accepted orphan assignments
  (PROJECT_B's 10) are warned about, never refused.
- Verification after a delete: dependent FixedData byte-identical, targeted
  entries read back `+0=0x02 / +2=0`, non-targeted entries byte-identical, header
  `+4` gets bit 0x02 and no other bit moves, table sizes unchanged.
- `save_field_values` rewrites the file and requires: FixedData/Fixed2Data
  byte-identical; only FixedMeta (target entry `+2`/`+24`), Fixed2Meta (Flag1
  mirror `+31/+61`), VarMeta (header `+8`/`+0x14`, repointed entry `+4`, inserted
  entries), Var2Data (appended blocks), and the two Props registries (added 88-byte
  records + their five preamble words) may move. It re-reads every value.
- New field entries are INSERTED inside the uid's contiguous group, key-ascending
  (`insert_var_entry`) -- appending at the end is what made Project ignore the
  values. Overwriting an existing field appends a new block and repoints the
  entry, leaving the old block orphaned -- the same append-only scheme table.h
  uses.
- A field is registered once per table in Props; a rewrite adds nothing. Clearing
  a flag does NOT remove its registration (the flag-off fixture keeps Flag1's
  record, so it is no longer byte-identical to synthetic-base).
- **The Fixed2Meta Flag1 mirror is written** because it is Project's own measured
  write shape; whether Project *needs* it on a fresh open is **unmeasured** (the
  Flag1 gate passed, so it is harmless). If the row has no Fixed2Meta entry the
  mirror is skipped and warned about.
- **Cost is refused** with "storage is unmeasured"; **Flag2..Flag20 are refused**
  (only Flag1's bit position is known); and every field without a measured Props
  record is refused (only the six measured fields are writable).
  `mpp::CustomFieldValue` reports Cost / Flag2..20 blocks with
  `storage_measured == false` instead of decoding them.

### Verification (exact, 2026-10-22, `build\vs-w48`)

```
cmd /c '"...\vcvars64.bat" >nul && cmake -S . -B build\vs-w48 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"...\vcvars64.bat" >nul && cmake --build build\vs-w48 --config Release --target <targets>'
```

| binary | result |
|---|---|
| `mppwrite_roundtrip_add_delete` | **760 checks, 0 failures** |
| `mppwrite_roundtrip_field_values` | **317 checks, 0 failures** |
| `mpp_field_value_tests` | **283 checks, 0 failures** |
| `mpp_reader_tests` | **PASS: 685 checks** (W49's added link/assignment tombstone cases), PROJECT_A 83 / PROJECT_B 73 |
| `mpp_tombstone_tests` | **PASS: 47 checks** |
| `mppwrite_save_flow` | 101 checks, 0 failures |
| `mppwrite_roundtrip_authority` | 1045 checks, 0 failures |
| `ctest -R "mppwrite|mpp_reader|tombstone|field_value"` | **12/12 passed** |

`plan_recalc_tests` was built but **not run**: it rewrites
`tests/data/project_a_recalc_golden.tsv`, which W48 must not edit; W48 touches no
`core/plan`/`core/sched` code, so M2 is unaffected.

### Fixtures (git-ignored, regenerated by the test binaries)

In `build\vs-w48\core\mppwrite\test-out\` and mirrored to
`core\mppwrite\build\vs-w48\test-out\`; `w45-manifest.json` and
`w48-manifest.json` carry size + FNV-1a 64.

| file | bytes | FNV-1a 64 | meaning |
|---|---|---|---|
| `w48-del-cascade.mpp` | 238,080 | `0xd195f93950e27c4b` | **Project gate**: `w11-linkdelete-pre.mpp` + delete uid 2; Cons bytes identical to Project's post file |
| `w48-del-cascade-synthetic.mpp` | 237,568 | `0xf9a3451c8a273cf8` | corpus-free: synthetic + links 1->2, 2->3; delete uid 2 |
| `w48-fields.mpp` | 240,640 | `0x55667f0769a5258b` | uid 1 Text1 "W48 alpha", Number1 42.5, Number2 3.25, Duration1 4800, Date1 2026-10-26 08:00, Flag1 true; **Task/Props 614 B and Assn/Props 616 B byte-identical to Project's w10-fieldvalues.mpp** |
| `w48-fields-rewrite.mpp` | 240,640 | `0xa02cf6e1b0ea3b5d` | the same fields overwritten (entries repointed, counts unchanged), Flag1 false; registries untouched |
| `w48-flag-on.mpp` | 238,080 | `0x437905da564d2d2d` | synthetic + uid 2 Flag1 true; both Props gain the Flag1 record |
| `w48-flag-off.mpp` | 238,080 | `0xca57ec93adbefd3f` | the same with Flag1 cleared; the registration stays, so this is NO LONGER byte-identical to `synthetic-base.mpp` |

W45 delete fixtures changed because of the cascade (the add fixtures are
unchanged): `w45-del-middle.mpp` and `-a` `0xb4af6315546d0a0f`,
`w45-del-middle-b.mpp` `0xa11b5abdc9df5824`, `w45-del-last.mpp`
`0x35706ef995c09644`, `w45-del-probe-middle.mpp` `0xb9d3ab265daa8544`.

### Verified vs inferred

Verified locally and re-runnable: the cascade selects and tombstones exactly the
rows naming the uid (both link endpoints, assignment task uid), sets all three
header flags, leaves every dependent FixedData byte and every other stream
byte-identical, and the emitted cascade fixture's link table is byte-identical to
Project's own post-delete file. Field values round-trip through the writer's
re-read and through `core/mpp`; the VarMeta array is uid-grouped and key-sorted
after every write; **both Props registries are byte-identical to Project's own
`w10-fieldvalues.mpp` streams**; the uid 1 VarMeta order, FixedMeta tail bytes and
`+2` equal Project's own six-field save `w11-fields-base.mpp`; the field test
asserts all of that directly. Refusals do not touch the document or the output
path.

Verified by Project: the W10 oracle encodings/ids; the delete cascade and the
Flag1 pair (W48 gate); the tombstone/header-bit contract. NOT yet verified: the
custom-field VALUES on a fresh Project open after the group + Props fixes (two
earlier opens showed zeros; both diagnosed causes are now fixed and the
definitions match Project's file byte-for-byte). Whether the Fixed2Meta Flag1
mirror is *required* is still unmeasured, though Flag1 passed with it.

### Open items / next actions

1. **Confirm the field values in Project**: `w48-fields.mpp` / `-rewrite`. The
   third pass adds the last measured store (tail bits + duration marker) and the
   uid 1 row now matches Project's own six-field save; if zeros somehow persist,
   send the file back rather than guessing (the definitions and the row already
   match Project's own bytes).
2. **Reader parity (W49, now landed):** W49 added the tombstone-state predicate to
   link/assignment liveness; `w48-del-cascade.mpp` now reads 0 links / 0
   dead-endpoint links / 2 assignments, so W48's cascade test expectations were
   updated to match. PROJECT_A 83 / PROJECT_B 73 unchanged.
3. `save_field_values` cannot delete a field; clearing Text to "" is the only
   "clear" shape, and clearing a flag leaves its Props registration. A measured
   field-delete encoding is the missing oracle case.
4. Cost storage stays unmeasured; one oracle fixture with Cost1..Cost3 set would
   turn the refusal into an implementation. Fields other than the six measured
   ones are refused for lack of a Props record.

### Traps added this wave

- The W48 CMake mirror was still `build/vs-w45`; changed to `vs-w48`. ctest runs
  with the repo root as cwd, so the relative probe paths work, but reading the
  emitted fixtures from `core/mppwrite/build/vs-w48/test-out` needs the new path.
- `tests/data/synthetic-base.mpp`'s `TBkndCons` table is EMPTY; the cascade's
  link half needs `make_link_source` (W48 test helper) or a corpus fixture.
- The drop-in `#include "field_value.h"` in `reader.h` means editing the id table
  rebuilds `core_mpp`; that is expected, not a CMake problem.
- `field_values.cpp` must set `field_id` on the value `decode_payload` returns,
  or the read-back comparison fails on `field_id` while printing the right text
  (cost a build cycle).
- **Never rewrite a multi-line file with `Get-Content` (array) + `Set-Content
  -NoNewline`.** It concatenates every line into one; `field_values.cpp` was
  flattened that way and had to be reconstructed. Use `Get-Content -Raw` for
  literal replacements, or the write/edit tools, and check the line count after.
- The Task and Assignment Props preambles are NOT interchangeable: Task stores
  `44+88n` in the high half of u32 +28 (u16 +30) and its count/length at
  +78/+82; Assignment stores the total at u32 +32 and count/length at +80/+84.
  The first implementation used the Task layout for both and Project ignored the
  Assignment registration (harmless) -- the test now compiles both against
  Project's own streams.




