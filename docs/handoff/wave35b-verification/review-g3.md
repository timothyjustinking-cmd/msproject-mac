# Wave 35b G3 review: view state, Bold and the shells

Range: `733d6d3..af78097`, excluding `70b8a3b` (coordinator record).  Brief:
`docs/handoff/wave35b-brief.md`, "### G3" (W35b-3.1 to 3.8).  Date: Mon 9 Nov 2026.

Reviewer: independent, adversarial, read-only.  Nothing was built, run or edited.  Every
statement below comes from reading the source and tests at `af78097`, and the gate logs the
commits cite under `build/w35b/`; it is not from a fresh run.

Verdict: 0 Critical, 0 High, 1 Medium, 4 Low.

## Per item

| Item | Ids | Verdict | Evidence |
|---|---|---|---|
| 3.1 | R35G2-5 | Closed | `push()` now calls `mppwrite::outline_shape_measured` on the rows rebuilt from the plan, and those rows must equal the edit's vectors (`app/edit_model.cpp:4374-4389`).  The writer uses the same function (`core/mppwrite/viewstate_outline.cpp:138`, `:467-470`).  The two Level 1 cases (`app/app_edits_show_level_tests.cpp:521-526`) pass the range, row and level checks and so reach the new guard.  `red-3.1-run.log` shows them red without it.  The Level 3 case stops at the level cap first (R35bG3-3).  The a2 erratum is in `docs/handoff/wave35-complete.md:192-201`. |
| 3.2 | R35G2-6 | Partly closed | The delegate's toggle goes through `toggle_outline_row` (`app_qt/main_window.cpp:487`), which refuses while a `show_level` edit is queued (`:6053-6069`).  `back()` is enough because `push()` makes `show_level` exclusive, so it is always the only applied edit.  The `qt_outline_persist` case asserts the message, the unchanged rows and the still-queued level (`app_qt/tests/outline_persist_test.cpp:123-148`).  Other paths still change the displayed outline under a queued level (R35bG3-1). |
| 3.3 | R35G2-7 | Closed | The Show Level needle is now "task rows (with the project row)" (`app/app_edits_show_level_tests.cpp:461`).  The Sort needle had already been replaced in G2 (`e4a380d`), and the view pin is tested in the writer with "digest mismatch" (`tests/roundtrip_viewstate_sort.cpp:560-563`).  The Qt Level 3/4/9 needles now name the level and the measured pair (`outline_persist_test.cpp:165-168`).  New tests cover the doubled records, the layout length, the missing streams and "runs past the stream", each with its own count or index in the needle (`tests/roundtrip_viewstate_outline.cpp:610-615, 660-669, 696-712`; `tests/roundtrip_viewstate_sort.cpp:533-541, 634-657`). |
| 3.4 | M13 on G2 | Closed | Both Qt gates now save through File > Save and run `save_pair::compare_all` over the union of every stream.  This is a mismatch count outside the control-versus-null mask, the same discipline as the app tests (`outline_persist_test.cpp:212-257`, `sort_persist_test.cpp:193-234`, `tests/save_pair_compare.h:174-236`).  `red-3.4-m13-run.log` shows FixedMeta byte 219 red (port 0x02, Project 0x03).  The sort mutation goes red at `CV_iew/Var2Data` byte 42620 while queueing still succeeds.  A one-byte change outside the mask goes red.  A change inside the mask (a byte the null save also changed) does not, as in every pair test. |
| 3.5 | R35G3-3, -10 | Closed (brief option 2) | The mask is still one index (`core/mppwrite/viewstream_edit.cpp:535`).  Payload bytes 42677 and 42679 are flipped and refused for Bold off and for Bold on, and the masked byte is accepted in both directions (`tests/roundtrip_viewstream.cpp:539-561, 701-724`).  The widened-mask mutations repinned the digest, so their reds are real (`red-3.5-{minus,plus,window}-run.log`).  The comment and the spec now call the byte a session-date float32.  Residual exposure: R35bG3-5. |
| 3.6 | R35G3-4 | Closed | Each case reaches its own guard (`tests/roundtrip_viewstream.cpp:491-537` against `viewstream_edit.cpp:679-705`).  The swap keeps one cell item but moves it, so the not-last guard fires before the digest.  Relabelling the item before it as a cell item gives a count of 2.  A font top byte of 0x80 passes the digest (which skips the item) and fails `std::equal`.  Project's Bold-off save fails `std::equal` on its flags.  All four red logs exist and name the right needle. |
| 3.7 W4, W5 | RW35-W4, -W5 | Closed in G2; records accurate apart from one wording slip | The commits cited (`114861b`, `84a24d1`, `7bd9c5c`) match what the rows asked for.  `gates-g3.md:25` says "Show Level 1 to 3", but the cap is 2 (R35bG3-4). |
| 3.7 W6 | RW35-W6 | Closed | `check_project_row_key` (`core/mppwrite/varmeta_edit.cpp:201-235`, called at `:1087`) pins uid 0, eight zero bytes for the overtime records and two UTF-16LE digits plus a NUL for the project WBS record.  Every whitelisted entry for these keys is `append` (`:125-147`), so no other shape escapes the pin.  The measured callers (`wbs_code.cpp` `number_payload(3)`, the Calculate overtime appends) still pass. |
| 3.7 W7 | RW35-W7 | Closed | `payload_pin_clause` (`core/mppwrite/props_edit.cpp:122-156`) runs after the kind, shrink and equal checks (`:428-434`).  The only callers that write these ids are `wbs_code.cpp:58-64`, whose payloads match the pins byte for byte, and `calculate.cpp:745-756`, which writes a 4-byte date and 00 00.  No measured path is newly refused.  The comment that said "four" is corrected (`props_edit.h:91-97`). |
| 3.7 R3 | RW35-R3 | Closed | Zero, milestone and negative durations are refused by the start-plus-duration rule, in the builder and in `push()`.  Byte 16 of the FixedMeta flags is varied on both leaf entries.  The calendar and tie cases already had tests (`app/app_edits_calculate_tests.cpp:892-925`). |
| 3.7 R4 | RW35-R4 | Closed, with a parity gap on two unmeasured arrangements | `calculate_measured_late` (`core/mppwrite/calculate.cpp:67-112`) uses the same convention as `calculate_measured_finish` (`:36`): day 0 is Saturday 31 December 1983, so `day % 7` gives 0 for Saturday and 1 for Sunday.  Slack counts the working days in (finish, project finish].  The late start steps back D-1 working days from the project-finish day, then sets 08:00.  There is no off-by-one: one day gives 08:00 on the project-finish day, the mirror of the forward pass.  The writer checks it against Project's own words (`request_from(project)`, `tests/roundtrip_calculate.cpp:305`).  The built edit, made with the core calendar's `work_time_between` and `sub_work`, still queues (`app_edits_calculate_tests.cpp:265`).  The edit side and the writer agree on the measured shape.  For a tie or a swapped arrangement, `push()` accepts and only the writer refuses (R35bG3-2). |
| 3.8 | R35G2-8, R35G3-2, -5, -7 | Closed | Each correction matches its review row: `view-state.md` section 2 and the status row in `wave35-complete.md`; the font-byte rule in `view-stream-214.md:174-188` and `wave35-complete.md:283-286`; the lower bound and the session suffix in `table-growth.md:357-386` and `roundtrip_assignment_growth.cpp:2538-2569`; and "not yet measured" for Underline and the range.  The commits use the review's ids.  The brief's id-to-description order swaps R35G3-2 and -5, and the builder's id notes say so correctly. |

## Findings

### R35bG3-1 (Medium): other commands still change the displayed outline under a queued Show Level, and Save writes the level

- Where: `app_qt/main_window.cpp:2178-2189` (`MenuAction::collapse_all` and `expand_all` call
  `collapse_all()` / `expand_all()` with no queue check); `:2211-2239` (structural undo/redo);
  `:6077-6082` (`apply_outline_level` withdraws a queued level with `structural_.undo()`, which
  leaves it on the redo stack); `:2306` (the project-summary toggle).  Each rebuild goes through
  `app_qt/plan_document.cpp:766-776`, which makes a new `SheetModel` whose `collapsed_` starts
  all false (`core/sheet/sheet.cpp:2818`).
- What is wrong: 3.2 guards only the delegate's per-row toggle.  R35G2-6 is about the whole
  failure, the file describing an outline the user is not looking at, and that can still be
  reached without the delegate.
- Failure scenarios, on `wave35-viewstate-outline-level1-control.mpp`:
  1. View > Outline > Level 1 queues the level.  Expand All then shows every row, and File >
     Save writes UID 1 collapsed and UIDs 2 and 3 hidden.
  2. Level 1, then Ctrl+Z (the structural undo rebuilds, all expanded), then Ctrl+Y.  The redo
     re-queues Level 1, but the rebuilt sheet shows every row.  The status line says "redo the
     structural edit".  Save writes Level 1.
  3. Level 1, then All Subtasks (the level is withdrawn into redo), then Ctrl+Y.  The result is
     the same as scenario 2.
  4. Level 1, then toggling the project summary row.  The rebuild expands everything while the
     level stays queued.
- Fix: put every collapse-state mutator behind one guard: Expand All, Collapse All, the
  delegate, and any rebuild while a `show_level` edit is queued.  Choose one rule, refuse or
  withdraw (with `clear_redo()` after a withdrawal, so the level cannot come back unseen).  In
  `after_structural_change`, re-apply `doc_.show_outline_level(level)` when the applied list
  ends in a `show_level` edit, so redo shows what Save will write.  Add `qt_outline_persist`
  cases for Expand All, Collapse All, redo after undo, redo after All Subtasks, and the
  summary toggle.  Each should assert the status message and that the queued level matches
  the displayed rows.

### R35bG3-2 (Low): push() accepts a Calculate edit on two arrangements that only the writer refuses (a tie, and the critical leaf second)

- Where: `app/edit_model.cpp:3741-3778` (`calculate_slack_refusal`) and `:3604-3733`
  (`calculate_plan_refusal`); the writer refuses at `core/mppwrite/calculate.cpp:418-423`
  ("finish together") and `:520-527` ("is not the first leaf row").
- What is wrong: the RW35-R4 commit says `push()` recomputes "the project finish (the latest
  leaf Finish), the project duration (that leaf's)".  With tied finishes, "that leaf" is
  ambiguous.  `push()` takes the first in plan order (strict `>`), while the writer's loop
  takes the later row (`>=`) after first refusing the tie.  Neither `calculate_plan_refusal`
  nor `calculate_slack_refusal` refuses a tie, or a plan whose project-ending leaf is the
  second row.
- Failure scenario: a hand-built edit over a plan whose two leaves both start at the project
  start with equal durations, carrying zero slack and late start equal to start for both.
  `push()` returns true, the edit is queued, and Save refuses it with "finish together".  A
  swapped plan, where the second leaf ends the project and the words are right, queues in the
  same way and is refused at Save.  No bad file is written, and the shells use the builder,
  whose dry run refuses both.  So this is a parity gap in the backstop R4 was meant to close,
  not an exposure.
- Fix: in `calculate_plan_refusal`, refuse equal leaf finishes (needle "finish together") and
  a project-ending leaf that is not the first in file order (needle "is not the first leaf
  row").  Add `push()` cases for both to `app/app_edits_calculate_tests.cpp`.

### R35bG3-3 (Low): the 3.1 "Level 3 on a four-level outline" case never reaches the shape guard

- Where: `app/app_edits_show_level_tests.cpp:523-524`; the cap is
  `kOutlineMeasuredMaxLevel = 2` (`core/mppwrite/viewstate_outline.h:96`), checked first at
  `app/edit_model.cpp:4377`.
- What is wrong: the brief names this case as the a2 kill.  With the shape guard in place, a2
  is equivalent (the builder records this at `wave35-complete.md:192-201`), and the case is
  refused by the cap with or without the new guard.  It carries an "R35G2-5" label but proves
  nothing about R35G2-5.
- Fix: add Level 2 on `{0,1,2,3,0}`.  It is in range, and it hides the level-2 and level-3
  rows, so only the shape guard can refuse it.  Relabel the Level 3 case as a cap check.

### R35bG3-4 (Low): the RW35-W4 record says "Show Level 1 to 3"

- Where: `docs/handoff/wave35b-verification/gates-g3.md:25`; the `90a3ca9` commit message.
- What is wrong: the measured range is Level 1 and Level 2 (`viewstate_outline.h:95-96`, the
  W35-2.3 row in `docs/handoff/wave35-complete.md`, and the Qt needle "the measured Project
  pairs show Level 1 and Level 2").  The record copies the review row's text instead of
  checking it against the code.
- Fix: correct it to "Show Level 1 and 2 only" in `gates-g3.md`.  Do not amend the commit.

### R35bG3-5 (Low): enabled Bold on still accepts a second session's view block with no second-session pair

- Where: `core/mppwrite/viewstream_edit.cpp:520-537` (the mask applies to both directions);
  `spec/writers/view-stream-214.md` ("Bold on has no pair measured from a second session").
- What is wrong: 3.5 took the brief's second option, pinning the mask's width, and kept the
  mask on the enabled Bold-on path.  The brief allowed this, and the width is now proven by
  test.  But the standing rule 1 tension from R35G3-3 remains: Bold on over a 0x26-session
  control is enabled because only one known byte differs, not because a pair was measured.
- Fix: queue a Bold-on pair (uid 1, Name) taken in a second session for the measuring wave.
  Add it to the brief's "Queued for the next MEASURING wave" list so it is not lost.

## Clean

- 3.1: the edit's two vectors must equal the rows rebuilt from the plan before the shape check
  runs.  The guard therefore covers every `show_level` push.  The builder
  (`make_show_level_edit`) reaches the same function through the writer's dry run.
- 3.2: `back()` is sound.  `push()` keeps `show_level` exclusive in both directions, so no
  public sequence puts another edit after it.  The Qt case asserts the status text.
- 3.3 and 3.4: the needles are specific.  Each doubled-record needle carries all of its counts.
  The "runs past" needles carry the item index.  The missing-stream helper
  (`tests/cfb_stream_removal.h`) refuses an edited document and bounds its directory walk.
- 3.5 and 3.6: each test edits the bytes it claims, and the cited mutation logs exist and name
  the right needle.  The masked index is the payload index in both directions, because the
  Bold-off skip is after 42678.
- 3.7 calendar arithmetic: the weekday convention matches `calculate_measured_finish` and the
  MPP epoch.  Slack and late start have no off-by-one.  Integer bounds are checked
  (`remaining > 0xFFFF`, `last.day < 0`).  Invalid dates make the function return false, so
  the edit is refused rather than read as undefined behaviour.  The `hand_edit` test helper
  reuses the production function.  The independent oracles are the built edit's push and
  Project's own request.
- 3.7 W6 and W7: no measured caller is newly refused.  The pins come after the existing checks,
  so the new tests reach them and not an earlier refusal.
- 3.8: every correction is accurate and carries its review id.
- Hygiene: every builder commit in the range carries
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35b)` (17 of 17; `70b8a3b` is the
  excluded coordinator record).  The added lines name no client, company or person and hold no
  personal or absolute path.  The new tests write only under the build output directory.
