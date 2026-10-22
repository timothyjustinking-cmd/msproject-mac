# Handoff — build and CI (W8)

_2026-10-22. Detail and evidence: `memory/ci.md`; instructions for humans:
`docs/build.md`._

## (a) What is done

1. **`.github/workflows/ci.yml`** — configure → build → ctest on
   `windows-latest` (MSVC via `ilammy/msvc-dev-cmd@v1` + **Ninja**),
   `macos-latest` (AppleClang, arm64) and `ubuntu-latest` (GCC),
   `fail-fast: false`, `timeout-minutes: 30`, no caching. Warnings are errors on
   all three (`-DCORE_WARNINGS_AS_ERRORS=ON`), tests have `--timeout 60`, and
   there is exactly one dated exclusion (`mpp_reader`, W6's harness aborts on
   Windows; see (b)). The first run (2026-10-22) **passed on macOS and Linux** —
   the first time this code met clang and GCC — and failed Windows at configure
   (`Generator Visual Studio 17 2022 could not find any instance of Visual
   Studio`); the Ninja + msvc-dev-cmd change is the fix for that. YAML validated
   with PyYAML plus structural assertions (`logs/w8-validate-workflow.py`); no
   `act` available locally.
2. **`CMakeLists.txt` rewritten.** Directory-wide warning policy (so stream
   subdirectories cannot opt out), `enable_testing()` before subdirectories,
   guarded `add_subdirectory` for `core/mpp` and `core/sched` that reports
   `included`/`skipped`, C++20 defaults inherited by subdirectories,
   `CMAKE_POSITION_INDEPENDENT_CODE ON`, `core_set_warnings()` retained for
   subdirectories that call it.
3. **`docs/build.md`** — Windows (vcvars64 path, VS generator, `--parallel` and
   Ninja traps with evidence, config trap), macOS, Linux, options, subdirectory
   contract, ten portability rules, re-runnable verification commands.
4. **Local proof (Windows only):** fresh configure + Release build + ctest with
   MSVC 19.44.35229 / CMake 3.31.6, `/W4 /permissive- /utf-8 /WX`, **0 warnings**,
   **5/5 tests pass** with the reader excluded; `PASS: 119614 checks`;
   `gen_field_tables.py --check` OK; four public headers compile standalone at
   `/W4 /WX` (`core_c.h` also as C99 via `/TC`).
5. **Portability audit of existing code:** no real defect found, and none
   invented. `#pragma once` is supported by all three CI compilers (not a
   defect); no MSVC-only pragmas; headers self-contained; no narrowing under
   `/W4`; LF/UTF-8; paths portable. The audit table is in `memory/ci.md`.

## (b) Exactly what is not done

- **The Windows fix is unverified until the next run.** The job now sets up MSVC
  explicitly and uses Ninja, but that configure path cannot be exercised on this
  box (Ninja hangs in the confined shell), so the next push is its first test.
  macOS and Linux are proven; Windows MSVC is not, yet.
- **The reader is not tested by CI.** Two separate reasons: (i) `mpp_reader` is
  excluded (`-E '^mpp_reader$'`) because W6's harness aborts on Windows with a
  modal CRT dialog → hang risk on a runner; W6 is fixing it. (ii) On a fresh
  clone `corpus/` is git-ignored and `tests/data/synthetic-tiny.mpp` is absent,
  so the reader tests `SKIP:` — CI proves the reader *builds* with all three
  compilers but does not exercise it against real data.
- **Only arm64 macOS is covered.** `macos-latest` is Apple Silicon. An Intel Mac
  target would need `macos-13` added to the matrix.
- **No Intel/PowerPC, no Xcode project, no Swift** — out of scope this wave.
- **`build/` is still not in `.gitignore`** (only `logs/` is), so the docs tell
  people to build under `logs/`. One-line fix belongs to whoever owns
  `.gitignore`.

## (c) Next three concrete actions

1. **Push the updated workflow and watch the Windows job.** It should configure
   with Ninja under `msvc-dev-cmd`; if it fails, read whether the cause is MSVC
   setup (the action), the generator, or a real compile error, and report which.
   Do not drop Windows from the matrix.
2. **Delete the exclusion** (`-E '^mpp_reader$'`, the `TODO(2026-10-22)` in the
   workflow and the note in `docs/build.md`) once W6 reports the harness fix;
   re-run CI. That is the acceptance test for the fix.
3. **Decide the target Mac's architecture** (`macos-latest` is arm64; add
   `macos-13` if it is an Intel Mac) and get a committed reader fixture under
   `tests/data/` so the reader tests stop skipping on CI (W6's call).

## (d) Traps

- **`windows-latest` moves under you.** An image upgrade removed the Visual
  Studio installation the "Visual Studio 17 2022" generator could discover. The
  Windows job now sets MSVC up explicitly (`msvc-dev-cmd`) and uses Ninja. Prefer
  explicit toolchain setup over generator discovery in CI.
- **Ninja configure hangs** on this box (stops at "Detecting CXX compiler ABI
  info", killed at 90 s; `logs/w2-build/` is an earlier corpse). Use
  `-G "Visual Studio 17 2022"`. Never Ninja locally.
- **`cmake --build … --parallel` exits 1 in 4–6 s with no error** and leaves
  ~90 orphaned `MSBuild.exe` workers behind. Never pass `--parallel`; the VS
  generator parallelises internally and a clean rebuild is 10 s.
- **`cmake`/`ctest`/`cl` are not on PATH** until `vcvars64.bat` runs; `ctest` is
  next to `cmake.exe`.
- **Multi-config generator:** pass `--config Release` to the build **and**
  `-C Release` to ctest, or ctest looks in Debug.
- **A wedged test hangs a runner**, so keep `--timeout 60` and the job
  `timeout-minutes`. A Windows CRT assertion opens a modal dialog; a test must
  write to stderr (`_set_error_mode(_OUT_TO_STDERR)`) and catch, or CI hangs.
- **PyYAML 1.1 reads `on:` as `True`** — normalise before validating the
  workflow; GitHub parses it fine.
- **clang-tidy (LLVM 19) is installed but rejects `--checks=…` here** ("no checks
  enabled"); there is no `clang.exe`/`clang-cl.exe`. Do not retry it as a Clang
  oracle — the macOS job is the real check.
- **Do not edit `core/mpp/`, `core/sched/`, `data/` or the shared docs** to make
  CI green. Report instead; the streams own their fixes.
