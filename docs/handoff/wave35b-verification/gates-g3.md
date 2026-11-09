# Wave 35b G3: records of ids fixed elsewhere in the wave

_Records only.  The code, tests and gates are in the commits cited; this file adds no
behaviour.  One id per commit._

## RW35-W4: outline persistence did not read the file's rows or levels

Row: `docs/handoff/wave35-verification/close-review-writers.md`, RW35-W4 (`save_view_outline`
took the uids and levels from the edit, not from the file, and read no view block).

Fixed in G2, before G3 began:

- `114861b` (W35b-1.1, R35G2-1): `save_view_outline` now reads the uid-2 view block, the
  applied view, the applied table and its column list, and requires the measured shapes (the
  Gantt Chart view, the Entry table with its eight fields, one of the two measured view-block
  digests with both sort-spec copies zeroed, the default sort spec in both copies).  That is the
  "digest pin on the view block" the row named as absent.
- `84a24d1` (W35b-1.2, R35G2-2): `save_view_outline` reads each row's stored OutlineLevel from
  the Task table and refuses a request whose level differs (needle "stores outline level X in
  the file, but the request describes level Y"); the task row count and UniqueID order must be
  the request's.  That is the "derive rows and levels from the file" direction of the row.
- `8d55b5c`, `961b311` and `d3a9fad` (W35b-3.1, 3.2, 3.4) added the edit-model and Qt-gate
  layers on top.

Left as the row's own text says: Show Level 1 and 2 only, on the two measured shapes
(`kBaseLevels` five rows, `kDeepLevels` six rows).  No new shape is measured in wave 35b.
(Corrected by W35c-2.23, R35bG3-4: this line, and the `90a3ca9` commit message, first said "Show
Level 1 to 3", copied from the review row.  The measured range is Level 1 and Level 2
(`core/mppwrite/viewstate_outline.h`: `kOutlineMeasuredMinLevel` = 1,
`kOutlineMeasuredMaxLevel` = 2; the W35-2.3 row in `docs/handoff/wave35-complete.md`; the Qt needle "the measured Project
pairs show Level 1 and Level 2").  The commit is not amended: no history rewrite.)

Status: fixed, with the commits above.  Nothing is deferred for this id.

## RW35-W5: sort persistence checked only that the order was a permutation

Row: `docs/handoff/wave35-verification/close-review-writers.md`, RW35-W5 (any permutation of the
rows was written the measured sort-spec bytes; Duration ascending was accepted with no pair
behind it).

Fixed in G2, before G3 began:

- `84a24d1` (W35b-1.2, R35G2-2): `save_view_sort` reads the task row count, the UniqueID order,
  the OutlineLevel, the names and the durations from the file, recomputes the sorted order under
  the pinned rules in one shared function (`sorted_order_of`, which the edit model's
  `sort_plan_order` also calls) and requires the request to equal it.  Needles: "the request is
  not the file's row order", "the sorted order does not match the order the file's own outline,
  names and durations give".  That is the "recompute the sorted order from the file's key
  column and refuse a mismatch" direction of the row.
- `7bd9c5c` (W35b-1.4, R35G2-4): Duration ascending is refused in the writer ("Duration
  ascending is unmeasured (the pairs sort Duration descending only)") and in the edit model, and
  an ascending key whose order differs from the rows' own is refused ("an ascending sort that
  moves rows is unmeasured").  That is the "refuse Duration until a pair measures it" direction.
  The review's suspicion that no Duration-ascending pair exists is confirmed by the pair list in
  the spec's capability matrix, which this commit extended.
- `d3a9fad` (W35b-3.4) made the Qt sort gate compare its save with Project's pair.

Still queued for the next measuring wave, not here: `sort-duration-asc` and an ascending-reorder
Sort pair (R35G2-4).

Status: fixed, with the commits above.  Nothing is deferred for this id.

## G3 summary

Every item of section 2, group G3, is done; none is deferred.  One commit per finding id, each
with its gate log under `build/w35b/` quoted in the commit message.

| Item | Ids | State |
|---|---|---|
| 3.1 | R35G2-5 | fixed |
| 3.2 | R35G2-6 | fixed |
| 3.3 | R35G2-7 | fixed |
| 3.4 | coordinator mutation M13 on G2 | fixed (d3a9fad) |
| 3.5 | R35G3-3 | fixed |
| 3.6 | R35G3-4 | fixed, each `unbold_cell` guard tested with its own needle |
| 3.7 | RW35-W4, W5 (records, fixed in G2), W6, W7, R3, R4 | fixed |
| 3.8 | R35G2-8, R35G3-5, R35G3-2, R35G3-7 | record corrections; the brief and review-g3 swap the ids R35G3-2 and R35G3-5, and the commits use the review ids |

The `guard_names=1` in every gate log is the two pre-existing `wave35-g3-notes-generator*.mpp`
blobs in `main..HEAD`, not a new finding.  `app/main.mm` was not touched; CI is its only compile
gate.  No push, no workflow dispatch, no amend, no history rewrite.

Queued for the measuring wave, not done here: Toolbar Underline (early-bound call), a two-row
Bold range that checks `ActiveSelection.Tasks.Count == 2`, the Applied-table writer attempt
(R35G2-8), an effort-driven pair (R35G0-5), and the pairs listed in the brief.
