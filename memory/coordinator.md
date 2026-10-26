# memory — coordinator (2026-10-26, wave 29 closed on branch)

**Entry point:** `docs/START-HERE.md`.  Wave gate:
`docs/handoff/wave29-complete.md`.  History:
`memory/archive/coordinator-through-wave20-full.md`.

## Where the branches are

`main` = **fcb1c10** = `origin/main`.  **`wave23`** (`e1d40b8` =
`origin/wave23`), **`wave24`** (`6dbf126`), **`wave25`** (`c012813`),
**`wave26`** (`1299cc7` = `origin/wave26`), **`wave27`** (`bc52cb8` =
`origin/wave27`) and **`wave28`** (`21646f4` = `origin/wave28`) are closed and
**not merged**.  Wave 28 was pushed only after Tim approved the `076438f`
rewrite; it was replayed clean onto `21646f4` and the guard now passes.

**Wave 29 is closed on `wave29`, pushed and not merged**; `origin/wave29` =
the CLOSE docs commit, final code tree `7e14aaa`.  Wave 29 commits (new ids
after the move; `wave29-sha-map.txt` maps the pre-move ids the G0 table
cites): brief `f2641c5` (+ addendum `9fd872a`), W29-0.0 `ff8ab39`, W29-0.1
`f6c958c`+`8531b40`, W29-0.2 `eddd9e9`+`b751435`+`61f19c8`, W29-0.3
`bd9e13d`+`7fc719b`, W29-0.4 `8f80c2a`+`50df4b2`, G0 complete `4fee013`;
W29-1.0a `a790a24`, 1.0b `fed21fb`+`2b9862e`, 1.1 `7b01d37`, 1.2 `1a743b6`,
1.3 `aaf05fe`, 1.4 `c79a45d`, G1 record `830b5b6`, G1 complete `6a36a43`;
W29-2.1 `190e1ce`, G2 record `afc7606`, G2 complete `624903d`; W29-3.1
`7fd6851`, G3 record `3ad4d67`, `ecb4d3a`, G4 record `4ee0cf3`; W29-4.1
`56f0ffa`, G4 record `4448df3`, G4 complete `12b4cf1`; W29-5.1 `13e0235`,
5.2 `f09f114`, G5 record `10ad181`, 5.2 ink `7ea220c`, G5 record `d70e97e`,
G5 complete `e13f5a4`, G5 record `7262148`; CLOSE fixes `02b720a` (C1),
`4b03ecb` (C2), `f7434d2` (C7), `31bb11b` (C8), `133cb3f` (C9), `7e14aaa`
(C10); then the CLOSE docs commit.  Every group marker
(`WAVE-29-G0-COMPLETE` .. `WAVE-29-G5-COMPLETE`, then `WAVE-29-COMPLETE`) is in
`wave29-complete.md`.

## Numbers (wave 29 close, fresh trees build/w29-close-fresh{,-app,-qt}, final code tree 7e14aaa)

* core configure/build with `CORE_WARNINGS_AS_ERRORS=ON` exit 0 / exit 0;
  core ctest `-E plan_recalc` **52/52**; plan_recalc **512** with corpus /
  **266** without; reader **1100** with corpus / **420** without; M2
  **332/332**, **268/268** and **64/64**; app **20/20**; app_qt **27/27**;
  fixtures OK (**98** documented hashes, **101** tracked `.mpp`), paths OK
  (148), oracle provenance OK (8).  The log is
  `build/w29/close-final-fresh-gates.log`, filed as
  `build/w29/gates-7e14aaa-W29-CLOSE.log`; an earlier part-A attempt on
  `31bb11b` is kept as `close-final-fresh-gates-attempt1.log`.
* Engine **7/8** (`constraint-mso` deliberately expected-failing;
  `mppwrite::save_plan` is test-only); selftest 53 checks, suite 8 recorded
  (7 pass + 1 expected), 0 unrecorded, exit 0.
* Parity: **wave23-outline 10/10** and **wave25-auto colour 1/0**, re-run on
  the fresh Qt shell at 7e14aaa (`build/w29/c6-parity-fixed.log`,
  `build/w29/c6-parity-wave25.log`); every measure PASS.  The pixel score is
  not re-run (no reference PNG locally).
* CI: first dispatch on `31bb11b` **RED** — qt run 40245417658 (macOS C7,
  windows parity C10) and ci run 40245429060 (ubuntu-gcc C9).  Second dispatch
  on `7e14aaa` is **GREEN**: qt run 40248190323 (windows-qt, macos-qt,
  linux-qt) and ci run 40248193087 (windows-msvc, ubuntu-gcc); snapshots
  `build/w29/close-ci4-{qt,ci}-status.json`.
* Privacy: line scan **0 files / 0 hits** on HEAD and **0 commit messages** on
  `wave28..HEAD`; normalised `guard_names.py` **0 hits, exit 0** on
  `wave28..HEAD` and `origin/wave28..HEAD`; no `client-string-<8 hex>`.
  Preflights are `build/w29/close-push{3,4}-*.log` (guard/hex logs are empty
  because those commands print only on failure; the exit codes are in the
  final gate log).  Residuals (Tim's decision): two pre-wave fixtures
  (`synthetic-base.mpp`, `wave23-outline.mpp`) carry the operator's name three
  times each, 69 tracked text files (historical docs) carry it, and the 13
  unsalted FNV-1a name hashes removed in wave 27 remain in older wave-26
  commits plus `tests/data/project_a_golden.tsv`.  **Run BOTH scans.**  The
  156-name scanner still cannot see skipped-row names (print uid/row only).

## Wave 29 result (what a later session must not re-break)

* **G0's measured-shape gate stays** (W29-0.0..0.4): every enabled command
  refuses a shape no committed Project pair measured.  The save-pair tests
  compare every owned stream field by field against PROJECT's file; streams
  outside the owned set are "port == Project" or a named `EXPECTED FAIL`
  never "port == control".  Project reopened 10 port saves with **0 differing
  fields** (project row + IDs 1..3; `build/w29/W29-0.1-project-reopen.json`).
  Percent writer: only leaf 0→40 under a summary, work assignment 0→40 and
  zero-duration milestone 0→100 are lifted; mid-to-mid (25→60) stays refused
  with 50 named expected failures because Project reschedules the chain.
  Mark on Track / Update Project lower recorded progress as Project does
  (60→40 through a date before, 60→80 after); the summary/project roll-up and
  date-update shapes are measured.  Indent/outdent write the assignment
  tombstone (state 0x02) and re-create (uid max+1) on automatic rows.
* **G1**: stored `CTable` column widths go through the default font's measured
  unit; the per-row yellow Highlighted fill paints from the stored bytes; every
  bar label the applied view's stored styles call for is drawn (top/bottom/
  inside placement stays a stated convention); a stored timescale origin/zoom
  applies — BUT a stock start (14400) with the stock 100 % day tier changes
  nothing, because that width is the shell's own week zoom and applying it
  re-bases the axis to days and breaks the wave-23 outline parity left edge
  (W29-C6, `app::apply_stored_timescale` early return).
* **G2/G3/G4**: Text Styles, Gridlines and Bar Styles dialogs; Page Setup fully
  wired (six tabs, every field edits the shared print layout, OK/Preview/Print
  gated); Visual Reports and Compare Projects laid out with every
  current-affecting control disabled, the exact decision-9 tooltip and an
  inline note.  Text/Bar styles apply to the open view only — no stored-style
  write exists, so Save keeps the file's bytes.
* **G5**: dark themes paint the sheet caption band, gutter/corner and Gantt
  timescale band from the measured Dark Gray/Black palettes; the ribbon ink
  follows the theme; menu-bar/QAT/status-bar glyphs are one-ink drawings
  re-inked live (`window_ink`), with contrast pinned.  The dark timescale tier
  boundary uses the measured WEAK rule (`chart_header_rule`), not the strong
  one (C8); light keeps the wave-26 strong line (no light measurement).
* **The CLOSE review stays binding**: 0 Critical / 2 High / 2 Medium / 2 Low
  for C1-C6; CI added C7/C9/C10 and the audit C8.  All Highs fixed.  C3
  (`add_task_link` Fixed2Meta +8 for a second incoming link) needs a Project
  pair; C4 (null-save masked tail) is tooling; C5 (percent edit on a linked
  row refused only at Save) and C6 (Props-mask wording) remain queued.
* **The wave-28 residuals are unchanged**: whole-tree operator-name purge is
  Tim's decision; `check_fixtures.py`'s UTF-16 user-profile pattern is still
  dead on backslash forms (R28-5); save-time ordering and late refusals
  (R28-6); Task Form view-only fields (R28-8); SKIP-on-missing fixture blocks
  (R28-9); `tests/data/README.md` still lacks 21 tracked files (R28-11).
* **Still binding from waves 26-28**: display rows resolve through their uid;
  Priority 750 measured and kept; `StructuralEdits::push(edit, plan)` needs
  the plan for percent edits; every real-save `Loaded` has a named check;
  Split Task stays measured-but-disabled; the corpus task-order case asserts
  no names/hashes; text files are LF (174 grandfathered CRLF files remain,
  `git grep -Il $'\r'`).

## Next

Wave 30 per `docs/handoff/plan-waves27-30.md`: the 83 disabled ribbon
commands, highest use first — Task tab (Move, Inspect, Deliverable), Resource
tab (Assign Resources, Resource Pool, levelling), Project tab (baselines, WBS,
Calculate), View tab (Sort, Outline, Filter, Group by, Highlight) — then the
second view family (Resource Sheet, Task Usage, Resource Usage) at Gantt
fidelity.  Every command that writes needs its own Project measurement before
it is enabled (rule 1) and a save-and-reopen test.  Behind it: the wave-29
CLOSE "Found, not fixed" list (C3-C6, the theme tier-boundary sample) and the
carried wave-26/27/28 residuals.  Keep the CLOSE gates green before every
commit and run BOTH name scans.
