# Wave 27 - the completion record

One table row per brief item.  A group ends only when its exact
`WAVE-27-<group>-COMPLETE` line is appended below (wave-27-brief section 1).
Deferrals need the attempt evidence the standing rules and the brief name.

## G0 - the wave-26 Highs (save safety)

All three items are DONE; nothing in G0 is deferred.  Each item's fixture
commit precedes its code commit under the same finding id.

| item | finding | status | evidence |
|---|---|---|---|
| W27-0.1 | High: % Complete saves one word of the eight-plus copies Project writes (`corpus/probe/var-pct.mpp`) | **DONE** | fixtures `d30c2e8`, code `0961f19`; gates `build/w27/gates-d30c2e8-W27-0.1.log`, fixtures `build/w27/gates-fixtures-W27-0.1.log` |
| W27-0.2 | High: % Complete accepted on summary tasks; the refusal lived only in the shell | **DONE** | commit `29f6827`; gates `build/w27/gates-0961f19-W27-0.2.log` |
| W27-0.3 | Medium: Project Information Priority save unmeasured | **DONE (KEPT)** | fixtures `a40ef6d`, code `5a991e9`; gates `build/w27/gates-a40ef6d-W27-0.3.log`, fixtures `build/w27/gates-fixtures-W27-0.3.log` |

W27-0.1 detail:

- `scripts/oracle_w27.py` (committed) opened `tests/data/synthetic-base.mpp` on
  a copy under `C:/w27`, set Task ID 1 `PercentComplete = 50` through Project
  2021 COM, neutralised UserName/Author/Manager/Company/Title and saved
  `tests/data/wave27-percent-actuals.mpp` (239,616 bytes, SHA-256
  `4C9AECBF...CC7AA2`).  A byte-by-byte Task FixedData diff against
  synthetic-base shows the task row changing +80/+81 ActualDuration,
  +88/+89 RemainingDuration, +92 PercentComplete, +120..123 ActualStart,
  +132..139 the two date words, +169 (the +168 flag word) and Var2Data key
  0x0B40015E, and the project row changing +80/+81, +88/+89, +92, +120..123 and
  +132..139.  Project writes eight-plus copies; the port wrote +92 only.
- Because the port has no measured actuals writer, the five Task > Schedule %
  buttons are DISABLED with the exact decision-9 tooltip ("not implemented in
  this port"): the pin (`app_qt/ribbon_pin.tsv`), the live ribbon spec, the
  enabled-wiring list and the live gate all agree.  `MainWindow::
  set_percent_complete` is removed and the five ribbon cases collapse to an
  unreachable return, so no UI route can queue `StructuralKind::set_percent`.
- The edit kind, its save path and its tests are kept and marked NOT ENABLED,
  ready for the wave-28 actuals writer that re-enables the buttons.
- The Move Project gate can no longer create actuals by clicking 50 %; it opens
  the Project-saved actuals fixture, checks the prompt text/buttons and asserts
  answering Yes queues no edit (app_qt 23/23).

W27-0.2 detail:

- The leaf/summary rule is now in the edit model.  `app::make_percent_edit(plan,
  uid, percent, out, error)` refuses uid 0 (project summary), an unknown uid (a
  pending insert has no saved row), a value above 100 and any `PlanInput`
  TaskRow whose `summary` is set, naming the reason in `error`.
- `StructuralEdits::push(edit, const PlanInput* = nullptr)` REQUIRES the plan
  for a `set_percent` edit and refuses a plan-less one rather than trusting it;
  the validated edit replaces the caller's.  The other kinds ignore the new
  argument.
- The new app test covers every refusal, a hand-built summary edit pushed with
  the plan, a plan-less push, leaf acceptance and the 50 % display value; the
  two real-save pushes pass the loaded plan.

W27-0.3 detail:

- The parent pair `tests/data/wave27-priority-control.mpp` /
  `wave27-priority-750.mpp` is ONE Project 2021 session's saves of synthetic-base
  at Priority 500 and after `ProjectSummaryTask.Priority = 750`, with neutral
  metadata.  The pair's Task FixedData differs in exactly row 0's +78/+79 word
  (f4 01 -> ee 02, 500 -> 750); every other stream difference is the per-save
  bookkeeping the wave-24 same-session pair also shows (114/Props GUID tail,
  the Fixed2Data GUIDs, the edit list, cached view state).  Project's stored
  edit is therefore the one +78 word, which is what `set_priority` writes, so
  the save is KEPT, not disabled.
- `app/project_dialogs_tests.cpp` now saves Priority 750 FROM the control
  fixture through the shared structural cascade and asserts, through
  `mpp::CompoundFile` rather than the port reader, that the saved
  `114/TBkndTask/FixedData` stream is byte-identical to Project's 750 fixture.
  It also pins the pair itself (only +78/+79, 0x01F4 -> 0x02EE) and keeps the
  round-trip checks.  This is the field-by-field fixture comparison standing
  rule 1 requires.

Gates on the group's final commit `5a991e9`, fresh trees configured from scratch
and built serially under `build/w27-fresh{,-app,-qt}` (log
`build/w27/g0-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- `engine_diff.py --selftest` 53 checks, 0 failures; `--suite
  --expect-fail constraint-mso` 7 pass + the one expected failure, 0 unrecorded
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare` 332/332
  (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23
- `check_fixtures.py` OK (28 documented hashes, 24 tracked `.mpp`), oracle
  provenance OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0 on
  `c632c8f..HEAD`; client-string hex grep no matches

## Found, not fixed

- `app/edit_model.cpp` is wholly CRLF in the index (1,093 lines) while
  `app/edit_model.h` and the surrounding app sources are LF; the wave-26 review's
  W27-3.6 names two other wholly-CRLF files and does not name this one.
  Converting it under W27-0.2 would have put a whole-file EOL diff under a
  two-function finding, so it is left for the G3 EOL item.  Everything G0 added
  or created elsewhere is LF.
- Project changes per-save GUIDs and edit-list/view-state bytes on EVERY save
  (the wave-24 same-session pair proves it), so no future save test can compare
  two whole files byte-for-byte even when both were produced by Project.  The
  comparison has to be the semantic stream that carries the edited field, as
  W27-0.3 does for Task FixedData.

WAVE-27-G0-COMPLETE

## G1 - CI green on all three OSs

All three items are DONE; nothing in G1 is deferred.

| item | finding | status | evidence |
|---|---|---|---|
| W27-1.1 | `widest_line` unused: an error under GCC/Clang, so the Linux and macOS Qt builds stop | **DONE** | commit `5cd1c61`; gate log `build/w27/gates-8e7895f-W27-1.1.log` |
| W27-1.2 | oracle-provenance sub-check needs history; CI checks out one commit | **DONE** | commit `2f80dd1`; gate log `build/w27/gates-5cd1c61-W27-1.2.log` |
| W27-1.3 | push, dispatch both workflows, fix the first macOS/Linux failures | **DONE** | commits `61772a5`, `231a422`, `9b69e9c`, `07f67b6`, `1d0a92a`; final green runs `40056060660` (qt-shell, three OSs), `40056462711` (ci, two OSs), `40056064713` (app-macos) |

W27-1.1 detail:

- `app_qt/ribbon_bar.cpp`'s anonymous-namespace `widest_line()` had no
  caller.  It and the two includes only it used (`<QFontMetrics>`, `<algorithm>`)
  are removed; `<QFont>` stays for `ribbon_font()`.  No behaviour change.
- MSVC passed it; GCC and Clang fail `-Wunused-function` under `-Werror`.
  The first wave-27 qt-shell run (`40053878076`) reached the Linux and macOS
  builds past this point, and the final green run `40056060660` shows both
  compilers accept the tree.

W27-1.2 detail:

- Both checkouts now use `actions/checkout@v4` with `fetch-depth: 0` (ci.yml
  keeps its `ref:` expression for the workflow_run case beside it).
  `scripts/check_fixtures.py`'s provenance walk sees full history, so the
  last-measured-body check verifies on a runner rather than failing every
  oracle on a one-commit clone.
- The fixtures step passed on windows-msvc and ubuntu-gcc in ci run
  `40056462711`, and in all three qt-shell jobs of `40056060660`.

W27-1.3 detail:

The brief names `app/main.mm` ("not compiled since wave 24"), which only the
`app-macos` workflow compiles, so that workflow was dispatched alongside the
two the standing rules name.  It found two real breaks.  Dispatch series
(all on branch `wave27`; every red was fixed before the next dispatch):

| tip | qt-shell | ci | app-macos |
|---|---|---|---|
| `2f80dd1` | `40053878076` red: linux-qt backstage ink, macos-qt build (`kRailBackground`, `kSeparator`) | `40053880511` **green** | `40053935957` red: `main.mm:1524` undeclared `refresh_menu_items` |
| `61772a5` | - | - | `40054198682` red: declaration put in the anonymous namespace, recursive call ambiguous |
| `9b69e9c` | `40054734014` red: macos-qt About check (`activeModalWidget` null) | `40054736287` **green** | `40054738405` **green** |
| `07f67b6` | `40055403392` red: macos-qt About title | `40055405582` **green** | `40055407782` **green** |
| `1d0a92a` | `40056060660` **green** (linux-qt, windows-qt, macos-qt) | `40056062664` red: windows-msvc `cal` SEGFAULT; re-dispatch `40056462711` **green** | `40056064713` **green** |

- `61772a5` + `231a422` (main.mm): W26-6.1's recursive
  `refresh_menu_items()` is defined at global scope near the bottom of the
  file and called from `refresh_menu_bar()` inside the first anonymous
  namespace.  `231a422` puts the forward declaration at global scope, before
  the namespace, matching the definition; the wrong-scope first attempt is
  kept in history as the evidence for the ambiguity error.
- `9b69e9c` (qt): removed the two unused colour constants Clang rejected
  (GCC had accepted them); and replaced the two "HSL-darkest pixel equals the
  ink" rail checks with "the row contains the measured ink within the
  12-per-channel tolerance the selected-green check already uses" (Linux
  FreeType can make an antialiasing fringe darker than the glyph core).  The
  selected-green and disabled-ink assertions are unchanged in strength:
  #242424 is still separated from black and from #999999.
- `07f67b6` + `1d0a92a` (qt): the About box is found by walking
  `QApplication::topLevelWidgets()` (the repo's dialog idiom) instead of
  `activeModalWidget()`, which returned null on macOS; and it is identified
  by its text (product name + measured version), because macOS does not
  carry the QMessageBox window title the check had pinned.  The dialog is
  closed so `exec()` returns.
- ci `40056062664`'s windows-msvc `cal` SEGFAULT was triaged as a runner
  flake, not a code defect: the core sources at `1d0a92a` are identical to
  the green ci run `40055405582` at `07f67b6` (the only change between them
  is `app_qt/tests/backstage_gate_test.cpp`, outside the root project), and
  `cal` passed 20/20 local repeats on MSVC.  Re-dispatch `40056462711` was
  green on both OSs.

Group gates on the final commit `1d0a92a`, fresh trees configured from
scratch under `build/w27-fresh{,-app,-qt}` and built serially (log
`build/w27/g1-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- engine `--selftest` 53 checks, 0 failures; `--suite --expect-fail
  constraint-mso` reached the expected failing set, exit 0
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare`
  332/332 (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23
- fixtures OK (28 documented hashes, 24 tracked `.mpp`), oracle provenance
  OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0
  on `c632c8f..HEAD`; client-string hex grep no matches

Per-commit gate logs: `build/w27/gates-8e7895f-W27-1.1.log`,
`gates-5cd1c61-W27-1.2.log`, `gates-2f80dd1-W27-1.3.log`,
`gates-61772a5-W27-1.3.log`, `gates-231a422-W27-1.3.log`,
`gates-9b69e9c-W27-1.3.log`, `gates-07f67b6-W27-1.3.log` (all under
`build/w27/`).  CI watch logs are beside them as `ci-watch-*`.

## Found, not fixed (G1)

- macOS does not surface the `QMessageBox::about` window title the Windows
  run carries; the gate now asserts the box's text and prints the title as
  a note.  This is a platform difference in Qt, not a port defect.
- ci run `40056062664`'s `cal` SEGFAULT happened once on an unchanged core
  tree and did not reproduce (20/20 local repeats, green re-dispatch).  If
  it recurs, treat it as a runner-memory symptom and add the `cal` binary to
  the triage, not a scheduling change.

WAVE-27-G1-COMPLETE

## G2 - commit the decoding evidence

W27-2.1 is DONE; nothing in G2 is deferred.

| item | finding | status | evidence |
|---|---|---|---|
| W27-2.1 | The one-change-per-save Project files behind the wave-26 text-style, bar-style and timescale decoders live only under `build/w26/` | **DONE** | fixtures `7a6ec96`, tests `72bf912`, docs `3049471`; gates `build/w27/gates-97e0b81-W27-2.1.log`, `gates-7a6ec96-W27-2.1.log`, `gates-72bf912-W27-2.1-readme.log`, `build/w27/g2-fresh-gates.log` |

W27-2.1 detail:

- Took the brief's lighter option: commit the extracted records, not the
  239,104-byte `.mpp` saves.  `tests/data/wave26-gantt-saves/` holds 16 files:
  14 `*.style.bin` (41,363 bytes each) and 2 `*.fonts.bin` (8,710 bytes each).
  Each style blob is the applied view's `0x22400008` record from one Project
  2021 save (the applied view is main-block index 1 of `214/CV_iew`, which is
  the block the COM calls edited; index 0 decodes but shows no diff).  Each
  font blob is that save's `214/Props` `0x03400000` FONT_BASES payload.
- `control.style.bin` is the same session's unedited save.  Its FONT_BASES
  payload is byte-identical to the tracked `synthetic-gantt-fonts.bin`, so it is
  not duplicated; every non-font save's payload is byte-identical too, so only
  the two real font-table changes are committed.  Duplicate evidence was
  committed once: `control-a` = `control-b`, `t3-milestone-italic` = `-int`,
  `b2-task-red-int` = `-name`, `ts-ymd` = `ts-2tier`; the Arial and size-8 saves
  share a style record and differ only in the font table.  Failed and no-op COM
  calls (`b2-task-red-explicit`, `ts3-toplabels`, the wave-23 timescale
  variants) are not committed; `ts3-count2` has distinct bytes but decodes to
  the same tiers as `ts-yqm`, so it adds no field evidence.
- No metadata needed neutralising: a record is the style payload only.  A
  printable ASCII/UTF-16 scan of all 16 blobs finds only Project's stock bar
  style names and font families -- no path, author, project title or task text.
  The source `.mpp` SHA-256s are in `tests/data/README.md`; the `.mpp` files
  stay local under the git-ignored `build/w26/g3/`.
- 13 tests, one per save (198 checks): `gantt-save-font-family`, `-font-size`,
  `-critical-colour`, `-milestone-italic`, `-milestone-bold`,
  `-milestone-underline`, `-highlight-fill`, `-task-bar`, `-summary-bar`,
  `-milestone-bar`, `-timescale-yqm`, `-timescale-ymd`, `-timescale-yhq`.  Each
  loads the control record and the save through the public decoder, builds the
  full decoded field-difference list (`gantt_differences` compares every field
  the decoder exposes, including the has-colour flags, bar ids, tier counts and
  view flags), asserts the list is exactly the recorded field(s), then asserts
  the changed field's decoded value.
- The decoded changes: Arial (point size 0xFFFF) on all 17 text styles; Calibri
  8 on all 17 plus the bold/foreground resets the explicit-argument call made;
  critical foreground (0,0,255); external italic/bold/underline on with the grey
  foreground becoming automatic; noncritical background (255,255,0); Task
  middle red with start/end becoming automatic and id 40; Summary's three
  colours green and id 40; Milestone start blue with middle/end becoming
  automatic and id 40; middle/bottom tiers weeks/13 -> quarters/2 and days/20 ->
  months/10 (`ymd`: months/9 only; `yhq`: half_years/123 and quarters/5).  The
  raw records also differ in per-save tail bytes and in undecoded regions; the
  tests assert the decoded projection, and every decoded field except the
  recorded change is equal.
- Wave-26 errata confirmed by the files and written into the README: Project's
  `TextStyles` `Item` index is not the slot index (Item 3 edited the external
  slot, Item 7 the noncritical slot), and a family-only save stores 0xFFFF as
  the point size.

Group gates on the final code commit `72bf912`, fresh trees configured from
scratch under `build/w27-fresh{,-app,-qt}` and built serially (log
`build/w27/g2-fresh-gates.log`):

- core configure/build exit 0 with `CORE_WARNINGS_AS_ERRORS=ON`; core ctest
  `-E plan_recalc` 51/51
- `plan_recalc` 512 checks with corpus / 266 with `MPP_NO_CORPUS=1`
- engine `--selftest` 53 checks, 0 failures; `--suite --expect-fail
  constraint-mso` 7 pass + the one expected failure, 0 unrecorded
- reader 1100 checks with corpus / 420 without; M2 `recalc --compare` 332/332
  (268 leaf + 64 summary fields)
- app ctest 17/17; app_qt ctest 23/23
- `check_fixtures.py` OK (44 documented hashes, 24 tracked `.mpp`), oracle
  provenance OK (8)
- client-name scan 0 hits on HEAD; whitespace-normalised `guard_names.py` 0
  on `c632c8f..HEAD`; client-string hex grep no matches
- The script's exit code is 1 because its last command is the expected-empty
  hex grep.  Clearing `build/w27-fresh-app` also hit a transient
  directory-handle lock; CMake then re-ran compiler detection and every source
  compiled, so that tree was still configured and built from scratch (the core
  and app_qt trees were cleared normally).

Per-commit gate logs: `build/w27/gates-97e0b81-W27-2.1.log` (fixtures),
`gates-7a6ec96-W27-2.1-tests-build.log` and `gates-7a6ec96-W27-2.1.log`
(tests), `gates-72bf912-W27-2.1-readme.log` (docs).

## Found, not fixed (G2)

- **Medium**: a Project save that changes only the Gantt text font family stores
  point size 0xFFFF (65,535) in the target FONT_BASES slot (the `arial` save
  proves it; the `size8` save stores 8 when a size is given).  The decoder
  reports the stored value faithfully, but `app_qt/gantt_canvas.cpp:719` and
  `app_qt/sheet_table_model.cpp:261` pass any `point_size > 0` straight to
  `QFont::setPointSize`, so reopening such a file in the port would request a
  65,535-point font for the timescale header and the sheet rows.  A sanity
  bound in the consumers (or an "unset size" flag from the decoder) belongs to
  the rendering finding, not to W27-2.1's fixtures and tests.

WAVE-27-G2-COMPLETE
