0C/0H/1M/8L

# Wave 35f G0 review (`d884b57..79d24be`, items 0.1 to 0.4)

Read-only review. Everything was read through `git show 79d24be:<path>` and
`git diff d884b57..79d24be`. Nothing was built or run. Line numbers are at `79d24be`.

## What holds

- **0.1 (R35eG1-1): no route found where Save and the display disagree unannounced.**
  - The `doc_.sort_active()` check is now the second branch of the queue decision
    (`app_qt/main_window.cpp:6324-6331`). It comes after `read_only_reason_` (`:6316`) and after
    `make_show_level_edit` fails (`:6322`), so read-only, Level 3 to 9 and refused files keep
    their wave 31 notes. Only a Level that would be pushed is held back, and nothing reaches
    `structural_`.
  - A queued Sort is still refused first (`:6286-6289`). A queued Level and a shown session Sort
    cannot coexist, because `apply_sort_choice:6134-6137` refuses the sort. So the
    `had_queued_level` branch cannot run under a shown sort.
  - The session-only Level does not touch the redo list. The only view edit that can sit there
    under a shown sort is the Sort an Undo withdrew (0.3's state). Ctrl+Y re-queues the Sort the
    display shows, and `after_structural_change` (`:3614-3621`) rebuilds with every row
    expanded. So after Redo the display shows exactly what Save writes (the test asserts this at
    `sort_persist_test.cpp:286-290`).
  - Any later push rebuilds and drops the session collapse in the same way. No path queues a
    Show Level except `apply_outline_level`.
- **Both cases are tested in `qt_outline_persist`** (`outline_persist_test.cpp:716-749`):
  - the refused queue: Level 1 queues nothing, and the note needle is the new text;
  - the accepted session-only Level: Level 3 carries the builder's note and not the sort's.
  - Removing the new branch makes Level 1 queue on the six-task outline. The later "clearing the
    sort lets the same Level queue" check proves Level 1 is a measured shape there, so "queues
    nothing" goes red. The read-only case is not tested (R35fG0-5).
- **0.2 (R35eG2-1): every Qt formula and format surface is now guarded.**
  - `document_->apply_custom_formula` has two callers (`:4222`, `:4956`), and
    `request_view_state_edits` has one (`:4908`). Their functions guard at `:4183`, `:4929` and
    `:4879`. `MenuAction::custom_fields` reaches only `show_custom_fields` (`:2179-2183`).
  - The reverse order (formula pending, then Calculate) is refused by `calculate_project:3437`
    through `has_pending_edits()`, which covers the Custom Fields formula too.
  - The test goes red without the guard: the dialog opens and the timer closes it, so
    `dialog_opened` is set, and the status reads "custom fields cancelled".
  - The AppKit surfaces (R35eG2-3) and the Redo route (R35eG2-2) remain G1's.
- **0.3 reaches its guard.**
  - Green needs the note at `:6330`, which is only reachable when `make_show_level_edit`
    succeeded. So the measured-shape branch and the sort branch both ran.
  - The first guard does not fire, because the Sort was undone, not queued.
  - The `level < 0 &&` mutation turns the guard off, and push() then queues Level 1. The
    recorded 4 + 3 FAILs fit that.
- **0.4: the open queue matches `coord-gates-close.md`.**
  - START-HERE (`:12-44`), `coordinator.md` (`:8-22`) and the plan (`:450-468`) give:
    - the four Mediums;
    - the Lows R35eG0-1 to -3, R35eG1-4 to -6, R35eG2-2 to -10 and R35eW-1 to -4;
    - R35dG0-1 and R35dW-1 closed by the coordinator's G0 review (true, per
      `coord-gates-g0.md:35`).
  - The four 35b ids appear with their remainders. R35cW-3 has one status (`gates-g2.md:18`).
  - R35cW-4's remainder names R35dW-5 in `wave35d-complete.md`, START-HERE, the plan,
    `coordinator.md` and `gates-g2.md:28`. 2.11 now reads "partly fixed".
  - Every cited commit exists with the stated subject.
- **0.4 edited `status-2026-11-10.md`** (`:53-55` only). It dates the 05:35 "still open, one High"
  paragraph as a snapshot, and says R35dG0-1 was fixed by `b8ee455` and closed by the
  coordinator's review. That is true (the Latest section, `:7-16`; `coord-gates-close.md`). R35eG2-9's
  Fix offered this edit.
- **Hygiene.**
  - All five commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35f)`.
  - There are no names, client strings or personal paths, and `git diff --check` is clean.
  - I found no GCC/Clang-only warning pattern:
    - the size comparisons are `std::size_t` against `size()`;
    - `Surface` is fully initialised;
    - the new block's locals live in their own scope, and `-Wshadow` is not enabled;
    - the lambda is used twice.

## Findings

### R35fG0-1 (Medium): 0.3's corrected 1.1 record still says bypass (b) cannot be reached on the five-task file

- **Where:** `docs/handoff/wave35e-verification/gates-g1.md:57-58`. It reads "(b) only where the
  Sort stays session only, since on the five-task file the Sort queues and `push()` clears the
  redo list itself."
- **Failure:**
  - On the five-task file, only a single Name key queues. A Start, Finish or ID sort, or a
    multi-key sort, stays session only (`main_window.cpp:6165-6191`; `sort_persist_test.cpp:199-211`
    exercises Start descending as session only on this file).
  - So (b) was reachable there at `5d733ae`:
    1. Level 1 queued (a measured shape);
    2. Ctrl+Z;
    3. Sort Start descending (session only; the redo top is a `show_level`, which the old
       `redo_top_is(sort_view)` did not end);
    4. Ctrl+Y re-queued the Level under the Start-sorted rows.
  - This is R35eG1-3's error repeated for the other route, in the paragraph 0.3 was rewriting.
    The (b) tests run only on the six-task outline (`outline_persist_test.cpp`). The record now
    tells a reader that the five-task file needs no (b) test.
- **Fix:** rewrite as "(b) wherever the chosen Sort stays session only, including a non-Name sort
  on the five-task file", and either add a five-task Level 1, Ctrl+Z, Start descending, Ctrl+Y
  case to `qt_sort_persist` or say that the six-task test covers the same guard.

### R35fG0-2 (Low): the 35c and 35d records now call R35dG1-1, -2 and -3 "not yet closed by the coordinator" after that review ran, and the 35e CLOSE row still holds R35dG0-1 open

- **Where:**
  - "fixed in source by W35e-1.x, not yet closed by the coordinator":
    - `docs/handoff/wave35c-complete.md:59` and `:229`;
    - `docs/handoff/wave35d-complete.md:13`, `:61`, `:63`, `:65`, `:189`, `:191` and `:193`.
  - `docs/handoff/wave35e-complete.md:163`: "stays open in the coordinator's queue until the
    coordinator's mutations and independent review of G0".
- **Failure:**
  - The coordinator's G1 verification (`coord-gates-g1.md`: mutations red, review done) and CLOSE
    ran before 35f. `coord-gates-close.md` does not list R35dG1-1, R35dG1-2, R35dG1-3, R35dW-2,
    R35dW-3 or R35dW-4 as open. R35dG1-2 and R35dW-4 have no follow-on at all.
  - START-HERE `:42-43` says the 35d ids not named there "are not in the coordinator's open
    queue". So the records give two statuses again, the class R35eG2-9 fixed for R35cW-3.
  - The 35e CLOSE row contradicts `coord-gates-g0.md:35` and START-HERE.
- **Fix:**
  - Write "fixed by W35e-1.x; reviewed by the coordinator's 35e G1 verification", adding
    "follow-ons R35eG1-1, R35eG1-3" or "R35eG1-2" where they apply.
  - Mark the `wave35e-complete.md:163` row "closed by the coordinator's G0 review".

### R35fG0-3 (Low): the G0 record misstates what 0.1 builds and which guard was seen red

- **Where:**
  - `docs/handoff/wave35f-verification/gates-g0.md:45`: "nothing is built or pushed".
  - `gates-g0.md:90` and `docs/handoff/wave35f-complete.md:23`: "0.1: the queue-branch
    `sort_active()` check (the parent's `main_window.cpp`), 3 FAILs".
- **Failure:**
  - The edit is built by `make_show_level_edit` (`main_window.cpp:6322`) and then discarded. The
    commit message says so ("the edit is built as before").
  - The 0.1 red run is the parent, which has no queue-branch check, only the broader 35e refusal.
    Its 3 FAILs show the narrowing (the note, the shown Level, Level 3). They do not show the new
    branch turned off, and "queues nothing" stayed green there. The branch was seen red only by
    0.3's mutation, in `qt_sort_persist`.
- **Fix:**
  - Write "built but not pushed".
  - Under "Guards seen red", credit the branch to 0.3's `level < 0 &&` mutation, and call the 0.1
    run "the narrowing against the parent's refusal".

### R35fG0-4 (Low): clearing the sort leaves a session-only Level shown on a measured file, and nothing says it is still not queued

- **Where:** `main_window.cpp:6158-6164` (the cleared path of `apply_sort_choice`, "sort cleared")
  and `clear_session_sort` (`:6220`). Neither touches the collapse state.
- **Failure:**
  1. Six-task outline, session Sort, then Level 1. It is shown, and the note says "so the level
     is not queued".
  2. Clear the sort. The rows show Level 1 in the file's order, the state a queued Level 1 would
     write.
  3. The status reads "sort cleared". File > Save writes the file's own outline.
  - 0.1 makes this newly reachable: at 35e the Level was refused. This is not a wrong file. But
    the display shows a Level that Save will not write, and the last message does not say so.
    The builder's "Found, not fixed" (`gates-g0.md:105-107`) covers the shown sort, not this
    state after it is cleared.
- **Fix:** when a sort is cleared while a session Level is shown and nothing is queued, append
  "; the level shown stays session only, choose it again to queue it", and add a needle.

### R35fG0-5 (Low): the read-only case of 0.1 has no test

- **Where:** `outline_persist_test.cpp:736-749` tests Level 3 only. R35eG1-1's Fix and the brief's
  0.1 list a read-only file first.
- **Failure:** the `read_only_reason_` branch (`main_window.cpp:6316`) sits before the sort check
  today. A reorder that put the sort check first would refuse the queue with a note that names a
  Save a read-only file does not have, and every test would stay green.
- **Fix:** open the six-task copy read-only (or set the read-only reason through the test hook
  other gates use), show a session Sort, choose Level 1, and assert the read-only note and not
  "so the level is not queued".

### R35fG0-6 (Low): 0.3 does not assert that the Level is shown

- **Where:** `sort_persist_test.cpp:270-284`.
- **Failure:** "the displayed rows keep the sorted order" is a subsequence check with no size
  check. A refused Level (the 35e behaviour) leaves the rows unchanged and passes. So nothing in
  `qt_sort_persist` shows that the session Level is applied, unlike `outline_persist_test.cpp:726`.
- **Fix:** add `level_rows.size() < sorted_rows.size()` (the five-task Level 1 hides the
  subtasks) to the check.

### R35fG0-7 (Low): 0.2's status needle cannot tell the Custom Fields row from the row before it

- **Where:** `calculate_gate_test.cpp:417-425` and `:447`. The `custom_fields` needle is the same
  text as the `custom_field_formula` needle.
- **Failure:** if `show_custom_fields` returned silently before its dialog, the status line would
  still hold the previous row's refusal, and "the status line names it" would pass. Only
  `dialog_opened` discriminates. The record counts both FAILs as evidence.
- **Fix:** set a sentinel status (or check that the text changed) before each `trigger()`.

### R35fG0-8 (Low): view-state.md's test list leaves out 0.3's five-task case

- **Where:** `spec/writers/view-state.md:381-385`. It names the six-task `qt_outline_persist`
  cases only.
- **Failure:** the "Undo then Level" block of `qt_sort_persist`, which is the only test of (a) on
  a measured file, is not listed beside the 1.2 table row it guards.
- **Fix:** add "and, on the five-task file, a Level under the sort an Undo left shown, not queued
  (W35f-0.3)" to the `qt_sort_persist` list.

### R35fG0-9 (Low): hygiene, new prose lines over 100 columns

- **Where:**
  - `docs/handoff/status-2026-11-10.md:54` (122);
  - `docs/START-HERE.md:71` (106);
  - `docs/handoff/plan-waves27-30.md:441` (120);
  - `memory/coordinator.md:1` (106), `:8` (107), `:29` (130) and `:57` (173).
- **Problem:** this is the class R35eG2-10 lists, and the 0.4 edit adds one to the status file
  that R35eG2-10 named. Style only.
- **Fix:** rewrap.
