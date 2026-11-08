# Insert Summary and Insert Milestone: what Project writes, and why the port does not

Status: W35-3.1 decode and measurement; **deferred with evidence**.  Insert Task
is already refused (W34-4.0), and Insert Summary and Insert Milestone need all
of the same machinery plus more.

## 1. The pairs

`scripts/oracle_w35_g3.py` (Project 2021, build 16.0, neutral root `C:/w35`)
builds six automatic tasks on a three-level outline (Task A > Task B > Task C,
Task D; Task A > Task E; Task F), saves and reopens the seed, saves the control,
makes ONE call with the cursor on the named row, and saves the edit; the null
save is the control reopened and saved.  The four pairs are committed
(`tests/data/wave35-g3-insert-*`) and their anatomies are under
`spec/writers/evidence/wave35-g3-insert-*.md`.

| pair | call | streams with effects (null to edit) |
|---|---|---|
| `insert-milestone-nested` | `InsertMilestoneTask` on row 4 (a level-3 leaf) | 15 task, assignment and edit-list streams |
| `insert-summary-nested` | `InsertSummaryTask` on row 4 | the same set of streams; `TBkndTask/Var2Data` and `TBkndTask/FixedData` differ in more bytes |
| `insert-milestone-first` | `InsertMilestoneTask` on row 1 | the same, plus `DocumentSummaryInformation` (8 bytes, not noise) |
| `insert-milestone-last` | `InsertMilestoneTask` on row 6 | the same as nested |

The log is `build/w35/g3-run1.log` (all four pairs `ok`, exit 0); the facts file
is `build/w35/W35-3-g3.json` (neither is committed).

## 2. What the edit does (decoded)

* The new row is appended to `TBkndTask/FixedData` with the next UniqueID (7
  here) and the cursor row's ID; every row from that ID onward gets ID + 1.  The
  row order in the file stays by UniqueID; the displayed order is the CEdl
  list.
* Insert Summary indents the selected row under the new summary: in the nested
  pair the selected row's outline word (row offset 172) goes 3 to 4 and its
  word at 140 changes, and the new row takes the old level (3).  Insert
  Milestone changes no existing row except the ID shift.
* `TBkndTask/Fixed2Data` and `Fixed2Meta` gain one 64-byte record and one
  32-byte entry; the new record's ordinal is the midpoint of its neighbours
  (the 2.5 rule of W30), and the row's creation stamp at +128 is written.
* `TBkndTask/FixedMeta` gains a 47-byte entry whose 12-byte flag mask
  differs by row kind: the summary bit (`b4 & 0x08`), the project-row bit
  (`b5 & 0x10`) and a milestone bit (about `b2 & 0x02`).  A scan of 1,326
  rows in 281 corpus and fixture files did not give a closed rule for the
  remaining mask bits.
* `TBkndAssn` gains a placeholder assignment in all six streams (110-byte
  FixedData row, 48-byte Fixed2Data record, a Var2Data block and VarMeta
  entry); the port has no writer for the placeholder row.
* `214/CEdl/Var2Data` shrinks by 8 to 12 bytes and differs in 165 to 171
  bytes, its size words following the stream length; `Props14` shrinks by 16
  bytes and differs in 133 bytes in each pair.  Both streams also carry
  per-save noise, so the effect bytes are in the anatomy reports.

## 3. Attempt and why it stops here

The only task-adding writer in the port is `mppwrite::add_task_row`
(`core/mppwrite/add_delete.h`): it appends one MANUAL task at the end of a
flat project and still differs from Project's save in 15 streams
(`kInsertGaps` in `tests/roundtrip_add_delete.cpp`).  For the pairs above it
would also need:

1. positional insert with the ID shift in every table that stores IDs;
2. an automatic-task row template (schedule words, flags word at +168, the
   calendar and anchor words), a summary template with the roll-up and the
   indent of the selected row, and a milestone template;
3. the FixedMeta mask bits for each kind (not closed by the corpus scan);
4. the placeholder assignment rows;
5. the CEdl and `Props14` rewrites (133 bytes each, tails included).

Items 3 and 5 are not decoded to a rule that predicts both held-out pairs
(`TBkndTask/FixedData` alone differs in 3, 15 and 1 bytes inside the common length for the nested, first-row and last-row milestone pairs, so the position changes the row bytes), so no
shape can be enabled that matches Project field by field.  Standing Rule 1
therefore keeps Insert Summary and Insert Milestone session-only: the shell
queues nothing for them, exactly as for Insert Task.

## 4. What would close it

Decode the FixedMeta mask and the `Props14` tail from a pair set that varies
one factor at a time (kind only; position only; level only), then build the
positional insert on the manual-task writer.  The four committed pairs are the
held-out checks.
