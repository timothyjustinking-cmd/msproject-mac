# Wave 23 brief - the audit work order (for DeepSeek, coordinator role)

_Written 2026-10-23 by the Fable audit session from `docs/handoff/audit-w22-findings.md`.
Baseline audited: docs `e252041`, code `abeafd2`.  Drift at hand-over: none in code;
this brief, the findings file, `docs/handoff/audit-w22/*` and `tools/deepseek/*`
are the only additions.  Operator rulings (Tim, 2026-10-23) are final and are
listed in section 6.  This file is the one document the fix session reads; when
something here conflicts with an older handoff, this file wins._

## 0. Read first (in this order, nothing else until section 2 is under way)

1. this file;
2. `docs/handoff/audit-w22-findings.md` (every id below is defined there);
3. `docs/handoff/audit-w22/A3-s6-independence.md`, `A4-engine-score.md`,
   `A8-ui-parity-gate.md` when you reach their fixes; `A2-*`, `A5-*`, `A7-*` likewise;
4. `docs/START-HERE.md` house rules and `docs/ui-parity-spec.md` (the 16 decisions);
5. the captures in `corpus/reference/2026-10-23-audit/` (README inside) and
   `corpus/reference/2026-10-23-msproject/` - these are the measurement truth.  They
   are git-ignored and may show client content: never commit, copy into tests/ or
   docs/, or quote task names, dates or staff names from them.

## 1. Standing rules for this wave (all hard)

- **Target is 100 % parity with MS Project Professional 2021 (16.0.20430.20182) as
  installed on this machine.**  Project wins every disagreement.  "Not implemented"
  is acceptable only when the control is disabled with the exact tooltip and listed
  in the generated backlog; nothing is faked.
- **Branch:** work on `wave23` (create from `main` at e252041).  Commit per finding
  id (`A-7: ...`, `B-1: ...`), one lane per commit, honest numbers in the message.
  Push the branch.  **Do not merge to `main` and do not force-push**: Tim merges
  after reading `wave23-complete.md`.
- **Gates are hard stops.**  Before every commit: `ctest -E plan_recalc` on the core
  tree, `plan_recalc_tests` with `MPP_NO_CORPUS=1`, M2 `recalc --compare` 332/332,
  `engine_diff.py --suite --apply-bin <fresh exe>` 7/8 or better, app_qt ctest with
  the Qt bin dir on PATH, `python scripts/check_fixtures.py`.  A gate that goes red
  is fixed or the change is reverted; it is never re-targeted to pass.
- **Goldens and fixtures:** any change to a file under `tests/data/` or to a pinned
  hash is its own commit whose message says what Project did to justify it.  Never
  open a committed fixture or a corpus file in Project in place: copy to `build/`.
  `plan_recalc` is never run with the corpus present and `MPP_NO_CORPUS` unset.
- **Oracle/COM** only through `scripts/oracle_lock.py`, `FileClose(0)`, Project
  closed afterwards.  **Builds serial**, output redirected.  **One CI dispatch**, at
  the end, on the `wave23` branch (`gh workflow run qt.yml --ref wave23`); a red run
  is triaged and fixed before a second dispatch, with the reason recorded.
- **Corpus and captures** are never committed.  `git show --stat` before every push.
- **Report after, not before:** the operator has waived the ask-first queue.  Every
  finding is fixed or explicitly deferred with a reason in `wave23-complete.md`.
  Nothing is silently dropped.
- **Context discipline** (coordination.md): keep `memory/coordinator.md` current;
  at ~70 % of context, commit, write the handoff and stop.

## 2. Authorised now - do in this order

Each item: finding id, what to change, acceptance test.  "Verifier" lines name the
tempting fix that is wrong.

### 2.1 Data fidelity (the loaded file must look like Project's)

**W23-1  B-1 / A-3  Task order and outline.**  Render the file's tasks in Project
ID order with the outline (summary rows, indentation, collapse state, project summary
row 0 when the file shows it), not uid order with fresh IDs.  The ID column must show
the file's IDs.  Acceptance: `corpus/project_a.mpp` rows 0-12 in the port match the
clean capture `project-gantt-loaded-clean.png` row for row (ID, indent level, name
hash); add a ctest on `tests/data/synthetic-base.mpp` plus a new oracle-built fixture
with a 3-level outline (coordinator builds it through COM; copy to build/ first) that
asserts ID order == row order and outline levels.  Verifier: do not "fix" by sorting
uids; read the file's ID/outline fields (the reader already exposes them to M2, which
scores 64/64 summaries) and make the view model consume them.

**W23-2  B-2  View state: timeline.**  Show the Timeline strip only when the file's
stored view says so (project_a hides it; a new project shows it).  Acceptance: port
on project_a shows no strip; new project shows it; existing `qt_empty_state` stays
green.  Verifier: do not simply hide the strip for all loaded files.

**W23-3  B-3  Timescale from the file.**  Draw the stored view's tiers and zoom
(project_a: year / quarter / month) instead of the fixed week/day.  Acceptance: tier
labels at the top of the chart match the clean capture for the same visible range.

**W23-4  B-4 + wave-22 item 5  Bars and bar styles.**  Decode the file's Gantt bar
styles (navy summary, green non-critical/complete, red critical, milestone diamonds
with date labels, bar labels) and the project-summary bar.  Acceptance: a per-row bar
colour table for project_a rows 0-12 sampled from the port's 1920 render equals the
colours sampled from `project-gantt-loaded-clean.png` (write the sampler; keep it out
of CI if it needs the corpus).  Until decoded, state it in the backlog; do not
hard-code the capture's colours as globals.

**W23-5  B-11  Text styles.**  Apply the file's text styles (milestone italic/red,
highlighted summary rows, the sheet font Arial 8 when the file says so) rather than
the port's two rules.  Acceptance: row 3 (a milestone) renders italic red in the
port; the Font box and the sheet agree.

**W23-6  wave-22 item 2  SUMMARY (Flag10).**  Decode `Task.Flag10 0x0b400051` in
`core/mpp` so the file's SUMMARY column shows Yes/No.  Acceptance: project_a's column
matches the capture for the visible rows; reader test on a fixture.

### 2.2 Gate honesty (A-1, A-2, A-4, A-5, A-7, A-9, A-10)

**W23-7  A-2  S6 oracle independence.**  (a) Change the 4 lagged pins to what
Project prints ("9FS+50 d" style) and make the port print it - check first what
Project shows for the lag label rule (days vs d) by reading the capture, not by
guessing; (b) remove the claim that task 42's pin is "from the capture", or capture
it; (c) carry `lag_units` through `app::LinkRow`/`reader_adapter.h:152` and print
elapsed/hour/week/percent lags and FF/SF as Project does, with a constructed-plan test
per unit; (d) make a corpus-free fixture with real links and a named assignment
through the oracle so CI's Depends On / Who checks are not blank-vs-blank; (e) in
`s6.md` and `wave22-complete.md`, replace "independent oracle" with what it is.
Verifier: the oracle for the 83-row sweep stays the same reader (there is no other);
what must become independent is the expected text, so pin it from Project's own
Predecessors column via COM (`Task.Predecessors` strings) into a hashed golden.

**W23-8  A-1  UI parity gate.**  (a) Add a same-file structural gate on the corpus
locally (and on the new oracle fixture in CI): row order, ID column, bar colour per
row, timeline presence, tier labels, status-bar text, selection colour `#A2D3A0`,
font family of the sheet; (b) implement the pixel-difference score decision 12 asks
for (report only) and remove the wave-20 claim that it exists; (c) make
`qt_parity_geometry` compare against literals typed from the capture, not
`shell_style.h` symbols; (d) run `parity_report.py` in CI on the 1920 render
against a committed, client-free reference (the synthetic fixture captured in Project
by the coordinator) and fail on regression.  Verifier: a reference that is our own
earlier render is not a reference.

**W23-9  A-4  Engine wording and lane.**  (a) Make `--suite` a ctest lane with an
explicit `--apply-bin` from the current build tree; fix the documented command and
the stale `build/e1` default (A-4 re-run); (b) reword "deliberately not scored" to
"deliberately left failing" everywhere (engine-parity.md, e6.md, wave22-complete.md,
START-HERE, memory); (c) add provenance (Project version, recorded-at, saved-file
sha256) to the oracle JSON header and have `--record` write it; (d) state that
`mppwrite::save_plan` is test-only until the app calls it, or wire the app's save path
to the MSO rule and prove it with an app_qt scenario (set MSO, recalc, save, reopen in
Project through the oracle, read `Task.Duration`).  Verifier: do not edit
`constraint-mso.oracle.json` to make it pass; 8/8 is earned only by a held-out
Project measurement on a differently shaped calendar plus a reopen read.

**W23-10  A-5  Ribbon gate.**  Replace the duplicated literal with an independent
pin file (JSON/TSV typed from the strips, committed, client-free) that both the gate
and - optionally - the builder read; pin the enabled set (count and which) rather
than enabled-iff-implemented; make the 1920 pass assert that the last group of every
tab is present and not clipped; re-check the inventory at 900.  Restore the removed
live commands (Save as PDF, Export PDF, Task Sheet, Resource Sheet/Usage) through
the backstage or their Project homes so no working command is mouse-unreachable.
Fix `ui-reference-set.md` (Add Resources is the Insert group; 22 captures / 28
files).

**W23-11  A-7  Golden safety and fixtures in place.**  (a) Make the recalc golden
write opt-in (`PLAN_RECALC_UPDATE_GOLDEN=1`), diff-before-write, and never after a
failed assertion; add `-E plan_recalc` or the opt-in to `ci.yml`; (b) change
`oracle_w11/w12/w14.py` and `msproj_bench.py` to copy the fixture to `build/` before
`FileOpen`; (c) run `check_fixtures.py` in both workflows; (d) document all 19
tracked `tests/data` files with hashes in `tests/data/README.md`; (e) update the
F1 depth text (64) in `tests/fuzz/README.md`, `tests/fuzz/CMakeLists.txt`,
`docs/audit-findings.md`.  Verifier: do not delete the golden writer; the
coordinator still needs a way to refresh it.

**W23-12  A-9 / A-10  CI coverage and the local gate.**  Run the core workflow on
every dispatch alongside qt-shell (or trigger it from qt.yml), so "CI green" covers
50 core tests + suite + fixtures, not only the 14 Qt tests; document the Qt bin PATH
precondition in `docs/build.md` and have the app_qt CMake set it on the ctest env;
record the dispatch log honestly in the wave close (count and reasons).

**W23-13  A-6  E7 evidence trail.**  Commit `scan_newtasks.py` under `scripts/`,
a committed scan summary (counts only, no file names from the corpus), and a
reader test that reads the property-name table index from a committed byte fixture;
reword e7.md's "FUN_14034ede6" to the real function and say the decompiled file is
local-only.

### 2.3 Chrome and dialogs (B-5 .. B-14, decisions 1, 2, 3, 8, 10, 14, 15)

**W23-14  B-5  Icons.**  Replace the placeholder circle with a distinct monochrome
glyph per command (reuse where Project reuses).  Acceptance: a gate asserts no two
commands on a tab share the same icon pixmap hash except the documented reuses, and
no command shows the placeholder.

**W23-15  B-6  Control kinds.**  Font name/size combos; highlight and font-colour
split buttons; the five % buttons; outdent/indent/link/unlink/split; View > Highlight,
Filter, Group by and Timescale combos with Project's default texts; Split View
checkboxes with their combos; Status Date field; Format tab checkboxes (Critical
Tasks, Slack, Late Tasks, Outline Number, Project Summary Task, Summary Tasks);
the 16-swatch Gantt Chart Style gallery with expander and dialog launcher; the four
align/wrap buttons; the Font dialog launcher.  Acceptance: the ribbon gate pins
control kind per control, from the independent pin file.

**W23-16  B-7 / B-8 / B-14  Backstage and Options.**  Plain "File" tab and light
rail per the Account capture; Account page (About, version, theme chooser) so About
is reachable; New with templates area and Recent list; Open with Recent and Places;
Print with preview and settings; Share/Export per the captures; Options as the
11-page dialog (A-12) with Cancel honest and unimplemented pages laid out and
disabled.  Add the missing dialogs from B-14 (Update Project, Links Between
Projects, Custom Fields, Change Working Time with Exceptions/Work Weeks and
Details/Options, WBS Define Code and Renumber, Set Baseline/Clear Baseline, Move
Project with its Yes/No prompt), laid out from the captures, primary action
disabled where the engine is missing.

**W23-17  B-9 / B-10  Status bar and title.**  "Ready" at the left; five view
buttons (Gantt Chart, Task Usage, Team Planner, Resource Sheet, Report); light bar in
the light theme; window title "<file name>.mpp - <port name>".

**W23-18  B-12 / decision 10  Dark theme.**  Apply the theme live on selection;
offer Dark Gray and Black like Project; darken chrome only (sheet and chart keep the
file's colours, as measured).  Acceptance: a gate renders both themes and asserts the
palette swap on chrome and no change in the sheet.

**W23-19  B-13 / decision 15  Task Information wiring.**  Task tab > Information
and double-click on a task cell open Task Information (six tabs) as Project does;
move the "Task Fields" dialog to its own command (Custom Fields) or remove it.
Acceptance: `qt_dialog_gate` opens the dialog through the action and the
double-click path, not only programmatically.

### 2.4 The wave-22 ranked items that remain

**W23-20** Dialogs 14/15 (the rest of the inventory) and the split view 16
(View > Timeline / Details checkboxes, docked task form, splitter).
**W23-21** New-task writer (needs a Project acceptance fixture); macOS smoke and
notarization remain an admitted gap (no Mac on this box); sf248 task recovery.
**W23-22** Clipboard display-order limitation, render-selection workaround.

## 3. Fixes where the obvious change is wrong (verifier corrections)

| finding | tempting fix | why wrong | correct fix |
|---|---|---|---|
| A-2 pins | change the 4 pins from "days" to "d" and ship | the port's label rule, not the pin, is what differs; and the rule for other units is unknown | pin from COM `Task.Predecessors` strings; make the formatter follow Project's unit labels; test each unit |
| A-4 suite | point docs at `build/e1` again or delete `task-calendar` | hides the stale-exe trap; drops a passing scenario | `--apply-bin` from the current build tree, as a ctest lane |
| A-4 8/8 | edit the oracle JSON to 9600 or special-case uid 3 | the oracle is Project's recorded output | held-out Project measurement + reopen read; otherwise leave it failing and say so |
| A-7 golden | delete the golden writer | the coordinator must still refresh it | opt-in env var + diff-before-write |
| B-1 order | sort by uid or by Start | neither is Project's order | read ID/outline from the file; row order == ID order; outline from the outline level/parent fields |
| B-2 timeline | hide the strip for all loaded files | new projects show it | follow the stored view state |
| B-4 colours | hard-code project_a's bar colours globally | they come from the file's bar styles | decode bar styles; fall back to Project's stock palette only when the file has none |
| B-12 dark | repaint only the ribbon | status bar/backstage/title stay light | one palette swap on every chrome surface; sheet/chart untouched |
| A-1 reference | compare the port to its own earlier render | not a reference | a Project capture of a client-free fixture, committed |

## 4. Decisions

All decisions this wave depended on were made by the operator on 2026-10-23 (section
6).  None are pending.  If a new one arises (for example a Project behaviour the
captures do not settle), record the question and the default you took in
`wave23-complete.md`; do not block.

## 5. Verification gate (before `wave23-complete.md` says done)

1. Every W23 item: fixed with commit id, or deferred with a reason, in a table.
2. The gates in section 1 green on fresh trees, with the exact numbers.
3. A second, separate pass over `git diff main..wave23` by a fresh session or
   subagent looking for regressions and for anything that removed, weakened or
   re-targeted a gate; its findings fixed or listed.
4. One CI dispatch on `wave23`, green on Windows/macOS/Linux, with the run id.
5. The live check: the port and Project 2021 opened on the same copy of
   `corpus/project_a.mpp`, screenshots of both saved under
   `corpus/reference/<date>-wave23/` (git-ignored), and a one-page table of what
   still differs, in `wave23-complete.md`.
6. `docs/START-HERE.md`, `docs/handoff/INDEX.md`, `memory/coordinator.md` updated;
   the 16-decision scoreboard re-scored honestly (audit column in
   `audit-w22-findings.md` section 4 is the starting point).

## 6. Operator rulings (closed - do not reopen)

1. Target is 100 % parity with the installed Project 2021; no "good enough".
2. Fable audited; DeepSeek runs this work order as coordinator with full access
   (corpus, COM oracle, git, one CI dispatch).
3. No ask-first queue: fix everything, report after; the gates and the verifier pass
   on the diff are the backstop.
4. This work order is wave 23; wave 22 is not reopened except where the audit found
   it broken.
5. The spec's deliberate limits stand: our icons and artwork (but real, distinct
   glyphs - B-5), the OS window frame, the port's own identity.
6. macOS launch stays an admitted gap.

## 7. Talking to the auditor

The Fable session that wrote this can be reached through Tim; a relay to DeepSeek
exists at `tools/deepseek/relay.py` and the CLI launcher at
`tools/deepseek/deepseek-claude.cmd` (reads the key from
`%LOCALAPPDATA%\deepseek\api_key.txt` or `DEEPSEEK_API_KEY`).  Questions about a
finding go in `wave23-complete.md` under "Questions for the auditor" with the default
you took.  The first round of questions was answered below (section 8).

## 8. Auditor answers to the fix session's read-back (2026-10-23, Fable)

DeepSeek's read-back of this brief raised 18 questions and 9 feasibility notes.
Answers are binding; where a default was proposed and is not corrected here, the
default stands.

Q1 bar palette fallback: when a file's bar styles cannot be decoded, use Project's
STOCK defaults (blue task bars, black summary bars, black milestone diamonds) keyed
off summary/critical/complete flags, with "decoded-from-file" in the backlog.  Never
the clean capture's green/navy: those are that file's styles.
Q2 timeline: a new project (File > New, no file) shows the Timeline because Project's
default view is "Gantt with Timeline" (Options > General capture); a loaded file
follows its stored view state, which for project_a hides it.
Q3 yes: `PLAN_RECALC_UPDATE_GOLDEN=1`, exact name, documented in build.md and ci.yml.
Q4/Q5 one new synthetic fixture serves W23-1, W23-7(d) and W23-8(d): built in
Project 2021 through `oracle_lock.py` on a COPY, 3-level outline, one FS+lag link,
one SS link, one FF link, one synthetic resource ("Resource A") assigned, saved by
Project, committed under tests/data with its hash in check_fixtures.py and README.
Q6 yes: if the app's save path cannot be wired this wave, say "test-only", leave
constraint-mso failing, backlog it.  Never write "deliberately not scored".
Q7 JSON, keyed tab > group > control (kind, label, order, enabled, icon key), read
by both the builder and the gate.
Q8 ci.yml already has `workflow_dispatch`; it is simply never dispatched.  Dispatch
both workflows at wave end (budget: one dispatch per workflow, two runs recorded).
Q9 do not commit bytes from Microsoft's binary.  Commit a derived TSV of the
property-name table (index, name) under analysis/ as the other derived tables are,
with the generating script under scripts/, and test the reader against that TSV.
Q10 migrate "Task Fields" to the Custom Fields command where Project has it:
Project > Properties > Custom Fields and Gantt Chart Format > Columns > Custom
Fields; nothing else opens it.
Q11 Task Form docked below, splitter from the stored view state; View > Details
toggles it.
Q12 separate sub-item in the same commit; backlog with reason if undecodable.
Q13 report-only, per-region percentages (title, ribbon, timeline, sheet, chart,
status), no CI failure this wave.
Q14 confirmed from this audit's captures: in Black and Dark Gray only the chrome
darkens; sheet and chart keep the file's colours.  Assert chrome palette swap and
sheet/chart pixels unchanged, tolerance 0.
Q15 a synthetic resource name is acceptable; the check is non-empty and equal to
Project's own Resource Names string for that row (read through COM when the fixture
is built).
Q16 all three: Project version string, recorded-at (UTC ISO-8601), sha256 of the
source .mpp.
Q17 restore each command to Project's actual home: Save as PDF = File > Export >
Create PDF/XPS and File > Print; Task Sheet, Resource Sheet, Resource Usage = the
View > Task Views / Resource Views drop-downs ("Other Views" where Project puts
them).  No extra ribbon buttons.
Q18 literal "Ready", shown in every theme (the Black-theme capture shows it).

Feasibility notes, answered:
- parity in CI: per-OS references, or gate the Windows runner only and report the
  other two; say which in wave23-complete.md.
- `--apply-bin`: use `$<TARGET_FILE:engine_diff_tests>` in CMake; agreed.
- macOS: CI-only, reported as a gap; agreed.
- 8/8: Q6 default applies.
- 900 px: a self-consistency check, not parity; label it so.
- lag labels: settle with COM `Task.Predecessors` strings on at least four unit
  variants before touching the formatter; agreed.
- decision 12: add an "audit note" line under the decision in ui-parity-spec.md
  stating what is implemented (report-only score); do not rewrite the operator's
  decision text.
- Options and ribbon enabled sets both read the independent pin file; agreed.
- gates: once per commit, per-lane commits; record exact counts each time.
