# Wave 33 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-33-<group>-COMPLETE` line is appended below (wave-33-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - data safety (Critical)

| item | finding | status | evidence |
|---|---|---|---|
| W33-0.1 | A-2 / D-1 / D-4 (Critical): every `.mpp` write opens the final path with `ios::trunc`, so a failed or short write leaves the target truncated | **DONE** | commit `c176a0f`; gates `build/w33/W33-0.1-*.log` |
| W33-0.2 | A-1 (Critical): a second Save after a formula-only or date-format-only save silently discards it | **DONE** | commit `a6cfa05`; gates `build/w33/W33-0.2-*.log` |

No G0 item was deferred.

W33-0.1 detail:

- One shared helper, `mppwrite::write_file_atomically(path, bytes, verify)`
  (`core/mppwrite/atomic_write.h/.cpp`), writes `<target>.tmp-<pid>` in the
  target's directory, flushes it, re-reads it and compares every byte, runs
  the caller's self-check, then renames it over the target with
  `std::filesystem::rename`.  The target is never opened for writing.  On any
  failure the temporary file is removed and the target is byte-identical to
  what it was; only a file the call created is ever removed, so the test's
  directory at the temporary name is left alone.
- `CfbDocument::save` passes a verifier that re-parses the re-read container,
  runs `check()` and requires byte-identical re-emission (the D-4 self-check).
  `InPlacePatcher::save` calls the helper directly.  Both `save.cpp` call
  sites (`save_date_edits` and `save_plan`, `core/mppwrite/save.cpp:489` and
  `:598`) end in `CfbDocument::save`, and every write of the
  `.w51-structural-tmp` cascade in `app/edit_model.cpp:3738-3752` is a
  `CfbDocument::save` to the intermediate or the final path, so all four
  named sites and the cascade are covered.
- `core/mpp`'s self-contained `mpp_reader_tests` compiles `atomic_write.cpp`
  beside `cfb_writer.cpp`; `core/mppwrite` registers the new test as
  `mppwrite_atomic_write`.
- Tests (`tests/atomic_write.cpp`), RED then GREEN: with a directory
  occupying `<target>.tmp-<pid>`, the pre-fix writer rewrote the target --
  7 checks, 3 failures (`build/w33/W33-0.1-red.log`); after the fix the same
  binary refuses and reports 16 checks, 0 failures
  (`build/w33/W33-0.1-atomic-green.log`).  Sections: the blocked temporary
  name leaves the target byte-identical and removes nothing it did not
  create; an ordinary save and the no-edit save are byte-identical to the
  source; the temporary name; a forced re-read mismatch leaves the target
  byte-identical and no temporary file; the helper writes a new file and
  replaces an existing one.

W33-0.2 detail:

- `app::Document::save` always derives its output from the file it opened.
  The non-structural route now discards the pending view-state and
  custom-formula operations, reloads the written file as the open document
  (which sets the shell's `owns_path` and re-points the document), and
  refreshes the title and menus.  A failed re-read is reported in the status
  line but does not claim the save failed -- the file is already on disk.
- Qt shell: `MainWindow::save_document_path`
  (`app_qt/main_window.cpp:3271`).  macOS shell:
  `saveDocumentWithPanel` (`app/main.mm:4148`), which mirrors the structural
  route's adoption; the macOS shell is compiled by CI only, as the brief
  says, and this change stays mechanical (existing `PlanState` /
  `PlanInput` fields only).  The locally executed RED/GREEN is the Qt shell.
- Tests (`app_qt/tests/save_reopen_test.cpp`): a custom-field formula is
  saved to a new target, read back from `114/Props`, then a second Save with
  nothing pending leaves the file unchanged, then Insert Task and Save keep
  the formula; the same shape for the table date format read from
  `214/CTable/Var2Data` byte 11.  RED on the pre-fix shell: `qt_save_reopen`
  2 failures -- "the formula survives the next save (the document adopted
  its own write)" and "the date format survives the next save"
  (`build/w33/W33-0.2-red.log`).  GREEN: 0 failures
  (`build/w33/W33-0.2-green.log`); the full app_qt suite 37/37.
- The test copies the committed fixture into the build tree and writes only
  there; nothing under `tests/data` is written.

Per-commit gates (logs under `build/w33/`; every item's scan reports 0 files /
0 hits and fixtures OK):

| commit | builds | tests |
|---|---|---|
| W33-0.1 `c176a0f` | core build exit 0; app build exit 0; app_qt build exit 0 | core ctest `-E plan_recalc` 53/53; `mppwrite_atomic_write` 16 checks, 0 failures; app ctest 25/25; app_qt ctest 37/37 |
| W33-0.2 `a6cfa05` | app build exit 0; app_qt build exit 0 | `qt_save_reopen` 0 failures; app_qt ctest 37/37; app ctest 25/25 |

G0 group gates (fresh trees configured from scratch on `ed4864b` under
`build/w33-fresh{,-app,-qt}`, log `build/w33/W33-G0-fresh-gates.log`; the only
difference from `a6cfa05` is this file's marker line):

| gate | result |
|---|---|
| core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| core ctest `-E plan_recalc` | 53/53 passed |
| `plan_recalc` with corpus / `MPP_NO_CORPUS=1` | 512 checks / 266 checks, exit 0 |
| engine `--selftest` / `--suite --expect-fail constraint-mso` | 53 checks, 0 failure(s) / 8 recorded (7 pass, 1 fail), 0 unrecorded, exit 0 |
| reader with corpus / `MPP_NO_CORPUS=1` | 1136 checks / 456 checks, exit 0 |
| M2 `recalc --compare` | 332/332, 268/268 and 64/64 fields match, exit 0 |
| app / app_qt configure and build with `CORE_WARNINGS_AS_ERRORS=ON` | exit 0 / exit 0 |
| app ctest / app_qt ctest | 25/25 / 37/37 passed |
| `check_fixtures.py` | fixtures OK (139 documented hashes, 173 tracked `.mpp`); paths OK (222 tracked test-data files, no local path); provenance OK (8 oracles) |
| client-name scan on HEAD / messages `wave33..HEAD` / `guard_names wave33..HEAD` | 0 files, 0 hits / 0 commits / exit 0 |

The run started 19:40 and finished 19:58.  The numbers belong to `ed4864b`,
the marker commit; its code tree is `a6cfa05`, and the record commit that
follows is docs-only.

## Found, not fixed

- Nothing new.  G0 raised no Critical, High, Medium or Low finding beyond the
  two items above.
- The macOS shell change (W33-0.2) is compiled by CI only; no macOS compiler
  runs locally, so its compile is the CI job, as the brief anticipates.  The
  brief's "keep it mechanical" is met: the edit uses only fields the
  structural route already sets.
- CLOSE is out of scope for this group session: the fresh-tree gates on the
  final commit, the review pass over the wave-33 diff, the push and the CI
  dispatch belong to the CLOSE section (wave-33-brief section 2).

WAVE-33-G0-COMPLETE
