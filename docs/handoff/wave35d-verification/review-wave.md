# Wave 35d whole-wave review (d3b68fa..3d22c39, 31 commits)

0C/0H/3M/3L

Read-only, through `git show 3d22c39:<path>` and `git diff d3b68fa..3d22c39`. Nothing was built,
run or edited in the repo. Line numbers are at `3d22c39`. Findings already raised in
`review-g0.md`, `review-g1.md` and `review-g2.md` (R35dG0-n, R35dG1-n, R35dG2-n) are not repeated.
Where a CLOSE record still carries one of them, it is cited, not re-raised.

## The four questions

**1. Do the G1 and G2 shell changes bypass or strand the G0 redo fixes?** Mostly no. One route
is still open, and it reaches a third kind (R35dW-1). I walked Undo and Redo across Sort, Show
Level, Calculate Project and cell edits:
- **1.2's refusal sits before 0.4's redo clear in `apply_sort_choice`** (`main_window.cpp:6086-6089`
  vs `:6093`). That order cannot strand a withdrawn Sort.
  - The refusal fires only while a Show Level is queued. A queued Level got there through
    `push()`, which empties `undone_` (`edit_model.cpp:4710`), or through Redo, which pops the
    only entry.
  - So `undone_` never holds a Sort under a queued Level. It also never holds two exclusive
    edits: an exclusive push needs an empty `applied_`, and every push clears `undone_`.
  - No committed fixture takes both a queued Sort and a queued Level. So the cross-kind rule
    only ever compares a session state with a queued one.
- **2.11's `data_change_refusal` does not touch the 0.5 clears.**
  - Under a queued Calculate, Cut and Paste are refused before their `clear_redo()`. Nothing is
    lost: `push()` had already emptied the list.
  - After an Undo nothing is queued, so 2.11 is silent and the 0.5 clears run.
  - The sheet's own Undo and Redo cannot act under a queued kind. Every push rebuilds the sheet
    (`plan_document.cpp:775`), so the sheet has no history, and the edit gate blocks new history.
- **The rebuild's re-applied Sort** (`main_window.cpp:3605-3626`) changes nothing on any public
  route (confirmed by S4). It runs inside every queueing rebuild before `apply_session_sort`.
  That is harmless, because `apply_session_sort` then sets the user's own definition.
- **Task Information's partial apply (R35dG0-1) is the one route left open.** It also reaches
  Calculate Project, and the 2.11 record says that route is closed (R35dW-1).
- **The 0.4 record's Show Level conclusion is contradicted by 1.2's rule.** The 0.4 record says
  "Show Level does not diverge" (`wave35d-complete.md:34-36`, `:207-208`). 1.2 then made
  "sorted rows under a queued Level" a refused state, and R35dG1-1 route (b) reaches that state.
  The CLOSE record does not reconcile the two (R35dW-3).

**2. Does 2.8 (the Props pin moved last) change which refusal the 0.3 tests and the 1.4 text
reach?** No, for the tests:
- Each 0.3 case and the 1.4 title case changes only `114/Props` on the control.
- `check_unvaried_parameters` and the start-anchor loop do not read `114/Props`.
- The 0.2 Props-start read comes before both the old and the new position of the pin.
- So each case still reaches `check_props_pin`, and its own needle was green at CLOSE.

The spec, though, still says every other project is refused "at the pin" with the 1.4 text.
2.8's own test proves that is no longer true (R35dW-4).

**3. The records at CLOSE.**
- No R35dG0-n, R35dG1-n or R35dG2-n id appears anywhere in the repo (`git grep R35d` at
  `3d22c39`: no hits). This is the R35cW-1 failure again (R35dW-2).
- Five wave 35c ids are recorded "fixed" while the coordinator's own records say "partly"
  (R35dW-3).
- R35cW-CI1 and R35cW-CI2 are still recorded "fixed" before CI in all four files (R35dG1-7 is
  unchanged at CLOSE).
- The brief's partly-fixed ids R35cG0-2 and R35cG0-4 are recorded correctly. R35cW-3 has two
  statuses (R35dW-6).

**4. GCC and Clang warnings, and standard library differences.**
- **Nothing found** in the wave's added code:
  - no partial designated or aggregate initializer (`SortKey` has exactly two members, and
    `FieldId` and `field_id` are both `uint32_t`, so the brace init does not narrow);
  - no `-Wdangling-reference` pattern (`sort_definition()` and `applied().back()` take no
    temporary);
  - no new `switch`;
  - no sign compare (`row < rsc_meta_type.size()` is `uint32_t` against `size_t`; the fault
    seam is `size_t` on both sides);
  - no unused parameter, variable or lambda capture.
  - `%u`/`%08X` on `uint32_t` is correct on all three CI ABIs, and the 160-byte `snprintf`
    buffer fits the widest expansion, so `-Wformat-truncation` cannot fire.
  - `for (std::size_t word : {8u, 12u})` widens and does not trip Clang's
    `-Wrange-loop-construct`.
- **`std::filesystem`:** the 2.7 probe is correct on libstdc++ (ENOENT set, `not_found`), on
  libc++ (ENOENT/ENOTDIR cleared, `not_found`) and on MSVC. Only the guard's test depends on the
  platform (R35dG2-4).
- **`std::sort` and `char`:** no new sort of equal keys (`var_keys` are `uint32_t`). No `char`
  signedness dependence (`rsc_meta_type` is `vector<uint8_t>`).
- **Not proven here:** `-Wpedantic -Werror` on GCC 13 and AppleClang cannot be compiled locally.
  CI remains the only proof.

**5. Could the writer now produce a file Project reads differently from the measured pair?**
No.
- Every core change this wave is a refusal, a refusal text, a test seam, or a reorder of
  read-only checks. 2.14 collects a superset-equal key set.
- No writer output byte changed, so the 4(e) result at `319d187` still stands.
- The R35dG0-1 route (and R35dW-1 for Calculate) makes Save write the file's own measured shape
  while the display shows something else. That is a display and guard divergence, not a
  divergent file.

## Findings

### R35dW-1 (Medium): Task Information's partial apply also re-queues a withdrawn Calculate Project over a changed Duration; 2.11's record, `calculate.md` and the G2 review all call that Redo route closed

- **Where:**
  - `app_qt/main_window.cpp:5899-5937`: `ok = false` on a later field, while
    `structural_.clear_redo()` sits only inside `if (ok)`, at `:5930` and `:5935`.
  - `app/edit_model.cpp:4781-4795`: `redo()` re-appends without `push()`.
  - `:4524-4536`: `calculate_slack_refusal` runs only in `push()`.
- **Records that claim the route is closed:**
  - `docs/handoff/wave35d-complete.md:110` ("the Redo route was closed by 0.5");
  - `docs/handoff/wave35d-verification/gates-g2.md:28`;
  - `spec/writers/calculate.md:393-395`;
  - `review-g2.md`, 2.11 ("the recorded closure holds", which cites `:5930`/`:5935` as
    unconditional).
- **Failure (calculate control):**
  1. Calculate Project, then Ctrl+Z. The Calculate goes to the redo list, and
     `data_change_refusal` is now silent.
  2. Open Task Information. Change Duration to a valid value and Finish to one that fails
     (read-only or unparsable).
  3. Duration commits and Finish fails. The redo list is kept, and `after_cell_change()` does
     not run.
  4. Ctrl+Y re-queues the Calculate. Its slack and late dates were computed for the old
     Duration. `push()` would now refuse it (`calculate_slack_refusal`), but Redo never asks
     `push()`.
  5. Save passes the digest check (`edit_model.cpp:6660-6666`), because the file is unchanged. It
     writes the file's own recalculation. The display shows a Duration the queued calculation
     did not use, and the edit's slack sits over it.
- **Impact:** the file is the measured pair, so this is Medium, not a new High. It is
  R35dG0-1's root in a third kind. That finding's Fix and its tests name only `qt_sort_persist`
  and `qt_outline_persist`, so a fix that follows it literally leaves Calculate unpinned.
- **Fix:** clear the redo list and call `after_cell_change()` whenever any field committed with
  `outcome.changed`, whatever `ok` is. Add the Undo-first partial-apply case to
  `qt_calculate_gate` as well as the two persist tests. Correct the three records to "open:
  R35dG0-1".

### R35dW-2 (Medium): at CLOSE no 35d review id is in the repo, and every entry file reads "no High open, awaiting review"

- **Where:**
  - `docs/START-HERE.md:21-22` and `:33`;
  - `memory/coordinator.md:24` and `:30`;
  - `docs/handoff/plan-waves27-30.md:410` and `:419`;
  - `docs/handoff/wave35d-complete.md:142` ("Every 35d fix still awaits an independent review").
  - `git grep R35d 3d22c39` finds nothing.
  - The G0 and G1 reviews (1H/1M/3L and 0H/3M/4L) and the coordinator's G0 and G1 records were
    written before CLOSE. They live only in the git-ignored `build/verify-w35d/`.
- **Failure:** the merge rule is "no Critical or High open". Every file a new session reads
  first says the three 35c Highs are fixed and says nothing of R35dG0-1 (High). This is R35cW-1
  again ("none of the per-group findings was in the repo"), in the files that finding named.
- **Fix:** commit the review and coordinator files to `docs/handoff/wave35d-verification/`. Add
  the R35d queue, R35dG0-1 first, to START-HERE, `coordinator.md` and `plan-waves27-30.md`.

### R35dW-3 (Medium): five 35c ids are recorded "fixed" that the coordinator recorded as partly fixed, the 35c back-annotations repeat it, and two CLOSE statements are stale

- **Where ("fixed" that is partly fixed):** `wave35d-complete.md`:
  - `:14` and `:183`: R35cG1-2. The coordinator says "only in part"; the remainder is
    R35dG0-1, High.
  - `:57` and `:184`: R35cG1-4. The coordinator says "partly"; the remainder is R35dG1-1.
  - `:61` and `:187`: R35cW-1. Partly; the remainder is R35dG1-3.
  - `:59` and `:186`: R35cG0-3. Its scope list is wrong (R35dG1-2).
  - `:110` and `:194`: R35cW-4. See R35dW-1.
  - The same statuses appear in `START-HERE.md:21-26`, `coordinator.md:24-27` and
    `plan-waves27-30.md:410-414`.
- **Where (the 35c back-annotations):**
  - `docs/handoff/wave35c-complete.md:59`, `:61`, `:229` and `:230`: "closed by W35d-0.5" and
    "closed by W35d-1.2".
  - `wave35d-complete.md:200-201`: the four 35b ids "have each had their remainder closed by
    35d".
  - R35bW-1's remainder (R35cG1-2) and R35bG3-1's (R35cG1-4) are still open.
- **Where (stale):**
  - `wave35d-complete.md:34-36` and `:207-208` still call the 0.4 Show Level route
    non-divergent and a pending decision. Under 1.2's own rule, R35dG1-1(b) diverges. This is a
    G0 conclusion that G1 invalidated, and CLOSE did not reconcile it.
  - R35cW-CI1 and R35cW-CI2 are still "fixed" at `wave35d-complete.md:10-11` and `:180`,
    `START-HERE.md:23`, `coordinator.md:25` and `plan-waves27-30.md:411` (R35dG1-7, unchanged
    at CLOSE).
- **Failure:** this breaks the brief's own "fixed means fully closed" rule five times in a wave
  whose item 1.5 existed to correct exactly that (R35cW-2).
- **Fix:**
  - Re-mark the five ids "partly fixed: <R35d id>" in the CLOSE table, the group rows, the
    three summaries and the 35c back-annotations, and correct `:200-201`.
  - Mark the Show Level route as diverging (R35dG1-1).
  - Write "committed, pending CI" for CI1 and CI2.

### R35dW-4 (Low): `calculate.md`'s 1.4 Scope sentence still says every other project is refused "at the pin" with the 1.4 text; 2.8 made the pin last, and 2.8's own test proves otherwise

- **Where:**
  - `spec/writers/calculate.md:349-352`: "Every other project ... is refused at the pin with ...".
  - `:358-362`: the Add Resources and custom-field port saves, attributed to Props records.
  - Against `:327-330` (the pin is last) and `tests/roundtrip_calculate.cpp`, the 2.8 case
    "another title and a resource row: the resource row is named".
- **Failure:**
  - A file that differs in the resource table, the calendar, the VarMeta keys, Active,
    FixedMeta or a start anchor now gets that check's text, not the pin's.
  - An Add Resources port save, in particular, is refused at the resource-table check.
  - The section states both orders.
- **Fix:** say "refused, by the first unvaried-parameter check it fails, or else by the pin
  with ...". Note that the Add Resources and field-set saves meet an earlier check first.

### R35dW-5 (Low): the AppKit shell queues Calculate Project but has no counterpart of 2.11; R35cW-4 is recorded "fixed" with no shell scope

- **Where:**
  - `app/main.mm:3570-3603` (`calculateProject:` pushes the edit);
  - `:1442-1463` (`commit_open_edit` has no queued-edit refusal);
  - `:1808-1841` (paste has none either);
  - `wave35d-complete.md:110`.
- **Failure:**
  - On macOS a grid edit or paste beside a queued Calculate is accepted silently. Save then
    writes the file's own recalculation, so only the display is wrong: exactly R35cW-4, on the
    other shell.
  - `calculate.md:387-395` names `MainWindow` only, but the id is ticked closed.
- **Fix:** add the same refusal to `commit_open_edit` and `paste_from_pasteboard` while
  `plan.edits.applied().back()` is a Calculate. Otherwise record R35cW-4 as "fixed in the Qt
  shell; the AppKit shell open".

### R35dW-6 (Low): smaller record inconsistencies at CLOSE

- **Where:**
  - R35cW-3 is "partly fixed" with R35cG0-2 at `wave35d-complete.md:188`, and "fixed" at
    `:198` (only two ids are partly fixed), `START-HERE.md:29-30` and `plan-waves27-30.md:415-416`.
    The finding asked only for a record, which 2.1 made.
  - `docs/START-HERE.md:3`: the header still reads "Updated 2026-11-09, wave 35 closed".
    R35dG1-3 covered only `coordinator.md`'s header.
  - `docs/handoff/status-2026-11-10.md:58` still says wave 35d "is started".
- **Fix:**
  - Give R35cW-3 one status, "fixed `6f5cc18` (record)", and keep R35cG0-2 partly fixed.
  - Update the START-HERE header and the status file to wave 35d closed.
