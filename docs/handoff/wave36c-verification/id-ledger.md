# Id ledger: coordinator work orders of 11 and 15 November 2026

Checked on 22 Nov 2026 at `main` = `origin/main` = `wave36c` = 2052d36 (980 commits). Commit ids are main's rewritten ids, matched by subject. Gate and trace notes that left the public tree are read from the private notes repo (`../msproject-mac-trace/evidence`). Read-only: nothing in any repo was changed.

Abbreviations: WC = `docs/handoff/wave36-complete.md`; W36V = `docs/handoff/wave36-verification/`; TRC = `build/trace-2021/`; PLAN = `docs/handoff/plan-waves27-30.md`; SH = `docs/START-HERE.md`; CM = `memory/coordinator.md`.

| id | order | status | evidence |
|---|---|---|---|
| A-1 | 11 Nov | DONE | `TRC/d1-export-report.txt` last line "D-1 done: 25 match, 0 problems"; refreshed 15 Nov (see C-3) |
| A-2 | 11 Nov | FIXED | 382e381 W36-0.1 (A-2): the trace gate's noise allow-list moves into trace_gate.py; `tools/trace/trace_gate.py`; WC:12, red/green WC:33 |
| A-3 | 11 Nov | FIXED | 68d6036 R7 fixtures: control-nulls for the ten pairs that had none (A-3); 35f1b71 W36-0.3 (A-3): an absent control null is a FAIL named "no null"; `tests/data/*-control-null.mpp` (202) |
| A-4 | 11 Nov | FIXED | 4592295 W36-0.2 (A-4): no wrong-length mask in the trace gate; WC:13, WC:34 |
| A-5 | 11 Nov | DONE | Records: private `evidence/trace-captures-r5.md:23` marks wave30-inactivate-second, wave30-level and the notes 6-byte run OPEN. VM half partial (`TRC/PROGRESS.md:67` items 5-7 PARTIAL). Both pairs still FAIL at the re-gate (`W36V/regate-w5/regate-w5.csv`: R36W5-4, R36W5-10), not queued in PLAN (Gap 6) |
| A-6 | 11 Nov | FIXED | d16efce W36-0.4 (A-6): a ctest flips one byte in each stream class of a committed pair and asserts FAIL; `tools/trace/test_trace_gate.py`; WC:15, WC:36 |
| A-7 | 11 Nov | FIXED | adb9587 W36-1.5 (D-2): wave35-g3-font-bold-range is a one-row Bold, not a range; 67172a6 R7 fixtures: the six font pairs (range2 with `selection_tasks_count` 2 in `build/r7/font-run.log`); `W36V/regate-w5/gate-wave36-r7-font-range2.txt` PASS |
| A-8 | 11 Nov | DONE | PLAN:491-494 "Queued from wave 35g (CLOSE): no wave 35h"; 0b79283 wave 35g verification ... merged into main. Later fix-only waves 36b/36c: Gap 9 |
| A-9 | 11 Nov | FIXED | 0251b22 W36-0.6 (A-9): START-HERE under 8 KB, the wave history moved, memory/coordinator.md current; N-6 texts in private `evidence/trace-gates-r6c-n6-g1-5c-assignadd.txt`. Both files stale again: Gap 1 |
| A-10 | 11 Nov | DONE | `docs/handoff/wave35g-verification/coord-gates-close.md:33,65` "10 pairs: 7 checked and passing, 3 not checked" |
| A-11 | 11 Nov | FIXED | 5d300ba W36-0.5 (A-11): Skip for now only in a Sign in dialog; vmx throws on a MISMATCH and a wrong reply length; `tools/trace/capture/r5b-skip.ps1`, `tools/trace/kvp/host.ps1`; WC:16, WC:37 |
| A-12 | 11 Nov | DONE | `TRC/PROGRESS.md:59` guest clock agrees with host within a second; `:63` cutoff written as 18:00 Fri 13 Nov 2026; `:69` dialog "pass ends on Sunday, 15 November 2026" |
| N-1 | 11 Nov | DONE | `TRC/PROGRESS.md:59,63,69` (guest Get-Date, dialog date, host time, absolute cutoff) |
| N-2 | 11 Nov | DONE | 0b79283 wave 35g verification: gates, mutations, CI green on all seven jobs, reviews 0 Critical / 0 High / 0 Medium, 4(e) 7 checked 3 not checked; merged into main; Mediums and Lows in PLAN:491 |
| N-3 | 11 Nov | DONE | 263cc50 TRACE (N-3, N-6): re-gate the 17 view-state recordings (now an empty commit after the history rewrite); 17 gate texts in private `evidence/trace-gates-r6c-n3-viewstate.txt` |
| N-4 | 11 Nov | DONE | `TRC/PROGRESS.md:69,80,91` (round 7 recorded 96 captures; 8 round 8 items in r8-notrecorded.txt); fixtures 68d6036, 67172a6, 727968c, 62f71af, fda280c, 53a5388; build mismatch (host 20527 vs VM 20430) asked and answered in `TRC/ROUND7.md:5` |
| N-5 | 11 Nov | DONE | 393bd41 W36: the wave 36 brief (the writer wave ...); `docs/handoff/wave36-brief.md` |
| N-6 | 11 Nov | DONE | Private `evidence/trace-captures-r5.md:15,21,23,26` (PASS is not a gate result; viewstate nulls exist; two pairs OPEN); gate texts `trace-gates-r6c-n6-g1-5c-assignadd.txt` |
| W36-0.1 | 11 Nov | FIXED | 382e381 W36-0.1 (A-2): the trace gate's noise allow-list moves into trace_gate.py; marker WAVE-36-G0-COMPLETE WC:61 (34e806c) |
| W36-0.2 | 11 Nov | FIXED | 4592295 W36-0.2 (A-4): no wrong-length mask in the trace gate |
| W36-0.3 | 11 Nov | FIXED | 35f1b71 W36-0.3 (A-3): an absent control null is a FAIL named "no null" |
| W36-0.4 | 11 Nov | FIXED | d16efce W36-0.4 (A-6): a ctest flips one byte in each stream class ... |
| W36-0.5 | 11 Nov | FIXED | 5d300ba W36-0.5 (A-11): Skip for now only in a Sign in dialog ... |
| W36-0.6 | 11 Nov | FIXED | 0251b22 W36-0.6 (A-9): START-HERE under 8 KB ... |
| D-1 | 11 Nov | DONE | `TRC/d1-export-report.txt` (25 match, 0 problems); 15 Nov export `TRC/d1-export-report-2026-11-15.txt` |
| D-2 | 11 Nov | FIXED | adb9587 W36-1.5 (D-2): wave35-g3-font-bold-range is a one-row Bold; fixture untouched (WC:73) |
| R35fG1-1 | 11 Nov | FIXED | 86c3b98 W35g-0.1 (R35fG1-1): Custom Fields... opens beside a queued exclusive edit |
| R35fG0-1 | 11 Nov | FIXED | ee4b9f8 W35g-0.2 (R35fG0-1): the 1.1 record's bypass (b) on the five-task file, corrected and tested |
| R35fG0-2 | 11 Nov | FIXED | 751f83a W35g-1.9 (R35fG0-2): the 35c, 35d and 35e records stop calling closed ids "not yet closed" |
| R35fG0-3 | 11 Nov | FIXED | d59ce22 W35g-1.10 (R35fG0-3): the G0 record says 0.1 builds the edit |
| R35fG0-4 | 11 Nov | FIXED | c29115b W35g-1.4 (R35fG0-4): clearing the sort says a Level still shown is session only |
| R35fG0-5 | 11 Nov | FIXED | 738662a W35g-1.1 (R35fG0-5): 0.1's read-only case tested in qt_outline_persist |
| R35fG0-6 | 11 Nov | FIXED | adb20e0 W35g-1.2 (R35fG0-6): 0.3's test asserts that the session Level is shown |
| R35fG0-7 | 11 Nov | FIXED | 9e3f123 W35g-1.3 (R35fG0-7): 0.2's status needle tells the Custom Fields row from the row before it |
| R35fG0-8 | 11 Nov | FIXED | e4a2c01 W35g-1.11 (R35fG0-8): view-state.md's test list names 0.3's five-task case |
| R35fG0-9 | 11 Nov | FIXED | 0a5232f and 3f4dc7f W35g-1.14, 1.15 (R35fG0-9): over-long lines rewrapped |
| R35fG1-2 | 11 Nov | FIXED | 80e111d W35g-1.5 (R35fG1-2): a document-edit refusal beside a Sort or Show Level advises a step it accepts |
| R35fG1-3 | 11 Nov | FIXED | d549c11 W35g-1.8 (R35fG1-3): the untested parts of 1.8 get tests |
| R35fG1-4 | 11 Nov | FIXED | 8567fc8 W35g-1.6 (R35fG1-4): the partial-apply status counts the fields |
| R35fG1-5 | 11 Nov | FIXED | fa9dc63 W35g-1.16 (R35fG1-5): the new test lines over 100 columns are rewrapped |
| R35fW-1 | 11 Nov | FIXED | 06c6078 W35g-1.12 (R35fW-1): the three state files stop describing wave 35f G0 and G1 as in progress |
| R35fW-2 | 11 Nov | FIXED | 623ae8f W35g-1.13 (R35fW-2): the 2.1 row of wave35e-complete.md says which routes clear the redo list |
| R35fW-3 | 11 Nov | FIXED | af30d13 W35g-1.7 (R35fW-3): the partial-apply status says a dropped Percent or custom-field change was not queued |
| R35cG0-4 | 11 Nov | FIXED | afdc3f2 W36-2.3 (plan Props digest) ... (closes R35cG0-4); `W36V/review-g2.md:56` digest mutation red. SH and CM still list it open: Gap 2 |
| R35cG0-2 | 11 Nov | QUEUED | PLAN "Queued from wave 35g (CLOSE): no wave 35h" (line 509-510, "Revisit at wave 37 planning") |
| R35bG0-9 | 11 Nov | QUEUED | PLAN "Queued from wave 35g (CLOSE): no wave 35h" (line 509-510) |
| R35bG1-10 | 11 Nov | QUEUED | PLAN "Queued from wave 35g (CLOSE): no wave 35h" (line 509-510) |
| R35bG2-9 | 11 Nov | QUEUED | PLAN "Queued from wave 35g (CLOSE): no wave 35h" (line 509-510) |
| M8-1 (every guard reproduced red by the coordinator) | 11 Nov s.8 | DONE | `W36V/coord-close-c123.md` (C.1-C.3); `wave36b-verification/coord-verify.md` s.2 (only 0.1 and R36W-1 re-planted, "the other rows ... not re-planted"); `wave36c-verification/coord-verify.md` re-red m1-m6 (m6 stayed green: R36cG0-2). Partial: Gap 10 |
| M8-2 (109 recordings plus round 7 re-gated, counts, gate texts committed) | 11 Nov s.8 | DONE | b5746d9 W36 (W-5): the committed re-gate, 250 targets, 198 PASS 46 FAIL 0 OPEN 0 no-null 6 missing copy; `W36V/regate-w5/README.md:7`, csv adds to 250 |
| M8-3a (CI green on seven jobs) | 11 Nov s.8 | NO EVIDENCE | Looked in `wave36b-verification/coord-verify.md` s.5 (first run: 3 jobs failed), `wave36c-verification/coord-verify.md` ("Merge when CI is green"), SH, CM, git log after 2052d36. No record of a green run on the merged tip: Gap 3 |
| M8-3b (0 Critical, 0 High) | 11 Nov s.8 | DONE | `wave36c-verification/coord-verify.md` "Merge decision": 0 Critical, 0 High open; `W36V/review-wave.md` and `wave36b-verification/review-g0.md` list the earlier ones, all fixed or queued |
| M8-3c (4(e) with checked and not-checked counts) | 11 Nov s.8 | DONE | `W36V/coord-4e-g2.md` (0 of 9 Props inserts pass; refusal restored, WC:787-799). Font 4(e) "planned, not run" (WC:171-172): Gap 7 |
| M8-4 (every section 9 id ticked) | 11 Nov s.8 | NO EVIDENCE | Looked in WC, `wave36b-complete.md`, `wave36c-complete.md`, SH, CM: no tick table for A-1 to A-12 and N-1 to N-6 at wave 36 close (wave 35g has one, `wave35g-complete.md:134-139`). This ledger is the first |
| M8-5 (START-HERE under 8 KB and true at the merge commit) | 11 Nov s.8 | NO EVIDENCE | Size holds (8143 bytes), but SH:12 says `main` = b2c0b19 and SH:28 "Nothing is pushed" while main = origin/main = 2052d36: Gap 1 |
| C-1 | 15 Nov | DONE | `TRC/triage/TRIAGE.md` (34 rows labelled), `TRC/triage/V3.md`; re-gate W36V/regate-w5/README.md |
| C-2 | 15 Nov | DONE | `TRC/saved/INVENTORY.txt:2-3` "guest files 563; host files 563; sha and size equal 563; not equal or missing 0" |
| C-3 | 15 Nov | DONE | `TRC/d1-export-report-2026-11-15.txt` last line "54 of 54 match, 0 problems after the re-copy" (one CRC read error on E:, re-copied; the unreadable copy kept as .bad) |
| C-4 | 15 Nov | FIXED | 464b742 W36-2.0 (span widening): the gate's stamp, view-stamp and LastSave spans end at the decoded maximum of 1187 saved copies; prediction confirmed `TRC/triage/TRIAGE.md:5` ("107 FAILs are the span alone") |
| C-5 | 15 Nov | DONE | a40fa3e W36-0f.close (marker) then 7ad453a W36: G2 gains the span widening first ... G0f is the last gate group; 83eb991 W36-C.5 queued the Mediums and Lows. Later fix-only waves: Gap 9 |
| C-6 | 15 Nov | NO EVIDENCE | Looked in `W36V/gates-close.md:52-62`, WC:733-736, `build/r7/`, `TRC/`. Only a document re-check ("no note ... checks the four-pair rule against them"); no byte-for-byte test of the round 7 insert pairs: Gap 5 |
| C-7 | 15 Nov | FIXED | 5caad6f W36-2.4 (meta-test split): trace_gate_meta is its own labelled ctest, timed at 75 minutes; 8c4a0da W36b-0.6 (R36W-6) split into two ctests, 36c eb781b5 into four shards |
| C-8 | 15 Nov | DONE | Ten bundles on `E:` (`msproject-mac-wave36-2026-11-15.bundle` to `...wave36c-2026-11-22-close.bundle`); `git bundle verify` run now on the last one: "records a complete history" |
| C-9 | 15 Nov | FIXED | f598cda W36 (D-8): the trace notes leave the public tip; the private repo holds them; history rewrite 22 Nov (`wave36b-verification/coord-verify.md` s.4); brief line 108 path now intact |
| C-10 | 15 Nov | DONE | db11ce9 W36: CLOSE opens with the G0f review's findings ...; the G0e and G0f reviews (adds `W36V/review-g0e.md`); local main = origin/main. SH and CM stale again: Gap 1 |
| C-11 | 15 Nov | DONE | `TRC/d1-export-report-2026-11-15.txt:2` VM stopped (Off) 10:02:42; the free-memory figure after was not recorded |
| V-1 | 15 Nov | DONE | `TRC/saved/INVENTORY.txt:2-4` (563 equal; 231 of 243 with copy; 6 of 12 no-copy found now) |
| V-2 | 15 Nov | DONE | `TRC/triage/TRIAGE.md` (34 rows: PASS, span, host, VM, undecided) and 260 files under `TRC/triage/` |
| V-3 | 15 Nov | DONE | `TRC/triage/V3.md` (host remakes not possible; VM re-recording "none"; pass dialog not opened). No `PASS ENDED` line needed or written |
| V-4 | 15 Nov | DONE | `TRC/d1-export-report-2026-11-15.txt` (export 10:02-12:18, after the last recording at 09:21; 0 problems after re-copy) |
| V-5 | 15 Nov | DONE | Bundles on `E:` at the G0f (15 Nov 12:22), pause, G2, CLOSE, 36b and 36c markers; verify run (see C-8) |
| W-1 | 15 Nov | DONE | a40fa3e (G0f marker) then 7ad453a, 464b742 (G2 first item) before the G0f review's CLOSE fixes |
| W-2 | 15 Nov | DONE | 83eb991 W36-C.5 (R36G0f-4 to -7, -9): queued, not fixed; Critical/High as CLOSE items 4dd52e9 (C.1), 3e18ed2 (C.2). Gap 9 for 36b |
| W-3 | 15 Nov | FIXED | 464b742 W36-2.0 (span widening) |
| W-4 | 15 Nov | NO EVIDENCE | As C-6 |
| W-5 | 15 Nov | DONE | b5746d9 W36 (W-5): the committed re-gate, 250 targets, 198 PASS 46 FAIL 0 OPEN 0 no-null 6 missing copy; each FAIL has an R36W5 id in the README |
| W-6 | 15 Nov | FIXED | 5caad6f W36-2.4 (meta-test split) (75 min recorded, WC:663, 4528 s) |
| W-7 | 15 Nov | DONE | review-g0e.md committed in db11ce9; coordinator memory and START-HERE rewritten (26d3ed2, 93a5f3b); main fast-forwarded. Stale again: Gap 1 |
| D-6 | 15 Nov | DONE | `TRC/triage/V3.md` last line: the 14 undecided "stand as findings (D-6 default)" |
| D-7 | 15 Nov | NO EVIDENCE | Looked in SH, CM, `TRC/triage/`, `TRC/NEXT-ROUND.md`, `TRC/REGATE.md`, W36V: no decision recorded: Gap 8 |
| D-8 | 15 Nov | FIXED | f598cda W36 (D-8): the trace notes leave the public tip; history rewrite on the operator's word, `wave36b-verification/coord-verify.md` s.4 |
| R35cG0-4 | 15 Nov s.7 | FIXED | afdc3f2 W36-2.3 (plan Props digest) ... (closes R35cG0-4) |
| R36G0b-8 | 15 Nov s.7 | NO EVIDENCE | Not in PLAN (grep). Recorded "open" in WC:406, WC:489, WC:781 only; `W36V/review-g0c.md:77`. The re-gate (b5746d9) ran real recordings but no record closes it: Gap 4 |
| R36G0d-5 | 15 Nov s.7 | NO EVIDENCE | Not in PLAN. "Queued (Low, not fixed)" in WC:542 and `W36V/review-g0e.md:21` only: Gap 4 |
| R36G0e-1 | 15 Nov s.7 | NO EVIDENCE | `W36V/review-g0e.md:132` (Medium); not in PLAN, WC, SH or CM: Gap 4 |
| R36G0e-2 | 15 Nov s.7 | NO EVIDENCE | `W36V/review-g0e.md:152`; not in PLAN: Gap 4 |
| R36G0e-3 | 15 Nov s.7 | NO EVIDENCE | `W36V/review-g0e.md:158`; not in PLAN: Gap 4 |
| R36G0e-4 | 15 Nov s.7 | NO EVIDENCE | `W36V/review-g0e.md:162`; not in PLAN: Gap 4 |
| R36G0e-5 | 15 Nov s.7 | NO EVIDENCE | `W36V/review-g0e.md:166`; not in PLAN: Gap 4 |
| R36G0e-6 | 15 Nov s.7 | FIXED | 464b742 W36-2.0 (span widening) (inferred: the record never names the id) |
| the 56 OPEN pairs | 15 Nov s.7 | NO EVIDENCE | Not in PLAN. Now 22 OPEN, pinned (WC:593, SH:34); the re-gate's 46 FAILs (R36W5-1 to -11) also not in PLAN: Gap 4 |
| G6-1 (11 Nov section 8 stands) | 15 Nov s.6 | DONE | See rows M8-1 to M8-5 |
| G6-2 (V-1 inventory adds up; committed re-gate ran from those copies) | 15 Nov s.6 | DONE | `TRC/saved/INVENTORY.txt:2`; `W36V/regate-w5/README.md:3-4` (563 saved copies) |
| G6-3 (V-4 report 0 problems, dated after the last recording) | 15 Nov s.6 | DONE | `TRC/d1-export-report-2026-11-15.txt` last line (12:18, last recording 09:21) |
| G6-4 (G2 lift held to the three-part rule, held-out Props pairs named) | 15 Nov s.6 | DONE | WC:665-668 names the held-out pairs; rule (c) failed at 4(e) (`W36V/coord-4e-g2.md`), so "not lifted" (WC:787-799, 9e5fb70-equivalent caa69f8 W36b-0.1) |
| G6-5 (every id ticked) | 15 Nov s.6 | NO EVIDENCE | As M8-4 |

## Gaps

1. **START-HERE and coordinator memory are stale at the merged tip.** SH:4,12,28 and CM:10,14 say main = b2c0b19 and "nothing is pushed". Git: main = origin/main = wave36c = 2052d36. Fails 11 Nov s.8 "true at the merge commit" (M8-5) and W-7 (C-10).
2. **R35cG0-4** is closed by afdc3f2 (G2 review mutated it red) but CM:62, SH (open list) and PLAN:509 still list it as "partly fixed or open".
3. **M8-3a: no CI result recorded for the merged tip.** The only recorded run (wave36b) had 3 failing jobs (R36bCI-1 to -3); wave36c claims to fix them and says "merge when CI is green", yet main now holds wave36c and no green run is on record.
4. **Ids the 15 Nov order puts in PLAN are not there:** R36G0b-8, R36G0d-5, R36G0e-1 to -5, the 56 OPEN pairs (now 22), and the re-gate's R36W5-1 to -11. PLAN's last wave-36 sections cover only R36G0f-4 to -9, R36W-7 to -10, R36bG0-1 to -7, R36cG0/R36cC.
5. **C-6 / W-4: the round 7 insert pairs were never tested against the rule.** WC:733-736 records "G3 not started"; no agent or note runs the byte-for-byte check the order asked for.
6. **A-5 residual:** wave30-inactivate-second and wave30-level still FAIL at the re-gate (R36W5-4, R36W5-10, "undecided"); not queued in PLAN.
7. **Font 4(e)** "planned, not run" (WC:171-172); no later record runs it, so the six font pairs stay "not checked" (A-10 / Q-3 territory).
8. **D-7** (no new recording without a licence): no decision on record.
9. **A-8 / C-5 / W-2 versus the later waves:** wave 36b was a fix-only wave that included three Mediums (R36W-4 to -6, `wave36b-complete.md:12-17`) and wave 36c a second fix-only wave, against standing rule 5 and W-2 ("queue Mediums and Lows"). A record-versus-order conflict, not a missing id.
10. **M8-1 partial:** `wave36b-verification/coord-verify.md` s.2 re-planted two guards and "read" the rest; m6 in wave 36c's re-red stayed green (R36cG0-2).
11. **Minor record conflicts:** WC:167 says no round 7 font pair is committed under `tests/data` (true at 1fa7338; merge 475fa42 then added them); TRIAGE counts 14 undecided where the order says six; N-3's commit 263cc50 is empty on main (texts only in the private repo); the first export copy of one data disk had a CRC read error on E: (re-copied, .bad kept).

## After the merge (coordinator, 22 November 2026)

Gaps 1 to 3 are closed by the commit that adds this file: START-HERE and `memory/coordinator.md` say `main` =
`2052d36`, R35cG0-4 is off the open lists, and the CI run on `2052d36` (below and in `coord-verify.md`) is green on all
12 jobs.  Gaps 4 to 8 and 10 are queued in `plan-waves27-30.md` ("Queued at the wave 36 merge").  Gap 9: wave 36c was
fix-only because the merge rule (CI green) required it; wave 36b fixed the wave 36 review's Highs.
