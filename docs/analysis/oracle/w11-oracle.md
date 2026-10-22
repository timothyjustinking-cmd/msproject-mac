# Wave-11 oracle — linked exact-date pins and the task-delete cascade

2026-10-22, coordinator-only (COM). Tools: `scripts/oracle_w11.py`
(`linkeddate`, `linkdelete`); committed JSONs `w11-snet-link.json`,
`w11-linkdelete.json`; fixtures under git-ignored `corpus/probe/`.

## 1. A pin earlier than the dependency is clamped (measured)

`w11-snet-link.mpp`: P1 is a 5-day auto task Mon 2026-10-19 08:00 → Fri 10-23
17:00; S1 is a 1-day auto successor (FS, lag 0) at Mon 10-26 08:00. S1 was then
given a real SNET constraint at **Wed 2026-10-21 08:00** — two days *before* its
predecessor finishes — with `Manual=False`.

| when | S1 Start | S1 Finish | ConstraintType / Date |
|---|---|---|---|
| before the pin | 2026-10-26 08:00 | 10-26 17:00 | 0 / NA |
| after `CalculateProject` | **2026-10-26 08:00** | 10-26 17:00 | 4 / 2026-10-21 08:00 |
| reopened from disk | **2026-10-26 08:00** | 10-26 17:00 | 4 / 2026-10-21 08:00 |

So the inference in `save.h` / spec §10.8 is confirmed: **SNET is "no earlier
than"; a linked row reopens at `max(predecessor finish, pin)`.** A date edit
earlier than the dependency allows is stored faithfully (type 4 and the exact
date read back) but is not reflected in Start. A writer must warn when the pin
is before the earliest dependency-allowed start, or the app must place the edit
where it can take effect. Exact-date editing on a linked row is therefore not a
writer bug to fix; it is a scheduling rule to surface.

## 2. Deleting a task cascades by tombstoning the dependent rows (measured)

`w11-linkdelete-pre.mpp`: T1 → T2 → T3 (FS), plus one Work resource assigned to
T2. Delete T2 in Project, save, and reopen: Project reports 2 tasks, no link or
assignment naming the deleted uid. The file diff (all six table streams below;
`214/` churns as usual) shows the mechanism:

| stream | change |
|---|---|
| `TBkndCons/FixedMeta` | header `+4 4→6`; both link entries `+0 00→02`; size unchanged |
| `TBkndAssn/FixedMeta` | header `+4 4→6`; the T2/R1 entry `+0 00→02`, `+2 09→00` |
| `TBkndAssn/FixedData` | size unchanged; other rows' cached values shift with the recalculation |
| `TBkndAssn/VarMeta` / `Var2Data` | deleted row's entries/blocks adjusted; header count and sizes updated |
| `TBkndTask` | the task's own tombstone (+0=2, +2=0) and header +4=6 |

**Rule:** every MPP table uses the same tombstone-state contract — the deleted
row's `FixedMeta` entry `+0 = 0x02`, `+2 = 0`, and the table's `FixedMeta`
**header +4 |= 0x02**. A task delete must tombstone:

1. the task row (`TBkndTask`),
2. every `TBkndCons` row whose predecessor **or** successor is the deleted uid,
3. every `TBkndAssn` row whose task uid is the deleted uid,

and set the header bit in each of those tables. No stream shrinks. Project
itself then reopens with the dependent rows gone from its model. (A resource's
own rows are untouched; only assignments naming the deleted task go.)

T3's start moves to the project start after the delete because its only
predecessor was removed — that is Project's recalculation, not a writer
concern.

## 3. Consequences for wave-11 streams

* **W48** implements the cascade in `delete_task_row`: it must tombstone
  dependent link/assignment rows and set their tables' header +4 bit, instead of
  refusing by default. `DeleteOptions::allow_dangling` becomes unnecessary for
  this case; orphan rows can only remain when a *link endpoint* names a uid that
  is already dead in a damaged file.
* **W48** also documents the linked-pin clamp in `save.h` / spec §11 (the
  `w37-snet-link` open item is now closed by `w11-snet-link.mpp`).
* **The app** (a later wave) should warn when a Start edit is earlier than the
  dependency-allowed start, because Project will not show the pinned date.

## 4. What makes Project *display* a written custom field (measured)

A VarMeta value alone is not enough: Project ignored W48's first two attempts
(correct payloads, correct uid group, even byte-identical `TBkndTask/Props` and
`TBkndAssn/Props`). The missing pieces were found by having Project set the
same values on the **same base fixture** (`synthetic-base.mpp` ->
`corpus/probe/w11-fields-base.mpp`) and diffing:

1. **VarMeta entry position.** Text/Number/Duration/Date entries are inserted
   **immediately after the uid's Name entry** (`0x0b40000e`), sorted ascending
   by field id, *before* the standard overtime fields. A Duration custom field
   additionally occupies an internal entry **`0x0b4000b7`** (2-byte payload
   `07 00`) inserted after the last standard overtime entry (`0x0b4000aa`), not
   next to Duration itself.
2. **FixedMeta entry tail bitmap** (entry offsets, OR-ed per field):

   | field | bit |
   |---|---|
   | Text1 0x0b400033 | `+21` |= 0x20 |
   | Flag1 0x0b400048 | `+24` |= 0x02 |
   | Number1 0x0b400057 | `+25` |= 0x08 |
   | Number2 0x0b400058 | `+25` |= 0x10 |
   | Duration1 0x0b400067 | `+26` |= 0x01, `+27` |= 0x20 |
   | Date1 0x0b400109 | `+29` |= 0x40 |

   and the entry's `+2` u16 becomes the uid's VarMeta entry count.
3. **Per-table registries.** `114/TBkndTask/Props` and `114/TBkndAssn/Props`
   each gain one 88-byte record per used field (Project's own streams are 614
   and 616 B for the six fields; the preamble carries a record count and a
   byte count). W48 reproduced both streams byte-identically from the measured
   templates.

Flag1 was visible before the rest because it is a FixedMeta tail bit, not a
VarMeta value. Single-field probes establish the bitmap; the six-field file is
the OR of them. The payload `07 00` for `0x0b4000b7` is measured for the
Duration1 case only; it is written as-is.

