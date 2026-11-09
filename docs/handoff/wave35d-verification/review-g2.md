0C/0H/0M/5L

# Wave 35d G2 review (2ebcfd8..d335791, items 2.1 to 2.15)

Read-only review at `d335791`, from the detached worktree and `git show`/`git diff`. Nothing was
built or run in the repo. One independent read-only decode was run outside the repo (the 2.15
Props start of the 51 `wave35-g3-*` and 51 `wave35-viewstate-*` fixtures, via olefile).

## What was checked and holds

- **2.8, the Props pin moved last** (`core/mppwrite/calculate.cpp:858-878`). `read_calculate_shape`
  has a single `return true` (`:878`) after the pin; every check between the old and new place
  (`check_unvaried_parameters`, the start-anchor loop) throws `mppwrite::Error` or returns `fail`,
  and every helper it reaches (`read_var_entries`, `read_var_block`, `fixed_meta_entry_at`, the
  new Fixed2Meta size test) is bounds-checked and throws `Error`, so no file reaches a later
  stage, and no new exception type escapes the `catch (Error const&)`. `check_unvaried_parameters`
  does not read `114/Props`, so no Props-only pin test can now be caught by it. Every pin test in
  `tests/roundtrip_calculate.cpp` (the flip loop `:833-858`, the record-kind needle `:876-892`, the
  header/count/tail cases, the pending-kind case `:1345-1361`) mutates only `114/Props` on the
  control, so it passes the moved-ahead checks and still reaches the pin; the record-kind text does
  contain "differs from the measured control", so the generic needle at `:1357` still matches the
  pin. No app test (`app/app_edits_calculate_tests.cpp`) depends on the pin firing before the
  parameter checks. The callers (`validate_calculate`, `save_calculate`, `make_calculate_edit`
  `app/edit_model.cpp:3993`) use the shape only on `true`.
- **2.6, the Fixed2Meta row word** (`calculate.cpp:511-519`). `kF2Record * (row + 3)` is the same
  expression as `task_mode.cpp:578-582` (`kFixed2RecordSize = 64`, record = row + 3) and as the
  writer's own `fixed2_record_offset` addressing (`:1016-1019`); the loop is rows 1..2, so 256 and
  320, which the control passes (the measured pair still writes). The new case
  (`tests/roundtrip_calculate.cpp:1185-1193`) is red without the check (it is then refused later
  with another text). The consistent swaps at `:1151-1158` and `:1194-1201` also still prove the
  by-index pairing: a by-position lookup would now trip the pairing check.
- **2.7, the scratch probe** (`calculate.cpp:1057-1070`). Correct on all three standard libraries:
  libstdc++ and libc++ return `file_type::not_found` and set `ec` to ENOENT/ENOTDIR for a free
  path; MSVC returns `not_found` for file/path-not-found. `ignored && type != not_found` lets every
  `not_found` through whatever `ec` says, so a free scratch path is not refused on Linux or macOS.
  A real failure (EACCES on the parent, ENAMETOOLONG) is `file_type::none` with `ec` set and now
  gets the OS text. The later `remove(scratch, ignored)` calls do not read `ignored`.
- **2.11, the Calculate gate.** Every cell surface goes through `data_change_refusal`: the sheet
  gate (`app_qt/main_window.cpp:749`, which also covers Resource Sheet cells), Cut `:4977`, Paste
  `:5077`, Fill `:5167`, Task Information `:5733`. Every other plan-changing command
  (`set_selected_percent`, indent, unlink, manual mode, rename/add/assign resource, bold, WBS,
  Project Information, Update Project, Mark on Track, insert/delete) goes through
  `StructuralEdits::push`, which refuses anything beside an exclusive kind
  (`app/edit_model.cpp:4092-4102`). Undo while Calculate is queued pops the Calculate: the rebuild
  (`app_qt/plan_document.cpp`, `rebuild_from_base`) makes a new `SheetModel`, so the sheet has no
  undo history to take first. Redo still re-appends without `push()` (`edit_model.cpp:4781-4795`),
  but every plan change after an Undo ends the redo list (`after_cell_commit` `:3556`, Cut `:5065`,
  Paste `:5151`, Fill `:5302`, Task Information `:5930`/`:5935`, and `push()` itself clears
  `undone_` at `edit_model.cpp:4710`), so the recorded closure holds. `queued_edit_is` tests only
  `applied.back()`, which is enough because nothing can be pushed after an exclusive edit.
- **2.12, skipped resource rows.** The reader loop starts at row 1 (`core/mpp/reader.cpp:2286`), so
  row 0 (0x5C in the control, read by the writer only when it is the target,
  `core/mppwrite/assignment_growth.cpp`) is never in `skipped_resources`; uid-0 rows at row >= 1 and
  tombstones (`rsc_live` is FixedMeta coverage, so a 0x02 entry is covered) carry their byte; a row
  with no entry is refused (fail closed, recorded). `PlanInput` copies (`edits.apply`,
  `without_structural_edits`, `sync_plan_from_records`) carry the flag; the only producers
  (`reader_adapter.h`, `app/main.mm:845`, `app/smoke.cpp:222`) go through the adapter, so the
  false default does not over-refuse the shells. The push and needle checks are red without the
  plan check.
- **2.14, VarMeta keys.** The custom-field walk is an unconditional block per task
  (`reader.cpp:2069-2155`); its only skip before the new push is the uid/secondary-uid filter,
  which keeps every `entry.uid == uid`, so the key set is the old loop's, then sorted at `:2176`.
  No other code writes `var_keys`.
- **2.3, 2.4, 2.5, 2.9, 2.10**: arithmetic and needles correct (`PackedDate::seconds_of_day` is
  `int`, so no sign compare; `PropsFrame` fields are `uint32_t`, so `%u`/`%08X` are right on every
  CI platform); no default-argument caller remains.
- **2.15**: my own decode agrees exactly: 36 files at 0x3BF11E78 (bytes `781ef13b`, 13:00) for the
  twelve named pairs, 15 at 0x3BF13138 (`3831f13b`, 21:00) for the five named pairs, all 51
  `wave35-viewstate-*` at 0x3BF13138, one start record per file.
- **Hygiene**: 16 commits, 16 `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35d)`
  trailers; no client names, people's names or personal paths in the diff; new struct fields have
  default member initialisers and no positional aggregate initialisation of `SkippedRow` or
  `PlanInput` exists, so no `-Wmissing-field-initializers`; no new unused variables or
  sign-compare.

## Findings

### R35dG2-1 (Low): Cut, Paste, Fill and Task Information's Calculate guard has no test

- Where: `app_qt/main_window.cpp:4977`, `:5077`, `:5167`, `:5733`; the only test is the cell gate,
  `app_qt/tests/calculate_gate_test.cpp:202-229`.
- Scenario: any of the four calls changed back to `refuse_view_change` stays green; the user's Cut
  or Paste is accepted beside a queued Calculate and refused only at File > Save. Not the only
  protection (`push()` and the save's exclusivity refuse), so Low; the builder record says so.
- Fix: in `calculate_gate_test`, after the Calculate is queued, call Cut, Paste, Fill Down and
  `apply_task_information` and assert each is refused with the Calculate text and the queue stays
  at one.

### R35dG2-2 (Low): Table / Date Format and Custom Field Formula are still accepted beside a queued Calculate, and a Calculate is queued beside them

- Where: `app_qt/main_window.cpp:4841-4884` (`show_table_date_format`, `request_view_state_edits`
  at `:4874`, then "edit pending; File > Save writes 214/CTable"), `:4917` (custom formula), and
  `calculate_project` `:3419-3445`, which pushes with no `document_->has_pending_edits()` check.
  The save refuses both orders (`app/edit_model.cpp:5719-5724`).
- Scenario: Calculate queued, then a date-format edit: the status says Save writes it; Save then
  refuses the whole save. The spec note `spec/writers/calculate.md:387-395` says the missing piece
  was the shell refusing "at the moment of the change", which is still not true for these two
  surfaces. No wrong file.
- Fix: refuse in `show_table_date_format`/`show_custom_formula` while a Calculate is queued, and in
  `calculate_project` while document edits are pending; or narrow the spec note to the cell
  surfaces.

### R35dG2-3 (Low): the "not blamed by the plan" check passes on any earlier refusal

- Where: `app/app_edits_assignment_tests.cpp:1460-1466`.
- Scenario: the check only asserts the error does not contain "a deleted or unreadable resource
  row"; if `make_assignment_edit` refuses the unmarked tombstone file for any earlier reason (or
  the plan check moved after another refusal), it stays green and proves nothing about the plan
  check. The reader flag itself is pinned by `:1452-1455`, so the gap is the plan direction only.
- Fix: assert the exact outcome (the builder accepts, or the error is the specific later text the
  file is expected to reach).

### R35dG2-4 (Low): the scratch-probe seam is a production global, and the "ec set but not_found" branch has no case

- Where: `core/mppwrite/calculate.cpp:38-43` and `:1057-1066`; `core/mppwrite/calculate.h:172-179`;
  test `tests/roundtrip_calculate.cpp:1430-1460`.
- Scenario: the seam can only fake `file_type::none`, so the branch that matters for CI (libstdc++
  and libc++ set `ec` to ENOENT on every free path) is exercised only implicitly, and only on Linux
  and macOS; the builder ran MSVC only. A mutation to `if (ignored)` would be green wherever the
  library clears `ec` for not-found and would refuse every Calculate save on the other CI
  platforms. The code is correct; it is the guard on it that is platform-dependent. The global is
  callable by any core library user (same precedent as `set_calculate_fault_after_appends`).
- Fix: let the seam take a `(file_type, error_code)` pair and add a case with `not_found` plus
  `no_such_file_or_directory` that asserts the save proceeds.

### R35dG2-5 (Low): two record lines misdescribe 2.4's build, and one spec sentence names "2.2" ambiguously

- Where: `docs/handoff/wave35d-verification/gates-g2.md:40` ("2.4 ...; app only"), and
  `docs/handoff/wave35d-complete.md:120` ("2.4 and 2.9 did not build app_qt (the change is in core
  and app)"); `spec/writers/calculate.md:239` ("2.2 must not be dropped").
- Scenario: `7d908c6` (2.4) changes only `tests/roundtrip_calculate.cpp`, a core test, and its
  commit message gives core 65/65 and app 34/34, so "app only" is wrong and "core and app" is true
  of 2.9 only. In a wave where W35d-2.2 is a records-only item, "2.2 must not be dropped" reads as
  that item, not the W35c-2.2 second-date-copy check it means.
- Fix: "2.4: core and app built, no app_qt (a core test only)"; "W35c-2.2 (`ecc51d1`) must not be
  dropped".
