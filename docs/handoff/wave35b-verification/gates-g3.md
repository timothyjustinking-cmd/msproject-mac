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
