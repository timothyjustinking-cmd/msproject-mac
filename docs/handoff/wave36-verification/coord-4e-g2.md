# Wave 36 G2: brief 4(e), Project's reopen of the Props inserts (W36-2.2)

Coordinator, Sun 22 Nov 2026.  Project 16.0, read-only COM through `scripts/oracle_lock.py`, copies on the
neutral root `C:\w36\reopen`.  Script `reopen_4e_g2.py`, raw facts `reopen-4e-g2.json`, summary `SUMMARY.txt`.

Port saves: `tests/roundtrip_props.cpp` `test_insert_pairs` output `w36-props-insert-0..8.mpp`, copied from
`build/w36-tree/core/mppwrite/test-out` (written 15 Nov 2026 13:32:15, four seconds after `7961a6e`; neither
`props_edit.cpp` nor `roundtrip_props.cpp` has changed since).  Hashes in `port/hashes.txt`.

## Result: 0 of 9 pass

Project opens all nine port saves without error.  In every pair the port's reopen equals the control's, field for
field.  Project's own save shows the edit; the port's does not:

| pair | what Project's save shows | port |
|---|---|---|
| baseline-derived, baseline-nested, b-mode-automatic, b-scope-all | Baseline Start, Finish, Duration; BaselineSavedDate(0) set | none of it |
| b-assign-one, b-assign-two | the same plus Baseline Work | none of it |
| b-into-baseline2 | Baseline2 Start, Finish, Duration; BaselineSavedDate(2) set | none of it |
| note-summary-short, note-summary-600 | the summary task's note | no note |

## What it means

- The Props record W36-2.2 appends is not what Project reads the edit from.  The baseline values live in the task
  tables and the note text in the Task Var2Data, which the port does not write.
- For the note pairs the port's file carries Project's Task size word (from `props_table_words` over Project's Task
  table) over the control's Task table, which has no note.  Project opened it anyway and showed no note.
- Lift rule (c) fails for W36-2.2: the port has no save of the edit, only of one stream of it.  The refusal is not
  lifted; the writer is limited to its measured framing.  No app or engine path calls the insert, so no user's
  file is affected.  Finding **R36G2-C1** (coordinator, High for the record: a refusal recorded as lifted that is
  not), for the wave 36 record and the whole-wave review.
