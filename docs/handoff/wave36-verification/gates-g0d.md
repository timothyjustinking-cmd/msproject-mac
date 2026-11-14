# Wave 36 G0d: the builder's gates

Builder Claude Sonnet 5.5, 14 November 2026, branch `wave36`.  G0d is the third fix of the trace gate (`tools/trace/trace_gate.py`, `tools/trace/test_trace_gate.py`),
from the G0c review (`review-g0c.md`, R36G0c-1 to -11).  Logs are under `build/w36/` (git-ignored), each named for the parent commit of the item it gates.  The builder
used no COM and did not touch the VM, `build/trace-2021/` or `build/r7/`.  G0d changes no C++ source: `CMakeLists.txt` changes in one place, the `TIMEOUT` and its comment
of the ctest `trace_gate_guards` (300 s to 3600 s), so the 0d.8 doc gates print SOURCE CHANGED for it.

## Acceptance counts

The committed ctest `trace_gate_guards` (`tools/trace/test_trace_gate.py`, then `tools/trace/test_gate_guards_meta.py`).

| sweep | TRACED | REFERENCE | at G0c (`3c92b13`) | at G0d (`f48beba`) |
|---|---|---|---|---|
| negative | the control's null save | the pair's edit | 191 edits, 191 FAIL, 0 PASS | 191 edits, **191 FAIL**, 0 PASS |
| positive | the control's null save | the control | 202 control-nulls, 146 PASS, 56 OPEN | 202 control-nulls, **146 PASS, 56 OPEN** (the 56 now pinned as 170 (pair, stream, count) rows) |

Every hand-built wrong file of `review-g0c.md` FAILs (R36G0c-1: 5 words x 2 sides, the empty path records; -2: the collision and the renamed stream; -3: the view GUID string
as zero, version, variant and timestamp; -4 and -5: the type word, the first tail entry, the blob size word, the 2048-byte cap; -6: the stamp, FILETIME and string cases;
-8: four character classes in two records, two sides).  The test: 491 checks, 0 FAIL (G0c: 335).

## The meta-test (W36-0d.8)

`tools/trace/test_gate_guards_meta.py` lists 61 guards (60 at 0d.8, 61 with 0d.9's), plants each removal in a temporary copy of the gate, and runs the test against it with
`--gate-dir DIR --no-meta --fail-fast`.  The last full run, the plain default run on the tree committed as `a04d135` (`build/w36/ea5e70d-W36-0d.9-green.log`; `f48beba` adds a docstring only):

`guards listed 61, red 61 (60 by a failing case, 1 by a crash), not red 0` and `exit 0`.

* The first run of 0d.8 found **59 red and 1 green**, `stream-count` (the head line counting printable names, not raw entries): no case read the head count of a renamed stream
  (`build/w36/f517eb2-W36-0d.8-red.log`).  One check was added (the head line is `entry_count(ref) + 1`, both directions) and the guard went red.
* The one red-by-crash is `guidstr-shape`: without the shape test a non-hex digit reaches `uuid.UUID` and the test dies with a `ValueError` (exit 1), not a FAIL line.
* A guard whose text to remove does not occur exactly as stated is reported NOFIT and the run fails (one was miscounted when the table was written: 4 sites, not 5).
* The default run is 486 s at 0d.8 (the test 36 s, the 60 copies six at a time).  A run that is not the plain default (`--gate`, `--gate-dir`, `--cli-only`, `--no-meta`)
  does not run the meta-test.

## Per commit

| item | id | commit | parent (log prefix) | red | green |
|---|---|---|---|---|---|
| W36-0d.1 | R36G0c-1 | `1956d20` | `d57e33b-W36-0d.1-*` | 14 FAIL (each of five words set to 0, both sides; the empty path records), exit 1 | 357 PASS |
| W36-0d.2 | R36G0c-2 | `6a7d4f4` | `1956d20-W36-0d.2-*` | 6 FAIL of 10 (the collision on both sides and the renamed stream returned rc 0), exit 1 | 367 PASS |
| W36-0d.3 | R36G0c-3 | `527a951` | `6a7d4f4-W36-0d.3-*` | 10 FAIL (zero digits, version, variant, zero timestamp), exit 1 | 381 PASS |
| W36-0d.4 | R36G0c-4 | `b239c79` | `527a951-W36-0d.4-*` (mutant 13) | the type clause removed: 12 FAIL | 399 PASS |
| W36-0d.5 | R36G0c-5 | `b13b86c` | `b239c79-W36-0d.5-*` (mutants 14, 15, 16) | `marks[0]` 2 FAIL, size-word cross-check 2 FAIL, 2048 cap 1 FAIL | 408 PASS |
| W36-0d.6 | R36G0c-6 | `89c5fb4` | `b13b86c-W36-0d.6-*` | 52 FAIL, 9 PASS (the equality and noise cases), exit 1 | 469 PASS |
| W36-0d.7 | R36G0c-7 | `f517eb2` | `89c5fb4-W36-0d.7-*` (mutant 17) | the printed count + 1: 9 FAIL (8 earlier + the pin), exit 1 | 471 PASS |
| W36-0d.8 | R36G0c-4, -5 | `ea5e70d` | `f517eb2-W36-0d.8-*` | the meta-test's first run: 59 red, 1 green (`stream-count`) | 473 PASS, then 60 of 60 red |
| W36-0d.9 | R36G0c-8 | `a04d135` | `ea5e70d-W36-0d.9-*` | 17 FAIL (16 character cases and the refusal check), exit 1 | 491 PASS, then 61 of 61 red |
| W36-0d.10 | R36G0c-11 | `f48beba` | `a04d135-W36-0d.10-*` | none: a docstring only | 491 PASS (`--no-meta`) |

Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only;
each carries both trailers.  The sweeps (202 control-nulls, 146 PASS, 56 OPEN; 191 edits, 191 FAIL, 0 PASS) were unchanged at every commit.

## The guards (the meta-test's table, by finding)

| finding | guards | ids |
|---|---|---|
| R36G0b-1 | 3 | `control-mask-no-problem`, `control-mask-excuses`, `bytes-differ-no-problem` |
| R36G0b-2 | 5 | `named-absent-header`, `named-absent-count`, `tail-ffff-dropped`, `tail-names-waived`, `tail-entries-dropped` |
| R36G0b-3 | 3 | `slot-length`, `slot-padding`, `slot-call-site` |
| R36G0b-4 | 6 | `guid-zero`, `guid-record-other`, `guid-absolute-other`, `guid-props-other`, `new-row-other`, `varkey-uid` |
| R36G0b-6 | 11 | `si-placeholders`, `si-lastsave-any`, `text-ok-true`, `cedl-path-waived`, `cedl-size-words`, `view-props-size`, `props14-path-waived`, `font-byte-any-record`, `cedl-meta-length`, `guidstr-shape`, `guidstr-other` |
| R36G0b-10, W36-0.3, W36-0.4 | 4 | `not-checked-uncounted`, `no-null-count`, `nothing-checked`, `control-as-null` |
| R36G0c-1 | 6 | `derived-compare`, `derived-cedl-words`, `derived-cedl-path`, `derived-cedl-meta`, `derived-view-size`, `props14-empty-path` |
| R36G0c-2 | 3 | `stream-collision`, `stream-raw-name`, `stream-count` |
| R36G0c-3 | 4 | `guidstr-v1`, `guid-version`, `guid-variant`, `guid-timestamp` |
| R36G0c-4, -5 | 4 | `slot-type`, `tail-first-entry`, `props14-size-words`, `text-cap` |
| R36G0c-6 | 10 | `stamp-zero`, `stamp-ones`, `stamp-new-row`, `stamp-props-word`, `stamp-varkey`, `filetime-lower`, `filetime-upper`, `lpstr-text-nul`, `lpstr-text-control`, `derived-summary` |
| R36G0c-7 | 1 | `control-mask-count` |
| R36G0c-8 | 1 | `text-c-category` |

(`python tools/trace/test_gate_guards_meta.py --list` prints the same 61 with what each removes.)
