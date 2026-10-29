# Progress assessment and adversarial audit - 29 October 2026

_Written by the Fable session on 29 October 2026 at the operator's request ("have a look over the
state of the MS Project port ... what still needs to be done ... an adversarial audit at
the same time").  Baseline for every statement and finding: `main` = **17f5782** in
`D:\dev\msproject-mac` (= `origin/main`; CI green on it, runs 40469833962 and
40469223613).  The previous assessment is `assessment-2026-10-24.md` (after wave 26).
No client data is quoted; rows are referred to by ID._

## 1. Bottom line

- **Waves 27 to 32 ran unattended between 24 and 27 October and are merged.**  `main`
  moved from `fcb1c10` (wave 22 plus the audit) to `17f5782`, which contains waves 23 to
  32, after a second history rewrite that swapped seven fixture blobs for cleaned ones.
  CI is green on the merged tree on all three operating systems.  The old wave branches
  are gone from GitHub.  This is the first time since 23 October that `main` and the
  verified work agree.
- **The port is now a faithful reader and renderer and a narrow, measured editor.**
  Reading, scheduling and the Gantt rendering are strong and measured against Project.
  Every enabled save path is pinned to a Project-saved pair and refuses any shape it did
  not measure.  That discipline (standing rule 1) held through five independent reviews:
  the wave-31 and wave-32 reviews found 0 Critical, and wave 32's review found 0 High.
- **"100 % parity" is still a long way off, and the shape of the gap changed.**  Before
  wave 27 the gap was unknowns.  Now it is a known list: 69 of 114 ribbon commands are
  disabled with a reason, 15 of them *measured and deferred* because Project's save for
  them is a scheduler rewrite or needs a writer the port does not have (VarMeta
  insert/remove, the `214` view-stream writer, baselines, calendars, RichEdit notes).
  No writer was added in waves 31 or 32.  Four whole view families (Team Planner,
  Network Diagram, Calendar, Reports) are out of scope so far.
- **The records lag the tree.**  `docs/START-HERE.md`, `memory/coordinator.md` and
  `docs/handoff/INDEX.md` still say `main` = `fcb1c10` and "nothing merged".  The merge
  happened after they were written and nobody updated them.  Dropbox
  `Projects/MS Project Decompile` is a stale second clone (branch `wave30`, 26 Oct) that
  still carries a multi-gigabyte `build/` tree.  The master `MEMORY.md` has no entry for
  any port work after 24 October, and the "thorough status report" the operator asked for at the
  end of wave 31 exists only in a chat transcript.  This document is that report.
- **The audit (section 7) is in progress** at the time of writing; its findings and the
  work order are appended as they land.

## 2. What happened since the 24 October assessment

| wave | scope | outcome | review (Critical / High) |
|---|---|---|---|
| 27 | save safety, CI back, decoding evidence | % Complete disabled until measured; Priority kept (measured); CI green on all three OSs | 0 / 2 (fixture paths carried the operator's name; outline numbers) |
| 28 | editing core: actuals writer, indent/outdent, new-task mode, Task Form, split | every item DONE but split (measured and deferred); the branch was rewritten once with the operator's approval to remove the operator's name | 0 / 3 |
| 29 | wave-28 Highs, PROJECT_A visible gaps, six format dialogs, Page Setup, dark chrome | every item DONE; Project reopened 10 port saves with 0 differing fields | 1 / 6 (indent could delete a real zero-work assignment; six unmeasured shapes) |
| 30 | the wave-29 Critical and Highs, then the Task, Resource and Project tab commands | G0 closed all eight; of 24 commands, 9 DONE (mostly view-only or "stays disabled with the exact tooltip"), 15 DEFERRED with a committed Project pair each | 0 / 4 |
| 31 | the wave-30 Highs, the View tab, Resource Sheet and both Usage views at Gantt fidelity | every item DONE; all view state is session-only (file bytes kept) | 1 / 4 (the Critical was a record claiming a push and CI run that had not happened; fixed in CLOSE) |
| 32 | fix-only: the wave-31 Highs | three refuse-first fixes; no writer, no measurement | 0 / 0 (2 Medium, 5 Low) |

Then, 27 October 17:00 to 18:46: the verifier merged `wave32` into `main`.  The pre-push
guard refused the first merge because seven old fixture blobs in `fcb1c10..wave32`
carried the operator's save path or name, so the range was rewritten
(`git filter-branch`, 329 commits remapped in `wave32-verification/rewrite-sha-map.txt`),
`wave23-outline.mpp` was neutralised in place, and `main` was pushed.  Consequence: every
commit id quoted in the wave 23 to 32 records is an old id.

## 3. Where it stands against the 16 decisions

The scoreboard was last re-scored at the wave-31 close and wave 32 changed nothing:
**10 DONE, 6 PARTIAL, 0 NOT DONE** (24 Oct: 9 / 7 / 0; the wave-22 audit: 4 DONE, 8
PARTIAL, 2 NOT DONE, 1 FAILING, 1 UNVERIFIED).  "DONE" means the shape matches Project.
The honest qualifier is unchanged from 24 October: the share of commands that actually
*do* something is far lower than the share that *look* right.

## 4. Engine, file and UI fidelity

| area | state at 17f5782 | evidence |
|---|---|---|
| reader | strong | 1136 checks with the corpus, 456 without; MPP12 and MPP14 streams, view state, styles, timescale, stored tables, resource Entry values, time-phased usage decoded from controlled Project saves |
| scheduler | strong on what is measured | M2 recalculation of the real file 332/332; engine suite 7 pass + `constraint-mso` expected-fail (deliberate) |
| Gantt / sheet rendering | strong | PROJECT_A renders in Project's order with its outline, text styles, bar styles, yellow fill, bar labels, stored column widths, timescale origin; parity harness PASS on both fixtures |
| second view family | new, read-only | Resource Sheet, Task Usage, Resource Usage painted from the file and gated against COM facts |
| writer | **narrow, measured** | enabled saves: field values and custom fields, % Complete (three measured transitions on automatic rows), Mark on Track / Update Project, link add/remove, insert/delete task, indent/outdent (measured shapes), task mode (manual direction), project Priority, custom-field formulas, two view-state edits.  Everything else refuses up front. |
| measured-and-deferred writes | 15 commands | Insert Summary/Milestone, Inactivate, Respect Links, Move, Notes, Add to Timeline, text styles, Add Resources, Resource Information/Notes, Assign Resources, levelling, Calculate Project, Set Baseline, WBS: each has a committed Project pair showing a save shape the port cannot yet produce |
| Start editing | **read-only since wave 32** | the wave-31 Fill pair showed the Start writer cannot reproduce Project's save (dates, slack, roll-up, DSI strings), so Start was made read-only at every shell entry |
| macOS shell (`app/main.mm`) | compiles in CI, never run by a person | `app-macos.yml` builds it; no manual smoke test on a Mac has ever been recorded |
| Qt shell | the working shell | 37 ctest gates including live-widget parity gates on Windows, Linux, macOS |

The whole fixture set is synthetic plus two client files (one drives most acceptance).
Fixtures: 173 tracked `.mpp`, 139 with documented hashes.

## 5. What remains for parity, in order

| # | item | size | note |
|---|---|---|---|
| 1 | **Bring the records up to the tree**: START-HERE, coordinator memory, INDEX say unmerged; the Dropbox clone; the master memory gap | small | hygiene, but every new session reads these first and will be told the wrong state |
| 2 | **The shared writers** the 15 deferred commands wait on: VarMeta insert/remove, the `214` view-stream writer, Props-record insertion, table growth with re-keying, a `DocumentSummaryInformation` writer | medium each | the single biggest unlock; nothing in the Task/Resource/Project tabs moves without them |
| 3 | **A scheduler save path**: Calculate Project, Respect Links, Move, Auto Schedule, Level All all save Project's reschedule.  The engine exists (`mppwrite::save_plan` is test-only); measuring Project's save of a recalculation on a linked file is the gate | large | this is where "parity" is decided: without it the port can display a schedule but never change one |
| 4 | **Start / Finish / Duration editing** against a measured pair on a linked file | medium | depends on 3; today Start is read-only and Finish/Duration never save |
| 5 | **View-state persistence**: sort, table, outline level, zoom, window arrangement into the file | medium | all session-only today; each needs a Project pair |
| 6 | **Baselines, calendars, WBS, resource rows** (the "no-writer engines") | medium-large | decision 14/15 depth |
| 7 | **The four missing view families**: Team Planner, Network Diagram, Calendar, Reports | large | out of scope so far; Project is more than the Gantt |
| 8 | **The review queue**: R30-5..R30-14, R31-5..R31-12, R32-1..R32-7 (Medium/Low) | small each | listed in `plan-waves27-30.md` |
| 9 | **Privacy residuals**: 71 tracked files carry the operator's name, 45 carry a user-profile path, `synthetic-base.mpp` carries the name, 13 unsalted 64-bit hashes of client task names in `project_a_golden.tsv` and older history, GitHub still serving pre-rewrite commits by hash | small | the repo is public; see the audit's lane B |
| 10 | **macOS launch**: a person opens the `.app` on a Mac, notarisation | small-medium | the stated end state is a native Mac app; it has never been run by a person |

## 5a. Progress to parity: about 50 % of the behaviour surface, about 35 % of the effort

The operator asked for a percentage.  There is no single honest number, so here are three, each
with its basis; the headline is the first.

**By behaviour surface (weighted areas of what Project does for one user): about 50 %.**

| area | weight | done | basis | contributes |
|---|---|---|---|---|
| open any `.mpp` and read it right | 15 | 85 % | MPP12/14 streams, views, styles, tables, usage decoded; two client files only | 12.8 |
| scheduling engine (recalculation parity) | 15 | 70 % | 332/332 on the real file, suite 7/8; no levelling, no scheduler save | 10.5 |
| Gantt and sheet rendering | 10 | 90 % | measured against captures; pixel score not re-run | 9.0 |
| Resource Sheet, Task Usage, Resource Usage | 5 | 70 % | rendered from the file, COM-gated; presentation gaps | 3.5 |
| Team Planner, Network Diagram, Calendar, Timeline view, Reports | 10 | 5 % | the timeline strip only | 0.5 |
| editing and saving (the point of the product) | 25 | 20 % | field values, %, links, insert/delete, indent/outdent on measured shapes; no Start/Finish/Duration, no scheduler save, no resources, assignments, baselines, notes | 5.0 |
| ribbon, backstage, dialogs, commands | 10 | 50 % | shape 90 %, function 41 % (47 of 114 commands enabled) | 5.0 |
| interchange and print (MSPDI, CSV, PDF, Page Setup) | 5 | 60 % | writers exist; not every option measured | 3.0 |
| a native Mac app a person has run | 5 | 20 % | compiles in CI; never launched by a person; no notarisation | 1.0 |
| **total** | **100** | | | **50.3** |

**By the project's own 16-decision scoreboard: 10 of 16 DONE, 6 PARTIAL, about 62 %
by shape** (the records' own caveat: DONE means the shape matches, not that the command
works).

**By remaining effort: about 35 %.**  The 24 October assessment put four waves at about 7
dollars of DeepSeek and 25 to 30 hours of wall time; waves 27 to 32 took about 20 hours
more.  What remains (section 5 rows 2 to 7) is the shared writers, a scheduler save path,
view-state persistence, the no-writer engines and four view families, plus the long tail
(levelling, splits, EVM, custom outline codes, printing options, every dialog's semantics).
Each of those is at least a wave 28-sized item, several are two or three, and the
scheduler save path is the one with real unknowns.  A fair estimate is 40 to 60 more
hours of driven work to reach "every common command writes what Project writes", and an
open-ended tail after that for the literal 100 %.

The three numbers move differently: wave 33 (fixes) moves none of them much but removes
the two ways the port can lose a user's file; wave 34 should move the surface figure to
about 58 % (editing from 20 % to 40 %, commands from 41 % to about 55 %).

## 6. On `w0ldan/exoproc`

The operator asked whether github.com/w0ldan/exoproc would help this assessment.  **No, not for
the assessment, and not yet for the port.**

- **What it is.**  A TypeScript/Bun toolkit (npm `exoproc` 0.6.10, 32 MB unpacked, on
  npm since July 2026) and a 22-tool MCP server for *live* Windows x64 process work:
  attach to a running process, read/write/scan its memory, install MinHook detours on
  functions, hijack a thread to call functions inside the target, JIT C to shellcode,
  screenshot windows.  The GitHub repo was created on 28 October 2026 (two commits, one
  author, 42 stars, MIT), and the recommended install is `irm https://... | iex`.
- **Why it does not help the assessment.**  The assessment is about the port's own code,
  records and tests.  Exoproc looks at a running `WINPROJ.EXE`; it has nothing to say
  about the port.
- **Where it could help the port later.**  The port's hardest remaining items (section 5
  rows 2 to 4) are "Project's save is a scheduler rewrite we cannot reproduce from the
  bytes alone".  Today the evidence is black-box: COM edit, save, diff streams.  Live
  instrumentation could add a glass-box view: hook the serialisation entry points found
  in the Ghidra model (`TBkndRecalc`, the `backend.serialization` module) and record
  which routine writes which bytes, or read the in-memory TASKNODE/flat-buffer records
  the Claude Code memory already maps.  That is a real capability the project does not
  have, and it would speed up decoding the deferred shapes.  But Ghidra static analysis
  plus the COM oracle has been enough so far, and each deferred item is blocked on
  *writing* a shape, not on *understanding* it.
- **Why not now, and not on this box.**  (1) It is a day-old public release of a tool
  that injects code into other processes, installed by piping a remote script into
  PowerShell, on the company workstation that holds the company Dropbox, the M365 tenant
  sessions and the Site Portal credentials.  Audit the source first, pin a version,
  install from npm (not the `iex` one-liner), and run it only in a throwaway VM with its
  own Project licence.  (2) Hooking Project 2021 is debugging a licensed binary; the
  project's "personal use, never distributed" framing covers the port, but this would
  be a new line to think about.  (3) The Ghidra project (770 MB) and 2.59 M lines of
  decompiled C already give the static map; the missing piece is dynamic tracing, which
  exoproc provides but which also exists in x64dbg/WinDbg Time Travel Debugging with
  no supply-chain exposure.

Recommendation: note it as a candidate for a future "scheduler save path" wave, do not
install it now.

## 7. Adversarial audit (baseline 17f5782)

_Four read-only lanes ran against `17f5782` on 29 October 2026 (A save paths and write
parity, B privacy, publication and tooling, C record and gate honesty, D robustness and
data safety), then a refutation pass over every Critical and High in A and D, and the
coordinator's own re-check of the B and C Criticals and Highs.  Verdicts: 13 CONFIRMED,
4 CONFIRMED-NARROWER, 0 REJECTED.  Full tables with evidence and refutation steps:
`build/audit-w32/laneA-save-paths.md`, `laneB-privacy-tooling.md`,
`laneC-records-gates.md`, `laneD-robustness.md`, `verify-AD.md` (git-ignored, local).
Severity: Critical = a saved file Project reopens with wrong data, a file Project cannot
open, the user's file lost or truncated, or client or personal data exposed publicly
now; High = an enabled unmeasured write, a bypassable guard, a hang on a malformed file,
or a record that is false about state that matters; Medium = loud failure, wrong
in-session output, stale records; Low = hygiene._

**Counts after verification: Critical 3, High 10, Medium 14, Low 17.**

### Critical

| id | finding | evidence (file:line at 17f5782) | verdict |
|---|---|---|---|
| A-1 | **A second Save after a formula-only or date-format-only save silently discards that save, in both shells.**  `Document::save` always derives the output from the ORIGINAL file (`app/document.cpp:567`); the non-structural route sets the remembered target but never reloads or re-points the document (`app_qt/main_window.cpp:3243-3262, 3311`; `app/main.mm:4085-4109`), so the next Save rebuilds the target from the original and the formula or date format just saved is gone.  Also: formula, Save As, Insert Task, Save = formula lost. | code read; the structural route (`open_file:1198`) does reload, the non-structural one does not | CONFIRMED |
| A-2 / D-1 (+D-4) | **No save is atomic.**  Every writer opens the final path with `ios::trunc` and writes directly (`core/mppwrite/cfb_writer.cpp:1257-1269`, `inplace.cpp:204`, `save.cpp:489, 598`); `grep rename app/ core/` = 0 hits.  After the first structural save both shells set `owns_path_` and reload the target as the open document (`main_window.cpp:3234/3246`, `main.mm:4087/4217`), so every later Save overwrites the only copy in place.  An I/O failure or process death between truncate and the last byte leaves a truncated `.mpp`; the alert "The opened file was not modified" is then false.  The `.w51-structural-tmp` intermediate protects earlier steps only; a reread mismatch after the final write (D-4) also leaves the just-written file. | code read; narrower than lane D wrote: `to_bytes()` and every refusal run BEFORE the stream opens, so a refused save never truncates, only an I/O failure does | CONFIRMED-NARROWER (still Critical) |
| B-1 | **The pre-scrub history is still public and still carries client terms.**  All four pre-scrub tips named in the unsent support-request draft (old `main` 3f973d5f, `wave23` 63cd087d, `w20-q5` 76aec744, `wave1/*` 14015292) return 200 from the GitHub API; none exists locally.  Diffing old `main` against its post-scrub twin `fcb1c10`: 11 blobs differ and all 11 carry client terms (16 distinct corpus labels).  725 of 944 short hashes quoted in tracked docs do not resolve locally and sample probes resolve on GitHub to 21-22 Oct commits, so the public docs point readers at the pre-scrub history.  `build/audit-w22/scrub/github-support-request.md` is still a draft (TASKS row of 24 Oct, default 31 Oct). | `gh api .../commits/<sha>` re-run by the coordinator 29 Oct: all four 200 | CONFIRMED |

### High

| id | finding | evidence | verdict |
|---|---|---|---|
| A-3 | **Delete Task is accepted on a summary row** everywhere (`push` is plan-free, `app/edit_model.cpp:2495-2512`; `delete_task_row` has no children check, `core/mppwrite/add_delete.cpp:1033-1120`; both shells check only the selection) and tombstones the summary alone, leaving children with has-parent set and a parent identity pointing at a tombstone.  No Project pair measured a summary delete; `app_edits_outline_tests.cpp:2046-2055` asserts the summary delete is QUEUED. | the port's own reader flattens the orphans to level 0 with a warning (`reader.cpp:1578-1587`); Project's reopen unmeasured | CONFIRMED-NARROWER (Critical -> High: consequence in Project unknown) |
| A-4 | **The `0x02401392` flag heuristic is unmeasured and probably misread.**  Project's own property table names it `DOCHASALIAS` (`docs/handoff/e7.md:122`), not "custom-field data in use".  `custom_field_value_remains` (`app/document.cpp:93-158`) checks Task custom VarMeta keys, the Flag1 bit and assignment keys only (not Flag2..20, resources, aliases); the clear path writes 0; no Project pair exists for a formula clear (`w59.md:246-266` lists the meaning as open).  Enabled in both shells. | code and record read | CONFIRMED |
| A-5 | **Formula SET is enabled for every Task field kind**; only Text and Number were measured and the formula grammar is recorded as open. | code read | CONFIRMED |
| A-6 | **Insert Task and custom-field set/clear have no committed Project pair and no test against Project's file**: every test re-reads the port's own save of `synthetic-base`.  (Delete Task and link ADD are Medium, not High: `tests/roundtrip_add_delete.cpp:1130-1235` and `tests/roundtrip_links.cpp:853-960` DO diff them byte-for-byte against Project's corpus files, but only when `corpus/` is present, which CI never has.) | verifier found the corpus diffs lane A missed | CONFIRMED-NARROWER |
| A-9 | **Insert Task clones the LAST live row, including its % complete, actual dates, duration and work** (`core/mppwrite/add_delete.h:72-100`; the cascade passes only name/mode/uid=0).  After a progressed last row the new task is saved in progress without the `0x0B40015E` block Project writes. | code read | CONFIRMED |
| B-2 | **Two tracked `.mpp` files carry the operator's name as Author and LastAuthor** (ASCII and UTF-16LE): `tests/data/synthetic-base.mpp` and `analysis/test_project.mpp`; the second is outside `tests/data`, so `check_fixture_paths` never scans it.  171 of 173 are neutral; `wave23-outline`'s 27 Oct neutralisation is confirmed clean at HEAD. | lane B's own property-set reader, output `laneB-mpp-props-HEAD.txt` | CONFIRMED |
| B-4 | **The only client-name guard is bypassable and not in the repo**: the untracked `.git/hooks/pre-push` imports the git-ignored `build/audit-w22/guard_names.py`, which needs the local corpus and `reader_dump.exe`.  A fresh clone has no hook; `--no-verify` skips it; it scans the pushed range only.  CI runs `scripts/check_fixtures.py`, which checks hashes and local paths under `tests/data` only: no name check, no coverage of `analysis/*.mpp`, no commit-message check.  The `scripts/scan_client_names.py` the records cite does not exist in the tree (B-12). | hook read; `ls scripts/scan_client_names.py` fails | CONFIRMED |
| B-8 | **The DeepSeek driver runs an autonomous agent with full write access on the company box.**  `tools/deepseek/run-wave.ps1:47,49` runs Claude Code headless with `--permission-mode auto`, Bash/PowerShell/Write/Edit/WebFetch, up to 40 iterations per group and no human gate, on the workstation where `corpus/` (client `.mpp`) and the company Dropbox are readable; everything it reads goes to DeepSeek's API.  `deepseek-claude.cmd` exports the key as `ANTHROPIC_AUTH_TOKEN` into the agent's environment (readable by any Bash call, exfiltrable via WebFetch under prompt injection); `--verbose` transcripts and relay session JSON land in `build/deepseek-sessions`.  No tracked `.claude` settings constrain the sessions.  Not `--dangerously-skip-permissions`, but equivalent for this threat. | script read | CONFIRMED |
| C-1 | **The three entry-point records are false about merge state.**  `docs/START-HERE.md:85-93`, `memory/coordinator.md:9-14, 119` and `docs/handoff/INDEX.md:16-37` say `main` = `fcb1c10`, waves 23-32 "closed and not merged", `origin/wave32` exists, "next step: verify and merge".  Everything was merged on 27 Oct after a history rewrite, the remote wave branches were deleted and CI ran green on `main`.  Only `wave32-verification/gates.md` says so, and it is not in the reading order.  Every commit id quoted in the wave 23-32 records is a pre-rewrite id. | coordinator re-checked | CONFIRMED |
| C-2 | **The `app/` suite never runs in CI.**  All 25 app ctest entries, including every save-pair gate against a Project-saved file (actuals 3782 checks, outline 3230, task mode, new-task mode, links, fields), run on no CI job on any OS: `qt.yml` builds only `app_qt` (line 169) with `APP_BUILD_TESTS OFF`, the root tree `ci.yml` configures has no `app/` (`CMakeLists.txt` has no `add_subdirectory(app)`), and `app-macos.yml`, the only workflow that runs them, is dispatch-or-tag only and last ran on `wave27` on 24 Oct, naming 15 of the 25 tests.  The CI green for waves 28-32 and `main` never included these tests; the records quote "app 25/25" as a fresh-tree gate.  (The macOS Qt job does compile `app/main.mm`: that claim is true.) | coordinator re-checked the three workflows and the root CMake | CONFIRMED |

### Medium (one line each; detail in the lane files)

- A-7: Task Information and Task Fields ignore the `push()` return, so refused field edits are dropped while the status says queued.
- A-8 / D-7: a file with FAT slack (MS Project writes them) opens and is editable all session, then every Save fails because `to_bytes()` refuses slack.
- A-10: the `214/CTable` column-replace writer has no Project diff (the date-format writer does: `tests/viewstate_tests.cpp:2261-2320`).
- A-11: table edits hard-code `table_index = 0`, so the wrong table changes in a two-table file.
- A-6 (part): Delete Task and link ADD are Project-diffed only against corpus files that CI never has; nothing committed under `tests/data`.
- B-3: 69 tracked text files publish the operator's name, the company Dropbox folder and the Windows username; 49 `scripts/phase*.py` hard-code the operator's absolute paths.
- B-5: `tests/data/project_a_golden.tsv` names are the first 64 bits of UNSALTED SHA-256 (not FNV-1a; `scripts/ref_reader.py:130`), beside real durations, dates, priority and % complete.  A 10^8-candidate construction-task dictionary runs in about a minute in Python and under a second on one GPU.  The client programme's skeleton is public in clear.
- B-7: the tracked `rewrite-sha-map.txt` publishes 329 old ids whose commits GitHub still serves (with the un-neutralised operator-name blobs; 0 client terms in that range).
- C-3: the three merge commits reintroduced CRLF (`plan-waves27-30.md` 153/153 lines, `scripts/check_fixtures.py` all 849 lines, `gates.md`), undoing W32-C1; `gates.md` claims the tip differs from the verified tip "only in wave23-outline.mpp".
- C-4: 21 of 31 Qt gate sources load no reference and assert the window against the shell's own `shell_style.h` constants; the committed light-theme "reference" declares its source as `app_qt/shell_style.h`.  Real Project-derived checks: 10 gate sources plus the CI-only `parity_report.py` step (10 geometry measures and one bar colour).
- C-5: `save_pair_noise.h` masks the whole 16-byte block around any per-save noise byte and everything beyond the null save's length; `effects` is printed, not asserted (except task mode), so a fully masked stream passes with 0 effects and 0 mismatches.
- C-6: 153 SKIP sites exit 0 on a missing fixture, at least 25 guarding committed save-pair controls (mitigated by `check_fixtures.py`; 0 fired today).
- C-7: the verifier's wave-32 review (R32-1, R32-2) is missing from START-HERE, which reports only the CLOSE session's own review.
- D-2: the writer-side CFB parser's DIFAT walk has no visited set and no file-size bound (`cfb_writer.cpp:321, 347-361`; the reader at `cfb.cpp:258-281` has both); a self-pointing DIFAT with `num_difat` = 0xFFFFFFFF spins to `bad_alloc`.  Medium because the app parses with the guarded reader first; High for any direct `CfbDocument::from_file` caller (tools, tests, a file changed after open).
- D-3: fuzzing never feeds `CfbDocument::from_bytes`, the MSPDI XML or the CSV importers; no sanitizer job in any workflow.

### Low (grouped)

A-12 one pending formula op, a second overwrites silently.  B-6 one commit message on `main` (`6056f15`) quotes corpus label N17 (a generic-looking section header; operator to rule).  B-9 actions on floating tags (mitigated: no `pull_request` trigger, `contents: read`, no secrets).  B-10 artifacts always uploaded, 90-day retention.  B-11 local `wave23-pre-push` and `wave23-with-screenshot` still hold client terms; `--all` or `--mirror` would republish.  B-12 the scanners are untracked.  B-13 14 older oracle scripts save without neutralising metadata.  B-14 `relay.py` key handling otherwise sound.  B-15 62 unexpired CI artifacts built from pre-scrub commits (zips ship only the Qt exe and runtime; unverified).  C-8 CI runs 35 Qt tests, records say 37.  C-9 "69 disabled" counts two enabled split buttons; pin truth 47 enabled / 67 disabled.  C-10 57 named EXPECTED FAIL lines run today (honest anti-rot guards, never counted in the records).  C-11 the first CI ctest pass runs corpus-aware tests without `MPP_NO_CORPUS`.  C-12 `qt_render_parity` passes on "rendered 1920x1032", not a comparison.  D-5 (reproduced) a Resource Fixed2Meta count of 0xFFFFFFFF makes a 4.3-billion-iteration `continue` loop: `reader_dump` 0.02 s -> 2.2 s, same output.  D-6 CFB v4 sector offset formula wrong and the whole-sectors check rejects every real v4 file.

### Checks that passed

The read-only CFB reader: bounded FAT/DIFAT/mini-FAT/directory walks with visited sets and step limits, all accessors bounds-checked, VarMeta/Var2Data/FixedMeta/Fixed2Meta offsets range-checked in u64, outline walked iteratively, calendars and Props bounded.  Writer growth (`replace_stream`/`layout`/`to_bytes`) updates FAT, DIFAT (beyond 109 FAT sectors by formula), mini FAT, root and header; lane D's independent [MS-CFB] walker passes all six existing writer outputs including the FAT-growth and mini-cutoff cases.  MSPDI XML: DOCTYPE skipped wholesale, depth cap 256, numeric parsing range-checked.  Scheduler: Kahn topological sort with an explicit cycle error and a pass limit, no recursion.  Undo/redo cannot rebuild a refused pair.  Start is refused at every entry.  Percent, actuals, outline, task mode, Priority and link delete have real Project pair tests.  HEAD tree: 0 client-term hits across 1283 blobs in three encodings; all 3226 blobs reachable from `main`: 0 client terms.  CI: no `pull_request` trigger, `contents: read`, no secrets, `workflow_run` follows only the same repo's `qt-shell`.  Both CI run ids in the wave-32 record are real and green on `7727760`; three green runs on `main` at `17f5782`.  Gate numbers reproduced on the existing fresh trees: core 52/52, plan_recalc 512/266, reader 1136/456, app 25/25, app_qt 37/37, fixtures 139/173/222/8, backlog 114/114.  `constraint-mso` is a documented, nine-times-measured Project save-time recomputation the port chose not to model: honest.

## 8. Work order (wave 33, fix-only)

_Issued as `docs/handoff/wave33-brief.md` (this work order in the wave-brief shape) and
`docs/handoff/wave34-brief.md` (the parity wave: decode Project's save shapes with the
committed pairs, the Ghidra model and COM, build the shared writers, enable the
measured-and-deferred commands), both written 29 Oct 2026._

_Baseline `17f5782`.  Standing rules bind.  One finding id per commit; a fixture commit
carries fixtures only; LF.  Nothing here adds a writer or lifts a refusal._

### Authorised now

| # | finding | change | acceptance |
|---|---|---|---|
| 1 | A-2 / D-1 | Every `.mpp` write goes to `<target>.tmp-<pid>` in the target's directory, is reread and self-checked, then `std::filesystem::rename` over the target (`cfb_writer.cpp` `CfbDocument::save`, `inplace.cpp:204`, `save.cpp:489/598`).  On any failure the temp is removed and the target is untouched. | a test that injects a write failure (unwritable temp dir, or a stream that throws) leaves the target byte-identical; a reread-mismatch test leaves the target byte-identical; `grep -c rename` > 0 |
| 2 | A-1 | The non-structural save route adopts the target exactly as the structural route does: reload the written file as the document, set `owns_path_`, so a second Save derives from what was saved. | test: formula save to T, second Save to T, reopen T: formula present; formula, Save As T, Insert Task, Save: formula present |
| 3 | A-3 | Refuse Delete Task when the row has children, in `push()` and in both shells, with a status-line reason. | the `app_edits_outline_tests.cpp:2046` assertion is inverted to "refused"; a shell test |
| 4 | A-9 | Insert Task refuses when the row it would clone carries progress, actual dates, work or assignments beyond the placeholder (the clone copies them). | test on a fixture whose last live row is 40 %: refused with reason; the measured flat insert still passes |
| 5 | A-5 | Formula SET accepts Text1..30 and Number1..20 only; every other field kind is refused up front with the decision-9 text. | one test per refused kind |
| 6 | A-4 | Formula CLEAR (the `0x02401392` write) is disabled with the decision-9 tooltip until a Project pair measures it; SET keeps the flag at 1. | the clear path is unreachable from both shells; test |
| 7 | A-7 | `apply_task_information` / Task Fields check every `push()` return; on the first refusal, undo the pushes made in that dialog and report. | test: queue an automatic indent, apply Text1 + 40 % on a row, `pending_edits().size()` unchanged |
| 8 | A-11 | The table edit addresses the table the view shows, not index 0. | test on a two-table fixture |
| 9 | D-2, D-5, D-6 | Bound `num_difat` by file size and add a visited set in the writer-side DIFAT walk; bound the Resource Fixed2Meta count by `available / 51`; refuse CFB v4 explicitly (or fix the sector offset formula). | lane D's `laneD_rsc_meta_hang.py` fixture parses in under 0.1 s; a self-pointing DIFAT fixture fails fast with an error |
| 10 | C-2 | Add the `app/` suite to CI: build `app` with `APP_BUILD_TESTS=ON` and `CORE_WARNINGS_AS_ERRORS=ON` on windows-msvc and ubuntu-gcc in `ci.yml`, run ctest; run `app-macos.yml` on push as well as dispatch. | CI log shows "25 tests" (or the current count) on both OSs |
| 11 | C-1, C-7 | Rewrite START-HERE "Branch state", `memory/coordinator.md` "Where the branches are" and "Next", INDEX headings 23-32: merged on 27 Oct at `17f5782` after the rewrite; note that quoted ids are pre-rewrite and how to map them; add R32-1/R32-2. | a reader of START-HERE alone learns the true state |
| 12 | C-3 | Restore LF on `plan-waves27-30.md`, `scripts/check_fixtures.py`, `wave32-verification/gates.md`; add a `.gitattributes` `eol=lf` rule for `*.md *.py *.yml` if not present. | `git ls-files --eol` shows `i/lf` for the three |
| 13 | C-5, C-6 | `save_pair::compare` asserts that the effect set is non-empty for every stream the pair is meant to change; SKIP on a missing COMMITTED fixture becomes a failure (keep SKIP for corpus-gated tests, printed with "corpus absent"). | the 25 committed-fixture SKIP sites fail when the fixture is renamed |
| 14 | B-2 | Neutralise Author/LastAuthor in `tests/data/synthetic-base.mpp` and `analysis/test_project.mpp` in place, same length (as done for `wave23-outline`); update the pinned SHA-256 and `tests/data/README.md`; extend `check_fixtures.py` to every tracked `.mpp` and to name markers at both UTF-16 alignments. | lane B's `laneB_mpp_props.py` reports 0 flagged at HEAD |
| 15 | B-4 (marker half) | Commit a CI check (`scripts/check_markers.py`) for the operator's name, Windows username, company folder and user-profile paths across every tracked file including binaries, ASCII and both UTF-16 alignments; run it in `ci.yml`.  The client-term list stays local (see Queued). | CI red on a planted marker, green on HEAD after item 14 |
| 16 | D-3 | Fuzz `CfbDocument::from_bytes` and the MSPDI/CSV importers with the same mutators; add an ubuntu ASan/UBSan job running the core suite. | sanitizer job green |
| 17 | A-8 / D-7 | At open, a file whose container has FAT slack is marked read-only in both shells with a status reason ("saved by Project with unreferenced sectors; saving is not supported yet"), so the user learns at open, not at Save. | test on the reader's cited slack case |

### Queued, ask the operator first

| # | finding | proposal |
|---|---|---|
| Q1 | B-1, B-7, B-15 | Send the GitHub support purge request today with the 329 ids from `rewrite-sha-map.txt` added to the four tips; delete the 62 pre-scrub CI artifacts; consider flipping the repo to private until GitHub confirms, and removing `rewrite-sha-map.txt` from the public tree once the purge lands. |
| Q2 | B-8 | Before the next DeepSeek wave: run the driver under a dedicated Windows account (or container) with no read access to `corpus/` or the company Dropbox; pass the key through `apiKeyHelper`, not the environment; deny WebFetch; rotate the DeepSeek key now. |
| Q3 | B-5 | Replace the SHA-256 name column in `project_a_golden.tsv` with an HMAC keyed by a secret kept beside the corpus, or drop the column (uid is the key) and coarsen dates to the week. |
| Q4 | B-3, B-6, B-11 | Purge the operator's name and paths from the 69 text files and 49 scripts (a tree change, not a history rewrite); rule on the N17 commit message; delete the two local pre-push backup branches. |
| Q5 | A-6 | Measure and commit synthetic Project pairs for Insert Task, Delete Task, link ADD and custom-field set/clear (coordinator-only COM work, about half a day), so the evidence is in `tests/data` and runs in CI. |
| Q6 | housekeeping | Delete the stale Dropbox clone `Projects/MS Project Decompile` (branch `wave30`, 22 GB `build/`), leaving a `MOVED.md` pointer as the Design Game did. |

### Fixes where the obvious change is wrong

- **A-2:** the tempting fix is "catch the exception and delete the partial file".  With `owns_path_` that deletes the user's only copy.  Write beside, verify, rename.
- **A-4:** the tempting fix is "compute the flag from a better value scan".  The flag's meaning is unmeasured (`DOCHASALIAS` per Project's own table); rule 1 says disable the clear until a pair measures it.
- **A-6:** the tempting fix is "disable Delete Task and link ADD as unmeasured".  They ARE diffed against Project's corpus files; the gap is that the evidence is corpus-only.  Commit synthetic pairs (Q5) instead of disabling.
- **B-4:** the tempting fix is "commit `guard_names.py`".  Its term list is the client names.  Commit a marker guard (item 15); keep the client-term guard as a local hook, and add a CI check that the hook's hash matches a tracked one.
- **C-2:** the tempting fix is "turn `app-macos.yml` on for push".  It only builds on macOS and names 15 of 25 tests.  Add `app/` to `ci.yml` on both OSs and fix the registration check.

### Decisions (with defaults)

| # | decision | recommendation | default |
|---|---|---|---|
| 1 | B-1: purge request and repo visibility | send today; private until confirmed | if not sent by Sat 31 Oct 2026, Claude asks once more on Mon 2 Nov 2026 and proposes flipping to private; the operator owns |
| 2 | B-8: DeepSeek sandbox | dedicated account, no corpus or Dropbox, key via helper, rotate now | no wave launches until decided |
| 3 | B-5: the golden hashes | drop the name column | if silent by Fri 6 Nov 2026, item Q3 "drop the column" is authorised |
| 4 | next scope | wave 33 = this work order (fix-only); wave 34 = the shared writers (VarMeta insert/remove, `214` view-stream, Props record) | wave 33 only, nothing after it until the operator says |
| 5 | A-8: FAT-slack files | item 17 (read-only at open) now; a slack-preserving save later if real files need it | item 17 |
| 6 | Q6: the Dropbox clone | delete with a pointer file | stays until the operator says; nothing is built or read from it |

### Verification gate

Fresh-tree gates on the final commit with `CORE_WARNINGS_AS_ERRORS=ON` on all three trees; CI green on all three OSs INCLUDING the `app/` suite (item 10); both name scans plus the new marker check 0 hits; a verifier pass on `git diff 17f5782..wave33`, then a second narrower pass over the verifier-fix commits; `gh api` returns 422 for the four pre-scrub tips (after Q1); lane B's property-set scan reports 0 flagged `.mpp`; the three A-1/A-2/A-3 tests seen RED before the fix and GREEN after.
