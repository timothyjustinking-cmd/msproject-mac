# Review: wave 36 G0c, `5396f56..02c0d89` (branch `wave36`), run at the tip `02c0d89`

Independent, read-only, adversarial review, 14 November 2026.  Nine commits, `4352eb7` to `02c0d89` (eight
items and the group record).  Read with `git show`, `git diff` and `git log` only; nothing checked out, built,
staged or changed in the tree; no compiler, cmake or ctest was run; the VM, `build/trace-2021/` and
`build/r7/` were not touched.  `tools/trace/trace_gate.py`, `tools/trace/test_trace_gate.py`,
`tools/trace/kvp/host.ps1`, `tools/trace/test_host_skip.ps1` and `tools/trace/capture/runone6.ps1` were
exported from `02c0d89` into `build/verify-w36/g0c-review/` (git-ignored: the reviewer's temporary folder),
and run and mutated there with Python against the committed fixtures in `tests/data` (read only; the copy of
the test has its `DATA` line pointed at them).  `test_host_skip.ps1` was read, not run (no PowerShell test
run: Python only).

**Count: 2 Critical, 3 High, 2 Medium, 4 Low.**

Reproduced runs, one each, on `02c0d89`:

| run | denominator | result |
|---|---|---|
| the committed test, copy, `python test_trace_gate.py` | 335 checks | **335 PASS, 0 FAIL, "all passed", exit 0** (38 s) |
| positive, command line `trace_gate.py NULL CONTROL CONTROL NULL`, every `*-control-null.mpp` | 202 pairs (the 8 odd-named controls from the test's table, which matches the capture specs' `Ctl` and `scripts/oracle_r7_nulls.py` `NULLS`) | **146 exit 0, 56 exit 1, 0 other**; the exit-1 set equals the in-process OPEN set (symmetric difference empty) |
| negative, command line `trace_gate.py NULL EDIT CONTROL NULL`, every edit that differs from its null outside `SummaryInformation` | 191 edits | **191 exit 1, 0 exit 0, 0 other** |
| negative, vacuity check (my own): edits of the 56 OPEN pairs | 46 edits | 44 FAIL in a stream the null itself does not fail in; **2 fail only in streams the null already fails in** (the two known no-op percent edits); 145 edits belong to a pair that passes the positive sweep |

## What holds

- **Hand-built wrong files of review-g0b.md: all FAIL** (see "Results" below for the denominators).  The
  inverted-control-mask files, the Props14 junk and second 0xffffffff entry, the junk after the
  SummaryInformation strings, every zeroed GUID, and the twelve rows of R36G0b-6.
- **The control mask is gone from the verdict.**  `compare()` counts a problem for every byte outside the named
  windows, whether or not the control and null differ there; the mask only decides the label
  (`control-mask` or `bytes-differ`).  Planted back (`masked = []`): 11 FAIL.  Not counted as a problem: 11
  FAIL.
- **Exit paths.**  Exit 0 only on zero problems; `nothing checked` is a problem (planted: red); the control as
  its own null is "no null" (planted: red); an uncaught exception in the gate is a Python traceback, exit 1,
  never 0.  The gate prints no OPEN: an OPEN pair is exit 1 on the command line, and `runone6.ps1` records
  `$LASTEXITCODE` as `gate exit N` ("0 is PASS, anything else is not").  OPEN exists only in the test, which
  prints it and exits 0 by design (the test asserts that the pair still fails).
- **Parse failures fall to raw, never to equal.**  A parse that fails leaves the stream raw, so a noise
  difference there is a size or byte difference.  Fuzz result is in "Hang and crash" below.
- **Counts in the records are true.**  335, 202 / 146 / 56, 191 / 191 reproduced; nine commits, nine
  `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)` trailers (`git log --format=%B | grep -c`: 1
  per commit), one item id in each subject (W36-0c.1 to 0c.8 and W36-G0c); bodies cross-refer to other ids
  but name one item.
- **The binary GUID classes mask a field only when both sides are version-1 GUIDs**: 108 GUID fields zeroed
  across three pairs, in the traced and in the reference, 145 of 145 files FAIL (below).
- **The review's R36G0b-6 mutants are now red**, all twelve (table in "Mutations").

## Results: hand-built wrong files

Scripts and outputs are in the reviewer's temporary folder.  Each file was run through the gate at `02c0d89`;
the unaltered file was run first and passes (the control for every case).

| case (review-g0b.md) | built how | denominator | result |
|---|---|---|---|
| R36G0b-1, the seven named pairs, inverted control/null bytes (`wave30-baseline`, `wave30-wbs`, `wave29-percent-leaf`, `wave28-outline-summary`, `wave30-resource-rename`, `wave36-r7-table1-column-replace`, `wave28-mark-auto`) | the edit with every byte where its control and null differ inverted, by olefile in a copy, command line, REFERENCE = the edit | 7 files (one stream each; three for `wave28-outline-summary`) | **7 of 7 exit 1**, each `1 problems` (3 for outline-summary): `control-mask <stream> <n>`; the untouched edit exits 0 |
| same, REFERENCE = the null (positive form) | 7 | **7 of 7 exit 1** |
| R36G0b-1, every pair | for each of the 202 control/null pairs, the null with every byte inverted where its control and null differ, equal-length streams, REFERENCE = the null, `compare()` | 202 pairs (all have at least one differing byte) | **202 FAIL, 0 PASS** |
| R36G0b-2, Props14 tail | on `wave30-calculate` and `wave34-assign-add-second`: 0xffffffff entry of 400 non-text bytes, of 400 control characters, of 30 zero bytes; a second 0xffffffff entry of 400 junk bytes, count raised; the same with a text name; a second 0x3540000b entry; the 3540000b entry removed; entries swapped; only the 0xffffffff entry; 4 bytes appended; and, by command line, same-size 30-byte non-text junk in either entry | 10 wrong files per pair = 20 by `compare()`, plus 4 by command line | **24 of 24 FAIL**.  (Two of my first "junk" payloads were valid UTF-16 text and are the named field, so they pass as designed; they are not counted.)  The named cases pass: the 0xffffffff entry absent, a 900-character text name, an empty name: 6 of 6 expected passes |
| R36G0b-3, SummaryInformation | 240 junk bytes after Author, RevNumber, LastAuthor with offsets and section size rebuilt; 4 junk bytes; nonzero padding; a lying length word; a 16-byte LastSave slot; Title, CreateTime, pid 18, 19, 1 changed; and by command line, the Author length word one word too long, on both pairs | 12 cases x 2 pairs = 24, plus 2 by command line | **26 of 26 FAIL**; LastSave changed, another Author string, an empty or 0xff string, swapped Author and LastAuthor (the named fields): 10 of 10 expected passes |
| R36G0b-4, GUIDs | every GUID field zero in the traced, every one zero in the reference, the first one zero, all version 4, the variant bits of the first wrong, in every GUID-bearing stream (Assn, Task, Rsc, Cal, Cons Fixed2Data; CEdl, CFilter, CGrouping, CTable, CV_iew FixedData; the two save GUIDs of `114/Props`) | 29 stream instances in three pairs (108 GUID fields: 25 + 45 + 38) x 5 variants = 145 files | **145 of 145 FAIL**; the control (every GUID another version-1 GUID) passes in 29 of 29 |
| R36G0b-6, the eleven mutation cases (twelve table rows; rows 3 and 4 share a file) | Title changed; CreateTime changed; a CEdl path record of C0 control characters; CEdl size +4 off by one; 0x00010007 = length + 1; a Props14 path record and a tail name of control characters; the 3540000b entry removed; a version-4 GUID in `114/Props`; a view string with 76 junk bytes on one side; a non-font record of 44-multiple length with its +43 flipped | 11 files, each on two or three pairs (the font case on one) | **11 of 11 FAIL** on the real gate; the font case (`+43` of the real font record passes, `+42` fails) behaves |

Not FAIL, found by new probes (R36G0c-1 to -3, -6, -8 below): derived size words set to 0, an emptied path
record, a view GUID string of zeros or with another version digit, a C1-control path, a creation stamp of 0.

## Status of R36G0b-1 to -12

| id | status | evidence |
|---|---|---|
| R36G0b-1 | **fixed** | the control mask excuses nothing (the 7 and the 202 above; mutant `masked = []` red).  The cost is the 56 OPEN pairs.  The OPEN pin is coarse (R36G0c-7) |
| R36G0b-2 | **fixed** | junk and a second 0xffffffff entry FAIL (24 of 24).  Three of the new `named_absent` conditions are redundant (R36G0c-9) and two older guards of the same function are untested (R36G0c-5) |
| R36G0b-3 | **fixed** | 26 of 26.  The new `_lpstr_slot_ok` type test is not held by the test (R36G0c-4) |
| R36G0b-4 | **partly fixed** | the binary GUIDs: fixed.  The view GUID string is still shape-only: zeros and a changed version digit pass (R36G0c-3) |
| R36G0b-5 | **fixed** | 335 PASS reproduced; the control comes from one table (matches the capture specs); a null whose control is missing is a FAIL line |
| R36G0b-6 | **fixed for the twelve rows**, but the mutations of this review leave four guards green (R36G0c-4, -5) | 34 red, 8 green of 42 mutants |
| R36G0b-7 | **fixed as the brief asks** (listed as OPEN, not decoded) | 56 OPEN with a stream and reason each; coarse pin (R36G0c-7) |
| R36G0b-8 | **open, as the brief says** | recorded in `gates-g0c.md` and `wave36-complete.md`; needs a real recording.  My projection: 146 of 202 committed pairs pass with nothing excused, so any edit that moves the OPEN streams' bytes will FAIL a faithful trace until they are decoded |
| R36G0b-9 | **partly fixed, UNVERIFIED on the VM** | read the diff: `ERR ` is thrown as the agent's error before the length test; base64 has no space, so the prefix cannot be a reply.  `test_host_skip.ps1` was not run here.  The no-wrap claim is the builder's measurement on the host in two PowerShell editions; the VM has not confirmed it (the record says so) |
| R36G0b-10 | **fixed** | `not checked` counts a stream in one file only or of another size (3 of 3 cases red when the counts are removed; no-null counts every stream) |
| R36G0b-11 | **recorded, history not rewritten** | nine of nine G0c commits carry the trailer; the record states 0 of 11 for G0b |
| R36G0b-12 | **partly fixed** | the counts are correct and the old sentences are corrected; new overstatements (R36G0c-11) |

## Findings

### R36G0c-1 (Critical): a derived size word passes when it is 0 (the placeholder collides with the value)

- **Where:** `_canon_cedl_var2` (`tools/trace/trace_gate.py`, the loop over `(0, 4, 8)`), `_canon_cedl_meta`
  (+20) and `_canon_view_props` (record 0x00010007).  A size word that equals the stream-derived length is
  replaced by 0 in the canonical form; a word that is not derived stays raw.  The reference's canonical word
  is 0, so a traced word that is itself 0 compares equal.
- **Failure** (`compare()` on `wave30-calculate`; olefile cannot grow a stream, so no command-line file, but
  these are same-size edits and can be written in place): `214/CEdl/Var2Data` +0 = 0, +4 = 0, +8 = 0;
  `214/CEdl/VarMeta` +20 = 0; `214/Props` record 0x00010007 = 0.  **5 of 5 PASS**, `0 problems`; the same
  words set to the true length plus 1 all FAIL (5 of 5), which is the planted case the test has.
  The same collision lets an emptied CEdl path record (length 0, no payload, size words kept consistent)
  pass, on all three pairs tried.
- **Disclosure:** the builder lists this as "Found, not fixed" in `docs/handoff/wave36-complete.md`
  ("A zero placeholder collides ...").  It is a wrong file built from a committed pair that the gate passes,
  and the brief's rule ("a masked field is accepted only when both sides hold a well-formed value") is
  broken for these five fields, so it is Critical on this review's scale.
- **Fix:** do not use 0 as the placeholder.  Keep a separate boolean per derived field ("equals the derived
  value") in the canonical form, or replace with a sentinel that a raw word cannot take (and put a
  `derived <stream> +n` problem line when only one side is derived).  Tests: each of the five set to 0 FAILs
  on a pair whose true value is not 0; an emptied path record FAILs.

### R36G0c-2 (Critical, reachable only with a hand-patched directory): two stream names that differ only in non-printable characters collapse to one, and the later wins

- **Where:** `streams()` (`trace_gate.py:57-63`).  The key is the stream path with every character outside
  `" " < c < "\x7f"` replaced by `?`.  There is no collision check, so the last entry in directory order
  overwrites the earlier one.
- **Failure:** I took `wave30-calculate`, added a free directory entry named `\x06SummaryInformation` that
  carries the original SummaryInformation sectors, and pointed the real `\x05SummaryInformation` entry at
  the first 256 bytes of the document-summary stream's sectors.  olefile lists 97 streams; the gate sees
  96, `?SummaryInformation` holds the extra entry's (original) bytes, and the real stream, which is wrong, is
  never compared.  `python trace_gate.py TRACED REFERENCE CONTROL NULL` prints
  `96 streams, 96 checked, 0 not checked, 0 problems`, **exit 0**.  A stream present on one side only is the
  case the brief names; this is one the count does not show.
- **Reach:** a real Project save cannot do this; it needs a directory edited by hand.  Critical by this
  review's scale (a wrong file, built from a committed pair, passes the command line); the operator may weigh
  the reach.
- **Fix:** key streams by their full raw path (for example `repr` of the parts, or the UTF-16 name) and
  print the lossy form for display only, or count the entries before and after and add a
  `duplicate-name <key>` problem.  Test: the file above (or two entries given the same key by a stub of
  `olefile`) FAILs.

### R36G0c-3 (High): the view GUID string is accepted on its shape alone, so zeros and a changed version digit pass

- **Where:** `_guidstr_digits()` and the `guidstr` class in `noise_windows` (`trace_gate.py:469-480`, `541-544`).
  The 32 hex digits are masked when both sides match the `{8-4-4-4-12}` pattern.  The binary GUIDs are held
  to version 1 (W36-0c.2); the same field class as a string is not.
- **Failure** (`compare()`, `wave30-calculate`, `wave34-assign-add-second`,
  `wave35-g3-insert-milestone-first`): the `0x03400001` string replaced by
  `{00000000-0000-0000-0000-000000000000}`: **3 of 3 PASS**; the version digit (index 15) changed to `4`:
  **3 of 3 PASS**.  A non-hex digit and a string that is not a GUID FAIL (the test's cases).
- **Evidence:** in all 624 committed files that have a view `Props`, the string is
  `{xxxxxxxx-xxxx-F111-9F..-<the host's node>}`-shaped: 19 of the 32 masked digits (positions 15 to 18, 20 to 22,
  25 to 36) never change in 624 files, and no string is all zero.  The node digits may move on another
  machine; the version and variant digits cannot.
- **Fix:** parse the string to 16 bytes in the same byte order as the binary view GUIDs (or compare the
  version and variant digits), and require a version-1 GUID on both sides, as for the binary fields.  Test:
  a zero string and a changed version digit FAIL.

### R36G0c-4 (High): the new `_lpstr_slot_ok` type test can be removed and the test stays green

- **Where:** `_lpstr_slot_ok()`, `trace_gate.py:338-345`, `if len(raw) < 8 or _u32(raw, 0) != 0x1E`.
- **Failure:** mutant `if len(raw) < 8:` in a copy: the committed test, **335 PASS, 0 FAIL, exit 0**.
  Wrong file: the Author slot of `wave30-calculate` with its type word 0x1F (or 0) and the same size.  Real
  gate: FAIL (`size-differs ?SummaryInformation 256->272`, the slot stays raw); mutant: **PASS**.  Same on
  the second pair.
- **Fix:** a case with the Author, LastAuthor and RevNumber type words changed (each to 0x1F and to 0) in
  `test_trace_gate.py`; assert the FAIL.

### R36G0c-5 (High): two guards of `_canon_props14` and the text cap are not held by the test

These pre-date G0c (G0b code), found by this review's extra mutations, and bear on W36-0c.4's Props14 shape.

- **`marks[0] != 4`** in `_tail_entries` (`trace_gate.py:281`, `(marks and marks[0] != 4) or `): removed,
  the test is green (**335 PASS**).  Wrong file: eight junk bytes between the tail count word and the first
  entry.  Real: FAIL (`size-differs Props14 416->564`); mutant: **PASS**, because the entries are re-emitted
  and the bytes before the first mark are dropped.
- **`_u32(data, 4) != end - 4`** in `_canon_props14` (`:295`): removed, green.  Wrong file: the second blob
  size word +8 off the first.  Real FAIL; mutant **PASS** (both words are zeroed).
- **`len(payload) > 2048`** in `_text_ok` (`:196`): removed, green.  A 2,100-byte path text: real FAIL,
  mutant PASS.  (Low weight; the cap is arbitrary, but it is the only bound on a text field.)
- **Fix:** add the three wrong files as FAIL cases; run the removals in the test's `--gate-dir` form.

### R36G0c-6 (Medium): masked noise fields are accepted whatever their value

The brief's rule for G0c is that a masked field has a well-formed value of its kind.  These classes check
the length and nothing else: the creation stamp of a new Task row (record +128..131) and of a new Assn var
block (key 0x0F40027A), the `114/Props` save words (0x02400007, 0x024013B1), the font word's top byte, the
SummaryInformation LastSave FILETIME and the Author, LastAuthor and RevNumber strings.

- **Failure** (`compare()`): `wave35-g3-insert-milestone-first`, the new row's stamp `d00c263d` replaced by
  `00000000` and by `ffffffff`: **2 of 2 PASS**; the two Props words each set to 0 and to 0xffffffff: 4 of 4
  PASS; LastSave 0 and all-ones: 2 of 2 PASS; an Author string of 33 bytes of 0xff in a valid slot: PASS.
  A save that never wrote the stamp is accepted.
- **Judgement:** a stamp or a counter has no decodable "well-formed" test beyond a plausibility range, so
  this is Medium, not High.  It is the same class as R36G0b-4, which was fixed for GUIDs.
- **Fix:** for the two stamps, accept only a non-zero value that is not all-ones (and, from the committed
  files, within the range they span); for LastSave, a FILETIME after 2000; for the strings, a code page
  string without control bytes.  Record each bound with its evidence row.

### R36G0c-7 (Medium): the OPEN list is pinned by pair and stream, not by byte or field; the reasons contradict the evidence on two rows

- **Where:** `OPEN`, `MASKED_IN`, `MASKED` in `test_trace_gate.py` (about lines 395-540); the checks "an OPEN
  pair fails only in the streams its entry names" and "the control mask minus the named fields is exactly the
  MASKED_IN streams".
- **Failure:** mutant `control-mask <stream> <n+1>` (the printed byte count): 8 FAIL, seven of them the
  seven inverted-file cases and one the identity case; the other 49 OPEN pairs do not notice that the count
  changed.  So a new undecoded byte in a stream that is already on a pair's list (for example 106 bytes in
  `214/CV_iew/Var2Data` of `wave31-tables`) joins the OPEN list silently.  The brief asked for "the stream,
  the record id and the reason"; the record id is absent for most streams.
- **Reasons:** `MASKED["114/TBkndAssn/Fixed2Data"]` says "+32..+37 and +41: data ... never masked", and
  `MASKED["114/TBkndTask/Fixed2Data"]` says the Calculate save stamp is "data".  The committed null saves
  differ from their controls at exactly those bytes with no edit (for example `wave30-level` rows 3 to 5,
  `46 6b 30 4e -> 59 2e 88 c1`; `wave30-baseline` rows 4 to 6 +42 `00 -> 20`, +46..48 `c0 12 f2 -> 78 1e f1`),
  so they are per-save values; calling them data is the reason the pair is OPEN rather than a reason to
  leave them unmasked.
- **Fix:** pin each OPEN entry as (pair, stream, count of bytes) and fail when a count grows; correct the two
  reasons ("a per-save value nobody decoded; masking it would also pass an edit of the real field").

### R36G0c-8 (Low): the text test takes any character at or above U+0020 except U+007F

`_text_ok()` (`trace_gate.py:194-204`): C1 controls (U+0080 to U+009F) and private-use characters are
"printable".  `compare()`: a CEdl path record of 20 U+0085 characters ending `!` and NUL, a `Props14` path
record and a tail name of U+0085: **3 of 3 PASS on each of three pairs** (9 passes); U+E000: PASS.  The test's
"control characters" cases use U+0001 only.  Fix: reject the C1 range, private use and unassigned code points
(`unicodedata.category` not in `C*`), and add the cases.

### R36G0c-9 (Low): three `named_absent` conditions and the SummaryInformation noise range are not isolated by any committed shape

- Mutants `named_absent` without the text test, without the id test, and with `len(entries) >= 1`: all
  green (335 PASS).  I tried five tail shapes (lone junk, lone 0xffffffff, two 0x3540000b, two 0xffffffff,
  reversed) against two references: the verdict is identical on the real gate and each mutant (10 of 10).
  The committed Props14 tails are three shapes only (217 lone 0x3540000b, 403 the pair, 4 not parsed), and
  the second layer (the comparison with the reference's canonical form) catches what the conditions miss.
  So they are redundant, not holes; the record's claim that each is a guard is not shown.
- Mutant "noise range to the end of the slot" in `_canon_summary`: green; the three audited pairs have an
  Author length that is a multiple of 4 (no padding), so the range is the same; it affects `with_noise`
  only, never the verdict.  Fix: a case with an Author string that needs padding.

### R36G0c-10 (Low): the control null no longer changes any verdict

- `compare()` uses the null only to label a failing byte `control-mask` or `bytes-differ` and to refuse a
  missing null.  A fake null (the control with one SummaryInformation byte changed) gives exit 0 on an
  identical pair by the command line; the verdict is the same for the real null, a fake null and another
  pair's null in 6 of 6 trial sets (edit and null as traced, three pairs; in one the number of problem lines
  differs, 14 against 13, both FAIL).  `runone6.ps1` and the
  "no null" guard therefore demand a file that proves nothing.  The brief's design ("the control and its null
  are then used only to tell a new row from an old one") is met by the control alone.
- Fix: either drop the null from the command line and the specs, or make "no null" mean "no measured noise"
  by checking the null differs from the control in the SummaryInformation and Props only (a real null).

### R36G0c-11 (Low): the records overstate again

- `docs/handoff/wave36-complete.md` (G0c) and `gates-g0c.md`: "a masked field is accepted only when both
  sides hold a well-formed value of the named kind" is not true of R36G0c-1, -3, -6 and -8; the gate
  docstring still says "The named fields are the whole of the waiver", which the placeholder collision
  (R36G0c-1) and the name collapse (R36G0c-2) contradict; "Hand-built wrong files ... all FAIL" holds for
  the review's cases only.
- The G0c record counts twelve planted removals where the brief says eleven (the twelfth is the view GUID
  string): harmless.  The builder's own "Found, not fixed" entry for the zero placeholder understates it:
  "the new cases catch it only where the true value is not 0" omits that the empty path record and
  `214/Props` 0x00010007 = 0 pass.
- Fix: restate the claim to what the tests hold; move R36G0c-1 into the acceptance as a failing case.

## Mutations

Each planted in its own copy of the gate, run against the committed test with `--gate-dir` (baseline 335
PASS, exit 0).  Each pattern was checked to occur the stated number of times.  **42 mutants: 34 red, 8
green.**  One mutant I wrote wrongly (a count change that left the loop in place) is excluded and redone.

| mutant | result |
|---|---|
| `_guid_ok` accepts zero (helper) | red, 33 |
| guid_record, absolute guid, Props guid: other side not required (3 call sites) | red, 19 / 23 / 5 |
| `_lpstr_slot_ok` exact length / zero padding removed (helper) | red, 6 / 3 |
| `_lpstr_slot_ok` **type test removed** (helper) | **green** (R36G0c-4) |
| slot call site back to the G0b condition | red, 12 |
| slot noise range to `hi` | green; verdict-neutral (R36G0c-9) |
| `named_absent`: header not written; count word = raw count | red, 2 / 2 |
| `named_absent` without text test; without id test; `>= 1` | green x 3; redundant (R36G0c-9) |
| 0xffffffff name dropped whatever its bytes; every tail name dropped; every tail entry dropped | red, 1 / 2 / 2 |
| `control-mask` bytes not a problem; control mask excuses again; `bytes-differ` not a problem | red, 11 / 11 / 96 |
| `not checked` never counted; no-null `not checked` 0 | red, 3 / 1 |
| R36G0b-6 rows 1 to 7, 10, 11, 12 (SI placeholders; any FILETIME as LastSave; `_text_ok` true; CEdl path waived; CEdl size words unconditional; 0x00010007 unconditional; Props14 path waived; Props GUID records unconditional; view string one side; font byte any record) | red, 6 / 4 / 5 / 2 / 3 / 1 / 1 / 5 / 2 / 2 |
| CEdl VarMeta +20 unconditional; `nothing checked` passes; control as own null accepted; guidstr shape test removed; new_row other side not required; varkey uid test removed | red, 1 / 1 / 1 / 2 / 3 / 4 |
| Props14 blob size words not cross-checked (`:295`) | **green** (R36G0c-5) |
| Props14 `marks[0] != 4` removed | **green** (R36G0c-5) |
| text cap 2048 removed | **green** (R36G0c-5) |
| printed control-mask count +1 | red, 8 (only the seven inverted cases and one identity case) (R36G0c-7) |

Call site and helper were both planted for the GUID guard (helper red; three call sites red) and for the
slot (helper red for length and padding; call site red; type test green).  For `_text_ok`, the helper was
planted (always true: red, 5); the call sites of the CEdl and Props14 path records were planted
(red, 2 and 1).  A removal at the call site of the font byte (`rid == FONT_ITEM_ID and kind == 1`) is red.

## OPEN: sample of 16 of the 56

For each I read the gate's command-line output (TRACED = null, REFERENCE = control) and decoded the first
differing bytes by stream, row and record.  No sampled OPEN is a mask bug (a byte under a named field that the
logic mishandles): `control-mask` lines are bytes outside every window by construction, and the test asserts
that no positive pair fails by `bytes-differ`.  All are undecoded fields honestly reported.  Five carry
count-like bytes that a mask would turn into a pass of a wrong row count.

| pair | failing streams | what the bytes are | verdict |
|---|---|---|---|
| `wave28-mark-auto` | view bag 6 bytes (`+85258 16>17`, `+87542..44`) | a 4-byte save word and kin of the bag | undecoded, honest |
| `wave29-percent-leaf` | Assn Fixed2Data row 6, `+32..37` (`23 4b b5 cf > f1 aa 72 09`) | a per-save value the record calls data | undecoded; masking it would pass an edit of the real field |
| `wave30-baseline` | Task Fixed2Data rows 4 to 6, +42 `00>20`, +46..48 | a flag and the Calculate stamp | undecoded, honest |
| `wave30-resource-rename` | Rsc FixedMeta +176, +213 (`1c>5c`) | a flag bit in a meta item, set in the null | undecoded, honest |
| `wave36-r7-table1-column-replace` | CTable FixedData +447 (`16>95`) | one byte | undecoded, honest |
| `wave28-outline-summary` | `114/Props` 0x01000004 `03>02`, 0x00800004 `01>00`; Assn FixedMeta +4 `06>04`, +8 `03>02`; Fixed2Meta +8 `03>02` | **counts that fall by one** | undecoded; a mask here would pass a wrong row count (do not mask) |
| `wave34-assign-add-second` | the same records `04>03`, `01>00`, FixedMeta +4 `06>04`; Assn Fixed2Data row 3 `+32..35` | counts and a per-save value | as above |
| `wave35-g3-assign-remove-port-saved` | as above, plus Rsc FixedMeta, `Props14` +394 `01>00`, CEdl size 486>422 | counts, a flag byte, an undecoded CEdl record | as above |
| `wave36-r7-w37-b-rebaseline-same` | `114/Props` 0x024013bd `cc86>cd05` | a time-like word | undecoded, honest |
| `wave27-unlink` | Assn Fixed2Data row 6, view bag, CEdl 536>600 (0x26400024 doubled) | a per-save value and an undecoded record | undecoded, honest |
| `wave30-inactivate` | CEdl 384>422 only | record 0x2640003d present in one save | undecoded, honest (the only failure) |
| `wave31-tables` | 8 size-differs (CFilter x4, Assn Var2Data and VarMeta, CEdl, Props14) and 106 view-bag bytes | the null was saved by a later Project build than its control | a null from another build is not a null; honest |
| `wave23-flags` | as `wave31-tables`, 15 problem lines | same | same |
| `wave28-percent-auto100` | view bag 6 bytes and `Props14` +394 `01>00` | a save word and a flag byte | undecoded, honest (the no-op edit) |
| `wave35-viewstate-zoom-custom-narrow` | CEdl Var2Data 7 bytes (`+52 f8>a0`, `+386..87`) | payload of record 0x26400024 | undecoded, honest |
| `wave30-level` | Assn Fixed2Data rows 3 to 5, the same 4 bytes `46 6b 30 4e > 59 2e 88 c1`; Rsc FixedMeta +176 | the same per-save value in every row | undecoded, honest |

The list is pinned so a **new pair** cannot join without an entry and a pair that starts passing must leave
(both checks red when planted: 11 FAIL).  It is not pinned by byte count or field (R36G0c-7).  Whether the
OPEN result can be read as a pass: the gate gives exit 1, `runone6.ps1` records the number, and the test
prints `OPEN` lines with the count in its own summary line and exits 0 only because it asserts the pair
fails; a person reading only the ctest result sees "Passed" with 56 of 202 pairs failing the positive sweep,
which the record states and the brief requires.

## Hang and crash

I read the new and changed code for an unbounded loop or an unguarded index: `_lpstr_slot_ok` and the Props14
named-entry code add none (each loop advances at least 2 bytes or is bounded by the stream).  A fuzz over the
parsed streams (`Props14`, `SummaryInformation`, `214/CEdl/Var2Data` and `VarMeta`, `214/Props`, `114/Props`,
`CV_iew` Var2Data and VarMeta, Assn Var2Data and VarMeta, Task FixedData, Assn Fixed2Data) of three pairs,
with truncations, 0xffffffff, 0, length and length plus 1 poked at every word of the first 120 bytes, and 60
random bit-flip runs each, in the traced, reference and control roles:

**35,532 calls of `compare()`: 0 exceptions, none over 3 s** (124 s in all).  A parse failure leaves a stream
raw on that side.  736 mutated streams passed in the traced and reference roles; 658 of them are in
`114/TBkndAssn/Fixed2Data`, and a separate run of 3,000 random bit-flip files per pair showed every passing
flip lies in a version-1 GUID byte, none at row +32 to +47 (0 of 3,246 passes net of double flips).  Word
pokes into `Props14`, `SummaryInformation` and `214/CEdl/Var2Data` (9 values at each of the first 30 word offsets, 270 pokes per stream, over the first 120
bytes, three pairs): the only passes are the CEdl words +0, +4, +8 set to 0 (9 of 9; R36G0c-1).  The other 78
passing bit-flip files (in `Props14`, `SummaryInformation` and CEdl) were not each decoded; they are flips
inside text, FILETIME or derived fields.  No hang and no crash was reached from a mutated real stream; a
damaged compound file on the command line was not fuzzed.

## Not checked

- Nothing was compiled or run under ctest; `test_host_skip.ps1`, `host.ps1` and `trace_host_scripts` were
  read, not run (so R36G0b-9 is read only); the CI steps were not run.
- No real VM recording, and nothing in `build/trace-2021/` or `build/r7/`, was read; the trace-evidence
  notes that `trace-gate-fields-g0b.md` cites by file name were not opened, so the "decoded field" column of
  the evidence note is taken on trust.  R36G0b-8 (a real recording failing on undecoded noise) is not
  testable here.
- The frozen copy of the tools on the VM.
- `build/w36/` logs (the builder's red and green runs) were not read; every number here is my own run.
- R36G0c-1 and R36G0c-3 to -6 were run through `compare()` on dictionaries, because olefile cannot grow a
  stream; the same-size cases were also run by the command line (the seven inverted files, 6 of the Props14
  and SummaryInformation files), and the sweeps by the command line.
- The collision case (R36G0c-2) is one hand-patched directory entry, in one stream; other collisions
  (a storage-only difference, a root CLSID, timestamps of directory entries) are not compared by the gate
  and were not examined.
- 14 of the 56 OPEN pairs were not sampled, and the reasons printed in the test for the 40 unsampled streams
  were not checked against bytes.
- Whether a view string of zeros (R36G0c-3) or a zero stamp (R36G0c-6) is something a real Project save can
  write; only that the gate accepts it.

Probe, sweep and mutation scripts and their outputs are kept only in the reviewer's temporary folder.
