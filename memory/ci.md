# W8 — build and CI: working memory

_Last updated: 2026-10-22 ~10:30 (Australia/Sydney)._

## Update 2026-10-22 (W13): `mpp_reader` is in CI, with a committed fixture

The exclusion is gone. What changed:

- **Fixture.** `corpus/probe/base.mpp` (237,568 B, 3 tasks + project row) was
  copied byte-for-byte to `tests/data/synthetic-base.mpp`; SHA-256
  `55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069`, source and
  destination hashes equal. It is synthetic (coordinator, COM, 2026-10-22), not
  client data; provenance and the "never replace it with a client file" rule are
  in `tests/data/README.md`. It is the only `.mpp` that may be committed.
- **Test.** `tests/mpp_reader_tests.cpp` grew `test_synthetic_fixture`: it runs on
  every machine and asserts the decoded values (3 tasks A/B/C, 5/3/2 days,
  2026-01-05 08:00 starts, priority 500, 0 %, project title `base`, 21600 ticks).
  If the fixture is missing while its sibling `project_a_golden.tsv` is present, the
  test **fails** (deliberately) instead of skipping, so a forgotten `git add`
  cannot pass CI with the reader unexercised. PROJECT_A/PROJECT_B/`corpus/probe` still
  short-circuit on `MPP_NO_CORPUS` (any non-empty value) as well as on file
  absence, so they **skip** rather than fail. The fixture path does not depend on
  cwd: it is the parent of the configured `MPP_GOLDEN_PATH`, which is always
  `tests/data/`. `tests/test_paths.h.in` now also defines `TEST_DATA_DIR`
  (absolute, `@PROJECT_SOURCE_DIR@/tests/data`) for any target that wires the
  header in; the reader test uses it if defined (verified by a scratch build that
  forced the header in: it compiled at `/W4 /WX` and used that path).
- **Workflow.** Removed `-E '^mpp_reader$'` and the dated TODO (header + Test
  step). Added a second step:
  `MPP_NO_CORPUS=1 ctest --test-dir build -C Release --output-on-failure --timeout 60 -R '^mpp_reader$'`
  — a cheap proof that the committed-fixture path works with no corpus, on every
  platform, every run.
- **Docs.** `docs/ci.md` is new (what CI runs, the fixture, how to read a
  failure, what is not verified); `docs/build.md` lost the exclusion and gained a
  "Test data" section; `tests/data/README.md` is the provenance record.

Local evidence (MSVC 19.44.35229, VS 2022 BuildTools, CMake 3.31.6, `build\vs`):

- configure + build with `-DCORE_WARNINGS_AS_ERRORS=ON`: exit 0, no warnings;
- `ctest --test-dir build\vs -C Release`: 12/12 passed; `mpp_reader`
  `PASS: 106 checks` (corpus present, `SYNTHETIC: 3 tasks`, `PROJECT_A: 83 tasks…`,
  `PROJECT_B: 73 tasks…`);
- `MPP_NO_CORPUS=1 ctest … -R '^mpp_reader$'`: passed, `SYNTHETIC: 3 tasks`, three
  `SKIP:` lines, `PASS: 52 checks`. This is the "no corpus" proof; `corpus/` was
  never renamed, because another stream may be reading it, and the env hook is
  re-runnable.

Traps hit here:

- `std::getenv` is ISO C++ but MSVC's CRT marks it deprecated (C4996, fatal under
  `/WX`). Do **not** add `_CRT_SECURE_NO_WARNINGS` (build.md rule 5); use
  `_dupenv_s` inside `#ifdef _MSC_VER` and `std::getenv` elsewhere.
- A freshly configured tree can fail the first build with
  `C1083: Cannot open compiler generated file … Permission denied` on several
  test targets at once; an immediate rebuild succeeds (suspected Dropbox/AV
  holding the new `.dir` directories). Not a code defect.
- PyYAML still parses the bare `on:` key as `True`; normalise before validating
  the workflow.

Not verified: macOS and Linux jobs for this change (needs the coordinator to
commit and push; agents cannot push), and `windows-latest` under Ninja has still
never run.

## Update 2026-10-22: first CI run (the experiment)

The coordinator merged and pushed the workflow. First run:

- **macos-appleclang: passed. ubuntu-gcc: passed.** First time this code was
  compiled by clang and by GCC — portability is now demonstrated, not argued.
- **windows-msvc: failed at configure**, in the build system rather than any
  stream's code:
  `CMake Error at CMakeLists.txt:15 (project): Generator Visual Studio 17 2022
  could not find any instance of Visual Studio`. `windows-latest` has moved on
  from the VS installation that generator can discover.
- Fix in `.github/workflows/ci.yml`: the Windows job now runs
  `ilammy/msvc-dev-cmd@v1` (the same environment `vcvars64.bat` provides) and
  configures with `-G Ninja`; no VS installation lookup anywhere. Local builds
  keep the Visual Studio generator because the Ninja configure hangs in the
  confined shell (below).
- `build/` was added to `.gitignore` by the coordinator; `docs/build.md` now
  points at the ignore rule instead of telling people to build under `logs/`.
- The `mpp_reader` exclusion stays until W6's harness fix, now carrying a dated
  `TODO(2026-10-22)` in the workflow header, the Test step and `docs/build.md`.


## What this stream is for

The project targets macOS but is developed on Windows and has never been built on
a Mac. CI is therefore the **portability oracle**: if `macos-latest` compiles the
tree and passes `ctest`, portability is evidenced; if it does not, we learn what
is wrong. Everything in this stream serves that one job.

## Files I own / changed

| Path | State |
|---|---|
| `CMakeLists.txt` | **rewritten**. Warning policy directory-wide, `enable_testing()` before subdirectories, guarded `add_subdirectory` for `core/mpp` + `core/sched`, `core_set_warnings()` kept for subdirectories that call it, `CMAKE_POSITION_INDEPENDENT_CODE ON`, C++20 defaults, Python3 found once |
| `.github/workflows/ci.yml` | **new**. 3-platform matrix, configure → build → ctest, `-DCORE_WARNINGS_AS_ERRORS=ON`, `--timeout 60`, one documented test exclusion |
| `docs/build.md` | **new**. Windows/macOS/Linux instructions, options, subdirectory contract, portability rules, traps with evidence, re-runnable verification |
| `memory/ci.md`, `docs/handoff/ci.md` | this file and the handoff |

Nothing else was edited. The reader (`core/mpp/`), scheduler (`core/sched/`),
`data/` and the shared docs were left alone. Build trees live in `logs/`
(git-ignored) and are not committed.

## The decision that matters: the CMake wiring

`core/mpp/CMakeLists.txt` and `core/sched/CMakeLists.txt` are self-contained and
owned by W6/W7. The top level includes them only if they exist:

```cmake
foreach(stream_subdir mpp sched)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/${stream_subdir}/CMakeLists.txt")
        add_subdirectory("core/${stream_subdir}")
    else()
        message(STATUS "core/${stream_subdir}: no CMakeLists.txt, skipped")
    endif()
endforeach()
```

Consequences a successor must keep:

- A **missing** subdirectory is tolerated (all four combinations occurred during
  the wave: neither, mpp only, both).
- An **existing but incomplete** subdirectory is not: a CMakeLists that
  references an unlanded source fails configure. That is intentional.
- Subdirectories are added after `core_model` so they can link
  `core_model` / `msproject::core` by target name.
- `enable_testing()` is at top level before `add_subdirectory()`; W6 and W7 also
  call it in their own directories, which is harmless.
- A new stream directory means editing the `foreach` list — a one-line change in
  a file only the build stream edits. Tell the coordinator if that is needed.

Warnings are applied with `add_compile_options` **before** the subdirectories, so
every target in the tree gets MSVC `/W4 /permissive- /utf-8` or GCC/Clang
`-Wall -Wextra -Wpedantic`, and `-Werror` / `/WX` when `CORE_WARNINGS_AS_ERRORS=ON`.
That is the point: a stream subdirectory cannot silently opt out of the
portability check. `core_set_warnings(<target>)` still exists because W6's
CMakeLists calls its own copy (`mpp_set_warnings`) and other files may call the
top-level one; the duplication of identical flags is harmless.

## CI workflow, exactly

_This section records the W8 revision. The W13 update at the top of this file is
the current state: the test step no longer carries `-E '^mpp_reader$'` and a
second, no-corpus reader step exists. Everything else below still holds._

- Matrix: `windows-latest` (Visual Studio 17 2022, MSVC), `macos-latest`
  (**arm64** AppleClang), `ubuntu-latest` (GCC). `fail-fast: false` so all three
  report; `timeout-minutes: 30`.
- Steps: `actions/checkout@v4`; configure
  `cmake -S . -B build -G "<gen>" -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON`;
  build `cmake --build build --config Release`; test
  `ctest --test-dir build -C Release --output-on-failure --timeout 60 -E '^mpp_reader$'`
  (W13: the `-E` is gone; see the top of this file).
- `shell: bash` everywhere (Git Bash on Windows) so the commands are identical on
  all three platforms and match `docs/build.md`.
- No caching, no packaging, no `--parallel`, no Ninja.

### The one exclusion, and why it is written down

_(Historical: W13 removed this exclusion on 2026-10-22; kept as the record of why
it existed.)_

`-E '^mpp_reader$'` skips W6's reader test. Its harness aborts on Windows
(`0xc0000409`, STATUS_STACK_BUFFER_OVERRUN / fast-fail abort; the coordinator saw
the modal CRT "Debug Error! abort() has been called" dialog in debug runs). On a
runner a modal dialog is a **hang**, not a failure. W6 is fixing the harness
(`_set_error_mode(_OUT_TO_STDERR)` plus try/catch). The exclusion is in the
workflow header comment and in `docs/build.md` so the green is honest: CI
currently proves the model and scheduler are portable, not the reader. Delete
`-E '^mpp_reader$'` once W6 reports the fix and re-run.

`--timeout 60` is the safety net for any other test that wedges; it is not a
substitute for removing the exclusion.

## Verification actually run (Windows, MSVC)

Toolchain: VS 2022 BuildTools, MSVC 19.44.35229, CMake 3.31.6. `cmake`, `ctest`,
`cl`, `clang` are not on PATH; `vcvars64.bat` adds `cl`, `cmake`, `ctest`,
`ninja`.

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B logs\w8-verify -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build logs\w8-verify --config Release
ctest --test-dir logs\w8-verify -C Release --output-on-failure --timeout 60 -E "^mpp_reader$"
python scripts/gen_field_tables.py --check
```

Results (2026-10-22):

- Configure `exit 0`, 6.6 s, `-- core/mpp: CMakeLists.txt found, included`,
  `-- core/sched: CMakeLists.txt found, included` (sched landed while I worked;
  the earlier "skipped" message was observed too).
- Build `exit 0` with `/W4 /permissive- /utf-8 /WX`: `core_model.lib`,
  `core_field_table_tests.exe`, `core_mpp.lib`, `mpp_reader_tests.exe` (W6),
  `core_sched_*` (W7). **No warnings from any stream's code.**
- `ctest` with the exclusion: **5/5 passed** (2 model + `core_sched_date`,
  `core_sched_duration`, `core_sched_calendar`).
- `ctest` without the exclusion: 5/6, `mpp_reader` fails `0xc0000409` (W6's bug,
  reported, not mine to fix).
- `core_field_table_tests.exe` → `PASS: 119614 checks`.
- `gen_field_tables.py --check` → `OK: … 3207 fields, 1763 named, 1444 with storage detail`.
- Header isolation: four one-line TUs (`core_c.h` as C `/TC`, `core_c.h` as C++,
  `field_table.h`, `field_table.gen.h`) each compiled standalone at `/W4 /WX` —
  **0 failures**, so missing includes are not hiding behind include order.
- `--clean-first` (no `--parallel`) rebuild: 10 s. Configure: ~6 s.

## Portability audit result

**No real code defect was found in the code that existed at the start of the
wave**, and none was invented to justify an edit. Checked, with evidence:

| category | finding |
|---|---|
| MSVC-only pragmas / keywords | none (`grep` for `#pragma`, `__declspec`, `_MSC_VER`, `_WIN32` over all sources: only `#pragma once`) |
| `#pragma once` vs guards | `field_table.gen.h` (generated) and `tests/test_paths.h.in` use it; supported by MSVC, Clang and GCC, so **not** a defect. Changing the generated header would require a generator change and would churn 467 KB; left alone deliberately |
| missing `<cstdint>`/`<cstring>` | none: all four headers self-contained (verified by compiling each as its own TU) |
| narrowing conversions | none: `/W4 /WX` clean, and the test's `equal()` template has no mixed signed/unsigned instantiations |
| path separators | C++ uses `std::filesystem`; CMake turns paths into C strings with `file(TO_CMAKE_PATH)` in `core/mpp`; tests use `logs/`-style forward slashes |
| MSVC CRT deprecation warnings | none triggered (`strcmp`, `std::ifstream`, no `strcpy`/`sprintf`). I deliberately did **not** add `_CRT_SECURE_NO_WARNINGS`: use the ISO C++ equivalent instead |
| C ABI validity | `core_c.h` compiles as C99 (`cl /TC`) at `/W4 /WX` |
| line endings / encoding | all sources LF-only, no CRLF; `/utf-8` is set for MSVC |

What the wave *did* fix is the build-level defect: before this wave the tree had
no platform matrix, no warnings-as-errors on GCC/Clang, and no way to include the
new subdirectories. That is now in place.

## Traps (all reproduced this session)

- **Ninja configure hangs.** `cmake -S . -B logs/w8-ninja -G Ninja` printed
  `-- Detecting CXX compiler ABI info` and was still running at 90 s; killed.
  `logs/w2-build/` is the corpse of an earlier identical attempt (compiler log,
  no `CMakeCache.txt`). Use `-G "Visual Studio 17 2022"` on this box.
- **`--parallel` fails silently and litters.** `cmake --build … --parallel
  --clean-first` and `--parallel 2` both exit **1 after 4–6 s** with only the
  MSBuild banner and no error, and leave ~90 orphaned `MSBuild.exe` processes
  (I killed them). Without `--parallel` the same clean rebuild succeeds in 10 s.
  Inference: MSBuild's `/m` worker nodes cannot run in the confined shell.
- **`ctest` not on PATH** unless `vcvars64.bat` ran (it is next to `cmake.exe`).
- **A wedged test hangs the runner**, not fails it — hence `--timeout 60` and the
  `mpp_reader` exclusion.
- **PyYAML parses the bare key `on:` as `True`** (YAML 1.1). When validating the
  workflow with `yaml.safe_load`, normalise `True` → `on` first; GitHub itself
  parses it correctly. Validation script: `logs/w8-validate-workflow.py`.
- **clang-tidy is present but not usable as a Clang oracle** here
  (`VC\Tools\Llvm\x64\bin\clang-tidy.exe`, LLVM 19): it rejects
  `--checks=-*,clang-diagnostic-*` with "no checks enabled" in this shell setup.
  Do not burn time on it; the macOS CI job is the real Clang check. There is no
  `clang.exe` / `clang-cl.exe` on the box, only clang-format/clang-tidy.

## Open / next three actions

1. ~~Coordinator: commit and push, then watch the first macos-latest run.~~
   **Done.** macOS and Linux are green. The Windows job is fixed in the workflow
   (Ninja + msvc-dev-cmd) but its first run under that generator is still to
   come — watch it, and if it fails, read whether the cause is MSVC setup, the
   generator, or a real compile error. Never drop Windows from the matrix.
2. ~~Remove `-E '^mpp_reader$'` from `.github/workflows/ci.yml` once W6's harness
   fix lands and re-run CI.~~ **Done by W13 (2026-10-22):** exclusion deleted,
   committed fixture added, a no-corpus ctest step added. The first CI run of
   this workflow revision is still to come (see the W13 section at the top).
3. Decide whether the target Mac is Intel: `macos-latest` is arm64. If it is an
   Intel Mac, add `macos-13` to the matrix (last Intel image) so the claimed
   platform is actually the tested one. (The committed reader fixture W13 added
   is no longer a blocker here.)

## Known gap in the coverage, stated plainly

**Closed by W13 (2026-10-22).** On a fresh clone `corpus/` is absent, but
`tests/data/synthetic-base.mpp` is committed, so `mpp_reader` now exercises the
reader against a real MPP14 file on every runner; only the PROJECT_A/PROJECT_B golden
comparisons and the `corpus/probe` sweep skip there. CI proves the reader's build
portability **and** its decode behaviour on synthetic data; it still cannot prove
agreement with MS Project for the client corpus (that comparison needs `corpus/`
and only runs in the main checkout).
