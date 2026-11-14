# Wave 36 G0e: the builder's gates

Builder Claude Sonnet 5.5, 15 November 2026, branch `wave36`.  G0e is the fourth fix of the trace gate (`tools/trace/trace_gate.py`, `tools/trace/test_trace_gate.py`,
`tools/trace/test_gate_guards_meta.py`), from the G0d review (`review-g0d.md`, R36G0d-1 to -6).  Logs are under `build/w36/` (git-ignored), each named for the parent commit of the item
it gates.  The builder used no COM and did not touch the VM, `build/trace-2021/` or `build/r7/`.  G0e changes no C++ source and no `CMakeLists.txt`.

## Acceptance counts

The committed ctest `trace_gate_guards` (`tools/trace/test_trace_gate.py`, then `tools/trace/test_gate_guards_meta.py`).

| sweep | TRACED | REFERENCE | at G0d (`f48beba`) | at G0e (`88488a1`) |
|---|---|---|---|---|
| negative | the control's null save | the pair's edit | 191 edits, 191 FAIL, 0 PASS | 191 edits, **191 FAIL**, 0 PASS |
| positive | the control's null save | the control | 202 control-nulls, 146 PASS, 56 OPEN | 202 control-nulls, **146 PASS, 56 OPEN** |

(`build/w36/ebbbb9a-W36-0e.3-green.log`: `W36-0c.1 positive: 202 control-nulls, 146 PASS, 56 OPEN; negative: 191 edits, 191 FAIL, 0 PASS`.)  Everything G0d accepted stays green: the
491 checks of G0d are all in the test, which is now **652 checks, 0 FAIL** (G0d: 491).  Every hand-built wrong file of `review-g0d.md` FAILs: R36G0d-1 (the four clauses removed by hand:
the Props14 tail count word, the SummaryInformation section size, the first value after the offset table, the LastSave type word), R36G0d-2 (the six files: a stamp of 1, a stamp of
0x7fffffff, both save words 1, the two save words unequal, a LastSave in 2600, a LastSave in 2001, each on both sides) and R36G0d-3 (both tail names emptied, each alone, the lone one against
the reference's text, in both directions).

## The meta-test (W36-0e.1, grown by 0e.2 and 0e.3)

`tools/trace/test_gate_guards_meta.py` now has two sources of removals: the **named guards** (a list, each with the finding it answers and the text it removes) and the **derived
candidates** (`ast` over `trace_gate.py`: every `if` condition taken False or True, every operand of an `or` taken False and of an `and` taken True, every conditional expression both ways).
A candidate may stay green only on `REDUNDANT`, as `key -> (cover, reason)`; the cover must itself be red, and an entry that is not derived, is red itself or has a cover that is not red is a
stale error.  A removal that crashes the test with no FAIL line is a CRASH and fails the run; the test turns a gate that raises, or returns the wrong shape, into a named FAIL, so no removal
is red by a crash.  The last full run, the plain default run on `e95c295` (`build/w36/ebbbb9a-W36-0e.3-meta.log`; `88488a1` changes one description only):

`named guards listed 67, red 67, not red 0` and `candidates derived 214 (run 214): removed red 202 (61 of them by a case that asserts the gate does not raise), listed redundant 12, green 0, crashed 0`,
`the unmodified gate: passes`, `exit 0`.

| commit | named | derived | red | redundant | green | crashed |
|---|---|---|---|---|---|---|
| `0c0563e` (0e.1) | 65 of 65 | 208 | 196 | 12 | 0 | 0 |
| `ebbbb9a` (0e.2) | 66 of 66 | 212 | 200 | 12 | 0 | 0 |
| `e95c295` (0e.3) | 67 of 67 | 214 | 202 | 12 | 0 | 0 |

* The first derived run (0e.1) found 214 candidates, 137 red, 76 green and 1 crash.  Triage: new cases for the semantic and raise-protective guards, three dead clauses deleted from the gate,
  one `ValueError` path made a verdict, 12 equivalent clauses on `REDUNDANT` with a red cover and a reason each.
* The first full runs of 0e.2 and 0e.3 found guards green or NOFIT, each turned red before the commit: at 0e.2 one named guard (`new-row-other`) and three candidates (the span made a row the
  control has verdict-neutral); at 0e.3 three named guards NOFIT (their removal text named `_text_ok`, renamed `_name_ok`) and `tail-ffff-dropped` green (the new derived flag caught its old
  case on its own; a case of two different junk names in the 0xffffffff entry holds it).
* The wall time of the full default run is about 25 minutes with 8 jobs (G0d: 8 minutes at 61 guards); the ctest `TIMEOUT` is 3600 s, unchanged.  `--no-meta` skips it.  `--only named` and
  `--only derived` print a partial table and cannot check `REDUNDANT` (a stale check needs both); a full run is the acceptance.

## Per commit

| item | id | commit | parent (log prefix) | red | green |
|---|---|---|---|---|---|
| W36-0e.1 | R36G0d-1 | `0c0563e` | `1aaaa2e-W36-0e.1-*` | the four new guards against the unchanged test: 0 of 4 red | 588 PASS; 65 of 65 named red, 208 derived (196 red, 12 redundant, 0 green, 0 crashed) |
| W36-0e.2 | R36G0d-2 | `ebbbb9a` | `0c0563e-W36-0e.2-*` | 42 FAIL, 602 PASS (the span, the save words, the six review files) | 641 PASS; 66 of 66 named red, 212 derived (200 red, 12 redundant, 0 green, 0 crashed) |
| W36-0e.3 | R36G0d-3 | `e95c295` | `ebbbb9a-W36-0e.3-*` | 8 FAIL, 644 PASS (the emptied-name cases, both directions) | 652 PASS; 67 of 67 named red, 214 derived (202 red, 12 redundant, 0 green, 0 crashed) |
| W36-0e.4 | R36G0d-4 | `88488a1` | `e95c295-W36-0e.4-*` | none: one description (the crash half is 0e.1) | `mut_one.py` on `control-mask-count`: 1 FAIL (W36-0d.7) |

Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each carries both
trailers.  The sweeps (202 control-nulls, 146 PASS, 56 OPEN; 191 edits, 191 FAIL, 0 PASS) were unchanged at every commit.

## The guards added (by finding)

| finding | guards | ids |
|---|---|---|
| R36G0d-1 | 4 | `tail-count`, `si-section-size`, `si-first-value`, `si-lastsave-type` |
| R36G0d-2 | 5 (3 new, 2 replacing the old FILETIME pair) | `stamp-lower`, `stamp-upper`, `save-words-equal`, `filetime-lower`, `filetime-upper` |
| R36G0d-3 | 1 | `tail-name-empty` |

(`python tools/trace/test_gate_guards_meta.py --list` prints the 67 named guards and the 214 derived candidates with what each removes.)  The value spans of R36G0d-2 are in
`spec/writers/evidence/trace-gate-fields-g0b.md` (Value bounds), with the probe (`build/w36/g0e_probe.py`).

## The cost of the spans

A masked value is accepted only inside the span of the 624 committed files (stamps 0x3d141482 to 0x3d282db4, LastSave 22 October to 11 November 2026).  A capture made after the newest
committed save lies outside both spans and FAILs as data until the two spans, the test's copy and the evidence table are widened together.  The coordinator is re-gating the recordings:
a verdict that turns at the re-gate is this, and it shows as a masked stamp or a LastSave outside the span.
