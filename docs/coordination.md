# Working agreement for agents

You are one stream in a coordinated project: porting Microsoft Project to a
native macOS application, personal use only. Read `README.md`, then
`docs/status.md`, then `docs/scope.md`, then the spec for your area. Do not
re-derive what is already established there.

## Non-negotiables

1. **Stay in your lane.** You own the files listed for your stream. Do not edit
   `WAVES.md`, `README.md`, `docs/coordination.md`, `docs/status.md`, `docs/scope.md`,
   `docs/architecture.md`, `scripts/oracle_lock.py`, `scripts/mpp_dump.py`,
   `spec/mpp-container.md`, or any other stream's directory. If you need a change
   there, message the coordinator.
2. **Never merge, never push to `main`.** Commit to your branch, push it, message
   the coordinator. The coordinator merges.
3. **No claim without evidence.** Every finding in a spec must cite the command,
   file and byte offsets that show it. Every "verified" needs a re-runnable
   command. If you inferred something, write "inference" and say what would prove it.
4. **Do not invent MS Project behaviour.** If the binary and the oracle disagree,
   the oracle wins — say so explicitly.
5. **Do not install software, do not touch `C:`, do not write outside your
   worktree** (the one exception is `corpus/probe/` inside your worktree).

## Files and directories

- The real corpus (client files) is **read-only** and lives in the main checkout:
  `corpus/*.mpp`.
  Read them from there; never copy them into a commit. `corpus/` is git-ignored.
- `decompiled/` (2.59 M lines) and `ghidra-project/` are git-ignored but present in
  the main checkout. Read them there.
- Your worktree is `D:\dev\wt\<stream>`. Everything you commit comes from there.

## Driving MS Project (the oracle)

Only through `scripts/oracle_lock.py`, which serialises access across agents:

```python
from oracle_lock import oracle_session
with oracle_session() as app:      # raises if another agent holds the lock
    app.FileNew()
    ...
```

Rules:
- **Never pass `/automation`** (or any argument) to `WINPROJ.EXE`. Project treats an
  unknown switch as a file name and shows a modal error that rejects every COM call
  with 0x80080005. This has already cost the project an hour.
- Launch with no arguments; poll `Application.Projects` until it binds (20–30 s cold).
- `Application.Version` does not exist.
- Always close Project and release the lock; never leave an instance running.
- Write fixtures only under `corpus/probe/`.

## Client data in committed files (added 2026-10-22 after a near-miss)

`corpus/` is git-ignored because it holds a client's real construction programme.
The same rule applies to anything *derived* from it:

- Never commit a task **name** from the corpus. `tests/data/project_a_golden.tsv` is the
  precedent: names are hashed (`sha256:...`), ids/dates/durations are kept.
- Oracle output committed under `docs/analysis/oracle/` must be built from
  synthetic fixtures (`corpus/probe/*.mpp`, generated via COM) or carry counts
  only. A script that dumps rows from a client file must write the counts, not
  the rows — `scripts/oracle_recalc.py` does this.
- One coordinator commit briefly contained `docs/analysis/oracle/recalc.json`
  with 83 client task names; the file was removed and the history rewritten in
  the same session (`cfdd5db`). Do not repeat it: check `git show --stat` for
  anything under `docs/analysis/oracle/` before pushing.

## Context discipline (important)

You have a fixed context budget. Do not push to the end of it.

- Keep `memory/<stream>.md` current as you go: what is established, what is open,
  where the evidence is. Assume a successor will read only that file.
- At roughly **70% of your usable context**, stop starting new work: commit what
  works, push, finish `memory/<stream>.md` and `docs/handoff/<stream>.md` with
  (a) what is done, (b) exactly what is not, (c) the next three concrete actions,
  (d) traps you hit. Then message the coordinator that you need a successor.
- A clean, honest handoff is a success. Running out mid-edit is a failure.

## Definition of done for your stream

- Deliverable(s) committed and pushed on your branch.
- The spec/doc states findings with evidence, and clearly separates verified from
  inferred.
- The verification commands are recorded somewhere the coordinator can re-run.
- `memory/<stream>.md` reflects reality.
- You have messaged the coordinator with: branch, commit range, what is verified,
  what is open, and anything that changes the plan.

## Revision 2 (2026-10-22): single tree, coordinator commits

The worktree model is withdrawn. Every agent session inherits the coordinator's
workspace root, so trees outside it are unwritable; `D:\dev\wt\*` and the
`wave1/*` branches are deleted. Instead:

- **Work directly in the main checkout** (the directory that contains
  `core/`, `scripts/` and `tests/`).
- You own only your stream's paths (see your brief). Never touch another stream's.
- **Do not run git.** No add, commit, branch, checkout, stash, reset, push.
  Concurrent git in one working tree corrupts the index. The coordinator commits
  and merges; your files persist on disk regardless, so nothing is lost if you stop.
- Shell works now. The folder was missing an explicit WRITE_OWNER grant (you had
  WRITE_DAC but not WRITE_OWNER), which made sandbox provisioning fail and no
  shell could start. If a shell command fails again, report it and stop; do not
  invent workarounds.

## Revision 3 (2026-10-22): two hard limits

Confirmed by test, not assumed:

1. **COM to Project hangs in a confined shell.** `oracle_lock` binds, then the first
   real call never returns. The oracle is therefore **coordinator-only**. If you need
   a fixture, message the coordinator with the exact change and it will be built.
2. **`git push` fails confined** (no credential store: `SEC_E_NO_CREDENTIALS`).
   Agents cannot push. The coordinator commits and pushes.

What still works for you: reading any file, `pwsh`, Python, and `git commit`.
Use Python freely - it is the main tool for this project.

### Fixtures already on disk

`corpus/probe/` (read-only for you):

| file | contents |
|---|---|
| `n1.mpp` .. `n6.mpp` | 1..6 tasks, all 2d, otherwise identical |
| `probe-v1.mpp` | 3 tasks (5d/3d/2d). **Not** a link/assignment fixture: `TBkndCons` has zero rows and all three assignment rows are placeholders (verified 2026-10-22). Use `link1.mpp` / `assign1.mpp`, built by `scripts/oracle_fixtures.py`. |
| `probe-v2.mpp` | same project; only task 1 duration 5d->7d and task 2 priority 700 |

The real corpus (read-only) is `corpus/project_a.mpp` (84 tasks) and
`corpus/project_b.mpp` (85 tasks).
