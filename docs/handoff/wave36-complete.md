# Wave 36 record: the writer wave

Brief `docs/handoff/wave36-brief.md`; branch `wave36`, cut from `main` at `b2c0b19`; builder Claude Sonnet 5.5.
Nothing is pushed.  Gates are per commit under `build/w36/` (git-ignored), each log named for the commit it was run on.

## G0: the gate and the records

Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0.1 | A-2 | `a20874d` | the noise allow-list moves into `tools/trace/trace_gate.py`: per-stream windows and byte ceilings taken from the trace null run (the G0 review found by-name waivers and regions still in them, R36G0-1 to -5; G0b removed them, and every mask now names a field); four narrow masks (the GUID counter bytes, the creation stamps, the font word's top byte at payload +43, the per-file Props identity records); `runone6.ps1` records the exit code |
| W36-0.2 | A-4 | `028fb51` | no wrong-length mask: refused when the control and the reference differ in length; fixed-stride streams masked per record (Task FixedData 202 from 48, Task Fixed2Data 64, Assn FixedData 110, Assn Fixed2Data 48, and the smaller Fixed2Data streams) |
| W36-0.3 | A-3 | `f0c3113` | an absent control null, or the control passed as its own null, is a FAIL named "no null"; `runone6.ps1` passes a path that does not exist instead of the control |
| W36-0.4 | A-6 | `cebf445` | ctest `trace_gate_guards` (`tools/trace/test_trace_gate.py`): one flipped byte, at the first offset the mask and windows do not cover, in each non-empty stream of a committed pair is a FAIL (a window over a whole stream but its first byte went unseen, R36G0-8; G0b's test flips every offset outside the named fields), with `checked > 0` and an equal stream set; a gate that checked nothing is a FAIL "nothing checked" |
| W36-0.5 | A-11 | `4ec0562` | `r5b-skip.ps1` invokes "Skip for now" only in a dialog whose title starts "Sign in"; `kvp/host.ps1` `vmx` throws on a MISMATCH and on a reply whose length is not the one announced; ctest `trace_host_scripts` (`tools/trace/test_host_skip.ps1`, only when `pwsh` is found) |
| W36-0.6 | A-9 | `2ecf42b` | `docs/START-HERE.md` 28605 to 6267 bytes; the wave history moves to `docs/handoff/history-waves34-35.md`; `memory/coordinator.md` 18672 to 5265 bytes and current; `INDEX.md` |

**Gate line.**  At `4ec0562`, core tree `build/w35g-close-fresh` (incremental, built and reconfigured at 0.5): build exit 0,
`ctest -E plan_recalc -j4` 67/67 passed, including `trace_gate_guards` and `trace_host_scripts`
(`build/w36/cebf445-W36-0.5-core-ctest.log`); `MPP_NO_CORPUS=1 ctest -R plan_recalc` passed
(`build/w36/2ecf42b-W36-G0-plan_recalc.log`); `check_fixtures.py` OK.  Every commit: `check_fixtures` 0, `check_markers` 0,
`guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean,
no CR; each commit carries both trailers.  This wave's G0 changes no C++ source, so no new fresh-tree build was
run (the coordinator's fresh-tree gates follow the marker).

### Guards seen red

Each red was run on the previous commit's tool (or the old copy), then the green on the new one.

| item | red (the old tool) | green |
|---|---|---|
| 0.1 | 200 flipped bytes in `214/CEdl/Var2Data` and in `Props14` of `wave30-calculate`: `96 streams, 94 checked, 0 problems` (the old gate skipped both by name) | both are problems (`build/w36/0e8c9d4-W36-0.1-red.log`, `-green.log`) |
| 0.2 | `wave34-assign-add-second` with one byte flipped at offset 4 of `114/TBkndAssn/FixedMeta` (a control/null noise offset; control 152 bytes, reference 186): `96 streams, 96 checked, 0 problems` | the stream is a problem (`build/w36/a20874d-W36-0.2-red.log`, `-green.log`) |
| 0.3 | a nonexistent null path: a traceback, no verdict; the control as its own null: `96 streams, 96 checked, 0 problems` | both print "no null" and exit 1 (`build/w36/028fb51-W36-0.3-red.log`, `-green.log`) |
| 0.4 | the "nothing checked" case failed on the `f0c3113` gate (`FAIL W36-0.4 nothing checked: a problem 'nothing checked' -- ['size-noise Props14 556->566']`, `1 failed`); `ctest` on the core tree before registration: `No tests were found!!!` | 105 checks pass; `66/66 Test #3: trace_gate_guards ... Passed` (`build/w36/f0c3113-W36-0.4-red.log`, `-red-ctest.log`, `-core-ctest.log`) |
| 0.5 | the HEAD copies: `FAIL host.ps1 sha MISMATCH: throws`, `FAIL host.ps1 reply length differs from out_meta: throws`, `FAIL r5b-skip.ps1 Invoke() is under a title StartsWith(Sign in) test`, `3 failed` | all 6 pass; `67/67 Test #4: trace_host_scripts ... Passed` (`build/w36/cebf445-W36-0.5-red.log`, `-green.log`, `-core-ctest.log`) |
| 0.6 | START-HERE.md 28605 bytes (limit 8192), coordinator.md 18672 bytes, both stale (main `5e1c8ea`, "nothing of wave 35g is pushed") | 6267 and 5265 bytes, true at `b2c0b19` on `wave36` |

### Deferred

None.  Every G0 item is fixed.

### Found, not fixed

* **CI has no `olefile`.**  `trace_gate_guards` exits 77 and ctest reports SKIPPED on a runner without it, so CI does not yet run the guard.
  Direction: `pip install olefile` in the workflows that run ctest.  Workflow files are outside G0.
* **Path-bearing streams tolerate up to their ceiling.**  `214/CEdl/Var2Data`, `Props14` and `SummaryInformation` carry file paths, so the
  gate allows a size delta (128, 96, 16) and up to 64, 64 and 40 differing bytes at equal size.  One flipped byte there is undetectable by design;
  the test flips ceiling plus one.  The path-stream bytes are not counted as checked.
* **The windows are tuned to the trace null run and the 99 committed control pairs** (`tests/data`).  A recording made on another machine can carry
  noise the null run did not show, and the coordinator's re-gate of the 109 recordings may FAIL on it.  Such a FAIL is a finding by design: widen a
  window only with a measured offset, never by stream name.  Same-machine noise outside the null run's list (Props tail bytes, Assn and Rsc FixedMeta
  and Fixed2Meta, Cal Fixed2Data above its null-run ceiling, `CEdl/VarMeta` 2 over 1, `CV_iew/Var2Data` 6 over 4) is left to the control mask.
* **The scripts of 0.5 were not run against the VM.**  The wave rules bar touching the VM, so `trace_host_scripts` stubs the Hyper-V calls and checks
  `r5b-skip.ps1` by AST.  The first real VM run of the two scripts is the coordinator's.
* **The trace-agents' frozen copy of the tools and `build/trace-2021/gs6.ps1`** were not read or changed (wave rule).  The VM runs the frozen copy of
  `trace_gate.py` until the coordinator copies the new one in.
* **`python -I` cannot import `olefile`** here (it lives in the user site-packages), so the trace tests run without `-I`; they read only committed files.

WAVE-36-G0-COMPLETE

## G1: the font item

Items, one finding or item id per commit, in order:

| item | commit | what |
|---|---|---|
| W36-1.1 | `baab011` | Bold off is wired: `make_cell_bold_edit` builds the writer's Bold off when the view's one item is the measured Bold item (a toggle, as Project's button is); both shells queue it |
| W36-1.2 | `95e8195` | Bold on any stored task row and on the Start column; held-out pairs `wave35-g3-font-bold-row2` and `-start`; 6607 / 6608 effect bytes, no mismatch outside the font word's top byte, masked by exact field |
| W36-1.3 | `893684f` | Italic for the one measured cell (row uid 1, Task Name, on); `font_italic` in both shells (Edit > Font Italic, no shortcut: Cmd+I is Insert Task) and the ribbon's I; 6607 effect bytes; **no held-out Italic pair, so (b) is not met and the writer stays limited to that one shape**; the pin and the backlog regenerated |
| W36-1.4 | `64a185e` | the 4(e) read-back plan, below: **planned, not run** (the builder uses no COM) |
| W36-1.5 (D-2) | `1fa7338` | `wave35-g3-font-bold-range` relabelled in `view-stream-214.md` and its evidence note as a one-row Bold, not a range; the fixture is untouched |
| Underline, size, name, colour, range, combined attributes, a second change | none | **not started**, with the evidence in Deferred below |

**Gate line.**  At `1fa7338`, trees `build/w35g-close-fresh` (core), `-app` and `-qt` (configured fresh at the wave 35g CLOSE, built
incrementally here, not re-cloned): core build 0, `ctest -E plan_recalc -j4` 67/67 (this includes `engine_diff_harness`,
`engine_diff_apply` and `engine_diff_suite`, the engine suite lane); `MPP_NO_CORPUS=1 ctest -R plan_recalc` 1/1; M2
`recalc corpus/project_a.mpp --compare corpus/probe/project_a-recalc.mpp` overall 332 / 332 (the log's other two `overall` lines,
268 / 268 and 64 / 64, are two subsets of the same 332 fields); `check_fixtures.py` 0; app build 0, ctest 34/34; Qt build 0, ctest 52/52
(`build/w36/1fa7338-W36-G1-group-*`).  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0,
the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean, no CR; each commit carries both trailers.  `app/main.mm` is
AppKit code and builds only on macOS, so it is gated by the CI job `macos-appkit`, not here; its edits mirror the Qt ones.

### W36-1.4: the 4(e) read-back plan for the font item

**Status: planned, not run.**  The builder uses no COM (brief section 1), so no method below has been tried.  Nothing here is a
finding that a read-back exists; each method is a candidate with the result that would settle it.  The coordinator runs them in
order and records, per method, the command and what came back, including a failure.

**What is known.**  The wave 35g close record says "there is no COM read-back of cell font state: `FontBold` is setter-only"
(`docs/handoff/wave35g-verification/coord-gates-close.md`).  That is the only method on record as failed, and it was a statement about
`Application.FontBold` and `FontItalic`, not a survey of the object model.  The wave 35 oracle (`scripts/oracle_w35_g3.py`,
`font_edit`) only selects the cell with `SelectTaskField(Row, Column, RowRelative=False)` and calls the setter.  Because of that,
4(e) counted `cell-bold` as "not checked, vacuous because Project's reopen equals the control's": the facts it reads (tasks,
dates, assignments) do not change when a font does.

**Why this matters.**  Every font pair (`cell-bold`, `cell-bold-row2`, `cell-bold-start`, `cell-bold-range`, `cell-bold-off`,
`cell-italic`) is currently checked only field by field against Project's own save.  4(e) is the one step that asks Project
whether it reads the port's save as it reads its own, and it cannot while the read has no font in it.

**Method A: enumerate the object model, then read a getter if one exists.**  With the Project COM server running in the neutral root:
1. `ti = app._oleobj_.GetTypeInfo()`, then walk `ti.GetTypeAttr().cFuncs` with `GetFuncDesc` and `GetNames`; print every member
   whose name contains `Font`, `Style`, `Bold` or `Italic`, with its `invkind` (1 = function, 2 = property get, 4 = put,
   8 = putref).  Do the same for the `Task`, `Selection` (`app.ActiveSelection`), `View` and `Table` interfaces, and for
   `app.ActiveProject.TextStyles` if the attribute exists.
2. Record the list.  If any member is a property get (`invkind` 2) or a function that returns a font or style object, call it
   after `SelectTaskField(Row=r, Column=c, RowRelative=False)` and read `.Bold`, `.Italic` (and `.Underline`).
3. Positive control before any claim: open `wave35-g3-font-bold-row2-control.mpp` and `wave35-g3-font-bold-row2.mpp`, select
   row 2, `Name`, and read the getter on each.  The method is usable only if the two reads differ (control not bold, Project's
   save bold).  If they do not differ, or the getter raises, the method failed: record the exception text and stop on A.

**Method B: read the formatting from the clipboard (no getter needed).**  `SelectTaskField(Row=r, Column=c, RowRelative=False)`,
`app.EditCopy()`, then read the clipboard formats `HTML Format` and `Rich Text Format` (`win32clipboard`, after
`RegisterClipboardFormat`).  Bold is `font-weight:bold` or `<b>` in the HTML, `\b` in the RTF; italic is `font-style:italic`
or `<i>`, `\i`.  This uses the same HTML serialiser that `view-stream-214.md` notes reads the styles for export, so it is
the most likely to carry the font.  Same positive control as A step 3.  It changes the clipboard: save and restore it, and run
it in the neutral root.  If the copy carries no formatting at all (plain text only), the method failed: record the formats the
clipboard held.

**Method C: compare a rendered cell.**  Last resort, weaker because it is pixels and not a stored value.  If A step 1 lists
`EditCopyPicture` (or a similar call), select the cell, copy a picture of the selected row, and compare the bitmaps of control,
Project's save and the port's save.  Record the call signature found in step 1; do not guess it.

**What 4(e) must then report, per font pair.**
- A pair is **checked** only if the read-back of the edited cell on the control differs from the read-back on Project's save.
  A pair whose reads are equal stays **not checked**, as `cell-bold` is today.
- It **passes** if the read-back of the port's save equals Project's on the edited cell, and equals the control's on every other
  cell of the first ten rows of the `Name` and `Start` columns (this catches a font written onto the wrong cell).
- Pairs and files (all committed under `tests/data`): `wave30-textstyle` (row 1, Name, Bold), `wave35-g3-font-bold-row2`,
  `wave35-g3-font-bold-start`, `wave35-g3-font-bold-range` (a one-row Bold on row 2, not a range: decision D-2),
  `wave35-g3-font-bold-off` (the control is the Bold file, so the read-back must go from bold to not bold) and
  `wave35-g3-font-italic`.  The report states checked and not-checked counts for these six, separately from the other pairs.
- The port saves are built the way `reopen_4e.py` builds them for `cell-bold` (through `save_view_cell_style`'s cascade), not
  by copying Project's file.

**If A, B and C all fail.**  The coordinator records each command and its failure here.  4(e) then stays vacuous for the font
item, the six pairs count as not checked, and the wave record says so in those words.  The font writers stay limited to their
field-by-field pairs, which is what the lift rule already requires.

### Guards seen red

Each red was run on the tree before the change, then the green on the new one.

| item | red | green |
|---|---|---|
| 1.1 | `app_edits_cell_bold` on `53351b2`: "make_cell_bold_edit accepts the Bold item's cell", 406 checks passed, 1 failure (`build/w36/53351b2-W36-1.1-red.log`) | 720 checks passed (`-green.log`); app 34/34, Qt 52/52 |
| 1.2 | core: "row uid 2 is not the measured row uid (1)"; app: 9 failures, 747 checks passed, among them "cell-bold-row2: make_cell_bold_edit accepts the measured cell" and "cell-bold-start: ..." (`build/w36/baab011-W36-1.2-red-*`) | core 67/67, app 34/34, Qt 52/52 with the row 2 and Start cells saved |
| 1.3 | core: `mppwrite_roundtrip_viewstream` dies with "FAIL: unhandled exception: view cell style write: only Bold is measured; italic, underline, ..." (`build/w36/95e8195-W36-1.3-red-core-ctest.log`).  **App: red by a temporary probe, not by the old tree**: the italic builder was made to refuse, and `app_edits_cell_bold` failed with "cell-italic: make_cell_italic_edit accepts the measured cell" and four more, 5 failures, 1763 checks passed (`-red-app-*`); the probe was removed before the commit | core 67/67, app 34/34, Qt 52/52; the pair has 6607 effect bytes and 0 mismatches |
| 1.3, Qt | after the I button was enabled, `qt_ribbon_inventory` failed ("tab 0 (Task) / Font / I: enabled state is the independent pin's") and `qt_command_backlog` failed ("PIN ENABLED but backlog disabled: Task / Font / I"): both pins were stale, which is the check working | pin and generated backlog updated; Qt 52/52 |

### Deferred

Each of these is **not started**, not a failed attempt: the lift rule's (b) or (a) has no pair or no trace to stand on, so there is nothing to
build.  The evidence is a command run on the committed pairs, `python build/w36/dump_font_item.py tests/data/<pair>.mpp`
(`build/w36/1fa7338-W36-1.6-underline-dump.log`), which prints the appended 44-byte item:

| item | what the committed pair holds | why it is not started |
|---|---|---|
| Underline | `wave35-g3-font-underline`: words `00000001 0b40000e 04000003`, zeros at +12 and +24, 0 at +36, font word `000000ca` | The pair is the `Font32Ex` shape, not the toolbar path's (`ff000000` at +12 and +24, 1 at +36, font word `…02`, as the italic pair `02000003 ff000000 … 00000001 6b000004` shows).  A writer built to the toolbar shape could not match this pair, and no toolbar-path Underline pair exists, so there is no pair to derive from and none held out.  The toolbar `FontUnderline(True)` call raised a Python `TypeError` in wave 35 (a harness error, not a Project statement) and has not been re-measured |
| Size, name | `font-size` and `font-name` differ from each other only in the top (noise) byte of the font word: `…00000006 … 6b0000d8` against `…00000006 … 340000d8` | The storage of font size and name is not located (`trace-font-item-r6.md`): the item carries neither, so a writer has nowhere to put them |
| Colour | `wave35-g3-font-colour`: `00000003 000000ff … d70000c8` | waits on the open bits 0x40 and 0x80 of the font word (`trace-font-item-r6.md`), and the pair is `Font32Ex` shaped |
| Range | `wave35-g3-font-bold-range` holds one 44-byte item for row 2, so it is a one-row Bold (D-2) | the missing pair is a Bold (or Italic) saved with `ActiveSelection.Tasks.Count == 2` confirmed before the call; no capture round 7 font pair is committed under `tests/data` (listed at `1fa7338`: only the `wave30-textstyle` and `wave35-g3-font-*` files) |
| Combined attributes | none | the missing pair is two attributes on one cell (for example Bold then Italic) saved once |
| A second change to a cell | none | the missing pair is two font edits on one cell, or a font edit on a cell of a view that already stores a cell format; the writer refuses a view that already carries one (except the measured Bold off) |

The 1.4 read-back is **planned, not run**: the section above is the coordinator's to run, and until it does 4(e) is vacuous for the
six font pairs (`cell-bold`, `cell-bold-row2`, `cell-bold-start`, `cell-bold-range`, `cell-bold-off` and `cell-italic`).

### Found, not fixed

* **Italic has one pair.**  The writer is limited to Italic on, row uid 1, Task Name; Italic off, another cell and a second change are refused.
  A second Italic pair (another row, or a held-out cell) is what lifts it.
* **Bold off is limited to (row 1, Name).**  No pair holds another Bold-off shape.
* **The joint cell (row 2, Start) is unmeasured.**  The row pair and the column pair are separate; no pair crosses them.  The writer accepts it by
  the rule, and no fixture tests it.
* **Row ID, UniqueID and the displayed row are not separated by any pair.**  Every pair has them equal, so the app refuses a task whose ID differs
  from its UniqueID, and the writer's row is the UniqueID.  A sorted, filtered or collapsed view could separate them.
* **The font word's top byte (entry +43) is uninitialised in Project.**  The port pins `0x90`; the tests assert Project's value (`0x6b` or
  `0x00` by session) and the port's, then mask that one byte by exact field (record id and offset), never by stream name.
* **The sheet does not paint the stored font.**  This is the W34-4.5 record's finding and was not re-checked in G1: Bold and Italic are written
  and read back by the port's own reader, and the sheet is not claimed to show bold or italic text.
* **`app/main.mm` is CI-gated only** (`macos-appkit`); the three font edits there were not built on this machine.
* **The backlog's Font Bold gap text was stale** from W36-1.2 until 1.3, which regenerated it.
* **The pair-anatomy note for `wave35-g3-font-bold-range` carries a hand-added paragraph** (1.5); regenerating the note with `scripts/pair_anatomy.py`
  drops it.  The correction in `view-stream-214.md` stays.
* **The fresh-tree gates are the wave 35g CLOSE trees built incrementally**, not a re-clone; the coordinator's verify-worktree gates follow the marker.
* **Gate scripts** (`build/w36/*.sh`, git-ignored) are the builder's; they are named in each commit message by log path only.

WAVE-36-G1-COMPLETE

## G0b: the trace gate, fixed

Brief `docs/handoff/wave36-brief.md` section 2 (G0b); findings R36G0-1 to -11 of `docs/handoff/wave36-verification/review-g0.md`.  Design rule: a mask
names a field, never a region, and there are no single-run ceilings.  Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0b.1 | R36G0-1, -6 | `916ac5b` | the whole-view-bag window of `214/CV_iew/Var2Data` is gone; the only noise left there is the font word's top byte (payload +43), found by parsing the bag blocks VarMeta locates; windows come from each side's own VarMeta |
| W36-0b.2 | R36G0-2 | `6082441` | no size tolerance: `214/CEdl/Var2Data`, `Props14` and `SummaryInformation` are parsed and their decoded fields replaced by placeholders in a canonical form compared byte for byte; the acceptance sweep enters the ctest |
| W36-0b.3 | R36G0-3 | `e2602fb` | stream names match whole (no `endswith`); `DocumentSummaryInformation` is compared in full |
| W36-0b.4 | R36G0-4 | `1a7a4d8` | every mask is a named field, cited by row of `spec/writers/evidence/trace-gate-fields-g0b.md`: GUIDs only when both sides are version 1 or zero, the creation stamp only of a row the control lacks, the view GUID string at payload +0 |
| W36-0b.5 | R36G0-5 | `0e96af7` | no single-run ceilings (`ceiling`, `rows`, `ceiling_for`) |
| W36-0b.6 | R36G0-7 | `6b7f690` | `vmx`'s reply-length check accepts the Out-String reply shape (the announced length counts the trailing newline); the stub models it |
| W36-0b.7 | R36G0-8 | `6279a9e` | the flip test covers every offset outside the named fields in three pairs, and six planted windows; this found the `_meta_blocks` hang (a flipped VarMeta count looped 2^32 times) |
| W36-0b.8 | R36G0-9 | `c1a5cd5` | stride masks follow a row's identity (uid word, or the FixedData item a Fixed2Data row follows), not its position; a row either side lacks, or a uid twice, has no mask |
| W36-0b.9 | R36G0-10, -11 | `285da21` | the three overstated sentences corrected; `gates-g0.md` committed; the standing facts restored verbatim into `history-waves34-35.md` |
| W36-0b.10 | CI | `712b40d` | both ctest-running CI jobs install `olefile` so `trace_gate_guards` runs; the test carries a 300 s TIMEOUT |

**Acceptance (the committed ctest `trace_gate_guards`).**  Negative: TRACED = the control's null save, REFERENCE = the edit, over every committed
pair whose edit differs from its control-null outside `SummaryInformation`: **87 of 89 FAIL**; the two that PASS (`wave28-percent-auto100`,
`wave28-percent-auto40`) are no-op edits equal to the control outside the noise.  Positive: TRACED = the control's null save, REFERENCE = the control:
**97 of 99 PASS**; the two that FAIL (`wave30-inactivate`, `wave35-g3-assign-remove-port-saved`) have a control and null that are not one state
(`214/CEdl/Var2Data` record structure differs), and no mask was widened.  Before G0b the negative sweep let 8 of 89 PASS.

**Gate line.**  At `712b40d`, core tree `build/w35g-close-fresh`: build exit 0; `ctest -E plan_recalc -j4` 67/67 passed, including `trace_gate_guards`
(219 checks, 15 s) and `trace_host_scripts` (`build/w36/285da21-W36-0b.10-core-ctest.log`).  Every commit: `check_fixtures` 0, `check_markers` 0,
`guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each commit carries the one
trailer.  Per-item logs are in `docs/handoff/wave36-verification/gates-g0b.md`.  G0b changes no C++ source.  The builder did not touch the VM
and pushed nothing.

### Guards seen red

Each red ran on the previous commit's tool, then the green on the new one; logs are `build/w36/<parent>-W36-0b.N-red.log` and `-green.log`.

| item | red (the old tool) | green |
|---|---|---|
| 0b.1 | TRACED = control-null, REFERENCE = the sort-by-name-descending edit and the Bold-off edit: `96 streams, 94 checked, 0 problems`, exit 0 | both `1 problems`, `bytes-differ ???214/CV_iew/Var2Data`, exit 1; a flip at payload +43 of the font row passes, +40 fails |
| 0b.2 | the same sweep over 89 edits: 8 PASS (the two no-ops and six others) | 2 PASS (the two no-ops) |
| 0b.3 | one byte flipped at +0 and at +60 of `?DocumentSummaryInformation`: `96 streams, 96 checked, 0 problems` | both `bytes-differ ?DocumentSummaryInformation` |
| 0b.4 | a non-version-1 GUID, a flipped Assn Fixed2Data +40, Task Fixed2Data +46, a stamp of an existing Task row, a changed opening brace of the view GUID string: all PASS | all FAIL; one changed hex digit of the view GUID PASSes |
| 0b.5 | a save with all 80 named fields of 12 streams changed: ten FAIL lines (`over-ceiling ???114/Props 30>24`, `...Assn/Fixed2Data 138>100`, ...) | PASS; the same save with one data byte changed FAILs |
| 0b.6 | the new test against the previous `host.ps1`: 6 FAIL lines (the CRLF reply one character short, four pieces, two last-piece-newline replies, the sha MISMATCH case, no newline) | all pass |
| 0b.7 | red1: a window over all of Task Var2Data but byte 0, the old test ends "all passed"; red2: the new test FAILs on all three pairs ("the windows are 665 bytes, the named fields 0"); red3: the new test on the old gate hangs in `_meta_blocks` | all passed, 200 checks, 13 s |
| 0b.8 | the noise of one control row planted in a middle-insert pair: the same row of the edit FAILs and the control's position (another row) PASSes, both wrong; the new test file crashes on the old gate | the same row PASSes, the control position FAILs, the inserted row FAILs |
| 0b.9 | records only; no red | scans clean; the restored section equals lines 284 to 381 of START-HERE at `2ecf42b^` by `diff` |
| 0b.10 | the CI jobs could not run the guard: `olefile` absent, exit 77, SKIPPED | `ci.yml` parses with the steps in both jobs; ctest 67/67 with the guard run |

### Deferred

None.  Every G0b item is fixed.

### Found, not fixed

* **The CI steps of 0b.10 have not run.**  Nothing is pushed; the first CI run after the push shows whether `setup-python` and `pip install olefile` give
  every runner the interpreter CMake finds.  The 300 s test limit is a guess from 15 s locally.
* **The gate on the VM is still the frozen copy.**  The VM runs the old `trace_gate.py` and `host.ps1` until the coordinator copies the new ones in;
  the new `host.ps1` reply check models the Out-String shape, and whether the real CIM XML keeps the CR is not settled here (the test models both).
  The coordinator confirms it with one `vmx 'Get-Date'` before the re-gate.
* **Undecoded 4-byte save-date words in `214/CV_iew/Var2Data`** (17 control/null pairs differ there, outside the font bytes).  Only the control mask covers
  them; a traced save whose control is another pair's would show them as differences.  Direction: decode the word's record and name the field.
* **Task Fixed2Data +42 and +46..49 are data**, not noise: they are equal in every committed control/null pair.  The old VM null run may have seen
  noise there; the coordinator's re-gate would then FAIL on it, which is a finding by design (name the field with a measured offset, never a region).
* **Props14 tail entry `0xffffffff`**: present in the controls and edits, absent in 98 of the 99 null saves.  It is replaced by a placeholder in the
  canonical form; what it means is not known.
* **Two control/null pairs are not one state** (`wave30-inactivate`, `wave35-g3-assign-remove-port-saved`): the positive sweep FAILs there by design.  The
  pairs need re-recording, or the difference in `214/CEdl/Var2Data` needs decoding.
* **Row identity (0b.8) is decoded from the committed files, not from a format spec.**  The uid word at row +0 is the identity for Task and Assn
  FixedData; for Rsc, Cal and Cons the identity is the word the FixedData item carries at the offset noted in `trace-gate-fields-g0b.md`, found unique and
  stable across all 332 files, not proven to be the resource or calendar uid.  The Assn FixedMeta and Fixed2Meta streams list only some rows, so they are
  not used.  A uid carried by two rows (a deleted Task row that stays; two Rsc rows in two files) leaves those rows unmasked.
* **The two G0 "Found, not fixed" bullets on ceilings and null-run windows** (path-bearing streams tolerate a ceiling; windows tuned to the null run)
  **are superseded**: the tolerances are gone (0b.2, 0b.5) and every mask is a named field (0b.4).
* **A real flaw the flip test found**: `_meta_blocks()` looped up to 2^32 times on a flipped VarMeta count and hung the gate.  Fixed in 0b.7 by limiting the
  count to what the stream can hold.  No other meta reader was searched for the same fault.
* **`python -I` still cannot import `olefile`** here (user site-packages), so the trace tests run without `-I`; they read only committed files.

WAVE-36-G0b-COMPLETE
