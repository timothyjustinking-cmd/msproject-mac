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
