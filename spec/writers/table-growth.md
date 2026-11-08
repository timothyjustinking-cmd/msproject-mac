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

### W34-3.1: the assignment ADD and REMOVE shapes, byte for byte

The two `wave30-assign-*` pairs fix every byte of the first-assignment ADD and
the REMOVE; `wave34-assign-add-second` and `wave34-assign-add-third` fix the
settled APPEND (a task that already holds one or two assignments).  The writer
`core/mppwrite/assignment_growth.cpp` encodes exactly these shapes and refuses
every other shape before it writes.  `n` is the old FixedData row count; the
target task's placeholder row is row `p`.

ADD (wave30-assign-add: every row is an unassigned placeholder; the target
task has exactly one, and every row's `+8` is `0xFFFF0001`):

- **Assn FixedData** 330 -> 440: append a byte copy of row `p` with `+0` =
  largest uid + 1 and `+8` = the resource uid.  Every existing row byte stays.
- **Assn FixedMeta** 118 -> 152: row `p`'s entry `+0 := 0x02`, `+2 := 0`
  (tombstone, offset and tail kept); append `{+0 0, +2 4, +4 n*110,
  +8..14 f1 ff 23 f0 69 00 02}`; header `+4 := 6`, `+8 := n+1`, `+12 :=
  (n+1)*110`.
- **Assn Fixed2Data** 144 -> 192: append `{fresh identity, the task row's
  Fixed2Data identity, the resource row's Fixed2Data identity}`.  Project
  renumbers every identity's first dword on save; the pair test normalises
  the three counter dwords of the appended record.
- **Assn Fixed2Meta** 175 -> 228: append `{+4 n*48, +8 0xcf, +9 0x09,
  +35 0x10, +52 0x28}`; header `+8 := n+1`, `+12 := (n+1)*48`.
- **Assn VarMeta** 132 -> 144: remove row `p`'s uid group (three entries:
  0x0F400031, 0x0F40027A, 0x0F400299) and append four entries for the new
  uid in ascending key order, blocks appended in Project's order: 0x0F40027A
  at `base`, 0x0F400031 at `base+8`, 0x0F400299 at `base+84`, 0x0F40029B at
  `base+104`.  Header `+8 += 1` (four added, three removed), `+0x14 :=
  base+124`.
- **Assn Var2Data** 396 -> 520: append the 0x0F40027A block whose 4-byte
  payload is the file's current session word (`114/Props` record 0x02400007,
  `12 2a 18 3d` in the control as well as the result), the 0x0F400031 block
  (72 B: header `[0x00200000][44][units*16][work][ticks8][0][1][0][0]` and
  one 28-byte record `[work][units*16][0][0][ticks8]`, work in 60000-per-hour
  units, ticks8 = work/100*8), then two 16-byte zero blocks (0x0F400299,
  0x0F40029B).  Nothing here is save noise.
- **Task FixedData**: project row and target task row `+8` and `+24 := work`.
  **Task FixedMeta** project entry (row+3) `+16 |= 0x10`, target entry
  `+16 |= 0x90`.  **Task Var2Data** 0x0B4004E5 leaf payload `[1][0xFFFF0001]`
  becomes `[1][resource uid]` (the 8-byte payload's second word).
- **Rsc FixedData**: the target resource row `+48`, `+90` and `+106 := work`.
  **Rsc FixedMeta**: row 0's entry (index 3) `+13 := 0x00`.
- **114/Props**: Assignment count `:=` FixedMeta header `+8` (3 -> 4),
  freed `+=` the removed group's payload bytes (100+4+16 = 120), dirty
  `:= 1`, size `:=` the new Var2Data size.
- **DocumentSummaryInformation**: the project row's total work 0 -> W moves
  the work strings `"0 hrs" -> "40 hrs"` (titles[3] and typed pid 7).

SETTLED APPEND (wave34-assign-add-second, wave34-assign-add-third: the task
already holds one or two real assignments plus exactly one free placeholder
row; nothing is tombstoned and every old row, record and var block stays):

- **Assn FixedData** +110: append a copy of a real row with `+0` = largest uid
  + 1 and `+8` = the resource uid.
- **Assn FixedMeta** +34: append `{+0 0, +2 4, +4 n*110, +8..14 f1 ff 23 f0
  69 00 02}`; header `+8 := n+1`, `+12 := (n+1)*110`; `+4` is untouched (the
  control's 6).
- **Assn Fixed2Data** +48: append `{fresh identity, the task row's
  Fixed2Data identity, the resource row's Fixed2Data identity}`.
- **Assn Fixed2Meta** +53: append `{+4 n*48, +8 0xcf, +9 0x09, +35 0x10,
  +52 0x28}`; header `+8 := n+1`, `+12 := (n+1)*48`.
- **Assn VarMeta** +48: every existing entry stays; append the new uid's four
  entries in ascending key order at `base+8`, `base`, `base+84` and
  `base+104`; header `+8 += 4`, `+0x14 := base+124`.
- **Assn Var2Data** +124: the same four blocks as the first-assignment add
  (session word, 72-B contour, two 16-byte zero blocks), byte-identical to
  the `wave30-assign-add` group apart from the session word.
- **Task FixedData**: project row and target task row `+8`/`+24 :=` the old
  total plus the new row's work.  **Task FixedMeta** is byte-identical (its
  flags are already 0x17/0x97).  **Task VarMeta**: the leaf entry's `+4 :=`
  the old Task Var2Data size; header `+0x14 :=` old size + 8 + 4 * new
  resource count.  **Task Var2Data**: append `[4+4*k][k][sorted resource
  uids]`; the old leaf record stays behind as a stale copy.
- **Rsc FixedData**: target row `+48`, `+90`, `+106 :=` the new row's work.
  **Rsc FixedMeta** is byte-identical.
- **114/Props**: count `:= n+1`, size `:=` the new Var2Data size, dirty
  `:= 0` when it was 1 (a control taken from an earlier edit save already
  holds 0), and the Task size scalar (`0x01000100`) `:=` the new Task
  Var2Data size (650 -> 666 -> 686).  The freed scalar stays 120.
- **DocumentSummaryInformation**: the total work strings move
  (`"40 hrs" -> "80 hrs"`, `"80 hrs" -> "120 hrs"`).

REMOVE (wave30-assign-delete: the settled shape -- every row but the last is
an unassigned placeholder, the last is the only real assignment, FixedMeta and
Fixed2Meta have `n-1` real entries and a byte-duplicate spare at the end):

- **Assn FixedData** 440 -> 550: append a copy of the real last row with `+0`
  = largest uid + 1, `+8 = 0xFFFF0001`; the deleted row's bytes stay.
- **Assn FixedMeta** 152 -> 152: the real row's entry `+0 := 0x02`,
  `+2 := 0`; the spare duplicate entry `+4 := n*110`, `+13 := 0x01`; header
  `+4 := 6`, `+8 := n`, `+12 := (n+1)*110` (the file grows by one row).
- **Assn Fixed2Data** 192 -> 240: append `{fresh identity, the task row's
  identity, the placeholder rows' null-resource identity}`.  **Task
  FixedMeta** target entry `+16 &= ~0x10` (0x97 -> 0x87); the project entry
  keeps 0x17.
- **Assn Fixed2Meta** 228 -> 228: the spare entry `+4 := n*48`, `+9 := 0x01`
  (0x09 -> 0x01), `+8 0xcf` and `+52 0x28` kept; header `+8 := n`, `+12 :=
  (n+1)*48` (the header's size word tracks the grown Fixed2Data; the
  Fixed2Meta stream itself does not grow).
- **Assn VarMeta** 144 -> 144: re-key the real row's group from its uid to
  the new uid and move all four block offsets `+124`; entry count stays, and
  the four entries keep the ascending key order (0x0F400031 at `base+8`,
  0x0F40027A at `base`, 0x0F400299 at `base+84`, 0x0F40029B at `base+104`).
  Header `+0x14 := old Var2Data size + 124`.
- **Assn Var2Data** 520 -> 644: append a copy of the real group's 124 bytes
  with its 28-byte contour record (group `+56`) ZEROED.  The block headers'
  units (`+24`), work (`+32`) and ticks8 (`+48`) bytes are kept; Project
  zeroes only the record of the assignment that is no longer live.
- **Task FixedData**: project row and target task row `+8`/`+24 := 0`.
  **Task Var2Data**: leaf payload `[1][resource uid]` -> `[1][0xFFFF0001]`.
- **Rsc FixedData**: target row `+48`, `+90`, `+106 := 0`.
- **114/Props**: count `:=` header `+8` (3 -> 4), freed `+=` the copied
  group's payload bytes (72+4+16+16 = 108), dirty `:= 1`, size `:=` 644.
- **DocumentSummaryInformation**: total work W -> 0, `"40 hrs" -> "0 hrs"`.


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
- `wave34-assign-add-second` (a second assignment on the same task, 1 -> 2)
  — Assn FixedData 440 → 550 (+110), FixedMeta +34, Fixed2Data +48,
  Fixed2Meta +53, VarMeta +48 (four entries), Var2Data 520 → 644 (+124);
  Task FixedData keeps its size with only the two work words changed; Task
  Var2Data 650 → 666 (+16, the appended two-resource leaf); the Props Task
  size scalar 650 → 666.  Both FixedMeta streams grow, so for the first
  time the settled append's Assn FixedMeta `+4` stays 6 while `+8`/`+12`
  move.
- `wave34-assign-add-third` (a third assignment, 2 -> 3) — Assn FixedData
  550 → 660 (+110), FixedMeta +34, Fixed2Data +48, Fixed2Meta +53, VarMeta
  +48, Var2Data 644 → 768 (+124); Task Var2Data 666 → 686 (+20); Props Task
  size 666 → 686.  The two pairs differ only in the old row/entry counts and
  the session word the control carries, which shows the append rule is
  independent of the count within the measured 1 → 2 → 3 range.
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

## W35-3.5: removing an assignment from a file the port itself saved

Question (wave-35 brief, G3 item 3.5): the writer's REMOVE needs the settled
shape that Project's own save leaves, and the plan-side check refuses a file the
port saved.  Does the port's removal match Project's when the control is the
port's own Assign Resources output?  Until it matches, it stays refused.

**Pair** (`scripts/oracle_w35_g3.py`, pair `assign-remove-port-saved`; fixtures
`tests/data/wave35-g3-assign-remove-port-saved{-control,-control-null,}.mpp`;
anatomy `evidence/wave35-g3-assign-remove-port-saved.md`).  Control = the port's
output of the wave-34 add (task 1, resource uid 2) on
`wave30-assign-add-control.mpp`, byte for byte; null = Project reopens and
resaves it; edit = Project deletes that assignment.  A first generation of the
edit used `neutralise()` in the edit session, which bumped the revision counter
one past the null's (byte 155 of SummaryInformation, 472 of 114/Props, 490 of
Task Var2Data).  The committed pair sets only `UserName`, as the null does, and
the three bytes agree.

**Measured.**

1. Project's null save SETTLES the port's file: `114/TBkndAssn/FixedMeta`
   (header +4 6 -> 4, +8 4 -> 3, the leading tombstoned entry dropped, entries
   re-pointed), `Fixed2Meta`, two flag bytes of `114/TBkndRsc/FixedMeta` and the
   114/Props scalars all change on a save that edits nothing.  The port's append
   shape is therefore not the shape Project resaves, and the writer's existing
   check ("the assignment FixedMeta is not the measured (rows - 1) / rows / flag 4
   settled shape") refuses the raw file.  The test pins that refusal and that the
   document is left unchanged.
2. Project's edit of the port's file produces the same Assn streams as its edit
   of the settled file (`wave30-assign-delete`): FixedData 440 -> 550, Var2Data
   520 -> 644, FixedMeta and VarMeta at the same offsets.
3. With the four streams Project's null rewrites (Assn FixedMeta and Fixed2Meta,
   Rsc FixedMeta, 114/Props) copied from the null into the control, the writer
   accepts the file.  Its output then matches Project's edit save in every
   declared stream except four, now named KnownGaps in
   `tests/roundtrip_assignment_growth.cpp` (`test_remove_port_saved`):
   - `114/TBkndAssn/Fixed2Data`: 6 bytes from 196; the appended record's identity
     counter, port 0xd1, Project 0xdb (Project's is 10 higher).
   - `114/TBkndAssn/Var2Data`: 3 bytes from 524, port 0x12, Project 0xba.
   - `114/TBkndRsc/Fixed2Meta`: byte 279, port 0x2b, Project 0x0b.  Project clears
     a flag bit that the port-saved file still carries; in the settled file the
     bit was already clear, which is why wave 30's pair never showed it.
   - `214/CEdl/Var2Data`: 64 bytes from 430; Project rebuilds the displayed-row
     list (586 -> 540 B).
   - `Props14` carries Project's edit marker; the harness's declared-effect check
     fails for this pair (effects = 0), so the test leaves it out of its declared
     list and does not vouch for it.

**Decision.** DEFERRED with evidence.  The removal stays refused for a file the
port saved (both writer and plan).  Enabling needs (a) the settle transform
derived as a rule rather than copied from a null (only this one pair measures
it), (b) the identity-counter rule behind the Fixed2Data and Var2Data bytes,
(c) the Rsc Fixed2Meta flag rule and (d) the CEdl rebuild; none can be derived
from one pair.  Pairs still needed: the same removal from a port-saved file with
a different row count (two and three existing assignments) and with the add made
on a different task, to separate the counter and flag rules from this file.
