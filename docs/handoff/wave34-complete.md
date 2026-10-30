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
