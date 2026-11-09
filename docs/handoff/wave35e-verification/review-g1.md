0C/0H/3M/3L

# Wave 35e G1 review (`5d733ae..07c4ef6`, items 1.1 to 1.4)

Read-only review. Everything was read through `git show 07c4ef6:<path>` and
`git diff 5d733ae..07c4ef6`. Nothing was built or run.

## What holds

- **1.1(a): the refusal works and shadows nothing.**
  - The new check (`app_qt/main_window.cpp:6263`) comes after the queued-Sort refusal at
    `:6255-6258`. Under a queued Sort the earlier guard still fires first, with its own text, so
    the `qt_sort_persist` needles ("a Sort is queued ... not a session Show Level") are unchanged.
  - `sort_active()` is true for a queued Sort too, since `apply_sort_choice` ends in
    `apply_session_sort`. It is also true after an Undo of a queued Sort
    (`sort_persist_test.cpp:239`).
  - All Subtasks (level 0) is correctly exempt. It only expands and queues nothing.
- **There is no other path that queues a Show Level.** `make_show_level_edit` has one shell caller,
  `apply_outline_level` (`:6301`). The View > Outline menu (`:6213-6228`) and the ribbon action
  (`:2495`) both go through it. `app/main.mm` does not queue a Show Level.
- **1.1(b): the route is closed.**
  - `apply_sort_choice` (`:6110`), `apply_session_sort` (`:6181`) and `clear_session_sort`
    (`:6192`) all end a `show_level` on the redo list.
  - Every place that sets the session sort goes through `apply_session_sort`, except the Redo
    rebuild (`:3630`), which runs only when a Sort is queued.
  - A Level can no longer sit on the redo list while a sort is shown.
- **The tests go red without the guards.** The (a) needles are specific: the queue, the exact
  status text and the displayed rows. The (b) needles are the Redo action's enabled state, the
  queue after Ctrl+Y and the rows. The record quotes a red run of 6 FAILs.
- **1.2: the paragraph matches the code at `07c4ef6`.**
  - `varmeta_edit.cpp:95` maps a remove to the freed tag `0x01000002`.
  - `actuals.cpp:1931-1938` moves Task freed, Task size, Assignment freed and Assignment size,
    and `:2023` writes the status-date record `0x02400045`.
  - `outline.cpp:2246-2251` moves Task freed, Task size, Assignment rows, freed, size and
    deleted.
  - The tail hash zeroes only `0x01000100` (`calculate.cpp:368-376`).
  - The pin is the last check (`calculate.cpp:862-877`). The "first unvaried-parameter check, or
    else the pin" wording is right. Smaller wording points are under R35eG1-6.
- **1.3 and 1.4: most of the lists are right.**
  - In `wave35d-complete.md`, R35cG1-2, R35cG1-4, R35cW-1, R35cG0-3 and R35cW-4 read "partly
    fixed" with their R35d ids, in both the item tables and the CLOSE table.
  - R35cW-CI1 and R35cW-CI2 read "fixed in source; CI is its gate" in START-HERE,
    `coordinator.md`, `plan-waves27-30.md` and `wave35d-complete.md`. No unqualified "fixed" is
    left (a `git grep`).
  - No list says "no High open". R35bG0-1 and R35bG0-2 are marked closed.
  - The 0.4 and 0.5 rows in `gates-g0.md` and `wave35d-complete.md` are corrected.
- **Hygiene is clean.** All 5 commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code
  (wave 35e)`. The diff adds no names, client strings or personal paths. I found no
  GCC/Clang-only warning pattern:
  - `SortKey{field, ascending}` names both members;
  - `SortDefinition{}` is value-initialised;
  - the loops compare `int` with `int`.
- **Informational, not a finding.** START-HERE (`:16-19`), `coordinator.md` and
  `plan-waves27-30.md` say R35dG0-1 is open until the coordinator's review. That review has since
  confirmed W35e-0.1. The builder could not have known this; CLOSE should update it.

## Findings

### R35eG1-1 (Medium): the 1.1(a) refusal also refuses Levels that could never be queued, and its text is false for them

- **Where:** `app_qt/main_window.cpp:6263-6269`. The check runs before the queue decision at
  `:6296-6312`.
- **Failure:**
  - With a session Sort shown, these Levels are now refused:
    - any Level on a read-only file (`read_only_reason_` set);
    - Level 3 to 9, which `make_show_level_edit` refuses;
    - any Level on a file the builder does not accept.
  - Before 1.1, each of these was applied for the session only and nothing was queued (wave 31,
    W31-1.2), so no Save risk exists.
  - The message says "File > Save would write the file's own order". On a read-only file there
    is no Save at all, and for an unmeasured level nothing would be written.
  - The record's reason for not keeping the Level session only (`gates-g1.md:51-53`, "Redo could
    re-queue under") does not apply. A session-only Level never goes on the structural stack.
- **Fix:** keep the check only on the queue branch. While `doc_.sort_active()`, show the Level
  for the session only, with the note "session only; a session Sort is shown", and do not build or
  push the edit. Add a needle that a Level under a shown sort on a read-only or unmeasured file is
  still applied.

### R35eG1-2 (Medium): R35dG1-3 is only partly done. START-HERE and plan-waves27-30.md still show the four partly fixed 35b ids as closed

- **Where:**
  - `docs/START-HERE.md:58-60`: wave 35c "fixed ... the wave 35b Mediums and Lows ...; partly
    fixed: R35bG1-3 and R35bG2-6".
  - `docs/START-HERE.md:79`: the same list of exceptions.
  - `docs/handoff/plan-waves27-30.md:391`: the new note says the ids 35c did not fix "are named
    in the paragraph below". That paragraph, at `:404`, names only R35bG1-3 and R35bG2-6, but
    R35bG3-1 is in the Medium list at `:396`.
  - `memory/coordinator.md:31` is the only file that adds R35bW-1, R35bW-2, R35bG2-2 and
    R35bG3-1. It says they "carry their remainders in the wave 35d and 35e states", but neither
    state names them.
- **Failure:** R35dG1-3's Fix said "Add the four partly fixed ids to the three summaries". A reader
  of START-HERE or the plan still sees all four as closed. Two of them are open through
  R35cG1-2 and R35cG1-4, which is R35cW-2's error again. `git grep` finds none of the four in
  START-HERE.
- **Fix:** add "R35bW-1, R35bW-2, R35bG2-2 and R35bG3-1 partly fixed (remainders R35cG1-2,
  R35cG1-1, R35cG2-1 and R35cG1-4; see `wave35c-complete.md`)" at `START-HERE.md:60` and `:79`,
  and at `plan-waves27-30.md:404`.

### R35eG1-3 (Medium): the 1.1 record misstates where bypass (a) was reachable, so one route is untested, and its "Found, not fixed" item describes a route 1.1 already refuses

- **Where:**
  - `docs/handoff/wave35e-verification/gates-g1.md:56-59`: "only where the Sort stays session
    only. On the five-task file ... neither route is a bypass there".
  - `gates-g1.md:117-121` and the matching "Found, not fixed" line in `wave35e-complete.md` (G1).
- **Failure:**
  - **Bypass (a) was reachable on the five-task file at `5d733ae`.** Queue Sort Name descending,
    then press Ctrl+Z. The Undo leaves the session sort shown (`main_window.cpp:2290-2301`,
    `sort_persist_test.cpp:239`) with nothing queued, and the Sort on the redo list. Level 1 then
    passed the queued-Sort check and was pushed, because the five-task Level 1 is measured. The
    push cleared the redo list, and the Level was queued under the sorted rows.
  - The new `sort_active()` check closes this route too, but no test drives it. If the check
    were narrowed to "a session-only Sort", every test would stay green and the route would
    reopen.
  - **The "Found, not fixed" route cannot happen at `07c4ef6`.** It is: Sort queued, Ctrl+Z, a
    session-only Level, then Ctrl+Y re-queuing the Sort over the Level. After the Ctrl+Z the sort
    is still shown, so the Level is refused at `:6263` before the builder is consulted. All
    Subtasks is allowed, but Ctrl+Y then re-queues the Sort that the display already shows, which
    matches.
- **Fix:**
  - Correct `gates-g1.md:56-59` to say that (a) was reachable through an Undo of a queued Sort on
    the five-task file.
  - Add to `qt_sort_persist`: queue the Sort, Ctrl+Z, Level 1. Assert nothing is queued, the
    refusal needle and the sorted rows.
  - Withdraw the "Found, not fixed" item, or restate it as "refused by 1.1(a)".

### R35eG1-4 (Low): the cleared-sort paths drop a withdrawn Show Level for nothing, and the redo clears were seen red only together

- **Where:** `main_window.cpp:6110` (it also runs on the `cleared` path, which returns at
  `:6125-6131`) and `:6192` (`clear_session_sort`).
- **Failure:**
  - Whenever a `show_level` is on top of the redo list, no session sort can be shown. A sort
    choice would have ended it, and (a) refuses a Level under a shown sort.
  - So clearing the sort does not change the display, yet it now throws away a Redo that would
    still match it: Redo re-queues the level relative to the file order, which is what is shown.
    That loss is the "needless" case the brief asked about, and it is never tested.
  - The two clears are also redundant on the non-cleared path. `apply_sort_choice` always ends
    in `apply_session_sort`, so removing either one alone keeps `qt_outline_persist` green.
    `gates-g1.md` "Guards seen red" removed both together and does not say they are redundant.
- **Fix:**
  - On the cleared path in `apply_sort_choice`, and in `clear_session_sort`, match `sort_view`
    only (`redo_top_is(..., sort_view)`). Keep `redo_top_is_view_edit` where a sort is set.
  - Record `apply_sort_choice:6110` and `apply_session_sort:6181` as redundant for the
    non-cleared path, and run each one red alone.

### R35eG1-5 (Low): the R35cW-4 remainder leaves out R35dW-5, and the R35d remainders are called "closed" in the same commit that says they await review

- **Where:**
  - `docs/handoff/wave35d-complete.md:114` and `:200`: R35cW-4's remainder is "R35dW-1 (closed
    by W35e-0.1)". `START-HERE.md:29` and `:50`, `memory/coordinator.md:20` and `:45`, and
    `plan-waves27-30.md:419-420` give R35dW-1 as the only remainder.
  - `wave35c-complete.md:59`, `:61`, `:229` and `:230`, and `wave35d-complete.md:13-14`, `:61`,
    `:63`, `:65`, `:114` and `:187-200`: "closed by W35e-0.1", "closed by W35e-1.1", "closed by
    W35e-1.2" and "closed by W35e-1.3".
  - `START-HERE.md:20-22`, `coordinator.md:16-17` and `plan-waves27-30.md:434-435`: the G1
    commit list stops at 1.3 and does not give 1.4 (`f244d02`, R35dG0-2).
- **Failure:**
  - R35dW-5 (the AppKit shell has no 2.11 refusal) is a second remainder of R35cW-4, and it is
    still open (G2). A reader of the 2.11 row concludes R35cW-4 is now fully closed.
  - "Closed by W35e-x" before the coordinator's review breaks the brief's tightened rule that
    "fixed" means fully closed. START-HERE, written in the same commit, says these ids "await the
    same review".
- **Fix:**
  - Give R35cW-4's remainder as "R35dW-1 (fixed in source, W35e-0.1) and R35dW-5 (open, G2)".
  - Write "fixed in source by W35e-x, awaiting review" in place of "closed by" until the
    coordinator closes each id.
  - Add 1.4 `f244d02` to the three G1 lines.

### R35eG1-6 (Low): two loose phrases in calculate.md's new paragraph

- **Where:** `spec/writers/calculate.md:363-366` and `:371-372`.
- **What is wrong:**
  - "when they come before the pin" reads as a condition. Those checks always run before the pin
    (`calculate.cpp:862-877`).
  - Assign Resources and its removal also leave a resource row on the Resource Sheet, so those
    saves meet the resource-table check (`calculate.cpp:460-469`) first, just like Add Resources.
    The paragraph names only Add Resources and the custom-field set.
- **Fix:** write "An Add Resources or Assign Resources save meets the resource-table check first,
  and a custom-field set the variable-record check, before the pin is reached; the pin's records
  for each are:".
