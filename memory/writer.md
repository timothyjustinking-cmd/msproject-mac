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
- Not implemented, deliberately: adding/removing directory entries, adding a row,
  adding a VarMeta entry, repairing PROJECT_A's damaged metadata, free-sector reuse,
  regenerating `214/`. Reasons in spec §5 and §10.4.

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
