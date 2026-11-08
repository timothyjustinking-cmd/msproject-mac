# Wave 34 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-34-<group>-COMPLETE` line is appended below (wave-34-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the decoding lane: assess what Project actually does

| item | finding | status | evidence |
|---|---|---|---|
| W34-0.1 | brief W34-0.1: one stream-by-stream, offset-by-offset diff for each wave-30 pair and the wave-31 Fill pair | **DONE** | commit `bd669c0`; `scripts/pair_anatomy.py`; 23 reports under `spec/writers/evidence/`; gates `build/w34/W34-0.1-gates.log` |
| W34-0.2 | brief W34-0.2: one decode note per shared writer, with rule / evidence / limits / pairs-still-needed and the decompiled addresses | **DONE** | commit `7998d20`; eight notes in `spec/writers/`; gates `build/w34/W34-0.2-gates.log` |
| W34-0.3 | brief W34-0.3: one more pair per open rule, one factor per pair, measured through the oracle lock | **DONE** | commit `c0acd7c`; 8 pairs, 24 new `.mpp`; reports and note updates; gates `build/w34/W34-0.3-gates.log` |
| W34-0.4 | brief W34-0.4: decide, do not do - record the rules that would need a live trace | **DONE** | commit `ef12339`; the decision and each writer's open rules are recorded in the notes; gates `build/w34/W34-0.4-gates.log` |
| W34-0.5 | Q5 / A-6: synthetic pairs for Insert Task, Delete Task, link ADD and custom-field SET/CLEAR, with the existing tests routed through `save_pair::compare` | **DONE** | commits `ec38908`, `f9ae511`; 5 pairs, 15 new `.mpp`; 41 named expected failures; gates `build/w34/W34-0.5a-gates.log`, `build/w34/W34-0.5b-gates.log` |

No G0 item was deferred.

W34-0.1 detail:

- `scripts/pair_anatomy.py` mirrors `tests/save_pair_noise.h`: control vs the
  committed no-edit save is the mask; null vs Project's edit save are the
  effects.  It decodes Props records and tails, VarMeta entries, Var2Data
  blocks, table rows and FixedMeta entries; text-like payloads are reported
  by shape only, never by content.
- One report per pair under `spec/writers/evidence/`: 22 wave-30 pair
  reports and `wave31-fill.md`, with stream lengths, noise and effect
  offsets, shift notes and the structural decode.  Later commits extend the
  same registry: 31 pairs after W34-0.3, 36 after W34-0.5.

W34-0.2 detail:

- Eight notes, each with `## Rule`, `## Evidence` (measured pairs and the
  static Ghidra model with function addresses), `## Limits` and
  `## Pairs still needed`: `var-meta.md`, `view-stream-214.md`,
  `props-records.md`, `table-growth.md`, `calendar-tbkdcal.md`,
  `document-summary-information.md`, `note-richtext.md`, `scheduler-save.md`.
- The measured rule is stated where the static model cannot supply one:
  `var-meta.md` records that the single compacting serialiser in the model
  (`FUN_1408b05ac`) contradicts every measured editor save, so the pair
  evidence is the rule; `props-records.md` records that the record-framing
  writer was not isolated and the layout is measured through `core/props`.
- Where a writer could not be isolated within the two-hour box, the note
  says so and names the W34-0.3 measurement that would settle it.

W34-0.3 detail:

- Eight pairs on `synthetic-base` descendants, each with its Project no-edit
  null save: six note-RTF pairs (escape, non-ASCII, two-line, replace,
  clear, two changes in one save), `calculate-second` and
  `resource-add-third`.
- Every edit went through `scripts/oracle_lock.py` on copies under `C:/w34`
  with `neutralise()` before the first save; no corpus file was touched
  (rule 4).  The COM facts are recorded by `scripts/oracle_w34_notes.py`,
  `scripts/oracle_w34_calculate.py` and `scripts/oracle_w34_resource.py`.
- Decode notes corrected or extended: `note-richtext.md` (generator string
  is environment state - Riched20 16.0.20430 in wave 30, 16.0.20527 in every
  wave-34 pair, so a note writer must reproduce or refuse the saving
  machine's generator), `calendar-tbkdcal.md` (a second added resource
  writes a calendar row but no name block; only Set Baseline writes one;
  the W34-0.2 claim is corrected), `var-meta.md` (second and third removal
  measurements and their composition), `scheduler-save.md` (a second
  Calculate with no model change writes nothing but the Props14 edit
  marker).
- 24 new `.mpp` fixtures, each SHA-256 pinned in `tests/data/README.md` and
  `scripts/check_fixtures.py`; check_fixtures at this commit reports 164
  documented hashes over 198 tracked `.mpp`.

W34-0.4 detail:

- Decision 3's default stands: no dynamic trace of `WINPROJ.EXE` in wave 34,
  and Exoproc is not used (rule 5).
- Each note states, for its own writer, which rules remain open and whether
  a save-time trace could settle them: `var-meta.md` needs none (shrink, two
  removals, an existing orphan and a zero-payload block are all
  pair-measurable; a break on `WriteFile` would identify a write route, not
  a shape); `scheduler-save.md` is the strongest future candidate because
  the static model does not expose the engine's write-back;
  `props-records.md` and `view-stream-214.md` are conditional candidates for
  the record-framing and item-replace rules; `note-richtext.md` and
  `document-summary-information.md` would gain only confirmation of a
  source, not a shape.
- The notes' `## Pairs still needed` sections remain the work list; no
  writer waits on a trace.

W34-0.5 detail:

- Five synthetic pairs on `synthetic-base`, each the same Project 2021
  session's save before and after ONE edit (rule 2) plus a committed
  no-edit null save produced by `scripts/oracle_w34_null_saves.py` on a copy
  under `C:/w34/null`: Insert Task (`Tasks.Add`), Delete Task
  (`Tasks(2).Delete`), link ADD (FS + 0 dependency 1 -> 2), custom-field SET
  (`Task.Text1` from no value) and custom-field CLEAR (a seeded two-session
  clear).  `scripts/oracle_w34_task_ops.py` records the COM facts; every
  save went through `scripts/oracle_lock.py` with `neutralise()` before it.
- Measured shapes, recorded in the decode notes: SET appends one VarMeta
  entry, one 40-byte Var2Data block and one 88-byte record to BOTH per-table
  Props streams, plus 8 bytes to the directory-level `114/Props`; CLEAR is
  a VarMeta REMOVE in an unchanged 540-byte stream with Var2Data
  byte-identical; Insert Task appends the task row AND a placeholder
  assignment row; Delete Task tombstones the row with a logical VarMeta
  removal; link ADD reschedules the successor and changes
  DocumentSummaryInformation 1100 -> 1104.
- The five tests now route every stream through `tests/save_pair_compare.h`
  (the whole-file wrapper over `save_pair_noise.h`), with each pair's
  declared effect streams passed externally from the committed anatomy so a
  widened mask cannot hide an effect.  `tests/roundtrip_add_delete.cpp`,
  `tests/roundtrip_links.cpp` and `tests/roundtrip_field_values.cpp` carry
  41 exact-signature `KnownGap` entries - 15 insert, 6 delete, 13 link-add,
  2 field-set, 5 field-clear - each a named expected failure; an unlisted
  mismatch is a hard failure, and a listed stream that now matches Project
  is a hard failure too.
- Assessment A-6 is explicit that the tempting fix - disabling Delete Task
  and link ADD as unmeasured - is wrong: they were already diffed against
  Project's corpus files, and Q5's remedy is to put the evidence in CI.  The
  paths are therefore not disabled and no command's enablement changed in
  these commits; each mismatch the synthetic pair exposes is recorded as a
  named expected failure, not patched blind.  The two shapes that contradict
  an earlier decode - `delete_task_row`'s physical VarMeta shrink and
  `clear_field_value`'s kept entry with an empty block - are queued for the
  writer groups with the measurement recorded in `var-meta.md`.
- Runs after the change (Release, `build/vs-verify33b`):
  `mppwrite_roundtrip_add_delete` 1166 checks, 0 failures, 21 expected;
  `mppwrite_roundtrip_links` 976 checks, 0 failures, 13 expected;
  `mppwrite_roundtrip_field_values` 1193 checks, 0 failures, 7 expected.
- 15 new `.mpp` fixtures, each SHA-256 pinned in `tests/data/README.md` and
  `scripts/check_fixtures.py`; check_fixtures at the test commit reports 179
  documented hashes over 213 tracked `.mpp`.

Per-commit gates (logs under `build/w34/`; every item's client-name scan
reports 0 files / 0 hits and guard_names exits 0):

| commit | builds | tests and tracking gates |
|---|---|---|
| W34-0.1 `bd669c0` | none (tool and spec only) | `pair_anatomy.py` compiles and emits all 23 reports, exit 0; check_markers 1333 files, 9 markers, 0 message hits |
| W34-0.2 `7998d20` | none (spec only) | check_markers 1341 files, 9 markers, 0 message hits; check_fixtures 140 documented hashes, 174 tracked `.mpp` |
| W34-0.3 `c0acd7c` | none (spec, fixtures and tooling) | check_markers 1377 files, 9 markers, 0 message hits; check_fixtures 164 hashes, 198 tracked `.mpp` |
| W34-0.4 `ef12339` | none (docs only) | check_markers 1377 files, 9 markers, 0 message hits; check_fixtures 164 hashes, 198 tracked `.mpp` |
| W34-0.5a `ec38908` | none (fixtures and evidence) | check_markers 1398 files, 9 markers, 0 message hits; check_fixtures 179 hashes, 213 tracked `.mpp` |
| W34-0.5b `f9ae511` | mppwrite tree build exit 0, 0 compiler/linker errors | `roundtrip_add_delete` 1166 checks, 0 failures, 21 expected; `roundtrip_links` 976 checks, 0 failures, 13 expected; `roundtrip_field_values` 1193 checks, 0 failures, 7 expected; check_markers 1399 files, 9 markers, 0 message hits; check_fixtures 179 hashes, 213 tracked `.mpp` |

G0 group gates (fresh trees configured from scratch on `f9ae511` under
`build/w34-g0-fresh{,-app,-qt}`, log `build/w34/W34-G0-fresh-gates.log`; the
marker commit that follows changes this file only):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 55/55 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failure(s) / 8 scenarios, 7 pass, 1 fail, 0 unrecorded, expected failing set reached, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, 83/83 rows, exit 0 |
| app / app_qt configure and build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app ctest / app_qt ctest | 25/25 / 41/41 passed |
| `check_fixtures.py` | fixtures OK (179 documented hashes, 213 tracked `.mpp`); paths OK (262 tracked test-data files, no local path); property sets OK (213 tracked `.mpp`/`.mpt`, no marker); provenance OK (8 scenario oracles, recording_commit = last measured-body change) |
| client-name scan on HEAD / messages `main..HEAD` / `guard_names main..HEAD` | 0 files, 0 hits / 0 commits / exit 0 |

The run started 11:33 and finished 12:01.  The numbers belong to `f9ae511`,
the code tree this record commit follows; the marker commit is docs-only.

## Found, not fixed

- G0 raised no new Critical or High finding beyond the writer gaps it was
  chartered to measure.  The five W34-0.5 paths each expose a port save that
  does not yet match Project; each mismatch is recorded as a named expected
  failure and is a work item for the writer groups, not a silent pass.
- The measured CLEAR shape (a VarMeta removal with unchanged streams)
  contradicts the W52 clear decode the port implements; `var-meta.md` and
  `props-records.md` record the contradiction.  Per the brief it is queued,
  not patched blind.
- The note writer cannot assume a RichEdit envelope: the generator string
  changed on this machine between wave 30 and wave 34, and the note records
  that a writer must reproduce or refuse the saving machine's string.
- **Erratum.**  The W34-0.5a commit message says check_fixtures reports "179
  documented hashes over 198 tracked `.mpp`".  The log for that commit says
  213 tracked `.mpp`; 198 was the W34-0.3 count.  This record quotes the log
  (179/213).  The message is left as written: this record does not rewrite
  its own history.
- The final wave commit, the review pass over the wave-34 diff, the push and
  the CI dispatch belong to the CLOSE section (wave-34-brief section 2), not
  to this group session.  Per the brief's launch note, push and CI are by
  the coordinator after CLOSE.

WAVE-34-G0-COMPLETE

## G1 - the VarMeta entry writer (insert, remove, resize)

| item | finding | status | evidence |
|---|---|---|---|
| W34-1.1 | brief W34-1.1: add, drop and re-point one Var2Data record for one row, keeping Project's VarMeta ordering, the logical-removal-with-stale-tail convention and the Props count words | **DONE** | commits `2ef60df`, `1d2f22b`; `core/mppwrite/varmeta_edit.{h,cpp}`; `spec/writers/var-meta.md`; gates `build/w34/9fdab79-W34-1.1-*.log`, `build/w34/af01454-W34-1.1b-*.log` |
| W34-1.2 | brief W34-1.2: compare against Project's file for all three shapes with the null save; a refusal test for every unmeasured shape | **DONE** | commits `521dbb0`, `d1883da`; `tests/roundtrip_varmeta.cpp`; five pairs, 0 KnownGap; gates `build/w34/2ef60df-W34-1.2-*.log`, `build/w34/af01454-W34-1.2b-*.log` |
| W34-1.3 | brief W34-1.3: first consumer, core only: `rename_resource(uid, name)` through the new writer, field by field against wave30-resource-rename | **DONE** | commit `af01454`; the consumer's independent read-back and name refusals in `tests/roundtrip_varmeta.cpp`; gates `build/w34/521dbb0-W34-1.3-*.log` |

No G1 item was deferred.  The Assignment and project-row shapes named in
W34-1.1's scope sentence are refused under rule 1 for want of a pair rather
than enabled unmeasured -- recorded in the capability matrix and in Found,
not fixed below, with the pair each would need.

W34-1.1 detail:

- `core/mppwrite/varmeta_edit.{h,cpp}` is the writer the wave's field work
  stands on: ONE 12-byte (uid, offset, key) VarMeta entry of ONE row added,
  re-pointed or removed, with Project's bookkeeping.  Rule 1's capability
  matrix is enforced in code: Task append/re-point/remove enabled, Resource
  re-point only, and every other shape refused before the document or the
  output path is touched.
- `append` inserts the entry at the measured key position inside the uid's
  contiguous group, appends `[u32 len][payload]` at the Var2Data high-water
  mark, header +8 +1, FixedMeta +2 +1, the caller's measured presence bits
  ORed, 114/Props size = the new length.  Pair: wave30-notes, 42 -> 43
  entries, 516 -> 718.
- `re-point` moves one entry to the high-water mark, orphans the old block,
  leaves the counts and the freed word alone, and writes the new size to
  +0x14 and 114/Props.  Pairs: wave30-resource-rename (114 -> 140, 6 -> 8 B)
  and wave34-note-replace (524 -> 725, 197 -> 209 B).
- `remove` erases the entry from the logical array, header +8 -1, FixedMeta
  +2 -1, presence bits ANDed out, the freed word += the payload, and leaves
  Var2Data untouched.  The stream keeps its length via
  `merge_logical_var_meta`, so the freed last slot stays a byte-identical
  duplicate of the new last logical entry -- the convention wave31-fill
  measured and `outline.cpp` already uses.  Pairs: wave34-field-clear (freed
  0 -> 40) and wave34-note-clear (0 -> 188); the brief's "one new pair for a
  removal" is the W34-0.3/W34-0.5 removal set.
- 114/Props is the directory stream of 16-byte scalar groups
  `[u32 4][tag 4 BE][u32 4][u32 value]`; the Task and Resource size/freed
  words are located by their BIG-endian tags (0x01000100 / 0x01000002 and
  0x02000100 / 0x02000002), a missing or duplicated tag is refused, and the
  sibling tags are preserved byte for byte.  Every save writes the Props14
  edit marker.
- All-or-nothing on a copy, saved atomically, re-opened and proved from
  disk: byte-identical re-read, `CfbDocument::check`, the table geometry,
  the exact entry state, FixedMeta +2 and the requested bits, the two Props
  words, and every stream outside {table VarMeta, table Var2Data, table
  FixedMeta, 114/Props, Props14} unchanged.
- The brief's scope sentence also names the Assignment table and the project
  row; neither has a pair, so both are refused (rule 1), recorded in the
  capability matrix and under Found, not fixed.  `1d2f22b` adds the
  family-entry refusal half the spec was missing.

W34-1.2 detail:

- Five committed Project pairs drive the writer through
  `tests/save_pair_compare.h` with each pair's null save separating noise
  from edit, each pair's edited streams declared externally, and NOT ONE
  KnownGap: wave30-notes (append), wave34-note-replace and
  wave30-resource-rename (re-point, Task and Resource), wave34-field-clear
  and wave34-note-clear (remove).  Effects reproduced, by pair:
  271, 219, 16, 55, 56 bytes against masked noise of 133, 137, 154, 136,
  137.
- The append and Task re-point payloads are the blocks Project stored, read
  from each pair's edit save and handed back as the command's input; the
  resource name is decoded from Project's block and re-encoded by
  `rename_resource`, with the test asserting the encoding round-trips
  Project's bytes.  No task or resource text appears in the test.
- Structural assertions independent of the comparison: entry index/offset,
  FixedMeta +2, presence bits, Var2Data length, the 114/Props size and freed
  words, and for a removal the kept stream length and the stale-slot
  duplicate.
- The refusal suite exercises every refusal `varmeta_edit.h` lists, each
  case also asserting the document is byte-identical and no output file
  appeared: 0/2 edits, unknown table, the project row, key 0, unknown uid,
  duplicated uid, dead FixedMeta row, a FixedMeta entry naming another row,
  broken FixedData/Fixed2Data/Fixed2Meta geometry, VarMeta not 24 + 12*n, a
  stale tail on the way in, a +0x14 disagreement, append collisions (plain
  and 0x80000000|uid family), empty append, Resource append/remove, re-point
  and remove with no entry, with two entries and with a lone family entry, a
  0xFFFFFFFF offset, a zero-length block, smaller and equal re-points, a
  remove with a payload, presence bytes outside the entry, a multi-bit mask,
  a duplicated presence operation, a missing and a duplicated 114/Props tag,
  and empty/unusable output paths.  1601 checks, 0 failures.
- The pair harness caught two writer defects while it was written: the
  append count word was seeded from zero (the staged re-read refused it),
  and `matching_entries` alone would have adopted a lone family entry for
  re-point/remove (the explicit family refusal was added to both).  Both
  fixes are in `2ef60df`; `d1883da` then completed the refusal variants.

W34-1.3 detail:

- `rename_resource(uid, name)` encodes UTF-16LE + NUL under Resource/Name
  (0x0c400001), applies the measured larger re-point to the Resource table,
  and reads the value back from the file the writer re-opened.
- The success path is the field-by-field resource-rename pair above (Name
  114 -> 140, Var2Data 140 -> 152, Resource FixedMeta and the Task table
  untouched).  The test also reads the name from the command's output with
  the low-level helpers -- one entry, offset 140, the exact encoded block --
  and covers the consumer's refusals: empty name, embedded NUL, past 65,535
  bytes, an unknown uid, a shorter name and the control's own equal-size
  name (the unmeasured shrink and equal re-points).

Per-commit gates (logs under `build/w34/`; every client-name scan exits 0; a
log's prefix is the HEAD the gates ran on, i.e. the commit before the one
the row names -- W34-1.1's logs are `9fdab79-...`, W34-1.2's
`2ef60df-...`, and so on):

| commit | builds | tests and tracking gates |
|---|---|---|
| W34-1.1 `2ef60df` | `core_mppwrite` builds clean under /W4 with warnings as errors | mppwrite ctest 14/14 (2.57 s, including the new test built in the working tree); check_markers --worktree 1400 files, 9 markers, 0 hits; check_fixtures 179 hashes / 213 tracked `.mpp` / 262 test-data files; guard_names exit 0 |
| W34-1.2 `521dbb0` | test-only; library unchanged | `mppwrite_roundtrip_varmeta` 1569 checks, 0 failures, 0 expected; pair effects 271/219/16/55/56 against noise 133/137/154/136/137; mppwrite ctest 14/14 (6.86 s); check_markers worktree 1402 files, range 1403 files, 0 hits; check_fixtures 179/213; guard_names exit 0 |
| W34-1.3 `af01454` | test-only | 1591 checks, 0 failures, 0 expected; ctest 14/14 (3.70 s); check_markers 0 hits; check_fixtures 179/213; guard_names exit 0 |
| W34-1.2b `d1883da` | test-only | 1601 checks, 0 failures, 0 expected; ctest 14/14 (2.76 s); check_markers 0 hits; check_fixtures 179/213; guard_names exit 0 |
| W34-1.1b `1d2f22b` | docs only | check_fixtures 179/213/262; check_markers worktree and range 0 hits; guard_names exit 0 |

G1 group gates (fresh trees configured from scratch on `af01454` under
`build/w34-g1-fresh{,-app,-qt}`, log `build/w34/W34-G1-fresh-gates.log`;
`d1883da` and `1d2f22b` after it are test- and docs-only):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 56/56 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine self-test (bare exe / `engine_diff.py --selftest`) | 265 checks, 0 failures, exit 0 in the fresh log / 53 checks, 0 failures, exit 0, run separately on the same tree (`build/w34/W34-G1-engine-selftest.log`) |
| engine `--suite --expect-fail constraint-mso` | 8 scenarios, 7 pass, 1 fail, 0 unrecorded, expected set reached, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, 83/83 rows, 0 unexplained, exit 0 |
| app configure/build with `CORE_WARNINGS_AS_ERRORS=ON`; ctest | exit 0 / exit 0; 25/25 passed |
| app_qt configure/build with `CORE_WARNINGS_AS_ERRORS=ON`; ctest | exit 0 / exit 0; 41/41 passed |
| `check_fixtures.py` | 179 documented hashes, 213 tracked `.mpp`, 262 test-data files, property sets OK, provenance OK |
| scans on `af01454` | check_markers 1403 files, 0 hits; messages `main..HEAD` 0 hits; guard_names exit 0 |

The main run took 12:46 to 13:05.  Its app_qt configure passed a relative
`CMAKE_PREFIX_PATH`, which resolves against the build dir, and failed; the
recorded app_qt result is the retry with the absolute Qt path, which finished
at 13:17 and appended to the same log.  The numbers belong to `af01454`, the
code tree this record follows.

## Found, not fixed (G1)

- **G1: the Assignment table and the project row.**  The brief's W34-1.1 scope
  sentence names `114/TBkndAssn` and the uid-0 row, but no committed pair
  exercises a var-record append/re-point/remove there.  Rule 1 forbids
  enabling an unmeasured shape, so the capability matrix refuses both; one
  pair each (an append and a remove on a live assignment row; a re-point or
  remove on the uid-0 row) would enable them.  This is a rule-1 refusal
  recorded in `spec/writers/var-meta.md`, not a silent gap.
- **G1: the shrink and equal-size re-point.**  The in-place shrink rule is
  measured inside the wave-28 outline edits but no committed pair isolates it
  as the only edit, so it and the equal-size case are refused until an
  isolated pair exists.
- **G1: the note payload encoder.**  This writer stores bytes it is given;
  producing Project's RichEdit note payload is G0's note-richtext rule and
  the G4 note commands' work, not part of the var writer.
- The five G0 `KnownGap` sets for the task-op paths are untouched by G1;
  the brief amendment `9fdab79` (W34-4.0) governs their G4 enablement.

WAVE-34-G1-COMPLETE

## G2 - the `214` view-stream writer and Props record insertion

| item | finding | status | evidence |
|---|---|---|---|
| W34-2.1 | brief W34-2.1: mid-stream insertion in a `214/` stream with the VarMeta offset rewrite and the Props size byte, measured against `wave30-textstyle-*` (one Bold cell), Bold only | **DONE** | commit `6b2ba05`; the pair test and the outer-length-word fix in `70cea19`; `core/mppwrite/viewstream_edit.{h,cpp}`; `spec/writers/view-stream-214.md`; gates `build/w34/d811aea-W34-2.1-*.log`, `build/w34/55b2b1e-W34-2.3-*.log` |
| W34-2.2 | brief W34-2.2: insert and update a `114/Props` or `214/Props` record (id, kind, length, payload) keeping the table's order and the `Props14` mirror, measured against `wave30-wbs-code-*` and the three count words of `wave30-resource-add-*` | **DONE**, insert refused under rule 1 with the evidence | commit `46b49e0`; tests in `70cea19`; `core/mppwrite/props_edit.{h,cpp}`; `spec/writers/props-records.md`; gates `build/w34/6b2ba05-W34-2.2-*.log`, `build/w34/55b2b1e-W34-2.3-*.log` |
| W34-2.3 | brief W34-2.3: tests as G1; refusals for every unmeasured sibling (I/U/strikethrough, colours, row vs cell selection, other views, the project summary row) | **DONE** | commit `70cea19`; `tests/roundtrip_viewstream.cpp`, `tests/roundtrip_props.cpp`; gates `build/w34/55b2b1e-W34-2.3-*.log` |

No G2 item was deferred.  W34-2.2's brief names an insert the measured
pairs do not perform; it is refused with the evidence below rather than
enabled unmeasured (rule 1), and the `214/Props` record half is refused for
the same reason.  The 15:00 restart note's uncommitted work was read,
finished, gated and committed -- never unread -- as the brief directs.

W34-2.1 detail:

- `core/mppwrite/viewstream_edit.{h,cpp}`: `save_view_cell_style(doc,
  edits, path)` appends ONE `0x2240000c` cell-format item to the applied
  view's main block in `214/CV_iew/Var2Data`, moves the block's own
  length/count words, re-points the VarMeta entry's Var2Data offset and
  writes the `214/Props` CV_iew size scalar, measured end to end on
  `wave30-textstyle` (control + null save + edit save).  The applied view
  is the target VarMeta entry (uid 2, key 0x20400006); the block walks as
  `u32 payload-4` twice, `u32 0x00010000`, a u16 item count (the u16 at +14
  preserved), then items `u32 length, u32 id, u32 kind, payload` with one
  zero pad byte after an odd length.  The W34-0.2 note is corrected: +12 is
  a u16 count and the pad byte is part of the framing.
- The appended item is 56 B: length 44, id 0x2240000c, kind 1, and the
  44-byte entry wave30-textstyle stores (row uid, field id, flags
  0x01000003, the two automatic colour words, pattern 1 and the font word
  0x90000001).  The font word is written as the one measured literal and
  only for Bold set: `spec/writers/view-stream-214.md` records that the
  W34-0.2 static pass settled no other bit and the literal appears nowhere
  in the Ghidra model; italic, underline, strikethrough, both colours and
  Bold-off are refused.
- Refusals: zero/two edits; a view uid other than 2; the project summary
  row (uid 0); field id 0; the five attribute siblings; Bold not set;
  whole-row selection; damaged VarMeta/Var2Data (not 24 + 12*n, bad magic,
  a stale count or size word, the 0xFFFFFFFF no-block offset, an offset
  past Var2Data, gapped blocks, trailing bytes, a missing or duplicated
  target entry), every block-walk defect, and a `214/Props` with no,
  two or a disagreeing CV_iew size tag.
- The write is all-or-nothing and proved from disk: the edit goes to a
  copy, the package is saved, the file is re-read and must hold exactly the
  bytes built, re-emit byte-identically, pass `CfbDocument::check()`,
  show the item, the counts, every re-pointed offset, the props value, the
  marker and no changed stream outside {214/CV_iew/VarMeta,
  214/CV_iew/Var2Data, 214/Props, Props14}; only then is `doc` replaced.
- The W34-2.3 pair test proved the committed writer left the Var2Data
  block's OUTER length word (at the block start) at its control value;
  Project's save grows it 42,692 -> 42,748.  `70cea19` rewrites it with the
  two inner size words and proves all four words, the preserved +14 u16 and
  the unchanged region between the block header and the item from the
  re-opened file.  Pair totals: noise=131, effects=6607 (Var2Data 6542,
  VarMeta 7, 214/Props 1, Props14 1), 436 checks, 0 failures, 0 expected.

W34-2.2 detail:

- `core/mppwrite/props_edit.{h,cpp}`: `save_props_edit(doc, edit, path)`
  updates a record that already exists in `114/Props` and/or one
  typed-property scalar in the tail of `114/Props` or `214/Props`.
  Measured pairs: wave30-wbs-code (records 0x0240138b 4 -> 24 and
  0x024013be 2 -> 6, Task size 0x01000100 512 -> 552, stream 88,724 ->
  88,748, tail 3,316 -> 3,340), wave34-field-set (0x02401392 equal-size
  00 00 -> 01 00, 0x024013c5 8 -> 16, Task size 530 -> 574, stream
  88,742 -> 88,750), wave30-resource-add's three count words (0x02000001
  5 -> 6, 0x02000100 86 -> 140, 0x03000001 7 -> 8, stream length
  unchanged) and wave30-textstyle's `214/Props` CV_iew size (0x01000100
  100,178 -> 100,234).
- The measured rules: the 16-byte header with +0/+4 = stream size - 4,
  +8 = 0x00010000 and +12 preserved verbatim; records back to back
  (u32 length, u32 id in the 0x00400000 family, u32 kind, payload) with
  an update keeping id, kind and position, equal or larger payload only,
  later records and the tail moving by the growth; a scalar is one 16-byte
  `[LE u32 4][BE tag][LE u32 4][LE u32 value]` entry in the tail whose
  value word alone changes.  The whitelist is per stream: `114/Props`
  accepts 0x01000100, 0x01000002, 0x02000001, 0x02000100, 0x03000001;
  `214/Props` accepts 0x01000100 only.  The Props14 marker is written as
  every measured editor save writes it.
- INSERT IS REFUSED WITH ITS EVIDENCE: wave30-wbs-code and
  wave34-field-set keep 161 records on all three sides; wave30-baseline,
  the one pair whose `114/Props` grows (+110 B), keeps its 161 records and
  grows only the undecoded tail; the custom-field SET's 88-byte insert is
  a record in `114/TBkndTask/Props` (86 -> 174), a stream family this
  writer does not cover.  A record edit on `214/Props` is refused too
  (every measured pair has 47 records / 13,102 B there, unchanged beyond
  masked noise).  Shrink, kind change, an equal payload or value, a
  duplicated target, an unlisted tag, a tag absent or twice present, a hit
  outside the tail, a damaged header or record walk and an unusable output
  path are all refused before `doc` or the path is touched.
- `props-records.md` is corrected and extended: the W34-0.2 "four records
  change" claim was wrong (0x024013b1 and 0x024013c2 differ from the null
  save only at masked positions, a report artifact of the growth shifting
  every later record's span); the true updates are 0x0240138b and
  0x024013be, and field-set's 0x02401392 keeps 2 bytes.  The note now
  records why a growing stream is compared field by field: a whole-stream
  positional compare flags exactly one seam byte per pair (2,859: our 2b
  vs Project's 3e; 2,865: our c6 vs d9), while the field-wise comparison
  -- header, record list, every record payload, the tail -- has zero
  mismatches with the effects declared only on the edited fields.

W34-2.3 detail:

- `tests/roundtrip_viewstream.cpp` is the pair test above plus the refusal
  suite, each refusal also asserting the document was not modified and no
  output file appeared.  It records one branch it cannot construct: the
  writer refuses a document without `214/CV_iew/VarMeta`, Var2Data or
  `214/Props`, but `CfbDocument` has no remove-stream operation and every
  committed `.mpp` carries all three, so that branch stays a plain
  `has_stream()` guard with no test.
- `tests/roundtrip_props.cpp` drives the four committed pairs through
  `save_props_edit` and compares each FIELD under its own control-vs-null
  mask: the header, every record's header+payload walked separately in
  each document (so the field is aligned after the shift), and the tail.
  It additionally requires Project's record count, id, kind and offset
  sequence for every record, and every record the request did not name to
  be byte-identical to the control's.  Structural assertions pin the
  measured sizes (88,724 -> 88,748 and 88,742 -> 88,750, tails 3,316 ->
  3,340 and 3,334 -> 3,342, 214/Props 13,102 unchanged), the size words,
  the +8/+12 words, the scalar values and the Props14 marker.  The refusal
  suite exercises every branch `props_edit.h` lists, including the
  fabricated damaged streams the absent/duplicate/outside-tail cases need.
  1,645 checks, 0 failures.
- The first props run caught one wrong test expectation, not a writer
  defect: 0x02401392 moves by the +8 growth of the 0x024013c5 record that
  sits before it in the stream; the assertion was corrected before the
  commit.  The viewstream test needed no such correction: its writer fix
  is in the same commit.

Per-commit gates (logs under `build/w34/`; a log's prefix is the HEAD the
gates ran on, i.e. the commit before the one the row names -- W34-2.1's
logs are `d811aea-...`, W34-2.2's `6b2ba05-...`, and W34-2.3's
`55b2b1e-...`; the post-commit scans carry the commit's own prefix):

| commit | builds | tests and tracking gates |
|---|---|---|
| W34-2.1 `6b2ba05` | `core_mppwrite` builds clean under /W4 with warnings as errors | mppwrite ctest 14/14 (1.82 s); check_fixtures 179/213/262; check_markers worktree and range 0 hits; guard_names exit 0; scan_client_names 0 files, 0 name hits |
| W34-2.2 `46b49e0` | `core_mppwrite` builds clean under /W4 /WX | full tree ctest 57/57 (52.70 s) including the mppwrite lane 14/14 (3.27 s); check_fixtures 179/213/262; check_markers worktree and range 0 hits; guard_names exit 0; scan_client_names 0 files, 0 name hits; git diff --check and LF check clean |
| W34-2.3 `70cea19` | `core_mppwrite` and both new tests build clean under /W4 /WX | mppwrite ctest 16/16 (5.79 s); roundtrip_viewstream 436 checks, 0 failures, 0 expected; roundtrip_props 1,645 checks, 0 failures; check_markers worktree 1,407 files and range 0 hits (the two new tests are untracked at that point and are covered by the post-commit scan); check_fixtures 179/213/262; guard_names exit 0; scan_client_names on `70cea19` 0 files, 0 name hits, 0 commit messages |

G2 group gates (fresh trees configured from scratch on `70cea19` under
`build/w34-g2-fresh{,-app,-qt}`, log `build/w34/W34-G2-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 58/58 passed (77.82 s), including `mppwrite_roundtrip_viewstream` and `mppwrite_roundtrip_props` |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine self-test (bare exe / `engine_diff.py --selftest`) | 265 checks, 0 failures / 53 checks, 0 failures, exit 0 |
| engine `--suite --expect-fail constraint-mso` | 8 scenarios, 7 pass, 1 fail, 0 unrecorded, expected set reached, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, 83/83 rows, 0 unexplained, exit 0 |
| app configure/build with `CORE_WARNINGS_AS_ERRORS=ON`; ctest | exit 0 / exit 0; 25/25 passed (32.15 s) |
| app_qt configure/build with `CORE_WARNINGS_AS_ERRORS=ON`; ctest | exit 0 / exit 0; 41/41 passed (44.10 s), the absolute Qt prefix from the start (no retry) |
| `check_fixtures.py` | 179 documented hashes, 213 tracked `.mpp`, 262 test-data files, property sets OK, provenance OK |
| scans on `70cea19` | check_markers 1,409 files, 0 hits; messages `main..HEAD` 0 hits; guard_names exit 0; client-name scan 0 files, 0 name hits, 0 commit messages |

The run started 15:13 and finished 15:49.  The numbers belong to `70cea19`,
the code tree this record commit follows; the marker commit is docs-only.

## Found, not fixed (G2)

- **G2: the Props insert.**  W34-2.2's brief names it, but no committed
  pair inserts a directory record: the two update pairs keep 161 records
  and wave30-baseline grows only the tail (+110 B, +12 word 262 -> 263),
  in a section the decode note cannot yet rebuild; the custom-field SET's
  insert is inside the per-table `114/TBkndTask/Props` family.  Rule 1
  refuses it (and every `214/Props` record edit) until a pair exists.  The
  same evidence is in `spec/writers/props-records.md`.
- **G2: the view-stream siblings.**  Italic, underline, strikethrough,
  highlight colour, font colour, whole-row selection, other views and the
  project summary row are refused for want of a pair; one pair each would
  enable them, and the remaining font-word bits need the W34-0.2 static
  pass to settle them (`spec/writers/view-stream-214.md`).
- **G2: the unconstructible missing-stream branch.**  The view writer
  refuses a document lacking one of its three streams, but `CfbDocument`
  has no remove-stream operation and every committed `.mpp` carries them,
  so the branch is a plain `has_stream()` guard without a test; recorded
  in the test header rather than counted as covered.
- **G2: the `114/Props` +12 word.**  It is preserved verbatim on every
  measured update and moves only in the wave30-baseline tail shape this
  writer refuses; a decoded pair for that region is needed before anything
  writes it.

WAVE-34-G2-COMPLETE

## G3 - table growth with re-keying, the resource calendar row, DocumentSummaryInformation

Builder note: G3 began under DeepSeek (W34-3.1 committed, then the 402
Insufficient Balance stop) and resumes under Claude Sonnet 5.5 from
`cedba33` (W34-BUILDER).  Builder commits from here carry the
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 34)` trailer.

| item | finding | status | evidence |
|---|---|---|---|
| W34-3.1 | brief W34-3.1: assignment table growth (append a row AND rewrite the existing rows' pointer words and the task's Work summary as `wave30-assign-add-*` shows; removal as `wave30-assign-delete-*` shows); one new pair each varying the task's existing assignment count (0, 1, 2) | **DONE** (record written by the Sonnet builder; the code and pairs are the DeepSeek session's) | commits `281ac54` (writer + first-assignment ADD and REMOVE tests), `07046d0` (the two new pairs and their anatomy), `73bc6e6` (the settled append); `core/mppwrite/assignment_growth.{h,cpp}`; `tests/roundtrip_assignment_growth.cpp`; `spec/writers/table-growth.md`; gates `build/w34/cedba33-W34-3.1-*.log` |
| W34-B5 | brief W34-B5: drop the name column from `tests/data/project_a_golden.tsv` and the docs, readers and scripts that used it | **DONE** | commit `8b79c62` |
| W34-3.2 | brief W34-3.2: the resource calendar row (Add Resources writes the `TBkndCal` streams and three `114/Props` counts, not only the six `TBkndRsc` streams) | **DONE** | commit `adfd358`; `core/mppwrite/resource_growth.{h,cpp}`; `tests/roundtrip_resource_growth.cpp`; `spec/writers/calendar-tbkdcal.md`; gates `build/w34/8b79c62-W34-3.2-*.log` |
| W34-3.3 | brief W34-3.3: `DocumentSummaryInformation` writer for the strings and dates Project rewrites on Calculate Project and Move, property-set encoding verified against an independent reader | **DONE** | commit `a08a9ff`; `tests/roundtrip_summary_info.cpp`; `scripts/dsi_reader.py`, `scripts/check_dsi.py`; `spec/writers/document-summary-information.md`; gates `build/w34/adfd358-W34-3.3-*.log` |
| W34-3.4 | brief W34-3.4: reader gaps the enablements need (`Task.Active`, the inserted summary and milestone result with its fractional Fixed2Data ordinal and reparenting, baseline fields on task, assignment and resource rows, LevelingDelay / Preleveled / Overallocated), each with reader tests against COM facts | **DONE** | the `W34-3.4` commit after `a08a9ff`; `core/mpp/reader.{h,cpp}`; `tests/mpp_reader_tests.cpp` (`test_w34_active`, `test_w34_insert`, `test_w34_baseline`, `test_w34_leveling`); gates `build/w34/a08a9ff-W34-3.4-*.log` |

W34-3.1 detail:

- **Measured shapes (rule 1).**  Four pairs, each against Project's own edit
  save with its committed null save masking per-save noise, through
  `save_pair::compare_all` with the wave-33 `effects` assertion and no
  `KnownGap`:
  `wave30-assign-add` (task count 0 -> 1: the placeholder row is tombstoned,
  a row appended, VarMeta re-keyed, the four assignment `114/Props` scalars
  and the Task/Resource work roll-ups moved; noise=154, effects=1012),
  `wave30-assign-delete` (the REMOVE: appended real group with the 28-byte
  contour record zeroed; noise=158, effects=905),
  `wave34-assign-add-second` (count 1 -> 2; noise=191, effects=462) and
  `wave34-assign-add-third` (count 2 -> 3; noise=205, effects=655).  The two
  `wave34-*` pairs were measured in one Project 2021 session by
  `scripts/oracle_w34_assign_counts.py` on a copy of
  `wave34-resource-add-third.mpp` under `C:/w34`, `neutralise()` before each
  save; their anatomy notes are `spec/writers/evidence/wave34-assign-add-{second,third}.md`.
- **What the pairs show.**  The settled append is additive: old rows,
  FixedMeta/Fixed2Meta entries and Var2Data blocks stay byte-identical; new
  entries append (+110, +34, +48, +53, +48, +124); the task leaf array grows
  and the `114/Props` Task size scalar follows (650 -> 666 -> 686).  The only
  normalisation is the three identity counter dwords of the appended
  `Fixed2Data` record (`TailNormalisation`; Project renumbers them on every
  save).  Also in the commit: `summary_info` derives the work strings for a
  whole non-negative hour count (still refusing negative, fractional,
  one-hour and huge totals), `props_edit` admits the four `114/Props`
  assignment scalar tags, and `save_pair_noise.h`/`save_pair_compare.h` treat
  a position neither document stores as agreement (a stream the edit shrinks,
  DocumentSummaryInformation 1104 -> 1100, could not otherwise match).
- **Gates re-run on `cedba33` (this session).**  Incremental Release build of
  `build/vs-verify33b`: exit 0, no error lines; full ctest 60/60 (49.83 s)
  including `mppwrite_roundtrip_assignment_growth`; that test 1878 checks, 0
  failures, 0 expected failures; `check_fixtures.py` 185 documented hashes,
  219 tracked `.mpp`, 268 test-data files, property sets OK, provenance OK.
- **Guard seen red.**  Mutating the Task Var2Data size scalar in the settled
  append (`task_var2_base + 8 + ...` -> `+ 12 + ...`) made the test fail 4
  checks (`Task size` actual 670 vs 666 and 690 vs 686, and `114/Props`
  differing from Project's save at byte 3402 on both new pairs); the file was
  restored byte for byte and the suite is back to 0 failures
  (`build/w34/mut-W34-3.1-ctest.log`).
- **Limits and refusals (rule 1).**  `assign_resource` accepts only the
  first-assignment shape (every row an unassigned placeholder) and the
  settled append for a task holding one or two real assignments plus one
  free placeholder; counts 3 -> 4 and beyond have no measured file and are
  refused, as is any other layout, row kind, leaf count, Task size scalar or
  roll-up mismatch.  `unassign_resource` accepts only the measured last
  real-row removal.  Every refusal leaves `doc` and the output path
  untouched, and the refusal suite asserts that.
- **Deferred / found, not fixed.**  Removal from a task that holds two or
  three assignments has no pair (the delete pair is 1 -> 0 only); the
  placeholder-row behaviour for the count-3 task is likewise unmeasured.  One
  pair each would extend it.  The DeepSeek session was cut off before this
  entry; the guard-red evidence above is the Sonnet builder's, not the
  original session's.

W34-3.2 detail:

- **Measured shapes (rule 1).**  Two pairs, each against Project's own edit
  save with its committed null save masking per-save noise, through
  `save_pair::compare_all` with the wave-33 `effects` assertion, eleven
  declared streams (six `TBkndRsc`, four `TBkndCal`, `114/Props`) plus
  `Props14`, and no `KnownGap`: `wave30-resource-add` (table rows 2 -> 3;
  noise=137, effects=509) and `wave34-resource-add-third` (rows 3 -> 4;
  noise=147, effects=509).  The only normalisation is the first dword of each
  identity in the appended records (three in the `Cal Fixed2Data` slot, two in
  the `Rsc Fixed2Data` record).  Every other appended byte equals Project's.
- **What the writer does.**  Appends the resource row (a clone of the last
  row, with id, uid and the `+154` counter advanced), its FixedMeta and
  Fixed2Meta entries, the Fixed2Data record, five Var2Data blocks with their
  VarMeta entries, the calendar row, slot and entries, and moves the three
  `114/Props` scalars.  Written through `save_props_edit` and proven from the
  re-read file, with the exact changed-stream set asserted.
- **Gates (this session).**  `build/vs-verify33b` Release build exit 0 with no
  warning (the `CORE_WARNINGS_AS_ERRORS` build); full ctest 61/61;
  `mppwrite_roundtrip_resource_growth` 869 checks, 0 failures, 0 expected
  failures; `check_fixtures.py` 185 documented hashes, 219 tracked `.mpp`;
  `check_markers.py --worktree` OK; client-name scans 0 hits.  Logs
  `build/w34/8b79c62-W34-3.2-*.log`.
- **Guard seen red.**  Changing the Rsc FixedMeta `+13` rule (`0x44 + rows` ->
  `0x45 + rows`) made the test fail 4 checks (the entry check and the
  `TBkndRsc/FixedMeta` pair comparison, on both pairs); restored, 0 failures
  (`build/w34/red1-ctest.log`).
- **Limits and refusals.**  Only row counts 2 and 3 are accepted (the
  FixedMeta `+13` and Fixed2Meta `+8` bytes are per-count and undecoded);
  anything else, a tombstoned row, a MaxUnits other than 100 %, an unmeasured
  `0x55` / `0xe3` block, a calendar row count other than rows + 1, a Props
  count disagreeing with its table, a damaged table header, an unrelated
  third identity or a counter at `0xffff` is refused before `doc` or the
  output path is touched.  Only a work resource with default rates; the
  Created stamp is supplied by the caller.
- **Not constructible.**  A document with the resource or calendar stream
  absent (`CfbDocument` has no remove-stream operation).
- **Deferred / found, not fixed.**  Counts 4 and beyond need a pair (named in
  `calendar-tbkdcal.md`, pairs still needed 4).

W34-3.3 detail:

- **What was there and what was added.**  The writer
  (`write_project_summary`, `core/mppwrite/summary_info.{h,cpp}`) already
  existed from wave 29 and is used by the actuals, outline and assignment
  writers; no committed test ran it against the Calculate or Move pairs.
  `tests/roundtrip_summary_info.cpp` now does, for seven pairs
  (`wave30-calculate`, `wave30-move-back`, `wave30-move-forward`,
  `wave34-calculate-second`, `wave30-inactivate-second`, `wave31-fill`,
  `wave30-task-mode-manual`): the port rebuilds the stream from the control and
  Project's edited task row and it equals Project's edit save byte for byte
  (the stream has no per-save noise, so there is no mask), with every other
  stream untouched.  The changed/unchanged flag for each pair is asserted
  against control vs edit, so the test cannot pass vacuously.  Twelve
  refusals: a short stream, byte order, section count, format identifier, a
  trailing byte, a truncated stream, an unmeasured DurationFormat, an invalid
  start, and a negative / non-finite / fractional / one-hour total work, each
  with the document unchanged.
- **Independent reader.**  `scripts/dsi_reader.py` parses the OLEPS layout
  with no code shared with the C++ (olefile only opens the container);
  `scripts/check_dsi.py <test-out dir>` compares the port's saved file with
  Project's edit save property by property for the seven pairs (7 pairs, 0
  failures), after proving the reader rejects five damaged streams.  Run
  locally; olefile is not installed in CI, so the script is a manual gate
  (listed in the commit's gates) rather than a CI step.  Writing it corrected
  three claims in the decode note's layout prose (LPSTR padding, vector and
  variant packing, the unpadded dictionary); the note is updated.
- **Gates (this session).**  `build/vs-verify33b` Release build exit 0, full
  ctest 62/62; `mppwrite_roundtrip_summary_info` 710 checks, 0 failures;
  `check_dsi.py` 7/7; `check_fixtures.py` OK (185 hashes, 219 `.mpp`);
  `check_markers.py --worktree` OK; client-name scan 0 hits.  Logs
  `build/w34/adfd358-W34-3.3-*.log`.
- **Guard seen red.**  Changing the percent-work suffix in the summary
  rebuild (`"%"` -> `"p"`) failed `mppwrite_roundtrip_summary_info` plus the
  outline and assignment-growth tests that share the writer (DSI differing
  from Project's at byte 337); restored (`build/w34/mut-W34-3.3-ctest.log`).
- **Limits.**  Unchanged from the note: DurationFormat only 21 and 53, a move
  across a DST change, a total-work change outside whole hours and the
  singular "1 hr" are refused or unmeasured.  No pair here needed a new
  measurement.

W34-3.4 detail:

- **Task.Active** (`Task::active`).  Bit 0x40 of the Task Fixed2Meta flags
  byte (+8), clear exactly where COM reports Active=False
  (`W30-1.2-project-inactivate{,-second}.json`: uid 2 on `wave30-inactivate`,
  uid 1 and 2 on `wave30-inactivate-second`, the null saves agreeing with their
  controls).  Defaults to true when the file has no Fixed2Meta entry (MPP12,
  synthetic base).  `test_w34_active`: six files x uids 1..3, the project row,
  and `synthetic-base`.
- **Inserted summary and milestone.**  The reader already produced the right
  ID, level, outline number, Summary / Milestone flags, reparenting and dates
  for the five insert files (summary, milestone, Tasks.Add, and the controls);
  the missing piece was the Fixed2Data +16 ordinal (`Task::outline_ordinal`:
  2.0, 3.0, 4.0 stored, 2.5 for a row inserted between uid 1 and 2, 5.0 for an
  appended row).  `test_w34_insert` asserts ID, level, outline number,
  summary, milestone, manual, minutes and ordinal per uid, plus the inserted
  summary's Start / Finish / child count and the milestone's dates, against
  `W30-1.1-project-insert.json` and `W34-0.5-task-ops.json`.
- **Baseline 0** (`Baseline` on Task, Resource, Assignment).  Var blocks
  0x0b400001/06/1b/2b/2c on tasks (work, cost, duration, start, finish),
  0x0c40000f/11 on resources, 0x0f400010/20/92/93 on assignments, named from
  `field_table.gen.cpp` and sized from the baseline pair.  COM facts
  (`W30-G3-project-pairs.json`) cover tasks only: control NA / 0, set start
  2026-01-04 13:00, finishes 2026-01-09 / 07 / 06 12:00, durations 2400 / 1440 /
  960 min, work and cost 0.  The assignment (dates equal its task's) and
  resource (zero work / cost stored) rows are asserted as measured on that pair
  and labelled so in the test.  Work and cost scale is deliberately not
  claimed: only zeros have COM facts, so the reader exposes `work_raw` /
  `cost_raw`.
- **Leveling** (`W30-2-level.json`, `wave30-level-control` vs `-all`).
  `Task::leveling_delay_ticks` is the u32 at Task row +70 (0x189c0 = 100800
  ticks, COM 10080 min; 0x23280 = 144000, COM 14400 min).
  `Task::preleveled_start` / `preleveled_finish` are the Task var blocks
  0x0b400171 / 0x0b400172 (packed dates equal to the control's COM Start and
  Finish; NA on the never-leveled files).  `Resource::peak_units_x100` is the
  f64 at Resource row +98 (30000.0 -> 10000.0; COM PeakUnits 3.0 -> 1.0);
  `overallocated()` is derived from it because no other resource byte moved
  when COM's Overallocated went True -> False.  The "+70 RemainingDuration"
  comment in `reader.cpp` is not stale: it sits in the MPP12 layout table
  (264-byte rows), where +70 is a different field from the MPP14 row's.
- **Not claimed.**  Assignment LevelingDelay (COM reads 0 on every assignment
  in the pair, so no byte can be tied to it); baselines 1..10; the work /
  cost scale of a nonzero baseline.
- **Gates (this session).**  `build/vs-verify33b` Release build exit 0
  (`CORE_WARNINGS_AS_ERRORS`), full ctest 62/62 (`mpp_reader` includes the
  four new groups); `check_fixtures.py` OK (219 `.mpp`); `check_markers.py
  --worktree` OK; client-name scan 0 hits; `guard_names.py main..HEAD` clean.
  Logs `build/w34/a08a9ff-W34-3.4-*.log`.
- **Guard seen red.**  Three reader constants changed together (the Active
  bit 0x40 -> 0x20, the leveling-delay offset 70 -> 66, the peak-units offset
  98 -> 106): `mpp_reader` failed with the Active, LevelingDelay and
  PeakUnits checks naming the files and uids; restored
  (`build/w34/red-W34-3.4.log`).

G3 group gates (fresh trees configured from scratch on `2735324` under
`build/w34-g3-fresh{,-app,-qt}`, log `build/w34/W34-G3-fresh-gates.log`):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 61/61 passed (55.60 s), including the four new `mpp_reader` groups and `mppwrite_roundtrip_summary_info` |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine self-test (bare exe / `engine_diff.py --selftest`) | 265 checks, 0 failures / 53 checks, 0 failures, exit 0 |
| engine `--suite --expect-fail constraint-mso` | 8 scenarios, 7 pass, 1 fail, 0 unrecorded, expected set reached, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1469 checks / 789 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, 83/83 rows, 0 unexplained, exit 0 |
| app configure/build with `CORE_WARNINGS_AS_ERRORS=ON`; ctest | exit 0 / exit 0; 25/25 passed (23.62 s) |
| app_qt configure/build with `CORE_WARNINGS_AS_ERRORS=ON`; ctest | exit 0 / exit 0; 41/41 passed (58.68 s) |
| `check_fixtures.py` | 185 documented hashes, 219 tracked `.mpp`, 268 test-data files, property sets OK, provenance OK |
| scans on `2735324` | check_markers 1,429 files, 0 hits; messages `main..HEAD` 0 hits; guard_names exit 0; client-name scan 0 files, 0 name hits, 0 commit messages |

The run started 10:46 and finished 11:08 on 08 November 2026.  The numbers
belong to `2735324`, the code tree this record commit follows.

WAVE-34-G3-COMPLETE

## G4 - enablements: refuse the unmeasured paths, then enable each measured shape

Builder: Claude Sonnet 5.5 (W34-BUILDER), commits carry the
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 34)` trailer.

| item | finding | status | evidence |
|---|---|---|---|
| W34-4.0 | brief W34-4.0: Insert Task, Delete Task, link ADD, custom-field SET and CLEAR differ from Project's own save (the W34-0.5 KnownGap tables); refuse or show as noise | **DONE** (all five refused up front) | the `W34-4.0` commit after `3a8a31c`; `app/edit_model.{h,cpp}`, `app/field_link_edits.cpp`; `app/app_unmeasured_paths_tests.cpp`, `app_qt/tests/unmeasured_paths_test.cpp`; gates `build/w34/3a8a31c-W34-4.0-*.log` |
| W34-4.1 | brief W34-4.1: Resource Information rename (G1); Notes only if the RichEdit serialisation is decoded for plain single-line ASCII | **DONE for the rename; Notes DEFERRED with evidence** (stays disabled, exact tooltip) | the `W34-4.1` commit after `9c5f497`; `app/edit_model.{h,cpp}`; `app/app_edits_resource_rename_tests.cpp`; `app_qt/tests/resource_information_gate_test.cpp`; Qt shell `app_qt/{ribbon_bar,dialogs,main_window}.*`; gates `build/w34/9c5f497-W34-4.1-*.log` |
| W34-4.2 | brief W34-4.2: Add Resources (one work resource, default rates) | **DONE** (enabled for a project with one or two resources; every other shape refused up front) | the `W34-4.2` commit after `726dcfd`; `core/mppwrite/resource_growth.{h,cpp}` (`dry_run`), `app/{edit_model,menu_model,main.mm}`; `app/app_edits_resource_add_tests.cpp`; Qt shell `app_qt/{ribbon_bar,dialogs,main_window,command_status}.*`, `app_qt/tests/resource_add_gate_test.cpp`; gates `build/w34/726dcfd-g42c-*.log` |
| W34-4.3 | brief W34-4.3: Assign Resources (one work resource at 100 % to an unstarted leaf, and remove it) | **DONE** (enabled for the first assignment of a task and for removing a task's one assignment from a settled file; every other shape refused up front) | the `W34-4.3` commit after `399902d`; `core/mppwrite/assignment_growth.{h,cpp}` (`dry_run`), `app/edit_model.{h,cpp}`; `app/app_edits_assignment_tests.cpp`; Qt shell `app_qt/{ribbon_bar,dialogs,main_window}.*`, `app_qt/tests/assign_resources_gate_test.cpp`; gates `build/w34/399902d-g43f-*.log` |
| W34-4.4 | brief W34-4.4: Insert Summary and Insert Milestone on a clean last row (W33-1.2's conditions) | **DEFERRED with evidence** (both stay disabled, "future: not implemented in this port"; no write path exists) | probe `build/w34/4.4-probe.diff`, log `build/w34/4.4-probe-insert-ctest.log` (15 failing streams per pair, summary and milestone); evidence `spec/writers/evidence/wave30-insert-summary.md`, `spec/writers/table-growth.md` Limits |
| W34-4.5 | brief W34-4.5: Font Bold on one cell (G2.1); the other font controls stay disabled | **DONE** (enabled for the one measured cell, the Task Name cell of UniqueID 1 on an unformatted view, alone in the save; every other cell refused up front) | the `W34-4.5` commit after `f950a03`; `core/mppwrite/viewstream_edit.{h,cpp}` (`dry_run`), `app/edit_model.{h,cpp}`; `app/app_edits_cell_bold_tests.cpp`; Qt shell `app_qt/{ribbon_bar,main_window}.*`, `app_qt/tests/cell_bold_gate_test.cpp`; gates `build/w34/f950a03-b45all-*.log` |
| W34-4.6 | brief W34-4.6: WBS code definition (G2.2) with the measured prefix shape only | **DONE** (Project > Properties > WBS > Define Code... enabled for a project of exactly three flat tasks that stores no WBS text; every other shape refused up front; Renumber... stays disabled) | the `W34-4.6` commit after `b07d435`; `core/mppwrite/wbs_code.{h,cpp}`, `varmeta_edit.{h,cpp}` (uid-0 allowance), `tests/roundtrip_wbs.cpp`; `app/edit_model.{h,cpp}`, `app/app_edits_wbs_tests.cpp`; Qt shell `app_qt/{ribbon_bar,main_window}.*`, `app_qt/tests/wbs_code_gate_test.cpp`; gates `build/w34/b07d435-g46final-*.log` |
| W34-4.7 | brief W34-4.7: Set Baseline 0 for the entire project (G1 + G2.2 + G3.4), no other baseline | **DEFERRED with evidence** (the Set Baseline menu stays disabled, "not implemented in this port"; the retained writers cannot produce Project's save) | probe `build/w34/4.7-probe.cpp`, log `build/w34/4.7-probe-ctest.log` (19 streams fail with the one block the writers can append); evidence `spec/writers/evidence/wave30-baseline.md`, `spec/writers/props-records.md`, `spec/writers/table-growth.md` Limits |

W34-4.0 detail:

- **Refuse, not show-as-noise.**  Every difference in the five KnownGap tables
  is a real stream difference between the port's save and Project's own save
  of the same edit (not per-save noise that the null save masks), so the
  honest outcome is standing rule 1: no save-path edit is enabled until it is
  measured.  Per path (stream counts are the named `KnownGap` entries):

  | path | KnownGap table | differing streams | outcome |
  |---|---|---|---|
  | Insert Task | `tests/roundtrip_add_delete.cpp` | 15 | refused |
  | Delete Task | `tests/roundtrip_add_delete.cpp` | 6 | refused |
  | link ADD | `tests/roundtrip_links.cpp` | 13 | refused |
  | custom-field SET | `tests/roundtrip_field_values.cpp` | 2 | refused |
  | custom-field CLEAR | `tests/roundtrip_field_values.cpp` | 5 | refused |

- **Where the refusal sits.**  `app::unmeasured_path_refusal(StructuralKind)`
  returns the decision-9 text ("... is not implemented in this port: its save
  differs from Project's in N stream(s)") and is called from the shared
  builders both shells use (`check_insert_task`, `make_delete_edit`,
  `validate_link_add` / `make_link_add`, `make_task_field_edits`), from
  `StructuralEdits::push` (a hand-built queue cannot bypass the builders) and
  from the `save_structural_edits` cascade before its first write (a queue
  built under the test-only opt-in cannot be saved).  The Qt shell and the
  macOS shell both inherit it: the Qt handlers put the builder's text in the
  status line, the macOS handlers pass it to `note(plan, refusal, true)` or
  `show_alert`.  The ribbon controls stay enabled, so `ribbon_pin.tsv` and the
  command backlog still agree (`check_command_backlog.py`: 114 pin controls,
  114 rows, 0 mismatches); the refusal is on invocation, with the reason.
- **Measured kinds untouched.**  `delete_link`, `set_percent`,
  `set_project_priority`, `update_actuals`, outline indent / outdent,
  `set_new_task_mode` and `set_task_mode` are not refused
  (`test_other_kinds_untouched`).
- **Existing tests.**  The retained writers and the cascade are still
  exercised by 7 app tests and 7 Qt tests through
  `app::allow_unmeasured_paths_for_tests(true)`, called on the first line of
  each `main`.  Two dedicated programs never call it and prove the shipped
  default: `app_unmeasured_paths` (builder, queue and cascade for each of the
  five paths; 43 checks) and `qt_unmeasured_paths` (the real MainWindow: Insert
  Task opens no prompt and queues nothing, Delete Task shows no confirmation
  and queues nothing, the Task Information custom-field SET queues nothing and
  names the refusal).  The two smoke steps that used to queue a text value and
  a link now assert the refusal.
- **Guard seen red.**  With `g_unmeasured_paths_allowed` initialised to `true`
  `app_unmeasured_paths` failed on every refusal check, from "the refusal is
  on by default" through the cascade (`build/w34/3a8a31c-W34-4.0-mutation-app-ctest.log`);
  restored before the gate run.
- **Gates (this session).**  core build exit 0, ctest `-E plan_recalc` 61/61;
  app build exit 0, ctest 26/26; Qt build exit 0, ctest 42/42
  (`3a8a31c-W34-4.0-qtfinal-qt-ctest.log`); `check_fixtures.py` OK;
  `check_markers.py --range main..HEAD` OK; client-name scan 0 hits;
  `guard_names.py main..HEAD` clean.  The first all-tree Qt run showed
  `qt_task_order` and `qt_task_order_corpus` failing with "unknown error":
  running the executable directly gave "An Application Control policy has
  blocked this file" (a machine policy on that one freshly linked binary; it
  had run earlier in the same session).  A normal `--clean-first` rebuild of
  the one target ran both tests green; the policy was not touched
  (`3a8a31c-W34-4.0-qt-ctest.log` keeps the blocked run).
- **Not verified here.**  `app/main.mm` cannot be built on Windows; the macOS
  handlers were read, not run.  No macOS-side change was needed because the
  refusal is in the shared builders.

W34-4.1 detail:

- **Measured shape.**  The pair is `tests/data/wave30-resource-rename-control.mpp`
  -> `wave30-resource-rename.mpp` (Project 2021 renamed resource UniqueID 2 to a
  longer name and saved) plus Project's own no-edit save of the control.  The
  port's save (`mppwrite::rename_resource`, the W34-1.3 writer, reached through
  the `save_structural_edits` cascade) is compared stream by stream with
  Project's: `app_edits_resource_rename` 304 checks, noise 154 bytes masked,
  **effects 16** bytes, the four owned streams (114/Props, `TBkndRsc` VarMeta
  and Var2Data, Props14) declared changed so a mask that swallowed the change
  fails `effects_ok`; the port's save reopens in the independent core/mpp reader
  with Project's name.
- **Enabled only for that shape.**  A new exclusive `StructuralKind::
  rename_resource` (`make_rename_resource_edit`, shared by both shells) takes a
  strictly longer name than the stored one (the pair grew the name; the shrink
  and same-size shapes are unmeasured), printable ASCII only, trimmed and
  non-empty, a resource that exists, a resource with no assignment (the pair
  renamed an unassigned resource), and it must be the only edit in the save (no
  task-mode, start-date or other structural edit beside it).  Every other shape
  is refused in the builder, in `push()` and in the save cascade with the
  decision-9 text ("... is not implemented in this port: ...").  The project row
  (uid 0), an unknown resource, empty / blank / control / non-ASCII names, an
  unchanged, a same-size and a shorter name, an assigned resource, queue and
  cascade exclusivity, and a stale file (Project's saved file as the source:
  "not larger") each have a check.
- **Shells.**  Qt: `RibbonCommand::resource_information` enables the Resource
  tab's Information (pin and backlog row regenerated: `implemented`, with the
  limits as its gap text; `check_command_backlog.py` 114 / 114 / 0
  mismatches), an input dialog asks for the name, and `MainWindow::
  rename_selected_resource()` refuses read-only, the wrong view and an empty
  selection before it asks.  The gate `qt_resource_information_gate` drives the
  real window: wiring, the Notes tooltip, the Task Sheet and no-selection
  refusals (no prompt, nothing queued), Cancel, shorter / same-size / non-ASCII
  refusals (status line, nothing queued, document clean), the longer name
  queued, shown in the plan and the Resource Sheet, then the real save and a
  reopen.  macOS: `app/main.mm` has no Resource Information handler and no
  ribbon, so there is no shell to enable there; the rule lives in the shared
  builder, which a future handler must call.  The Resource Sheet did not rebuild
  after a queued edit (the first Qt run failed "the Resource Sheet cell shows
  the new name"); `after_structural_change` now rebuilds a non-task view from
  the displayed plan and restores the current cell, which also covers undo.
- **Notes DEFERRED, with evidence.**  The brief enables Resource Notes and Task
  Notes only if the RichEdit serialisation is decoded for plain single-line
  ASCII.  The layout is decoded (`spec/writers/note-richtext.md`: 159-byte
  prefix, escaped text, 10-byte suffix; wave30-notes, wave30-resource-notes,
  wave34-note-*), but the prefix embeds the saving machine's RichEdit generator
  string, `Riched20 16.0.20430` in every wave-30 fixture and `16.0.20527` in
  every wave-34 fixture, both saved on this one machine: a Windows update
  between the waves changed the bytes.  The note's own W34-4.1 condition is
  that a writer must reproduce or refuse the saving machine's generator, and
  the port (which also runs on macOS) cannot read what Project's RichEdit would
  write, so a note save would be unmeasured on every machine but the one that
  measured it.  Both Notes controls stay disabled with "not implemented in this
  port"; the Resource tab's Notes row in `docs/ui-command-backlog.md` is
  unchanged.
- **Guard seen red.**  The app pair test with the push guard and the display
  application broken failed five checks ("the display shows the new name",
  redo x2, the assigned-resource refusal x2;
  `build/w34/9c5f497-4.1-mutation-app-ctest.log`), then green restored
  (`...-4.1-restored-app-ctest.log`).  The Qt gate failed its cell check before
  the view rebuild (`...-4.1-qt-first-qt-ctest.log`) and passed after
  (`...-4.1-qt-second-qt-ctest.log`).
- **Gates (this session).**  core build exit 0, ctest `-E plan_recalc` 61/61;
  app build exit 0, ctest 27/27; Qt build exit 0, ctest 43/43
  (`9c5f497-W34-4.1-{core,app,qt}-ctest.log`); `check_fixtures.py` OK;
  `check_markers.py --worktree` and `--range main..HEAD` OK;
  `guard_names.py main..HEAD` clean; client-name scan 0 hits.
- **Not verified here.**  `app/main.mm` was not touched (no handler to build).

W34-4.2 detail:

- **Measured shapes.**  Two pairs, each with Project's own no-edit save of the
  control: `wave30-resource-add-control.mpp` -> `wave30-resource-add.mpp`
  (one saved resource, "R2" added, new uid 2) and
  `wave34-resource-add-third-control.mpp` -> `wave34-resource-add-third.mpp`
  (two saved resources, "R3" added, new uid 3).  The port's save
  (`mppwrite::add_work_resource`, the W34-1.4 writer, reached through the
  `save_structural_edits` cascade) is compared stream by stream with Project's
  over the 12 declared streams: `app_edits_resource_add` 815 checks, **effects
  509** bytes per pair (the evidence docs' figure), the identity dwords compared
  under `TailNormalisation`; untouched streams byte-equal; the save passes
  `check_table` and reopens in the independent core/mpp reader.  The display
  row is compared field by field with Project's own saved row (the first run
  failed Accrue At and base calendar; both now come from the project's last
  resource, the template the writer clones).
- **Enabled only for those shapes.**  A new exclusive `StructuralKind::
  add_resource` (`make_add_resource_edit`, shared by both shells) takes a
  trimmed, non-empty, printable-ASCII name of at most 64 characters; a project
  whose last resource is a work resource with zero standard, overtime and
  cost-per-use rates (default rates); exactly one or two saved resources (the
  writer accepts table rows 2 and 3 only); a file that still matches the open
  plan; and it must be alone in the save.  The builder runs the writer in a new
  `ResourceGrowthSpec::dry_run` mode, which validates and reports without
  writing, so a writer refusal reaches the operator as "Add Resources is not
  implemented in this port for this project: <reason>" and queues nothing.  The
  save cascade repeats the dry run and refuses a uid or ID that is not the one
  the edit queued.  `push()` is a backstop: plan present, uid and ID non-zero
  and unused, one or two plan resources, Created stamp valid, template and
  name valid.  Undo and redo re-apply the display row.
- **Shells.**  Qt: Resource > Insert > Add Resources (`MenuAction::insert_resource`) is
  live (pin row, `command_status.h` and the backlog regenerated, 114 / 114 / 0
  mismatches; ribbon, menu and pin counts: 114 ribbon (67 disabled), 51 menu
  (6 disabled), pin 49 enabled / 65 disabled); an input dialog asks for the name;
  `MainWindow::add_resource()` refuses read-only and no document.  The gate
  `qt_resource_add_gate` drives the real window: the button and Insert >
  Resource are live; Cancel, blank and non-ASCII names queue nothing; a name
  queues one edit, dirties the document and shows in the plan and the Resource
  Sheet; a second Add while one is pending is refused; the real save and reopen,
  twice (second, then third resource); a three-resource file refuses a fourth.
  `ribbon_inventory_test` carries the wiring row.  macOS: `Insert > Resource`
  now calls `-[AppDelegate addResources:]` (NSAlert name prompt, same builder).
- **Guards seen red.**  `is_exclusive_kind`, the template refusal, the cascade
  start-date refusal, the stale-uid dry-run check, the builder's plan-size
  check, the Accrue At display copy and the menu `!editing` condition each
  failed the app tests when broken (`build/w34/mut-*-ctest.log`); the Qt status
  message broken failed "the status line reports the queued resource" in both
  adds (`mut-qtmsg-ctest.log`).  Two checks are shadowed by design: push's
  resource-count check (the uid check also refuses that case) and the cascade
  `applied().size() != 1` check (push prevents a hand-built queue).
- **Tooling trap.**  `build/w34/mutate.sh` restored the file with an older
  mtime, so the next build kept the mutated object and the gate failed on a
  clean source; the helper now preserves and touches the file on restore, and
  the sources were touched and rebuilt before the gate run below.
- **Gates (this session).**  core build exit 0, ctest `-E plan_recalc` 61/61;
  app build exit 0, ctest 28/28; Qt build exit 0, ctest 44/44
  (`726dcfd-g42c-{core,app,qt}-ctest.log`); `check_command_backlog.py` 114 / 114
  / 0; `check_fixtures.py` OK; `check_markers.py --worktree` OK; `guard_names.py
  main..HEAD` clean; client-name scan 0 hits.
- **Not verified here.**  `app/main.mm` (`addResources:`) cannot be built on
  Windows.

W34-4.3 detail:

- **Measured shapes.**  Two pairs, each with Project's own no-edit save of the
  control: `wave30-assign-add-control.mpp` -> `wave30-assign-add.mpp` (task 1
  held only Project's unassigned placeholder; Project assigned resource 2, 100 %,
  new assignment uid 4) and `wave30-assign-delete-control.mpp` ->
  `wave30-assign-delete.mpp` (the same assignment removed; the placeholder
  returns as uid 5).  The port's save (`mppwrite::assign_resource` /
  `unassign_resource`, the W34-3.1 writers, reached through the
  `save_structural_edits` cascade) is compared stream by stream with Project's
  over the declared streams (14 for the add, 13 for the removal):
  `app_edits_assignment` 784 checks, **effects 1012** bytes for the add and
  **905** for the removal, the identity dwords of the appended Assn Fixed2Data
  record compared under `TailNormalisation`; untouched streams byte-equal; the
  save passes `check_table` and reopens in the independent core/mpp reader.
  The displayed assignment row is compared field by field with the row the
  reader reads from Project's own saved file (uid, task, resource, raw resource
  word, work, units, start, finish) and the task's physical `assignment_rows`
  follows Project's.  The display puts the new row last (the writer appends it
  and the reader returns file order); the first run failed only on that order
  and was fixed in `apply()`.
- **Enabled only for those shapes.**  Two exclusive kinds,
  `StructuralKind::assign_resource` and `unassign_resource`, built by
  `make_assignment_edit` (shared by both shells).  The plan-side rules, in
  order: the task is a saved task, the resource a saved resource; the task is
  not a summary; the task is unstarted (0 %, no actual start or finish, no
  actual duration); the resource is a work resource with Max. Units 100 %; an
  assign needs a task with no real assignment; a removal needs the task's one
  assignment, naming that resource.  The builder then runs the writer in a new
  `dry_run` mode (validate, report, write nothing) against the FILE, so a writer
  refusal reaches the operator as "Assign Resources is not implemented in this
  port for this project: <reason>", and a plan that disagrees with the file is
  refused as stale.  The cascade repeats the dry run and refuses an assignment
  uid that is not the one the edit queued; a start-date edit beside an
  assignment is refused before anything is written.  `push()` is a backstop.
  The settled-append pairs (second and third assignment on a task,
  `wave34-assign-add-second` / `-third`) are measured in core but are not
  enabled here: the brief asks for one work resource, so a task that already
  holds an assignment is refused.
- **Shells.**  Qt: `RibbonCommand::assign_resources` (Resource > Assignments >
  Assign Resources) is live; pin row, backlog row and the ribbon-inventory
  wiring row regenerated (114 / 114 / 0; 66 disabled in the table; pin 50
  enabled / 64 disabled).  `MainWindow::assign_resources()` takes the selected
  task: an unassigned task is offered the resource list, an assigned task a
  Yes / No removal question; the builder owns every refusal.  The gate
  `qt_assign_resources_gate` drives the real window: the button is live and
  wired; Cancel queues nothing; choosing the resource queues one edit, dirties
  the document and shows the real assignment; a second click is refused and the
  queue keeps one; the real save and reopen read the assignment back; the
  removal on the port's own save is refused up front; the removal on Project's
  own settled delete control queues, saves and reopens as the placeholder.
  macOS: `app/main.mm` was not touched (there is no menu action for the
  command, as for W34-4.1).
- **Guards seen red.**  Four mutations each turned a gate red and were
  restored: the summary-task refusal (`mut-m43a`: "a summary task"), the
  actual-duration test (`mut-m43b`: "a task with an actual duration"), the
  task's `assignment_rows` increment (`mut-m43c`: add 1 vs 2, remove 2 vs 3)
  and the ribbon button's enabled flag (`mut-m43d`: the Qt gate and ribbon
  inventory).  Sources were touched and rebuilt before the gate run below.
- **Gates (this session).**  core build exit 0, ctest `-E plan_recalc` 61/61;
  app build exit 0, ctest 29/29; Qt build exit 0, ctest 45/45
  (`399902d-g43f-{core,app,qt}-ctest.log`); `check_command_backlog.py` 114 /
  114 / 0; `check_fixtures.py` OK; `check_markers.py --worktree` OK;
  `guard_names.py main..HEAD` clean; client-name scan 0 hits.
- **Found, not fixed (4.3).**  A removal on a file the PORT saved after an
  assignment is refused ("the assignment FixedMeta is not the measured
  (rows - 1) / rows / flag 4 settled shape"): the removal pair starts from a
  file Project saved, and the port's add leaves the freshly-appended state.
  The refusal is correct under standing rule 1; enabling the sequence "assign,
  save, remove" needs a Project pair that starts from the port's own output.
- **Not verified here.**  `app/main.mm` was not touched.

W34-4.4 detail:

- **Brief item.**  Enable Insert Summary and Insert Milestone for the clean
  last-row shape that W33-1.2 (`app::check_insert_task`,
  `insert_task_clone_is_clean`, `last_live_task_uid`) defines, behind G1 and
  G3.4, each refusing every other shape up front, with a field-by-field
  Project pair test.  Deferral needs evidence of an attempt.
- **Attempt.**  A probe in `tests/roundtrip_add_delete.cpp` (kept as
  `build/w34/4.4-probe.diff`, reverted afterwards) ran the retained
  `add_task_row` insert writer against the two measured pairs,
  `wave30-insert-summary` and `wave30-insert-milestone`, through the same
  `compare_all` path as `test_w34_insert_pair`.  Result
  (`build/w34/4.4-probe-insert-ctest.log`): **15 streams fail for each pair**,
  the same 15 that `kInsertGaps` already names as KnownGap for Insert Task:
  114/Props, Props14, TBkndAssn (Fixed2Data, Fixed2Meta, FixedData,
  FixedMeta, Var2Data, VarMeta), TBkndTask (Fixed2Data, Fixed2Meta,
  FixedData, FixedMeta, Var2Data, VarMeta) and 214/CEdl/Var2Data.  For the
  summary pair the assignment Var2Data and VarMeta also fail the effects
  assertion ("the edit's declared effect set is non-empty").
- **Why it cannot be closed here.**  What the pairs show Project doing beyond
  what the writer does: it renumbers the IDs of the existing rows;
  reparents the rows under a new summary; writes the 2.5 Fixed2Data ordinal;
  sets the summary and milestone words in the task fixed block; adds a
  tombstoned (summary) or live placeholder (milestone) assignment row with
  contour blocks in TBkndAssn; changes about 120 bytes of 214/CEdl/Var2Data;
  and changes typed properties in the Props tail.  None of those rules is
  decoded in `spec/writers/table-growth.md` (Limits, W34-0.5), so enabling
  either command would write a file that differs from Project's own save in
  14 or more streams, which standing rule 1 forbids.
- **State left.**  The ribbon rows stay disabled with the exact tooltip
  "future: not implemented in this port" (`docs/ui-command-backlog.md` lines
  40-41, regenerated by `app_qt/tools/command_backlog.py`; no change, so the
  counts stay 114 / 114 / 0).  W34-4.0 already refuses Insert Task up front,
  so no shell has an enabled insert path.  No source file changed in this
  commit.
- **Found, not fixed (4.4).**  Decoding the insert rule set (ID renumber,
  reparent, ordinal, summary / milestone words, the assignment placeholder
  rows, CEdl and the Props tail) from the two wave-30 pairs plus at least one
  insert pair with a non-trivial outline is the prerequisite; the pairs exist,
  the decode does not.

W34-4.5 detail:

- **Measured shape.**  One pair, with Project's own no-edit save of the control:
  `wave30-textstyle-control.mpp` -> `wave30-textstyle.mpp`.  Project bolded the
  Task Name cell of the task with UniqueID 1 and nothing else.  The port's save
  (`mppwrite::save_view_cell_style`, W34-2.1, reached through the
  `save_structural_edits` cascade) is compared stream by stream with Project's:
  `app_edits_cell_bold` 373 checks, **effects 6607** bytes (null-save noise 131),
  untouched streams byte-equal, the save passes the table check and reopens in
  the independent core/mpp reader.  The writer appends one 56-byte cell-format
  item (`0x2240000c`, row uid, field id, flags `0x01000003`, font word
  `0x90000001`) to the applied view's main block and re-points the five later
  VarMeta offsets, the VarMeta header, the view-size value and the Props marker.
- **Enabled only for that cell (brief rule 2).**  One data point is one cell, so
  the plan-side rule is the pair's own cell and nothing wider: a saved task, the
  Task Name column, UniqueID 1.  Another row or another column is refused with
  "... is not implemented in this port" and the reason (the row text says a
  second pair has not been measured).  The builder `make_cell_bold_edit` (shared
  by both shells) then runs the writer in a new `dry_run` mode against the FILE,
  so a view that already stores a cell format, or any other unmeasured framing,
  is refused as "Font Bold is not implemented in this port for this project:
  <reason>".  The edit (`StructuralKind::bold_cell`) is exclusive; the cascade
  repeats the dry run and refuses a start-date edit beside it.  `push()` is a
  backstop.  The cascade's own "only edit in a save" check is unreachable
  through `push()` (exclusivity refuses the second edit first), so no test can
  turn it red; it stays as a defence.
- **Shells.**  Qt: `RibbonCommand::font_bold` (Task > Font > B) is live; I and U
  stay disabled.  Pin row, backlog row and the ribbon-inventory wiring row
  regenerated (114 / 114 / 0; 65 disabled in the table; pin 51 enabled / 63
  disabled).  `MainWindow::bold_selected_cell()` takes the selected cell; a
  range is refused before anything is built.  The gate `qt_cell_bold_gate`
  drives the real window: B is live and wired, I and U disabled; another row, another
  column of the measured row and a two-cell range each queue nothing and name
  the refusal; the measured cell queues one edit and dirties the document; a
  second click keeps the queue at one; the real save differs from the control;
  a reopen of the saved file refuses a second Bold (the view stores a format).
  macOS: `app/main.mm` was not touched (there is no menu action for the
  command).
- **Guards seen red.**  Six mutations each turned a gate red and were restored
  (sources touched and rebuilt afterwards): the UniqueID 1 restriction and the
  Task Name restriction (`mut-b45-uid`, `mut-b45-field`), the start-date
  refusal in the cascade (`mut-b45-startdate`), the ribbon enabled flag
  (`mut-b45-ribbon`) and the Qt range refusal (`mut-b45-range`).  A first
  field mutation that did not compile left a stale binary and was redone with a
  compiling form; the "only edit in a save" mutation stayed green (unreachable
  backstop, above).
- **Gates (this session).**  core build exit 0, ctest `-E plan_recalc` 61/61;
  app build exit 0, ctest 30/30; Qt build exit 0, ctest 46/46
  (`f950a03-b45all-{core,app,qt}-ctest.log`); `check_command_backlog.py` 114 /
  114 / 0; `check_fixtures.py` OK; `check_markers.py --worktree` OK.
- **Found, not fixed (4.5).**  (1) Bold on any other cell, a range, a view that
  already stores a cell format, Bold turned off, Italic, Underline, font name,
  size and colour need further Project pairs, each varying one factor.  (2) The
  Task Sheet does not paint the stored font word: a bolded cell reads back as
  bold only in Project; the port's display and the plan do not render it.
- **Not verified here.**  `app/main.mm` was not touched.

W34-4.6 detail:

- **Measured shape.**  One pair, with Project's own no-edit save of the control:
  `wave30-wbs-control.mpp` (project row plus three flat tasks, UniqueIDs 1-3) ->
  `wave30-wbs-code.mpp` (prefix `W`, ordered two-digit numbers).  Project's edit
  is four Task var blocks (the project row's last-issued number `03` under key
  `0x0B40019F`, and `01`..`03` under key `0x0B40018D` for the three tasks), the
  matching FixedMeta presence bits (+40 `0x04`, +20 `0x80`), the VarMeta count
  42 -> 46, Var2Data 512 -> 552, and two 114/Props directory records growing
  (`0x0240138B` 4 -> 24 B holding the mask/prefix definition, `0x024013BE` 2 ->
  6 B holding the prefix) with the Props14 marker.  `mppwrite::save_wbs_code`
  composes `save_var_record` x4 and `save_props_edit` and reproduces every byte:
  `mppwrite_roundtrip_wbs` 340 checks, 0 failures, no KnownGap.  The two Props
  records grow mid-stream, so control and null are zero-filled to the edit's
  coordinates before `compare_all` (harness unchanged).  The chain runs on a
  scratch file and the target is written once after every stream is proven
  (only FixedMeta, VarMeta, Var2Data, 114/Props and Props14 may change), so a
  refused save leaves an existing target byte-identical.
- **Enabled only for that shape (brief rule 2).**  `make_wbs_code_edit` (shared
  by both shells) refuses, with "Define WBS Code is not implemented in this
  port ...": any task count but three, a pending task, UniqueIDs other than
  1, 2, 3, a summary or nested task; then a dry run against the FILE refuses a
  file that already stores WBS text or a code definition, a stale or truncated
  table, and a non-empty directory record.  `StructuralKind::define_wbs_code`
  is exclusive; the cascade repeats the dry run and refuses a start-date edit
  beside it; `push()` is a backstop.  `app_edits_wbs`: 368 checks, **effects 216**
  (8+108+48+40+11+1), refusals, exclusivity, and a refused save leaves the target
  untouched.
- **Shells.**  Qt: the WBS split's Define Code... item is live with its own
  tooltip, Renumber... stays disabled with the exact tooltip, and
  `MainWindow::define_wbs_code()` surfaces the builder's refusal in the status
  line.  Backlog row WBS now `implemented` with its gap text (114 / 114 / 0; 64
  disabled in the table; the pin counts the control, 1 split remains live with
  all items disabled).  `qt_wbs_code_gate` drives the real window: another
  shape queues nothing and names the refusal; the control queues one edit; a
  second click keeps the queue at one; the real save differs from the control;
  a reopen refuses a second definition.  `project_dialog_gate_test` expects the
  new item state.  macOS: `app/main.mm` not touched.
- **Guards seen red.**  Twelve mutations each turned a gate red and were
  restored (trees rebuilt afterwards): task-row count, existing-WBS-text check,
  both presence bits, the mask payload, the prefix payload, the uid-0 allowance
  (red in `roundtrip_varmeta`, not in `roundtrip_wbs`), the app task count,
  the outline/summary refusal, the UniqueID set, the pending-task refusal and
  the Qt ribbon item (`build/w34/mut-*.log`).  Two first forms did not compile
  (warnings as errors) and left a stale binary; they were redone with compiling
  mutations (`existing2`, `uid03`) and only those reds are counted.
- **Gates (this session).**  core build exit 0, ctest `-E plan_recalc` 62/62;
  app build exit 0, ctest 31/31; Qt build exit 0, ctest 47/47
  (`b07d435-g46final-{core,app,qt}-ctest.log`); `check_command_backlog.py` 114 /
  114 / 0; `check_fixtures.py` OK; `check_markers.py --worktree` OK.
- **Found, not fixed (4.6).**  (1) Other task counts, outlines (summary tasks
  number differently), prefixes, separators and number lengths need further
  Project pairs, each varying one factor.  (2) Redefining an existing code and
  Renumber... have no pair.  (3) The Define Code dialog's fields are not built:
  the menu item queues the one measured definition directly.  (4) The plan shows
  no WBS column, so the stored numbers are not displayed.
- **Not verified here.**  `app/main.mm` was not touched.

W34-4.7 detail:

- **Brief item.**  Enable Set Baseline 0 for the entire project, no other
  baseline, behind G1 + G2.2 + G3.4, with a field-by-field Project pair test.
  Deferral needs evidence of an attempt.
- **Attempt.**  A probe (`build/w34/4.7-probe.cpp`, registered temporarily in
  `core/mppwrite/CMakeLists.txt` and reverted; the tree is clean) opens
  Project's control `wave30-baseline-control.mpp`, appends the one block the
  retained writers can append for this pair, Task uid 1 BaselineStart
  (`0x0B40002B`, Project's own payload `78 1E F1 3B`), through
  `save_var_record`, and compares every stream with Project's
  `wave30-baseline-set.mpp` through `compare_all` with no KnownGap and the three
  Task streams declared (`build/w34/4.7-probe-ctest.log`).  Result: **19 streams
  fail**: 114/Props (144 bytes), TBkndAssn (Fixed2Meta, FixedMeta, Var2Data,
  VarMeta), TBkndCal (all six streams), TBkndRsc (Fixed2Meta, FixedMeta,
  Var2Data, VarMeta) and TBkndTask (Fixed2Meta, FixedMeta, Var2Data, VarMeta).
- **Why it cannot be closed here.**  What the pair shows Project doing: 94 new
  var blocks (Task 54 over the project row and three tasks, Assignment 27,
  Resource 12, Calendar 1); 26 of them are structured blobs no note decodes (Task
  `0x0B4000AD` 108-148 B, `0x0B4000AE` 76 B, `0x0B4000B3`; Assignment
  `0x0F400034` 88 B, `0x0F400035` 76 B, `0x0F40010B`; Resource `0x0C400044`
  68 B, `0x0C400045` 36 B; Calendar `0x0D400001` 96 B); a new calendar row
  (uid 4: TBkndCal FixedData 100 -> 112, Fixed2Data +48); presence bits in four
  tables' FixedMeta and Fixed2Meta; and a 110-byte tail growth of 114/Props with
  the +12 word 262 -> 263 (the region `spec/writers/props-records.md` already
  records as not rebuildable).  The retained var writer handles the Task and
  Resource tables only, one record per call, and refuses the project row and
  every Assignment or Calendar table; the baseline cost, work and duration
  payloads are scheduler outputs.  None of these rules is decoded, so enabling
  the command would write a file that differs from Project's own save in at least
  19 streams, which standing rule 1 forbids.
- **State left.**  Project > Schedule > Set Baseline stays disabled with the
  exact tooltip; no source file changed in this commit.

## Found, not fixed (G4)

- **G4: port-saved assignment removal (4.3).**  Removing an assignment is
  enabled only from a settled, Project-saved file; a file the port itself
  saved is refused until a pair measures it.
- **G4: Resource Notes (4.1).**  Stays disabled: the payload embeds the saving
  machine's RichEdit generator string, which this port cannot reproduce.
- **G4: Insert Summary and Insert Milestone (4.4).**  Deferred; 15 streams per
  pair fail with the retained insert writer (renumbering, reparenting, the 2.5
  ordinal, assignment placeholder rows, CEdl and Props changes are undecoded).
- **G4: Font Bold (4.5).**  One cell only; other cells, ranges, a view that
  already stores a cell format, Bold off, Italic, Underline, font name, size and
  colour need further pairs.  The Task Sheet does not paint the stored bold.
- **G4: WBS code (4.6).**  Other task counts, outlines, prefixes, separators
  and number lengths, redefining a code and Renumber... have no pair; the
  Define Code dialog's fields are not built; the plan shows no WBS column.
- **G4: Set Baseline 0 (4.7).**  Deferred; 94 new var blocks (26 undecoded),
  a new calendar row, four tables' presence bits and the Props tail growth are
  needed; Set Baseline stays disabled.

G4 gates on the group head `4298770`, configured from scratch with
`CORE_WARNINGS_AS_ERRORS=ON` (`build/w34/W34-G4-fresh-gates.log`): core build
and ctest `-E plan_recalc` 62/62, plan_recalc with and without corpus, engine
diff self-tests and suite, reader tests with and without corpus, M2 recalc
compare, app build and ctest 31/31, Qt build and ctest 47/47, fixtures and
oracle provenance, marker scans (worktree and messages), guard names and the
client-name scan: every step exit 0.

WAVE-34-G4-COMPLETE
