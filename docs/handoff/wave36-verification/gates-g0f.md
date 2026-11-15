# Wave 36 G0f: the builder's gates

Builder Claude Sonnet 5.5, 15 November 2026, branch `wave36`.  G0f is the fifth fix of the trace gate (`tools/trace/trace_gate.py`, `tools/trace/test_trace_gate.py`,
`tools/trace/test_gate_guards_meta.py`): the machine and session fields the first re-gate of the VM's recordings found (`build/trace-2021/regate/DOSSIER.md`, CAUSES.md), each masked as a
value a save writes and on both sides, and nothing else.  Logs are under `build/w36/` (git-ignored), each named for the parent commit of the item it gates.  The builder used no COM and did not
touch the VM, `build/trace-2021/` or `build/r7/`.  G0f changes no C++ source and no `CMakeLists.txt`.

## Acceptance counts

The committed ctest `trace_gate_guards` (`tools/trace/test_trace_gate.py`, then `tools/trace/test_gate_guards_meta.py`).

| sweep | TRACED | REFERENCE | at G0e (`88488a1`) | at G0f (W36-0f.6) |
|---|---|---|---|---|
| negative | the control's null save | the pair's edit | 191 edits, 191 FAIL, 0 PASS | 191 edits, **181 FAIL, 10 PASS** (all 10 pinned: 5 `NOOP_NULL`, 5 `NOOP_EDITS`) |
| positive | the control's null save | the control | 202 control-nulls, 146 PASS, 56 OPEN | 202 control-nulls, **180 PASS, 22 OPEN** (107 rows pinned) |

(`build/w36/a090ed9-W36-0f.6-green.full`: `W36-0c.1 positive: 202 control-nulls, 180 PASS, 22 OPEN; negative: 191 edits, 181 FAIL, 10 PASS`.)  The ten negative PASSes are edits that equal
their null (five, W36-0f.1: the Props14 flag 0x3540000a is a bounded session field) or their control (five, W36-0b.2, 0f.4.2 and 0f.4.5) in every byte the gate checks; the test pins each by
name and shows that it equals its control, so a new PASS anywhere else turns the negative check red.  Every positive pair that PASSes now passes by a named field, not by an excuse: the
control mask excuses nothing (W36-0c.5), and the 22 OPEN pairs are pinned as (pair, stream, count).  Everything G0e accepted stays green: the 652 checks of G0e are all in the test, which is
now **1739 checks, 0 FAIL** (G0e: 652).  Every hand-built wrong file of `review-g0b.md`, `review-g0c.md` and `review-g0d.md` still FAILs (those cases are in the test: the seven
inverted-byte files, the four clauses removed by hand, the six span files and the emptied tail names); the test is green with all of them.

## The meta-test at the tip

The full default run (the test, then the meta-test, one process) now takes more than two hours on this machine with eleven jobs, so the tip was run as two partial runs on `2922b56`, which hold every removal but
cannot check `REDUNDANT` for a stale entry (a partial run raises a `KeyError` at that loop after the table is printed; G0e recorded this).  The ctest `TIMEOUT` of 3600 s is exceeded by the full run.

* derived (`build/w36/2922b56-W36-0f.5-meta-derived-final.log`): `candidates derived 299 (run 299): removed red 283 (85 of them by a case that asserts the gate does not raise), listed redundant 16, green 0,
  crashed 0`, `the unmodified gate: passes`.  The 16 redundant are the 12 of G0e and four added at 0f.5, each green with a red cover.
* named (`build/w36/2922b56-W36-0f.5-meta-named-final.log`): 182 of 184 red, 2 NOFIT: `derived-cedl-path` (its removal text ended in an `else:` that 0f.4.1 replaced) and `derived-view-size` (its text
  occurred twice after 0f.1).  Repaired at `217f3e6` by anchoring each on the neighbouring line; both red as picks (`2922b56-W36-0f.5b-picks.log`), test 1739 PASS.

The per-commit named runs saw every named guard of its own item red (counts in the table below).

## Per commit

| item | commit | parent (log prefix) | red | green |
|---|---|---|---|---|
| W36-0f.1 | `643d1c4` | `f1b86da-W36-0f.1-*` | 13 FAIL, 669 PASS | 692 PASS; 16 named red |
| W36-0f.2 | `64565cf` | `643d1c4-W36-0f.2-*` | 20 FAIL, 732 PASS | 752 PASS; 10 named red |
| W36-0f.3 | `58ed375` | `64565cf-W36-0f.3-*` | 21 FAIL, 866 PASS | 896 PASS; 9 named red |
| W36-0f.4.1 | `41a6be3` | `58ed375-W36-0f.4.1-*` | 33 FAIL, 1010 PASS | 1043 PASS; 8 named red |
| W36-0f.4.2 | `ccad382` | `41a6be3-W36-0f.4.2-*` | 39 FAIL, 1221 PASS | 1263 PASS; 22 named red |
| W36-0f.4.3 | `96eca67` | `ccad382-W36-0f.4.3-*` | 12 FAIL, 1259 PASS | 1271 PASS; 3 named red |
| W36-0f.4.4 | `f26da8f` | `96eca67-W36-0f.4.4-*` | 13 FAIL, 1292 PASS | 1315 PASS; 11 named red |
| W36-0f.4.5 | `d2227a1` | `f26da8f-W36-0f.4.5-*` | 10 FAIL, 1323 PASS | 1333 PASS; 5 named red |
| W36-0f.4.6 | `a81fe68` | `d2227a1-W36-0f.4.6-*` | 7 FAIL, 1331 PASS | 1338 PASS; 3 named red |
| W36-0f.4.7 | `cf3d094` | `a81fe68-W36-0f.4.7-*` | 9 FAIL, 1362 PASS | 1377 PASS; 9 named red |
| W36-0f.4.8 | `64c173a` | `cf3d094-W36-0f.4.8-*` | 19 FAIL, 1464 PASS | 1489 PASS; 30 named red |
| W36-0f.4.9 | `ed3d859` | `64c173a-W36-0f.4.9-*` | 24 new FAIL against a gate that masks the three streams whole (47 in all) | 1519 PASS; no clause added, no guard |
| W36-0f.4b | `11875ed` | `ed3d859-W36-0f.4b-*` | 32 FAIL, 1620 PASS | 1652 PASS; 11 named red |
| W36-0f.5 | `a090ed9` | `11875ed-W36-0f.5-*` | the 15 removals that were green, run against the new test with their two covers (17): 13 red, 4 listed redundant and green (each with a red cover), 0 green unlisted | 1665 PASS; no gate change, no named guard added |
| W36-0f.6 | `2922b56` | `a090ed9-W36-0f.6-*` | the 22 field cases against a gate that sets the traced stream of the nine streams to the reference's: 22 of 22 FAIL | 1739 PASS, 0 FAIL |
| W36-0f.5 (again) | `217f3e6` | `2922b56-W36-0f.5b-*` | the tip's named run: 2 of 184 NOFIT | both red as picks; 1739 PASS, 0 FAIL |

Every commit: `check_fixtures` 0, `check_markers` 0, `guard_names` 0, `scan_client_names` 0, the `client-string-[0-9a-f]{8}` grep none, `git diff --check` clean; LF only; each carries both
trailers.

## The data targets (W36-0f.6)

The DOSSIER's 13 "data" targets are `wave30-inactivate-second`, `wave31-usage`, `wave35-viewstate-sort-flat-name-desc`, `wave35-g3-font-bold-range`, the three assign-effort targets
(`-off`, `-on`, `dsi-total-work`), the four table edits and the two interim copies.  For each, the test builds a file from the committed reference with the DOSSIER's field set to the VM's
recorded value (22 field cases), asserts the reference's bytes first, and runs the command line against the pair's own control and null: every one FAILs in the stream of its field.  The
reference against itself is clean in each pair, so the FAIL is the field.  For the streams with a canonical form (CEdl and view-state Var2Data) the canonical form differs as well, so the
field is not masked.  The two slot lists of `wave30-inactivate-second` and `wave31-usage` are in OPEN pairs (the control and its null differ in size), so the gate's FAIL there is also the
pair's own; the canonical form is what shows the uids compared.  Offsets are the `ro` and `runs` of `build/trace-2021/regate/r11/<target>.txt`, read only.

## The guards added

The named guards of the meta-test went from 67 (G0e) to 184 (G0f): 117 added across the items above (`python tools/trace/test_gate_guards_meta.py --list` prints them and the derived
candidates).  The derived candidates went from 214 to 299.
