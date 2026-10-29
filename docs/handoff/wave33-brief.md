# Wave 33 brief - fix-only: the 29 October audit's Critical and High findings (for DeepSeek, coordinator role)

_Written 2026-10-29 by the Fable assessment session.  Baseline: branch `wave33`, cut
from `main` = `17f5782` (waves 23-32 merged).  Source: `docs/handoff/assessment-2026-10-29.md`
sections 7 and 8 (the audit and its work order); the lane evidence is local under
`build/audit-w32/` and is NOT in the repo, so every item below carries its own file:line.
`docs/handoff/standing-rules.md` binds this wave.  The repo lives at `D:\dev\msproject-mac`;
the neutral root for any Project save is `C:\w33\`.  This wave adds no writer, lifts no
refusal and measures no new Project pair: every change is a refusal, a disable, a safety
mechanism, a test, a CI job or a record._

## 0. Publication

Push `wave33` and dispatch both CI workflows in CLOSE; fix and re-dispatch on red (check
for unused functions and constants GCC and Clang reject before the first push).  Never
rewrite or force-push history; never work round the pre-push guard.  G3 adds the `app/`
suite to CI, so the CLOSE CI run must show it.

## 1. Rules for this wave

`standing-rules.md` plus the group rules of waves 26-32: groups in order, one fresh
session each, the marker line `WAVE-33-<group>-COMPLETE` appended to
`docs/handoff/wave33-complete.md` and committed when the group is done, one finding id
per commit (the audit ids below: A-n, B-n, C-n, D-n), gates logged per commit under
`build/w33/`, deferral only with evidence of an attempt.

**Safety first, then refusals, then gates, then records.**  Each refusal names its
reason in plain words in the status line.  Every test that needs a committed fixture
FAILS when the fixture is missing (no SKIP-and-pass; G3 makes this the rule).  Each
behaviour fix has its test seen RED on the baseline before the fix and GREEN after; quote
both runs in the commit message from the log.  Do not quote check counts a cited log does
not show.  Never name the operator or anyone else in docs, commits, fixtures or tests;
refer to rows by ID.  LF line endings everywhere (`.gitattributes` enforces it from G4).

**Fixes where the obvious change is wrong** (from the work order; do these, not the
tempting ones):

- A-2: do NOT "catch the exception and delete the partial file" - with `owns_path_` that
  deletes the user's only copy.  Write beside, verify, rename.
- A-4: do NOT "compute the flag from a better value scan" - the flag's meaning is
  unmeasured (`DOCHASALIAS` per Project's own property table, `docs/handoff/e7.md:122`).
  Disable the clear path until a pair measures it.
- A-6: do NOT disable Delete Task or link ADD as "unmeasured" - they ARE diffed against
  Project's corpus files (`tests/roundtrip_add_delete.cpp:1130-1235`,
  `tests/roundtrip_links.cpp:853-960`); the gap is that the evidence is corpus-only.  That
  is wave 34's Q5 (synthetic pairs), not a refusal here.
- B-4: do NOT commit `build/audit-w22/guard_names.py` - its term list is the client names.
  Commit a MARKER guard (operator name, Windows username, company folder, user-profile
  paths) and leave the client-term guard as the local hook.
- C-2: do NOT just turn `app-macos.yml` on for push - it builds on macOS only and names 15
  of the 25 tests.  Add `app/` to `ci.yml` on both OSs and fix the registration check.

## 2. Groups

### G0 - data safety (Critical)

- **W33-0.1  (A-2 / D-1 / D-4, Critical)  Atomic save.**  Every `.mpp` write
  (`core/mppwrite/cfb_writer.cpp:1257-1269` `CfbDocument::save`, `inplace.cpp:204`,
  `save.cpp:489` and `:598`) opens the final path with `ios::trunc`; `grep -rn rename
  core app` finds nothing.  After the first structural save both shells set `owns_path_`
  and reload the target as the open document (`app_qt/main_window.cpp:3234, 3246`;
  `app/main.mm:4087, 4217`), so every later Save overwrites the only copy in place, and an
  I/O failure between truncate and the last byte leaves a truncated file while the alert
  says "The opened file was not modified".  Fix: one shared helper (for example
  `mppwrite::write_file_atomically(path, bytes)`) that writes `<target>.tmp-<pid>` in the
  target's directory, flushes, rereads and self-checks (the existing `to_bytes()`
  equality / CFB self-check), then `std::filesystem::rename` over the target; on any
  failure the temp is removed and the target is untouched.  Route all four call sites and
  the `.w51-structural-tmp` finish through it.  Tests: an injected write failure (an
  unwritable temp directory, or a stream wrapper that throws after N bytes) leaves the
  target byte-identical (SHA-256 before = after); a forced reread mismatch leaves the
  target byte-identical; the no-edit save is still byte-identical to the source.
- **W33-0.2  (A-1, Critical)  A second Save after a formula-only or date-format-only save
  silently discards it.**  `Document::save` always derives the output from the ORIGINAL
  file (`app/document.cpp:567`); the non-structural route sets the remembered target but
  never reloads or re-points the document (`app_qt/main_window.cpp:3243-3262, 3311`;
  `app/main.mm:4085-4109`), so the next Save rebuilds the target from the original.  Fix:
  the non-structural route adopts the target exactly as the structural route does
  (`open_file:1198`): reload the written file as the document, set `owns_path_`, reset the
  pending formula/view-state ops.  Tests: formula save to T then Save again then reopen T,
  formula present; formula, Save As T, Insert Task, Save, reopen, formula present; same for
  the table date format.  Both shells (the macOS change is compiled by CI only: keep it
  mechanical).

### G1 - refusals and disables (High and Medium)

- **W33-1.1  (A-3, High)  Delete Task on a summary row.**  `push` is plan-free
  (`app/edit_model.cpp:2495-2512`), `delete_task_row` has no children check
  (`core/mppwrite/add_delete.cpp:1033-1120`), both shells check only the selection; the
  summary is tombstoned alone and the children keep has-parent and a parent identity
  pointing at a tombstone.  No pair measured a summary delete.  Refuse in `push()`, in
  `delete_task_row` (defence in depth) and in both shells: "delete the subtasks first, or
  outdent them".  Invert the `app_edits_outline_tests.cpp:2046-2055` assertion from
  "queued" to "refused"; add a Qt shell test.
- **W33-1.2  (A-9, High)  Insert Task clones the last live row's progress.**
  `add_task_row` copies the last live row including % complete, actual dates, duration and
  work (`core/mppwrite/add_delete.h:72-100`); the cascade passes only name/mode/uid=0.
  Refuse Insert Task up front when the row that would be cloned carries progress, actual
  dates, work, a task calendar, a constraint other than ASAP, or any assignment beyond
  Project's `0xFFFF0001` placeholder, with a reason; the measured flat insert on a clean
  last row still passes.  Test on a fixture whose last live row is 40 %: refused; on
  `synthetic-base`: accepted.
- **W33-1.3  (A-5, High)  Formula SET on unmeasured field kinds.**  Only Text and Number
  were measured.  Restrict SET to Text1..30 and Number1..20; refuse every other kind up
  front with the decision-9 text.  One test per refused kind (Date, Duration, Cost, Flag,
  Start/Finish, Outline Code).
- **W33-1.4  (A-4, High)  The `0x02401392` flag heuristic.**  `custom_field_value_remains`
  (`app/document.cpp:93-158`) decides from Task custom VarMeta keys, the Flag1 bit and
  assignment keys only, and the clear path writes 0; the flag's meaning is unmeasured.
  Disable formula CLEAR (both shells and `Document::clear_custom_formula`) with the
  decision-9 tooltip; SET keeps the flag at 1 unconditionally (the measured value).  Test:
  the clear path is unreachable from both shells; a SET on a value-free file still writes 1.
- **W33-1.5  (A-7, Medium)  Task Information and Task Fields ignore `push()`'s return.**
  `apply_task_information` (`app_qt/main_window.cpp:5237-5276`) pushes the Custom Fields
  tab's edits before the percent edit and ignores their result.  Decide the whole dialog
  on a trial copy (`app::StructuralEdits trial = structural_; trial.push(...)` for every
  edit) and apply only if every push succeeds; otherwise report the first refusal and queue
  nothing.  Test: queue an automatic indent, apply Text1 + 40 % on a row, assert
  `pending_edits().size()` unchanged and the status names the refusal.  Same for Task
  Fields.
- **W33-1.6  (A-11, Medium)  Table edits hard-code `table_index = 0`.**  Address the table
  the view shows.  Test on a two-table fixture (`wave31-tables.mpp` is committed): the
  second table's date format changes, the first's bytes do not.
- **W33-1.7  (A-8 / D-7, Medium)  FAT-slack files.**  A container with unreferenced
  trailing sectors (MS Project writes them; the reader's own cited real case) opens and is
  editable all session, then every Save fails because `to_bytes()` refuses slack.  At
  open, detect slack and mark the document read-only in both shells with a status reason
  ("saved by Project with unreferenced sectors; saving is not supported yet"); every edit
  entry point refuses with that reason.  Test on a synthetic slack fixture built by
  appending unreferenced sectors to `synthetic-base` (commit it as a fixture with its
  hash).
- **W33-1.8  (A-12, Low)  One pending formula op; a second overwrites silently.**  Refuse
  the second with a reason until the first is saved or undone.  Test.

### G2 - robustness (Medium and Low)

- **W33-2.1  (D-2, Medium)  Writer-side DIFAT walk unbounded.**  `cfb_writer.cpp:321`
  takes `num_difat` from the header; the loop at `:347-361` has no visited set and no
  file-size bound (the reader at `core/mpp/cfb.cpp:258-281` has both).  Mirror the reader's
  guards.  Test: a fixture with a self-pointing DIFAT sector and `num_difat` = 0xFFFFFFFF
  fails fast with an error (under 0.1 s), both through `mpp::load_bytes` and through
  `CfbDocument::from_bytes` directly.
- **W33-2.2  (D-5, Low, reproduced)  Resource Fixed2Meta count unbounded.**
  `core/mpp/reader.cpp:2112-2118` trusts the declared count; a 16-byte stream with
  0xFFFFFFFF makes a 4.3-billion-iteration `continue` loop (reader_dump 0.02 s -> 2.2 s).
  Bound the count by `available / 51` (and audit the sibling Fixed2Meta/FixedMeta count
  sites for the same pattern).  Test with the patched fixture: under 0.1 s, same rows.
- **W33-2.3  (D-6, Low)  CFB v4.**  `sector_offset` is `512 + sid*4096` where the spec
  says `(sid+1)*4096`, and the whole-sectors check rejects every real v4 file.  Either fix
  both and add a v4 round-trip test, or refuse v4 explicitly at open with a clear message.
  Default: refuse (no v4 fixture exists to measure against).
- **W33-2.4  (D-3, Medium)  Fuzz coverage.**  `tests/fuzz` mutates only into
  `mpp::load_bytes`; `CfbDocument::from_bytes`, the MSPDI XML and CSV importers are never
  fed mutated bytes.  Extend the mutators to all three; add an `ubuntu-asan` job to
  `ci.yml` that builds the core tree with `-fsanitize=address,undefined` and runs the
  core suite plus the fuzz targets.  CI must be green on it before CLOSE.

### G3 - gates and CI (High and Medium)

- **W33-3.1  (C-2, High)  The `app/` suite never runs in CI.**  `qt.yml` builds only
  `app_qt` (`APP_BUILD_TESTS OFF`), the root tree `ci.yml` configures has no `app/`
  (`CMakeLists.txt` has no `add_subdirectory(app)`), and `app-macos.yml` is dispatch-or-
  tag only (last run `wave27`, 24 Oct), naming 15 of 25 tests.  Add an `app` build with
  `APP_BUILD_TESTS=ON` and `CORE_WARNINGS_AS_ERRORS=ON` to both `ci.yml` jobs (windows-
  msvc, ubuntu-gcc) and run its ctest with `MPP_NO_CORPUS=1`; make `app-macos.yml` run on
  push as well and derive its registration check from the CMake list, not a hand-typed
  one.  Acceptance: the CLOSE CI logs show the app ctest total on both OSs.
- **W33-3.2  (C-5, Medium)  The null-save mask's blind spots.**  `tests/save_pair_noise.h`
  masks the whole 16-byte block around any per-save noise byte in fixed streams and
  everything beyond the null save's length, and `effects` is printed, not asserted (except
  task mode).  Narrow the fixed-stream mask to the noisy bytes themselves (the control vs
  null diff positions), compare the appended tail beyond the null save's length against
  Project's file explicitly, and make `save_pair::compare` assert a NON-EMPTY effect set
  for every stream the pair is declared to change.  Re-run every pair test; a pair that now
  fails is a real gap: record it under Found, not fixed with the stream and offsets (do not
  widen the mask back).
- **W33-3.3  (C-6, Medium)  SKIP-and-pass.**  153 SKIP sites exit 0 on a missing fixture;
  at least 25 guard committed `tests/data` save-pair controls.  For every committed
  fixture, a missing file is a FAILURE with the path; keep SKIP (printed as "corpus
  absent") only for `corpus/`-gated tests.  Acceptance: rename one committed control
  locally, run the suite, see the failure, restore.
- **W33-3.4  (C-8, C-9, C-11, C-12, Low)  Honest numbers.**  Records and START-HERE say
  "app_qt 37/37" where CI runs 35 (two corpus-gated): quote both.  The backlog's "69
  disabled" counts two enabled split buttons (pin truth 47 enabled / 67 disabled): say so
  in the generated summary.  Set `MPP_NO_CORPUS=1` for the first CI ctest pass.  Rename
  `qt_render_parity` to what it tests (`qt_render_1920`), or make it run
  `parity_report.py --reference-json` as a real comparison.

### G4 - records and privacy (High and Medium)

- **W33-4.1  (C-1, C-7, High)  The entry-point records are false about merge state.**
  `docs/START-HERE.md:85-93`, `memory/coordinator.md:9-14, 119` and
  `docs/handoff/INDEX.md:16-37` say `main` = `fcb1c10`, waves 23-32 "not merged",
  `origin/wave32` exists.  Rewrite them to the truth: merged on 27 Oct 2026 as `17f5782`
  after a history rewrite; GitHub holds only `main`; every commit id quoted in the wave
  23-32 records is a PRE-rewrite id, mapped by `wave32-verification/rewrite-sha-map.txt`
  (say how to look one up); add the verifier's R32-1 and R32-2 to "Still open"; add this
  wave and `assessment-2026-10-29.md` to INDEX "Now".  Acceptance: a reader of START-HERE
  alone learns the true branch state.
- **W33-4.2  (C-3, Medium)  CRLF reintroduced by the merge commits.**  Restore LF on
  `docs/handoff/plan-waves27-30.md`, `scripts/check_fixtures.py` and
  `docs/handoff/wave32-verification/gates.md`; add `* text=auto eol=lf` for `*.md`, `*.py`,
  `*.yml`, `*.cpp`, `*.h` to `.gitattributes` if absent, and a CI step that fails on a CR
  in those types.  Correct the gates.md sentence that says the tip differs from the
  verified tip "only in wave23-outline.mpp".
- **W33-4.3  (B-2, High)  Two tracked `.mpp` carry the operator's name as Author and
  LastAuthor** (ASCII and UTF-16LE): `tests/data/synthetic-base.mpp` and
  `analysis/test_project.mpp`.  Neutralise in place with a same-length replacement (the
  method used for `wave23-outline` on 27 Oct: size and every offset unchanged), update the
  pinned SHA-256 in `scripts/check_fixtures.py` and `tests/data/README.md`, and re-run
  every test that hashes `synthetic-base` (several do: the no-edit save must still be
  byte-identical to the NEW bytes).  Fixture commit first, code/doc commit second.
- **W33-4.4  (B-4 marker half, B-12, High)  A tracked marker guard in CI.**  Write
  `scripts/check_markers.py`: scans EVERY tracked file (text and binary, including
  `analysis/*.mpp`, `.bin`, `.tsv`) for the operator's name variants, the Windows
  username, the company Dropbox folder name and user-profile path fragments
  (`C:\Users\`, `Documents and Settings`), in ASCII, UTF-16LE and UTF-16BE at both
  alignments, plus commit messages in the pushed range; the marker list lives in the
  script (these are not client terms).  Run it in `ci.yml` on both OSs.  It must be RED on
  HEAD before W33-4.3 and W33-4.5, GREEN after.  Also extend `check_fixtures.py`'s
  property-set scan to every tracked `.mpp`, not only `tests/data`.
- **W33-4.5  (B-3, Medium)  Operator name and paths in tracked text.**  69 tracked text
  files carry the operator's name, the company Dropbox folder or the Windows username;
  49 `scripts/phase*.py` hard-code absolute paths under the operator's profile.  Replace
  the name with "the operator", the paths with a `REPO_ROOT`/`CORPUS_DIR` environment
  lookup or a relative path; historical handoffs may be edited in place (they are records
  of a stream, not fixtures).  W33-4.4's guard is the acceptance test.
- **W33-4.6  (B-13, Low)  Oracle scripts.**  14 older `scripts/oracle_*.py` save without
  neutralising Author/LastAuthor/Company/Title/UserName; the `neutralise()` body is
  copy-pasted about 25 times.  Move it to one shared module and call it from every oracle
  script before any save.

### CLOSE

Fresh-tree gates on the final commit, logged (core, app with `CORE_WARNINGS_AS_ERRORS=ON`,
app_qt, M2, engine suite, check_fixtures, check_markers, the sanitizer build if local
tooling allows); both name scans and the new marker check clean; a review pass over
`git diff main..wave33` (two agents: one on the safety and refusal code, one on the gates
and records); push and dispatch as section 0 says, and confirm the CI logs show the `app/`
ctest total, the asan job and the marker check; `wave33-complete.md`, START-HERE, INDEX,
`memory/coordinator.md` and the regenerated `docs/ui-command-backlog.md`.  Then append
`WAVE-33-COMPLETE`.

## 3. Queued - Tim's rulings (not for this wave)

| id | item | default |
|---|---|---|
| Q1 | B-1 (Critical), B-7, B-15: send the GitHub support purge request with the 329 ids from `rewrite-sha-map.txt`; delete the 62 pre-scrub CI artifacts; consider private until confirmed and removing the sha map from the public tree afterwards | Tim; if not sent by Sat 31 Oct 2026 the coordinator asks once more on Mon 2 Nov 2026 |
| Q2 | B-8: the DeepSeek sandbox (dedicated account/container without `corpus/` or Dropbox reads; key via `apiKeyHelper`; rotate the key) | Tim decided 29 Oct 2026 to run waves 33-34 on the current box; the coordinator removes WebFetch/WebSearch from the driver's tool list before launch; rotation stays with Tim |
| Q3 | B-5: the SHA-256 name column in `project_a_golden.tsv` | if silent by Fri 6 Nov 2026: drop the column (uid is the key), coarsen dates to the week - a wave-34 item |
| Q4 | B-6 (commit `6056f15` quotes corpus label N17), B-11 (two local pre-push backup branches) | Tim rules; delete the branches on his word |
| Q5 | A-6: synthetic Project pairs for Insert Task, Delete Task, link ADD, custom-field set/clear | wave 34 G0 |
| Q6 | the stale Dropbox clone `Projects/MS Project Decompile` (22 GB `build/`) | Tim; leave a MOVED.md |

## 4. Verification gate (the verifier session, after CLOSE)

Fresh-tree gates with warnings-as-errors on all three trees; CI green on all three OSs
INCLUDING the `app/` suite, the asan job and the marker check; a verifier pass on
`git diff 17f5782..wave33`, then a second, narrower pass over the verifier-fix commits;
the three G0/G1 tests (W33-0.1, W33-0.2, W33-1.1) seen RED on `17f5782` and GREEN on the
tip; lane B's property-set scan (`build/audit-w32/laneB_mpp_props.py`) reports 0 flagged
`.mpp`.  Merge rule unchanged: CI green and no Critical/High in the review.
