# Independent whole-wave review, wave 36 (b2c0b19..efe9061)

Worktree D:\dev\msproject-mac-wt-revw36 (detached efe9061, clean at the end: every mutation reverted, `git status` empty). Scratch: build/rev/ (excused.py, wrong.py, mut_gate.py, mut_writer.py, scan_data.py, cli_findings.py, logs, wrong files in build/rev/files/). No COM, VM, build/trace-2021 or build/r7 touched; D:\dev\msproject-mac not edited.

Result: 0 Critical, 3 High, 3 Medium, 4 Low.

## Gate counts (fresh tree, MSVC Release, CORE_WARNINGS_AS_ERRORS=ON, build exit 0, 0 warnings)
- core `ctest -E plan_recalc -LE trace_gate_meta -j4`: 67/67 passed, 0 skipped, 62 s (trace_gate_guards 61 s, trace_host_scripts 26 s both ran, neither skipped).
- `MPP_NO_CORPUS=1 ctest -R plan_recalc`: 1/1 passed.
- app tree (`cmake -S app`) ctest: 34/34 passed, 0 skipped.
- `python tools/trace/test_trace_gate.py`: 1901 PASS lines, 0 FAIL, "all passed".
- check_markers --worktree and --range: OK; check_fixtures: OK; `git diff --check b2c0b19..efe9061`: clean; client-string grep: none.
- trace_gate_meta (75 min) not run, as instructed.

## Findings

### R36W-1 (High) Gantt zoom window dates are noise in every view-state record: a wrong zoom save passes (the coordinator's R36W-C1, confirmed)
tools/trace/trace_gate.py:741-760 (`_canon_cview_var2`, `CVIEW_DATE_AT = (1176, 1182, 1186)`, `CVIEW_DATE_SPAN`). The three words are masked on any value inside 220968000..226137600 in every 0x22400008 record of length >= 1194, so the zoom pair's window dates (which a zoom writer must get right) are excused. C.1 narrowed only the +1180 unit word.
Reproduction (wrong file built by build/rev/cli_findings.py: same-size write of the Gantt record of the committed zoom-custom reference, +1176 and +1182 each moved 30 days inside the span):
```
$ python tools/trace/trace_gate.py build/rev/files/wave35-viewstate-zoom-custom-dates-wrong.mpp tests/data/wave35-viewstate-zoom-custom.mpp tests/data/wave35-viewstate-zoom-custom-control.mpp tests/data/wave35-viewstate-zoom-custom-control-null.mpp
96 streams, 96 checked, 0 not checked, 0 problems
exit 0
```
(The same script's unit-word wrong files exit 1, so C.1 holds for the unit word.)
Fix: mask a window date only when the control and its null differ in it (they do in 17 to 21 of 202 pairs, so a pair-specific OPEN is honest), or only outside the zoom pairs, and add a zoom wrong-date case that must FAIL; until then the zoom lift is not gated.

### R36W-2 (High) W36-2.2 Props insert is recorded as a lifted refusal but lift rule (c) fails (the coordinator's R36G2-C1)
docs/handoff/wave36-complete.md:665-668 and core/mppwrite/props_edit.cpp:535-571. The record's own text says (c) "is that test" and 4(e) is "the coordinator's, not run"; the coordinator now reports that Project's reopen of the port's saves shows no baseline and no note, because only the Props record is written (the Task-table baseline fields and note text are not). I confirm the code side: no caller outside props_edit.cpp names 0x02401393 or 0x02400005 (`git grep`), so it is library-only today, and the writer accepts the two ids as a stand-alone edit (`mppwrite_roundtrip_props` red only when the append is wrong). That is a lifted refusal that fails (c) and a record row ("the only item that lifts a refusal") that overstates.
Fix: restore the refusal (make the insert unreachable from `save_props` outside a future combined baseline/note writer) or reclassify W36-2.2 as "framing decoded, not lifted"; correct wave36-complete.md, START-HERE.md and props_edit.h's "insert" paragraph; keep R36G2-4 (two inserts in one save accepted, order unmeasured) and R36G2-5 (any note text of 4..514 even bytes) with it.

### R36W-3 (High, hygiene) network addresses are still printed in the public tip; the R36G0f-8 fix and its test miss one and add one
- tools/trace/test_trace_gate.py:2288 holds the host's address as a literal, `re.compile("<the host node>", ...)`: the check that "no note prints the host's network address" itself prints it.
- spec/writers/evidence/trace-gate-fields-g0b.md:39 prints the VM's node, `the VM's <the VM node>` (a Hyper-V MAC). The C.4 test (test_trace_gate.py:2283-2302) searches only the host's literal and only `*.md` under spec/ and docs/, so it cannot see it.
Reproduction: `git grep -nIiE "<the host node>|00155d" -- tools spec docs` (hits: the test line, the evidence note line, and docs/handoff/wave36-verification/review-g0f.md:160 which names the pattern). Recorded file bytes (assignment_growth.cpp:694, analysis/*.tsv GUID strings) are data and fine.
Fix: remove the VM node from the note ("the VM's node"); in the test compare a SHA-256 of each 12-hex window of the notes against the stored digest instead of the literal, or search with a generic "12 hex digits after `node`" rule; accept that both strings already sit in history from the C.4 commit onward (rewrite before the public push if that matters).

### R36W-4 (Medium) a refusal the G2 record lists has no test
core/mppwrite/props_edit.cpp:559-565 ("record is already in the stream after the typed-property section"). Planted `if (false)` at the call site (count-asserted anchor, 1): `mppwrite_roundtrip_props` stays green (`G2-insert-already-present ctest rc 0`). Same as R36G2-3, still open at the tip. Fix: a roundtrip_props case that inserts the baseline text into a file whose tail already holds 0x02401393 and expects the refusal.

### R36W-5 (Medium) CI turns a missing olefile into a green skip
.github/workflows/ci.yml (jobs build-and-test, ubuntu-asan, trace-gate-meta) and CMakeLists.txt:192, 210: `SKIP_RETURN_CODE 77`. If `pip install olefile` lands in another interpreter than the one CMake found (Windows registry Python is found before PATH unless setup-python's `Python3_ROOT_DIR` applies), `trace_gate_guards` and the 75-minute `trace_gate_meta` report Skipped and the job is green: the gate's own guard is then not run. Fix: in the CI steps, `ctest ... --output-on-failure | tee log` then fail on `grep -E "Skipped|Not Run"` for the two trace tests, or add `python -c "import olefile"` after Configure using `${Python3_EXECUTABLE}` from CMakeCache.

### R36W-6 (Medium) trace-gate-meta CI job probably times out
ci.yml:351-386: 75 minutes on eleven cores becomes about 200 on ubuntu-latest's four; job `timeout-minutes: 200`, ctest TIMEOUT 10800 s. A first dispatch likely cancels. (Same as R36G2-11; not yet run on a runner.) Fix: shard the meta-test by `--only named`/`--only derived` into two jobs, or raise both limits.

### R36W-7 (Low) dangling citations of removed notes in code and comments
efe9061 deleted trace-font-item-r6.md, trace-props-framing-r4.md and docs/handoff/trace-runbook.md; still cited by name in core/mppwrite/viewstream_edit.h:83,111, core/mppwrite/props_edit.h:62, app/app_edits_cell_bold_tests.cpp:19, tests/roundtrip_viewstream.cpp:582, tools/trace/new-trace-vm.ps1:1, tools/trace/odt-project-pinned.xml:1, tools/trace/trace_gate.py:242 and many "DOSSIER" cites. Nothing reads a removed file at run time (checked tests, CMake, CI, scripts: the only evidence file opened is trace-gate-fields-g0b.md, test_trace_gate.py:2301, which exists). Fix: say "(private trace notes)" at each, as START-HERE.md does.

### R36W-8 (Low) the removed notes remain in the public history
`git log --diff-filter=D -- spec/writers/evidence/trace-*.md` : the 44 deleted files, including the runbook, are in main's history (b2c0b19 and earlier). The tip move does not remove them. Say so in START-HERE.md or rewrite before the public push.

### R36W-9 (Low) the gate compares streams only
trace_gate.py `streams()` lists `listdir(streams=True, storages=False)`: storage entries (names, CLSIDs, the directory order) are never compared. A save missing an empty storage passes. Fix: add the storage names to the compare, or state the limit in the docstring.

### R36W-10 (Low) local worktree paths in committed reviews
docs/handoff/wave36-verification/review-g0e.md:~1 and review-g0f.md (`D:/dev/msproject-mac-wt-revg0e`, `...revg0f`, scripts and logs lists). No user name, but machine layout; the rule says no personal paths.

## Item 1: gate masks (field-named, value-checked, evidenced, red when removed)
Fixtures excused sets printed with build/rev/excused.py (a reference compared with itself) show only the named widths: Gantt record `{2: width, 4/4/8: stamp and dates}`, GUID windows of 16, stale words of 4 and 5, Props words of 4 and 16. No region mask exists. Wrong files that FAIL as they must (build/rev/wrong.py): an existing Task row's creation stamp moved in span, in all four rows of three pairs (C.2 holds); font word +42 and +40 flipped (only +43 is excused); baseline slot 0x2401395 set unset or xor 1; a zeroed Task Fixed2 GUID; the Gantt unit word off the ratio (3 pairs); the CEdl rows kind word 0x99. Passing by design (named noise): font +43, the CEdl path text. Only R36W-1 passes wrongly.
Mask removals planted in copies of the gate, each run through `test_trace_gate.py --gate-dir` (anchors count-asserted at 1, build/rev/mut_gate.py): C.2 call site (ctl -> {}), C.2 helper (`known = set()`), C.2 varkey known, C.1 helper (`return True`), C.1 call site (lambda True), C.1 tolerance x50, C.3 kind check, 0f.4b width span, 0f.4b Gantt length, 0f.4.4 slot call site, 0f.4.8 stale other side, 0f.4.8 stale address, 2.0 stamp span +0x1000000, 0f.4.5 guid_rows whole-row test, 0f.4.2 date span, 0.3 no-null (main and compare), 0.2 wrong-length mask, 0c.5 control mask, 0d.2 collision, 0b.8 identity: 21 of 21 red (`02-len` and `0b8-ident` red by a crash, not a named FAIL; the meta-test counts a crash as an error, so they are covered only by the meta run). One mutation of mine (`43-guid-assn3`, extra offsets that duplicate the next row) is equivalent and stayed green: not a finding.

## Item 2: writer refusals
viewstream_edit (font item): mutations at the call site and the helper, `mppwrite_roundtrip_viewstream` red for each: `require_task_row` call removed; helper made always-true; Bold-ON measured-cell check removed; Italic cell pin removed; Italic-off refusal removed; Bold-off cell pin removed; italic flags word replaced by the bold one. 7 of 7 red. props_edit: insert id gate (`return true`), insert pin call, odd-length pad: red (3 of 3); already-present check: GREEN (R36W-4). Reading the source for inputs that should be refused found: Bold ON on any cell but the three, Italic anywhere but (1, Task Name), Italic off, Bold off elsewhere, row not in the Task table, row 0 and 0xFFFFFFFF all refused in validate_edit/require_task_row; app plan layer refuses `id != uid` in addition. Accepted but arguably not measured: two inserts in one Props save (R36G2-4) and any even-length note text (R36G2-5), under R36W-2.

## Item 3: CI workflows
`git diff` of .github/workflows: only ci.yml changed. Jobs at b2c0b19 (build-and-test windows and ubuntu, ubuntu-asan, plus qt.yml and app-macos.yml untouched) keep every step; the one change per job is `-LE trace_gate_meta` on the main ctest of build-and-test and ubuntu-asan, plus olefile install, and the new job trace-gate-meta runs `-L trace_gate_meta`. Only trace_gate_meta carries that label, so no earlier test is dropped; the plan_recalc, mpp_reader and app steps are unchanged. Not run on a runner (see R36W-5, R36W-6). `--timeout 60` does not cap trace_gate_guards (property TIMEOUT 600; local run 61 s).

## Item 5: hygiene
Scans over the added lines of the 110 commits and of the 292 added `.mpp` fixtures (every stream, ASCII and UTF-16): no person's name, client name, user-profile path, e-mail or IP address in text; the only path in fixtures is the neutral `C:\w35\r7-nulls\...`; SummaryInformation authors are "Fixture Builder" and synthetic titles. Commit authorship is the repo owner's noreply address on all 110. Network-address hits: R36W-3. `check_markers` OK. After efe9061 nothing at run time reads a removed file (R36W-7).

## Item 6: "Guards seen red" spot checks (all re-planted at helper and call site, anchors asserted)
C.2 (stamp-row-known; wave36 record row C.2): red 3 of 3 variants. C.1 (pane-unit; row C.1): red 3 of 3. C.3 (rows-kind): red. 0f.4.4 (slot), 0f.4.8 (stale), 0f.4b (pane width), W36-2.0 (span): red 7 of 7. G1/G1b (font item): red 7 of 7. G2 (Props insert, row 2.2): 3 of 4 red; the "refusal list holds" claim for the already-present refusal is not backed by a test (R36W-4). The 0.3, 0.2, 0c.5, 0d.2 and 0b.8 rows are red.
