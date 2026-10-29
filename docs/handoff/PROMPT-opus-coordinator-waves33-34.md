# Paste-in prompt: Opus coordinator for waves 33 and 34

_Written 29 Oct 2026 by the Fable assessment session.  Open a NEW Claude Code session in
the desktop app (model Opus, folder `D:\dev\msproject-mac`) and paste everything below the
line.  The session could not be started from the assessment session because it was running
over Remote Control._

---

You are the Opus coordinator for MS Project port waves 33 and 34 in `D:\dev\msproject-mac`.

Background. The operator is porting Microsoft Project (WINPROJ.EXE, Project 2021 installed on this
box) to a native app for personal use, with 100 % parity as the acceptance bar. `main` =
`17f5782` holds waves 23-32, merged 27 Oct 2026 after a history rewrite, CI green on all
three OSs. The build method: a Claude coordinator and verifier; DeepSeek (deepseek-flash
through the Claude Code CLI, `tools/deepseek/deepseek-claude.cmd`) codes in waves driven
headless by `tools/deepseek/run-wave.ps1 -Wave NN -Groups G0,...,CLOSE`, a fresh DeepSeek
session per group, each group ending when its `WAVE-NN-<group>-COMPLETE` marker is
committed in `docs/handoff/waveNN-complete.md`. After each wave: fresh-tree gates, one
independent read-only review agent of the wave's diff, the verification record committed
under `docs/handoff/waveNN-verification/`, push, both CI workflows dispatched, merge into
`main` only if CI is green on all three OSs and the review has no Critical or High.
`docs/handoff/standing-rules.md` binds everything (rule 1: no save path is enabled until
the same edit was made in Project via COM, saved, and diffed field by field against the
port's save). On 29 Oct 2026 an assessment and adversarial audit of `17f5782` was written
(`docs/handoff/assessment-2026-10-29.md`: 3 Critical, 10 High, 14 Medium, 17 Low; lane
evidence under `build/audit-w32/`, git-ignored) with two work orders:
`docs/handoff/wave33-brief.md` (fix-only) and `docs/handoff/wave34-brief.md` (parity).
`docs/START-HERE.md`, `memory/coordinator.md` and `docs/handoff/INDEX.md` are STALE (they
say main = fcb1c10, nothing merged); trust `git` and the assessment until wave 33 G4 fixes
them. Audit finding B-8: the DeepSeek driver runs with `--permission-mode auto`,
Bash/Write/Edit/WebFetch, no human gate, on a box where `corpus/` and the company Dropbox
are readable, with the DeepSeek key in the agent's environment; the operator decided on 29 Oct to
run waves 33 and 34 here anyway. The operator's standing instruction for unattended runs: run the
waves back to back, verify each, merge when the rule is met, then STOP and give a thorough
status report; never force-push; never work round the pre-push guard; never commit client
task names, project titles or people's names (rows by ID). The previous coordinator
runbook is `<operator Dropbox>\Claude\Projects\MS Project Decompile\build\audit-w22\HANDOFF-successor.md`
(its per-wave loop still applies, its branch state does not). The Dropbox folder
`Projects/MS Project Decompile` is a stale clone: never build or read from it except for
that runbook and its tools.

Read, in order: `docs/handoff/assessment-2026-10-29.md` (sections 1, 7 and 8),
`docs/handoff/wave33-brief.md`, `docs/handoff/wave34-brief.md`,
`docs/handoff/standing-rules.md`, and the runbook's "Per-wave loop" and "Tools" sections.
Confirm `git rev-parse main` = `17f5782` and `git status` clean apart from the untracked
handoff docs (`assessment-2026-10-29.md`, `wave33-brief.md`, `wave34-brief.md`, this file).

Step 0, before any DeepSeek launch (on branch `wave33`, cut from `main`):
1. Commit the untracked handoff docs on `wave33` (one commit: "wave 33 brief, wave 34
   brief and the 29 Oct assessment"); run the local name scans first (the pre-push hook's
   `build/audit-w22/guard_names.py` range, and a grep for the operator's name in the new
   files: there must be none).
2. Edit `tools/deepseek/run-wave.ps1`: remove `WebFetch` and `WebSearch` from `$tools`
   (B-8: no network reads for the DeepSeek agent). Commit as "B-8: the DeepSeek agent gets
   no WebFetch or WebSearch".
3. Tell the operator in your first report that two items are the operator's alone: rotate the DeepSeek key
   (B-8), and send the GitHub purge request (B-1, Critical;
   `build/audit-w22/scrub/github-support-request.md` in the Dropbox clone, adding the 329
   ids from `docs/handoff/wave32-verification/rewrite-sha-map.txt`).
4. `Get-CimInstance Win32_Process | ? { $_.CommandLine -match 'run-wave' }` must be empty.

Run wave 33: `pwsh -NoProfile -ExecutionPolicy Bypass -File tools/deepseek/run-wave.ps1 -Wave 33 -Groups G0,G1,G2,G3,G4,CLOSE`
with output to `build/deepseek-sessions/run-wave33-driver.out`, as a DETACHED process
(`Start-Process pwsh -ArgumentList ... -RedirectStandardOutput ...`; background PowerShell
runs have died at 2 h on this box), plus the stall alarm
`python -I <Dropbox clone>/build/audit-w22/stall_alarm.py build/deepseek-sessions/run-wave33-driver.out 45`.
Watch only for BLOCKED, STALLED and done; read logs with tail, never whole. When the driver
exits: fresh-tree gates on the final commit (core, app with `CORE_WARNINGS_AS_ERRORS=ON`,
app_qt, M2, engine suite, `check_fixtures.py`, the new `check_markers.py`); one
independent read-only review agent of `git diff main..wave33` (priorities: the audit ids
closed as claimed, no new unmeasured write, no weakened gate, privacy); commit `gates.md`
and `review.md` under `docs/handoff/wave33-verification/`; push `wave33` without force;
dispatch `qt.yml` and `ci.yml` on `wave33`; confirm the CI logs show the `app/` ctest
total on both OSs, the asan job and the marker check. Merge rule: merge `wave33` into
`main` only if CI is green on all three OSs and the review has no Critical or High;
otherwise write a fix-only `wave33b` brief (G0 = the review's Critical/High) and run it
first. Report to the operator before starting wave 34.

Run wave 34 the same way (`-Groups G0,G1,G2,G3,G4,G5,G6,CLOSE`) on `wave34` cut from the
merged `main`, honouring the brief's decisions table defaults unless the operator has answered them
(decision 1: sequential; decision 5: stop after G4 and report if the wave passes 12 hours,
G5-G6 become wave 35). COM measurements run only through `scripts/oracle_lock.py` on copies
under `C:\w34\`; Project closed and the lock released after each. Verify, review, push, CI,
merge under the same rule.

Rules for you: never force-push, never rewrite history, never bypass the pre-push guard,
never commit a client task name, project title or person's name, never touch `corpus/`
beyond read-only use by the existing tooling, never build inside the Dropbox folder. Commit
messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. If a group
reports BLOCKED on something only the operator can resolve, record it, continue with the next
independent group, then report. Report partial results at every wave boundary; never go
silent for hours. At the end of wave 34 (or its stop point): STOP, start nothing, write a
thorough status report as `docs/handoff/status-2026-11-NN.md` (date from the clock)
covering both waves, the merge state, CI, every audit id closed or still open, what remains
for parity and a progress-to-parity percentage using the weighting in
`assessment-2026-10-29.md` section 9; then append the session entry to
`<operator Dropbox>\Claude\MEMORY.md` under `## Session Log` (anchored insert,
newest first, absolute dates, no client names, read back after writing) and the open items
to `TASKS.md` under `## Open` with a default outcome and owner on every dated row.

Done looks like: wave 33 merged into `main` with CI green including the `app/` suite; wave
34 merged or stopped at its decision-5 point with the decode notes and pairs committed; the
status report written; memory and tasks updated; a final message to the operator with the commit
ids, what closed, what is still open, and what only the operator can do.
