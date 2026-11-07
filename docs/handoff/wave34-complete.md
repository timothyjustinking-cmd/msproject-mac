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
| W34-3.2 | brief W34-3.2: the resource calendar row (Add Resources writes the `TBkndCal` streams and three `114/Props` counts, not only the six `TBkndRsc` streams) | **DONE** | commit titled `W34-3.2` (the next commit after `8b79c62`); `core/mppwrite/resource_growth.{h,cpp}`; `tests/roundtrip_resource_growth.cpp`; `spec/writers/calendar-tbkdcal.md`; gates `build/w34/8b79c62-W34-3.2-*.log` |

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
