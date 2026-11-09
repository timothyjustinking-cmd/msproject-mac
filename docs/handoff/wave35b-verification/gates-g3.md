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

Left as the row's own text says: Show Level 1 to 3 only, on the two measured shapes
(`kBaseLevels` five rows, `kDeepLevels` six rows).  No new shape is measured in wave 35b.

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
