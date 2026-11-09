# Wave 35d, group G0: builder record

Branch `wave35d`. Five items, in order, one finding id per commit, each with the trailer
`Generated-By: Claude Sonnet 5.5 via Claude Code (wave 35d)` (checked by `build/w35d/commit.sh`
before the commit and counted after). No writer added, no refusal lifted, no pair measured, no
COM, no push, no workflow dispatch, no history rewrite.

Gate tooling: `build/w35d/gates.sh <label> [qt]` (Visual Studio 17 2022, Release,
`CORE_WARNINGS_AS_ERRORS=ON`; trees `build/w35b-close-fresh`, `-app`, `-qt`). Logs are
`build/w35d/<parent-sha>-W35d-<label>-*.log` and `*-gates.status` (git-ignored `build/`).
Scans each time: `check_fixtures.py`, `check_markers.py --range main..HEAD`, `guard_names.py`,
`scan_client_names.py`, and `git grep "client-string-[0-9a-f]{8}"` (none).

| Item | Finding | Commit | Status |
|---|---|---|---|
| 0.1 | R35cW-CI1 | `23de6e2` | fixed in source; the GCC compile is CI's (MSVC does not report `-Wmissing-field-initializers`) |
| 0.2 | R35cW-CI2 | `171831b` | fixed in source; macos-qt is CI's (macOS cannot be run here) |
| 0.3 | R35cG0-1 (High) | `abc8a60` | fixed: three tests, each branch red when switched off |
| 0.4 | R35cG1-1 (High) | `61a451b` | fixed, both halves; Show Level checked and does not reproduce |
| 0.5 | R35cG1-2 (High) | `6458203` | fixed |

## Gate lines

- 0.1 (`e17ddb7-W35d-0.1-*`): core build 0 / 0 warnings, core ctest `-E plan_recalc` 65/65, app
  build 0 / 0 warnings, app 34/34, fixtures 0, markers 0, guard_names 0, scan_client_names 0,
  client-string grep none.
- 0.2 (`23de6e2-W35d-0.2-*`): the same, plus app_qt build 0 / 0 warnings and app_qt 52/52
  (`qt_backstage_gate` passed).
- 0.3 (`171831b-W35d-0.3-*`): core 65/65, app 34/34, scans 0, 0 warnings.
- 0.4 (`abc8a60-W35d-0.4-*`): core 65/65, app 34/34, app_qt 52/52, scans 0, 0 warnings.
- 0.5 (`61a451b-W35d-0.5-*`): core 65/65, app 34/34, app_qt 52/52, scans 0, 0 warnings.

The measured pairs still write and match with effects on in every row (`mppwrite_roundtrip_calculate`,
`app_edits_calculate`, and the `qt_sort_persist` and `qt_outline_persist` save-and-compare blocks,
all inside the suites above).

## 0.1 (R35cW-CI1)

`app/app_edits_assignment_tests.cpp` built the default resource row with
`app::ResourceRow{.uid = row.uid, .id = row.id}`, a designated initializer naming two of the
struct's members: `-Werror=missing-field-initializers` on GCC. It is built from the defaults now
and `uid` and `id` are assigned afterwards. Sweep: `git grep` for a designated initializer in
every tracked `.cpp`, `.h` and `.mm` (inline and line-leading forms) found no other, in `app/`,
`app_qt/`, `tests/` or `core/`. No red locally; CI (ubuntu-gcc) is the gate.

## 0.2 (R35cW-CI2)

The About case of `qt_backstage_gate` is restored to the QTimer top-level widget walk it had
before `c235973` (W35c-2.18), with the macOS comment kept (`activeModalWidget()` is null for this
box on the macOS offscreen run; qt-shell 40054734014 and 40055403392; identify the box by its
text). The comment is quoted in the commit. The New-from-existing and Options cases stay on
`gate::PromptAnswerer`. Both forms pass on Windows, so there is no local red; macos-qt (expect
50/50) is the gate.

## 0.3 (R35cG0-1)

Three cases in `tests/roundtrip_calculate.cpp`, each with a needle only its own branch produces:
- header words +8 and +12: "114/Props header differs from the measured control";
- the last framed record cut out: "114/Props holds 160 records, the measured control holds 161";
- one 2-byte record appended: "is past the 161 records of the measured control".

Red, each branch switched off with the source restored after (`build/w35d/0.3-red-*-run.log`),
`mppwrite_roundtrip_calculate` ctest=8:
- header off: `+12` "expected a refusal, none thrown", and `+8` refused by a later guard ("props
  edit: 114/Props format word is not 0x00010000") with another text: 2 FAILs;
- record-count off: the removed-record file is refused by the later size-words guard ("props
  edit: 114/Props size words ... not stream size - 4"), so the needle FAILs;
- past-the-table turned into a break: refused by the tail-length guard ("114/Props tail is 85422
  bytes, the control's is 85408"), so the needle FAILs.

So a later guard catches each shape with a different text; no file passes. The three branches are
the sole source of their own refusal text, which is what the coordinator's mutation P3 (the
record-count check off) now turns red through. Green: `171831b-W35d-0.3-*`.

## 0.4 (R35cG1-1)

`app_qt/main_window.cpp`, `app_qt/plan_document.h`:
- `apply_sort_choice`, `apply_session_sort` and `clear_session_sort` end the structural redo list
  when the edit a Redo would re-apply is a `sort_view` (`redo_top_is` over
  `StructuralEdits::next_redo`), whether or not a Sort is queued;
- `after_structural_change` re-applies a queued Sort to the display (the session sort is set to the
  edit's key and direction when it is not the one shown), as it does a queued Show Level;
  `PlanDocument::sort_definition()` is the new read-only accessor.

Show Level: `apply_outline_level` has the same `had_queued_level` shape. The route Level 1,
Ctrl+Z, All Subtasks or Level 3, Ctrl+Y was run: it does not diverge, because Redo re-queues
Level 1 and the rebuild re-shows it, so the rows match the queued level. No code changed for it;
`qt_outline_persist` pins the invariant (a re-queued level shows its rows).

Tests: `qt_sort_persist` (Undo-first: a refused Start sort, a cleared Sort, clearing the session
sort, and the plain Undo then Redo that must still re-queue with its rows) and
`qt_outline_persist`. Red with `main_window.cpp` and `plan_document.h` at the parent
(`0.4-red-run.log`): 5 FAILs in `qt_sort_persist`, `qt_outline_persist` green. With only the three
clears off and the re-apply on (`0.4-red-noclear-run.log`): the same five and two more, because
the re-apply moves the display to the queued sort. The re-apply has no route of its own once the
clears are in; it is defence in depth, shown only by that run.

## 0.5 (R35cG1-2)

`cut_selection` clears the structural redo list after its transaction applies, `paste_selection`
when it changed at least one cell. The other `after_cell_change()` callers are the sheet's own
Undo and Redo (must not clear) and Task Information (already clears). Tests in `qt_sort_persist` and
`qt_outline_persist`: Sort or Level 1, Ctrl+Z, a Cut or a Copy-then-Paste of a Name cell, Ctrl+Y;
the status line proves the edit applied, Redo is disabled, nothing is re-queued, the names are as
the edit left them. Red with the two `clear_redo()` calls removed (`0.5-red-run.log`): 11 FAILs
(5 Sort, 6 Show Level). Green: `61a451b-W35d-0.5-*`.

## Guards seen red

- 0.3: the three `check_props_pin` branches (needles; see above).
- 0.4: the redo clear on a refused, cleared and session-cleared Sort (5 FAILs); the re-apply by the
  second run (2 more).
- 0.5: Cut and Paste ending the redo list (11 FAILs).
- 0.1 and 0.2: none locally; CI is the gate.

## Deferred

Nothing.

## Found, not fixed

- The Show Level route of 0.4 does not diverge, so Redo of a withdrawn level after an intervening
  All Subtasks or session level still re-queues the level (and shows it). That is Redo as the shell
  has always behaved for it, not an R35bW-2 case; a decision for the coordinator if it should end
  the list like a Sort does.
- The queued-Sort re-apply in `after_structural_change` is unreachable from the public routes while
  the three clears stand, so only a mutation run exercises it.
- A Paste that changes no cell leaves the redo list alone by design (no plan change); a Cut of
  already-empty cells clears it, since its transaction is recorded.
- 0.1 and 0.2 are unconfirmed until CI compiles them with GCC and runs macOS.
