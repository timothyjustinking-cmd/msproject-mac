# Wave 33 independent review (read-only) - `git diff main..wave33`

_Coordinator note (30 Oct 2026): R33-1 confirmed against the source (`app_qt/dialogs.cpp:316` carries the shown table index into the date-format write; only table 0 is measured).  Merge rule not met: one High.  `wave33` is not merged; the fix-only `docs/handoff/wave33b-brief.md` (on branch `wave33b`) takes R33-1 as G0 and R33-2/R33-3 as G1._

Scope: `main` = `17f5782` to `wave33` = `617fc8d` (45 commits, 267 files).  Reviewer
was read-only: nothing built, no test run, no CI dispatched.  Commands run: `git
diff/show/grep/log`, `python -I scripts/check_markers.py --range main..wave33`
(1302 files, 9 markers, 0 hits, 0 message hits), `--self-test` (19/0),
`python -I build/audit-w22/guard_names.py main..wave33` (exit 0), and a replay
check over `docs/handoff/wave33-verification/rewrite-sha-map.txt`.

## Verdict

Wave 33 mostly does what its record claims, and most of it checks out against the code.
The atomic save helper is real and every `.mpp` writer goes through it. Both shells now
adopt the file after a non-structural save. The summary-delete, insert-clone,
formula-kind, formula-clear and FAT-slack refusals are present in the model and in both
shells. The W33-3.2 mask narrowing is honest: the narrowed mask exposed 23 mismatches
(11 actuals, 6 outline, 6 core outline; `build/w33/W33-3.2-*-after.log`), and they
became exactly 23 named expected failures. Each is pinned to its stream, offset and both
byte values. No mask was widened anywhere else, and a fixed byte turns the entry red. The
SKIP-to-FAIL sweep is genuine: committed fixtures go through `fixture::require_committed`,
and the remaining SKIPs are corpus-gated or already queued. The replay changed exactly
one tree (the `3b53b9d` blob) and one message (`964594d`), and the tip tree is identical
to the pre-replay tip. The privacy scans are clean.

One High stands. W33-1.6 enables a save path that has no Project measurement: a
date-format write to a non-first `214/CTable` table. The CLOSE record's "it added no
writer, lifted no refusal" is false for that path. Two Mediums are partial fixes:
- the formula-clear writer and its flag heuristic are still live in the public save
  hook, and a test still asserts that they work;
- the macOS Task Fields handler still leaves a partial queue behind after a refused push.

Every CI acceptance (the `app/` ctest totals, `ubuntu-asan`, the marker step on both
OSs) is unverified, because no CI run of `wave33` exists. By the brief's own merge rule
(CI green, no Critical/High), the wave is not mergeable until R33-1 is resolved and CI
is green.

Counts: Critical 0, High 1, Medium 2, Low 6.

## Findings

| id | severity | file:line | claim | evidence | suggested fix |
|---|---|---|---|---|---|
| R33-1 | High | `app_qt/main_window.cpp:4402`, `app_qt/dialogs.cpp:316`; record `docs/handoff/wave33-complete.md:1331` | W33-1.6 makes Table / Date Format write the date-format byte of the table the view shows. For any table other than index 0, this save path has never been measured against Project, which breaks standing rule 1. | The only Project diff for the date-format writer is one table at index 0 with value 0x05 (`tests/viewstate_tests.cpp:2261-2320`, built on `synthetic-viewstate-ctable-*.bin`). `docs/handoff/w64.md:318` explicitly lists "`table_index` other than 0" as unmeasured. `wave31-tables.mpp` (`scripts/oracle_w31_tables.py`) measured creating and applying a second table, not a date-format change on it. The new gate `app_qt/tests/table_date_format_test.cpp` only re-reads the port's own save: rule 1 calls that "a round-trip test, not evidence; it never enables a save path on its own". The brief directed the change, but rule 1 says a brief "may add rules but not relax one". The CLOSE record still says the wave "added no writer, lifted no refusal". | Until a COM pair measures Project's `TableEdit` DateFormat on a second table, refuse the Table / Date Format edit when the shown table index is not 0, using the decision-9 tooltip. Keep the index plumbing so the refusal can later be lifted to a measured write. Correct the CLOSE sentence. Alternatively, measure the pair now and add it as a field-by-field oracle test. |
| R33-2 | Medium | `app/document.cpp:249-296` (clear branch, flag heuristic at `:283`), `app/document_tests.cpp:1306-1324` | W33-1.4 says formula CLEAR is disabled and the `0x02401392` heuristic is gone. In fact only `Document::clear_custom_formula` and the two shell buttons were disabled. | `apply_formula_edit` still runs `clear_custom_field_formula` and the `custom_field_value_remains` heuristic (`custom_field_value_remains` is at `document.cpp:128`). That clear branch can be reached through the public `app::apply_document_edits` / `save_structural_edits` hook. `test_cascade_with_document_edits_in_one_write` still builds `Operation::clear` and asserts "the formula clear was written". W33-1.3 added a save-hook backstop for SET; CLEAR got none. No product caller reaches the clear branch today, but the unmeasured writer is still live, and a test protects it as working behaviour. | In `apply_formula_edit`, refuse `Operation::clear` with the same decision-9 reason, the way W33-1.3 refuses an unmeasured SET. Change the cascade test to assert the refusal (and that the target is untouched). |
| R33-3 | Medium | `app/main.mm:3414-3423` | W33-1.5 ("Same for Task Fields") is marked DONE. | The AppKit Task Fields handler pushes each edit onto the live `plan.edits`. On a partial refusal it shows "could not all be queued" and returns, without rolling back the edits it already pushed and without `apply_structural_change`. The next Save then writes edits the user was told failed, and the display never showed them. This is A-7's defect class in the second shell. The CLOSE review queued it as Low (RW33-3); because it leads to a silent write, Medium fits better. | Use the Qt pattern: push onto `app::StructuralEdits trial = plan.edits`, and replace `plan.edits` only if every push succeeds. Keep the change mechanical, since only CI compiles it. |
| R33-4 | Low | `scripts/phase2d_create_project.py:13,86` | W33-4.5 repointed the script's paths. | The script now runs `FileSaveAs` through COM directly onto the tracked, just-neutralised `analysis/test_project.mpp`. It never calls `oracle_privacy.neutralise()` (W33-4.6 covered only `oracle_*.py`), so re-running it reintroduces B-2 (Author/LastAuthor). The CI marker guard would catch the blob, but only after the local damage is done. | Save under `build/` or a neutral root, call `neutralise()` before saving, and refuse to overwrite a tracked file. |
| R33-5 | Low | `tests/save_pair_noise.h:266-289` | The gap matcher says it is "exact". | `open_gap_state` returns `fixed` for any `214/Props` or `214/CEdl/VarMeta` stream with exactly one effect and zero mismatches, whatever the offset. A future pair that correctly reproduces a different single effect there would be reported as "expected failure now PASSES" (a false red). In the other direction, `open` is not tied to the pairs measured (this overlaps RW33-7). | Also require `first`-effect offset == the signature offset for `fixed`. Key the expected failures by pair control name. |
| R33-6 | Low | `tests/save_pair_noise.h:239-256`; `docs/handoff/wave33-complete.md` (W33-3.2 detail, Found) | The record states the mechanism: "Project's editor save resets ONE flag byte". | The pre-W33 header attributed the same three offsets to "a coincidence byte" inside a per-save noise record, "measured on all 23 pairs". No new measurement settles which explanation is right; the record presents an interpretation as fact. The expected-failure handling is correct either way. | Describe the bytes as "unexplained: either a per-save coincidence byte or an editor flag", and leave the mechanism to the wave-34 pair work. |
| R33-7 | Low | `app_qt/dialogs.cpp:438` | Clear Formulas is disabled (W33-1.4). | The empty-formula warning still says "Use Clear Formulas to remove every stored formula", pointing at a button that no longer works. | Reword it to match the disabled state. |
| R33-8 | Low | `core/mppwrite/atomic_write.cpp:69-74` | The header says the helper will "remove only a file this call created". | If a regular file already sits at `<target>.tmp-<pid>` (for example a stale temp from an earlier crash with the same pid, or a user file), it is opened with `trunc`, `created` becomes true, and on any failure it is deleted. So a file this call did not create is destroyed. The W33 test only covers a directory at that name. This overlaps RW33-1. | Create the temp file exclusively (`O_EXCL` / `CREATE_NEW`, or check `exists()` first and pick another suffix), and set `created` only on exclusive creation. |
| R33-9 | Low | `docs/handoff/wave33-complete.md:415,616,804` | W33-2.4, W33-3.1 and W33-4.4 are **DONE**. | Each acceptance depends on CI ("green before CLOSE", "the CLOSE CI logs show the app ctest total on both OSs", "Run it in `ci.yml` on both OSs"). No `wave33` CI run exists: `origin/wave33` is absent and `gh run list --branch wave33` is empty. The record does disclose this under Found, not fixed. | Mark these "DONE pending CI", and put the run ids in the record once the coordinator has dispatched. |

## Per-item verification

| item | status | one-line evidence |
|---|---|---|
| W33-0.1 | verified | `CfbDocument::save` (`cfb_writer.cpp:1294-1316`) and `InPlacePatcher::save` (`inplace.cpp:205-210`) both go through `write_file_atomically`. The remaining `ios::trunc` writers in `core/` are the MSPDI/PDF exports, not `.mpp`. Temp file plus verify plus rename is present. The tests cover a blocked temp, a failing self-check and a fresh write (`tests/atomic_write.cpp`). See R33-8. |
| W33-0.2 | verified | Qt `save_document_path` reloads via `open_file(outcome.path)` and sets `owns_path_` (`main_window.cpp:3316-3337`). macOS calls `load_plan(plan, outcome.path)` and sets `owns_path` (`main.mm:4243-4265`). `qt_save_reopen` added. |
| W33-1.1 | verified | `push()` requires the plan and refuses a summary (`edit_model.cpp:2582-2593`); defence in depth in `delete_task_row` (`add_delete.cpp:1261-1313`); `make_delete_edit` used in both shells; the outline-test assertion inverted; `qt_delete_summary` added. |
| W33-1.2 | verified | Plan-side `insert_clone_plan_refusal`, file-side `insert_task_clone_is_clean`, and a re-check in the save cascade (`edit_model.cpp:3984-3998`); both shells call `check_insert_task` before asking for a name. |
| W33-1.3 | verified | `custom_formula_field_is_settable` allows only Task Text1..30 / Number1..20 ids (`field_value.h` families), at both the queue and the save hook (`document.cpp:250-256, 553-558`). |
| W33-1.4 | partly | Both shells' buttons are disabled with the decision-9 tooltip, and `Document::clear_custom_formula` returns false. The clear writer and its heuristic are still live in the save hook, and a test asserts they work (R33-2). |
| W33-1.5 | partly | The Qt Task Information and Task Fields handlers use a trial copy (`main_window.cpp:5344-5397, 3676-3689`); the macOS Task Fields handler does not (R33-3). |
| W33-1.6 | verified as a fix, but enables an unmeasured write | The dialog carries the shown index and the Qt test is green, but there is no Project measurement for an index other than 0 (R33-1). |
| W33-1.7 | verified | The reader-warning predicate matches the writer's refusal condition (FAT shorter than the file). The model backstop is in `push`, and both shells refuse at every edit entry point and at Save; the slack fixture is pinned in `check_fixtures.py`; `qt_fat_slack` added. |
| W33-1.8 | verified | `apply_custom_formula` refuses while `custom_formula_.any()` (`document.cpp:569-575`). |
| W33-2.1 | verified | Writer DIFAT gets the reader's count bound, a visited set and a FAT-id bound (`cfb_writer.cpp:349-398`); `mpp_cfb_hardening` times both parsers. |
| W33-2.2 | verified | MPP14 count is clamped to `available / 51` (`reader.cpp:2114-2125`); the MPP12 site was audited and justified in a comment. |
| W33-2.3 | verified | v4 and sector shift != 9 are refused explicitly in both parsers. |
| W33-2.4 | partly (CI pending) | `fuzz_reader` also feeds `CfbDocument::from_bytes`; `fuzz_xml` feeds `import_mspdi` and `export_csv` (no CSV importer exists, as recorded). The `ubuntu-asan` job is written but has never run (R33-9). |
| W33-3.1 | partly (CI pending) | `ci.yml` builds `app/` with `APP_BUILD_TESTS=ON`, `CORE_WARNINGS_AS_ERRORS=ON` and runs ctest with `MPP_NO_CORPUS=1` on both matrix jobs. `app-macos.yml` runs on push, and its registration check is derived from CMake in both directions. Its acceptance (CI totals) is unverified. |
| W33-3.2 | verified | The mask is exactly the control-vs-null bytes, the tail is compared directly, and `effects_declared` / `effects_ok` are checked by every caller. The 23 exposed mismatches equal the 23 named expected failures (logs), and no mask was widened elsewhere (`owned_stream` / `kind_for_path` unchanged). See R33-5, R33-6. |
| W33-3.3 | verified | `tests/fixture_guard.h`; committed fixtures exit 1 with their path; the remaining executable SKIPs are corpus-gated, MPXJ variants, generated W45 outputs (known RW33-8) and a build-mode probe. No test was removed from any CMake registry (only the rename `qt_render_parity` to `qt_render_1920`). |
| W33-3.4 | verified | Both app_qt counts quoted; the backlog summary carries the pin truth; `MPP_NO_CORPUS=1` on the first CI pass; render test renamed. |
| W33-4.1 | verified (spot-checked) | START-HERE states the true branch state, says how to look up a sha map entry, and lists R32-1/R32-2. |
| W33-4.2 | verified | `.gitattributes` pins five types; the CI CR step is on all jobs; `git ls-files --eol` shows 0 CRLF/mixed among the pinned types (and in `*.mm`, `CMakeLists.txt`, `*.ps1`). |
| W33-4.3 | verified | `check_markers` (UTF-16 at both alignments, binary included) is clean on both `.mpp`; pins updated; `source_sha256` provenance not rewritten. |
| W33-4.4 | partly (CI pending) | Script and self-test (19/0) work, and the property-set scan covers every tracked `.mpp` and fails closed on an unparsable file. The no-base path still prints "skipped" and exits 0 (known RW33-9). The CI step has never run. |
| W33-4.5 | verified | 0 marker hits across 1302 tracked files; `phase*.py` paths are repo-relative. See R33-4. |
| W33-4.6 | verified (spot-checked) | Shared `oracle_privacy.neutralise`; the null-save oracles deliberately set only UserName. |
| CLOSE | partly | Records updated and the replay verified (only `3b53b9d`'s tree and `964594d`'s message changed; old and new tips have identical trees). The "added no writer" sentence is wrong (R33-1), and post-replay CI is absent. |

## What I could not check

- **CI:** no run exists for `wave33`. `origin/wave33` is absent and `gh run list --branch wave33` returned nothing. So I could not see the app ctest totals on windows-msvc or ubuntu-gcc, the `ubuntu-asan` job (clang plus `-Werror` on the core tree has never been seen in CI), the marker step, or the macOS push run.
- **Builds and tests:** none were built or run (read-only brief; a gate build was in progress). That includes the brief's verification-gate requirement that W33-0.1, W33-0.2 and W33-1.1 be seen RED on `17f5782` and GREEN on the tip.
- **macOS code:** `main.mm` compiles only on CI; the AppKit changes were reviewed by reading only.
- **Linux builds:** I could not confirm that the standalone `app/` tree builds and passes under GCC with warnings-as-errors.
- **Project/COM:** no Project or COM measurement was possible, so the R33-1 Project behaviour is unknown, not known to differ.
- **Gate logs:** only the W33-3.2 logs were cross-checked against record numbers; the other per-commit gate logs were not audited.
- **Historic blobs:** `check_markers.py` scans the index, and `guard_names.py` scans text diffs only. Binary blobs that were added and later replaced inside the range were not marker-scanned by either. I relied on the replay check, which shows the only replaced blob is `3b53b9d`'s.
- **Assessment ids:** the audit's section 1/7 ids beyond those the brief lists (for example A-6, A-10, B-1, B-5) were not in this wave's scope and were not re-audited.
