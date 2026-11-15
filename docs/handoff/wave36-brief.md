# Wave 36 brief: the writer wave, putting the Project 2021 trace's rules into the writers (for the Sonnet builder)

_Written Wed 11 Nov 2026 by the Opus coordinator, from Fable's audit and work order of 11 November 2026, section 4, and the operator's word "Yes, after round 6"._

**Baseline.**  Branch `wave36`, cut from `main` at `b2c0b19`: the wave 35g merge, with CI green on all seven jobs and no Critical or High open.  Wave 35g left no Critical or High, so nothing goes ahead of G0.  `main` holds waves 34 to 35g and the trace evidence: `spec/writers/evidence/trace-*.md` (rounds 1 to 6) and `tools/trace/`.

**The operator's goal is parity.**  This wave lifts writer refusals where the trace explains the bytes.  Every lift is held to the three criteria below, and `docs/handoff/standing-rules.md` binds.

## 0. The lift rule

A refusal is lifted only when all three hold:
- **(a)** the trace names the writing routine and the source of every byte the writer emits;
- **(b)** the rule predicts, byte for byte outside field-level masks, at least one committed pair that was NOT used to derive it;
- **(c)** the port's save of that edit matches the fixture field by field (standing rule 1), and 4(e) reads the edit back through Project.

Where (b) has no held-out pair, the writer stays limited to its measured shapes, and the record says so.

**An explanation is a hypothesis.**  The trace notes say what Project did in the recorded runs.  The held-out pair is the test of a general rule.  Don't generalise a writer because a note "explains" it.

**Port tests mask by exact field** (record id and offset), never by stream name.

## 1. Rules for this wave

The wave 35g brief's section 1 rules apply unchanged, with `36` for `35g`:
- the marker `WAVE-36-<group>-COMPLETE` (CLOSE: `WAVE-36-COMPLETE`) in `docs/handoff/wave36-complete.md`, with each group's gate line, "Guards seen red", "Deferred" and "Found, not fixed";
- one finding or item id per commit;
- gates per commit under `build/w36/`;
- red then green, both quoted;
- specific needles;
- the trailer `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)` on EVERY commit;
- no names, and the three scans before every commit;
- LF line endings;
- no COM: the coordinator runs the oracle and 4(e);
- the builder writes only `gates-gN.md` in `docs/handoff/wave36-verification/`;
- "fixed" means fully closed.

Also:
- **A deferral needs evidence of an attempt:** the command run and the failure seen.
- **Machine limits:** the trace VM may be running.  Never start, stop or touch the VM, `vmconnect`, or anything under `build/trace-2021/`.  Keep builds to the gates the rules require.  An out-of-memory compiler error (C1060, C1076, C3859, heap space) is rerun once and logged; it is not a code fault.
- **A group whose preconditions don't hold is not started.**  The driver records BLOCKED with the missing fact.

## 2. Groups

### G0: the gate and the records

Precondition: the trace agents no longer read the repo's `tools/trace`.  They work from a frozen, hash-listed copy in `build/trace-2021/tools-frozen/`, which the coordinator confirmed before launch, so G0 may edit `tools/trace` freely.  Never touch `build/trace-2021/` or the frozen copy.

- **W36-0.1 (A-2): the noise allow-list moves into `tools/trace/trace_gate.py`.**
  - It becomes per-stream byte ceilings and exact offsets, taken from the null run.  Gone are the by-name whole-stream waivers that `build/trace-2021/gs6.ps1` applied.
  - The four known noise sources each get a narrow mask:
    - the `UuidCreateSequential` GUID counter bytes;
    - the creation date-time stamps (Assn Var2Data, Task FixedData);
    - the uninitialised top byte of the font word (payload +43);
    - the per-file Props identity records.

    Their offsets and record ids are in `trace-insert-milestone-r4.md`, `trace-bold-r4.md`, `trace-font-item-r6.md` and `trace-props-framing.md`.
  - Exit 0 means PASS.  `tools/trace/capture/runone6.ps1` records the exit code.
- **W36-0.2 (A-4): no wrong-length mask.**
  - The byte mask is refused when the reference and control lengths differ.
  - Fixed-stride streams (Task FixedData 202-byte rows, Task Fixed2Data 64, Assn Fixed2Data 48, Assn FixedData 110) are masked per record.
- **W36-0.3 (A-3): an absent control-null is a FAIL** named "no null", never a pass.
- **W36-0.4 (A-6): a ctest that flips one byte in each stream class of a committed pair and asserts FAIL.**  It also asserts `checked > 0` and an equal stream set.
- **W36-0.5 (A-11): tighten the skip script and the KVP host.**
  - `tools/trace/capture/r5b-skip.ps1` invokes "Skip for now" only in a dialog whose title starts "Sign in".
  - `tools/trace/kvp/host.ps1` throws on a hash MISMATCH and checks the reply length.
- **W36-0.6 (A-9): records.**
  - `docs/START-HERE.md` is rewritten under 8 KB: state, read order, commands and house rules.
  - The wave history moves to `docs/handoff/history-waves34-35.md`.
  - `memory/coordinator.md` is brought current.
- Any Critical or High from wave 35g's reviews goes ahead of these.  The coordinator adds them here before launch.

Done when every guard is seen red then green, both quoted.  Marker `WAVE-36-G0-COMPLETE`.

**After G0, the coordinator:** re-gates all 109 recordings, plus the round 7 captures, with the committed gate inside the VM, and commits the gate texts.  Every FAIL becomes a finding with an id before G1's lifts are merged.

**Group order after the G0 review (coordinator, Wed 11 Nov 2026).**  G0 and G1 ran first.  The G0 review
found 1 Critical and 3 High in the gate, so the driver was stopped between groups and relaunched with
G0b, G2 and CLOSE.  G0b runs before G2.

### G0b: the gate, fixed (the G0 review's Critical and Highs, first)

Source: `docs/handoff/wave36-verification/review-g0.md` (R36G0-1 to -11; every finding has a **Fix:**
paragraph; read it before the item).  The review ran the gate with an unedited save posing as "traced" and the
edit as the reference.  The gate PASSed 12 of 89 pairs that way, so the gate passes wrong files.  Nothing in
G1 or G2 depends on the trace gate, but the merge does: no Critical or High may be open.

**The design rule for every item here: a mask names a field, never a region.**
- A noise mask is an exact (stream, record id or row uid, byte offset, length) taken from a decoded noise
  source in the evidence: the `UuidCreateSequential` GUID counter bytes in the Fixed2Data rows; the creation
  date-time words; the per-file Props identity records by record id; the path and user-name strings in
  `CEdl` and `SummaryInformation`; and the font word's top byte at payload +43 of each `0x2240000c` item in
  `214/CV_iew/Var2Data`.
- No window spans a stream, a row or a record.
- No size change is waived.  A size change is allowed only where a decoded variable-length noise field (a
  path or user-name string) explains it exactly.
- Ceilings copied from one null run are gone.

Items:
- **W36-0b.1 (R36G0-1):** remove the whole-stream window on `214/CV_iew/Var2Data`.  Mask only the font word's
  top byte of each item, located by parsing the bag records (`trace-bold-r4.md`, `trace-font-item-r6.md`).
  This also closes R36G0-6.
- **W36-0b.2 (R36G0-2):** `214/CEdl/Var2Data`, `Props14` and `SummaryInformation` are compared field by field.
  Path and user-name strings are masked as decoded fields, and any other byte or length change FAILs.
- **W36-0b.3 (R36G0-3):** stream names match exactly, never by suffix.  `DocumentSummaryInformation` is
  compared in full, because the port writes it.
- **W36-0b.4 (R36G0-4):** remove the windows over Assn Fixed2Data +32..+42, the creation stamp of EXISTING
  Task FixedData rows, and Props record 0x02401394.  Mask only what the evidence names as per-save noise,
  quoting the note and line for each mask in a comment.  The trace notes are leaving the public repo (the operator's D-4): read them from `D:devmsproject-mac-traceevidence`, and cite them by file name only.
- **W36-0b.5 (R36G0-5):** there are no single-run ceilings.  The acceptance tests below replace them.
- **W36-0b.6 (R36G0-7):** settle the KVP reply-length check without the VM: build the guest's `Out-String`
  reply shape in a unit test, then make `host.ps1` accept exactly that shape, trailing newline and CR
  handling included.  The coordinator confirms it on the VM with one `vmx` call before the re-gate.
- **W36-0b.7 (R36G0-8):** the flip test also FAILs when a window covers a whole stream, a row or a record.
  Assert, for every stream class, that a flip at EVERY offset outside the named noise fields FAILs.
  Sampling is not enough; the stream sizes are small enough to do every offset.
- **W36-0b.8 (R36G0-9):** fixed-stride masks map rows by unique id, read from the row's uid field.  A row
  missing on either side is a FAIL.
- **W36-0b.9 (R36G0-10, R36G0-11):** correct the three overstated sentences, commit `gates-g0.md`, and
  restore the standing-facts section (the writer inventory) into `history-waves34-35.md` verbatim.

**Acceptance, in a committed ctest (`trace_gate_guards`):**
- **Negative:** for EVERY committed pair in `tests/data` whose edit file differs from its control-null outside
  `SummaryInformation`, the gate run with TRACED = the control-null and REFERENCE = the edit must exit
  non-zero.  Report the count; none may PASS.
- **Positive:** for every pair that has two independent saves of the same content, the gate must exit 0.
  Wave 36's round 7 pairs give these: the control and its control-null are both saves of one file.  Run
  with TRACED = the control-null and REFERENCE = the control.
- Both counts go in the commit message and `gates-g0b.md`.  If the positive test cannot pass without
  widening a mask beyond a named field, stop that item and record the field that is not decoded.  Do not
  widen it.
- CI has no `olefile`, so `trace_gate_guards` skips there.  Add `olefile` to the CI Python setup in
  `ci.yml`, so the test runs in CI too.

Done when every item is seen red, then green, both quoted.  Marker `WAVE-36-G0b-COMPLETE`.

### G1: the font item (the pilot)

Precondition: `trace-font-item-r6.md` is committed and answers "which bits, which instruction".  It is: `ad388e9`.

Writer: `core/mppwrite/viewstream_edit.{h,cpp}`.  Today it allows Bold set on uid 2's Name cell, and Bold off of that one measured item.  The facts from the trace (`trace-font-item-r6.md`, `trace-bold-r4.md`):
- **Calls and placement:** one set-by-id call per edit (id 0x2240000c, kind 1), 44 bytes per selected row, in the view's property bag.  A new item is appended at the bag's end, with the same framing as Props records.
- **Payload layout:**
  - +0 the row number;
  - +4 the field key;
  - +8 the word 3, or 6 for size and name;
  - +10 the attribute mask: 0x100 Bold, 0x200 Italic, 0x400 Underline;
  - +12 and +24 the colour triples;
  - +36 one byte, toolbar path only;
  - +40 the font word.
- **The font word:** bit 0x01 Bold, 0x04 Italic, 0x02 Underline, 0x10 face or size differs, 0x40 the colour group.  The top byte is never written, so it is masked at field level as uninitialised memory.
- **Font size and font name are not stored in the item.**  Their storage is not located.
- **Bold off** rewrites the existing payload in place: mask 0, flags 0x00000003, font word bit 0 clear.

Items:
- **W36-1.1:** wire Bold off.  The writer is built; the pair is `wave35-g3-font-bold-off`.
- **W36-1.2:** Bold on any task row, and on the Start column.  Pairs: `wave35-g3-font-bold-row2`, `wave35-g3-font-bold-start`.  The held-out check (b) is the pair not used to derive the row and field rule.
- **W36-1.3:** Italic (`wave35-g3-font-italic`).
- **W36-1.4: 4(e) for the font item must stop being vacuous.**  Find a COM read-back of cell font state for the coordinator's 4(e) script (`Font.Bold` and `Font.Italic` of the selected cell after `SelectTaskField`), or record the attempt and the method that failed.  The builder writes the read-back plan into the group record; the coordinator runs it.
- **Underline:** in this group only if (b) holds for it (pair `wave35-g3-font-underline`, toolbar path).
- **Size, name and colour:** not started.  The record says "storage of font size and name not located (trace-font-item-r6.md)".  Colour waits on the same note's open bits 0x40 and 0x80.
- **Range, combined attributes and a second change to a cell:** in this group only if capture round 7 item 2 captured them and (b) holds.  Otherwise not started, with the missing pair named.

Note that `wave35-g3-font-bold-range` is a one-row bold, mislabelled.  The trace's two-row selection gave one 88-byte item.  Keep the fixture file, and correct its label in `spec/writers/view-stream-214.md` and the evidence note (decision D-2).  Never rename or edit the fixture.

**Group order after the G1 review (coordinator, Wed 11 Nov 2026).**  The G1 review found 1 High in the font
writer (R36G1-1), so the driver was stopped after G0b and relaunched with G1b, G2 and CLOSE.  G1b runs
before G2.

### G1b: the font writer, held to the lift rule (the G1 review's High, first)

Source: `docs/handoff/wave36-verification/review-g1.md` (R36G1-1 to -7; read each **Fix:** paragraph).  `main`
was merged into this branch before G1b, so round 7's pairs are now in `tests/data` (`wave36-r7-font-*`;
`build/r7/oracle_r7_font.json` records how each was made).

- **W36-1b.1 (R36G1-1): Bold on is limited to the three measured cells,** (row 1, Name), (row 2, Name) and
  (row 1, Start), as measured by the pairs `wave35-g3-font-bold-row2`, `wave35-g3-font-bold-start` and
  `wave30-textstyle`.  Every other row or column, and the joint cell (row 2, Start), is refused again with the
  decision-9 wording.  Remove the false "no byte of these pairs was used" claims
  (`tests/roundtrip_viewstream.cpp:582`, `spec/writers/view-stream-214.md:236`).
  - Then check whether any round 7 font pair is a held-out prediction for a measured shape that the writer
    accepts.  `wave36-r7-font-bold-session2` is Bold on (row 1, Name) in a second Project session;
    `wave36-r7-font-second-cell` is Bold on (row 2, Name) beside an existing item.
  - For each pair the writer accepts, the port's save must match it field by field (field-level masks only),
    and the test names it as held out.
  - A pair the writer refuses stays refused, recorded as "not lifted: needs <fact>".
- **W36-1b.2 (R36G1-2):** the spec and the code agree on the joint cell.
- **W36-1b.3 (R36G1-3):** the writer checks that the row exists in the file, not only the app.
- **W36-1b.4 (R36G1-6):** a Qt gate case for B and I with Undo and Redo against Save, and one for a read-only
  file.
- **W36-1b.5 (R36G1-7):** a second Bold click before Save says why it was refused.
- R36G1-4 (the 4(e) method) and R36G1-5 (the Props14 marker byte's writer) go to the record as open, unless
  the fix is a record line.

Done when every guard is seen red, then green, both quoted.  Marker `WAVE-36-G1b-COMPLETE`.

**Group order after the G0b review (coordinator, Sat 14 Nov 2026).**  The G0b review found 6 High in the gate and
its test, so the driver was stopped after G1b and relaunched with G0c, G2 and CLOSE.  G0c runs before G2.

### G0c: the gate, second fix (the G0b review's six Highs, before G2)

Source: `docs/handoff/wave36-verification/review-g0b.md` (R36G0b-1 to -12; every finding has a **Fix:**
paragraph and the command that showed it; read each before its item).  The review ran at `9fe128a`, after
`main` was merged in: `tests/data` now holds round 7's pairs (624 `.mpp` files), which G0b never saw.
`trace_gate_guards` is red at that tip: it stops with `FileNotFoundError` after 103 PASS lines.  The
coordinator reproduced that.  No Critical: the negative sweep let no wrong file through (183 of 185 FAIL; the
two that PASS are the known no-op edits).  But the gate still passes wrong files built by hand, so the merge
rule is not met.

The G0b design rule stands: **a mask names a field, never a region.**  G0c adds: **a mask also checks the
value it masks.**  A masked field is accepted only when both sides hold a well-formed value of the named kind,
of the length that kind has.

Items, in this order:
- **W36-0c.1 (R36G0b-5, R36G0b-7): the test runs on the committed file set.**
  - A control-null's control comes from one table in the test (the capture specs' control name), never from
    the file name.  A null whose control cannot be found is a FAIL line, never a crash.
  - The positive sweep then covers every control and control-null in `tests/data`.  Each pair that fails it is
    listed by name in the test with the stream, the record id and the reason, read from the gate's output.
    The review counted 13 of 202; `214/CEdl/Var2Data` records 0x26400024 and 0x2640003d explain 8.
  - A pair on that list is reported as OPEN with its undecoded field.  It is not a PASS, and the list does not
    grow without a named field.  The counts in `gates-g0c.md` are those of the committed file set.
- **W36-0c.2 (R36G0b-4):** a GUID field is masked only when BOTH sides are version-1 GUIDs.  All-zero against
  a real GUID FAILs.
- **W36-0c.3 (R36G0b-3):** a `SummaryInformation` string slot is accepted only when its length is exactly
  8 + the string length rounded up to 4, and its padding is zero.  Bytes after the string FAIL.
- **W36-0c.4 (R36G0b-2):** the `Props14` tail entry 0xffffffff is handled like 0x3540000b.  Its 8-byte header
  stays in the canonical form; only a well-formed text name is replaced; the entry may be absent on one side
  only as a named case (one entry, a text payload).  Junk in the entry FAILs, and a second entry FAILs.
- **W36-0c.5 (R36G0b-1): the control mask no longer excuses a byte that no field names.**
  - For each row of the review's table (nine streams, 48 of 196 pairs): decode the bytes and add the field to
    `NOISE` with its evidence line, or leave them unmasked so the pair FAILs and goes on the OPEN list of
    W36-0c.1 with the field recorded as not decoded.  Do not widen anything.
  - Assn Fixed2Data +32..+37 and +41, and Task Fixed2Data +42 and +46..+48, are data by
    `trace-gate-fields-g0b.md`.  They are never masked.
  - The control and its null are then used only to tell a new row from an old one.  The gate prints
    `control-mask <stream> <n>` and counts a problem for any byte the control mask alone would excuse.
  - Test: for every committed pair, the control mask minus the named fields is empty; and each of the review's
    seven inverted-byte files FAILs.
- **W36-0c.6 (R36G0b-6): the test does not read its expectations from the gate.**
  - The expected noise of the five parsed streams and the font rows are written in the test, from
    `trace-gate-fields-g0b.md` and the test's own bag parse.
  - Add a FAIL case for each of the eleven surviving mutations in the review's table, and a
    `SummaryInformation` case.  Each is seen red with the mutation planted, then green.
- **W36-0c.7 (R36G0b-9):** in `tools/trace/kvp/host.ps1`, the agent's `ERR` reply reports the agent's error,
  not a length error.  Settle the wrapping of a reply over 300 characters in the unit test's reply model.
  The coordinator confirms it on the VM.
- **W36-0c.8 (R36G0b-10, R36G0b-12):** the head line's "not checked" count is real or is removed.  Correct
  the overstated sentences in the record.
- **R36G0b-8** goes to the record as open: it needs a real recording, which the coordinator's re-gate gives.
- **R36G0b-11:** the G0b commits carry `Co-Authored-By`, not the trailer rule 1 requires.  History is not
  rewritten.  Record it in the G0c row, and from this group on EVERY commit ends with
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)`.

The trace notes are in the private repo beside this one (`../msproject-mac-trace/evidence`).  Cite them by
file name only.  This group changes no C++ source.

**Acceptance, in `trace_gate_guards`, on the committed file set:**
- **Negative:** every pair whose edit differs from its control-null outside `SummaryInformation` exits
  non-zero.  The no-op edits are listed by name with the reason.
- **Positive:** every control and control-null exits 0, except the OPEN list of W36-0c.1.
- **Hand-built wrong files:** the review's cases for R36G0b-1 to -4 and the eleven of R36G0b-6 all FAIL.
- All counts, with their denominators, go in the commit messages and `gates-g0c.md`.

Done when every item is seen red, then green, both quoted.  Marker `WAVE-36-G0c-COMPLETE`.

**Group order after the G0c review (coordinator, Sat 14 Nov 2026).**  The G0c review found 2 Critical and 3 High
in the gate and its test, so G0d runs alone next, before G2.

### G0d: the gate, third fix (the G0c review's two Critical and three High, before G2)

Source: `docs/handoff/wave36-verification/review-g0c.md` (R36G0c-1 to -11; each finding has the command
that showed it and a **Fix:** paragraph; read each before its item).  The coordinator reproduced R36G0c-1
by command line: `wave30-calculate` with `214/CEdl/Var2Data` +0 set from 586 to 0 exits 0; set to 587 it
exits 1.

The G0b and G0c rules stand: **a mask names a field, and checks the value it masks.**  G0d adds: **a
placeholder in the canonical form can never equal a value a real field can hold**, and **every guard in the
gate has a test that goes red when the guard is removed.**

Items, in this order:
- **W36-0d.1 (R36G0c-1):** derived size words are not replaced by 0.  Keep a separate "equals the derived
  value" flag per derived field (CEdl Var2Data +0, +4, +8; CEdl VarMeta +20; 214/Props record 0x00010007), or
  a sentinel no raw word can take, and print `derived <stream> +n` when only one side is derived.  Tests:
  each of the five set to 0 FAILs on a pair whose true value is not 0; an emptied CEdl path record FAILs.
- **W36-0d.2 (R36G0c-2):** streams are keyed by their full raw name, never a printable rendering.  Two
  directory entries that render the same are a FAIL named `stream name collision`.  A stream present on one
  side only is a FAIL.  The "N streams" count is the count of raw entries.  Test: the review's patched
  directory FAILs.
- **W36-0d.3 (R36G0c-3):** the view GUID string is parsed to its 16 bytes and accepted only as a version-1
  GUID with a non-zero timestamp, the same as the binary GUIDs.  Tests: all-zero digits FAIL; a changed
  version digit FAILs.
- **W36-0d.4 (R36G0c-4):** a test case for each `_lpstr_slot_ok` type check: the Author, LastAuthor and
  RevNumber type words set to 0x1F and to 0 each FAIL.  Seen red with the check removed.
- **W36-0d.5 (R36G0c-5):** test cases for the three G0b-era guards (`marks[0] != 4`, the Props14 blob
  size-word cross-check, the 2048-byte text cap), each seen red with the guard removed.
- **W36-0d.6 (R36G0c-6):** each masked noise field checks its value as well as its place: the two creation
  stamps are non-zero and not all-ones; the Props noise words and LastSave are of their decoded kind; the
  Author strings pass the text test.  Take the accepted range of each from the committed control and null
  saves, and quote the evidence line in a comment.
- **W36-0d.7 (R36G0c-7):** each OPEN entry is pinned as (pair, stream, count of differing bytes), and the
  test FAILs when a count grows or a pair joins the list.  Correct the two "data, never masked" reasons that
  the null saves contradict (Assn Fixed2Data +32..+37 and Task Fixed2Data +42, +46..+48 differ between a
  control and its null with no edit): say they are OPEN, not masked, and why.
- **Meta-test:** a committed script, run by `trace_gate_guards`, that removes each named guard of the gate
  in turn (in a temporary copy) and asserts the test goes red.  Every guard the review names, and every guard
  G0d adds, is on its list.  Report the count: guards listed, guards red when removed.
- R36G0c-8 to -11 (Low) go to the record as queued, unless the fix is one line.

This group changes no C++ source.  It does not touch the VM, `build/trace-2021/` or `build/r7/`.

**Acceptance, in `trace_gate_guards`:** the G0c acceptance stays green (negative sweep all FAIL, positive
sweep PASS or pinned OPEN, every hand-built wrong file of review-g0b.md FAILs); every hand-built wrong file
of review-g0c.md FAILs; the meta-test shows every listed guard red when removed.  All counts, with their
denominators, go in the commit messages and `gates-g0d.md`.

Done when every item is seen red, then green, both quoted.  Marker `WAVE-36-G0d-COMPLETE`.

**Group order after the G0d review (coordinator, Sat 14 Nov 2026).**  The G0d review found no Critical and 1 High
in the gate's test, so G0e runs alone next, before G2.

### G0e: the gate's test, made to hold every guard (the G0d review's High, before G2)

Source: `docs/handoff/wave36-verification/review-g0d.md` (R36G0d-1 to -6; each finding has the command that
showed it and a **Fix:** paragraph).  The review found no Critical.  Its one High is in the test, not the
gate's verdicts: four guards of the gate are not on the meta-test's list and can be removed with the test
still green.  The coordinator reproduced one: with the SummaryInformation section-size check removed, the
test reads 491 PASS, exit 0.

The rule for this group: **the meta-test's list of guards is derived from the gate, not written by hand.**

Items, in this order:
- **W36-0e.1 (R36G0d-1):** the meta-test derives its candidate guards from `trace_gate.py` itself (for example
  with `ast`: every condition that leads to a problem line, a FAIL or a refusal), and removes each in turn.
  A candidate may be left off only on an explicit "redundant, with reason" list in the test, each reason
  naming the other guard that covers it.  A crash no longer counts as red: a removal that crashes is a FAIL
  of the meta-test, named.  Add a FAIL case for each of the four guards the review found green (the Props14
  tail count word, the SummaryInformation section size, the first value following the table, the LastSave
  type word 0x40), built from the review's wrong files.  Report: candidates derived, removed red, listed
  redundant, crashed.
- **W36-0e.2 (R36G0d-2):** a masked value is accepted only inside the range the committed saves show, quoted
  from them: the creation stamps within the span of the committed control and null stamps; the two 114/Props
  save words equal to each other (they are equal in all 624 files); LastSave within the span of the
  committed saves.  Tests: the review's six files (stamp = 1, stamp = 0x7fffffff, both save words = 1, the
  two words unequal, LastSave in 2600, LastSave in 2001) each FAIL.
- **W36-0e.3 (R36G0d-3):** an emptied Props14 tail name is not equal to its placeholder (the same `payload
  and` guard the path record has).  Test: both tail names emptied FAILs.
- R36G0d-4 to -6 (Low) and R36G0c-9, -10 go to the record as queued, unless the fix is one line.

This group changes no C++ source.  It does not touch the VM, `build/trace-2021/` or `build/r7/`.  The
coordinator is re-gating every recording with the G0d gate while this group runs; a change here that turns
any verdict means those targets are re-gated after G0e.

**Acceptance, in `trace_gate_guards`:** everything G0d accepted stays green; every hand-built wrong file of
review-g0d.md FAILs; the derived meta-test shows every candidate red when removed, or listed redundant with a
reason, and none crashed.  All counts, with their denominators, in the commit messages and `gates-g0e.md`.

Done when every item is seen red, then green, both quoted.  Marker `WAVE-36-G0e-COMPLETE`.

**Group order after the first re-gate (coordinator, Sun 15 Nov 2026).**  The re-gate with the G0d gate FAILed
all 231 recordings on fields the gate does not yet name, so G0f runs next, before G2.

### G0f: the gate names the machine and session fields the re-gate found (before G2)

Source: the first re-gate of every recording with the G0d gate (`b57a87c`) FAILed all 231 recordings with a
saved copy; none PASSed.  The failure dossier, `build/trace-2021/regate/DOSSIER.md` (git-ignored, private
evidence: cite it by section name only, never copy its paths or values into a tracked file), takes every
failure down to the field.  It sorts each differing field into one of four kinds:
- **machine**: differs because the trace VM is not the host;
- **session**: differs between any two saves on one machine;
- **data**: a recorded save holds different content from its reference;
- **unknown**: not yet decided.

Its prediction, which is arithmetic and not a gate run, is PASS 210, FAIL 13 (a data field remains) and
UNKNOWN 8, if every machine and session field were masked by name.

The rules stand: **a mask names a field and checks the value it masks; a placeholder never equals a real
value; every guard has a test that goes red when it is removed.**  G0f adds: **a field is masked only with
evidence that it is noise**:
- for a session field, the committed control and control-null (two saves on one host) differ there, or the
  36 recordings saved with the same path and user name as their references differ there;
- for a machine field, its decoded value on the VM side is the VM's value (its path, its user "trace", its
  language id 2057, its rich-text build 16.0.20430) in every recording.
Each mask's comment cites the DOSSIER section and the decoded values.  Data and unknown fields are never
masked.

Items, in this order:
- **W36-0f.1, Props14:** the saved-by user name in the tail entry is read as `[id][byte length][UTF-16
  name]` of any even byte length with a valid text name, not only length 0x1e.  The name and its length word
  are a machine field.  The saving path stays a decoded field.  The tail count, the 0xffffffff entry and
  record 0x3540000a follow the DOSSIER's session evidence, or they are left compared.  Test: a 10-byte name
  and a 16-byte name are each read; a wrong length word FAILs.
- **W36-0f.2, the CEdl task-order slot table (record 0x26400024):** its uids are compared one by one; only
  the trailing 0xffffffff fill, which is the allocation capacity, is a session field.  The size words follow
  from the canonical form.  Record 0x2640003d, present on one side, is masked only if the DOSSIER's evidence
  shows it in a host control and null pair; otherwise it stays compared.  Tests: a changed uid FAILs; a
  different capacity with equal uids passes.
- **W36-0f.3, the note rich text in Task, Assn and Rsc Var2Data:** `\deflang<n>` and the rich-text generator
  build string are machine fields, decoded inside the RTF payload, with the value checked as a language id
  or a build string.  Everything else in the note is compared.  Test: a changed character of note text
  FAILs.
- **W36-0f.4, the session fields of the DOSSIER's "session" lists:**
  - the CEdl date words, as dates within the span of the committed saves;
  - the view-state records and pad bytes the DOSSIER names;
  - the 114/Props stamp words and GUID record;
  - the extra view GUIDs, the third Assn GUID and the creation stamps, as version-1 GUIDs and stamps with
    value checks;
  - the stale memory in the baseline sub-records: the module base and the per-process counter at the
    offsets named, each checked as such;
  - the FixedMeta and Fixed2Meta bytes the DOSSIER calls session.

  Each mask is one named field with its offset and length, and a test case that FAILs when its value is
  out of kind.
- **W36-0f.4b, the Gantt chart pane width (round 12, `build/trace-2021/regate/CAUSES.md`, the zoom
  section):** the u16 at +1172 of the Gantt view block, and the date words the same section says follow
  from it, are a machine field (the host references hold 599 at a window width of 966 and 511 at 900; the VM
  wrote 351 and 355).  Mask them only in that record, with a value check (a plausible pixel width), and
  keep everything else in the block compared.  Test: the five zoom recordings' shape passes; a changed
  zoom unit word in the same block FAILs.
- **W36-0f.5:** the meta-test lists every new guard.  Every one is seen red when removed.
- **W36-0f.6:** the gate's own acceptance, unchanged (negative sweep all FAIL; positive sweep PASS or pinned
  OPEN; every hand-built wrong file of review-g0b.md, review-g0c.md and review-g0d.md FAILs), plus a new
  case.  For each of the 13 "data" targets the DOSSIER lists, a file built from the committed reference
  with that data field changed to the recorded value FAILs.

This group changes no C++ source.  It does not touch the VM, `build/trace-2021/` (read only) or
`build/r7/`.  After the marker the coordinator re-gates every recording again.

Done when every item is seen red, then green, both quoted.  Marker `WAVE-36-G0f-COMPLETE`.

**Group order after the G0f marker (coordinator, Sun 15 Nov 2026, from the audit of that day).**  G0f is the last
gate group ahead of the goal.  G2 and CLOSE run next in one driver run, without waiting for the G0f review.  From that
review on, standing rule 5 applies to the gate as to everything else: a Medium or Low is queued in
`plan-waves27-30.md`.  A Critical or High that lets a wrong file pass goes in as items inside the next writer group,
not as a gate group of its own.

### G2: Props

Writer: `core/mppwrite/props_edit.{h,cpp}`, and the plan layer.  The facts from the trace (`trace-props-framing-r4.md`, `trace-baseline-r4.md`, `trace-calculate-r3.md`):
- **New-record framing:** length (4), id (4), kind word (2), flags word (2), then the payload, appended at the end of the blob.  Blob +0 (the size) += 12 + length, plus 1 pad byte if the length is odd.  Blob +8 (the record count) += 1.  The stream's two leading size words follow the blob size.
- **Per-table typed properties:**
  - id `0x0001000N` holds table N's final Var2Data size, `[table+0x344]`;
  - id `0x0100000N` holds table N's entry count, `[table+0x250]`;
  - N = 1 Task, 2 Rsc, 3 Cal, 4 Assn.

  Both are rewritten at save, after the var blocks are laid out (the publish at 0x397993 / 0x3978d9).  The view tables use the same rule, e.g. 214/Props.
- **Existing records** are rewritten in place by id.

Items:
- **W36-2.0, first:** the trace gate's `LASTSAVE_SPAN` and `STAMP_SPAN` (and `CVIEW_STAMP_SPAN`, if the same
  saves need it) are widened to cover the saves of 14 and 15 Nov 2026.  The coordinator's triage of Sun 15 Nov 2026
  found 107 recordings that FAIL only because their LastSave or stamp lies after the span's top.
  - The new ends come from decoded values: read every saved copy in `build/trace-2021/saved/work/`
    (git-ignored, read only) and the committed saves, decode the LastSave FILETIME, the Props save words and the
    stamps with the gate's own decoders, and set each span to the minimum and maximum seen.  No end is set by hand.
  - The gate, the test's copy of the spans and the evidence row in `trace-gate-fields-g0b.md` change together.
  - The tracked files carry only the numeric span ends and a count of the files read, never a file name, path or
    user name from `build/trace-2021/`.
  - Test: a save one tick past the new top FAILs; one at the top passes.  Red, then green.  One commit.
- **W36-2.1:** the per-table size and count words are computed from the tables the save lays out.  The measured-constant copies go.
- **W36-2.2:** new-record framing in the Props writer.
- **W36-2.3:** the plan gains a Props digest, which closes R35cG0-4.  Held-out check: the Props stream of every growth pair the rule was not derived from.  The baseline pair derived it, so check at least the insert, assign-add and resource-add pairs.
- **W36-2.4, last:** the meta-test leaves `trace_gate_guards`.  The ctest `trace_gate_guards` runs the acceptance
  sweeps only.  The guard-removal run becomes its own labelled test, `trace_gate_meta`, with its own timeout.  It is
  run on a fresh tree at each group close and in one CI job.  Time one full meta run and put the minutes in the
  commit message.  Test: `ctest -R trace_gate_guards` no longer runs the meta-test; `ctest -L trace_gate_meta`
  runs it.

### G3: Insert Task, Insert Milestone, Insert Summary

Precondition: `trace-insert-rules-r6.md` states a rule that predicts both held-out pairs.

- As committed (`ad388e9`), the note predicts the new row's FixedMeta entry byte-exact for the four wave35-g3 insert pairs.  But it derived the rule from those same four, so none is held out.
- It leaves open: the Props14 and CEdl rewrites, the source of the 3bf13138 start word, and +142 and +172 on summary rows.

**So the precondition is NOT met today.**  The coordinator launches this wave with the groups G0, G1, G2 and CLOSE only.  The CLOSE record lists G3 as "not started: no held-out insert pair (trace-insert-rules-r6.md)".  The coordinator re-checks the precondition against `spec/writers/insert.md`'s held-out pairs, and any round 7 one-factor pairs, before CLOSE.

If it is met, the two GUIDs and the creation stamp are masked by field.

### CLOSE

**First, the G0f review's findings that let a wrong file PASS** (`docs/handoff/wave36-verification/review-g0f.md`;
the coordinator reproduced R36G0f-1 and -2 on the gate at `8d0d032`: exit 0, 0 problems).  These are items of
this driver run, not a gate group (the audit of Sun 15 Nov 2026, W-2).  Each is seen red, then green, in its own commit:
- **W36-C.1 (R36G0f-1, Critical):** the Gantt view block's unit word at +1180 is masked only together with a
  changed pane width at +1172, and is checked against that width by the ratio the committed zoom references show.
  The window date words beside it stay compared unless the same evidence covers them.  Test: each of the five zoom
  pairs with only the unit word changed FAILs; the five zoom recordings' shape still passes.
- **W36-C.2 (R36G0f-2, High):** a creation stamp is masked only for a row, assignment or resource block whose uid
  is not in the control.  The stamp of a row present in the control is compared.  Test: wave30-calculate with
  one existing row's stamp changed (in span) FAILs; a new row's stamp still passes.
- **W36-C.3 (R36G0f-3):** CEdl record 0x2640003d, when present on one side only, keeps its header compared:
  only the evidence-backed payload is dropped.  Test: its kind word changed (1 to 0x99) FAILs on
  wave28-mark-auto.
- **W36-C.4 (R36G0f-8):** the host network address printed in `trace-gate-fields-g0b.md` (the node of a
  version-1 GUID) is replaced by "the host's node".  Test: `git grep` for the 12 hex digits finds nothing.
- The review's Mediums and Lows (R36G0f-4 to -7 and -9) are queued in `plan-waves27-30.md`, not fixed here.

Then:
- Rerun every gate on a fresh tree.
- Write the completion record with each id ticked: fixed (commit), partly fixed (what remains), deferred (evidence) or not started (the precondition that failed).
- Update START-HERE (under 8 KB), `memory/coordinator.md` and the plan.
- Mark `WAVE-36-COMPLETE`.  Push nothing.

## 3. Not in wave 36

These are wave 37, in this order:
- Notes: waits on the 6-byte Task Var2Data run (round 6 item 7).
- Set Baseline: 26 undecoded var blocks.
- Linked, lag and constraint Calculate: the scheduling arithmetic upstream of the four value stores is not unrolled.

## 4. The coordinator's part

After each group marker:
- fresh-tree gates in the verify worktree;
- mutations at each new guard, at the call site and in the helper;
- an independent read-only reviewer on every group (each one changes a writer).

After CLOSE:
- range scans, push, and CI on all seven jobs;
- a whole-wave review;
- 4(e) with checked and not-checked counts;
- fast-forward `main`, then ask the operator for the Fable audit.

**Merge `wave36` into `main` only when:**
- CI is green on all seven jobs;
- no Critical or High is open;
- 4(e) passes, with its checked and not-checked counts stated;
- the 109 recordings plus round 7 are re-gated by the committed gate, with every gate text committed;
- START-HERE is under 8 KB and true at the merge commit.
