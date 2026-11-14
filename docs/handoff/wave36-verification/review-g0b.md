# Review: wave 36 G0b, `16752d4..cea3c6d` (branch `wave36`), run at the tip `9fe128a`

Independent, read-only, adversarial review, 11 November 2026.  Eleven commits, `916ac5b` to `cea3c6d`
(ten items and the group record).  Read with `git show`, `git diff` and `git log` only; nothing checked
out, built, staged or changed in the tree; no compiler, cmake or ctest was run; the VM and
`build/trace-2021/` were not touched.  `tools/trace/trace_gate.py`, `tools/trace/test_trace_gate.py`,
`tools/trace/kvp/host.ps1` and `tools/trace/test_host_skip.ps1` were exported from `9fe128a` into
`build/verify-w36/g0b-review/` (git-ignored: the reviewer's temporary folder) and run and mutated
there with Python against the committed fixtures in `tests/data`.  `git diff cea3c6d 9fe128a -- tools/`
is empty, so the gate and the test at the tip are G0b's; what moved after G0b is `tests/data`
(624 `.mpp` files at the tip, 332 when G0b was written).

**Count: 0 Critical, 6 High, 3 Medium, 3 Low.**

Sweeps at the tip, by the command line (`python trace_gate.py TRACED REFERENCE CONTROL NULL`), one
run each:

| sweep | denominator | result |
|---|---|---|
| negative, TRACED = control-null, REFERENCE = edit | 185 pairs named `<stem>.mpp` / `-control.mpp` / `-control-null.mpp`, every one differing from its null outside `SummaryInformation` (0 excluded) | **183 exit 1, 2 exit 0**: `wave28-percent-auto100`, `wave28-percent-auto40`, the two known no-op edits (checked below) |
| negative, the six nulls whose control has another name | 6 | **6 exit 1** |
| positive, TRACED = control-null, REFERENCE = control | 196 pairs with `-control.mpp` and `-control-null.mpp` | **189 exit 0, 7 exit 1** |
| positive, the six nulls whose control has another name | 6 | **0 exit 0, 6 exit 1** |

`tools/trace/test_trace_gate.py` itself at the tip: **103 PASS lines, then a traceback, exit 1**
(R36G0b-5).  Restricted to the 332 files of `cea3c6d` it is 219 PASS, `all passed`, exit 0, which is
the builder's number.

## What holds

- **Negative sweep.**  No committed pair lets an unedited save pass as its edit.  The 97 round 7
  pairs the builder never ran are included: 93 round 7 stems have a control and a null, 92 of them
  an edit, and all 92 edits FAIL (92 of the 93 nulls PASS against their control).  The expected-fail example was seen failing (`wave27-unlink`: 9 problem lines,
  exit 1), and the expected-pass example passing (`wave28-mark-auto` null against control, exit 0).
- **The two PASSes are no-ops.**  For `wave28-percent-auto100` and `-auto40` I compared the edit
  with the control stream by stream.  Outside the named GUID and Props fields the only differences
  are the path strings and the three size words that follow from them (`214/CEdl/VarMeta` +20..21,
  `214/Props` record 0x00010007, `SummaryInformation`).  No task, assignment or view byte differs.
- **R36G0-1 fixed.**  The whole-stream rule is gone.  Planted back at the call site
  (`offs.update(range(len(data)))` for `214/CV_iew/Var2Data`): the test goes red, 6 failing.
- **R36G0-3 fixed.**  Names are compared with `==`.  A suffix match planted in `_noise_rule` and in
  `_stride`: red (66 and 39 failing).  A suffix match planted at the `CANON` call site stays green,
  but it is an equivalent mutant: `_canon_summary` refuses the two-section document summary, so
  its bytes are unchanged (checked on `wave30-calculate`).
- **R36G0-6 fixed.**  `font_item_garbage` runs on `214/CV_iew/Var2Data` only and masks payload +43.
  A whole-row mask planted in the helper: red.
- **R36G0-9 fixed.**  Rows are masked by identity.  Planted: positional masking (`if False`), no
  identities at the call site, a row named twice still masked, the row-count check removed: all
  red.  Two Task rows swapped in the traced save FAIL (38 bytes).
- **R36G0-11 fixed.**  The "Standing rules (from wave 27)" section in
  `docs/handoff/history-waves34-35.md` equals the old START-HERE section line for line (78 lines,
  `diff` empty).  `docs/handoff/wave36-verification/gates-g0.md` exists.
- **The four R36G0-4 windows are gone.**  Planted back: Assn Fixed2Data +32 as a GUID, the stamp
  on every Task row, a wider stamp, Props 0x02401394: all red.
- **One side only.**  A stream in one file only, a Task row missing on either side, and an empty
  pair are all problems; planted `only-in-traced` and `only-in-reference` waivers go red.
- **Exit paths.**  Exit 0 only on zero problems.  Planted `return 0`, "no null" as a pass, the
  control accepted as its own null, "nothing checked" as a pass, `size-differs` and `bytes-differ`
  not counted: all red.  An uncaught exception exits 1 by traceback, never 0.
- **Mutation totals.**  48 mutations of the gate, each in its own copy, run against the test on the
  `cea3c6d` file set (baseline 219 PASS, exit 0): 36 red, 12 green.  One of the 12 is equivalent
  (the `CANON` suffix above); the other 11 are R36G0b-6.
- **CI.**  `ci.yml` has the `setup-python` and `pip install olefile` steps in both ctest jobs,
  before Configure.  Not run (nothing here can run it).

## Status of R36G0-1 to -11

| id | status | evidence |
|---|---|---|
| R36G0-1 | fixed | negative sweep 183 of 185 and 6 of 6; mutant red.  The view bag's other per-save bytes are now under the control mask only (R36G0b-1) |
| R36G0-2 | partly | the three streams are parsed and compared; but two undecoded fields still waive a size change (R36G0b-2, R36G0b-3), and no test holds the parsers' guards (R36G0b-6) |
| R36G0-3 | fixed | mutants red; a flipped document-summary byte FAILs |
| R36G0-4 | partly | the four windows are gone; a zeroed GUID passes (R36G0b-4); Assn Fixed2Data +32..37 and Task Fixed2Data +42, +46..48 are still waived on the pairs whose control and null differ there (R36G0b-1) |
| R36G0-5 | partly | no ceiling is left; but 50 of 196 null saves FAIL against their own control once the control mask is unavailable (R36G0b-8) |
| R36G0-6 | fixed | mutant red; +43 passes and +40 fails |
| R36G0-7 | fixed in the model, UNVERIFIED on the VM | read only, not run (R36G0b-9) |
| R36G0-8 | partly | the every-offset audit is real for the `NOISE` streams; for the five parsed streams and the font byte it reads the expected mask from the gate (R36G0b-6) |
| R36G0-9 | fixed | four mutants red |
| R36G0-10 | partly | `gates-g0.md` exists; the corrected sentences are replaced by new ones that overstate (R36G0b-12) |
| R36G0-11 | fixed | 78 lines, `diff` empty |

## Findings

### R36G0b-1 (High): the control mask is still a mask no field names, and it passes changed data bytes on 48 of 196 pairs

- **Where:** `control_mask()` and `compare()`, `tools/trace/trace_gate.py:575-589` and `:618-629`.
  The docstring keeps it as rule 1: "the control mask covers it".  Any byte at which the control
  and its null differ is waived in the reference, at the same offset (or the same row), with no
  field, record id or evidence line behind it.
- **Measured:** for each of the 196 control/null pairs I took the bytes where the two differ and
  removed every named field and every canonical placeholder.  **48 pairs keep bytes that only the
  control mask covers** (10 of them round 7 pairs):

  | stream | pairs | where |
  |---|---|---|
  | `214/CV_iew/Var2Data` | 21 | bag block uid 2 +42682; bag block uid 3 +2270..2272, +2276..2277, +2280..2281 |
  | `114/TBkndAssn/Fixed2Data` | 18 | row +32..+37 and +41, which `trace-gate-fields-g0b.md:12` calls data |
  | `114/Props` | 12 | records 0x01000004, 0x00800004, 0x024013c8, 0x024013bd |
  | `114/TBkndAssn/FixedMeta`, `Fixed2Meta` | 8 each | +4, +8, +16..20, +50..54, +86..97; +8, +20, +68..78, +121..131, +174 |
  | `114/TBkndRsc/FixedMeta` | 7 | +176, +213 |
  | `Props14` | 4 | canonical +394 |
  | `114/TBkndTask/Fixed2Data` | 2 | row +42, +46..+48, which `trace-gate-fields-g0b.md:13` calls "a flags byte and the Calculate save stamp, data, not masked" |
  | `214/CEdl/Var2Data` | 2 | 12 payload bytes of record 0x26400024 |
  | `214/CTable/FixedData` | 2 | +447 |

- **Failure:** TRACED = the reference with exactly those bytes inverted, the pair's own control
  and null, by the command line.  Every one is `96 streams, 96 checked, 0 not checked, 0 problems`,
  exit 0:
  - `wave30-baseline` and `wave30-wbs`: 12 bytes of Task Fixed2Data at row +42, +46, +47, +48;
  - `wave29-percent-leaf`: Assn Fixed2Data row +32..35 and +37;
  - `wave28-outline-summary`: `114/Props` +63452 and +63484 (records 0x01000004 and 0x00800004),
    5 bytes of Assn FixedMeta, 3 of Assn Fixed2Meta;
  - `wave30-resource-rename`: Rsc FixedMeta +176 and +213;
  - `wave36-r7-table1-column-replace`: CTable FixedData +447;
  - `wave28-mark-auto`: six bytes of the view bag.

  The control mask also cancels the value checks of the named fields that sit under it: a view
  GUID string with a hex digit replaced by `Z` passes on `wave30-calculate`, although
  `_guidstr_digits` refuses it, because the control and null differ at that digit.
- **Why it was not seen:** every "named field" case in the test passes `dict(ctl)` as the null, so
  the control mask is empty there (`verdict()`, `window_audit()`); the mask's own width is never
  audited.  The builder's record names the view-bag words as "left to the control mask" but not
  the other eight streams.
- **Fix:**
  - Decode each row of the table above and add it to `NOISE` with its evidence line, or record it
    as not decoded and let it FAIL, as the brief's acceptance says ("Do not widen it").
  - Then delete rule 1: with every noise source named, the control and null are needed only to
    tell a new row from an old one.
  - Until then, print `control-mask <stream> <n>` for every stream in which the mask excused a
    byte outside the named fields, and count it as a problem.
  - Add a test: for every committed pair, the control mask minus the named fields is empty.

### R36G0b-2 (High): the `Props14` tail entry 0xffffffff is dropped whole, so any bytes of any length pass

- **Where:** `_canon_props14()`, `trace_gate.py:306-318`, and `_tail_entries()` at `:264-277`.  An
  entry with id 0xffffffff is removed from the canonical form with its payload, and the count word
  is rebuilt.  The payload has no length: it runs to the next entry mark or the end of the stream.
  The record says "what it means is not known".
- **Failure:** on `wave30-calculate`, by `compare()` (olefile cannot grow a stream in place):
  - a second 0xffffffff entry of 400 bytes that are not text, appended, with the count raised:
    `Props14` 556 to 964 bytes, **PASS**;
  - the existing entry's 30 bytes replaced by the same 400 bytes: **PASS** (also on
    `wave34-assign-add-second`).

  The brief: "No size change is waived ... only where a decoded variable-length noise field
  explains it exactly", and "record the field that is not decoded.  Do not widen it."  This is a
  size change waived by a field that is not decoded.  The committed entries hold a UTF-16 name
  of 30 bytes, so the field can be decoded.
- **Fix:** treat 0xffffffff like 0x3540000b: keep the 8-byte entry header in the canonical form
  when present, replace the name only when `_text_ok` holds, and allow the entry to be absent on
  one side only as a named case (one entry, text payload).  Test: junk in the entry FAILs; a second
  entry FAILs.

### R36G0b-3 (High): `SummaryInformation` waives everything from a noise string to the next property

- **Where:** `_canon_summary()`, `trace_gate.py:349-360`.  For Author, LastAuthor and RevNumber the
  whole value slot `data[lo:hi]` is replaced, where `hi` is the next property's offset.  The only
  check is `8 + length <= len(raw)`; the bytes after the string and its padding are never looked
  at, and the offsets and section size are rebuilt, so the slot may be any size.
- **Failure:** on `wave30-calculate`, 240 arbitrary bytes inserted after the Author string, with
  the offsets and section size made consistent: 292 to 532 bytes, **PASS**.  The same after
  RevNumber: **PASS**.  (Control cases: Title, CreateTime and properties 18 and 19 changed all
  FAIL.)
- **Fix:** accept a string slot only when `len(raw) == 8 + ((length + 3) & ~3)` and the padding is
  zero.  Test: junk after the Author string FAILs.

### R36G0b-4 (High): a GUID field passes as all zeros against a real GUID

- **Where:** `_guid_ok()`, `trace_gate.py:435-436`, used for `guid_record`, `guid` and the Props
  `guid` class: a field is masked when each side is a version-1 GUID **or all zero**.
- **Failure:** on `wave30-calculate`, TRACED = the reference with every GUID field zeroed:
  - Task Fixed2Data, 192 of 384 bytes: PASS, and by the command line `0 problems`, exit 0;
  - Cal Fixed2Data 256 of 288, Cons Fixed2Data 48 of 48, Assn Fixed2Data 64 of 96, Rsc Fixed2Data
    96 of 160, `214/CV_iew/FixedData` 48 of 414: all PASS;
  - both save GUID records of `114/Props` zeroed: PASS.

  A save that never wrote a row GUID, or a view stamp, is accepted.
- **Evidence against the rule:** in the 196 control/null pairs there are 11,933 GUID fields.  5,426
  are zero on both sides (so they need no mask) and **none is zero on one side only**.  Nothing in
  `trace-gate-fields-g0b.md` shows a GUID turning into zeros between two saves.
- **Widest masks, for the record:** the largest masked share of a stream is Cons Fixed2Data 100 %
  and Cal Fixed2Data 92.6 % (rows that are one GUID, `trace-gate-fields-g0b.md:15-16`); the widest
  contiguous span is 240 bytes there, 32 bytes in Assn and Rsc Fixed2Data, 16 elsewhere, 1 byte in
  the view bag.  Each is a named GUID; only the zero case is unevidenced.  (Of the 16 bytes, 0..5
  and 9 move between a control and its null; the others are stable on one machine.)
- **Fix:** mask a GUID field only when both sides are version-1 GUIDs.  Test: a zeroed GUID FAILs.

### R36G0b-5 (High): the committed acceptance test crashes at the tip, and is red once the crash is stepped over

- **Where:** `tools/trace/test_trace_gate.py:222-228`.  The sweep takes every `*-control-null.mpp`
  and opens `<stem>-control.mpp`.
- **Failure:**
  - At `9fe128a` six nulls have no file of that name: `wave23-flags`, `wave27-percent-actuals`,
    `wave27-priority-750`, `wave31-resource-sheet`, `wave31-tables`, `wave31-usage` (their controls
    are `wave23-outline`, `synthetic-base` and `wave27-priority-control`, per
    `tools/trace/capture/specs6-g5-*.ps1`).  The test stops with `FileNotFoundError` after 103 PASS
    lines, exit 1.  No sweep runs, and the 116 checks after it do not run either.
  - With that one line stepped over in a copy (a null with no `-control.mpp` is skipped): 223 PASS,
    1 FAIL, exit 1.  `W36-0b.2 positive: 196 pairs, 7 FAIL; negative: 185 edits, 2 PASS`; the
    failing check is the hard-coded set of two not-same-state pairs (R36G0b-7).
  - So `trace_gate_guards` is red on the branch tip, and the acceptance counts in the record
    (87 of 89, 97 of 99) describe a file set that is no longer the committed one.
- **Fix:**
  - Read the control of a null from one table (the specs' `Ctl`), not from the file name; a null
    whose control cannot be found is a FAIL line, not a crash.
  - Settle R36G0b-7, then update the two sets and the counts in `gates-g0b.md`.

### R36G0b-6 (High): eleven guards of the gate can be removed and the test stays green

- **Where:** `window_audit()` in `test_trace_gate.py:332-392`.  For the parsed streams it takes
  the expected mask from the gate (`g.canonical(RS, with_noise=True)[1]`), and for the view bag
  from `g.font_item_garbage`.  The comment above it says the fields are "not read back from the
  gate"; for these six streams they are.  There is no test case at all for `SummaryInformation`.
- **Failure:** each of these, planted in a copy of the gate, leaves the test at 219 PASS, exit 0.
  For each I then built one wrong file that the real gate FAILs and the mutant PASSes, so none is
  an equivalent mutant (except the first, noted):

  | planted | wrong file the mutant passes |
  |---|---|
  | every `SummaryInformation` property replaced by a placeholder | Title changed |
  | any 12-byte FILETIME property treated as LastSave | CreateTime changed |
  | `_text_ok` always true | CEdl path record of control characters |
  | CEdl path record waived whatever its bytes | the same |
  | CEdl size words +0, +4, +8 zeroed unconditionally | +4 off by one |
  | `214/Props` record 0x00010007 zeroed unconditionally | not the CEdl length |
  | `Props14` path record waived whatever its bytes | control characters |
  | `Props14` tail names waived whatever their bytes | control characters |
  | every `Props14` tail entry dropped | the 0x3540000b entry removed |
  | `114/Props` GUID records masked without the version-1 or length test | a version-4 GUID |
  | view GUID string masked when only one side holds one | (PASSes on the real gate too, under the control mask: R36G0b-1) |
  | font byte taken from any bag record whose length is a multiple of 44 | no committed bag record I checked distinguishes it |

- **Fix:**
  - Write the expected noise of the five parsed streams in the test from
    `trace-gate-fields-g0b.md`, as is done for the `NOISE` streams, and find the font rows by the
    test's own bag parse.
  - Add the eleven cases in the right-hand column as FAIL cases, and R36G0b-2 to -4.

### R36G0b-7 (Medium): 13 of 202 control/null pairs FAIL the positive sweep, 11 of them not in the record

- **Where:** the positive acceptance; `NOT_SAME_STATE` in `test_trace_gate.py:220`.
- **Failure:** the record names two pairs.  At the tip:
  - by file name, 7 of 196 FAIL, each on one line `size-differs ???214/CEdl/Var2Data`:
    `wave27-unlink` (536->600), `wave28-newtask-auto`, `wave28-newtask-manual`, `wave28-split`
    (422->486), `wave30-inactivate` (384->422), `wave35-g3-assign-remove-port-saved` (486->422),
    `wave36-r7-font-bold-session2` (384->422);
  - the six nulls with another control name FAIL too.  `wave27-priority-750` on the same one
    line (422->486); the other five on 8 lines each: `114/TBkndAssn/Var2Data` (+60),
    `114/TBkndAssn/VarMeta` (+36), `214/CEdl/Var2Data` (+12), `214/CFilter/FixedData` (230->460),
    `214/CFilter/FixedMeta`, `Var2Data`, `VarMeta`, and `Props14` (428->408).
  - So all ten nulls of `3193808` ("control-nulls for the ten pairs that had none") fail, and
    one round 7 font pair.
- **The fields:** in `214/CEdl/Var2Data`, record 0x26400024 is 64 bytes in one save and 128 in the
  other (`wave27-unlink`, `wave28-newtask-*`, `wave28-split`, `-port-saved`), and record 0x2640003d
  (26 bytes) is present in one and absent in the other (`wave30-inactivate`, `-session2`).  Neither
  is decoded.  A null save made by a later Project build, or in a second session, changes them, so
  a real recording may too.
- **Fix:** decode 0x26400024 and 0x2640003d (what makes the table double, what adds the record),
  then either name them or re-record the nulls in the control's session.  The five 8-line pairs
  need their nulls re-made from the control with the build that made the control, or a note that
  a null from another build is not a null.  Do not widen a mask for them.

### R36G0b-8 (Medium, UNVERIFIED on a real recording): a recording whose stream lengths differ from the control cannot pass on the named fields

- **Where:** the same undecoded bytes as R36G0b-1, seen from the other side.  The control mask is
  refused when the reference's length is not the control's (non-stride streams) and a new row has
  none.
- **Measured:** `compare(null, control, control, control)`, that is the control mask emptied:
  **146 of 196 PASS, 50 FAIL** (21 on the view bag, 18 on Assn Fixed2Data, 12 on `114/Props`, 8 on
  each Assn meta stream, 7 on Rsc FixedMeta, 7 on the CEdl size, and the rest of the table in
  R36G0b-1).  So for any edit that changes the length of `214/CV_iew/Var2Data`, `114/Props` or an
  Assn meta stream, or adds an assignment row, a faithful trace is likely to FAIL on per-save bytes
  that no field names.  This is R36G0-5's question with the ceilings gone.  No real recording was
  available to me, so it is a projection from the committed pairs.
- **Fix:** as R36G0b-1; and assert in the ctest that every control/null pair passes with the
  control mask emptied.

### R36G0b-9 (Medium, UNVERIFIED): `host.ps1` was read, not run; one reply shape is still refused

- **Where:** `tools/trace/kvp/host.ps1`, the `$tail` check in `vmx`, against `kvp/agent.ps1`.
- **Read:**
  - The check accepts an announced length 1 or 2 more than the trimmed reply.  That fits the
    agent's `Out-String` text (base64 plus CRLF or LF) whether or not the XML parse keeps the CR,
    and whether or not the newline falls in a piece of its own.
  - A short or padded reply is not accepted by it alone, and does not need to be: a reply one
    character short of an LF-ended text has `tail = 2` and passes the length check, but
    `FromBase64String` or the sha comparison then throws.  I could not make a mismatched reply
    return.
  - The agent's `catch` path sets `$o = "ERR $_"` with no `Out-String` newline.  `tail` is 0, so
    the host throws "reply length ... does not match" and the guest's error text is lost.  Before
    G0b it failed at the base64 decode, so this is no worse, but the message now points at the
    wrong fault.
  - `Out-String -Width 300` on a base64 line longer than 300 characters: if Windows PowerShell
    wraps it, the announced length gains a newline per wrap and every long reply is refused.  The
    stub's four-piece case does not model a wrap.  Not settled by reading.
- **Not done:** `test_host_skip.ps1` was not run (Python only in this review), and nothing ran on
  the VM.  The coordinator's one `vmx 'Get-Date'`, plus one reply over 300 characters, settles it.
- **Fix:** have the agent send the trimmed base64 length and its own sha in `out_meta`, and the
  host compare exactly; let an `ERR` reply through as an error with its text.

### R36G0b-10 (Low): the head line's "not checked" is always 0

- **Where:** `compare()`, `trace_gate.py:598` and `:636`: `unchecked` is set to 0 and never
  changed (only the "no null" return fills it).  The G0 review asked for the count so that a
  stream left out shows; as written the number carries no information.  Every stream is in fact
  compared or reported, so nothing is hidden today.
- **Fix:** drop the field, or count the streams whose mask was refused.

### R36G0b-11 (Low): no commit in the range carries the required trailer

- **Where:** `docs/handoff/wave36-brief.md:30` and `docs/START-HERE.md:96` require
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)` on every commit.
- **Failure:** 0 of 11 commits carry it (`git log --format=%B | grep -c`); each ends with a
  `Co-Authored-By:` line instead.  The G0 commits (`53351b2`, `2ecf42b`, `4ec0562`) do carry it.
  The record says "each commit carries the one trailer".
- **Item ids:** one per commit holds (W36-0b.1 to .10; `cea3c6d` is the group record).  The
  `ci.yml` and `CMakeLists.txt` comments added by `712b40d` say "W36-0b.9" for item 0b.10.
- **Fix:** the operator decides whether to rewrite the eleven messages before the merge; correct
  the two comments.

### R36G0b-12 (Low): the records overstate again

- **Where:** `docs/handoff/wave36-complete.md` (G0 table and the G0b section), `trace_gate.py`
  docstring, `test_trace_gate.py` docstring.
- **Failure:**
  - "every mask now names a field" and "The named fields are the whole of the waiver": not while
    rule 1, the control mask, stands (R36G0b-1).
  - "Task Fixed2Data +42 and +46..49 are data ... they are equal in every committed control/null
    pair": they differ in `wave30-baseline` and `wave30-wbs`, both committed before G0b.
  - "no size tolerance": two fields still absorb any size (R36G0b-2, -3).
  - `gates-g0b.md` gives the sweep as 87 of 89 and 97 of 99; at the tip it is 183 of 185 and
    189 of 196 (R36G0b-5, -7).
  - The test's module docstring still describes only W36-0.1 to 0.4.
- **Fix:** correct with the findings; give the counts with their commit.

## Not findings

- **No crash and no hang in the parsers.**  I read every meta and record reader for an unbounded
  loop (`frame_records`, `_tail_entries`, `_canon_summary`, `font_item_garbage`, `_meta_blocks`,
  `row_ids`): each advances by at least 12 bytes or is bounded by the stream length; the count
  word in `font_item_garbage` breaks at the end of the VarMeta.  Then a fuzz: 26 parsed streams
  and their companions of three pairs (`wave34-assign-add-second`, `wave35-g3-font-bold-off`,
  `wave30-calculate`), each truncated to 60-odd lengths, extended, poked with 0xffffffff, 0 and
  length + 1 at every word, and given 150 random bit flips, in the traced, the control and the
  both-sides role, under a 30 s watchdog.  **93,342 calls of `compare()`: 0 exceptions, none over
  3 s.**  2,148 mutated traced saves passed; 18 touched a byte outside the named fields and the
  control mask, and all 18 are a zero word written over bytes that were already zero next to a
  GUID field (the changed bytes lie inside the GUID).  A parse failure leaves the stream raw on
  that side, so it shows as a size or byte difference, never as equal.

- **The GUID mask is 16 bytes wide** although only bytes 0..5 and 9 moved in 5,723 same-machine
  fields.  The other bytes are the node and the version, which another machine changes, so the
  width is the field's.
- **The path fields** (CEdl record 0x26400031, `Props14` record 0x35400008 and the 0x3540000b
  name) accept any printable text of any length.  That is the named field.
- **`mask-refused` lines** are information, and stricter.
- **The no-null and same-file guards** were re-run by mutation only, not re-derived.

## Not checked

- Nothing was compiled or run under ctest; `trace_host_scripts` and `test_host_skip.ps1` were not
  run; the CI steps were not run.
- No real VM recording, and nothing in `build/trace-2021/` or the trace-evidence folder outside the
  repo, was read.  The notes that `trace-gate-fields-g0b.md` cites by file name were not opened, so
  its last column is taken on trust.
- The frozen copy of the tools on the VM.
- `build/w36/` logs (the builder's reds and greens) were not read; every result here is my own run.
- The fuzz covered three pairs and the parsed streams only, through `compare()`, not through a
  damaged compound file on the command line.
- The six odd-named nulls were swept with the control the capture specs name; whether those nulls
  were made from those controls rests on the commit message of `3193808`.

Probe, sweep and mutation scripts and their outputs are kept only in the reviewer's temporary folder.
