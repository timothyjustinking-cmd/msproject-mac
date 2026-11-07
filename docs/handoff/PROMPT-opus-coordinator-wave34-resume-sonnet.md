# Coordinator brief: resume wave 34 with Claude Sonnet as the builder

_Written Sun 8 Nov 2026 by the Fable assessment session (Windows Claude Code) on the
operator's instruction "create an opus coordinator session and have it restart works using
Sonnet as the builder".  This is the whole brief for a NEW Opus coordinator session in
`D:\dev\msproject-mac`.  It supersedes the resume line in the wave 34 brief's stop note
(`f296add`), which assumed DeepSeek._

---

## 0. Open with these, before any tool use

Invoke, via the Skill tool, in this order: `opus-performance`, `session-memory`,
`deepseek-wave-run`, `audit-handover`.  Then read, in this order and with `sed -n` or
`tail`, never whole files into context twice:

1. `docs/handoff/PROMPT-opus-coordinator-waves33-34.md` - the original coordinator brief.
   Every rule in it still binds you (roles, the verify loop, the merge rule, the 60 %
   handoff rule, the status report, silence until markers), except where this file says
   otherwise.
2. `docs/START-HERE.md` and `docs/handoff/standing-rules.md`.
3. `docs/handoff/wave34-brief.md` - the work order, including its four coordinator notes
   (launch, after G0, G2 restart, STOPPED).
4. `docs/handoff/wave34-complete.md` - G0, G1 and G2 records.  W34-3.1 has NO entry yet:
   the DeepSeek session was cut off before writing it (commits `281ac54`, `07046d0`,
   `73bc6e6`).  The builder writes that entry first when G3 resumes.
5. `git log --oneline main..wave34` and `git status`: `wave34` = `f296add`, tree clean,
   `main` = `origin/main` = `4fd9b90` (waves 33 and 33b merged Fri 30 Oct 2026).

## 1. What happened and where it stands

- Waves 33 and 33b are merged and green on all three OSs; the 29 Oct audit's Criticals
  and Highs are closed.  Wave 34 (parity) started Fri 30 Oct 2026 10:11.
- Wave 34 G0 (decoding lane and synthetic pairs), G1 (VarMeta writer) and G2 (214
  view-stream and Props writers) are complete with markers.  G3 has W34-3.1 (assignment
  append writer and its two pairs) committed; W34-3.2 (resource row and calendar),
  W34-3.3 (DocumentSummaryInformation writer) and W34-3.4 (reader gaps) are not done.
  G4 (shell enablement, starting with W34-4.0, the refusal of the five paths whose save
  still differs from Project's) has not started.  G5, G6 and CLOSE are untouched.
- The run stopped Fri 30 Oct 2026 at about 18:20: every DeepSeek call returned
  `API Error: 402 Insufficient Balance`.  The previous coordinator stopped the driver
  cleanly; nothing has run in this repo since.  Builder time used: about 7 h 15 min of the
  wave's 12 h.
- Nine days have passed.  Nothing in the tree changed in that time.  Re-check this
  yourself before launching (`git status`, `git log -1 --format=%cd wave34`).
- The previous coordinator session ("Opus coordinator: MS Project port waves 33 and 34",
  `local_32e669b9-5d65-442c-b114-38b3476552ae`) is idle at 28 % context.  Do NOT message
  or wake it: you replace it.  The Fable session that wrote this file is archived once you
  have started, so no parent session watches you; you own your own handoff (section 6).

## 2. The builder is now Claude Sonnet, not DeepSeek

The operator's decision, Sun 8 Nov 2026.  The DeepSeek account is out of credit and is not
being topped up for this run.  The builder from G3 onward is Claude Sonnet 5.5 through the
same Claude Code CLI, on the operator's own Claude account (the one the desktop app and this
session use; no API key).  Nothing changes in the wave's rules: fresh session per group,
markers, one finding id per commit, gates per commit, deferral only with evidence.

Make the switch as ONE tooling commit on `wave34` before launch, id `W34-BUILDER`:

1. Add `tools/deepseek/sonnet-claude.cmd` beside `deepseek-claude.cmd`.  Same shape, no
   DeepSeek lines: no `ANTHROPIC_BASE_URL`, no `ANTHROPIC_AUTH_TOKEN`, no key file; it must
   not read `%LOCALAPPDATA%\deepseek\api_key.txt`.  It sets, per-process only:
   `ANTHROPIC_MODEL=claude-sonnet-5-5`, `CLAUDE_CODE_SUBAGENT_MODEL=claude-sonnet-5-5`,
   `CLAUDE_CODE_EFFORT_LEVEL=high`, `CLAUDE_CODE_AUTO_COMPACT_WINDOW=200000`, then
   `cd /d "%~dp0..\.."` and `claude --model claude-sonnet-5-5 %*`.  Explicitly clear
   `ANTHROPIC_BASE_URL` and `ANTHROPIC_AUTH_TOKEN` (`set "ANTHROPIC_BASE_URL="`) so a
   value inherited from the environment cannot route the builder to DeepSeek.
2. In `tools/deepseek/run-wave.ps1` add `[ValidateSet("deepseek","sonnet")][string]$Builder = "deepseek"`
   and pick `$launcher` from it.  Nothing else in the driver changes: same `--tools` list
   (no WebFetch or WebSearch, audit B-8), same `--permission-mode auto`,
   `--strict-mcp-config`, same marker logic.  Keep `-Builder deepseek` the default so the
   Design Game and portal copies of the method are not affected by example.
3. Smoke-test the launcher ONCE before the wave, from the repo root, headless:
   `tools\deepseek\sonnet-claude.cmd -p "Reply with the single word READY and nothing else" --permission-mode auto --verbose --strict-mcp-config --tools Read`
   and confirm in the verbose output that the model is `claude-sonnet-5-5` and no
   `api.deepseek.com` line appears.  Record the log path under `build/w34/W34-BUILDER-smoke.log`.
   If the CLI asks to log in, stop and report BLOCKED: the operator signs in, you never
   type a credential.
4. Add a coordinator note to `docs/handoff/wave34-brief.md` under the STOPPED note: the
   builder from G3 is Claude Sonnet 5.5 via `sonnet-claude.cmd`; the commit trailer for
   builder commits from here is `Generated-By: Claude Sonnet 5.5 via Claude Code (wave 34)`
   (the standing rules want the real model named; the CLI's own co-author trailer now
   happens to be accurate, but the explicit line is still required); `W34-3.1`'s record
   entry is the first thing G3 writes; the remaining builder time before the 12 h stop
   point is 4 h 45 min, counted from the new launch, and the stop rule is unchanged (G5, G6
   and CLOSE only if G4 finishes inside it; otherwise CLOSE only and G5-G6 become wave 35).
5. Run `python -I build/audit-w22/guard_names.py main..HEAD` and
   `python scripts/check_markers.py --range main..HEAD` before the commit.

**Quota.** Sonnet draws on the operator's Claude plan, shared with every other session on
this account (the previous week ran the all-models weekly limit to 85 %).  Before launch,
and at every marker, read the plan usage with the session usage tool.  If the weekly
all-models figure is at or above 90 %, do not start the next group: stop the driver at the
boundary, write the stop in the brief and report.  If the 5-hour figure is at or above
95 %, pause the same way and resume when it resets.  Never let a group fail three times on
rate limiting and be "left" like G3 was on the 402: a rate-limit error in the log is a
pause, not a deferral.

## 3. Launch

- Pre-flight: `Get-CimInstance Win32_Process | ? { $_.CommandLine -match 'run-wave' }`
  must show no driver for `msproject-mac`.  Drivers for `D:\dev\dg-wt-*` (the Design Game)
  may be running from other sessions: leave them alone, they are not yours.
- Project must be closed and `C:\w34\` must exist (`scripts/oracle_lock.py` owns Project).
- Launch detached, never as the chat's child, never with `-ExecutionPolicy Bypass` in the
  tool shell:
  `Start-Process pwsh -ArgumentList '-NoProfile','-File','tools/deepseek/run-wave.ps1','-Wave','34','-Groups','G3,G4','-Builder','sonnet' -RedirectStandardOutput build/deepseek-sessions/run-wave34-sonnet-driver.out -RedirectStandardError build/deepseek-sessions/run-wave34-sonnet-driver.err`
  Marked groups are skipped by the driver, so listing `G3,G4` is safe.
- Watcher: the previous coordinator's watcher died at about 2 h 20 min (background shells
  die at 2 h on this box); its fix was a watcher that exits after 100 minutes and is
  re-armed.  Do the same.  A failed or expired watch is a work trigger: check the driver
  and the tree in that turn, never answer it with "no response requested".
- Silence until a marker, `BLOCKED`, a stall (no commit and no log growth for about
  3 hours) or the driver exiting.  Status in the deliverable's words, not bare group ids.

## 4. After G4

Apply the wave 34 brief's own rule: if G4's marker lands inside the remaining builder
time, run `G5,G6,CLOSE` with the same launch shape; otherwise run `CLOSE` only and queue
G5 and G6 as wave 35 in `docs/handoff/plan-waves27-30.md` (the queue file the previous
coordinator used).  Then your part, exactly as the original brief says: fresh-tree gates
on the tip, one independent read-only review of `main..wave34`, the verification record
under `docs/handoff/wave34-verification/`, push (normal push; the pre-push guard stays on
and is never worked round), dispatch `qt.yml` and `ci.yml`, merge into `main` only if CI is
green on all three OSs and the review has no Critical or High; otherwise a fix-only
`wave34b` first, as wave 33b was.

## 5. Decisions

The wave 34 brief's decisions table still stands with its defaults (sequential; enablement
in the listed order; no dynamic trace; stop after G4 at 12 h).  One default has now
matured: decision 4 / audit B-5 said the hashed task-name column in
`tests/data/project_a_golden.tsv` is dropped if the operator is still silent by Fri 6 Nov
2026.  He was silent.  The default applies: drop the column in this wave (the uid is the
key; the brief says where the item sits).  Record it in the brief's decisions table with
the date, and mention it in your first status line so the operator can reverse it in one
word.

Items only the operator can do, to be asked ONCE in your first status report and then left
in the status file, never chased mid-wave:
- B-1 (Critical): the GitHub purge request with the 329 ids in
  `docs/handoff/wave32-verification/rewrite-sha-map.txt`.  Whether it was sent between
  30 Oct and 8 Nov 2026 is unknown to this brief; ask.
- The DeepSeek key rotation (B-8), now low urgency since DeepSeek is no longer the builder.
- The DeepSeek balance, only if he wants DeepSeek back for wave 35.

## 6. Context and handoff

The operator's rule: at 60 % context, at a group boundary, write
`docs/handoff/HANDOFF-coordinator-<date>.md` (branch, driver pid and logs, markers,
decisions answered, what is next, the exact resume and watcher lines), commit it, start
your successor yourself (a new Opus session in this folder whose brief is that file plus
this one), and end with `HANDOFF READY <path>` as your last line.  There is no parent
session to do it for you.  A session cannot be started from a message that arrived over
Remote Control; if the start is refused, leave the file and the line and say so in your
status report so the operator starts it from the desktop app.  A second compaction forces
the handoff whatever the percentage.

## 7. Close

At your stop point (wave 34 merged, or held unmerged with the reason), write
`docs/handoff/status-2026-11-NN.md` in the shape of the previous status files, commit it,
and write the MEMORY.md session entry and the TASKS.md rows in the operator's Dropbox
`Claude` folder per the session-memory skill (absolute dates, default outcome and owner on
every dated row, weekday labels checked by script).  Then stop.  Do not start wave 35
without the operator's word.
