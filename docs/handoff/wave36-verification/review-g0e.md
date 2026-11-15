# Review of G0e (0c0563e..14b0818, parent 1aaaa2e), read-only

Reviewer worktree: `D:/dev/msproject-mac-wt-revg0e` (detached at 14b0818, left in place). Nothing in `D:/dev/msproject-mac` was touched.
Scripts and logs: `D:/dev/msproject-mac-wt-revg0e/build/rev/` (`mut.py`, `m_old.py`, `m_new.py`, `probe1.py`, `probe2.py`, `probe3.py`, `crashcheck.py`, `*.log`).
Mutations were made on temporary copies of the gate (the worktree file was never edited, so there is nothing to restore; `git status` of the worktree is clean). The harness `mut.py` counts each anchor (count asserted) and is LF/CRLF aware (the gate is LF only: 0 CR bytes).

## Summary

G0e fixes R36G0d-1, -2, -3 and -4 as the brief asks. The derived meta-test is not vacuous (214 candidates, 202 red, 12 redundant, 0 green, 0 crashed; I re-ran the full default meta-test and got the builder's numbers). A crash with no FAIL line is counted as a failure to detect. A value just outside a span FAILs on the command line. My own 84 mutations, of kinds the meta-test does not derive, left 12 green; about 7 are equivalent or verdict-neutral and 5 are real gaps in the test (Low). The structural gap behind them (the derive step only sees if/and/or/ifexp conditions) is the one Medium.

Findings: 0 Critical, 0 High, 1 Medium, 5 Low (R36G0e-1 to -6). The commit messages and the diff are clean of personal names, user folders and client names.

## 1. The G0d findings

| id | status | evidence |
|---|---|---|
| R36G0d-1 (High) | **fixed** | below |
| R36G0d-2 (Medium) | **fixed** | below |
| R36G0d-3 (Medium) | **fixed** | below |
| R36G0d-4 (Low) | **fixed** (crash half by 0e.1, label by 0e.4); see R36G0e-2 for what still counts as red | below |
| R36G0d-5 (Low) | queued, unchanged, as the brief allows (coordinator's call) | `wave36-complete.md` "Queued" |
| R36G0d-6 (Low) | records not rewritten; the G0e record states the correct counts | `wave36-complete.md` "Found, not fixed" |

### R36G0d-1: the four guards removed with the old test still green

Command: `python build/rev/mut.py build/rev/o build/rev/m_old.py 4` (`build/rev/o` = the 1aaaa2e gate and the 1aaaa2e test, with `tests` junctioned in; the four removals are the review's) and the same file against the 14b0818 tree (`python build/rev/mut.py . build/rev/m_old.py 4`).

```
At 1aaaa2e (old test):
old-tail-count               GREEN (exit 0) <<<<<< SURVIVED
old-si-section-size          GREEN (exit 0) <<<<<< SURVIVED
old-si-first-value           GREEN (exit 0) <<<<<< SURVIVED
old-si-lastsave-type         GREEN (exit 0) <<<<<< SURVIVED
At 14b0818 (new test):
old-tail-count               red: FAIL W36-0c.4 a count word of 0 over the two entries fails
old-si-section-size          red: FAIL W36-0e.1 a SummaryInformation section size word -4 in the traced save fails
old-si-first-value           red: FAIL W36-0e.1 a SummaryInformation whose first value does not follow the table in the traced save fails
old-si-lastsave-type         red: FAIL W36-0e.1 LastSave with the type word 0x41 in the traced save fails
```

4 of 4 green before, 4 of 4 red after.

Baseline: `python tools/trace/test_trace_gate.py --no-meta` at 14b0818: 652 PASS, 0 FAIL, "all passed", 41 s. The sweeps are `202 control-nulls, 146 PASS, 56 OPEN; 191 edits, 191 FAIL, 0 PASS`, as the builder says.

Full default meta-test, re-run by me: `python tools/trace/test_gate_guards_meta.py --jobs 7` (log `build/rev/meta_full.log`, about 45 minutes, run alongside my other work):
```
named guards listed 67, red 67, not red 0
candidates derived 214 (run 214): removed red 202 (61 of them by a case that asserts the gate does not raise), listed redundant 12, green 0, crashed 0
the unmodified gate: passes
```
Identical to `gates-g0e.md`. The 12 REDUNDANT entries each print GREEN with their cover; I read the twelve reasons and found none wrong (for example `_canon_summary:and-true:len(raw) == 12` is covered by `_filetime_ok` taking only `raw[4:]` of eight bytes; `noise_windows:and-true:off != 4294967295` by `_stamp_ok` of an empty slice).

### R36G0d-2: the six wrong files

Command: `python build/rev/probe1.py build/rev/o/tools/trace` against the old gate, and `python build/rev/probe1.py tools/trace` against the new one. It builds, from the committed `wave30-calculate` and `wave35-g3-insert-milestone-first` null and control saves, the wrong files as the review describes them, in both the traced and the reference role, and calls `compare()` (my own construction, not the test's helpers): both save words = 1; both = 0x7fffffff; the two words unequal; LastSave in 2600; LastSave in 2001; and (R36G0d-3) both Props14 tail names emptied. 6 files x 2 pairs x 2 roles = 24 cases.
```
old (1aaaa2e):  cases 24, accepted (PASS) 24
new (14b0818):  cases 24, accepted (PASS) 0
```
Every case on the new gate shows `FAIL problems=1` or `2`. The baseline of each pair (unmodified null against its control) is `problems=0` on both.

### R36G0d-3
The "Props14 tail names emptied" cases are 4 of the 24 above: PASS at 1aaaa2e, FAIL at 14b0818 (`problems=1` traced, `problems=2` reference).

### R36G0d-4
The crash is a named failure now: see section 3. `control-mask-count` is relabelled "a pinned number, not a guard removal" (88488a1).

## 2. My own mutations

84 mutations, none of them a removal the meta-test already makes. Each at a call site (one operand of a two-operand guard, both sides), in a helper, in a constant or a mask range, in the CLI. Command: `python build/rev/mut.py . build/rev/m_new.py 4` (`build/rev/mut_new.log`). Each runs `test_trace_gate.py --gate-dir <copy> --no-meta --fail-fast`.

Result: **72 red, 12 GREEN (survived), 0 crashed, 0 did not fit.**

Red groups: 16 call-site and set-operation mutations (e.g. `^` of the derived sets changed to `&`, `-`, reversed `-`: all red); span and bound mutations (the four `<=` made `<`, the spans widened by one: red; the lower one by the positive sweep, which is itself a good sign, since the newest committed file sits on the upper bound); 22 widened-mask mutations (every `range(...)` of a noise window grown by 1 to 8 bytes) of which 21 red; `_text_ok` cap `>=`, GUID variant and version, `GUIDSTR` digit skips, font garbage offsets, `endswith` matching in `_stride` and `_noise_rule`; the loaders; the 10 `problems += 1` lines of `compare` (each removed alone: red).

The 12 survivors:

| mutation id | what it changes | verdict |
|---|---|---|
| `h-text-trailing-nul-strip-all` | `_text_ok` strips every trailing NUL instead of one | real gap, R36G0e-3 |
| `c-canon-suffix` | `canonical_all` matches CANON by suffix (`zzz?SummaryInformation` would be canonicalised) | real gap, R36G0e-4 |
| `w-p14-count-range` | the named-absent noise window grows from the count word to the count word and the first entry's id | real gap, R36G0e-5 |
| `c-main-argc` | `len(argv) != 5` becomes `< 5` (extra arguments accepted) | real gap, R36G0e-5 |
| `c-mask-header-rows` | the header-row control mask of `_mask_by_id` is emptied | label only (`control-mask` against `bytes-differ`); verdict-neutral; R36G0d-5 territory |
| `cs-win-traced-side-dropped`, `cs-win-ref-side-dropped` | the noise windows of one side only | probably equivalent: the conditions are symmetric and the windows have the same offsets when the streams have the same length and framing; I did not build a file whose framing differs |
| `h-text-cap-2050` | `> 2048` to `> 2049` | equivalent: UTF-16 payloads are even |
| `h-guidstr-range37` | `range(38)` to `range(37)` | equivalent: digit 37 is excluded already |
| `h-tail-scan-end`, `h-tail-scan-step1` | the scan stops 1 byte early; it steps by 1 instead of 2 | probably equivalent for every committed shape (an entry with an empty name is not a name now; a misaligned mark needs crafted bytes); not shown either way |
| `h-named-absent-len1` | `len(entries) == 1` to `>= 1` | probably equivalent: a second entry changes the canonical count word, so the pair differs anyway; not shown |

## 3. The derived candidate list and crashes

Not vacuous: 214 candidates derived from `ast` (`python tools/trace/test_gate_guards_meta.py --list`), 202 red, 12 listed redundant, 0 green. Neither 0 nor 100 per cent; the 12 are green with a red cover that I read.

A crash is a failure to detect. `python build/rev/crashcheck.py` calls the meta-test's own `run_mutant` on four hand-made mutants:
```
syntax-error mutant -> ('CRASH', "AttributeError: 'NoneType' object has no attribute 'group'")
compare() raises -> ('red (raise)', 'gate raised on the command line: RuntimeError: boom')
_u32 exits 0 (SystemExit) -> ('CRASH', 'IndexError: list index out of range')
_u32 os._exit(0) -> ('CRASH', 'IndexError: list index out of range')
```
CRASH makes `main` return 1 (`ok = ... not d_crash and not n_crash`). A gate that raises is turned by the test into a named FAIL and is `red (raise)`: see R36G0e-2.

## 4. Range masks just outside the span

Spans against the files: `python build/rev/probe2.py` (`build/rev/probe2.log`) reads all committed `.mpp`:
```
files 624; LastSave values 624: min 0x1dd62019f6a0e00 max 0x1dd72130aac6c00 (gate span 0x1dd62019f6a0e00..0x1dd72130aac6c00)
Props save words 1248: min 0x3d141482 max 0x3d282db4 (gate span 0x3d141482..0x3d282db4); files with unequal words: 0
```
The two Props words and LastSave match the gate exactly. (I did not recount the Task +128 and Assn stamps that make up the "5835".)

Files built from the committed `wave30-calculate` null by same-size stream writes, run with the gate's command line (`python tools/trace/trace_gate.py <file> <control> <control> <null>`):
```
traced = the control-null itself (control)     exit 0  ... 0 problems
Props words = span_hi (inside)                 exit 0  ... 0 problems
Props words = span_hi + 1 (just outside)       exit 1  ... 2 problems | bytes-differ ???114/Props 2 | control-mask ???114/Props 2
Props words = span_lo - 1 (just outside)       exit 1  ... 2 problems | bytes-differ ???114/Props 4 | control-mask ???114/Props 2
LastSave = span_hi (inside)                    exit 0  ... 0 problems
LastSave = span_hi + 1 tick (just outside)     exit 1  ... 2 problems | derived ?SummaryInformation pid 13 | bytes-differ ?SummaryInformation 8
LastSave = span_hi + 1 day (just outside)      exit 1  ... 2 problems | derived ?SummaryInformation pid 13 | bytes-differ ?SummaryInformation 7
LastSave = span_lo - 1 tick (just outside)     exit 1  ... 2 problems | derived ?SummaryInformation pid 13 | bytes-differ ?SummaryInformation 8
```
Both ends are inclusive and one past either end FAILs. Also `python build/rev/probe1.py tools/trace edge` (compare, both roles, two pairs): the ends PASS, one below and one above FAIL, 32 cases.

## 5. Hygiene

`git log --format='%B' 1aaaa2e..14b0818` and `git diff 1aaaa2e 14b0818`, searched for personal names, `Users`, drive paths, e-mail addresses, company and client words: the only hits are the `Co-Authored-By: Claude Sonnet 5.5` trailer lines and the word "builder". No user folder path, no client name. All 5 commits carry `Generated-By`. The author and committer fields of the commits hold the repository owner's configured name and a GitHub no-reply address (metadata, not message or diff). I could not run `scan_client_names.py`: it is not a tracked file in this worktree.

## Findings

### R36G0e-1 (Medium): the derived list does not see most of the gate's guards

The meta-test's candidates come from `if`, `and`, `or` and conditional expressions only. A guard that is a comparison operator, a constant, a slice or range bound, a set operation, a `try/except`, a bare `return a == b`, or a name match is not a candidate. The gate's rule is that every guard goes red when removed; the 214 are only the ones `ast` sees. My 84 non-derived mutations (section 2) left 12 green, 5 of them real gaps (R36G0e-3 to -5), 7 equivalent or not shown.
Command: `python build/rev/mut.py . build/rev/m_new.py 4 | grep GREEN`
```
cs-win-traced-side-dropped   GREEN (exit 0) <<<<<< SURVIVED
cs-win-ref-side-dropped      GREEN (exit 0) <<<<<< SURVIVED
h-text-cap-2050              GREEN (exit 0) <<<<<< SURVIVED
h-text-trailing-nul-strip-all GREEN (exit 0) <<<<<< SURVIVED
h-tail-scan-end              GREEN (exit 0) <<<<<< SURVIVED
h-tail-scan-step1            GREEN (exit 0) <<<<<< SURVIVED
h-named-absent-len1          GREEN (exit 0) <<<<<< SURVIVED
h-guidstr-range37            GREEN (exit 0) <<<<<< SURVIVED
w-p14-count-range            GREEN (exit 0) <<<<<< SURVIVED
c-main-argc                  GREEN (exit 0) <<<<<< SURVIVED
c-canon-suffix               GREEN (exit 0) <<<<<< SURVIVED
c-mask-header-rows           GREEN (exit 0) <<<<<< SURVIVED
```
Fix: add to `derive` the comparison operators (each `Compare` op replaced by its neighbour), numeric constants (plus and minus one), `range`/slice bounds and `set` operators, and `except` clauses; triage with `REDUNDANT` as before. The record's "the meta-test's list of guards is derived from the gate" is true of conditions, not of guards.

### R36G0e-2 (Low): 61 of 202 "red" candidates are red only because the gate raised

`red (raise)` is counted red. The brief says a crash no longer counts as red. The test turns a raise into a named FAIL ("gate raised ..."), so no removal is a harness CRASH, but a removal whose only effect is that the gate raises on some input is shown by a case that asserts "no raise", not by a wrong file that the gate now passes. Some are legitimate raise-guards (for example `frame_records:if-never:pos + 12 > len(data)` -> `unpack_from requires a buffer`), and the compare-never-raises rule is a fair one, but the record's "0 crashed" is true only after this relabelling.
Command: `grep -c "^red (raise)" build/rev/meta_full.log` -> `61`; sample: `red (raise)  derived _canon_cedl_var2:if-never:recs is None (line 272) -> gate raised on the command line: TypeError: 'NoneType' object is not iterable`.
Fix: report the raise group separately in the summary line and say which of them also change a verdict; no code change needed.

### R36G0e-3 (Low): `_text_ok` "at most one trailing NUL" has no case

Command: `python build/rev/mut.py . build/rev/m_new.py 4` (mutant `h-text-trailing-nul-strip-all`: `text = text.rstrip("\0")` for the one-NUL strip) -> `GREEN (exit 0) <<<<<< SURVIVED`. The gate gives `_text_ok("ab\0\0") False`, `_text_ok("ab\0") True` (`python build/rev/probe3.py`), but no test has a name or path with two trailing NULs. Fix: one case for each of the path, name and CEdl path records.

### R36G0e-4 (Low): the whole-name rule is tested for `STRIDE` and `NOISE` but not for `CANON`

`canonical_all` uses `name in CANON`. The mutant that matches CANON by suffix (`name.endswith(k)`, so `zzz?SummaryInformation` is canonicalised) is GREEN; the same change in `_stride` and `_noise_rule` is red (W36-0b.3). The module comment says every stream name is matched whole. Fix: a W36-0b.3-style case for the five CANON streams.

### R36G0e-5 (Low): two more unpinned places

* `w-p14-count-range` (named-absent noise window `range(end, end + 4)` grown to `end + 8`, which would also take the first entry's id word) is GREEN: the noise set of the lone-entry shape is verdict-neutral and the sweep's own list of windows does not check it.
* `c-main-argc` (`len(argv) != 5` to `< 5`) is GREEN: extra command-line arguments are accepted and no test passes six.
Command: `python build/rev/mut.py . build/rev/m_new.py 4 | grep -E "w-p14-count-range|c-main-argc"`.

### R36G0e-6 (Low, disclosed by the builder): the spans fail every save after 11 November 2026

Not a defect in what was asked. Stamps and LastSave outside the committed span are data by design (section 4 shows it). Today is 15 November 2026, so any new capture made after the newest committed save FAILs until `STAMP_SPAN`, `LASTSAVE_SPAN`, the test's copy and the evidence table are widened together; the span is the data's range, not a law of the writer (the newest committed file sits exactly on the upper bound; `h-ft-hi-strict` is caught by it). The coordinator's re-gate will show it. Recorded here because two other places hold copies of the numbers and must move with the gate.

## What I did not check

* Denominators: 4 of the review's 4 wrong-file classes for R36G0d-1 and 24 of 24 files for -2 and -3 (6 kinds x 2 pairs x 2 roles); 84 own mutations; the meta-test 67 named + 214 derived re-run in full once; 8 + 32 boundary cases; 624 of 624 files for the span.
* The creation stamps of a new Task row and of a new Assn block (the third and fourth parts of `STAMP_SPAN`) were not rebuilt as just-outside files (the test's cases cover them; my `cs-newrow-*` and `cs-varkey-*` mutations were red). The "5835" count was not re-derived.
* I did not run the ctest `trace_gate_guards` itself, only its two scripts.
* The 56 OPEN pairs, the evidence rows other than "Value bounds", the 12 non-equivalent-looking REDUNDANT covers beyond reading them, and the `scan_client_names.py` scan (not tracked) were not examined.
* The seven survivors I call equivalent or probable were reasoned, not proved; no file with different framing on the two sides was built for `cs-win-*`.
* No VM, COM, `build/trace-2021/`, `build/r7/` or anything under `%LOCALAPPDATA%\deepseek`.
