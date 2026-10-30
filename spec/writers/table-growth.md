# Table growth with re-keying

_W34-0.2 decode note, 30 October 2026. Companion to the pair reports under
[`evidence/`](evidence/). Static evidence is from the git-ignored Ghidra
model. No dynamic trace this wave (W34-0.4 decision)._

## Rule

A `TBknd*` container is a four-stream table (`FixedMeta`, `FixedData`,
`VarMeta`, `Var2Data`) plus, for most classes, a second outline pair
(`Fixed2Meta`, `Fixed2Data`). The fixed streams are written whole at the new
row count: every measured growth moves each fixed stream by exactly one stride
per row and rewrites the meta offset words. The var streams are NOT rebuilt
compactly — new and changed blocks are appended and old bytes stay orphaned
(see [`var-meta.md`](var-meta.md)), so the measured save is an in-place update.
The storage the save writes into is built by `FUN_1408ad2d4` with the streams
opened in the order FixedMeta, VarMeta, FixedData, Var1Data, Var2Data, CopyUid,
Fixed2Meta, Fixed2Data (`FUN_1403782a4`).

Sizes follow one slot-based rule. Counting the project row in the Task table
(uid 0), and writing `n` for the live slot count:

| table | FixedData | FixedMeta | Fixed2Data | Fixed2Meta |
|---|---|---|---|---|
| TBkndTask | 48 + 202·n | 16 + 47·(n+3) | 64·(n+3) | 16 + 96·(n+3) |
| TBkndRsc | 48 + 172·n | 16 + 37·(n+3) | measured 120 + 40·n | 16 + 51·(n+3) |
| TBkndAssn | 110·n | 16 + 34·n | 48·n | 16 + 53·n |
| TBkndCal | 64 + 12·n | 16 + 10·(n+4) | 48·(n+4) | 16 + 10·(n+4) |
| TBkndCons | 20·n | 16 + 10·n | — | — |

The `+3` / `+4` slack is Project's spare-slot allowance: `FixedMeta` and the
outline pair carry that many extra entries beyond the live rows. The Task
table's own row (uid 0, the project row) counts as a row in all four Task
formulas. The Assignment table has no slack.

Growth and deletion:

- **Insert** appends new slot(s) at the end of FixedData/Fixed2Data, appends
  the corresponding FixedMeta/Fixed2Meta entries, appends the new var blocks
  at the end of Var2Data and adds their entries at the sorted `(uid, key)`
  positions; existing block offsets do not move (the measured insert pairs have
  no later block). It also renumbers the in-place `ID` word of existing rows
  where Project's ID order requires it — a row's stream position is not its
  ID.
- **Delete** does not shrink the table. Project leaves the deleted row's bytes
  in place and appends a tombstone/placeholder row instead (Assignment:
  +110 B, resource word `0xFFFF0001`), so FixedData grows on a delete.
- A row's `FixedMeta` entry carries its offset into FixedData (measured:
  a new calendar entry at row offset 100 = 0x64; a new Fixed2Meta entry at
  Fixed2Data offset 336 = 0x150), so a growth that moves a row rewrites its
  entry too.

The port must reproduce the whole updated stream set, not just the new row:
every row's FixedMeta entry may be rewritten, new var blocks append at the
current end, and the size words of all six streams move.

## Evidence

### Measured (pairs)

- `wave30-insert-milestone` and `wave30-insert-summary` (one row inserted,
  `Application.InsertMilestoneTask()` / `InsertSummaryTask()`) —
  Task FixedData 856 → 1058 (+202), FixedMeta 345 → 392 (+47), Fixed2Data
  448 → 512 (+64), Fixed2Meta 688 → 784 (+96), VarMeta 528 → 648 (+120,
  10 entries), Var2Data 518 → 666 (+148). Because the fixture has three task
  rows plus the project row (`n` = 4 → 5), every delta is exactly one stride
  in the table above.
  A placeholder assignment is appended at the same time: Assn FixedData
  330 → 440 (+110), FixedMeta 118 → 152 (+34), Fixed2Data 144 → 192 (+48),
  Fixed2Meta 175 → 228 (+53), VarMeta 132 → 180 (+48, four entries),
  Var2Data 396 → 560 (+164).
  The new task row is uid 4 / ID 2 and is appended at the end of FixedData;
  the old ID 2/3 rows are renumbered to 3/4 in place.
- `wave30-assign-add` (one assignment added) — Assn FixedData 330 → 440
  (+110), FixedMeta +34, Fixed2Data +48, Fixed2Meta +53, VarMeta +12 (one
  entry), Var2Data +124. Task FixedData keeps its size and shows 16 effect
  bytes (the assigned task's rollup).
- `wave30-assign-delete` (the same assignment removed) — Assn FixedData
  **grows** 440 → 550 by an appended 110-byte placeholder row (UniqueID 5,
  task UniqueID 1, resource word `0xFFFF0001`) while the deleted row's bytes
  stay in place; FixedMeta stays 152 B with 8 effect bytes;
  Fixed2Data 192 → 240 (+48); VarMeta stays 144 B; Var2Data 520 → 644
  (+124). This is the deletion rule in one measurement.
- `wave30-resource-add` — Rsc FixedData 392 → 564 (+172), FixedMeta
  201 → 238 (+37), Fixed2Data 200 → 240 (+40), Fixed2Meta 271 → 322 (+51),
  VarMeta 108 → 168 (+60, two entries), Var2Data 86 → 140 (+54); the same
  save also adds a calendar slot (see
  [`calendar-tbkdcal.md`](calendar-tbkdcal.md)).
- `wave30-baseline` — Cal FixedData 100 → 112 (+12), FixedMeta 86 → 96
  (+10), Fixed2Data 336 → 384 (+48), Fixed2Meta 86 → 96 (+10), VarMeta
  36 → 48 (one entry), Var2Data 22 → 122 (+100).
- `wave30-calculate` — no size changes in any table; FixedData/Fixed2Data
  change content only (41 effect bytes in Task FixedData, 13 in Fixed2Data),
  while VarMeta grows 252 → 360 B and Var2Data 264 → 372 B. A recalculation
  adds var data but no rows.
- Every pair: control-vs-null sizes are identical in all six streams of
  every table. A no-edit save never grows or shrinks a table.

### Static (Ghidra model)

- `FUN_1408b05ac` @ `0x1408b05ac` (3,961 B), called from `FUN_1408ad2d4` —
  writes the full FixedData row block (`row_size × rows`, row size at struct
  +0x26c), then the Fixed2Data block (stride at +0x26e), then a compacted
  VarMeta/Var2Data rewrite in entry order (see [`var-meta.md`](var-meta.md)).
  The fixed-stream half matches the measured strides; the compacting var half
  does not match any measured editor save, so the editor path does not use
  this loop end to end.
- `FUN_1408ad2d4` @ `0x1408ad2d4` (1,692 B) — builds the scratch storage,
  creates VarMeta, FixedMeta, FixedData, Var2Data, Fixed2Meta and Fixed2Data,
  binds them to the table object, and validates the freshly written headers
  by reading them back. `FUN_1403782a4` @ `0x1403782a4` opens the eight
  streams of a container in its fixed order.
- `FUN_1403761a8` @ `0x1403761a8` (588 B) — reads the FixedMeta 16-byte
  header (magic `0xFADFADBA`, count word at +8) and stores the count, the
  read-side counterpart of the geometry above.
- `FUN_1408ad0b4` @ `0x1408ad0b4` — writes the magic and version word 4 into
  both meta headers, showing the headers are emitted from constants, not
  copied.
- `core/mpp/reader.cpp` lines 255–290 are the port's independent statement of
  the same geometry; the table above is that statement checked against the
  measured deltas. The Task row's project-row accounting (`n` includes uid 0)
  is what makes 856 = 48 + 202·4 and 688 = 16 + 96·7 agree.
- `FUN_14043e960` @ `0x14043e960` — the container-set diff: it creates new
  containers in the serializer and deletes ones no longer present, which is
  how a table class with no rows is represented (a header-only stream set).

### COM facts

All insert/delete pairs are single editor commands through
`scripts/oracle_lock.py` (`Assignments.Add`, `Assignments.Delete`,
`InsertMilestoneTask`, `InsertSummaryTask`, `Resources.Add`, baseline set).
The fixtures are the same session's before/after saves; no COM call touched a
table stream directly.

### W34-0.5: the Insert Task and Delete Task pairs

The synthetic pairs ([insert](evidence/wave34-insert-task.md),
[delete](evidence/wave34-delete-task.md)) measure two task-level shapes the
corpus never covered:

- **Insert Task** (`Tasks.Add`): the Task table appends one 202-byte row
  (FixedData 856 -> 1058 with 202 effect bytes, FixedMeta 345 -> 392, VarMeta
  528 -> 672, Var2Data 530 -> 676) AND Project appends the new task's
  PLACEHOLDER assignment row (Assn FixedData 330 -> 440, FixedMeta 118 -> 152,
  VarMeta 132 -> 168, Var2Data 396 -> 500, Fixed2Data 144 -> 192, Fixed2Meta
  175 -> 228), even though the reopened task reports no assignments. COM shows
  one 480-minute manual task at uid 4, outline level 1, no links.
- **Delete Task** (`Tasks(2).Delete()`): the row is TOMBSTONED, not removed --
  Task FixedData stays 856 B with a 1-byte effect, FixedMeta 3 bytes, VarMeta
  26 bytes in an unchanged stream, Var2Data 3 bytes; the assignment table
  loses nothing but its FixedMeta (3) and VarMeta (7) carry the cascade.

This answers the "delete a task: tombstone or remove the slot?" question
below: tombstone, with the deleted row's VarMeta entries removed logically.
The port's `add_task_row` clones the last live row and writes no placeholder
assignment; `delete_task_row` shrinks VarMeta physically. Both are recorded
as named expected failures in `tests/roundtrip_add_delete.cpp`.

## Limits

- The in-memory row insert/delete and ID-renumber routines were not isolated
  in the static model; the append/tombstone rule is measured. In particular,
  why an inserted task's placeholder assignment is live for a milestone but
  tombstoned for a summary is documented in `tests/data/README.md` as a
  measured difference, not decoded.
- Resource Fixed2Data `120 + 40·n` fits the two measured points (2 → 200,
  3 → 240) but the reader does not parse that stream; it is an inference.
- Deletion is measured for an assignment and now for a TASK (W34-0.5: the
  tombstone shape above). Resource and calendar deletion shapes are still
  unmeasured.
- The `+3` / `+4` slack is an occupancy rule inferred from sizes; no pair
  fills or empties the slack.
- Multi-row inserts in one save are unmeasured.

## Pairs still needed

1. **Insert two rows in one save** (two `InsertMilestoneTask` calls before
   saving): confirms consecutive uid allocation and that every delta doubles.
2. ~~**Delete a task**~~ measured by W34-0.5 (the delete pair): the row is
   tombstoned in place, the dead row's VarMeta entries are removed logically
   and no placeholder row is appended. Still open: delete a RESOURCE or a
   CALENDAR row (the assignment and task shapes are measured).
3. **A change on a non-first table** (e.g. a Resource column edit while the
   Task table is the active table): the R33b-1 refusal expects the column
   change to alter only the first table; this pair would confirm the rule the
   refusal protects.

No dynamic trace this wave (W34-0.4 decision). The open rules are all
pair-measurable (two inserts in one save, a task delete, a change on a
non-first table); the static dispatch already names the container writers, so
a trace would not decide a shape the pairs cannot.
