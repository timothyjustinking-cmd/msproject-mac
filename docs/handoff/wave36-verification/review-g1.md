# Wave 36 G1 review: the font item writer

Range `53351b2..f88b1e2` on `wave36` (baab011, 95e8195, 893684f, 64a185e, 1fa7338, f88b1e2). Read with `git show` / `git diff` only; nothing checked out, built or run. The tree has moved on since then: `16752d4` (the G0b brief text) sits on top and is outside this review.
Evidence read: `docs/handoff/wave36-brief.md` (sections 0, 1, G1, 4), `docs/handoff/wave36-complete.md` (G1), `docs/handoff/wave36-verification/gates-g1.md`, `docs/handoff/standing-rules.md` rule 1, and the private repo's `evidence/trace-font-item-r6.md`, `trace-bold-r4.md` and `trace-bold-r3.md`.
Fixtures: `tests/data/wave35-g3-font-*` and `wave30-textstyle*` are present and unchanged in the range. **No `wave36-r7-font-*` pair exists anywhere at f88b1e2** (`git ls-tree -r f88b1e2`), so no round-7 pair is available for (b).

Count: Critical 0, High 1, Medium 0, Low 6.

## What holds

**Lift rule, per item**

| item | (a) trace names routine and bytes | (b) held-out pair | (c) field by field / 4(e) |
|---|---|---|---|
| W36-1.1 Bold off | Yes. `trace-font-item-r6.md` section 4: the same builder, the clear path at 0xe3a027, and the in-place copy at 1dabdb:976. Flags go 0x01000003 -> 0x00000003 and the font word bit 0 is cleared. | None. The record says so, and the writer stays pinned to its one measured shape (`unbold_cell` compares the whole built item, top byte 0x90 included, before rewriting). | Yes: `check_off_pair` gives 3 effect bytes and 0 mismatches. 4(e) not run. |
| W36-1.2 Bold on row 2 and on Start | Yes for the item: +0 at 0xe389d0, +4 at 0xe389db, +8/+10 at 0xe389e3 and 0xe389d7, +12/+24 at 0xece77a and 0xece793, +36 at 0xece7aa, +40..42 at 0xe3884b, 0xe38850 and 0xe3a027, and +43 written by nothing. `trace-bold-r4.md` sections 2 and 4 cover the header, the VarMeta re-pointing and the 214/Props size word. The Props14 marker is not covered (R36G1-5). | **Not met for the generalisation** (R36G1-1). | Yes for the three measured cells: `test_bold_cell_pairs` and `check_pair` give 6607, 6608 and 6607 effect bytes with 0 mismatches. 4(e) not run. |
| W36-1.3 Italic | Yes: +10 = 0x200, font bit 0x04 stored at 0xe3a05f. | None. The record says so, and the writer is limited to (uid 1, Name, on). | Yes: 6607 effect bytes, 0 mismatches. 4(e) not run. |

- **(c) and 4(e).** The field-by-field half of (c) holds for all five pairs, and every stream outside the declared ones is checked equal to the control. The 4(e) half is open for all three lifts. W36-1.4 is a plan, honestly labelled "planned, not run". The plan can see the edit if any of its methods works, because it has the right shape:
  - a positive control: the control and Project's save must read differently, or the pair is "not checked";
  - the edited cell compared with Project's;
  - the other cells of rows 1 to 10 in Name and Start compared with the control's, which catches a font on the wrong cell;
  - Bold off read from bold to not bold;
  - separate counts for the six font pairs.

  One candidate object is missing (R36G1-4). FontBold being setter-only is acknowledged, and the plan does not rely on it.
- **Masks.** The font word's top byte is masked at exact field level in both test files. They assert the item header at Var2Data 85,272 (length 44, id 0x2240000c, row, field), assert Project's byte (0x6b or 0x00) and the port's 0x90, and only then copy the port's byte into Project's copy at offset 85,327 (`tests/roundtrip_viewstream.cpp:650-664`, `app/app_edits_cell_bold_tests.cpp:355-377`). No stream is masked by name. The read-back checks mask only `& 0x00FFFFFF` of the font word.
- **Refusals that stay, all still in force at f88b1e2:**
  - Underline, strikethrough and both colours: `validate_edit`, "only Bold and Italic are measured". U, the font name and size combos and the colour splits stay disabled in `ribbon_pin.tsv`.
  - Size and name: no attribute exists for them.
  - Range: both shells refuse anchor != focus.
  - Combined attributes and a second change: one edit per save, and "already carries a cell format item". The Qt gate checks Bold on an Italic cell.
  - The project summary row: uid 0, `viewstream_edit.cpp:599`.
  - Other views: view uid 2 plus the block digest.
  - Italic off and Italic on any other cell.
  - Bold off on any cell but (1, Name).

  Each stays refused, and none is lifted without a pair.
- **Toggle logic.** `make_cell_bold_edit` tries Bold OFF only after ON is refused, and OFF succeeds only when the block's one item byte-equals the measured Bold item of that cell. An Italic item, another row's item, or Project's item with a non-0x90 top byte is refused, not rewritten. No path rewrites a foreign item.
- **Shell wiring.**
  - Read-only: Qt `refuse_read_only()` (`app_qt/main_window.cpp:3433`); AppKit `read_only_reason` (`app/main.mm:3572`).
  - Both shells take the cell's key from `bold_cell_field_id(table_field, field)`, and `static_assert`s tie the app's constants to the writer's.
  - A second Bold or Italic over a queued edit is refused by exclusivity (`edit_model.cpp:4191`).
  - Undo and redo of a Bold OFF keep it OFF (`check_off_pair`).
  - The save cascade re-runs the writer's dry run before writing.
- **W36-1.5 (D-2).** The label is corrected in `spec/writers/view-stream-214.md:155` and in the evidence note. No file under `tests/data` is touched in the range.
- **Hygiene.**
  - All six commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)` exactly once, and one item or group id each.
  - The diff has 0 CR bytes.
  - A scan of the diff for names, company words, personal paths, the sync folder and the private-repo name found nothing.
  - The author identity is the repo's existing noreply convention.

## Findings

### R36G1-1 (High): W36-1.2 lifts "any row" and "the Start column" without a held-out pair
**Where:**
- `core/mppwrite/viewstream_edit.cpp:599-618` accepts any uid other than 0 and 0xFFFFFFFF, and either field.
- `core/mppwrite/viewstream_edit.h:186` sets `kMeasuredStartFieldId`.
- `app/edit_model.cpp:3166-3181` (`cell_bold_plan_refusal`).
- `app/app_edits_cell_bold_tests.cpp:729-730` asserts "push() accepts the Start cell of a saved row" for `other_task`, which is an unmeasured cell.
- The (b) claims: `tests/roundtrip_viewstream.cpp:582`, `spec/writers/view-stream-214.md:234-238`, and commit `95e8195`.

**Failure:**
- **Only one pair has the Start key, and it was used to derive it.** `trace-font-item-r6.md` section 2 derives the row and field rule from the same runs the "held-out" pairs record. It reads +0 as the task row from `bold-row2` ("bold-row2 stores 2 with a single payload") and from the two-row `bold-range` run (w0 = 1, 2). It takes the Start key 0x0b400505 from the `bold-start` run. The writer's whitelist constant 0x0B400505 is that pair's byte, and the test feeds it back in (`pair.field = kStartFieldId`). So the claim that "no byte of these pairs was used" is not true for the field key. The only Start pair is the one the rule came from.
- **For rows, every pair with row != 1 is a derivation run.** `bold-range` duplicates `bold-row2`: same row 2, same field. No committed pair predicts a row value or cell the derivation did not already see.
- **The writer goes beyond any pair.** It writes Bold onto every row and onto the joint cell (row 2, Start), which the record says is unmeasured (`wave36-complete.md:175`). It also writes onto summary rows, and onto views where display position, ID and UniqueID can differ. +0 is "the row number" in the trace, and the app stores the UniqueID. The `id != uid` guard covers only one of those three readings: a collapsed outline (the port can save a show-level edit) changes the display position, not the ID.

The lift rule says: "Where (b) has no held-out pair, the writer stays limited to its measured shapes." The brief asked for "the pair not used to derive the row and field rule". None of the three qualifies.

**Fix:**
- Limit Bold ON, in the writer and in `cell_bold_plan_refusal`, to the three measured cells: (1, Name), (2, Name) and (1, Start). Each is its own pair, so each is a measured shape. Refuse the rest with the decision-9 text.
- Turn the push test at `app_edits_cell_bold_tests.cpp:729-730` into a refusal.
- Correct the (b) wording in the spec, the test comment and the record to "no held-out pair; limited to measured cells".
- Lift further only when a round-7 pair that played no part in the trace is predicted unchanged. Candidates: a row >= 3, the cell (row 2, Start), a task whose ID != UniqueID, or a collapsed outline.
- Alternatively, the coordinator can accept the builder's reading as a dated decision: committed pairs held out from trace runs of the same edits. That reading still leaves the joint cell and rows >= 3 untested.

### R36G1-2 (Low): the spec says the joint cell (row 2, Start) is still refused; the code accepts it
**Where:** `spec/writers/view-stream-214.md:249-250` ("Not measured and still refused: ... the joint cell (row 2, Start) as a direct pair"). Against it: `viewstream_edit.cpp:610-618`, `edit_model.cpp:3170-3180`, and `app_edits_cell_bold_tests.cpp:729-730`.

**Failure:** The record (`wave36-complete.md:175`, "The writer accepts it by the rule") and the spec contradict each other. A reader of the spec would believe a refusal exists.

**Fix:** This closes with R36G1-1's narrowing. Otherwise, reword the spec to "accepted by the rule, not tested by any pair".

### R36G1-3 (Low): the writer no longer checks the row against the file
**Where:** `core/mppwrite/viewstream_edit.cpp:599-609`.

**Failure:** Before W36-1.2 the writer pinned uid 1. Now it writes any non-zero uid other than 0xFFFFFFFF without checking that the row exists in the file's Task table. Only the app's `task_row_for_uid` stops a nonexistent row. A direct writer caller (a tool, a test harness or a future shell path) can save an item for a row that does not exist. The file header's refusal list (`viewstream_edit.h`) implies the writer guards this itself.

**Fix:** Have the writer refuse a uid that is not in the opened document's Task table (it already reads the document). R36G1-1's narrowing also removes the exposure.

### R36G1-4 (Low): the 4(e) plan's Method A does not list the Cell object
**Where:** `docs/handoff/wave36-complete.md:102-107`.

**Failure:** Method A enumerates Application, ActiveSelection, Task, View, Table and TextStyles. It leaves out `Application.ActiveCell` and its `Cell` interface, which is where Project's object model keeps per-cell formatting (CellColor and FontColor in later versions). Whether that interface has a Bold or Italic getter in Project 2021 is UNVERIFIED. It is the most direct candidate, though, and leaving it out could send the coordinator to the clipboard or pixel methods early.

**Fix:** Add `ActiveCell` and the `Cell` interface to step 1's enumeration, before Method B.

### R36G1-5 (Low): (a) has no named routine for the Props14 marker byte
**Where:** the writer's `write_edit_marker` (Props14 marker, `viewstream_edit.cpp`, shared with earlier waves). The evidence: `trace-bold.md:34`, which treats Props14 as whole-stream noise. Neither `trace-bold-r4.md` nor `trace-font-item-r6.md` names its writer.

**Failure:** Every font save emits 1 effect byte in Props14 (counted in the 6607 and 3). The pairs match it, but the three G1 traces do not name the instruction that writes it, so (a) is met for the item, VarMeta and 214/Props, and not strictly for every byte. This predates G1, and the field-by-field check covers it.

**Fix:** Record in the G1 record that (a) for the Props14 marker rests on the pairs, not the trace, or point to the trace that names it, if one exists outside these three files.

### R36G1-6 (Low): the Qt gate does not cover Undo or Redo against Save, or read-only, for the font commands
**Where:** `app_qt/tests/cell_bold_gate_test.cpp` has no undo, redo or read-only case for B or I. The guards themselves are at `main_window.cpp:3433` and in `edit_model` undo.

**Failure:** Undo and redo are tested only at the model layer (`check_off_pair`, `check_pair`). No shell test checks two things: that Undo of a queued Bold off followed by Save leaves the file byte-equal, and that B and I on a read-only file queue nothing.

**Fix:** Add both cases to `qt_cell_bold_gate`: undo, then save, then compare bytes with the source; and open read-only, click B and I, and assert an empty queue and the read-only message.

### R36G1-7 (Low): a second Bold click before Save gives an uninformative message
**Where:** `app_qt/main_window.cpp:3488` and `app/main.mm:3630`.

**Failure:** Bold ON is queued but not saved, and the user clicks B again. The builder reads the unchanged file and builds ON again. `push()` refuses it by exclusivity, and the status says only "Font Bold: the edit was refused". Refusing is correct, because a second change is unmeasured. But Project's button would toggle, and the text does not say why the port refused.

**Fix:** When the queue already holds a font edit, say so: "Undo the queued Font Bold first; a second change to the cell is not measured".

## Not findings

- **W36-1.1 and W36-1.3 enabled with no held-out pair.** Each writer is pinned to its one measured shape. The lift rule allows this ("stays limited to its measured shapes, and the record says so"), and standing rule 1 is met by the pair. This is the same footing as wave 30's one-cell Bold.
- **4(e) not run.** The brief makes 4(e) the coordinator's job, and the merge gate requires it. The builder labelled the plan "planned, not run" and claimed nothing.
- **Underline, size, name, colour, range, combined attributes and a second change not started.** The evidence is correct against `trace-font-item-r6.md`:
  - the underline, size, name and colour pairs are Font32Ex-shaped, and no toolbar-path Underline pair exists;
  - size and name are not stored in the item;
  - colour waits on bits 0x40 and 0x80;
  - no round-7 pair is committed.

  The dump script the record cites is under git-ignored `build/w36/`. That is acceptable for "not started", which needs no attempt.
- **The port pins the top byte at 0x90, and Project's own saves with 0x6b or 0x00 cannot be turned off.** This is a refusal on an unmeasured shape (`unbold_cell` compares the whole item), not a wrong file.
- **`app/main.mm` not built locally.** It is CI-gated (`macos-appkit`) at CLOSE, and the record says so. Read by eye, it mirrors the Qt code and uses the same shared builders.
- **The group gates ran on incrementally built wave 35g trees, not a re-clone.** The record says so, and the coordinator's fresh-tree gates follow the marker.
- **The extra `Co-Authored-By` trailer and the commit author identity.** The brief requires the `Generated-By` trailer and forbids neither of these. The author is the repo's existing noreply convention.
