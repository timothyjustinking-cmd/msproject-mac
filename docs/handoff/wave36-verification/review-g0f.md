# Review of G0f (f1b86da..c7ea56f), read-only

Reviewer worktree: `D:/dev/msproject-mac-wt-revg0f` (detached at c7ea56f, left in place, `git status` clean). Nothing in `D:/dev/msproject-mac` was edited, committed, checked out or stashed.
Scripts and logs: `D:/dev/msproject-mac-wt-revg0f/build/rev/` (`common.py`, `fuzz.py`, `evidence*.py`, `probe_*.py`, `cli_*.py`, `mut.py`, `muts_g0f.py`, `muts_g0f2.py`, `*.log`, wrong files in `files/`).
Python 3.14.4, olefile 0.47, `PYTHONIOENCODING=utf-8`, run from the worktree root. Mutations were made on temporary copies of the gate (`mut.py` runs `test_trace_gate.py --gate-dir <copy> --no-meta --fail-fast`; the worktree's gate was never edited).
Baseline: `python tools/trace/test_trace_gate.py --no-meta` at c7ea56f: 1739 PASS, 0 FAIL, "all passed", 94 s (the builder's 1739).

## Summary

G0f's masks are narrow in nearly every field I attacked: out-of-kind values and bytes next to the mask FAIL for the CEdl dates and view word, the slot table, the Props14 names and flag, the note language id and build, the CV_iew stamp and dates, the baseline-slot words, the third GUIDs and the per-row GUIDs. 0f.6 holds: 22 of 22 field cases FAIL, and 22 of 22 FAIL on the VM's own copies as well.

Three things are wrong. First, the Gantt pane "unit" word (+1180) is masked for any value from 1 up to the width, even when the width is unchanged, so a wrong zoom passes on all five committed zoom pairs and all five VM zoom copies (R36G0f-1, Critical). The brief says a changed zoom unit word FAILs. Second, the creation stamps of every Task row, assignment and resource are masked with no control/null evidence (0 of 788, 556 and 338 shared rows differ), so a changed stamp of an existing row passes (R36G0f-2, High). Third, the CEdl rows record is dropped whole, header included, so its kind word is unchecked (R36G0f-3, Critical by the letter of the scale, low practical weight).

The brief predicted PASS 210; the tip gives PASS 108 of the 210 VM copies whose reference is committed (R36G0f-4): the VM's newer saves lie above three spans.

**Count by severity: Critical 2, High 1, Medium 3, Low 3 (R36G0f-1 to -9).**

## Denominators

| what | count |
|---|---|
| committed `.mpp` read for evidence | 624 |
| committed control/null pairs (control by `CONTROL_OF`) | 191 (of the 202 control-nulls in the test: 11 have no committed reference or control in `tests/data`, as the glob finds them) |
| fuzz, committed: TRACED = the reference, mutated in every masked run | 191 pairs, 19,844 masked runs, at most 114,542 variants (each run: all 0xff, all 0x00, and a byte either side flipped by 0x01 and 0x80, only outside windows); 191 of 191 baselines PASS |
| fuzz, committed: variants that PASS | 380, all of 4 kinds: Props14 flag set to 0 (184, in kind); Task stale +58..59 ff/00 (84); Assn stale +58..61 ff/00 (80); CV_iew font word top byte ff/00 (32, pre-G0f, a named uninitialised byte) |
| fuzz, VM copies: TRACED = the VM's save (host copies in `build/trace-2021/saved/work`), reference from `plan.json` | 231 recordings: 21 with no committed reference (not testable), 102 FAIL the gate (BASEFAIL, not fuzzed), 108 PASS and fuzzed |
| fuzz, VM copies: variants that PASS | 20, all the font word top byte |
| targeted wrong files (in memory, `compare()`) | zoom 5 pairs x 16 edits; CEdl/CV_iew 3 pairs x 60; slot table 4 pairs x 6; notes 4 pairs x 12; Props14 3 pairs x 30; stale blocks 14 blocks x 15; baseline-slot words 8 pairs x 5 |
| evidence claims checked against the files | 14 masks (table in section 2) |
| mutations of my own, in the G0f code | 41 (29 + 12): 36 red, 5 GREEN (2 equivalent, 1 shape no file holds, 2 real test gaps), 0 NOFIT, 0 crashed |
| 0f.6 | 22 field cases, 13 targets; 22 of 22 FAIL on the VM copy in the named stream |

## 1. Wrong files that PASS

### R36G0f-1 (Critical): a wrong Gantt zoom unit passes; the unit word is masked for any value from 1 to the width

The gate masks the u16 at +1180 of the 41363-byte view-state record whenever the u16 at +1172 is a width of 128 to 4096 and the unit is 1 to that width (`_canon_cview_var2`, `CVIEW_UNIT_AT`). The width need not differ from the reference's. The unit is the zoom's span (reference 178, 185, 249, 100 and 152 in the five committed zoom references; the gate's own note says it "follows from the width and from the zoom's span"). The brief's own test for 0f.4b is "a changed zoom unit word in the same block FAILs"; the test pins the opposite (`W36-0f.4b ... the VM's width alone, the reference's unit (up to the width) is noise`, test line 3513).

Wrong file: the committed zoom reference with only the +1180 word rewritten (width 599 or 511 unchanged), same size, run on the CLI:
```
$ python build/rev/cli_findings.py        (builds build/rev/files/*, runs the gate)
$ python tools/trace/trace_gate.py build/rev/files/wave35-viewstate-zoom-custom-unit-wrong.mpp tests/data/wave35-viewstate-zoom-custom.mpp tests/data/wave35-viewstate-zoom-custom-control.mpp tests/data/wave35-viewstate-zoom-custom-control-null.mpp
96 streams, 96 checked, 0 not checked, 0 problems
exit 0
(the same, exit 0, 0 problems, for wave35-viewstate-zoom-entire and wave35-viewstate-zoom-reset)
```
In memory, `python build/rev/probe_zoom.py` (log `probe_zoom.log`): for all 5 zoom pairs (custom, custom-10d, custom-narrow, entire, reset), with the zoomed record's width unchanged and the unit set to ref+1, 1, or the width: PASS 5 of 5 each. With width 351 and unit 1, 351, 2x ref: PASS. A width of 127, 4097, 0 FAILs; the bytes at +1170 and +1174 FAIL.

On the VM's own copies: `python build/rev/vmzoom.py`:
```
wave35-viewstate-zoom-custom VM copy verdict: PASS [] | unit rewritten: [(1, 'PASS'), (2, 'PASS'), (50, 'PASS'), (300, 'PASS')]
(the same line for -custom-10d, -custom-narrow, -entire, -reset)
```
5 of 5 VM copies PASS, and still PASS with the unit rewritten to 1, 2, 50 or 300 (capped at the width).

The date words of the same record are masked as any date in the span (0f.4.2), so the zoom window moves freely too: `python build/rev/cli_findings.py`, case "zoom-custom: the zoom window's date words +1176/+1182 moved by 30 days": exit 0, 0 problems. Together a wrong zoom (unit and window) passes whatever its value; only the width of the zoomed record and the other bytes of the block are compared.
Fix: compare the unit by its ratio to the width (`u_t * w_r` against `u_r * w_t`, within a rounding of 1), or mask the unit only when the width differs and the ratio holds; leave the date words compared when the unit is data. The brief's "a changed zoom unit word FAILs" is the test to add.

### R36G0f-2 (High): the creation stamp of every row, assignment and resource is masked with no control/null evidence; a changed stamp of an existing row passes

0f.4.7 masks Task FixedData +128 of every row, the Assn Var2Data block 0x0f40027a and the Rsc block 0x0c4002d6 whenever both sides hold a stamp inside STAMP_SPAN. G0f's own evidence rule (brief: for a session field the control and control-null differ there; for a machine field the VM's value is the VM's) is not met: the spec note admits "a host control and its null carry one stamp in a row (stable between two saves), so the evidence is the VM's", and "the DOSSIER cites no host control/null difference" for the resource stamp. I counted:
```
$ python build/rev/ev_stamps.py
pairs 191 task rows shared by control and null 788 differing in +128 0
shared var blocks {'???114/TBkndAssn/Var2Data': 556, '???114/TBkndRsc/Var2Data': 338} differing {}
```
0 of 788, 0 of 556, 0 of 338. A stamp is also not a constant of the VM, so neither half of the rule holds. Before 0f.4.7 only a row the control lacks was masked.
Wrong file: the committed `wave30-calculate`, the Task row of uid 0 (present in the control) with its +128 stamp rewritten to another stamp inside the span:
```
$ python build/rev/cli_stamps.py
$ python tools/trace/trace_gate.py build/rev/files/wave30-calculate-rowstamp.mpp tests/data/wave30-calculate.mpp tests/data/wave30-calculate-control.mpp tests/data/wave30-calculate-control-null.mpp
96 streams, 96 checked, 0 not checked, 0 problems
exit 0
```
Why High and not Critical: the brief's item 0f.4 names "the creation stamps" for masking, so the builder followed the item; the evidence rule it also states is what fails. The Created date is a user-visible Task field, so a writer that rewrites the stamps of existing rows passes. Out-of-span stamps still FAIL (the fuzz found none). Fix: mask only rows and blocks whose uid the control lacks (the G0e rule), or with an equal-uid row in the control keep the stamp compared; add the VM evidence as an OPEN pair if those rows must differ.

### R36G0f-3 (Critical by the scale, low practical weight): the CEdl rows record is dropped whole, so its kind word is unchecked

`_canon_cedl_var2`, the `CEDL_ROWS_ID` branch, does `noise.update(range(pos, end)); continue`: the record's length, id and kind words and its pad are all gone from the canonical form, not only the payload. `_rows_ok` checks the payload only. A kind word changed from 1 to 0x99 passes on a committed pair:
```
$ python build/rev/cli_findings.py     (case R36G0f-3)
$ python tools/trace/trace_gate.py build/rev/files/wave28-mark-auto-rowskind.mpp tests/data/wave28-mark-auto.mpp tests/data/wave28-mark-auto-control.mpp tests/data/wave28-mark-auto-control-null.mpp
96 streams, 96 checked, 0 not checked, 0 problems
exit 0
```
(`probe_misc.py`: PASS on `wave28-mark-auto`; the same edit FAILs on `wave30-calculate`, whose rows record is not in the `_rows_ok` form.) By the scale (a wrong file passes on a committed pair) this is Critical; no writer group emits a different kind for this record, so I would not hold the writer groups for it. Fix: keep the header in the canonical form (`out += pack("<III", 0, rid, kind)` and a derived name) as the slot table does.

## 2. Evidence of each mask against the files

Commands: `python build/rev/evidence.py` (log `evidence.log`, `evidence_pairs.log`), `python build/rev/evidence_vm.py` (counts and spans only; `evidence_vm.log`), `ev_stamps.py`, `ev_stale.py`, `vmstamp2.py`, `vmsi.py`.

| mask | claim | result |
|---|---|---|
| 0f.1 Props14 saved flag | control/null differ in 9 of 223 pairs; values 0/1 | 9 of 191 pairs differ (5 more where the record is absent on one side); 411 x 0, 210 x 1, 3 files without it. Holds |
| 0f.1 Props14 name and length | VM's name is 10 bytes, the host's 30 | VM copies: 195 of 231 have 10 bytes, 36 have 30 (the same-name recordings); committed: 30 (1023 entries), 16 (4). Holds |
| 0f.2 CEdl slot capacity | 64 in 280 files, 128 in 97, 72 in the VM, 36 to 144 | committed 624: 64 x 485, 128 x 136, 144, 72, 36 (the counts differ from the note, the range holds); VM 64 x 152, 72 x 77, 68, 32; control/null differ in 6 of 191 pairs (note: 6 of 223). Holds as a range |
| 0f.3 note lang/build | VM 2057, 20430 | VM copies: lang 2057 x 28 and 1033 x 8; build 20430 x 29 and 20527 x 7. The VM does write other values (carried notes); the mask accepts any 1 to 5 digits (R36G0f-7) |
| 0f.4.1 CEdl dates and view word | control/null differ in 7 of 223; spans 220968000..226137600 and 24183812..53413760 | 7 of 191 pairs differ at each of the 4 words. Dates: committed min 220968000 (+2a), max 226137600 (+2b): holds. View word upper: committed 25035936..53413760, the lower bound 24183812 is the VM's (min of the 231 VM copies), not "the 624 committed values" (the note's wording) |
| 0f.4.2 CV_iew stamp and dates | 21 and 17 to 21 of 202 pairs; span | 20 pairs (stamp), 20 / 5 / 15 pairs at +1176 / +1182 / +1186 of 191; committed stamps 0x3d1412c0..0x3d2812c0 (627, word 0 always 0). Holds. The VM's stamps are not all in the span (R36G0f-4) |
| 0f.4.3 STAMP_SPAN top 0x3d28352a | the VM's three latest baseline-slot words | the three words exist (0x3d283502/16/2a); but 33 of the 231 VM copies hold a save word above the new top (R36G0f-4) |
| 0f.4.4 baseline-slot words | evidence is the VM's | no committed pair differs at a slot (0 of 191); 19 set slots in 624 files (16 of 0x02401395, one each of the other three), each equal to both save words; the mask is the narrowest possible (equal to both save words, both sides): 8 pairs x 5 wrong edits all FAIL (`probe_slotwords.py`) |
| 0f.4.4 third Props GUID | 1 to 2 of 223 pairs | 2 of 191 pairs; 605 of 624 zeros, 19 version-1. Holds |
| 0f.4.5 per-row view GUIDs | version-1 GUIDs, per save | inherits row 1's evidence; fuzz: 0xff/0x00 and flipped bytes next to a GUID FAIL |
| 0f.4.6 Assn Fixed2Data third GUID | 223 of 223 pairs | not recounted; the mask is a version-1 GUID on both sides (fuzz clean) |
| 0f.4.7 creation stamps | the VM's | no control/null evidence: R36G0f-2 |
| 0f.4.8 stale memory | +18/+38 one value; VM base differs | 14 shared control/null blocks, 0 differ at +18, +38 or +58; one +18 word in the 14 committed controls (0x7ff6d18a). Thin (14 blocks) but consistent. The +58 bytes carry no value check: R36G0f-5 |
| 0f.4b width/unit | 599 and 511 in the host zoom references, VM 351 and 355 | committed w1172: 100 x 1869, 599 x 4, 511 x 1, 0 x 1; VM 100 x 690, 355 x 3, 351 x 2, 0 x 1. The width holds; the unit is data: R36G0f-1 |

## 3. 0f.6: the 13 data targets

`python build/rev/check06.py` (log `check06.log`) executes the test's `DATA_TARGETS` (13 targets, 22 cases) against the VM's own copy of each target:
```
cases 22 with a VM copy 22 gate FAIL with a line in the stream: 22
```
For 21 of 22 the VM's bytes at the offset equal the recorded value; the 22nd (`wave31-usage`, CEdl slot list) is a stream of another size on the VM side, so the offset is not comparable. For all 22 the reference holds the `was` bytes. The test's own 22 cases are in the 1739 green checks. 0f.6 holds.

## 4. My mutations (41; 36 red, 5 GREEN)

`python build/rev/mut.py . build/rev/muts_g0f.py 5` (log `mut_g0f.log`, 29) and `... muts_g0f2.py 6` (log `mut_g0f2.log`, 12). Call-site mutations: the `other` side dropped from the slot, stale, guid_rows, varkey and row_stamp calls; `kind` and the Gantt length dropped from the view-state test; `_rows_ok` given the record's own n. Helpers: `_slot_uids` fill loop and bounds, `_stale_ok` (length, half compare, base list), `_saved_flag_ok`, `_tail_entries` (id, size, count), `_note_fields` (digit class, bound, whole block, build regex), the canonical record counts, the three date spans, the width and unit bounds. 36 red.

The 5 GREEN:
```
stale-half-compare           GREEN   block[18:22] == block[38:42] -> block[18:20] == block[38:40]
view-width-neighbour         GREEN   unit zeroing out[+1180:+1182] -> out[+1180:+1184]
slot-capacity-lowerbound     GREEN   _slot_uids: len(payload) < 4 -> None
cedl-count-kept              GREEN   out[16:20] = kept -> kept + 1     (equivalent: both sides move together)
p14-flag-pad-wide            GREEN   out[-2:] = bytes(2) after the placeholder  (equivalent: already zero)
```

### R36G0f-6 (Medium): two real survivors, the stale word's high half and the unit's neighbour

- `stale-half-compare`: with only the low halves of +18 and +38 compared, a block whose +40..41 differs while +20..21 is a valid image-base half still passes, because the mask covers +38..41. The test has no case with +38's high half changed alone (`+18 changed alone` and `+38 changed alone` change a low byte).
- `view-width-neighbour`: zeroing 4 bytes at +1180 instead of 2 also hides +1182..+1183 (the low half of the second date word) whenever the width is a masked width and that date word is not itself masked (unset 14400). A wrong file with the low half of an unset +1182 date changed beside a zoom passes under the mutant; no test touches +1182..1183 beside a masked width (the test's neighbours are 1162 to 1174, 1178, 1190, 1192).
Fix: add the two cases (+40 alone; +1182 beside a masked width, with the date unset on both sides).
- `slot-capacity-lowerbound` (Low, no committed file has the shape): a zero-length CEdl slot table is not tested; the two sides would differ only in `derived`. `cedl-count-kept` and `p14-flag-pad-wide` are equivalent.

## 5. Other findings

### R36G0f-4 (Medium): the VM's newer saves lie above three spans; 108 of 210 PASS, not the predicted 210

`python build/rev/vmtally.py` (log `vmtally.log`): of the 231 VM copies, 210 have all three of reference, control and null committed. Gate verdict: **PASS 108, FAIL 102**. The FAIL lines: `?SummaryInformation` 94 (derived 94, bytes-differ 94), `???114/Props` 15, CTable/CV_iew/Task Var2Data 4 + 3 + 3, `TBkndRsc/FixedMeta` 3, `TBkndTask/FixedMeta` 2, CEdl 4, others 1 each.
Cause of the 94, `python build/rev/vmsi.py`: the VM's LastSave FILETIMEs run 0x01dd715a80083600 to 0x01dd74433b72e800; the gate's `LASTSAVE_SPAN` top is 0x01dd72130aac6c00 (11 November 2026): 115 of 231 VM saves are above it. G0f widened `STAMP_SPAN` for the VM but not `LASTSAVE_SPAN`. `python build/rev/vmstamp2.py`: 33 of 231 VM copies hold a Props save word above the new `STAMP_SPAN` top, and 21 a CV_iew stamp above `CVIEW_STAMP_SPAN`'s top (0x3d2b12c0 against 0x3d282db4; these 21 are recordings with no committed reference). The spec's statements "the VM's lie within" (CV_iew stamp row) and "the span's top is the largest of them" (stamp row) do not hold for the newer saves. This is a false FAIL, not a wrong file passing, but the group's goal (the brief's PASS 210) is not met and the recorded claims are wrong. Fix: widen by the VM evidence of the newer saves (as W36-0f.4.3 did) or state the cut-off date.

### R36G0f-5 (Medium): the stale-memory heap bytes are masked with no value check

The +58..59 bytes of a Task block and +58..61 of an assignment block (heap-pointer low bytes) are masked on the strength of the block shape alone; the stale words get a high-half check but the heap bytes get none, and the resource block (`(False, 0, 0)`) gets neither a high-half check nor heap bytes. A rule of G0f is "a mask checks the value it masks".
```
$ python build/rev/probe_stale.py     (log probe_stale.log)
wave35-g3-baseline-set-nested  heap bytes +58..60 = ff (masked, no value check)   PASS
wave35-g3-baseline-set-nested  heap bytes +58..60 = 00                            PASS
```
`fuzz.py`: 84 Task and 80 Assn all-0xff / all-0x00 variants of the heap bytes PASS on committed pairs. The neighbours (+56, +57, +60, +17, +22, +37, +42) and `+18` or `+38` alone FAIL. The evidence for these bytes is thin (14 control/null blocks, 0 differing). Fix: bound the heap bytes (the committed and VM values are heap addresses: a user-mode range) or leave them compared and record those pairs OPEN.

### R36G0f-7 (Low): the note machine fields are checked as a format, not as the VM's value

`\deflang` 1 to 65535 and the Riched20 build of 1 to 5 digits pass, e.g. build 99999 and language 9999 (`python build/rev/probe_notes.py`, log `probe_notes.log`: lang 9999 PASS, build 99999 PASS; leading zeros, a non-digit, 6 digits, a changed note letter and a changed header byte FAIL, 4 pairs x 12). The brief asks for the VM's value (2057, 16.0.20430); the committed hosts write 1033 and 20527 too. Accepting the three known builds and languages is the narrow form.

### R36G0f-8 (Low): hygiene

`git log --format='%an|%ae|%B' f1b86da..c7ea56f` and `git diff f1b86da c7ea56f` searched for personal names, `Users`, drive paths, e-mail addresses, company and client words: no user folder, no client name, no personal name in a message or the diff. The 17 commits carry the author's configured name and a GitHub no-reply address as metadata, and the trailer `Co-Authored-By: Claude Sonnet 5.5`. One item: the spec note `trace-gate-fields-g0b.md` prints the saving host's MAC address (the version-1 GUID node `[host node]`, twice: the Assn third GUID row and the Props third GUID row) next to the VM's `[VM node]`. It is a device identifier of the owner's machine in a tracked file (it is also in every committed `.mpp` GUID). I could not run `scan_client_names.py`: it is not a tracked file in this worktree.

### R36G0f-9 (Low): the view word's span note

The note says the view word's upper u32 span "24183812 to 53413760" is that "of the 624 committed values and the VM's". The committed minimum is 25035936; the lower bound is the VM's. Harmless to the verdict, wrong as evidence text.

## What I did not check

- The full meta-test (over 2 hours; the brief's rule). I did not check the builder's meta-test numbers (299 derived candidates, 184 named guards), and my 41 mutations are not a measure of its coverage.
- The Props14 name content: the name is a machine field and any text passes; I did not decide whether a different name should be compared.
- 0f.4.6 (Assn third GUID) and 0f.4.5 evidence counts (223 of 223): not recounted; only fuzz-checked at the mask.
- The 21 VM copies with no committed reference, and the 102 VM copies that FAIL, were not mutated (nothing to mutate from a PASS); I did not classify each of the 102 failures beyond the stream and line tallies.
- Wrong files across two streams at once, and traced files whose stream sizes differ from the reference's (the fuzz keeps sizes); the CEdl slot table was tested with consistent size words only.
- The DOSSIER and `r11/<target>.txt` themselves (private): I checked claims against the committed files and the VM copies, not against the DOSSIER's own tables.
- The hand-built wrong files of review-g0b/g0c/g0d: I relied on the test running them (they are in the 1739 checks).
- The harness `trace_gate.py` as run inside the VM, and any COM, VM or Microsoft Project step: none was touched.
