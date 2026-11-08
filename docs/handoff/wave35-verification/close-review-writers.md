# Wave 35 CLOSE review, writers

_Independent read-only review of the writers in `git diff wave34b..wave35` (branch
`wave35`, 09 November 2026): `core/mppwrite/{assignment_growth,calculate,viewstate_sort,
viewstate_outline,viewstream_edit,varmeta_edit,props_edit,resource_growth,task_mode}.cpp`,
`core/mpp/reader.*`, `tests/roundtrip_*.cpp`, and the decode notes in `spec/writers/*.md`
and the fixtures' README entries.  Standards: `docs/handoff/standing-rules.md` rule 1 and
`docs/handoff/wave35-brief.md` section 1 rules 4 and 5.  No build, test, COM run or commit
was made; every finding is from source and from the committed evidence files.  Rows are
referred to by ID only._

## Verdict

**0 Critical, 2 High, 5 Medium, 6 Low.**  The two Highs are both "a shape that Project
writes differently can reach a save":  RW35-W1 (removal from a port-saved file that Project
has reopened and resaved, claimed refused but enforced only for the raw port output) and
RW35-W2 (Calculate Project assumes the pending cause is a leaf Duration edit and never
checks the leaf dates).  Under the merge rule both go to the front of the next wave.  The
wave-34 Highs (unmeasured work amount, identity-reference masks) are closed in the
assignment writer and its tests, and the new sort, outline, Bold, varmeta and props paths
have zero KnownGaps.

## High

| id | finding | file:line | direction |
|---|---|---|---|
| RW35-W1 | The W35-3.5 claim that removal from a port-saved file is "refused (writer and plan)" holds only for the raw port output.  The writer's settled-shape gate (`assignment_growth.cpp` ~2275-2361, "(rows - 1) / rows / flag 4 settled shape" and "is not the measured settled entry") reads the Assn FixedMeta entries, which a Project reopen-and-resave of a port-saved file can leave in the measured settled shape.  The evidence in `spec/writers/table-growth.md` (W35-3.5) shows Project's own edit of such a file differs from what `unassign_resource` writes in four places: Rsc Fixed2Meta flag 0x2b vs 0x0b (the port never touches Rsc Fixed2Meta in `unassign_resource`), the Assn Fixed2Data counter, Assn Var2Data bytes, and the CEdl list.  `test_remove_port_saved` (`tests/roundtrip_assignment_growth.cpp`) carries those as four KnownGaps and copies four null-save streams into the control, and it never runs `unassign_resource` on the null save itself, so the refusal is proved for the raw port output and for nothing Project has touched.  `app/edit_model.cpp` `make_assignment_edit` (~3015) has no origin check (port-saved or not) on removal, so the plan layer does not close it either. | `core/mppwrite/assignment_growth.cpp:2275-2361`; `tests/roundtrip_assignment_growth.cpp` (`test_remove_port_saved`); `app/edit_model.cpp` ~3015 | CONFIRMED at source for the test's settled document and the missing origin check; that the Project-resaved null save passes the gate is SUSPECTED (not traced byte by byte).  Direction: refuse removal whenever any of the four differing streams is not in the control's shape (Rsc Fixed2Meta flag 0x0b, the Assn counter, Var2Data and CEdl as in the unsaved control), or detect a Project-resaved file by the Props14 edit marker; add a test that runs `unassign_resource` on the null save and asserts the refusal. |
| RW35-W2 | `read_calculate_shape` (`calculate.cpp` ~120-185) gates on the unlinked two-leaf shape (no links, no constraint, FixedMeta 0x48/0xef, pending flag `01 00`) but never checks the leaves' Start or Finish against the project start or against Start + Duration, and never checks that the leaf early dates already equal the project start.  The writer then writes the project row Duration/Finish from the leaf with the latest Finish and rewrites the leaf slack and late dates on the assumption that the pending recalculation was caused by a leaf Duration edit.  A file left pending by any other edit (a leaf Start or Finish edit, a progress or calendar change that the checks do not see) reaches a save where Project would move the early dates too, and the port would write the old ones.  The single measured pair changes a Duration only. | `core/mppwrite/calculate.cpp:120-185` (shape), `:300-375` (writer) | CONFIRMED at source (no such comparison exists in the gate).  Direction: in `read_calculate_shape` require each leaf's Start == project start and Finish == Start + Duration on the working calendar (or the exact measured relation), and refuse otherwise; add a refusal test with a leaf Start edit. |

## Medium

| id | finding | file:line | direction |
|---|---|---|---|
| RW35-W3 | Calculate: critical-leaf selection uses `finish >= latest`, so on a tie the later row wins, and the critical leaf can be in row 2 or row 3.  The unlinked pair measures one arrangement only.  The committed linked pair shows the Fixed2Data stamp varies by row (control-to-edit +42: 0x30 for the first leaf and 0x28 for the second in the linked pair, 0x30 and 0x30 in the unlinked pair), yet `calculate.cpp:317` writes the constant `kCalculateLeafStamp = 0x30` for every leaf.  For the unlinked shape the stamp is measured as 0x30 for both leaves, but the tie case and a row order other than the measured one are unmeasured, so the constant is an assumption outside the measured pair. | `core/mppwrite/calculate.cpp:168, 317` | CONFIRMED at source and in `spec/writers/evidence/wave35-calculate-{unlinked,linked}.md`.  Direction: refuse equal leaf Finishes and any critical leaf other than the measured row; or measure a tie pair and a swapped-order pair. |
| RW35-W4 | Outline persistence (`viewstate_outline.cpp:119-146`) pins the view uid to 2 and the level lists to the two measured shapes (`kBaseLevels` 5 rows, `kDeepLevels` 6 rows), but takes the uids and levels from the edit, not from the file.  It never reads the Task table to confirm the rows' real outline levels or the view block (214/CV_iew) to confirm the view is the measured one.  A caller that passes a matching level list for a file with different real levels, or a different row order, is written the measured bytes.  Show Level 1..3 only, and the digest or noise-byte pin on the view block is absent. | `core/mppwrite/viewstate_outline.cpp:119-146` | CONFIRMED at source.  Direction: derive rows and levels from the file (Task table, outline level word) and refuse a mismatch; read the view block uid and key (0x20400006) before writing. |
| RW35-W5 | Sort persistence (`viewstate_sort.cpp:150-175`) checks that the requested order is a permutation of the displayed rows and that the key is one the pairs measure, but does not check the order against the file's data.  Any permutation of the rows is written the measured sort-spec bytes.  Duration ascending is accepted (`viewstate_sort.cpp:101-115` names it a supported key) while the measured pairs do not include it. | `core/mppwrite/viewstate_sort.cpp:101-115, 150-175` | CONFIRMED at source (permutation-only check), SUSPECTED that no Duration-ascending pair exists (the README entry lists the pairs and does not name one).  Direction: recompute the sorted order from the file's key column and refuse a mismatch; refuse Duration until a pair measures it. |
| RW35-W6 | `varmeta_edit` `kMeasuredKeys` (`varmeta_edit.cpp:125-170`) is keyed on (table, shape, key) only, with no uid and no payload pin.  The five overtime appends and the project-row appends are accepted for any record uid and any payload bytes that the caller builds for a measured key.  G0 pinned the key list, but the whitelist item is "field id" only, so a Project-different payload under a measured key reaches a save. | `core/mppwrite/varmeta_edit.cpp:125-170, 1034` | CONFIRMED at source.  Direction: pin uid and payload length (and the constant payload where the pair shows one) per key. |
| RW35-W7 | `props_edit` `record_measured` (`props_edit.cpp:112-125`) accepts six record ids (0x0240138B, 0x024013BE, 0x02401392, 0x024013C5, 0x02400003, 0x02400024) with no value pin, so any value is written under a measured id.  The header comment says "four ids", so the whitelist and its documentation have drifted. | `core/mppwrite/props_edit.cpp:112-125, 358` | CONFIRMED at source.  Direction: pin the value (or its measured range) per id and correct the comment. |

## Low

| id | finding | file:line | direction |
|---|---|---|---|
| RW35-W8 | `save_calculate` calls `save_var_record` (five times) and `save_props_edit`, each of which writes `path` (`varmeta_edit.cpp:1064`, `props_edit.cpp:493`), and then `work.save(path)`.  If `path` already exists and a later step throws, the intermediate save has already overwritten it with a half-applied file. | `core/mppwrite/calculate.cpp:355, 371, 373` | Write to a temporary path and rename on success. |
| RW35-W9 | `viewstream_edit.cpp:396` `kViewBlockNoiseAt = 42678` masks one byte of the view block in the digest gate.  The gate is enabled for Bold, so a changed byte at that offset is not caught.  It is a noise byte in the null save (counter-like), which is acceptable, but it widens an enabled gate by one byte and the reason is documented only in the decode note. | `core/mppwrite/viewstream_edit.cpp:396, 530` | Keep, with the null-save evidence cited at the constant. |
| RW35-W10 | Bold-on: the font word's top byte is recorded as noise in the pair, yet the command stays enabled.  The writer writes the measured value, not a computed one, so it is a trusted constant. | `core/mppwrite/viewstream_edit.cpp` | Name the byte in the README entry. |
| RW35-W11 | The outline roundtrip test normalises Project's CEdl size words (CEdl VarMeta +0x14 and 214/Props scalar 0x07000100) to the control's path-record length before comparing.  The normalisation is justified by the decode note (the size words track the path record) and is acceptable, but it is a test-side edit of Project's bytes, not a null-save mask. | `tests/roundtrip_viewstate_outline.cpp` | Cite the null-save evidence for the size word in the test comment. |
| RW35-W12 | `test_remove_port_saved` does not vouch for the Props14 edit marker in the port-saved file (see RW35-W1). | `tests/roundtrip_assignment_growth.cpp` | Fold into the RW35-W1 test. |
| RW35-W13 | `unbold_cell` (Bold off, in place) is exported but not wired to any shell.  Its proofs are sound; it adds writer surface that no shell exercises. | `core/mppwrite/viewstream_edit.cpp` | Leave, or remove until a shell needs it. |

## Checked and fine

- Bounds: `load_var_meta` in `assignment_growth.cpp` is now overflow-free (the R34G3-3 wrap is fixed); the new parsers (`parse_edl_items`, CEdl header and item loops in `viewstate_outline.cpp`, props and varmeta loops) check each offset against the stream size before reading; no unchecked offset found.
- W35-0.1 calendar checks (`check_resource_calendar`, `check_project_calendar`, `measured_default_week`) are called from `check_measured_shape` and from the unassign, first-add and settled-add paths, and the refusal cases in `tests/roundtrip_assignment_growth.cpp` (`add_cal_block`) pin them.
- RC-W1 and RC-W2 gates (`kFirstAddMeasuredRows = 3`, the `measured_order` gate at ~1639) and the R34b-3 `+128` stamp message are enforced in the writer, with tests.
- R34b-5 calendar-word assertions in `resource_growth.cpp` (word 154 is the highest calendar uid, the calendar row belongs to the template resource, the new row's word equals the appended calendar uid) are enforced.
- Bold: the writer pins the view block uid 1 and field 0x0B40000E, and the Bold-off proofs and the "no other stream moved" proofs hold.
- Sort and outline: both re-read the saved file and prove no other stream moved; `tests/roundtrip_viewstate_sort.cpp` and `tests/roundtrip_viewstate_outline.cpp` have zero KnownGaps; the viewstream, varmeta and props tests have zero KnownGaps.  No identity-reference mask (task uid, resource uid, calendar uid) was found in any new test; the only masked bytes are counters, timestamps and the null-save noise offsets.
- Calculate: the linked, lag and SNET pairs are refused with the "link rows" message and tested (`tests/roundtrip_calculate.cpp`); request and shape refusals are tested; the unlinked pair compares field by field against Project's edit save.
- `task_mode.cpp`: `check_task_row` covers deleted, summary, children, level, parent, duration, dates, progress, progress block and task calendar; the exported `read_task_file_shape` shares the same checks.
- `core/mpp/reader.*` additions (`type_block_stored`, calendar `stored_blocks`, `calendar_base_uid`, `calendar_stored_blocks`, `calendar_base_extra_blocks`) are read-only and bounds-checked.
- Check (6), deferred items: removal from a port-saved file is the only deferred refusal found not fully enforced (RW35-W1); the others (task mode, duration, calendar, constraint, type, effort-driven, resource type, rates, accrual, view, row order, outline level) are refused in the writer or pinned by a pair.

## Confirmation status

- Confirmed by reading source: RW35-W2, W3, W4, W6, W7, W8, W9, W13 and the missing origin check in RW35-W1.  RW35-W3 is also confirmed by the committed calculate evidence files.
- Only suspected: that Project's resave of a port-saved file passes the writer's settled-shape gate (RW35-W1), that no Duration-ascending pair exists (RW35-W5), and the Bold font-word noise byte (RW35-W10).

## Note, 09 November 2026 (the CLOSE coordinator)

The CLOSE coordinator re-read `read_calculate_shape` (`core/mppwrite/calculate.cpp` 120-185) and
confirms RW35-W2: it checks the link table, automatic mode, the FixedMeta bytes, the constraint
type and the pending flag, and takes the critical leaf from the leaves' Finish values, with no
check of any leaf's Start or Finish against the project start or Start + Duration.  The other
findings are the reviewer's, confirmed or suspected as each row says.  None is fixed in CLOSE;
RW35-W1 and RW35-W2 go to the front of `wave35b`.
