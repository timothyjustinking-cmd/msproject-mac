# Review: wave 36 G0, `0e8c9d4..53351b2` (branch `wave36`)

Independent, read-only review, 11 November 2026.  Seven commits, `a20874d` to `53351b2`.  Read with
`git show` and `git diff` only; nothing checked out, built or changed in the tree.  The gate and its
test were exported from `53351b2` into `build/verify-w36/g0-review/` and run against committed
fixtures in `tests/data` (full paths).  Probe and mutation scripts ran from the reviewer's temporary folder,
importing the exported gate; no file in the repo was written except this review.

**Count: 1 Critical, 3 High, 3 Medium, 4 Low.**

## What holds

- **Exit code.**  `trace_gate.py` returns 0 only on `problems == 0`, 1 otherwise, 2 for usage or a
  missing `olefile`.  Planted `return 0`: the test goes red (6 failing).
- **`runone6.ps1` records the exit code.**  `$gx = $LASTEXITCODE` straight after the gate's
  pipeline goes to `<t>.gate.exit.txt` and to the `saved` line.  An absent null now passes
  `C:\trace\in\NO-NULL.mpp`, not the control.
- **W36-0.2.**
  - `control_mask` builds a mask only from an equal-length control and null, maps it one to one
    only when the reference is the control's length, and otherwise masks the seven fixed-stride
    streams record by record, never a row the control lacks.
  - A refused mask prints `mask-refused` and leaves the stream unmasked, which is stricter.
  - Planting the old offset mask turns the test red (2 failing).
- **W36-0.3.**  "no null" is reachable and is a FAIL: a nonexistent path (OSError), an unreadable
  file, and the control passed as its own null all give `no null`, exit 1.  Planted mutation: the
  test goes red.
- **"nothing checked"** is a FAIL.  Planted mutation: the test goes red.
- **The test itself.**  `test_trace_gate.py` from `53351b2`, run against `tests/data`: 105 PASS,
  `all passed`, exit 0.  This matches the builder's 105.
  - A by-name skip of a stream (`continue` before the compare) turns it red.
  - So does treating an equal-size path stream as not compared.
- **W36-0.5.**
  - `r5b-skip.ps1` has one `Invoke()`, under `$skip -and $dlg.Current.Name.StartsWith('Sign in')`;
    any other dialog returns `OTHERDIALOG` and touches nothing.
  - `kvp/host.ps1` `vmx` throws on a sha MISMATCH and on a chunk length unequal to the length
    announced in `out_meta`.
  - The agent's `out_meta` is `id|n|length|sha`, so `m[2]` is the right field.  But see R36G0-7.
- **W36-0.6.**
  - `docs/START-HERE.md` is 6267 bytes at `53351b2`.
  - `main` is `b2c0b19` (checked).
  - "4(e) 7 checked and 3 not checked" matches `wave35g-verification/coord-gates-close.md:33`.
  - Every repo path the file names exists at `53351b2`.
  - `history-waves34-35.md` carries the old "30-second state" and "Still open" unchanged.
  - `INDEX.md` lists the brief, the record and the history file.
- **Hygiene.**
  - All seven commits carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)`, and each
    has one item id.
  - No CR in any changed blob, at the base or at the head.
  - No operator name, company or personal path in the diff or the messages.  The only paths are the
    VM's `C:\trace\...` and the neutral fixture path.

## Findings

### R36G0-1 (Critical): `214/CV_iew/Var2Data` has a whole-stream window, so the gate PASSes an unedited save as a sort edit or Bold off

- **Where:** `tools/trace/trace_gate.py:99`.  The rule is
  `"214/CV_iew/Var2Data": {"byte": [(0, 1 << 30)], "ceiling": 4}`.  It is a by-name waiver of every
  byte of a 100 KB stream, capped at 4.  The rule is the view stream's data: the sort keys, and the
  0x2240000c font items (`trace-font-item-r6.md:46`: "the item and nothing else changes in
  `214/CV_iew/Var2Data`").
- **Failure:** I ran the gate with TRACED = the pair's `-control-null` (a save with NO edit),
  REFERENCE = the edited file, and the pair's own CONTROL and NULL.  It PASSes, exit 0, with 94
  streams checked, for:
  - `wave35-viewstate-sort-name-desc`, `-sort-name-asc` and `-sort-flat-name-desc`.  The edit is 2
    to 4 bytes at `+42616`, `+42620`, `+43768` and `+43772`: the key 0x17 becomes 0x0e, and the
    ascending flag 01 becomes 00.  The null equals the control at those offsets, so no mask covers
    them; only the window does.
  - `wave35-g3-font-bold-off`, which is G1's W36-1.1 pair.  The edit is `+85295` (mask 0x01 to 0x00)
    and `+85324` (font word bit 0).

  A single flipped byte anywhere in this stream also passes: on `wave30-calculate`, 1 byte and then
  4 bytes at `+50089` were not caught.  G1's lift criterion (b) and the coordinator's re-gate would
  accept a trace or a port save that lost a sort or a font attribute.

  Across the 89 pairs that have a null under the default names, 12 PASS this way.  I verified the 4
  above at byte level.  The other 8 are no-op edits, or use non-default controls in `specs6*.ps1`,
  and are not attributed.
- **Fix:**
  - Delete the whole-stream rule.
  - Locate the save-date word the null run showed (4 bytes) by its VarMeta key, as the
    `TBkndAssn/Var2Data` `varkey` rule does, and window only those bytes.
  - Add a test case that runs TRACED = `-control-null` against REFERENCE = the edit for
    `wave35-viewstate-sort-name-desc` and `wave35-g3-font-bold-off`, and asserts FAIL with a
    `bytes-differ ???214/CV_iew/Var2Data` line.

### R36G0-2 (High): the path-stream rule is a by-name whole-stream waiver

- **Where:** `trace_gate.py:108-112` (`PATHSTREAM`) and `compare()` at `:271-279` and `:306-309`.
- **Failure:** For `214/CEdl/Var2Data`, `Props14` and `SummaryInformation`:
  - a size change up to 128, 96 or 16 bytes means the stream is not compared at all (`size-noise`,
    not a problem);
  - at equal size, up to 64, 64 or 40 bytes may differ anywhere.

  Probed on `wave30-calculate`, each of these PASSes:
  - `Props14` zeroed and 16 bytes shorter;
  - `CEdl/Var2Data` zeroed and 16 bytes shorter;
  - `SummaryInformation` zeroed and 8 bytes shorter;
  - 40 flipped `SummaryInformation` bytes.

  Yet the only per-machine difference in these streams is the file path and the user name.  In
  `wave35-viewstate-outline-all`, `CEdl/Var2Data` differs from control to edit only in the path run
  at `+434` and its length words.  The view name and table name around it are equal.  The head line
  `96 streams, 94 checked, 0 problems` also hides that two streams were never compared.

  The record says "no by-name whole-stream waiver", and `memory/coordinator.md` says "flips one byte
  in every stream".  Both are untrue for these streams.
- **Fix:**
  - Find the path and user-name strings by content, in the same way as the scanner in
    `check_fixtures.py`: UTF-16 runs under a drive root, and the author and last-author property ids
    in the property sets.
  - Mask exactly those runs, and the length words that frame them.
  - Compare every other byte after realigning on the run.
  - Print `N not checked` in the head line, and make any unchecked stream a FAIL unless it is named
    with the field that excused it.

### R36G0-3 (High): the suffix match makes `DocumentSummaryInformation` a path stream

- **Where:** `_path_rule()`, `trace_gate.py:133-137`.  It tests
  `name.endswith("SummaryInformation")`, which is also true of `?DocumentSummaryInformation`.
- **Failure:**
  - In `wave30-calculate`, DSI is matched as a path stream; I confirmed this by listing the
    streams `_path_rule` claims.
  - A 1-byte flip at equal size passes.
  - DSI zeroed and 16 bytes shorter passes.
  - The port writes this stream: `core/mppwrite/actuals.cpp:908` and `:1980`, and
    `spec/writers/document-summary-information.md`.  The calculate edit changes it (control 1100
    bytes, edit 1104).
  - The null run never listed DSI, so this waiver was never measured.

  The same `endswith` pattern in `_noise_rule` and `_stride` has no collision today.
- **Fix:**
  - Match stream names exactly, after the `?` prefix, for `PATHSTREAM`, `NOISE` and `STRIDE`.
  - Add a test that a flipped DSI byte FAILs.

### R36G0-4 (High): several windows cover data the evidence says is not noise

- **Where:** `NOISE` at `trace_gate.py:77-87`.
- **Failure:** Each of these single-byte flips PASSes on `wave30-calculate`:
  - **Assn Fixed2Data +36:** the window `(32, 42)` covers field 0x0f40027e, which
    `trace-insert-milestone-r4.md:9` says "is equal in the VM file and the pair".
  - **Task FixedData +581:** an existing row's +129.  The creation-stamp window `(128, 132)` is
    applied to EVERY row, at 3 bytes per row, though only a new row gets a fresh stamp (`:20`).  An
    existing row's Created date is data.
  - **114/Props record 0x02401394:** `trace-props-framing.md:17` says it "does **not** change between
    a control and its null save" (per-file identity).
  - **Task Fixed2Data row 3 +5:** no evidence note derives the windows `(0,10)`, `(24,34)` and
    `(42,50)`.

  The brief asked for narrow masks for four named sources.  These go wider than the notes the
  brief cites.
- **Fix:**
  - Window Assn Fixed2Data at +0 to +32 (the two GUIDs) only.
  - Apply the Task FixedData stamp only to rows the control does not have.
  - Drop 0x02401394 from the Props windows.
  - Cite a note and offset for each Fixed2Data window, or remove it.
  - Add a test: one data byte inside each window class, at a field the notes call data, FAILs.

### R36G0-5 (Medium): no real recording can PASS, because the ceilings come from a single null run

- **Where:** the ceilings and window widths in `NOISE` (`trace_gate.py:77-101`).  The record frames
  the expected FAILs as "a finding by design" (`wave36-complete.md`, "Found, not fixed";
  `memory/coordinator.md`, "Watch").
- **Failure:**
  - **Method.**  I replayed the 40 committed real-VM gate texts against the new rules.  The old
    gate's counts are bytes outside the control mask, and the new mask is a subset of the old one,
    so a count over the new ceiling, or over the window width, is a certain FAIL.  The texts are
    `trace-gates-r6c-n3-viewstate.txt` (17) and `trace-gates-r6c-n6-g1-5c-assignadd.txt` (23); the
    r5 texts are summaries and could not be replayed.
  - **Result: 40 of 40 certainly FAIL.**  None can pass.
  - **Examples:**
    - `214/CEdl/FixedData`: 12 or 13 bytes differ in 23 recordings, against a 10-byte window.  So
      the stamp is wider than the one null run showed.
    - `114/Props`: 26 to 32 bytes, against a ceiling of 24.
    - `TBkndTask/Fixed2Data`: up to 197, against 92.
    - `TBkndCal/Fixed2Data`: 30 to 39, against 12.
    - `214/CV_iew/FixedData`: 36 to 39, against 30.
  - **Answer to the practical test.**  A by-design PASS is reachable only for the null run the
    numbers were copied from, and for same-machine identical files.  Every real recording will FAIL,
    and the coordinator's re-gate of the 109 would raise about 109 "findings" that are gate
    calibration, not trace faults.  Taken together with R36G0-1, the gate fails good files and
    passes a wrong one.
- **Fix:**
  - Derive windows by field: the full stamp-word width, and the GUID bytes per row.  Use the null
    run plus these 40 committed texts as the calibration set.
  - Scale ceilings per window, not per stream.
  - Commit one real traced null save (scanned like any fixture) and assert in the ctest that it
    PASSes against `wave30-calculate-control-null`.

### R36G0-6 (Medium): the font item's top-byte mask is in the wrong stream

- **Where:** `noise_windows()`, `trace_gate.py:177-178`, and the ceiling at `:314-315`.  Both apply
  `font_item_garbage` only when the name ends `214/Props`.
- **Failure:**
  - The 0x2240000c items live in `214/CV_iew/Var2Data`.  For `font-bold-row2`, `-italic` and
    `-bold-off` (edit and control), the item is at `+85276`, length 44, kind 1, and none is in
    `214/Props`.
  - The payload +43 garbage byte (`trace-font-item-r6.md:25`) is therefore masked only by accident,
    through R36G0-1's whole-stream window.
  - Once R36G0-1 is fixed, every font recording whose top byte differs will FAIL.
- **Fix:**
  - Run `font_item_garbage` on `214/CV_iew/Var2Data`, and remove it from `214/Props`.
  - Add a test that flips only +43 of the bold-row2 item (PASS) and +40 of it (FAIL).

### R36G0-7 (Medium, UNVERIFIED): the new reply-length check may refuse every good KVP reply

- **Where:** `tools/trace/kvp/host.ps1:31`, against `kvp/agent.ps1:11` and `:16`.
- **Failure:**
  - The agent announces `$o.Length`, where `$o` comes from `Out-String`.  So the length includes
    the trailing newline Out-String appends, about 2 characters (CRLF) in Windows PowerShell.
  - The host reads the chunks back out of the CIM `GuestExchangeItems` XML through `[xml]`, and an
    XML parse normalises CRLF to LF.
  - If the CR does not survive, the host's length is one short and `vmx` throws on every reply.
  - The stub test (`test_host_skip.ps1`, `Reply`) announces `b64.Length` with no trailing newline,
    so it never models the real agent.
  - The builder records that the scripts were not run against the VM.
- **Fix:**
  - Announce and compare the length of the base64 payload after trimming whitespace on both sides.
  - Make the stub's reply mimic `Out-String`.
  - Falsifying test: one `vmx 'Get-Date'` on the VM.  If it returns, the finding is rejected.

### R36G0-8 (Low): the flip test cannot see a window that covers the whole stream

- **Where:** `tools/trace/test_trace_gate.py:175-192`.
- **Failure:**
  - When no byte is free, the test flips ceiling+1 window bytes, which a whole-stream window with
    a ceiling always FAILs.
  - Planted `"TBkndTask/Var2Data": {"byte": [(0, 1 << 30)], "ceiling": 4}`: the test stays green.
  - The same window on Task FixedData went red only because of the separate CLI check at offset 60.
  - The existing `CV_iew/Var2Data` rule passes the test for this reason.
  - The "unflipped pair passes" case compares the reference with itself, which is vacuous.
  - Only one pair (`wave30-calculate`) is used.
- **Fix:**
  - Assert that no window spans more than a named field width; for example, fail any window wider
    than 32 bytes that has no cited note.
  - Add the R36G0-1 negative cases.
  - Add one real cross-save PASS case (R36G0-5).

### R36G0-9 (Low): the per-record mask maps by row position, not by row identity

- **Where:** `control_mask()`, `trace_gate.py:241-247`.
- **Failure:** After a middle insert (the `insert-milestone-first` and `-nested` pairs, G3's inputs),
  control row r's mask lands on reference row r, which is a different task.  Harmless while noise
  offsets are the same in every row, but it is A-4 in a narrower form.
- **Fix:** Map rows by unique id, from the FixedMeta or uid column, before masking; or refuse the
  stride mask when the row count differs and the insert position is not the end.

### R36G0-10 (Low): the records overstate G0, and one is missing

- **Where:** `docs/handoff/wave36-complete.md` (G0), `memory/coordinator.md` (G0 bullet),
  `docs/START-HERE.md` ("G0 is done").
- **Failure:**
  - All three say "exact noise windows" and "no by-name waiver", and that the test "flips one byte
    in every stream".  These are not true (R36G0-1 to -3, -8).
  - `docs/handoff/wave36-verification/gates-g0.md` does not exist at `53351b2`, although brief
    section 1 names it as the builder's file.
- **Fix:** Correct the three sentences when the findings land, and commit `gates-g0.md` with the
  gate texts the record cites.

### R36G0-11 (Low): the history move dropped the standing-facts section

- **Where:** `2ecf42b`.
- **Failure:**
  - The old START-HERE's "Standing rules (from wave 27)" bullets went to none of these files:
    - the new START-HERE;
    - `history-waves34-35.md`;
    - `memory/coordinator.md`;
    - `standing-rules.md`;
    - `docs/build.md`.
  - Those bullets held the inventory of enabled and refused writers ("Saveable structural edits",
    "Split Task is measured but NOT enabled") and the local Qt path.  They also held the
    `check_command_backlog` ctest and the archive pointers.
  - They survive only scattered through older wave records.
- **Fix:** Move that section verbatim into `history-waves34-35.md`, or put the writer inventory in
  `spec/README.md`, and point to it from START-HERE.

Also open, and recorded by the builder: CI has no `olefile`, so `trace_gate_guards` SKIPs there.
It stays Low and open until the workflows install it.

## Not findings

- **Exit 0** means zero problems only; `mask-refused` is information, not a waiver.
- **"no null" for the ten no-null specs.**  The specs with `Null = $null` (wave27-percent-actuals
  and the others) now FAIL "no null" in `runone6.ps1`.  That is A-3's intent; round 7 item 1 makes
  their nulls.
- **The viewstate specs** use the default `-control-null` names (`specs6.ps1:23-28`), so W36-0.3
  does not refuse them.
- **The 0.1 to 0.6 red runs** are consistent with the code I read.  I did not re-run them on the
  old tool.  My own mutations M1 and M4 to M8 went red.
- **`$LASTEXITCODE` after `& python ... | Out-File`** is python's exit code.
- **`StartsWith('Sign in')`** is case-sensitive.  That is the intended narrowing.
- **Author line.**  Every commit's author is the operator's GitHub noreply, which is the repo's
  existing convention.

Probe outputs and scripts are kept only in the reviewer's temporary folder.
