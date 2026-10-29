# Wave 34 brief - parity: decode Project's save shapes, build the shared writers, enable the measured-and-deferred commands (for DeepSeek, coordinator role; Opus coordinator for the decoding lane)

_Written 2026-10-29 by the Fable assessment session.  Baseline: branch `wave34`, cut from
`wave33`'s tip once wave 33 G0 has committed (the atomic-save helper is a dependency of
every writer here).  Tim's intent for this wave: assess MS Project's live behaviour and
code to work out what the port must change to get closer to 100 % parity, and then make
those changes.  `docs/handoff/standing-rules.md` binds this wave.  The repo lives at
`D:\dev\msproject-mac`; the neutral root for any Project save is `C:\w34\`.  Estimate: 12
to 20 hours of DeepSeek plus the decoding lane; it will probably split into waves 34
and 35._

## 0. Why this wave, and how it relates to wave 33

The 29 October assessment (`assessment-2026-10-29.md` section 5) found that every
remaining parity gap in the Task, Resource and Project tabs is now blocked on the same
few missing WRITERS, not on unknown behaviour: wave 30 measured 15 commands against
Project, committed the pairs (`tests/data/wave30-*.mpp`, 97 controls and 46 null saves
in the tree) and deferred them because the port cannot produce the save shape.  The
deferral notes in `wave30-complete.md` name the gaps exactly:

| shared writer | commands waiting on it (wave-30 item) |
|---|---|
| VarMeta entry insert / remove / resize (a Var2Data record added, dropped or re-pointed, with the VarMeta offset table rewritten) | Resource Information `W30-2.1`, Notes `W30-1.5` and `W30-2.1`, Move `W30-1.4`, Add Resources `W30-2.0`, Calculate Project `W30-3.0`, Set Baseline `W30-3.1`, WBS `W30-3.2`, Insert Task's fractional ordinal `W30-1.1` |
| the `214` view-stream writer (mid-stream insertion plus the VarMeta offset rewrite plus a Props size byte) | Font B/I/U, colours, Format Painter `W30-1.9`; Move's StartText/FinishText removal; Notes' entry |
| Props record insertion (`114/Props` and `214/Props`) | WBS code definition `W30-3.2`, Set Baseline `W30-3.1`, Add Resources' three count words `W30-2.0` |
| table growth with re-keying (append a row AND rewrite existing rows' pointer words and the Work summary) | Assign Resources add and remove `W30-2.3`, Insert Summary / Milestone's reparenting `W30-1.1` |
| a `TBkndCal` resource-calendar row writer | Add Resources `W30-2.0` |
| a `DocumentSummaryInformation` writer | Calculate Project `W30-3.0`, Move `W30-1.4` |
| Project's RichEdit note serialisation (RTF header, font table, escaping, generator) | Task Notes `W30-1.5`, Resource Notes `W30-2.1` |
| a scheduler SAVE path (recalculate, then write the task, assignment and project rows Project rewrites, nine new VarMeta entries and the summary strings) | Calculate Project `W30-3.0`, Respect Links `W30-1.3`, Move `W30-1.4`, Auto Schedule `W30-1.0`, Level All `W30-2.4`, Inactivate's boundary case `W30-1.2`, and later Start/Finish/Duration editing (the wave-31 Fill pair) |

Wave 33 is fix-only and touches `app/`, `app_qt/`, the CFB writer's I/O, CI, scripts and
records.  Wave 34's G1-G3 touch NEW files under `core/mppwrite/` and `tests/`; G4-G6 touch
the shells.  **Sequencing (Tim's call, decision 1 below): default is sequential (34 after
33 closes).  If Tim says "alongside", G1-G3 may run in a separate worktree
(`D:\dev\wt\wave34`, branch `wave34` cut from `wave33` after its G0 commit) under a strict
ownership rule: no edits to `app/`, `app_qt/`, `.github/`, `scripts/check_*.py`,
`docs/START-HERE.md`, `docs/handoff/INDEX.md`, `memory/coordinator.md` or any `wave33*`
file; G4-G6 start only after wave 33 is merged and `wave34` is rebased onto it.**

## 1. Rules for this wave

`standing-rules.md` and the wave 26-33 group rules (groups in order, one fresh session
each, markers `WAVE-34-<group>-COMPLETE` in `docs/handoff/wave34-complete.md`, one item id
per commit, gates per commit under `build/w34/`, deferral only with evidence).  In
addition:

1. **Rule 1 is the whole wave.**  A writer is "done" only when the port's save of the
   measured edit matches PROJECT's file field by field through `tests/save_pair_noise.h`
   with the wave-33 `effects` assertion on, for every stream the pair changes, and the
   null save separates per-save noise from the edit.  A command is enabled only for the
   measured shape and refuses every other shape UP FRONT in the edit model, with a test
   per refusal.  "Port reopens its own save" proves nothing.
2. **One factor per pair.**  When a rule is inferred from one pair, measure a second pair
   that varies one factor (a different duration, a linked row, a second progressed leaf)
   before generalising; two readings that both fit one data point were the recurring
   failure of waves 28-30 (the contour rule, the 0x07 link byte, the summary roll-up).
3. **Decode before you write.**  Each writer starts from a written decode note in
   `spec/` (see G0's method) that cites the pair offsets, the decompiled routine (where
   found) and the COM facts, and states the rule as a formula with its known limits.  A
   writer with no decode note is a guess.
4. **COM is run only through `scripts/oracle_lock.py`** on copies under `C:\w34\`, with
   `neutralise()` (wave 33's shared module) before every save; never on the corpus files;
   Project closed and the lock released after every session.  Fixtures commit in their
   own commit with their SHA-256 in `tests/data/README.md` and `check_fixtures.py`.
5. **Exoproc is not used in this wave** (see G0.4): the decoding lane uses the committed
   pairs, the Ghidra model already on disk, and COM; a dynamic trace, if ever needed, is
   the coordinator's and runs on an isolated VM with free Microsoft tooling.

## 2. Groups

### G0 - the decoding lane: assess what Project actually does (coordinator-led; DeepSeek writes the notes and scripts)

This group is the "assess the live process and the code" Tim asked for, done with what
the project already has.  Deliverable: one decode note per shared writer in
`spec/writers/<writer>.md` (new folder), each with a "rule", "evidence", "limits" and
"pairs still needed" section, plus any new pairs it required.

- **W34-0.1  Pair anatomy.**  For each wave-30 pair (and the wave-31 Fill pair), produce
  a stream-by-stream, offset-by-offset diff of control vs edited vs null (the tooling
  exists: `scripts/oracle_*.py`'s diff helpers, `tests/save_pair_noise.h`'s classifier;
  if no standalone tool prints it, write `scripts/pair_anatomy.py` and commit it) and
  commit the human-readable report under `spec/writers/evidence/<pair>.md` (offsets,
  old/new words, the VarMeta keys touched - never task names).
- **W34-0.2  Static cross-reference.**  The Ghidra model is on disk and git-ignored:
  `decompiled/part_0000..0027.c` (2.59 M lines, pre-naming `FUN_1400xxxxx`),
  `analysis/meta/functions.tsv` (55,978 functions), `calls.tsv`, `strings.tsv`,
  `analysis/field_metadata_*.tsv` (3,207 field ids with storage class and slot), and
  `ghidra-project/` (regenerable).  For each writer, find the serialisation routine(s):
  start from the VarMeta key / field id constants the pair diff shows (grep the hex
  literal and its byte-swapped form in `decompiled/`), the `backend.serialization` module
  functions in `functions.tsv`, and the string xrefs; read the routine to derive the
  rule (what else it writes, under which conditions) rather than inferring it from one
  pair.  Record the function addresses in the decode note.  Where the decompile is too
  opaque within two hours per writer, say so and fall back to W34-0.3.
- **W34-0.3  Targeted COM measurements.**  For every rule the pair and the decompile do
  not settle, one more pair varying one factor (rule 2 above), measured through the
  oracle lock on `C:\w34\`, committed as a fixture with its null save.  Expected: 10 to 20
  new pairs across the wave.
- **W34-0.4  Dynamic tracing - decide, do not do, in this wave.**  The coordinator records
  in the decode notes which rules (if any) remain open after W34-0.1-0.3 and would need a
  live trace of `WINPROJ.EXE` during a save (break on `WriteFile`/`NtWriteFile` and the
  serialisation routines found in W34-0.2, record which routine writes which stream
  bytes).  That trace, if Tim approves it (decision 3), is the coordinator's job on an
  isolated VM with WinDbg Preview Time Travel Debugging or API Monitor, both free and
  from Microsoft or long-established vendors, not DeepSeek's and not on the company
  workstation.  `w0ldan/exoproc` is NOT used: it is a day-old public release that injects
  code into processes and installs by piping a remote script into PowerShell, and the
  same questions are answerable with the tooling above.
- **W34-0.5  Synthetic pairs for the enabled-but-corpus-only paths (Q5 from the audit).**
  Insert Task, Delete Task, link ADD and custom-field SET/CLEAR are today diffed against
  Project only through `corpus/` files CI never has.  Measure each on `synthetic-base`
  through COM, commit the pairs and null saves, and route the existing tests through
  `save_pair::compare`.  A path whose synthetic pair exposes a mismatch is refused (rule 1)
  and recorded, not patched blind.

### G1 - the VarMeta entry writer (insert, remove, resize)

- **W34-1.1**  `core/mppwrite/varmeta_edit.{h,cpp}`: add, drop and re-point one Var2Data
  record for one row in any of the Task, Resource, Assignment and project tables, keeping
  Project's VarMeta ordering, the logical-removal-with-stale-tail convention the outline
  writer already measured (`outline.cpp` indent tombstone), and the Props/CEdl count words
  the pair shows.  Decode note first (G0).  Measured against `wave30-resource-rename-*`
  (a text field update), `wave30-notes-*` (a new entry), and one new pair for a removal.
- **W34-1.2**  Tests: `tests/roundtrip_varmeta.cpp` comparing against Project's file for
  all three shapes, with the null save; a refusal test for every shape not measured
  (multi-row, a key already present when inserting, the project row, a table whose
  VarMeta has stale tails beyond the pair's shape).
- **W34-1.3**  First consumer, core only: `rename_resource(uid, name)` through the new
  writer; field-by-field against `wave30-resource-rename`.

### G2 - the `214` view-stream writer and Props record insertion

- **W34-2.1**  `core/mppwrite/viewstream_edit.{h,cpp}`: mid-stream insertion in a `214/`
  stream with the VarMeta offset rewrite and the Props size byte, measured against
  `wave30-textstyle-*` (one Bold cell).  Decode the font word's bits from the decompile
  (W34-0.2) before writing; if only the Bold bit is settled, the writer accepts Bold only.
- **W34-2.2**  `core/mppwrite/props_edit.{h,cpp}`: insert and update a `114/Props` or
  `214/Props` record (id, kind, length, payload) keeping the table's order and the
  `Props14` mirror the pairs show; measured against `wave30-wbs-code-*` and the three
  count words of `wave30-resource-add-*`.
- **W34-2.3**  Tests as G1; refusals for every unmeasured sibling (I/U/strikethrough,
  colours, row vs cell selection, other views, the project summary row).

### G3 - table growth with re-keying, the resource calendar row, DocumentSummaryInformation

- **W34-3.1**  Assignment table growth: append a row AND rewrite the existing rows'
  pointer words and the task's Work summary as `wave30-assign-add-*` shows; removal as
  `wave30-assign-delete-*` shows (Project's placeholder tombstone / appended row and the
  re-keyed VarMeta entries).  One new pair each varying the task's existing assignment
  count (0, 1, 2).
- **W34-3.2**  Resource row: `add_resource_row` plus the `TBkndCal` resource-calendar row
  and the three Props count words (`wave30-resource-add-*`); one new pair with a second
  resource added after the first.
- **W34-3.3**  `DocumentSummaryInformation` writer: the summary strings and dates Project
  rewrites on Calculate Project and Move (`wave30-calculate-*`, `wave30-move-*`); property-
  set encoding verified against an independent reader (`olefile`, to be installed in the
  CI job only, or a committed minimal reader in `scripts/`).
- **W34-3.4**  Reader gaps the enablements need: `Task.Active`, the inserted summary and
  milestone result (fractional Fixed2Data ordinal, reparenting), baseline fields on task,
  assignment and resource rows, LevelingDelay / Preleveled / Overallocated.  Each with
  reader tests against COM facts (the reader's existing pattern).

### G4 - shell enablement, Resource and Task tabs (after wave 33 is merged)

Each command below is enabled ONLY for its measured shape, refuses every other shape up
front with the reason, has a pair test field-by-field against Project's file, and gets its
backlog row regenerated.  Order by use:

- **W34-4.1**  Resource Information rename (G1); Resource Notes and Task Notes ONLY if the
  RichEdit serialisation is decoded in G0 for plain single-line ASCII (else they stay
  disabled with the exact tooltip and the decode note records why).
- **W34-4.2**  Add Resources (G3.2), work resource with default rates only.
- **W34-4.3**  Assign Resources: one work resource assigned at 100 % to an unstarted leaf;
  remove the same (G3.1).
- **W34-4.4**  Insert Summary and Insert Milestone (G1 + G3.4) on a clean last row, as
  W33-1.2's conditions define it.
- **W34-4.5**  Font Bold on one cell (G2.1), the other font controls stay disabled.
- **W34-4.6**  WBS code definition (G2.2) with the measured prefix shape only.
- **W34-4.7**  Set Baseline 0 for the entire project (G1 + G2.2 + G3.4), no other
  baseline.

### G5 - the scheduler save path, stage 1

- **W34-5.1**  Decode note for Calculate Project from `wave30-calculate-*`, the
  recalculation engine's facts (`plan_recalc` already reproduces Project's numbers 332/332
  on the real file) and the decompiled `TBkndRecalc` entry points: which task, assignment
  and project words Project rewrites after F9, the nine new VarMeta entries, the summary
  strings.
- **W34-5.2**  `mppwrite::save_plan` moves from test-only to a measured writer for ONE
  shape: a pending Duration change on an unlinked automatic leaf, F9, save.  Two more
  pairs (a linked pair with a lag; a constraint) before it is enabled for linked files.
- **W34-5.3**  Calculate Project enabled for the measured shapes; Respect Links, Move,
  Auto Schedule and Level All stay disabled until their own pairs match (next wave).  If
  W34-5.2 cannot match Project field by field after the decode and three pairs, defer with
  the decode note as evidence and list the differing fields.

### G6 - view-state persistence (if time remains; otherwise next wave)

Sort order, applied table, outline level, zoom: each is session-only today with the
file's bytes kept.  One pair each (`214/CV_iew`, `CTable` applied index) through COM;
write only what the pair shows; the Zoom Custom span must be measured before any write.

### CLOSE

Fresh-tree gates (all trees, warnings as errors, the wave-33 CI set including the `app/`
suite, asan and the marker check); both name scans; a review pass over
`git diff wave33..wave34` with one agent on the writers (rule 1: every enabled shape has a
pair and a Project comparison) and one on the refusals; push and dispatch; the decode notes
indexed from `spec/README.md`; `wave34-complete.md`, START-HERE, INDEX,
`memory/coordinator.md`, the regenerated backlog; the 16 decisions re-scored.  Then
append `WAVE-34-COMPLETE`.

## 3. Decisions for Tim (defaults apply if silent)

| # | decision | recommendation | default |
|---|---|---|---|
| 1 | Run wave 34 G1-G3 alongside wave 33 in a separate worktree, or after it | after: the shared writers depend on wave 33's atomic-save helper and the `effects` assertion, and two writers on one tree cost more than they save | sequential; the coordinator starts G0 (notes and pairs only) alongside wave 33 since it edits only `spec/` and `tests/data` |
| 2 | Order of enablements in G4 | as listed (Resource Information, Add Resources, Assign Resources, Insert Summary/Milestone, Bold, WBS, Set Baseline) | as listed |
| 3 | A dynamic trace of `WINPROJ.EXE` (W34-0.4) | only if G0 leaves a rule open; on an isolated VM; coordinator only; never exoproc on the work box | no trace; open rules are recorded and the writer refuses those shapes |
| 4 | B-5 from the audit: the SHA-256 name column in `tests/data/project_a_golden.tsv` | drop the column, coarsen dates | if silent by Fri 6 Nov 2026, dropped in this wave's G0 |
| 5 | Stop point | stop after G4 and report if the wave passes 12 hours; G5-G6 become wave 35 | stop after G4 |

## 4. Verification gate

As wave 33's, plus: every enabled command in G4/G5 has (a) a decode note, (b) a committed
pair and null save, (c) a field-by-field test against Project's file with effects
asserted, (d) one refusal test per unmeasured shape, and (e) Project 2021 reopens the
port's save of that edit with 0 differing fields over the project row and the edited rows
(the `oracle_w29_reopen.py` method).  A verifier pass on `git diff wave33..wave34`, then a
second over the verifier fixes.  Merge rule unchanged.
