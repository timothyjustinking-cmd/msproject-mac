# Wave 35b G1 review: view-state, assignment and shell Highs (items 1.1 to 1.8)

- Range: `git diff e1397d1 e3c86fe -- . ':!docs/handoff/wave35b-verification'` (commits 114861b,
  84a24d1, b47f343, 7bd9c5c, e4a380d, e813c11, 48630a4, 97afeee, marker e3c86fe; 7d25bbd ignored).
- Date: 9 November 2026.
- Method: independent, adversarial, read-only.  Source read at e3c86fe with `git show`; no build, no
  test run, no COM, no fixture parsed.  "Confirmed" below means confirmed by reading the code path,
  not by a failing run.  Governing text: `docs/handoff/wave35b-brief.md` sections 1 and G1,
  `docs/handoff/standing-rules.md` rule 1, and the cited findings in
  `docs/handoff/wave35-verification/` (review-g2, review-g3, review-g4, close-review-writers,
  close-review-shells).

## Counts

| Severity | Count |
|---|---|
| Critical | 0 |
| High | 0 |
| Medium | 3 |
| Low | 6 |

No Critical or High.  Each of the eight cited Highs is closed for the shape the brief names.  The
Mediums are a use-after-clear on a new refusal path, a guard (1.6) that reads only the target row,
and a "both layers" rule met through one shared guard rather than two.

## Findings

### R35bG1-1 (Medium): `sorted_order_of` reads `sorted[index]` after `sorted.clear()`

- Where: `core/mppwrite/viewstate_sort.cpp:498-506` (the W35b-1.4 ascending-reorder refusal).
- What is wrong: the loop finds the first row where the computed order differs from the file's.
  It then calls `sorted.clear()` and builds the message from `number(sorted[index])`.  After
  `clear()` the size is 0, so the subscript is undefined behaviour.
- Failure scenario: two tests reach this path.  The core test in `roundtrip_viewstate_sort.cpp`
  renames task 1 to "Task Z" and sorts Name ascending.  The app test in `app_edits_sort_tests.cpp`
  does the same through `make_sort_edit` and `push()`.
  - Release builds (every CI job, and the ASan lane, which has no vector container annotations)
    read the stale value left in the capacity, so the tests pass by accident.
  - An MSVC Debug build (`_ITERATOR_DEBUG_LEVEL=2`) asserts "vector subscript out of range" and
    aborts the test process.
  - A libstdc++ `_GLIBCXX_ASSERTIONS` or libc++ hardened build aborts the same way.
  - In theory the message can name a wrong UniqueID.
  - The refusal itself (the return) is still correct.
- Fix: build the message from `sorted[index]` first, then clear and return.  For example, copy
  the value into a local before `sorted.clear()`.

### R35bG1-2 (Medium): the 1.6 resave guard reads only the target resource's FixedMeta byte 12

- Where: `core/mppwrite/assignment_growth.cpp:2267-2283`.  The other checks in that path are
  `check_measured_shape` (masks byte 12 of the target entry, `:1063-1064`), `check_resource_tables`
  (`:1199-1215`, no per-row byte check) and the row 0 flag check (`:2419`, byte 13).
  `core/mppwrite/resource_growth.cpp:748` writes `entry[12] = 0x1C` for every resource the port
  adds.
- What is wrong:
  - R35G3-1 records that Project's resave sets bit 0x40 on both resources' entries (stream bytes
    176 and 213).
  - The new guard reads only the target row (byte 213 for the measured key, resource 2).  No
    check reads any other resource row's byte 12.
  - The port's own resource add stamps 0x1C even into a file that Project has saved.
  - So "target byte 12 is 0x1C" does not prove that no Project session has saved the file.  The
    discriminating evidence (Rsc Fixed2Meta byte 279, or the other rows' bit 0x40) is never read.
- Failure scenario (synthetic, inferred by reading):
  - Take the measured removal control, set byte 176 (resource 1) to 0x5C and leave byte 213 at
    0x1C.
  - `unassign_resource(doc, {1, 2})` passes the new guard.  It also passes every other check this
    review traced, because they read only the target row and row 0.
  - The writer therefore accepts a file with a Project-session mark that no removal pair measured.
- Reachability not traced: whether a real lineage (a Project-saved file, then a resource added
  in the port, assigned and settled) reaches the removal in the settled shape is not traced.  The
  settled-shape gate may stop most such lineages, so this is rated Medium, not High.
- Fix: require byte 12 = 0x1C on every resource row (1..N) of `114/TBkndRsc/FixedMeta`.  Or pin
  Rsc Fixed2Meta byte 279 to the control's value, or refuse on the Props14 edit marker, as
  RW35-W1 suggests.  Add a writer test that sets only the non-target row to 0x5C and asserts the
  refusal needle.

### R35bG1-3 (Medium): "both layers" is met through one shared guard for 1.1, 1.3 and 1.6

- Where:
  - 1.1: `app/edit_model.cpp:3376-3383`.  `make_show_level_edit` refuses only through the
    writer's dry run (`viewstate_outline.cpp:557-558`).
  - 1.6: `app/edit_model.cpp:3043-3049`.  `make_assignment_edit` refuses only through
    `unassign_resource`'s dry run.  `spec/writers/table-growth.md` says "through its dry run".
  - 1.3, and the edit-model half of 1.4: `sort_plan_order` (`edit_model.cpp:3191-3205`), `push()`
    (`:4270-4286`) and `save_view_sort` (`viewstate_sort.cpp:592-597`) all call one helper,
    `sorted_order_of` (`viewstate_sort.cpp:432-452` for the outline pin, `:497-509` for the
    ascending reorder).
- What is wrong:
  - Standing rule 1, and brief section 1 ("both layers (edit-model builder and `push()`, and the
    writer)"), ask for a refusal in each layer.
  - For 1.6 the brief is explicit: "in the edit model, where `make_assignment_edit` has no origin
    check".  No edit-model check was added.
  - In each case above, one mutation at a single line turns both layers green:
    - `assignment_growth.cpp:2272`;
    - `viewstate_outline.cpp:557` or `:558`;
    - the `kMeasuredLevels` block of `sorted_order_of`.
  - The app tests do pin the needles, so they go red too.  But they are red only because the
    writer is red, not because the app has its own guard.
  - Separately, commit 114861b's message says "push() and the save cascade call the writer
    again".  `push()`'s `show_level` case (`edit_model.cpp:4287-4313`) does not call the writer;
    only the save cascade does.
- Exception: 1.4's writer side is independent.  `validate_edit` (`viewstate_sort.cpp:151-162`)
  has its own Duration-ascending and moved-order checks, separate from the helper.
- Failure scenario: a later edit to the shared helper or to the dry-run guard silently removes
  both layers at once.  The coordinator's "plant at the call site and at the helper" mutations
  would show one red set, not two.
- Fix: the coordinator decides one of two routes.
  - Accept dry-run-as-mirror and shared-helper-as-mirror as this project's two-layer pattern, and
    record it in the standing rules or the wave record.
  - Or add an independent edit-model check: for example a reader-provided "resaved" flag on
    `PlanInput` for 1.6, and an explicit level-vector compare in `sort_plan_order` for 1.3.
  Either way, correct the 114861b record line about `push()`.

### R35bG1-4 (Low): the Show Level view-block pin accepts either digest for either outline shape

- Where: `core/mppwrite/viewstate_sort.cpp:27-29, 531-544`, called from
  `viewstate_outline.cpp:558`.
- What is wrong: the Show Level pairs measure these combinations:
  - five-task levels {0,1,1,0,0} at Level 1 and Level 2, both on b2a54e044baa30a2 controls;
  - six-task deep controls on both b2a54e044baa30a2 (deep-level1, per R35G2-1) and
    6f6082994d512927 (deep-level2).
  `require_measured_view_block` accepts 6f6082994d512927 for the five-task shape too, which no
  pair measured.  The code's own comments say the sort-flat-name-desc control is a five-task
  file with these levels and the alt digest (`viewstate_sort.cpp:27-28, 432-435`; not checked
  against the fixture bytes here).  Show Level would accept that file.
- Why Low: the outline writer copies the view block unchanged.  The six-task shape is measured
  on both digests, which is evidence that the "text style" bytes do not affect what Show Level
  writes.
- Fix: pin the digest per shape (five-task: the primary digest only), or record the reasoning in
  `spec/writers/view-state.md`.  Either way, add a test that runs Show Level on the
  sort-flat-name-desc control and asserts the chosen behaviour.

### R35bG1-5 (Low): the 1.1 save-time app test asserts only the writer prefix

- Where: `app/app_edits_show_level_tests.cpp:632`: `saved.error.find("view outline write")`.
- What is wrong: any refusal from `save_view_outline` passes this check, including a row-count,
  UID or FixedMeta refusal.  The up-front builder tests and the core tests do use specific
  needles, so the guard is pinned elsewhere.
- Fix: assert the per-fixture needle (`"the applied table is \"Cost\""`, `"(digest mismatch"`,
  `"already carries a sort (the spec at block + 40"`) in the loop.

### R35bG1-6 (Low): the Qt menu test proves only "not the fall-through", not the right handler

- Where: `app_qt/tests/assign_resources_gate_test.cpp:263-321`.
- What is wrong: `not_wired()` checks that the status line does not contain "is not wired".
  - A case that dispatched `font_bold` to `define_wbs_code` would pass.
  - So would a handler that leaves no status at all.
  - The step that says "opens the prompt" never checks that the prompt was seen: the
    `PromptAnswerer` result is not read.
- Fix: assert each handler's own status text, or the prompt's `found` flag, after each trigger.

### R35bG1-7 (Low): the backlog lists the Define WBS Code menu item as implemented with no gap text

- Where: `docs/ui-command-backlog.md:157`.  In `app_qt/tools/command_backlog.py` the gap
  dictionary has the key `wbs_menu` for the ribbon, but no `define_wbs_code` key for the menu
  item.
- What is wrong: the other four wave-35 menu rows carry their "gap: ..." text.  This row reads as
  a full implementation of a command that is enabled for one measured three-task shape.
- Fix: add a `define_wbs_code` gap entry (the `wbs_menu` text) and regenerate the backlog.

### R35bG1-8 (Low): the 1.5 refusal advises "save" for an edit that Save does not write

- Where: `app/edit_model.cpp:3229-3232`: "save or undo the grid edits first".
- What is wrong: RW35-R2 records that Name edits in the Qt grid are session only and are not
  written by Save.  For a rename, Save leaves the plan and the file different, and the refusal
  stays.  Only undo or reopen clears it.  The refusal itself is correct.  This review did not
  re-trace whether Save writes Name edits; it relies on the RW35-R2 statement.
- Fix: word it as "undo the grid edits (or save them, where Save writes that field) first", or
  name the field's persistence in the detail.

### R35bG1-9 (Low): no app-level Sort test still reaches the writer's view-state refusals

- Where: `app/app_edits_sort_tests.cpp:553-556`.
- What is wrong: before this wave, the Resource Sheet file case reached the dry run.  Now
  `plan_unsaved_edits` stops it first ("UniqueID 1 Summary differs").  Apart from "already
  carries a sort", no app Sort test reaches the writer's digest or applied-view refusal through
  `make_sort_edit`.  The core tests still pin those refusals.
- Fix: add an app Sort case on a five-task Project save that differs only in view state, for
  example zoom-entire (this assumes its names and outline match the plan's), with the digest
  needle.

## Clean

Checked and found sound, by reading:

- **1.1 (R35G2-1)**
  - `save_view_outline` checks all of these before staging anything:
    - the CEdl applied-view name ("&Gantt Chart");
    - the applied-table name ("Entry") and its eight column ids;
    - the uid-2 block's 42,692-byte size and FNV digest;
    - the default sort spec in both copies.
  - The digest masks only the two six-byte sort-spec copies, and the spec is then checked
    separately, so no varying byte outside the spec is masked.
  - Each listed fixture has a specific needle: table-cost, table-schedule, three zooms and
    sort-name-asc, plus synthetic damage to each record.
  - The measured controls are still accepted: the four Show Level pairs, and a dry run on
    deep-level2.
  - The sort writer still pins the primary digest only.
- **1.2 (R35G2-2)**
  - The outline writer compares every row's +172 word with the request (1-based against
    0-based), with a needle per case.
  - `save_view_sort` re-reads the rows (UID order, +172, +84 duration, the stored name through
    `read_task_name`) and recomputes the order under the same pinned rules.  It refuses any
    mismatch.  A hand-built edit that passes `push()` is refused at save.
  - No remaining writer-side read of caller data replaces file data on these two paths.
- **1.3 (R35G2-3)**
  - Only {0,1,1,0,0} is accepted.  Flat, three-level, a third child, two summaries and "deeper
    than its parent" are each refused in the builder and the writer, with the level list in the
    needle.
  - No measured shape is lost: the flat-name-desc pair is evidence only, and Outline = False is
    refused in both layers.
- **1.4 (R35G2-4)**
  - Duration ascending is refused in `make_sort_edit`, `push()`, the writer's `validate_edit` and
    the helper.
  - An ascending reorder is refused against the request (writer) and against the file (helper).
  - Name ascending that moves no row still builds, pushes (tested) and matches its pair.
- **1.5 (RW35-R2)**
  - `plan_unsaved_edits` compares the plan with a fresh read of the file, by UniqueID, on every
    field `sync_plan_from_records` carries from the grid, plus the row count.
  - Pending rows are refused earlier (`file_row == 0`).
  - The check is content-based: a rename then undo correctly passes, and an edit made in another
    view of the same plan is caught.
  - A clean plan matches, because the fixture tests and the Qt clean cases still build.
  - For Sort, the writer's recompute (1.2) is a second, independent guard, so the Qt gate is not
    vacuous.  Without `plan_unsaved_edits`, the writer's needle would differ.
- **1.6 (R35G3-1, RW35-W1)**
  - The needle is specific.
  - The writer test runs `unassign_resource` on the null save itself.
  - The app test covers the builder, and a hand-built `push()` followed by a save that writes no
    file.
  - The 3.5 hybrid test is re-pointed, so it no longer proves the writer accepts 0x5C.
  - The partial-row gap is R35bG1-2.
- **1.7 (R35G4-1)**
  - Qt labels are unique for duplicate names, blank names, and a literal name that collides with
    a generated label.  The test picks every position.
  - macOS: `resource_popup` adds `NSMenuItem`s through `[popup menu]` with `tag = uid`.  NSMenu
    allows duplicate titles, and the tag carries identity anyway.  `resource_popup_uid` reads
    `[[popup selectedItem] tag]` and checks it against the uid list.
  - Resource Information prefills the chosen resource's name (R35G4-4).
  - The new code sits in the file's anonymous namespace; ARC is on; `<algorithm>` is included;
    `to_ns(std::string)` exists.  No compile error was found by reading.  CI remains its only
    compile gate, and the nil-action item enablement in a pop-up menu was not exercised.
- **1.8 (R35G4-2, RW35-R1)**
  - The five `handle_action` cases call the same `MainWindow` handlers as the ribbon, so they get
    the same builder and `push()` refusals.
  - `state.resource_sheet_view` is now set.
  - `shell_implements` lists every enumerator with no `default`.
  - `handle_action` keeps its existing `default:` (`main_window.cpp:2253`).  That is
    pre-existing, and the exhaustive `shell_implements` is the compile gate.
- **Hygiene**
  - All nine commits carry the `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)` and
    Co-Authored-By trailers.
  - No personal paths, client names or people's names are in the diff.
  - No new fixture.
  - GCC/Clang classes: no new enum switch without full coverage; no sign-compare or unused
    variable or function in the added code; the headers each new symbol needs are included
    (`<map>`, `<cstdio>`, `<array>`, `<string>`); `-Wshadow` is not enabled.
