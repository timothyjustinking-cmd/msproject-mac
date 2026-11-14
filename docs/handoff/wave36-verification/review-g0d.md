# Review: wave 36 G0d, `d57e33b..b57a87c` (branch `wave36`), run at the tip `b57a87c`

Independent, read-only, adversarial review, 14 November 2026.  Eleven commits (`1956d20` to `b57a87c`: nine items,
the docstring item and the group record).  Read with `git show`, `git diff` and `git log` only; nothing checked
out, built, staged or changed in the tree; no compiler, cmake or ctest was run; the VM, `build/trace-2021/` and
`build/r7/` were not touched.  `tools/trace/trace_gate.py`, `tools/trace/test_trace_gate.py` and
`tools/trace/test_gate_guards_meta.py` were exported from `b57a87c` into `build/verify-w36/g0d-review/`
(git-ignored: the reviewer's temporary folder), and run and mutated there with Python against the committed
fixtures in `tests/data` (read only; the copy of the test has its `DATA` line pointed at them).  The earlier
reviews' probe scripts were reused after reading them and re-pointed at the tip copy.  One Python process at a
time.

**Count: 0 Critical, 1 High, 2 Medium, 3 Low.**

Reproduced runs, one each, on `b57a87c`:

| run | denominator | result |
|---|---|---|
| the committed test, copy, `python test_trace_gate.py --no-meta` | 491 checks | **491 PASS, 0 FAIL, "all passed", exit 0** (40 s) |
| positive sweep (printed by the test) | 202 control-nulls | **146 PASS, 56 OPEN**, as the record says |
| negative sweep (printed by the test) | 191 edits | **191 FAIL, 0 PASS** |
| every control/null pair with the bytes where it differs inverted, REFERENCE = the null, `compare()` | 202 pairs | **202 FAIL, 0 PASS** |
| the seven named pairs of R36G0b-1, inverted, by command line, REFERENCE = the edit and again = the null | 7 + 7 files | **14 of 14 exit 1**; the unaltered edit exits 0 (control) |
| hand-built wrong files of review-g0c.md and review-g0b.md (table below) | 214 + 48 + 7 + 6 CLI + 18 + 30 | every file FAILs except those listed under "Results" |
| meta-test, my own sub-run of six guards of the 61 (`--jobs 1`; `--list` prints 61) | 6 | **6 of 6 red** (5 by a failing case that is the guard's own new case, 1 by a crash) |
| my own removals of guards that are **not** on its list of 61 | 9 | **5 GREEN (the test stays green), 4 red** (R36G0d-1) |
| commit trailers | 11 commits | 11 of 11 carry `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 36)`; 11 of 11 have one item id in the subject |

## What holds

- **R36G0c-1, the two Critical collisions and the placeholder, are closed.**  Each of the five derived words
  set to 0 (CEdl Var2Data +0, +4, +8; CEdl VarMeta +20; `214/Props` record 0x00010007), on each of three
  pairs, in the traced and in the reference role: **30 of 30 FAIL** (`derived <stream> <field>`), and the
  same word 0 on both sides is equality (15 of 15 pass, as they should).  The coordinator's own file (`wave30-calculate`,
  `214/CEdl/Var2Data` +0 = 0, same size, by command line, both directions): exit 1.  An emptied CEdl path
  record FAILs on three pairs (my probe); the emptied Props14 path record is a committed case with its guard
  `props14-empty-path` on the list (not run by me).  The derived flag cannot be set falsely: it is
  set only when the word equals what its own stream implies (and for a path record only when the text test and
  the trailing `!` pass), and a flag on one side only is a problem.
- **R36G0c-2.**  The review's patched directory (a stream renamed to a name that renders alike) FAILs
  (`stream name collision`), on both sides and either side.  A stream whose raw name differs but renders alike FAILs
  (`stream name differs`); a **storage** renamed to another raw name with the same printable name (a storage that
  holds 59 streams) FAILs with 59 problems and the head line counts 155 raw entries; the same file on both sides
  passes.  A stream on one side only is `only-in-traced` / `only-in-reference`.
- **R36G0c-3 is fixed.**  The view GUID string as zeros, with the version digit changed, with the variant digit
  changed, and with a zero timestamp but a valid version: **15 of 15 FAIL** (3 pairs x 5 forms: index 17 to 4, index 17 to 0, index 20 to 0, index 20 to F, a zero
  timestamp with a valid version; and the zero string and Z cases of the old script also FAIL).  My predecessor's "version digit (index 15)" was wrong and the
  builder says so: in the rendered string index 15 is the high nibble of `time_hi`; the version is index 17 and the
  variant index 20.  The idx-15 control (3 of 3) passes as noise, so the three passes of the old script ("version digit 4" at index 15) are not a hole.
- **R36G0c-8 is fixed.**  A CEdl path record and a Props14 path record and a tail name of C1 controls, of
  private-use characters, on three pairs: all FAIL (inside the 211 FAILs below).
- **Hand-built wrong files, all FAIL (denominators in "Results").**
- **Exit paths and OPEN.**  The gate cannot exit 0 on an OPEN pair: an OPEN pair is a problem line and exit 1
  by command line (`main` returns 0 only when `problems == 0`; the test's in-process sweep has 56 pairs with problems; I did not
  re-run the 202 by command line); OPEN
  exists only in the test, which prints it and exits 0 by design.
- **The OPEN pins are exact.**  The pins are compared with the observed `(pair, stream, line)` rows as sorted
  lists, so a pair that joins, a stream that joins, a count that grows or shrinks, or a `size-differs a->b` that
  moves, fails.  A count that stays equal while the bytes move to another field of the same stream cannot be seen
  (the brief asked for the count).
- **Trailers and ids:** 11 of 11 commits, one id each.

## Results: hand-built wrong files

Scripts and outputs are in the reviewer's temporary folder.  Each file was run through the gate at `b57a87c`
(the unaltered file first; it passes).

| case | built how | denominator | result |
|---|---|---|---|
| R36G0b-1, seven named pairs, inverted control/null bytes | by olefile in a copy, command line, both directions | 7 + 7 files | **14 of 14 exit 1**; untouched edits exit 0 (7 of 7) |
| R36G0b-1, every pair | the null with the bytes inverted where control and null differ, `compare()` | 202 pairs | **202 FAIL, 0 PASS** |
| R36G0b-2 and -3 (Props14 tail and SummaryInformation: junk, second 0xffffffff entry, lying length words, padding, Title, CreateTime, pid 18/19, LastSave slot) | `compare()` on two pairs | 48 files built, 16 expected-pass | 44 FAIL; **4 PASS**, all the named valid-text cases (a 30-byte 0xffffffff name that is valid UTF-16, two NUL bytes in the last name), as in the G0c review; of the 16 expected passes, 12 pass and **4 FAIL** (an Author slot of 0xff bytes with no terminating NUL, and an Author slot of length 0: `_lpstr_text_ok` now refuses a slot with no terminating NUL, on purpose; a 33-byte 0xff string with its NUL, which the builder says stays noise, I did not build) |
| R36G0b-3, by command line | 6 files | 6 | **6 of 6 exit 1** |
| R36G0b-4 / R36G0c-1..-8, GUIDs (zeros, all version 4, variant, every GUID-bearing stream), view GUID strings, CEdl path (C0, C1, private use, no `!`, emptied, size words +1), 0x00010007 +1 and 0, VarMeta +20, Props14 path and names (C0, C1, zeros), all tail entries dropped, blob words, count word | three pairs, `compare()` | 214 files built, 29 expected-pass | **211 FAIL**, 3 PASS (the idx-15 `time_hi` digit, a time digit, not the version: not wrong files); the 29 expected passes all pass |
| R36G0c-1 five words to 0 | as above | 30 files | 30 of 30 FAIL |
| view GUID string, indices 15, 17, 20 and zero timestamp | three pairs | 18 files | 15 FAIL, 3 pass (the index-15 control) |
| R36G0b-6 rows (SI LastSave 0 and all-ones, two 114/Props save words 0 and all-ones, a Title and similar) | 7 files | 7 | **7 of 7 FAIL** |
| a probe of the seven stray cases of R36G0b (rv_probe: appended 0xffffffff entry, 240 junk bytes, GUID zeroing per stream) | `compare()` | 16 | all FAIL; one printed PASS is a harness no-op (0 of 160 bytes zeroed in `TBkndRsc/Fixed2Data`: a stream with no version-1 GUID in that file) |

Not FAIL, found by new probes: R36G0d-2 and -3 below.

## Status of R36G0c-1 to -11

| id | status | evidence |
|---|---|---|
| R36G0c-1 | **fixed** | 30 of 30, the coordinator's file, the emptied path records; guards `derived-*` and `props14-empty-path` are on the list |
| R36G0c-2 | **fixed** | collision, raw rename of a stream and of a storage, stream on one side only: all FAIL; head count is of raw entries.  Storage entries that hold no stream are still not looked at (Not checked) |
| R36G0c-3 | **fixed** | see above |
| R36G0c-4 | **fixed** | removal of the type check: the test goes red (meta, `slot-type`, first failing case "Author with its type word 0x1f in the traced save fails") |
| R36G0c-5 | **fixed for the three named guards** | `tail-first-entry`, `props14-size-words`, `text-cap`: each red on its own new case (junk after the count; blob word +4 by 16; two tail names of 2050 bytes).  But the same shape of hole is in guards the review did not name (R36G0d-1) |
| R36G0c-6 | **partly fixed** | zero, all-ones, FILETIME before 2000 and an Author with a control byte now FAIL; a value of the right kind but wrong content still passes (R36G0d-2) |
| R36G0c-7 | **fixed as the brief asks** | the pin is (pair, stream, count); the two wrong reasons are corrected; the record id is still absent |
| R36G0c-8 | **fixed** | above |
| R36G0c-9 | **queued, as the record says** | redundant `named_absent` conditions, not listed |
| R36G0c-10 | **not fixed, queued** | the null still proves nothing; the CLI and the specs still demand one |
| R36G0c-11 | **fixed** | the docstring now says what the waiver is and is not (a stamp of any non-zero value, text content, per-save fields nobody decoded are named) |

## Findings

### R36G0d-1 (High): guards of the gate that are not on the meta-test's list of 61 survive removal

The brief: "every guard in the gate has a test that goes red when the guard is removed"; the meta-test's own
claim is "every named guard".  The list is the G0c review's table plus what G0d added, so a guard nobody
named is not tested, which is the shape of R36G0c-4 and -5 again.  I read the gate for every clause that can
add a problem or decide a replacement, took nine that are not on the list, removed each in a copy
(`--gate-dir`, `--no-meta --fail-fast`, the test at baseline 491 PASS) and ran the committed test:

| removal | result |
|---|---|
| `_tail_entries`: `len(marks) != count` (the Props14 tail count word must equal the entries found) | **GREEN, 491 PASS** |
| `_canon_summary`: `_u32(data, sec) != len(data) - sec` (the section size word) | **GREEN** |
| `_canon_summary`: `ents[order[0]][1] != 8 + 8 * count` (the first value follows the offset table) | **GREEN** |
| `_canon_summary`: `_u32(raw, 0) == 0x40` (the LastSave type word) | **GREEN** |
| `font_item_garbage`: `key != BAG_KEY` | **GREEN** (I built no wrong file for it, so it is not shown to be a hole) |
| `new_row`: `_u32(data, row) not in known` | red |
| `compare`: `problems += 1` of `size-differs` | red |
| `only-in-traced` and `only-in-reference` problems | red, red |

For the four green guards I built the wrong file that the real gate fails and the mutant passes (`compare()`,
two pairs each, 12 files):

- Props14 tail count word 2 -> 3, and 2 -> 0 (same bytes otherwise): real **FAIL**, mutant **PASS** (4 of 4);
- SummaryInformation section size word +4: real FAIL, mutant PASS (2 of 2);
- 8 junk bytes between the SummaryInformation offset table and the first value, offsets and section size
  rebuilt: real FAIL, mutant PASS (2 of 2);
- LastSave type word 0x40 -> 0x41 and -> 0: real FAIL, mutant PASS (4 of 4).

So four of the five green removals are real guards (a wrong file passes without them) and no case reaches them.  The record states
the meta-test "lists none of" the redundant `named_absent` conditions because removal changes no verdict; these
four are not redundant.  I did not run the other 10 candidate removals I had written (the Props14 `0x10000`
word, the `!` of the CEdl path record, `length == 4` of the view record, the `0xFFFFFFFF` offset of a var
block, the `_lpstr_text_ok` decode, `_stamp_ok`'s length, the no-null early return, the exit code, the
colliding-name rename, row identity), for time.

- **Fix:** put these four on the list with a case each (each wrong file above is a case).  Then derive the
  list instead of writing it: instrument the gate (or use `ast`) so that every `if ... return`/`problems +=`
  clause is a candidate, and have the meta-test fail for a candidate that is neither on the list nor on an
  explicit "redundant, with the reason" list.  A count "61 of 61" is a count of the list, not of the guards.

### R36G0d-2 (Medium): a masked noise field is accepted for any value of its kind; the record's bounds are two refused values

The brief: "each masked noise field checks its value as well as its place ... Take the accepted range of each
from the committed control and null saves, and quote the evidence line".  `_stamp_ok` refuses 0 and all-ones
only, `_filetime_ok` takes any FILETIME from 2000 to all-ones, and the Author strings any Windows-1252 text.
The evidence note says "no upper bound is kept" and quotes the committed range (stamps 0x3d141482 to
0x3d282db4; LastSave 22 October to 11 November 2026).  Passing wrong files (`compare()`, two pairs, traced
role; every one **PASS, 0 problems**):

- the new Task row's creation stamp set to 0x00000001, and to 0x7fffffff (1 pair x 2);
- the two Props save words (0x02400007, 0x024013B1) set to 1, 2 pairs x 2, and made unequal to each other
  (0x024013B1 set to another plausible stamp; the evidence note says the two are equal in all 624 files and the
  gate does not test it);
- LastSave set to the year 2600 and to 2001-01-01 (2 pairs x 2).

Also a non-zero 33-byte Author of 0xff stays noise (the builder says so).  A stamp "from a different file" is
by nature not decidable; but a value 20 bits away from every committed value is, and the equality of the two
Props words is.

- **Fix:** keep a lower bound from the committed saves for the stamps (say 0x3c000000) and an upper bound that
  moves with the clock the capture ran at; for LastSave, within a year of the run; test that
  `0x02400007 == 0x024013B1` in each file.  Record each bound with its evidence row.

### R36G0d-3 (Medium): an emptied Props14 tail name equals its placeholder

The tail name is replaced by an empty byte string in the canonical form and, unlike the Props14 path record
(`payload and _text_ok`), an empty name is accepted by `_text_ok`.  So the placeholder equals a real value
(an empty name), the rule the brief adds in G0d.  There is also no derived flag for tail names.  **Passing wrong
files:** both tail names of `wave30-calculate` and of `wave35-g3-insert-milestone-first` emptied (the entry
headers and counts kept, the stream shorter): **2 of 2 PASS**, 0 problems.  Not a hole in any record the edits
carry, but it is the G0d rule broken in the one field class the rule was not applied to.

- **Fix:** treat an empty name as not text for a tail name (as for the path record), and add a `name` derived
  flag per entry so that one side raw and one side derived is a problem.  Case: both names emptied FAILs.

### R36G0d-4 (Low): the meta-test counts a crash as red, and one entry is not a guard

The meta-test calls `red (crash)` red: the record says 1 of 61 is red "by a crash" (`guidstr-shape`: the
mutant makes the test die on a `ValueError` rather than a case fail).  That proves the removal is visible, not
that a case covers it.  `control-mask-count` changes a printed number; it is not a guard removal.  And the
test run by `ctest` takes about 8 minutes (60 copies, six at a time), with `TIMEOUT 3600`.
**Fix:** a crash is red only if the crash is the intended behaviour, else add a case; drop or relabel the
count entry.

### R36G0d-5 (Low): R36G0c-10 (the null proves nothing) is not fixed and is not on the acceptance

Queued in the record with a direction; the control null is still demanded by the command line and by
`runone6.ps1`, and is used only to label a failing byte.  A reader of "no null is a FAIL" may think the null
is measured.  **Fix:** the coordinator's call (the record says so).

### R36G0d-6 (Low): records

- The record's "Props noise words and LastSave are of their decoded kind" is true of "a 32-bit word that is not 0
  or all-ones" and "a FILETIME after 2000": a kind, not a value.  R36G0d-2.
- `gates-g0d.md` says "Every hand-built wrong file of `review-g0c.md` FAILs": true of the review's cases except
  the two the builder discloses (the Author of 0xff and the index-15 digit).  Both are disclosed.
- The meta-test's "61 guards" is a count of the list (R36G0d-1).
- The `0d.8` commit message says 60 and the record 61; the 61st is 0d.9's, explained in the record.

## Mutations (my own, in addition to the review of the guard list)

Removal of nine guards that are not on the meta-test's list, each in a copy, the committed test at 491 checks,
`--no-meta --fail-fast`: **5 GREEN, 4 red** (table under R36G0d-1).  For six guards of the list I re-ran
the meta-test itself (`--jobs 1`): `slot-type`, `tail-first-entry`, `props14-size-words`, `text-cap` and
`stream-count` are red on the named new case; `guidstr-shape` is red by a crash (`ValueError`), as the record
says.  `guards listed 6, red 6`.

## OPEN: does an entry hide a field the evidence says is data?

I did not decode the 56 pairs again.  For the two reasons the builder corrected (Assn Fixed2Data +32..+37 and
+41; Task Fixed2Data +42, +46..+48) I compared the edit, the control and the null on the committed pairs of
those streams whose three files have the same length (six Assn pairs show a control/null difference there; no
Task pair was usable): of the 25 bytes in which control and null differ, the edit equals the control in 17, the
null in 0, and neither in 8.  So the per-save reading holds for most bytes; the 8 bytes in which the edit equals
neither (every one of the six pairs has at least one) could be a per-save value or a value the edit changed, which this comparison cannot separate.  No
OPEN entry is shown to hide data; none is shown not to.  The count-only pin cannot see a field that moves
within the stream.

## Hang and crash

I read the new code (`Streams`, the `derived` flags, `_stamp_ok`, `_filetime_ok`, `_lpstr_text_ok`,
`_guidstr_bytes`, `_is_v1_guid`, `_text_ok`) for an unguarded index or an unbounded loop: none.  `uuid.UUID` is
reached only after the regex has matched, so a non-hex digit cannot raise (the meta-test's crash case is the
mutant with the regex removed).  The G0c fuzz (35,532 `compare()` calls, 0 exceptions) was not re-run; I ran
nothing on a damaged compound file.

## Not checked

- Nothing was compiled or run under ctest; `test_host_skip.ps1` and `host.ps1` were not touched.  The full
  default meta-test (61 copies) was not run by me: I ran six of its guards and nine of my own.
- Ten candidate guard removals written for R36G0d-1 were not run (listed there); the other clauses of the gate
  (the Props14 `0x10000` word, the `!` of the CEdl path record, row identity, the colliding-name rename) were
  read, not removed.
- The fuzz of G0c was not repeated on the new code; the new code was read only.
- Storage entries that hold no stream, the root entry's CLSID and the directory timestamps are not compared by the
  gate and I did not examine them (a storage renamed so that it holds streams is caught).
- The 56 OPEN pairs were not re-decoded; the evidence rows were not re-derived from the 624 files.
- No VM recording, nothing in `build/trace-2021/` or `build/r7/`.
- Whether a stamp of 1 or a LastSave in 2600 is something a real save can write: only that the gate accepts it.

Probe, sweep and mutation scripts and their outputs are kept only in the reviewer's temporary folder.
