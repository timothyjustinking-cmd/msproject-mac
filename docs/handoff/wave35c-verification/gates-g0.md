# Wave 35c, group G0: builder record

Branch `wave35c`. Three items, in order, one finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35c)` (checked by `build/w35c/commit.sh`
before the commit and counted after). No writer added, no refusal lifted, no pair measured, no
COM, no push, no workflow dispatch, no history rewrite.

Gate tooling: `build/w35c/gates.sh <label> [qt]` (Visual Studio 17 2022, Release,
`CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`, `-app`, `-qt`). Logs are
`build/w35c/<parent-sha>-W35c-<label>-*.log` and `*-gates.status` (git-ignored `build/`).
Scans each time: `check_fixtures.py`, `check_markers.py --range main..HEAD`, `guard_names.py`,
`scan_client_names.py`, and `git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 0.1 | R35bW-CI1 | `9a2c079` | fixed in source; the GCC 13 compile is CI's (MSVC does not report `-Wdangling-reference`) |
| 0.2 | R35bG0-1 (High) | `7d923dc` | fixed, both layers |
| 0.3 | R35bG0-2 (High) | `743ad8b` | fixed in the writer; the plan layer is writer-only by a stated design (see below) |

## Gate lines

- 0.1 (`2ceb810-W35c-0.1-*`): core build 0, core ctest `-E plan_recalc` 65/65, app build 0,
  app ctest 34/34, fixtures 0, markers 0, guard_names 0, scan_client_names 0, client-string grep
  none. Core build 0 warnings.
- 0.2 (`9a2c079-W35c-0.2-*`): core 65/65, app 34/34, app_qt 52/52, 0 warnings, scans 0.
- 0.3 (`7d923dc-W35c-0.3-*`): core build 0 / 0 warnings, ctest 65/65, app 34/34, app_qt 52/52,
  fixtures 0, markers 0, guard_names 0, scan_client_names 0, client-string grep none;
  `scripts/gen_calculate_props_pin.py --check`: up to date.

The measured pair still writes and matches its pair with effects on in every row
(`mppwrite_roundtrip_calculate` `unlinked_pair`, `app_edits_calculate`).

## 0.1 (R35bW-CI1)

`unique_edl_item` in `core/mppwrite/viewstate_outline.cpp` took `std::string const& what`
and was called with literals, the pattern GCC 13 reports as `-Werror=dangling-reference`. It
takes `char const* what` now. A sweep of `core/` for a function returning a reference while a
temporary can bind to a reference parameter found no other (`require_prop` and `shape_for`
checked). No red locally: MSVC cannot report the warning and there is no GCC on this machine,
so the gate is CI (`ubuntu-gcc`, `linux-qt`), run by the coordinator after CLOSE.

## 0.2 (R35bG0-1)

- Writer: `calculate_measured_first_working_start` and a check in `read_calculate_shape`
  that each leaf Start is the first working instant at or after Props `0x02400002` on the
  measured Standard calendar. It runs after the 0.4 finish check so the earlier guards keep
  their own tests.
- Reader: `ProjectRow::props_start(_stored)`; plan: `PlanInput::props_project_start(_stored)`,
  filled by `reader_adapter.h`; `calculate_plan_refusal` repeats the relation and refuses a
  plan whose Props start was not read.
- Needle in both layers: "project start in 114/Props".
- Red (`0.2-red-*.log`, guards off with a non-constant false): the writer saved the moved-start
  file ("expected a refusal, none thrown") and `app_edits_calculate` failed 4 checks. Green:
  the control with ONLY Props `0x02400002` moved a week and a day is refused with the needle;
  the date table passes; `app_edits_calculate` loads the moved file through `load_plan` and
  asserts the plan-layer needle, the writer-layer needle and `push()`.

## 0.3 (R35bG0-2)

- Writer: `check_props_pin` pins `114/Props` to the control: header words, 161 framed records
  in order (id, kind, length, FNV-1a 64 of the payload) and the 85,408-byte tail. Payload hash
  exempt, by id: `0x02400003`, `0x02400024` (written by the save), `0x02400002` (the 0.2
  relation), `0x02400029` and `0x024013c2` (the only records the control's null save changes,
  checked by the generator). Tail: the Task Var2Data size scalar (tag `0x01000100`) excluded.
  Needles: "114/Props record 0x........ differs from the measured control" and "114/Props tail
  differs from the measured control".
- `scripts/gen_calculate_props_pin.py` writes `core/mppwrite/calculate_props_pin.inc` from
  the control; `--check` fails when stale.
- Red (`0.3-red-core-*.log`, pin call off with a non-constant false): 159 failures in
  `mppwrite_roundtrip_calculate`: 156 flipped payload bytes (one per pinned record), a changed
  record kind, a changed tail byte, all "expected a refusal, none thrown", and "request refusals
  leave no file" because the files were saved. Green (`0.3-green-core-*.log`,
  `7d923dc-W35c-0.3-*`): passes; a changed GUID record and a changed Task size scalar are
  accepted.
- Plan layer: writer-only, stated in `spec/writers/calculate.md` section 8. `PlanInput` carries
  no Props records and the pin is reached by construction: `make_calculate_edit` runs
  `read_calculate_shape` on the file and the save compares `source_digest`. A hand-built edit
  is refused at the save, not at `push()`.

## Guards seen red

- 0.2: the leaf-start-to-Props relation, writer and plan layers (4 plan-layer checks and the
  writer check).
- 0.3: the Props pin, 159 checks.
- 0.1: none. CI is the gate.

## Deferred

Nothing.

## Found, not fixed

- **The Props pin rests on one control and one null save.** The two exempt GUID records are
  the only ones that differ between them. A file saved on another machine or by another
  Project build may carry further per-file values in `114/Props`; it would be refused with
  "differs from the measured control" until a second null-save pair shows them as noise. That
  is a refusal, not a wrong write. Not measured (no COM this wave).  Correction (W35d-1.4,
  R35cG0-3): a second null save cannot show them, because the per-file records do not change on
  a resave, and the cause is not only another machine or build: every project has its own title
  and creation stamp.  Calculate Project is enabled for the measured control's values only; see
  `spec/writers/calculate.md`, "Scope".
- **`push()` does not see the Props pin** for a hand-built calculate edit (writer-only, above).
  A hand-built edit cannot be saved over a file whose options differ, but the refusal arrives
  at the save. Bears on R35bG0-5 (group G1, item 1.7: the plan layer matching the writer).
- **A project start inside the working day** (for example Monday 10:30) is refused by 0.2: it
  is its own first working instant and no leaf start equals it. Project's bytes for that and
  for any moved start are unmeasured.
- 0.1: no GCC here. The compile is unconfirmed until CI.
