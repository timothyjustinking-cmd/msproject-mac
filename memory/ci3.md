# Wave 21 — parallel Linux Qt build (`.github/workflows/qt.yml`): working memory

_2026-10-23. Lane: stream C6. Files I own: `.github/workflows/qt.yml`,
`docs/handoff/ci3.md`, `memory/ci3.md`, scratch under `build/ci3-w21/**`
(git-ignored). Nothing else was edited. No `git`, no dispatch, no installs; the
sandbox blocks Git Bash startup, so **no runtime claim below is verified** —
the next dispatch is the evidence._

## What the workflow is now

One job (`build-qt`), the three-row matrix unchanged, **17 steps**, 455 lines,
22,835 bytes, CRLF on disk, no tabs. The only change: the `Build` step branches
on `${{ runner.os }}` — Linux runs `cmake --build build-qt --config Release
--parallel 2`, Windows and macOS run the old serial line verbatim. The parsed
semantic difference from the pre-edit file is exactly one leaf,
`jobs.build-qt.steps[6].run`; the text diff is one hunk, +14 / −1.

Step order (asserted, unchanged): `Check out → Linux apt → install Qt 6.5.3 →
MSVC env → Configure → pinned-Qt guard → Build → ctest → parity render gate →
Package Windows → Package macOS → Package Linux → AppImage smoke → Upload
Windows → Upload macOS → Upload Linux → Upload renders`.

## Durable facts

- Generators: **Linux/macOS `Unix Makefiles`, Windows `Ninja`**. Ninja already
  starts one job per core, so only the Make rows were serial; the brief scopes
  the fix to Linux, so macOS stays serial on purpose.
- `cmake --build … --parallel N` passes `-j N` to the Makefiles generator since
  CMake 3.12. The runner's CMake is 3.31-era (local box 3.31.6-msvc6) and
  `app_qt/CMakeLists.txt` already requires 3.21, so the flag is not too new.
- `app_qt/CMakeLists.txt` has **no** `add_dependencies` and no
  `add_custom_command` (only `qt_add_executable`/`qt_add_resources`), and the
  Windows job has built in parallel with Ninja for waves, so the target graph is
  believed order-safe under `-j` (inference, not measured under make).
- Validator (scratch, re-runnable): `python build/ci3-w21/validate-workflow.py`
  parses before/after with PyYAML 6.0.3, asserts the matrix / Qt pin / every
  step body / the four artifacts, and deep-compares the documents.
- Expected `git diff --numstat -- .github/workflows/qt.yml`: **`14 1`**.

## Traps

- **Git Bash cannot start in this sandbox**: `bash -n` dies with
  `fatal error - couldn't create signal pipe, Win32 error 5` before reading the
  script. Shell syntax is therefore **unverified** in this lane; say so rather
  than implying otherwise.
- **`qt.yml` is CRLF** (455 CR / 455 LF, 0 tabs) while `ci.yml` is LF and
  `app-macos.yml` is LF apart from one line; there is no `.gitattributes`, and the
  CRLF predates this lane's edit (the pre-edit snapshot was 442/442). CI2's
  "LF-only" note no longer describes the file. If `git diff --numstat` shows
  ~`455 442` instead of `14 1`, the blob is LF and the worktree CRLF: normalise
  to LF before committing, then re-run the validator.
- **A `grep --parallel` counts the comment**: the file mentions `--parallel 2`
  twice — once in the documenting comment, once in the command. There is
  exactly one *command* (asserted across all `run` scalars).
- **Do not split the Build step** into two `if:`-guarded steps to get the flag
  onto Linux: it would change the 17-step contract that the wave-20 handoff and
  the validator pin, and it duplicates the command. One step, one shell branch.
- **The core budget is assumed, not measured**: the brief says 2 cores; GitHub's
  standard public-repo runner has advertised 4 vCPU since 2024. `--parallel 2`
  is the deliberate, conservative choice; raising it to 4 is a separate change
  after the next run's step timings.
- **CMake does not echo the `-j`** it hands to make, so the only proof is the
  Linux `Build` step duration versus run `39898703443`. `jobserver unavailable`
  in the log would mean `-j` was dropped.
- **Dropbox mtime churn** made `edit` refuse twice right after a read; re-read
  and retry (or patch bytes with Python).
- **Python text writes default to CRLF on Windows**: patch `qt.yml` with
  `read_bytes`/`write_bytes` (it is CRLF and must stay a one-hunk diff); write
  the `docs/`+`memory/` markdown as LF (the repo's docs are LF-only).
- **The local sandbox must stay serial**: MSBuild `/m` and piping
  `cmake --build` into anything both fail here (named pipes). CI is unaffected.
