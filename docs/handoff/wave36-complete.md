# Wave 36 record: the writer wave

Brief `docs/handoff/wave36-brief.md`; branch `wave36`, cut from `main` at `b2c0b19`; builder Claude Sonnet 5.5.
Nothing is pushed.  Gates are per commit under `build/w36/` (git-ignored), each log named for the commit it was run on.

## G0: the gate and the records

Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0.1 | A-2 | `a20874d` | the noise allow-list moves into `tools/trace/trace_gate.py`: per-stream windows and byte ceilings taken from the trace null run (the G0 review found by-name waivers and regions still in them, R36G0-1 to -5; G0b removed them; the control mask, which stayed a rule of the gate through G0b and excused any byte it covered, stopped excusing in G0c, W36-0c.5); four narrow masks (the GUID counter bytes, the creation stamps, the font word's top byte at payload +43, the per-file Props identity records); `runone6.ps1` records the exit code |
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
| W36-1.2 | `95e8195` | Bold on the Task Name cell of rows 1 and 2 and the Start cell of row 1; 6607 / 6608 effect bytes, no mismatch outside the font word's top byte, masked by exact field.  **Corrected by W36-1b.1 (R36G1-1):** this row first said Bold was enabled on "any stored task row" with "held-out pairs `wave35-g3-font-bold-row2` and `-start`".  That was wrong: each pair fixed the structure at its own cell, so none predicts another cell and (b) is not met for the row or field rule.  The writer and the app now accept exactly those three cells (`5cb0f36`) |
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
   8 = putref).  Do the same for the `Task`, `Selection` (`app.ActiveSelection`), `View` and `Table` interfaces, for
   `app.ActiveProject.TextStyles` if the attribute exists, and for `app.ActiveCell` and its `Cell` interface (W36-1b,
   R36G1-4).  `Cell` is where Project's object model keeps per-cell formatting, so it is the most direct candidate and is
   walked before Method B.  Whether `Cell` has a Bold or Italic getter in Project 2021 is UNVERIFIED: this line adds the
   interface to the enumeration and claims nothing about its members.  `app.ActiveCell` needs a cell selected
   (`SelectTaskField` as in step 2); if it raises with a selection made, record the exception text.
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
* **The joint cell (row 2, Start) is unmeasured.**  The row pair and the column pair are separate; no pair crosses them.  ~~The writer accepts it by
  the rule, and no fixture tests it.~~  Corrected by W36-1b.1 and W36-1b.2 (R36G1-2): the writer and the app now refuse it ("is not a measured
  cell"; `test_bold_cell_pairs` and `app_edits_cell_bold` assert the refusal), and `spec/writers/view-stream-214.md` says the same.
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
names a field, never a region, and there are no single-run ceilings (the control mask broke the first half of it until W36-0c.5, and two fields absorbed any size until W36-0c.3 and -0c.4: R36G0b-1 to -3).  Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0b.1 | R36G0-1, -6 | `916ac5b` | the whole-view-bag window of `214/CV_iew/Var2Data` is gone; the only noise left there is the font word's top byte (payload +43), found by parsing the bag blocks VarMeta locates; windows come from each side's own VarMeta |
| W36-0b.2 | R36G0-2 | `6082441` | no size tolerance, but for the SummaryInformation string slot and the Props14 tail count, which absorbed any size until W36-0c.3 and -0c.4: `214/CEdl/Var2Data`, `Props14` and `SummaryInformation` are parsed and their decoded fields replaced by placeholders in a canonical form compared byte for byte; the acceptance sweep enters the ctest |
| W36-0b.3 | R36G0-3 | `e2602fb` | stream names match whole (no `endswith`); `DocumentSummaryInformation` is compared in full |
| W36-0b.4 | R36G0-4 | `1a7a4d8` | every noise window is a named field (the control mask was not, until W36-0c.5), cited by row of `spec/writers/evidence/trace-gate-fields-g0b.md`: GUIDs only when both sides are version 1 or zero, the creation stamp only of a row the control lacks, the view GUID string at payload +0 |
| W36-0b.5 | R36G0-5 | `0e96af7` | no single-run ceilings (`ceiling`, `rows`, `ceiling_for`) |
| W36-0b.6 | R36G0-7 | `6b7f690` | `vmx`'s reply-length check accepts the Out-String reply shape (the announced length counts the trailing newline); the stub models it |
| W36-0b.7 | R36G0-8 | `6279a9e` | the flip test covers every offset outside the named fields in three pairs, and six planted windows; this found the `_meta_blocks` hang (a flipped VarMeta count looped 2^32 times) |
| W36-0b.8 | R36G0-9 | `c1a5cd5` | stride masks follow a row's identity (uid word, or the FixedData item a Fixed2Data row follows), not its position; a row either side lacks, or a uid twice, has no mask |
| W36-0b.9 | R36G0-10, -11 | `285da21` | the three overstated sentences corrected; `gates-g0.md` committed; the standing facts restored verbatim into `history-waves34-35.md` |
| W36-0b.10 | CI | `712b40d` | both ctest-running CI jobs install `olefile` so `trace_gate_guards` runs; the test carries a 300 s TIMEOUT |

**Acceptance (the committed ctest `trace_gate_guards`).**  Negative: TRACED = the control's null save, REFERENCE = the edit, over every committed
pair whose edit differs from its control-null outside `SummaryInformation`: **87 of 89 FAIL** at `c1a5cd5` (183 of 185 over the committed file set at the G0b tip `cea3c6d`, R36G0b-5, -7); the two that PASS (`wave28-percent-auto100`,
`wave28-percent-auto40`) are no-op edits equal to the control outside the noise.  Positive: TRACED = the control's null save, REFERENCE = the control:
**97 of 99 PASS** at `c1a5cd5` (189 of 196 at `cea3c6d`); the two that FAIL (`wave30-inactivate`, `wave35-g3-assign-remove-port-saved`) have a control and null that are not one state
(`214/CEdl/Var2Data` record structure differs), and no mask was widened.  Before G0b the negative sweep let 8 of 89 PASS.

**Gate line.**  At `712b40d`, core tree `build/w35g-close-fresh`: build exit 0; `ctest -E plan_recalc -j4` 67/67 passed, including `trace_gate_guards`
(219 checks, 15 s) and `trace_host_scripts` (`build/w36/285da21-W36-0b.10-core-ctest.log`).  Every commit: `check_fixtures` 0, `check_markers` 0,
`guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; the G0b commits carry `Co-Authored-By` and not the
`Generated-By` trailer rule 1 requires (R36G0b-11; history is not rewritten, G0c's commits carry it).  Per-item logs are in `docs/handoff/wave36-verification/gates-g0b.md`.  G0b changes no C++ source.  The builder did not touch the VM
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
* **Task Fixed2Data +42 and +46..49 are data**, not noise, in the gate; they are NOT equal in every committed control/null pair (they differ in
  `wave30-baseline` and `wave30-wbs`, committed before G0b; R36G0b-12).  G0b's control mask excused them there; since W36-0c.5 nothing does, and
  those pairs are OPEN.  The old VM null run may have seen noise there; the coordinator's re-gate would then FAIL on it, which is a finding by
  design (name the field with a measured offset, never a region).
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

## G1b: the font writer's row and field rule, narrowed to what was measured

Brief `docs/handoff/wave36-brief.md` section 2 (G1b); findings R36G1-1 to -7 of the G1 review (`docs/handoff/wave36-verification/review-g1.md`).  Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-1b.1 | R36G1-1 | `5cb0f36`, `e749d81` | Bold ON is limited to the three cells a committed pair measures: Task Name of rows 1 and 2 and Start of row 1 (`bold_on_cell_is_measured`); every other cell is refused with "is not a measured cell"; the spec, the writer's header and the tests no longer claim a held-out pair for a row/field rule; `e749d81` corrects the G1 table's W36-1.2 row in place |
| W36-1b.2 | R36G1-2 | `166734a` | records only: the G1 "Found, not fixed" line on the joint cell (row 2, Start) is struck through and corrected; the writer and the app refuse it |
| W36-1b.3 | R36G1-3 | `a0f3d32` | `save_view_cell_style` checks that the row's uid is in the opened file's Task table (`require_task_row`, through `TaskTable::from_document`) before it writes; a row that is not there throws and writes nothing |
| W36-1b.4 | R36G1-6 | `f87f271` | the Qt gate covers B and I with Undo and Redo against Save, and a read-only file (the status line gives the read-only reason) |
| W36-1b.5 | R36G1-7 | `3cc5a9d` | a second font click before Save says why it was refused: `app::queued_font_edit_refusal` names the queued edit and says to Undo it first; both shells call it |
| W36-1b | R36G1-4 | `f803924` | records only: Method A of the 4(e) plan enumerates `Application.ActiveCell` and the `Cell` interface, before Method B; whether `Cell` has a Bold or Italic getter in Project 2021 is recorded as unverified |
| W36-1b | R36G1-5 | open | the writer of the Props14 marker byte is not named in the traces; see Deferred |

**What the writer accepts now.**  Bold ON: (Task Name, row 1), (Task Name, row 2), (Start, row 1), each matching its own committed pair, and only for a row
whose uid is in the opened file's Task table.  Bold OFF: (row 1, Task Name).  Italic: (row 1, Task Name), on.  Everything else is refused before any
byte is written.  There is no held-out pair for a row/field rule, and the spec says so.  The round-7 shapes second-cell, second-change, bold-italic,
range2 and underline-toolbar are each recorded "not lifted: needs <fact>" in `spec/writers/view-stream-214.md`.

**Gate line.**  Group run at `f803924` (the last source-changing commit; `e749d81` changes records only), logs `build/w36/f803924-W36-G1b-group-*`:

| gate | result |
|---|---|
| core build (`build/w35g-close-fresh`, Release) | exit 0 |
| core `ctest -E plan_recalc -j4` | **66/67**: `trace_gate_guards` fails; every other test passes (cause below; `build/w36/f803924-W36-G1b-group-core-ctest.log`, rerun alone in `build/w36/f803924-W36-G1b-trace_gate_guards-rerun.log`) |
| `MPP_NO_CORPUS=1 plan_recalc` | 1/1 |
| M2 recalc `--compare` | exit 0; 332/332, 268/268, 64/64 fields |
| `check_fixtures` | exit 0 |
| app tree (`build/w35g-close-fresh-app`) ctest | 34/34 |
| Qt tree (`build/w35g-close-fresh-qt`) ctest | 52/52 |

`trace_gate_guards` fails with `FileNotFoundError tests/data/wave23-flags-control.mpp`: commit `3193808` added ten `*-control-null.mpp` fixtures whose control is not
`<stem>-control.mpp`, and `5939f6d` merged them into `wave36` after G0b's 67/67.  None of the G1b commits touches `tools/` or `tests/data`.  It is G0b's tool
and is not fixed here (Found, not fixed).  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the
`client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each commit carries both trailers.  The builder used no COM, did not touch the VM
or `build/trace-2021/`, edited no committed fixture, and pushed nothing.  Per-item logs are in `docs/handoff/wave36-verification/gates-g1b.md`.

### Guards seen red

Each red ran on the parent commit's code with the new tests; the green is the same tests on the new code.  Logs are `build/w36/<parent>-W36-1b.N-{red,green}-*`.

| item | red | green |
|---|---|---|
| 1b.1 | core `viewstream`: 15 to 16 FAIL lines, for example `FAIL: Bold on row 3, field 188743694 refused: no exception was thrown`; app `cell_bold`: 6 failures, for example `FAIL: Bold on the joint cell (row 2, Start)` | core `viewstream` 1/1, app `cell_bold` 1/1, Qt 2/2; the two session2 `EXPECTED FAIL` lines remain by design |
| 1b.2 | records only; no red | scans clean |
| 1b.3 | 9 FAIL lines, for example `Bold on a row the Task table does not hold refused: no exception was thrown`, three `document changed` and an `an output file was written` | core `viewstream` 1/1, core `view|style|bold|font` 8/8, app `cell_bold|view|edit` 17/17 |
| 1b.4 | two mutations of `app_qt/main_window.cpp` (the Undo handler's `structural_.undo()` replaced by a no-op; the read-only guard bypassed): 26 FAIL lines, 24 of them Undo/Redo, and `FAIL: read-only B/I: the status line gives the read-only reason` | Qt `cell_bold|qt_fat_slack` 2/2 |
| 1b.5 | app, `queued_font_edit_refusal` stubbed to return nothing: `FAIL: a queued Bold gives the Undo-first reason`, `a queued Bold off is named as Bold off`, `a queued Italic gives the Undo-first reason` (3 FAILURE(S)); Qt, the shell change backed out: four FAIL lines (`a second Bold click says to Undo the queued Bold first`, `Italic over a queued Bold ...`, `a second Italic click ...`, `Bold over a queued Italic ...`) | app `cell_bold|edit` 15/15, Qt 2/2 |
| R36G1-4 | records only; no red | scans clean |

### Deferred

* **R36G1-5 (the Props14 marker byte's writer) stays open, as a record line.**  The writer is `write_edit_marker` in `core/mppwrite/viewstream_edit.cpp`, shared with
  earlier waves.  `trace-bold.md` treats Props14 as whole-stream noise, and neither `trace-bold-r4.md` nor `trace-font-item-r6.md` names the instruction that writes
  the marker byte, so clause (a) of the lift rule rests on the pairs for that one byte, not on a trace.  The pairs match it field by field on the one-session
  saves; on the reopened session2 pair Project keeps 0 where the port writes 1 (offset 402), a named `KnownGap` expected failure in
  `tests/roundtrip_viewstream.cpp`.  The builder has not looked for a trace outside those three files that names it; naming the writer needs a trace, and the
  trace VM is outside this group.

### Found, not fixed

* **`trace_gate_guards` fails on `wave36`** since the merge at `5939f6d`: the ten `*-control-null.mpp` fixtures of `3193808` have controls the test's `<stem>-control.mpp`
  rule does not find.  It needs a pair-to-control map (as `NULLS` in `scripts/oracle_r7_nulls.py` has) in `tools/trace/test_trace_gate.py`.  The ten pairs are not
  covered by the gate until then.  This is G0b's tool; flagged to the coordinator.
* **Session2's two named gaps have no rule**: `214/CEdl/Var2Data` grows 38 bytes on Project's reopened save (first difference at 392; port 0x00, Project 0x1a), and the
  Props14 marker (402; port 0x01, Project 0x00).  Both are `KnownGap` expected failures, not masked.
* **No held-out pair exists for the row/field rule.**  The three measured Bold cells each match their own pair; nothing predicts a fourth.  The Bold OFF, Italic and
  `not lifted: needs <fact>` shapes (second-cell, second-change, bold-italic, range2, underline-toolbar) stay refused.
* **4(e) has not run**, and the `Cell` Bold/Italic getter in Project 2021 is unverified (R36G1-4 records it; the coordinator runs the read-back).
* **`app/main.mm` (`fontCellItalic:`) is changed but not built here**: macOS is CI-gated.  The change is the same few lines as the Qt shell's, calling the shared
  `queued_font_edit_refusal`; the app-tree test covers the function, and the first CI run covers the shell.
* **Save with an empty queue writes no file** (status line "nothing to save").  The 1b.4 test asserts exactly that after Undo; the G1 reviewer expected a
  byte-equal file.  Product behaviour, not changed here.
* **The Task-table check (1b.3) takes "uid in FixedData rows 1.." as existing.**  It has not been run against a file with deleted-slot rows; a deleted row that keeps its uid
  would count as present.
* **`python -I` still cannot import `olefile`** here (user site-packages); the scripts that need it run without `-I` and read only committed files.

WAVE-36-G1b-COMPLETE

## G0c: the trace gate, second fix

Brief `docs/handoff/wave36-brief.md` section 2 (G0c); findings R36G0b-1 to -12 of `docs/handoff/wave36-verification/review-g0b.md`.  Design rule: a mask
names a field, never a region, and a masked field is accepted only when both sides hold a well-formed value of the named kind.  Items, one finding id per
commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0c.1 | R36G0b-5, -7 | `4352eb7` | the test runs on the committed file set (624 `.mpp`, 202 control-nulls): a null's control comes from one table, a null with no control is a FAIL line, never a crash; the positive sweep lists the pairs that fail it as OPEN, each with its stream and reason |
| W36-0c.2 | R36G0b-4 | `7a23ab7` | a GUID field is noise only when both sides are version-1 GUIDs; all-zero against a real GUID FAILs |
| W36-0c.3 | R36G0b-3 | `4c854f1` | a `SummaryInformation` string slot is exactly type, length, string and zero padding to 4; bytes after the string FAIL |
| W36-0c.4 | R36G0b-2 | `a4a7ab7` | the `Props14` tail entry 0xffffffff is compared like 0x3540000b: its header stays, only a text name is replaced, it may be absent on one side only as the named case |
| W36-0c.5 | R36G0b-1 | `9d930c6` | the control mask no longer excuses a byte no field names: it is diagnostic (`control-mask <stream> <n>`) and a problem; nothing was decoded or widened, so 43 more pairs are OPEN (56 in all) |
| W36-0c.6 | R36G0b-6 | `5e47783` | the test holds its own parse of the five parsed streams and the font rows; 12 planted guard removals each go red; `--gate-dir` runs the whole test on a mutated gate |
| W36-0c.7 | R36G0b-9 | `a35cde2` | `vmx` reports the agent's `ERR` text, not a length error; a reply over 300 characters is modelled (no wrap, measured in PowerShell 7.7.0 and 5.1.26100.9818) |
| W36-0c.8 | R36G0b-10, -12 | `3c92b13` | the head line's "not checked" counts the streams not compared (one file only, another size); the overstated sentences in the gate and test docstrings, this record, `gates-g0b.md`, and the item id in `ci.yml` and `CMakeLists.txt` |

**Acceptance (the committed ctest `trace_gate_guards`, on the committed file set).**  Negative: TRACED = the control's null save, REFERENCE = the edit, over 191
edits: **191 of 191 FAIL** at `9d930c6` and after (189 of 191 at `4352eb7`: the two no-op percent edits passed until the control mask stopped excusing).
Positive: TRACED = the control's null save, REFERENCE = the control, over 202 control-nulls: **146 PASS and 56 OPEN** (13 OPEN at `4352eb7`; the 43 added at `9d930c6`
are bytes no field names, now FAILs by design).  An OPEN pair is reported as OPEN, never as a PASS, and leaves the list only by passing.  Hand-built wrong files: the
review's cases for R36G0b-1 to -4 and the twelve of R36G0b-6 all FAIL.

**Gate line.**  At `3c92b13`, core tree `build/w35g-close-fresh`: build exit 0; `ctest -C Release -E plan_recalc -j4` 67/67 passed, including `trace_gate_guards`
(335 checks, 45 s) and `trace_host_scripts` (`build/w36/3c92b13-W36-G0c-core-ctest.log`).  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0,
`scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each carries both trailers.  Per-item logs and counts are in
`docs/handoff/wave36-verification/gates-g0c.md`.  G0c changes no C++ source (one comment in `CMakeLists.txt`).  The builder used no COM, did not touch the VM or
`build/trace-2021/`, and pushed nothing.

### Guards seen red

Each red ran on the previous commit's tool (or a mutated copy), then the green on the new one; logs are `build/w36/<parent>-W36-0c.N-red.log` and `-green.log`.

| item | red | green |
|---|---|---|
| 0c.1 | the committed test: 103 PASS lines, then `FileNotFoundError tests/data/wave23-flags-control.mpp` | 221 PASS, 0 FAIL |
| 0c.2 | every GUID zeroed in the traced, in the reference, or one of them: 30 FAIL on the 0c.1 gate, e.g. `???114/Props: every GUID zero in the traced fails (2 GUIDs)` | 253 PASS |
| 0c.3 | 240 zero bytes after the string, 240 junk bytes, a nonzero padding byte, the wrong padding length: 12 FAIL | 273 PASS |
| 0c.4 | junk in the 0xffffffff name, a second 0xffffffff entry (non-text and text name): 3 FAIL | 287 PASS |
| 0c.5 | seven committed files with the control/null bytes inverted each had 0 problems on the 0c.4 gate: 13 FAIL (the OPEN list stale for 43 pairs, the negative sweep, three identity cases) | each of the seven FAILs with `control-mask <stream> <n>`; 304 PASS |
| 0c.6 | 12 planted removals (every SummaryInformation property a placeholder; any 12-byte FILETIME as LastSave; `_text_ok` always true; the CEdl path record waived; the CEdl size words zeroed; 0x00010007 zeroed; the Props14 path record waived; the tail names waived; every tail entry dropped; 114/Props GUIDs masked without the version-1 test; the view GUID string with one side; the font byte from any 44-multiple record): all red, 1 to 9 FAIL each | 329 PASS on the real gate |
| 0c.7 | the new test on the old `host.ps1`: `FAIL host.ps1 agent ERR reply: throws the agent error text, not a length error` and the two-piece one, 2 failed | 19 PASS |
| 0c.8 | the new test on the 0c.7 gate: a missing, an extra and a longer stream each read `0 not checked` (3 FAIL) | 335 PASS |

### Deferred

None.  Every G0c item is fixed, except that **R36G0b-8** goes to the record as open (below), as the brief says.

### Found, not fixed

* **R36G0b-8 is open.**  Every sweep and hand-built case runs on committed files; no file the real Project 2021 wrote in a trace has met the gate.  Only the coordinator's
  re-gate of the recordings settles it.
* **R36G0b-11: the G0b commits carry `Co-Authored-By`, not the `Generated-By` trailer rule 1 requires** (0 of 11).  History is not rewritten; the operator decides whether to
  before the merge.  Every G0c commit, and the three G0 commits, carry both.
* **56 of 202 control-nulls are OPEN**, 12 streams, each with a "not decoded" line in `SIZE_OPEN`, `MASKED_IN` or `MASKED` in the test (the sort table 0x26400024 and the 26-byte
  record 0x2640003d of `214/CEdl/Var2Data`; the save words of `214/CV_iew/Var2Data` and `114/Props`; Assn and Rsc meta items; Assn and Task Fixed2Data bytes that are data).  Direction:
  decode a field and name it with its evidence row, or re-record the pair.
* **A zero placeholder collides with a traced field that is itself 0 and fails its derivation rule.**  The canonical forms replace a derived field (the CEdl size words,
  `214/Props` record 0x00010007, `CEdl/VarMeta` +20) with 0 only when it equals what the stream implies; a traced field that is 0 and wrong stays 0, and the reference's placeholder is
  0, so the two compare equal.  The window audit and the new cases catch it only where the true value is not 0.  Not fixed here; direction: a distinct placeholder.
* **Two of the twelve planted removals (7 and 9) are caught by one check each** (the Props14 path-record case; the positive sweep).  A second case for each is cheap.
* **`host.ps1` takes the review's smaller fix.**  The `ERR` prefix is reported as the agent's error.  The review's fuller one (the agent sends the trimmed base64 length and its own sha in
  `out_meta`, and the host compares exactly) is not done: it changes `agent.ps1`, which the frozen VM copy does not follow until the coordinator copies it in.  The no-wrap result and the `ERR`
  case are confirmed on the host in both PowerShell editions, not on the VM; the coordinator's `vmx 'Get-Date'` plus one reply over 300 characters settles it.
* **The VM still runs the frozen gate, `host.ps1` and `agent.ps1`** until the coordinator copies the new ones in; the re-gate of the recordings follows.
* **`python -I` still cannot import `olefile`** here (user site-packages); the trace tests run without `-I` and read only committed files.

WAVE-36-G0c-COMPLETE

## G0d: the trace gate, third fix

Brief `docs/handoff/wave36-brief.md` section 2 (G0d); findings R36G0c-1 to -11 of `docs/handoff/wave36-verification/review-g0c.md`.  Design rule, restated from G0c's: a masked field is
accepted only as a value a save writes, on both sides, and a field derived on one side only is a FAIL.  Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0d.1 | R36G0c-1 | `1956d20` | a derived field is told from a raw one: a size word of 0 no longer equals the placeholder (`derived <stream> <field>` on one side only FAILs) |
| W36-0d.2 | R36G0c-2 | `6a7d4f4` | streams are keyed by their raw directory name: a collision of printable names FAILs, a renamed stream FAILs, the head line counts directory entries |
| W36-0d.3 | R36G0c-3 | `527a951` | the view GUID string is noise only as a version-1 GUID with a timestamp (the binary test, on the parsed string) |
| W36-0d.4 | R36G0c-4 | `b239c79` | the `_lpstr_slot_ok` type check is tested and seen red with it removed |
| W36-0d.5 | R36G0c-5 | `b13b86c` | the Props14 first-entry, blob size-word and 2048-byte guards are tested and seen red |
| W36-0d.6 | R36G0c-6 | `89c5fb4` | a masked field holds a value a save writes: stamps and save words 32 bits, not 0, not all-ones; LastSave a FILETIME after 2000; the strings at least a NUL, ending in a NUL, no control character; bounds from the 624 committed files |
| W36-0d.7 | R36G0c-7 | `f517eb2` | each of the 56 OPEN pairs is pinned as (pair, stream, count): 170 rows; two wrong "data, never masked" reasons corrected |
| W36-0d.8 | R36G0c-4, -5 | `ea5e70d` | `test_gate_guards_meta.py` removes each named guard in turn in a copy of the gate and the test goes red on each: 60 listed, 60 red at this commit |
| W36-0d.9 | R36G0c-8 | `a04d135` | `_text_ok` rejects every Unicode C category (C1 controls, format, private use, unassigned); the meta-test lists 61 |
| W36-0d.10 | R36G0c-11 | `f48beba` | the gate docstring states what the waiver is and what it is not |

**Acceptance (the committed `tools/trace/test_trace_gate.py`, on the committed file set).**  Negative, over 191 edits: **191 of 191 FAIL**.  Positive, over 202 control-nulls: **146 PASS and
56 OPEN**, the 56 pinned as 170 (pair, stream, count) rows.  Both counts are those of G0c and unchanged at every commit.  Every hand-built wrong file of the G0c review FAILs.  The meta-test
(61 guards removed in turn): **`guards listed 61, red 61 (60 by a failing case, 1 by a crash), not red 0`**.  The test is 491 checks (G0c: 335), 0 FAIL.

**Gate line.**  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only;
each carries both trailers.  The full default run (the test, then the meta-test) took 486 s at 0d.8 and passed again on the 0d.9 tree (`build/w36/ea5e70d-W36-0d.9-green.log`); 0d.10 is a
docstring, and the test alone (`--no-meta`) passed on it (491 PASS).  **The ctest itself was not re-run at the tip:** the build tree is shared and needs a reconfigure for the new
`TIMEOUT` (300 s to 3600 s, the one change to `CMakeLists.txt`, which is why the 0d.8 doc gates print SOURCE CHANGED); the ctest runs the same script with no arguments.  G0d changes no C++
source.  Per-item logs and counts are in `docs/handoff/wave36-verification/gates-g0d.md`.  The builder used no COM, did not touch the VM, `build/trace-2021/` or `build/r7/`, and pushed nothing.

### Guards seen red

Each red ran on the previous commit's gate or on a copy with one guard removed (`build/w36/mutants.py`, then the committed meta-test); logs are `build/w36/<parent>-W36-0d.N-red.log` and `-green.log`.

| item | red | green |
|---|---|---|
| 0d.1 | five derived words set to 0, both sides; the empty path records: 14 FAIL | 357 PASS |
| 0d.2 | the collision on both sides and both renamed cases returned rc 0: 6 FAIL of 10 | 367 PASS |
| 0d.3 | zero digits, version, variant, zero timestamp: 10 FAIL | 381 PASS |
| 0d.4 | the type clause removed: 12 FAIL | 399 PASS |
| 0d.5 | `marks[0]` 2 FAIL, the size-word cross-check 2 FAIL, the 2048 cap 1 FAIL | 408 PASS |
| 0d.6 | 52 FAIL, 9 PASS (equality and noise cases) | 469 PASS |
| 0d.7 | the printed control-mask count + 1: 9 FAIL (was 8) | 471 PASS |
| 0d.8 | the meta-test's first run: 59 of 60 red, **`stream-count` green** (the head line counting printable names: no case read the head count of a renamed stream); a check added, then 60 of 60 | 473 PASS |
| 0d.9 | four character classes in two records, two sides, and the refusal check: 17 FAIL | 491 PASS, 61 of 61 red |

### Deferred

None.  R36G0c-9 and R36G0c-10 (Low) are queued below, as the brief allows for a fix that is not one line.

### Queued (Low, not fixed)

* **R36G0c-9.**  Three of the `named_absent` conditions of `_canon_props14` (the text test, the id test, `len(entries) >= 1`) are redundant: the comparison with the reference's canonical form
  catches what they miss, and no committed tail shape isolates one (the review's 10 of 10 shapes give the same verdict with and without).  The meta-test lists none of them, as a removal
  changes no verdict.  The SummaryInformation noise range "to the end of the slot" is verdict-neutral (it affects `with_noise` only); a case with an Author string that needs padding would
  isolate it.  Direction: drop the redundant conditions, or find a shape where each matters.
* **R36G0c-10.**  The control null no longer changes any verdict: it only labels a failing byte `control-mask` or `bytes-differ` and refuses a missing null, so a fake null (the control with
  one SummaryInformation byte changed) gives the same verdict as the real one.  `runone6.ps1` and the "no null" guard therefore demand a file that proves nothing.  Direction: drop the null from
  the command line and the specs, or make "no null" mean "no measured noise" by checking that the null differs from the control in SummaryInformation and Props only.  Needs the coordinator's
  decision, because it changes the capture specs.

### Found, not fixed

* **R36G0c-11, the G0c record's own overstatements.**  The G0c record and `gates-g0c.md` say a masked field is "accepted only when both sides hold a well-formed value of the named kind"; that
  was not true of R36G0c-1, -3, -6 and -8 until this group, and the G0c record counts twelve planted removals where the brief says eleven.  The gate docstring is corrected (0d.10); the G0c
  records are not rewritten.  What the tests hold now is the acceptance above.
* **R36G0b-8 is still open** and **R36G0b-11 is as G0c recorded it** (the G0b commits carry `Co-Authored-By`, not `Generated-By`; history is not rewritten; every G0c and G0d commit carries both).
* **The review's "version digit index 15" is time_hi's high nibble.**  In the rendered string the version digit is index 17 and the variant digit index 20; the test changes 15, 17 and 20 and
  expects 15 to stay noise (W36-0d.3).
* **The review's "33 bytes of 0xff" example stays noise**: it is a valid Windows-1252 string.  The slot is bounded by its form (a NUL, no control character), not by what a user could be called.
* **The font byte has no bound**: 24 committed bytes, 8 distinct values (0, 52, 96, 107, 110, 144, 192, 215), an uninitialised allocator byte with no pattern to test.
* **The 56 OPEN pairs are still OPEN**, pinned to (pair, stream, count) but not decoded.  Two reasons that called per-save values "data, never masked" were wrong and are corrected (0d.7).
* **`_text_ok`'s "unassigned" set is the running Python's `unicodedata`**; a character assigned in a newer Unicode than the runner's would be refused there.  The committed names are not affected.
* **One guard is red by a crash, not a FAIL line** (`guidstr-shape`: a non-hex digit reaches `uuid.UUID` in the gate and the test dies with a `ValueError`); the meta-test reports it as such.
* **The meta-test lengthens the ctest** from about 36 s to about 8 minutes on this machine (12 cores, six copies at a time); the `TIMEOUT` is 3600 s.  `--no-meta` skips it.

WAVE-36-G0d-COMPLETE

## G0e: the trace gate, fourth fix

Brief `docs/handoff/wave36-brief.md` section 2 (G0e); findings R36G0d-1 to -6 of `docs/handoff/wave36-verification/review-g0d.md`.  Design rule, restated: a masked field is accepted only as a
value a save writes, on both sides; a field derived on one side only is a FAIL; and a guard is shown by a removal that turns the test red, found by the meta-test and not by a list.  Items, one
finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-0e.1 | R36G0d-1 | `0c0563e` | the meta-test derives its candidate guards from `trace_gate.py` (`ast`): 208 candidates, 196 red, 12 redundant with a red cover and a reason, 0 green; four guards the review found green have cases; a crash is a named FAIL |
| W36-0e.2 | R36G0d-2 | `ebbbb9a` | a masked value is accepted only inside the span the 624 committed saves show (stamps, LastSave) and the two Props save words must be equal; the six review files FAIL |
| W36-0e.3 | R36G0d-3 | `e95c295` | an emptied Props14 tail name is not a name (`_name_ok`) and each tail name is a derived field; both names emptied FAILs |
| W36-0e.4 | R36G0d-4 | `88488a1` | the meta-test labels `control-mask-count` as a pinned number, not a guard removal (the crash half was 0e.1) |

**Acceptance (the committed `tools/trace/test_trace_gate.py`, on the committed file set).**  Negative, over 191 edits: **191 of 191 FAIL**.  Positive, over 202 control-nulls: **146 PASS and
56 OPEN**.  Both counts are those of G0d and unchanged at every commit.  Everything G0d accepted stays green (the 491 checks are in the test, now **652 checks, 0 FAIL**).  Every hand-built wrong
file of the G0d review FAILs.  The meta-test: **`named guards listed 67, red 67, not red 0`; `candidates derived 214 (run 214): removed red 202 (61 of them by a case that asserts the gate does
not raise), listed redundant 12, green 0, crashed 0`**, the unmodified gate passes (`build/w36/ebbbb9a-W36-0e.3-meta.log`).

**Gate line.**  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each
carries both trailers.  The full default run (the test, then the meta-test) took about 25 minutes with 8 jobs on the 0e.3 tree and passed; the test alone (`--no-meta`) passed on the tip, 652 PASS
(`build/w36/88488a1-W36-0e.tip-green.log`); 0e.4 is one description.  The ctest itself was not re-run at the tip (the build tree is shared); it runs the same script with no arguments, and its
`TIMEOUT` of 3600 s is unchanged.  G0e changes no C++ source and no `CMakeLists.txt`.  Per-item logs and counts are in `docs/handoff/wave36-verification/gates-g0e.md`.  The builder used no COM, did
not touch the VM, `build/trace-2021/` or `build/r7/`, and pushed nothing.

### Guards seen red

Each red ran on the previous commit's gate or on a copy with one guard removed; logs are `build/w36/<parent>-W36-0e.N-red.log`, `-green.log` and `-meta.log`.

| item | red | green |
|---|---|---|
| 0e.1 | the four review guards (`tail-count`, `si-section-size`, `si-first-value`, `si-lastsave-type`) against the unchanged test: 0 of 4 red; the first derived run found 76 green and 1 crash | 588 PASS; 65 of 65 named red, 208 derived (196 red, 12 redundant, 0 green, 0 crashed) |
| 0e.2 | the new test against the parent's gate: 42 FAIL, 602 PASS; the first full run found `new-row-other` and three candidates green (the span made them verdict-neutral) | 641 PASS; 66 of 66 named red, 212 derived (200 red, 12 redundant, 0 green, 0 crashed) |
| 0e.3 | the new test against the parent's gate: 8 FAIL, 644 PASS; the first full run found three NOFIT (a rename) and `tail-ffff-dropped` green (the derived flag caught its old case alone) | 652 PASS; 67 of 67 named red, 214 derived (202 red, 12 redundant, 0 green, 0 crashed) |
| 0e.4 | none: a description | `control-mask-count` red by the W36-0d.7 case |

### Deferred

None.

### Queued (Low, not fixed)

* **R36G0d-5 (R36G0c-10, the control null proves nothing).**  Not fixed: the coordinator's call, as the review says, because it changes the capture specs.  The control null is still demanded by
  the command line and by `runone6.ps1`, and the gate uses it only to label a failing byte (`control-mask` or `bytes-differ`) and to refuse a missing one; the sweeps and every verdict are the same
  with a fake null.  A reader of "no null is a FAIL" should not take the null to be measured.  Direction as G0d recorded it: drop the null from the command line and the specs, or make "no null" mean
  "no measured noise" by checking that the null differs from the control in SummaryInformation and Props only.
* **R36G0c-9, as evidence.**  The derived meta-test lists every `named_absent` condition of `_canon_props14`: the id test (`entries[0][0] == 0x3540000b`), the name test (`_name_ok`) and
  `len(entries) == 1` are each **red** when removed (W36-0e.1 cases, and for the name W36-0e.3), and none is on `REDUNDANT`; the review's "redundant, not listed" no longer holds.  What
  stays queued is the SummaryInformation noise range "to the end of the slot", which is verdict-neutral (it affects `with_noise` only) and is not isolated by any committed shape.

### Found, not fixed

* **R36G0d-6, the G0d record's own statements.**  "Props noise words and LastSave are of their decoded kind" was a kind, not a value (R36G0d-2 turns it into a value: the span); the 61 of "61
  guards" was a count of a list (R36G0d-1 derives the candidates); the `0d.8` commit message says 60 and the record 61, explained in the record.  The G0d records are not rewritten; what the tests
  hold now is the acceptance above, and `gates-g0e.md` states its counts from the logs.
* **The spans make a post-span capture FAIL.**  A stamp or LastSave outside the span of the 624 committed files (stamps 0x3d141482 to 0x3d282db4, LastSave 22 October to 11 November 2026)
  FAILs as data until the two spans in the gate, the test's copy and the table of `spec/writers/evidence/trace-gate-fields-g0b.md` are widened together.  The coordinator is re-gating the
  recordings: a verdict that turns there is this, not a regression.
* **The meta-test is 25 minutes, not 8** (214 derived candidates and 67 named, eight at a time); the ctest `TIMEOUT` is 3600 s.  `--only named|derived` prints a partial table and cannot check
  `REDUNDANT` (it raises a `KeyError` on a key not run); a full run is the acceptance.  The test files must not be edited while a meta run is in progress: the test is read for each removal.
* **Three dead clauses were deleted from the gate at 0e.1** (`len(payload) % 2` in `_text_ok`, `digits and` in `noise_windows`, `nul is not None and` in `main`): each was never taken on any
  input the gate can see; every committed pair's verdict is unchanged.
* **The two Props save words must be equal** (0e.2): true of all 624 committed files (0 unequal).  A save that wrote two different words would FAIL until the evidence note says why.
* **The 56 OPEN pairs are still OPEN**, pinned to (pair, stream, count) but not decoded; `R36G0b-8` is still open and `R36G0b-11` is as G0c recorded it.

WAVE-36-G0e-COMPLETE

## G0f: the trace gate, fifth fix

Brief `docs/handoff/wave36-brief.md` section 2 (G0f); the machine and session fields the first re-gate of the VM's recordings found (`build/trace-2021/regate/DOSSIER.md`, `CAUSES.md`).  Design rule, restated: a
masked field is accepted only as a value a save writes, on both sides, from evidence in the DOSSIER or the 624 committed saves; a field the evidence calls data is never masked; a field derived on one side only is a
FAIL; and a guard is shown by a removal that turns the test red, found by the meta-test and not by a list.  Items, one finding id per commit, in order:

| item | commit | what |
|---|---|---|
| W36-0f.1 | `643d1c4` | Props14: the tail names are read by their length words (10, 16 and 30 bytes), and the saved-flag record 0x3540000a is a bounded session field |
| W36-0f.2 | `64565cf` | CEdl slot table: the uids are compared one by one; only the 0xffffffff fill is a session field |
| W36-0f.3 | `58ed375` | note rich text: the language id and the generator build of the header are machine fields; the text is compared |
| W36-0f.4.1 | `41a6be3` | CEdl: the three date words and the view word are session fields, each only as a value a save writes |
| W36-0f.4.2 | `ccad382` | CV_iew: the view stamp and three view date words are session fields |
| W36-0f.4.3 | `96eca67` | the span of a masked save stamp reaches the VM's latest decoded stamps, 0x3d28352a |
| W36-0f.4.4 | `f26da8f` | Props: the baseline-slot words only as a copy of the save word; the third save GUID only as a version-1 GUID |
| W36-0f.4.5 | `d2227a1` | the view GUID of every CTable and CV_iew FixedData row, as a version-1 GUID on both sides |
| W36-0f.4.6 | `a81fe68` | the third GUID of an assignment's Fixed2Data row, +32..+47, as a version-1 GUID |
| W36-0f.4.7 | `cf3d094` | the creation stamp of every Task row, assignment block and resource block, as a stamp inside the span |
| W36-0f.4.8 | `64c173a` | the +18 and +38 words of a 76-byte baseline sub-record and its heap bytes, as stale process memory |
| W36-0f.4.9 | `ed3d859` | Task FixedMeta +315, Rsc FixedMeta +140/+177 and Task Fixed2Data row +58 are data, not masked: each pinned to FAIL (deferred, with the evidence) |
| W36-0f.4b | `11875ed` | the Gantt pane width (u16 at +1172 of the 41363-byte view-state record) and the unit beside it (+1180) |
| W36-0f.5 | `a090ed9` | the 15 derived removals that stayed green: 11 get a case, 4 go on `REDUNDANT` with a red cover |
| W36-0f.6 | `2922b56` | the DOSSIER's 13 data targets as 22 field cases, each built from the reference with the VM's value, each FAILs |
| W36-0f.5 (again) | `217f3e6` | two named guards (`derived-cedl-path`, `derived-view-size`) found NOFIT by the tip run are anchored again; both red |

**Acceptance (the committed `tools/trace/test_trace_gate.py`, on the committed file set).**  Negative, over 191 edits: **181 FAIL and 10 PASS**, the ten being edits that equal their null or control in every byte the gate
checks, each pinned by name.  Positive, over 202 control-nulls: **180 PASS and 22 OPEN** (G0e: 146 and 56), the 22 pinned as (pair, stream, count), 107 rows.  Everything G0e accepted stays green: the 652 checks of G0e are
in the test, now **1739 checks, 0 FAIL**.  Every hand-built wrong file of the G0b to G0d reviews still FAILs.  The meta-test: **`named guards listed 184, red 184`** (182 red in the tip run and two NOFIT, repaired and red at `217f3e6`; `2922b56-W36-0f.5-meta-named-final.log`, `2922b56-W36-0f.5b-picks.log`) and **`candidates derived 299 (run 299): removed red 283 (85 of them by a case that asserts the gate does not raise), listed redundant 16, green 0, crashed 0`**, the unmodified gate passes (`2922b56-W36-0f.5-meta-derived-final.log`).

**Gate line.**  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each carries both trailers.
The test alone (`--no-meta`) passed on the tip, 1739 PASS (`build/w36/a090ed9-W36-0f.6-green.full`).  The meta-test was run per commit (named guards of the item, the derived candidates after 0f.5) and at the tip as
two partial runs, derived and named, because the full default run now exceeds two hours on this machine; the partial runs cannot check `REDUNDANT` for a stale entry (see Found, not fixed).  G0f changes no C++ source and no
`CMakeLists.txt`.  Per-item logs and counts are in `docs/handoff/wave36-verification/gates-g0f.md`.  The builder used no COM, did not touch the VM, `build/trace-2021/` or `build/r7/`, and pushed nothing.

### Guards seen red

Each red ran on the previous commit's gate or on a copy with one guard removed; logs are `build/w36/<parent>-W36-0f.N-*`.

| item | red | green |
|---|---|---|
| 0f.1 | 13 FAIL, 669 PASS | 692 PASS; 16 named red |
| 0f.2 | 20 FAIL, 732 PASS | 752 PASS; 10 named red |
| 0f.3 | 21 FAIL, 866 PASS | 896 PASS; 9 named red |
| 0f.4.1 | 33 FAIL, 1010 PASS | 1043 PASS; 8 named red |
| 0f.4.2 | 39 FAIL, 1221 PASS | 1263 PASS; 22 named red |
| 0f.4.3 | 12 FAIL, 1259 PASS | 1271 PASS; 3 named red |
| 0f.4.4 | 13 FAIL, 1292 PASS | 1315 PASS; 11 named red |
| 0f.4.5 | 10 FAIL, 1323 PASS | 1333 PASS; 5 named red |
| 0f.4.6 | 7 FAIL, 1331 PASS | 1338 PASS; 3 named red |
| 0f.4.7 | 9 FAIL, 1362 PASS | 1377 PASS; 9 named red |
| 0f.4.8 | 19 FAIL, 1464 PASS | 1489 PASS; 30 named red |
| 0f.4.9 | 24 new FAIL against a gate that masks the three streams whole (47 in all) | 1519 PASS; no clause added, no guard |
| 0f.4b | 32 FAIL, 1620 PASS | 1652 PASS; 11 named red |
| 0f.5 | the 15 green derived removals and two covers (17) against the new test: 13 red, 4 redundant with a red cover, 0 green unlisted | 1665 PASS |
| 0f.6 | the 22 field cases against a gate that sets the traced stream of the nine streams to the reference's: 22 of 22 FAIL | 1739 PASS |
| 0f.5 (again) | the tip's named run: 2 NOFIT | both guards red as picks; 1739 PASS |

The meta-test at the tip, as two partial runs on `2922b56` (the test files unchanged between): named 182 of 184 red and two NOFIT (`derived-cedl-path`, `derived-view-size`: the removal text no longer occurred once after
0f.4.1 and 0f.1), repaired at `217f3e6` (W36-0f.5, second commit) and both red; derived 299, 283 red, 16 redundant (12 of G0e and the 4 of 0f.5, each green with a red cover), 0 green, 0 crashed.  The named run's
final `REDUNDANT` loop raises a `KeyError` in a partial run, as G0e recorded; the table above it is complete.

### Deferred

* **W36-0f.4.9: Task FixedMeta +315, Rsc FixedMeta +140 and +177, Task Fixed2Data row +58.**  A mask needs a control/null pair that differs at the byte, and the 202 committed pairs do not show one: +315 differs
  in 4 pairs and +58 in 5, and every one of those nulls was saved from a different file than its control (a difference between two documents); no same-file pair differs at either, and none at +140 or +177.  The
  VM's decode names no meaning for the traced values.  No mask was added; the six bytes are pinned to FAIL by cases that go red if a later change masks them.  A same-file pair that differs at one of them, or the
  VM's decode of its meaning, would reopen it.  The DOSSIER's "223 of 223" for +58 matches no byte of the stream (the committed rows hold 0, 0xffffffff and duration-sized words there).

### Queued (Low, not fixed)

* **Rsc FixedMeta +176 and +213** differ 0x1c to 0x5c in 7 same-file pairs; no DOSSIER target fails there, so nothing is masked.
* **Props 0x2401390 +88** is not decoded ([U]); nothing is masked.
* **The Rsc 0xc40015f stale block** has no committed block, so the stale-block reader has no case for it.
* **The VM's own Gantt pane width** was never read; the unit's relation to the width is a bound (1 up to the width), not a formula, because the zoom's span is not decoded (W36-0f.4b).

### Found, not fixed

* **The meta-test now takes more than two hours** on this machine (299 derived candidates, 184 named, eleven jobs under load: the derived part about 90 minutes, the named part 45 to 60), and the ctest `TIMEOUT` of
  3600 s is exceeded.  `CMakeLists.txt` is out of G0f's scope; the timeout, or a split of the meta-test into two ctests, is the coordinator's call.
* **The stale-`REDUNDANT` check was not run at the tip**: it needs the named and derived parts in one run.  The 16 entries (12 of G0e, 4 of 0f.5) were each seen green with a red cover in the derived runs.
* **OPEN_PINNED**: 107 rows in 22 pairs, pinned to (pair, stream, count) and not decoded; the control and its null differ in size in those pairs.
* **A post-span capture FAILs** as data until the stamp span and the LastSave span are widened together (as G0e recorded); 0f.4.3 widened the stamp span to 0x3d28352a.

WAVE-36-G0f-COMPLETE

## G2: Props

Brief `docs/handoff/wave36-brief.md` section 2 (G2); the facts are `trace-props-framing-r4.md`, `trace-baseline-r4.md` and `trace-calculate-r3.md`.  Builder Claude Sonnet 5.5.  W36-2.0 to 2.3 were built in an earlier session;
W36-2.4 was checkpointed at `ba49c23` (the coordinator's commit, when the operator paused the wave; it carries the coordinator's trailer, not rule 1's) and finished in this one.  Items, one finding id per commit, in order:

| item | id | commit | what |
|---|---|---|---|
| W36-2.0 | span widening | `8d0d032` | the gate's `STAMP_SPAN`, `CVIEW_STAMP_SPAN` and `LASTSAVE_SPAN` end at the decoded maximum of 1187 saved copies (624 committed, 563 recorded, 0 unreadable), read by `tools/trace/span_probe.py`; no end set by hand; one tick past the top FAILs, the top passes |
| W36-2.1 | derived words | `7125071` | `props_table_words()` computes each table's size word and count word from the tables the save lays out; the measured-constant copies in `resource_growth` and the three `assignment_growth` shapes are gone; 624 of 624 committed files agree on all 8 words |
| W36-2.2 | new-record framing | `7961a6e` | an id absent from `114/Props` is appended after the typed-property section as `[length][id][kind+flags 0][payload]`, the two size words follow and the +12 count goes up by one; only `0x02401393` (the fixed baseline text) and `0x02400005` (the summary note, at most 514 B) insert |
| W36-2.3 | R35cG0-4 | `aa025f1` | the plan holds the Calculate Project Props pin as an FNV-1a 64 digest of `114/Props` (reader `props_digest`, adapter, `calculate_plan_refusal` against `mppwrite::calculate_props_pin_digest()`); the refusal is the last plan guard, so earlier needles keep their tests |
| W36-2.4 | meta-test split | `bfd4c33` (code in `ba49c23`) | `trace_gate_meta` is its own ctest, labelled `trace_gate_meta`, `TIMEOUT` 10800, left out of the matrix and asan jobs and run in one CI job; `trace_gate_guards` runs the acceptance sweeps only; one full run timed at **75 minutes** |

**Lift rule (section 0), per writer change.**  W36-2.2 is the only item that lifts a refusal.  (a) the trace names the routine (`FUN_140253fe4`, not-found branch) and every byte of the record is in the trace's framing or in the
measured text; (b) held out, never used to derive the rule: `wave35-g3-baseline-set-nested`, the five `wave36-r7-w37-b-*` pairs and the two `wave36-r7-w37-c-note-summary` pairs (64 B and 514 B payloads), all equal to Project's appended
bytes, the three header words and every control byte, and the note pairs to the whole stream outside the null-save noise (`tests/roundtrip_props.cpp`, `test_insert_pairs`); (c) the standing-rule 1 field-by-field comparison is that test;
**4(e) reads the two inserts back through Project: the coordinator's, not run by the builder (no COM).**  Everything the pairs do not show stays refused: any other id, a kind word other than 0, an odd payload length (the
disassembly pads an odd length by one byte; no committed file has one), a note past 514 B, another baseline text, an id already present, `214/Props`, and a stream whose count word does not walk to its end.

**Acceptance (W36-2.0, `trace_gate_guards`).**  1739 checks, 0 FAIL, the same as at G0f; the sweeps are unchanged (negative 191 edits, 181 FAIL and 10 pinned PASS; positive 202 control-nulls, 180 PASS and 22 pinned OPEN).  The
meta-test at the tip, one full run: **`named guards listed 184, red 184, not red 0`; `candidates derived 299 (run 299): removed red 283 (85 of them by a case that asserts the gate does not raise), listed redundant 16, green 0, crashed
0`; the unmodified gate passes**, 4528 s (`build/w36/86044a0-W36-2.4-meta-full.log`).  Being one process, it ran the closing `REDUNDANT` loop that G0e's and G0f's partial runs could not.

**Gate line.**  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each of the five carries both trailers (a
range check at the tip, `build/w36/bfd4c33-W36-G2-range.log`).  W36-2.0 has no scan log of its own; the range was scanned again at the tip.  Per group at `86044a0`: core `ctest -E plan_recalc -LE trace_gate_meta` **67/67**, `plan_recalc`
with `MPP_NO_CORPUS=1` **1/1**, M2 **332/332**, 268/268 and 64/64 fields, `check_fixtures` 0, app ctest **34/34**, Qt ctest **52/52**, `trace_gate_meta` **passes**.  The engine stays 7/8.  Per-item counts and logs are in
`docs/handoff/wave36-verification/gates-g2.md`.  The builder used no COM, did not touch the VM, `build/trace-2021/` (read only, in 2.0) or `build/r7/`, and pushed nothing.

### Guards seen red

| item | red | green |
|---|---|---|
| 2.0 | the test's new span ends against the old gate: 8 FAIL, 1731 PASS | 1739 PASS; the named guard `stamp-span-vm` red when its top is removed |
| 2.1 | size word off by one: `mppwrite_roundtrip_props`, `_assignment_growth`, `_resource_growth` FAIL (3 of 8 words) | core 67/67, `plan_recalc` 1/1 |
| 2.2 | +12 count word left alone: `mppwrite_roundtrip_props` FAILs, "the record count word at header +12 is 262" | `roundtrip_props` 2717 checks, core 67/67 |
| 2.3 | digest refusal switched off: `app_edits_calculate` FAILs 7 checks | `app_edits_calculate` passes; core 67/67, app 34/34 |
| 2.4 | the meta-test ran at the end of `test_trace_gate.py` and `ctest -N -L trace_gate_meta` listed 0 tests | each of `-L trace_gate_meta`, `-R trace_gate_guards` and `-LE trace_gate_meta -R trace_gate` lists exactly its one test; the full run passes in 75 minutes |

### Deferred

* **A Props record of any other id, an odd-length payload, or the insert of a note past 514 B.**  Not lifted: needs a committed pair that appends one.  The odd-length pad of the disassembly is in no committed file (a walk of
  all 624 has no odd record length), so the writer refuses an odd payload rather than guess the pad.

### Queued (Low, not fixed)

* None.  R35cG0-4 is fixed by `aa025f1`; per START-HERE only the coordinator's review closes the id.

### Found, not fixed

* **The message of `7961a6e` (W36-2.2) says `roundtrip_props` 2763 checks; the real count is 2717.**  Corrected in the message of `aa025f1`; history is not rewritten.
* **The spec note for W36-2.2 counts twelve pairs that grow `114/Props` by one record; `test_insert_pairs` holds nine (one derived, eight held out).**  The other pairs are not in the test; whether each is a held-out check
  of the same shape is not recorded.
* **`ba49c23` carries the coordinator's `Co-Authored-By` and no `Generated-By` trailer.**  It is the coordinator's checkpoint commit, not the builder's.
* **W36-2.0 has no per-commit scan log**; the range scan at the tip stands in for it.
* **The `trace-gate-meta` CI job has not run on a runner**: its 200-minute limit is set from the builder's 75 minutes on eleven cores, and a hosted runner may need more.  The coordinator's CI dispatch decides it.
* **4(e) for the two Props inserts** is the coordinator's (see the lift rule above).

WAVE-36-G2-COMPLETE

## CLOSE: the G0f review's findings, the G3 re-check and the fresh-tree gates

Brief `docs/handoff/wave36-brief.md` section 2 (CLOSE); the findings are `docs/handoff/wave36-verification/review-g0f.md` (the coordinator reproduced R36G0f-1 and -2 on the gate at `8d0d032`: exit 0, 0 problems).  Builder
Claude Sonnet 5.5, 22 November 2026.  Items, one finding id per commit, in order, each seen red and then green:

| item | id | commit | what |
|---|---|---|---|
| W36-C.1 | R36G0f-1 (Critical) | `de97271` | the Gantt unit word (u16 at +1180 of the 41363-byte view-state record) is masked only against a different pane width at +1172, with both units 1 up to their width and \|u1\*w2 - u2\*w1\| <= the larger width; each of the five zoom pairs with only the unit changed FAILs |
| W36-C.2 | R36G0f-2 (High) | `1a581b2` | a creation stamp is masked only for a Task row, assignment block or resource block whose uid the control lacks; an existing row's stamp is compared, in the span or not |
| W36-C.3 | R36G0f-3 | `79109bb` | the CEdl row record 0x2640003d is dropped only with kind word 1 (its value in 1140 of 1140 records); another kind is kept raw and compared; kind 1 to 0x99 on wave28-mark-auto FAILs |
| W36-C.4 | R36G0f-8 | `429f241` | the host's network address is gone from `trace-gate-fields-g0b.md`, `review-g0c.md` and `review-g3.md`; a test reads every `.md` under `spec/` and `docs/` for the 12 hex digits |
| W36-C.5 | R36G0f-4 to -7, -9 | `8f7786f` | queued in `plan-waves27-30.md`, "Queued from wave 36 (CLOSE)"; not fixed here, as the brief says |
| G3 | the precondition | none | not started: no held-out insert pair (`trace-insert-rules-r6.md`) |

**Ticks.**

* R36G0f-1: **fixed**, `de97271`.
* R36G0f-2: **fixed**, `1a581b2`.  The consequence is below: the VM's recordings of shared-row stamps now FAIL as data.
* R36G0f-3: **fixed**, `79109bb`.
* R36G0f-8: **fixed**, `429f241`.
* R36G0f-4 (the VM's newer saves above three spans), -5 (the heap bytes unchecked), -6 (two meta-test survivors), -7 (the note fields as a format), -9 (the view word's span note): **queued in the plan, not
  fixed**, `8f7786f`.
* G3 (W36-3, Insert Task, Milestone, Summary): **not started**.  The precondition is that `trace-insert-rules-r6.md` states a rule that predicts both held-out pairs.  It does not: it derived its rule from the
  four wave35-g3 pairs it predicts, so none is held out; `spec/writers/insert.md` still says items 3 and 5 are not decoded; and the seven round 7 recordings `tests/data/wave36-r7-insert-*` are committed but
  only `scripts/oracle_r7_insert.py` refers to them, and no note derives a rule from them or checks one on them (`docs/handoff/wave36-verification/gates-close.md`, "G3: the precondition re-checked").  The writers
  for Insert Task, Milestone and Summary stay refused.

**Acceptance (the committed `tools/trace/test_trace_gate.py`).**  **1901 checks, 0 FAIL** (1739 at G0f and G2; +94 at C.1, +46 at C.2, +20 at C.3, +2 at C.4).  The negative and positive sweeps keep their shape (191 edits;
202 control-nulls, 180 PASS and 22 pinned OPEN).  The meta-test, one full run on a fresh configure of `8f7786f`: **`named guards listed 194, red 194, not red 0`; `candidates derived 312 (run 312): removed red 296 (90 of them by a case that asserts the gate does not raise), listed redundant 16, green 0, crashed 0`; the unmodified gate passes**, 3488 s (`build/w36/8f7786f-W36-CLOSE-meta-full.log`).

**Gate line.**  Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each carries both trailers.  Per
group at `8f7786f`: core build exit 0, core `ctest -E plan_recalc -LE trace_gate_meta -j4` **67/67**, `plan_recalc` with `MPP_NO_CORPUS=1` **1/1**, M2 **332/332**, 268/268 and 64/64 fields, `check_fixtures` 0, app
ctest **34/34**, Qt ctest **52/52**, `trace_gate_meta` **passes** (58 minutes).  The engine stays 7/8.  CLOSE changes no C++ source and no `CMakeLists.txt`.  Per-item logs and counts are in
`docs/handoff/wave36-verification/gates-close.md`.  The builder used no COM, did not touch the VM, `vmconnect`, `build/trace-2021/` or `build/r7/`, and pushed nothing.

### Guards seen red

Each red ran the new cases against the previous commit's gate; logs are `build/w36/<parent>-W36-C.N-*`.

| item | red | green |
|---|---|---|
| C.1 | 60 FAIL, 1637 PASS | 1833 PASS; 14 named pane guards red; derived of the changed functions 30 of 31 red, 1 redundant |
| C.2 | 18 FAIL, 1861 PASS | 1879 PASS; 14 named stamp guards red; derived 56 of 58 red, 2 redundant |
| C.3 | 14 FAIL, 1885 PASS | 1899 PASS; 5 named row-record guards red; derived 22 of 23 red, 1 redundant |
| C.4 | 2 FAIL, 1899 PASS | 1901 PASS; no gate guard |
| C.5 | none (a note) | none |

New named guards: C.1 `pane-unit-zero`, `-over`, `-same-width`, `-ratio`, `-ratio-wide`, `-other` (the old `pane-unit-width` and a dead default clause are gone); C.2 `stamp-row-known`,
`stamp-row-known-other`, `stamp-row-known-ctl`, `varkey-known`, `varkey-known-ctl`; C.3 `rows-kind`, `rows-kind-value`.  Derived candidates: 308 after C.1, 312 after C.2 (299 at G2); none stays green.  One derived
candidate (`_task_uids:if-never:data is None`) was green at C.2 and is red by a raise since a case with a control that has no Task stream was added.

### Deferred

* **R36G0f-4 to -7 and -9**: not fixed here by the brief's own rule; each is written in the plan with the finding and the fix it names.
* **G3**: see the tick above.  A rule derived on the round 7 insert pairs and checked on the wave35-g3 pairs, or the reverse, would meet the precondition.

### Queued (Low, not fixed)

* R36G0f-4 to -7 and -9 as above (`plan-waves27-30.md`, "Queued from wave 36 (CLOSE)").

### Found, not fixed

* **C.2 makes the VM's recordings of shared-row stamps FAIL as data**: the 15 Task, 6 assignment and 5 resource stamp targets of W36-0f.4.7 are in rows the control and the reference share, and a control and its
  null differ at such a stamp in 0 of 788 Task rows, 0 of 556 assignment blocks and 0 of 338 resource blocks.  That is a finding by design (R36G0f-4: 108 of 210 VM copies PASS); only the coordinator's re-gate
  with a same-machine null can say whether the VM writes those stamps as noise.
* **C.4's scope**: the check covers the 12 contiguous hex digits in `.md` files under `spec/` and `docs/`.  The recorded bytes in `analysis/*.tsv` and `core/mppwrite/assignment_growth.cpp` and the spaced hex dumps in
  21 spec files are a record's data, not a printed address, and were left.  The VM's own node stays, as a different machine's.
* **C.3's design**: the kind condition rather than the header kept in the canonical form, because the record's presence is session noise (one save only of 2 of 223 pairs); a header kept beside an absent record
  would FAIL a legal save.
* **The meta-test's time**: one full run on the tip is 58 minutes (3488 s, nothing else running), against 75 minutes at G2 (184 named guards and 299 derived candidates then; the lists have grown, the run was quieter).
* **Still the coordinator's**: copying the new gate and `host.ps1` into the VM and the re-gate (R36G0b-8 needs a real recording), fresh-tree gates in the verify worktree, mutations at each new guard, the
  independent reviewers, range scans, push and CI on all seven jobs, 4(e) for the two Props inserts (W36-2.2), the whole-wave review and the merge.  CI has no `olefile` until its jobs `pip install` it, and the
  `trace-gate-meta` CI job has not run on a runner.

WAVE-36-COMPLETE

## Correction, Sun 22 Nov 2026 (wave 36b, W36b-0.1): W36-2.2 is "framing decoded, not lifted"

Nothing above is edited; this section replaces what it says about W36-2.2.  The row "W36-2.2 | new-record framing | `7961a6e`" and the paragraph "Lift rule (section 0), per writer change" call W36-2.2 "the only item
that lifts a refusal" and name 4(e) as "the coordinator's, not run".  4(e) has now run (`wave36-verification/coord-4e-g2.md`): Project opens all nine port saves and its reopen shows no baseline (no Baseline
Start, Finish, Duration, saved date or Work) and no note.  The baseline values live in the task tables and the note text in the Task `Var2Data`; the Props record the insert wrote is not where Project reads
either from.  Lift rule (c) fails, so the refusal is **not lifted**.

* **W36-2.2 stands as the decoded framing only**: `[length][id][kind+flags 0][payload]` at the stream end, the two size words, the +12 count up by one; it equals Project's appended bytes in the nine pairs
  of `test_insert_pairs`.  It is now `mppwrite::props_insert_framing()`, which the test calls and no save path calls.
* **`save_props_edit` refuses every record whose id is not already in `114/Props`**, as it did before `7961a6e` (W36b-0.1; the commit's message holds the red and the green).  This closes R36G2-4 (two inserts in
  one save) and R36G2-5 (unmeasured note shapes) for the save path: neither can be reached.
* `props_edit.h`'s insert paragraphs and START-HERE's wave 36 line say "framing decoded, not lifted".
* The record's other W36-2.2 claims (the nine pairs, the refusals of the framing function) hold.
