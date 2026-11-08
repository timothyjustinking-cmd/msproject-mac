# Coordinator handoff, wave 34 (Sun 8 Nov 2026, 16:25 AEDT)

**Successor: first invoke `opus-performance` and `session-memory` via the Skill tool, then
`deepseek-wave-run` and `audit-handover`.**  This is the second compaction of the wave 34
resume coordinator (Opus 5.5), so the brief's handoff rule applies at this group boundary.
The whole brief is still `docs/handoff/PROMPT-opus-coordinator-wave34-resume-sonnet.md`,
which inherits `docs/handoff/PROMPT-opus-coordinator-waves33-34.md`.  Its rules hold
unchanged: no force-push, no history rewrite, no pre-push-guard bypass, no client or
people's names or company paths (the repo is public), `corpus/` read-only, COM only through
`scripts/oracle_lock.py` on `C:\w34\`, no `-ExecutionPolicy Bypass` in the tool shell,
never print the DeepSeek key, commit trailer `Co-Authored-By: Claude Opus 5.5
<noreply@anthropic.com>`.  Do NOT wake the earlier coordinator
`local_32e669b9-5d65-442c-b114-38b3476552ae`.

## State at handoff

- Branch `wave34`; `main` = `origin/main` = `4fd9b90`.  Nothing on `wave34` is pushed.
- The builder is Claude Sonnet 5.5 through `tools/deepseek/sonnet-claude.cmd`
  (`run-wave.ps1 -Builder sonnet`, commit `cedba33`).
- Markers: G0, G1, G2 (DeepSeek, 30 Oct 2026); G3 `3a8a31c` 11:09 and G4 `7ba26a1` 16:18
  (Sonnet, 8 Nov 2026).  The G3,G4 driver (pid 37108) exited normally at 16:18:51.
- The 12-hour stop point (14:19) passed during G4.  The rule is "stop after G4": **CLOSE
  only; G5 and G6 become wave 35** (queue them in `docs/handoff/plan-waves27-30.md`).
- Plan usage at the G4 marker: weekly all-models 27 %.  Stop at a group boundary at 90 %;
  pause at 95 % of the 5-hour figure; a rate-limit error is a pause, never a deferral.

## CLOSE driver

The predecessor launches CLOSE right after committing this file:

    Start-Process pwsh -ArgumentList '-NoProfile','-File','D:\dev\msproject-mac\tools\deepseek\run-wave.ps1','-Wave','34','-Groups','CLOSE','-Builder','sonnet' -RedirectStandardOutput 'D:\dev\msproject-mac\build\deepseek-sessions\run-wave34-close-driver.out' -RedirectStandardError 'D:\dev\msproject-mac\build\deepseek-sessions\run-wave34-close-driver.err' -WindowStyle Hidden

Its pid is in the shared notes file (below).  Find it again with
`Get-CimInstance Win32_Process | ? { $_.CommandLine -match 'run-wave' }`.  Resume by
re-running the same line; marked groups are skipped.  CLOSE does not push or dispatch CI.
Do not edit or commit in `D:\dev\msproject-mac` while CLOSE runs: the builder commits there.

Watcher, a background tool run that re-arms on TIMEOUT:

    python %LOCALAPPDATA%/Temp/w34s/w34watch.py <close-driver-pid> 100

(Expand `%LOCALAPPDATA%` and use forward slashes when you run it.)

It exits on MARKER, BLOCKED, FAILEXIT, RATELIMIT, LEFT, FINISHED, DRIVER-GONE, STALL or
TIMEOUT.  Its state is in `w34watch-state.json` beside it.  Delete that state file before
the first CLOSE watch so the new session transcript is picked up.

## G3: verified, two Highs open

Record: `docs/handoff/wave34-verification/gates-g3.md` and `review-g3.md` (committed with
this file).  All gates were green on `3a8a31c` in the fresh worktree, and the five planted
mutations M1-M5 all went red.  The independent review found 0 Critical, 2 High (both
CONFIRMED at source), 2 Medium and 5 Low:

- **R34G3-1 High:** `assignment_growth.cpp` `check_assn_row_work` accepts any positive work.
  Every pair is a 40-hour step, and `summary_info` writes "<n> hrs" for any whole hour count.
- **R34G3-2 High:** `tests/roundtrip_assignment_growth.cpp:341-350` masks +0, +16 and +32 of
  the appended assignment Fixed2Data record.  +16 and +32 are the task and resource identity
  references, so a wrong resource passes `compare_all`.
- **R34G3-3 Medium:** a 32-bit wrap in a bounds check at `assignment_growth.cpp:367`, giving
  an OOB read on a crafted file.
- **R34G3-4 Medium:** the count-3->4 and no-free-placeholder refusals have no test.
- **R34G3-5..9 Low:** see `review-g3.md`.

**Merge rule:** wave 34 is not mergeable while R34G3-1 and R34G3-2 are open.

Verify worktree `D:\dev\msproject-mac-wt-v34g3` (detached at `3a8a31c`) holds a corpus
JUNCTION at `D:\dev\msproject-mac-wt-v34g3\corpus` pointing to the main corpus.  Remove it
ONLY with `cmd /c rmdir D:\dev\msproject-mac-wt-v34g3\corpus` (never `rm -rf`, never
`Remove-Item -Recurse`), and do that BEFORE `git worktree remove`.  Reuse the worktree for
the G4 verify (`git -C <wt> checkout --detach 7ba26a1`) or remove it in that order.

## G4: marked, not yet verified (the successor's first job)

| Commit | Item | Outcome |
|---|---|---|
| `9c5f497` | W34-4.0 | Insert Task, Delete Task, link ADD and custom-field SET/CLEAR refused up front |
| `726dcfd` | W34-4.1 | Resource Information rename enabled; Notes disabled (RichEdit generator string) |
| `399902d` | W34-4.2 | Add Resources: one work resource, default rates |
| `c4d3a56` | W34-4.3 | Assign Resources: first assignment and its removal (removal only from a Project-saved file) |
| `f950a03` | W34-4.4 | Insert Summary and Insert Milestone deferred with evidence (15 streams differ) |
| `b07d435` | W34-4.5 | Font Bold: one cell only |
| `7ee25d6` | W34-4.6 | Define Code: prefix W, three flat tasks; Renumber disabled |
| `4298770` | W34-4.7 | Set Baseline 0 deferred with evidence (94 new var blocks, 26 undecoded) |
| `7ba26a1` | W34-G4 | group gates (core 62/62, app 31/31, Qt 47/47) and the marker |

The G4 verify works like G3's: `build/verify-w34g3/run-gates.cmd` and
`%LOCALAPPDATA%\Temp\w34s\sf_mut.py` plus `mutbuild.cmd` are the templates.
Write fresh copies for G4; never derive one with `sed`.

- Run the fresh-tree gates on `7ba26a1` (core, Qt and app).
- Plant mutations at each new gate's call site and in its helper: the 4.2, 4.3, 4.5 and 4.6
  enablement checks, and one 4.0 refusal.
- Run one independent read-only review of `3a8a31c..7ba26a1`.
- **Check that Assign Resources (4.3) refuses every work amount except the measured
  40-hour shape.**  If it does not, R34G3-1 is live in the UI and stays a High.
- Check brief section 4 (e): Project reopens each enabled edit's port save with 0
  differing fields.
- Note: `app/main.mm` was not touched, so the macOS menu is not wired for the new items.
  That is a Medium for the plan, not a blocker.

## After CLOSE marks

1. Fresh-tree gates on the CLOSE tip, then one independent read-only review of
   `main..wave34`.
2. Write the verification record under `docs/handoff/wave34-verification/`.
3. Push normally: `git push origin wave34`.
4. Dispatch CI: `gh workflow run qt.yml --ref wave34` and `gh workflow run ci.yml --ref
   wave34`.
5. Because of R34G3-1 and R34G3-2, a fix-only **`wave34b`** runs before any merge.
   - G0 holds both Highs and any G4 or CLOSE High.
   - G1 holds R34G3-3 and R34G3-4.
   - Write `docs/handoff/wave34b-brief.md`, drive it with `run-wave.ps1 -Wave 34b ...
     -Builder sonnet`, then verify it.
6. Merge into `main` only with CI green on all three OSs and no Critical or High.
   Otherwise hold the wave unmerged and record the reason.
7. Queue G5 and G6 as wave 35 in `plan-waves27-30.md`.  Do not start wave 35 without the
   operator's word.

## For the operator (in the status file; not chased)

- **B-1:** the GitHub purge request for the 329 ids in
  `docs/handoff/wave32-verification/rewrite-sha-map.txt`.  Unanswered as at 8 Nov 2026.
- **B-8:** DeepSeek key rotation (low urgency).
- **DeepSeek balance:** needed only for wave 35.
- **Decision 4 (B-5):** the default was applied on 8 Nov 2026, and the operator can
  reverse it in one word.

## Stop-point duties

- Write and commit `docs/handoff/status-2026-11-NN.md`.
- Under the session-memory write window, add a MEMORY.md session entry and TASKS.md rows
  in the operator's Dropbox Claude folder (path in the operator's global instructions).
- MEMORY.md was 79 KB, over its 48 KB budget, so the archive pass comes first.
- Use absolute dates, and check every weekday label by script.

## Notes and logs

- Shared notes:
  `%APPDATA%\Claude\side-session-notes\local_3600f43b-8a47-4ca3-b282-3b05846fb5f4\opus-coordinator-w34-sonnet-progress.md`
  (keep appending).
- Driver logs: `build/deepseek-sessions/` (`run-wave34-sonnet-driver.out`,
  `run-wave34-G3-01.log`, `run-wave34-G4-01.log`).
- Builder transcripts: `%USERPROFILE%\.claude\projects\D--dev-msproject-mac\<sid>.jsonl`
  (G3 `1bb82ab5-...`, G4 `82dc7ca6-...`).
- The G4 builder's own probe and gate logs: `build/w34/`.
- Report to the operator, in the deliverable's words, at each marker and at the stop point.
