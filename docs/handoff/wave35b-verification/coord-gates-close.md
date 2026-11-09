# Wave 35b CLOSE: coordinator verification (Mon 9 Nov 2026)

The wave closed on the builder's `WAVE-35b-COMPLETE`.  The wave was then rebuilt onto the
rebuilt `wave35` and pushed, so the ids below are the pushed ones.  The builder's ids in every
earlier record of this folder are pre-rebuild; `history-rebuild-wave35b.map` in this folder
maps each old id to its new id, one pair per line (for example `90d8b4e` became `d117c15`, and
CLOSE's `25ad85b` became `95638bb`).

## The history rebuild (the operator's word: "Rebuild without it")

- `build/verify-w35/rebuild_history.py ec326b6 2ed490c wave35b ...` replayed the 75 commits of
  `ec326b6..wave35b` onto the rebuilt `wave35` tip `2ed490c`.  0 trees needed a substitution:
  `9564ddd` had already restored the neutral notes-generator fixtures before `ec326b6`.
- **Checked, pair by pair:** the same tree, the same author and committer, and the same
  message, with 0 mismatches over the 75 pairs.  The new tip's tree is identical to the old
  tip's.
- **The new tip descends from `2ed490c`,** and from neither the old `wave35` nor `9fa599b`.
  None of the three replaced blobs is reachable from `origin/main..wave35b`.
- `guard_names.py origin/main..e95c6ca` exits 0; the old branch exited 1.  The CLOSE record's
  remark that `guard_names` still names the two `9fa599b` blobs no longer holds.
- **The old tip** is kept only as the local ref `backup/wave35b-pre-rebuild` (`18b84f1`).  It
  is never pushed.
- `wave35b` = `e95c6ca`, pushed as a new branch (no force).

## CI on `e95c6ca` (dispatched; `ci` run 42575352642, `qt-shell` run 42575356533)

| job | result |
|---|---|
| windows-msvc, ubuntu-asan | green |
| **ubuntu-gcc** | **red: build** |
| windows-qt, macos-qt (50/50) | green |
| **linux-qt** | **red: build** |
| app-macos (push) | green |

- **R35bW-CI1 (coordinator; blocks merge).** GCC 13 stops at
  `core/mppwrite/viewstate_outline.cpp:435, 442, 449` with
  `-Werror=dangling-reference`.
  - The lines were added in `W35b-1.1` (R35G2-1, Show Level pins the view state).
  - `unique_edl_item` returns a reference into the caller's `items` vector.  GCC flags it
    because its `what` argument is a temporary `std::string`.
  - The reference does not dangle, but the gate is the build, and both Linux jobs fail on it.
  - **Fix:** take `what` as `char const*` or `std::string_view`, or return a pointer.  Then
    rerun both workflows.  This is the next fix wave's first item.
- **macOS:**
  - `macos-qt` is 50/50, including `qt_prompt_hook` and `qt_assign_resources_gate`, the two
    CI-4 failures on `wave35`'s macOS run.  CI-4 and R35bG2-9 are green here for the first time.
  - `app-macos` compiles `app/main.mm` (G1 item 1.7), the only gate it has.

## Brief section 4(e): Project reopens the port's enabled saves

These are `build/verify-w35b/reopen_4e.py` and `reopen_4e_b.py`, both git-ignored.
- **Method:**
  - COM through `scripts/oracle_lock.py`, with Project 16.0;
  - each file opened read-only from a copy on the neutral root `C:\w35\reopen`;
  - the port's saves are the app test binaries' output from the gated build at `d117c15`
    (pre-rebuild `90d8b4e`);
  - facts are recorded by ID only: task fields, assignments, the displayed rows in display
    order, and the project summary row.
- **The pass rule:** the port's reopen equals Project's reopen of its own save, field by field,
  and Project's reopen differs from the control's.
- **Evidence:** `build/verify-w35b/reopen-4e/reopen-4e.json` (sha256 prefix `2e6de4c28b12383f`)
  and `reopen-4e-b.json` (`daf5877631a4bbde`).

| enabled save | port vs Project | Project vs control | verdict |
|---|---|---|---|
| Calculate Project (`wave35-calculate-unlinked`), Calculation = manual for the reopen | 0 | 4 fields | **pass** |
| Sort, Name descending | 0 | display order | **pass** |
| Sort, Duration descending | 0 | display order | **pass** |
| Sort, Name ascending | 0 | 0 | opens and matches; vacuous by construction (the control is already in Name order) |
| Show Level 1, five-task outline | 0 | displayed rows | **pass** |
| Show Level 1 and Level 2, six-task outline | 0 | displayed rows | **pass**, each |
| Assign Resources, add / remove (`wave30-assign-*`) | 0 | 3 fields | **pass**, each |
| Bold, Task Name cell (`wave30-textstyle`) | 0 | 0 | opens and matches; Bold itself is not readable through COM |

Notes on the table:
- **Calculate:** with Project's default automatic calculation, its reopen recalculates the
  control too, so the first run read 0 differences.  The rerun sets `Calculation` to manual for
  the three reopens and restores it afterwards: it read -1 before and -1 after.
- **Bold:** `Cell` exposes only Text, FieldID, FieldName and the row objects through COM.  A
  visual check would need a screenshot.

The brief's 4(e) set (Calculate Project, Sort, Show Level, the wave 35 G0 assignment changes)
passes.

## Whole-wave independent review (`review-wave.md`)

A read-only agent reviewed `2ed490c..e95c6ca` for what per-group reviews miss: cross-group
interactions, every save path's dry run, the CLOSE record and hygiene.  Result: 0 Critical,
**0 High**, 2 Medium, 4 Low.

**Mediums:**
- **R35bW-1:** the "plan carries unsaved edits" refusal (W35b-1.5) is checked only when a Sort
  or Show Level is built.
  - A grid rename, Duration or Summary edit after the edit is queued is accepted silently.
  - So is a sheet undo, then Sort, then Ctrl+Y.
  - Save then writes the file's order while the screen sorts on the new values.
  - This is the Qt shell only.
- **R35bW-2:** the Sort counterpart of R35bG4-1.  A replaced or cleared queued Sort is
  withdrawn without `clear_redo()`, and Ctrl+Y re-queues it without `push()`.

**Lows:**
- **R35bW-3:** the record ids are pre-rebuild.  This file and the map now answer it.
- **R35bW-4:** the next wave's queue (`plan-waves27-30.md`, `wave35b-complete.md`, START-HERE,
  `memory/coordinator.md`) omits R35bG4-1 to -6 and R35bG2-9.
- **R35bW-5:** the CLOSE id table marks partly closed ids "fixed" with no caveat, omits RW35-R5
  and R35G3-10, and calls the builder's `gates-g3.md` and `gates-g4.md` the coordinator's.
- **R35bW-6:** builder commit `d60b3a6` (R35G0-12, pre-rebuild `ec07ab7`) also carries the
  coordinator's G3 records.

**Clean:**
- `read_task_file_shape` has one caller.
- The G4 defaults break no earlier path.
- The pins accept every existing Calculate and WBS write.
- Every Qt save runs its edit kind's dry run before writing, and Calculate's write is atomic.
- Hygiene: 60 of 75 commits carry `Generated-By`.  The 15 without it are:
  - 4 coordinator records;
  - 11 G2 builder commits, already R35bG2-4.

## Merge rule

The rule is CI green on all three OSs, no Critical or High open, and the 4(e) check run.  For
`wave34` to `wave35b`:
- **4(e):** run, and passing.
- **CI:** not met.  Linux is red (R35bW-CI1).
- **Highs:** not met.  R35bG0-1 and R35bG0-2 (Calculate Project) are open.

**No merge.**

## The next fix wave's queue, complete

1. **R35bW-CI1:** the GCC 13 `dangling-reference` build break.
2. **Highs:** R35bG0-1, R35bG0-2.
3. **Mediums:**
   - R35bG0-3 to -5, R35bG1-1 to -3, R35bG2-1;
   - the queued-view-state group, one fix with one guard over every collapse-state and order
     change, `clear_redo()` on withdrawal, re-application on rebuild, and the
     unsaved-edit refusal at queue time and on every later grid edit:
     - R35bG3-1 and R35bG4-1 (Show Level: redo, redo after All Subtasks, the summary toggle);
     - R35bW-1 (grid edits under a queued Sort or Show Level);
     - R35bW-2 (Sort redo).
4. **Lows:** the Lows of `review-g0.md` to `review-g4.md`; R35bG4-6 (the `TaskRow`
   `type_block_known` default is untested; mutation G7c stayed green); R35bG2-9 (watch, green
   on macOS now); R35bW-3 to -6.
