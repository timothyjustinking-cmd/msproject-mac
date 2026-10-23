# A5 - E7 (new-task scheduling default): the evidence behind each claim

Audit of wave 22 lane E7 (docs/handoff/e7.md, memory/e7.md,
wave22-complete.md "The new-task scheduling default (E7)").  Read-only;
nothing built or run except read-only greps/tallies over existing files.
Repo HEAD e252041.

Headline: NONE of the three evidence claims is backed by a COMMITTED script.
All three can be re-run on THIS machine today because the lane's probe scripts
and the decompile still exist under git-ignored paths (build/e7-w22/*.py,
decompiled/part_*.c) and the inputs are the git-ignored corpus.  A fresh clone
can reproduce only the committed C++ test (claim ii's values, hard-coded) and
only when the private corpus is present.

Grep coverage: `0x024013c8|024013c8|QNEWTASKSAREMANUAL|NewTasksAreManual`
hits nothing under scripts/ or analysis/; hits are only in core/mpp/reader.*,
tests/mpp_reader_tests.cpp, app_qt/main_window.h, app_qt/tests/ux_gate_test.cpp,
docs/handoff/{e7,coordinator,wave22-complete}.md, docs/roadmap.md,
docs/START-HERE.md, memory/{e7,coordinator}.md.  e7.md:88-89 itself says
"probe script left under `build/`, not committed".

## Claim (i) - Project's property-name table, QNEWTASKSAREMANUAL at index 64

Status: no script; evidence is a decompile that is git-ignored.  Re-runnable
by grep on a machine that has the Ghidra export.

Where the table comes from:
* decompiled/part_0000.c, function FUN_1401b8a70 (header line 12205, body
  from 12209).  This file is git-ignored (.gitignore:3 `decompiled/part_*.c`,
  "regenerable from the Ghidra project", itself ignored at .gitignore:2;
  exporter scripts/ExportAllC.java).  Only decompiled/failures.txt and
  summary.txt are tracked.
* Index-0 anchor:   part_0000.c:12340  `local_3e8 = L"DEFAULTCALENDAR";`
* Index-64 entry:   part_0000.c:12404  `local_1e8 = L"QNEWTASKSAREMANUAL";`
  (0x3e8 - 0x1e8) / 8 = 0x200 / 8 = 64.  Neighbours: :12402 USEOMIDS,
  :12403 WSSINFO, :12405 SUGGESTIONSMASK, :12406 IGNOREMASK.  Note that the
  same stack slots are re-used for a SECOND table later in the function
  (:12737 `local_3e8 = L"QISEDITABLEACTUALCOSTS"`, :12801 `local_1e8 =
  L"PASTELINKSCOUNT"`), so "index 64 of the table starting at 12340" is the
  precise statement; "element 64 of FUN_1401b8a70" alone is ambiguous.
* Index rule (id -> name):  e7.md:49-52 cites "decompiled/part_0003.c, around
  FUN_14034ede6".  FUN_14034ede6 does not exist as a function; 14034ede6 is a
  LABEL (`LAB_14034ede6`, part_0003.c:11544) inside FUN_14034ea20 (header
  part_0003.c:11423).  The rule is at part_0003.c:11527-11531:
    param_2 = param_2 & 0xffff;
    ... if ((param_2 < 5000) || (param_2 = param_2 - 5000, <table size> <= param_2)) goto LAB_14034ede6;
  i.e. index = (id & 0xffff) - 5000; 5000 = 0x1388, so id(i) = 0x02401388 + i
  and i = 64 -> 0x024013c8.
* Second tie:  part_0000.c:81230-81234 - when the dispatcher sets 0x24013cb
  (QCANPINTASKS, index 67) to 0 it calls the getter for 0x24013c8.  Also
  part_0000.c:8357 `_DAT_1412b9ae0 = 0x24013c8;` and :8450 `= 0x24013cb;`.
* Neighbour cross-checks (QISNEWTASKSESTIMATED 0x0240138a, DAYSPERMONTH
  0x0240138f, PROJECTCURRENCYCODE 0x024013bb, CREATIONSYSTEMTIME 0x02401394,
  BASELINE1..10SAVEDATE) were read from the props dumps the lane left in
  build/e7-w22/props-six.txt and cmp-114-props.txt (uncommitted), produced by
  build/e7-w22/probe_newtasks.py `props` and compare_props.py.

Re-run (repo root, needs decompiled/part_*.c locally):
    grep -n "L\"DEFAULTCALENDAR\"\|L\"QNEWTASKSAREMANUAL\"" decompiled/part_0000.c
        -> 12340 / 12404 (and the second table at 12737)
    grep -n "param_2 - 5000" decompiled/part_0003.c          -> 11530
    grep -n "0x24013c8" decompiled/part_0000.c                -> 8357, 81234
    python build/e7-w22/probe_newtasks.py props corpus/project_a.mpp corpus/project_b.mpp tests/data/synthetic-base.mpp
        (uncommitted helper; dumps every 114/Props record id/kind/payload)
Not re-runnable from a clean clone: the decompile must be regenerated from
the Ghidra project first.

## Claim (ii) - MSPDI <NewTasksAreManual> agrees 10/10 on exports

Status: no script; the comparison is hand-made (prose table e7.md:71-77) and
then HARD-CODED as expected values in the committed reader test.  The XML
side is never read by any code in the repo.

The exports (all git-ignored under corpus/, .gitignore:27):
    corpus/probe/probe-export-a.xml      <NewTasksAreManual>0  (labelled as PROJECT_A, "<client project title>", 83 tasks)
    corpus/probe/probe-export-b.xml   <NewTasksAreManual>0  (labelled as PROJECT_B, "client-text-064", 73 tasks)
    corpus/probe/assign1.xml         <NewTasksAreManual>1
    corpus/probe/lagff1.xml          <NewTasksAreManual>1
    corpus/probe/w9-resourcetypes.xml <NewTasksAreManual>1
Two further exports in the same folder carry NO <NewTasksAreManual> element:
corpus/probe/tf2007-mspdi.xml and corpus/probe/w10-formulas.xml (so "10/10"
is 5 files x 2 directions, not 10 files; e7.md:79 says this).  The pairing of
probe-export-a.xml / probe-export-b.xml with project_a.mpp / project_b.mpp is by
title and task count (prose, e7.md:68-69); there is no script that checks it.

Re-run (repo root):
    grep -o "<NewTasksAreManual>[01]</NewTasksAreManual>" corpus/probe/*.xml
    python build/e7-w22/scan_newtasks.py | grep -E "project_[ab]|assign1|lagff1|w9-resourcetypes"
        (uncommitted; prints the stored u16 for each .mpp - compare by eye)
    python build/e7-w22/compare_props.py 114/Props
        (uncommitted; AUTO/MANUAL file lists hard-coded at lines 15-17)
Committed re-check of the SAME five values (requires the local corpus;
MPP_NO_CORPUS=1 skips them):
    cmake -S . -B build/e7-w22 -G "Visual Studio 17 2022" -A x64 -DCORE_BUILD_TESTS=ON
    cmake --build build/e7-w22 --config Release --target mpp_reader_tests
    ctest --test-dir build/e7-w22 -C Release -R mpp_reader --output-on-failure
  mpp_reader_tests target: core/mpp/CMakeLists.txt:66-74.
  Expected values live in tests/mpp_reader_tests.cpp:1143-1158 (the Case[]
  table) with the XML names only in strings/comments (:1101-1106).

## Claim (iii) - 133 .mpp files scanned

Status: script exists but is UNCOMMITTED (build/ is git-ignored,
.gitignore:36).  Fully re-runnable on this machine; output already on disk.

* Script:  build/e7-w22/scan_newtasks.py (64 lines).  Globs
  `corpus/**/*.mpp` (recursive) + `tests/data/*.mpp` (lines 35-36); opens
  each with olefile; picks 114/Props else 112/Props; walks the props framing
  (16-byte header, then u32 len / u32 id / u32 kind / payload until an id
  whose byte 2 != 0x40); prints version, stream, kind/len, u16 value.
  Dependency: python `olefile` (0.47 is installed here).
* Output:  build/e7-w22/scan-newtasks.txt, 133 lines.  Tallied here:
  value=1: 102, value=0: 9, ABSENT: 16, "no props stream": 6, every present
  record kind=2 len=2 (0 exceptions), 17 found in 112/Props.  Matches
  e7.md:91-97 exactly.  The nine value=0 files: corpus/project_a.mpp,
  corpus/project_b.mpp, corpus/probe/project_a-recalc.mpp,
  corpus/imported/sf248.mpp, corpus/probe/SubprojectA-9.mpp,
  corpus/probe/SubprojectB-9.mpp, corpus/imported/mpp12nulltasks-from14.mpp,
  corpus/imported/task-costs-project2013-mpp12.mpp,
  corpus/probe/mpp12subproject-from14.mpp (matches e7.md:98-100).
* Where the 133 files are (all .mpp on disk, counted here):
    corpus/project_a.mpp, corpus/project_b.mpp      2   (client files, git-ignored)
    corpus/imported/*.mpp                           12  (git-ignored)
    corpus/probe/*.mpp                              117 (COM-driven probes, git-ignored)
    tests/data/*.mpp                                2   (synthetic-base, synthetic-calendars; TRACKED)
    corpus/bench, corpus/reference                  0 .mpp
  131 + 2 = 133.  A clean clone has only the 2 tracked files.
* Other uncommitted E7 helpers in the same folder: probe_newtasks.py
  (props/extract/streams sub-commands), compare_props.py, find_pattern.py
  (counts the 14-byte record pattern in synthetic-base.mpp), props-four.txt,
  props-six.txt, cmp-114-props.txt, streams-a.txt.

Re-run (repo root):
    python build/e7-w22/scan_newtasks.py > build/e7-w22/scan-newtasks.txt
    grep -c "value=1" build/e7-w22/scan-newtasks.txt     -> 102
    grep -c "value=0" build/e7-w22/scan-newtasks.txt     -> 9
    grep -c "ABSENT"  build/e7-w22/scan-newtasks.txt     -> 16
    grep -c "no props stream" build/e7-w22/scan-newtasks.txt -> 6

## Reader API, tests and the Manual fixture

* core/mpp/reader.h:800  `bool new_tasks_manual = false;`  (ProjectRow)
  core/mpp/reader.h:801  `bool new_tasks_manual_stored = false;`
  core/mpp/reader.h:812-815  `struct NewTaskMode { bool known = false; bool manual = true; };`
  core/mpp/reader.h:816  `NewTaskMode read_new_task_mode(std::string const& path);`
  core/mpp/reader.h:773-799  the evidence comment (name table, index rule,
  neighbour checks, MSPDI labels).
* core/mpp/reader.cpp:729  `constexpr std::uint32_t kProjectPropsNewTasksManual = 0x024013C8u;`
  core/mpp/reader.cpp:3615  `decode_new_tasks_manual(...)` (framing walk; a
  wrong kind/length is pushed to warnings, :3640-3650)
  core/mpp/reader.cpp:3694-3698  parse_document() sets
  project.new_tasks_manual / _stored
  core/mpp/reader.cpp:3798-3812  `read_new_task_mode(path)` (reopens the
  container only; swallows mpp::Error -> known=false)
* Tests:
  - tests/mpp_reader_tests.cpp:1112  `test_new_task_mode(TestContext&)`,
    registered at :3421; fixture block :1113-1126 (Manual, stored, path
    accessor agrees); corpus Case[] :1143-1158 (PROJECT_A/B Auto, assign1 /
    lagff1 / w9-resourcetypes Manual, probe/mpp12task.mpp not stored,
    imported/mpp12nulltasks-from14.mpp stored Auto via 112/Props,
    probe/oracle-patch.mpp not stored).  Built as `mpp_reader_tests`
    (core/mpp/CMakeLists.txt:66).
  - app_qt/tests/ux_gate_test.cpp:349  `case_status_mode(fixture, out_dir)`
    (called :547): fixture -> "Manually Scheduled"; a copy written to
    test-out with the single 14-byte record `02000000 c8134002 02000000 0100`
    flipped to 0 (write_auto_new_tasks_copy :285-310, requires exactly one
    match) -> "Auto Scheduled"; corpus/project_a.mpp -> Auto when present.
    Gate name `qt_ux_gate` in app_qt/CMakeLists.txt.
* The "Manual fixture": tests/data/synthetic-base.mpp (tracked; the only
  .mpp that may be committed, tests/data/README.md:8).  Provenance
  README.md:12-24: byte-for-byte copy of corpus/probe/base.mpp, a COM
  FileNew() project made 2026-10-22, SHA256 55C109F2...3FC6069, 237,568 bytes;
  hash pinned by scripts/check_fixtures.py:21.  scan-newtasks.txt line for it:
  `MPP14  114/Props  kind=2 len=2 value=1`.  tests/data/synthetic-calendars.mpp
  (also tracked) stores value=1 too but is not used by the E7 tests.

## Summary of reproducibility

| claim | committed script? | re-runnable here? | from a clean clone? |
|---|---|---|---|
| (i) name table / index 64 | no (grep over git-ignored decompile) | yes, grep lines above | no (needs Ghidra export) |
| (ii) MSPDI 10/10 | no (values hard-coded in mpp_reader_tests.cpp) | yes (grep + uncommitted scan) | partially: test runs but SKIPs without corpus |
| (iii) 133-file scan | no (build/e7-w22/scan_newtasks.py, ignored) | yes, output on disk and tallies match | no (131 of 133 inputs are git-ignored) |
