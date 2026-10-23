# Building the core

The tree under `core/` is portable C++20: no Win32, no COM, no MFC. It is
developed on Windows (to be testable against the MS Project oracle) and targets
macOS. The same sources must build with **MSVC**, **AppleClang** and **GCC**, and
`ctest` must pass on all three.

Status of that claim:

| platform | compiler | verified? |
|---|---|---|
| Windows 10/11 x64 | MSVC 19.44 (VS 2022 BuildTools) | **yes, locally** — build + ctest below, 2026-10-22. In CI the Windows job (Ninja + `msvc-dev-cmd`) has not run yet |
| macOS (Apple Silicon) | AppleClang | **yes, CI 2026-10-22** — the first time this code was compiled by clang |
| Linux x64 | GCC (ubuntu-latest) | **yes, CI 2026-10-22** |

So portability is now evidenced rather than asserted: CI compiles and tests the
tree on macOS and Linux on every push and fails on any warning there — and the
first run passed both. The Windows CI job is the remaining unproven row.

## Prerequisites

| | needed | notes |
|---|---|---|
| CMake | ≥ 3.20 | 3.31.6 is what has actually been used |
| C++20 compiler | MSVC 19.3x+, AppleClang 15+, GCC 11+ | `std::from_chars`, `<bit>`-era C++20 |
| Python 3 | optional | only for `regenerate_field_table` and the `core_field_table_generator_check` test; CMake finds it with `find_package(Python3)` and skips both when absent |
| NSIS/git | not needed | there is no packaging step yet |

No third-party libraries, no fetch content, no vcpkg/Conan, no network access at
configure time.

## Windows (this machine)

`cmake`, `ctest`, `cl` and `ninja` are **not on PATH** by default. They ship with
Visual Studio BuildTools:

| tool | path |
|---|---|
| `vcvars64.bat` | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat` |
| `cmake.exe` | `…\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| `ctest.exe` | same directory as `cmake.exe` |
| `ninja.exe` | `…\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` (do not use it here — see Traps) |

`call vcvars64.bat` puts `cl`, `cmake`, `ctest` and `ninja` on PATH for the
current shell, so run it first (or open a "Developer PowerShell for VS 2022"):

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B logs\build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build logs\build --config Release
ctest --test-dir logs\build -C Release --output-on-failure
```

Notes:

- Use the **"Visual Studio 17 2022" generator**. It is multi-config, so the
  config goes on the *build* and *ctest* commands (`--config Release`,
  `-C Release`); `-DCMAKE_BUILD_TYPE` is accepted but ignored.
- **Build trees are git-ignored.** `.gitignore` covers both `build/` and
  `logs/`, so use either; the examples here use `logs\` (the convention from the
  earlier waves). Never commit a build tree.
- `-DCORE_WARNINGS_AS_ERRORS=ON` is how CI builds. It is worth running once
  before you ask for a commit, because CI is stricter than a default build.
- Debug is the default config for a multi-config generator: a plain
  `cmake --build logs\build` builds Debug, and `ctest -C Release` would then
  fail to find the executable. Always pass the config to both.

### Expected output

The test list grows as streams land their own tests; this is the tree as observed
on 2026-10-22 with no exclusions (`mpp_reader` included — it reads the committed
`tests/data/synthetic-base.mpp`, see "The reader fixture" below):

```
build exit 0
Release/core_model.lib
Release/core_field_table_tests.exe
core/mpp/Release/core_mpp.lib
 1/12 Test  #1: core_field_table ...................   Passed    1.3 sec
 2/12 Test  #2: core_field_table_generator_check ...   Passed    0.2 sec
 3/12 Test  #3: mpp_reader .........................   Passed    0.1 sec
 4/12 Test  #4: core_sched_date ....................   Passed    0.1 sec
 5/12 Test  #5: core_sched_duration ................   Passed    0.1 sec
 6/12 Test  #6: core_sched_calendar ................   Passed    0.1 sec
 7/12 Test  #7: mppwrite_roundtrip_cfb .............   Passed    0.2 sec
 8/12 Test  #8: mppwrite_inplace_patch .............   Passed    0.1 sec
 9/12 Test  #9: mppwrite_roundtrip_stream ..........   Passed    0.1 sec
10/12 Test #10: view_model .........................   Passed    0.1 sec
11/12 Test #11: io_mspdi ...........................   Passed    0.1 sec
12/12 Test #12: io_csv .............................   Passed    0.1 sec
100% tests passed, 0 tests failed out of 12
```

`core_field_table_tests` prints `PASS: 119614 checks` on success, and the
generator check prints
`OK: generated table matches the TSVs: 3207 fields, 1763 named, 1444 with storage detail`.
`mpp_reader_tests` prints `PASS: 106 checks` where `corpus/` exists, and
`PASS: 52 checks` with the corpus forced absent (the corpus-only checks print
`SKIP:`); both are a pass.

The configure log shows which stream subdirectories were wired in:

```
-- core/mpp: CMakeLists.txt found, included
-- core/sched: CMakeLists.txt found, included
```

`skipped` instead of `included` means that stream has not landed its
CMakeLists.txt yet; that is not an error.

## Test data

Two kinds, and only one of them is committed as `.mpp`:

- `tests/data/` is committed and always present in a build: the golden TSVs
  exported from MS Project, plus `tests/data/synthetic-base.mpp`, a 237,568-byte
  **synthetic** file (3 tasks + project row) generated by the coordinator through
  MS Project's COM automation. It is what lets `mpp_reader` run on a CI runner.
  Its provenance, SHA-256 and the "never replace it with a client file" rule are
  in `tests/data/README.md`.
- `corpus/` is read-only, git-ignored, and holds a client's construction
  programme. Tests that need it (PROJECT_A and PROJECT_B golden comparisons, the
  `corpus/probe` sweep) print `SKIP:` and pass when it is absent. Never copy a
  file out of it into a commit.

`tests/test_paths.h.in` exposes `TEST_DATA_DIR` (the absolute `tests/data/` path)
to any target that includes the configured header. The reader test does not need
it: it resolves the fixture beside `MPP_GOLDEN_PATH`, so it works from any
working directory. `MPP_NO_CORPUS=1` makes a machine that *does* have `corpus/`
behave like a runner — that is how the no-corpus path is verified without
touching the client data.

## The Qt shell's ctest gate

`app_qt/CMakeLists.txt` adds its tests at the 900x560 and 1920x1032 sizes with
`QT_QPA_PLATFORM=offscreen`.  Before wave 23 a fresh shell reported **0/15**
with `0xc0000135` because the Qt `bin` directory was not on `PATH`; the CMake
file now prepends the linked `Qt6::Core` directory through ctest's
`ENVIRONMENT_MODIFICATION`, so this works from any shell:

```bat
cmake -S app_qt -B build-qt -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH=build\Qt\6.5.3\6.5.3\msvc2019_64
cmake --build build-qt --config Release
ctest --test-dir build-qt -C Release --output-on-failure --timeout 180
```

That is 17 tests: the 15 shell tests plus `qt_task_order` and
`qt_task_order_corpus` (the corpus one is registered only where
`corpus/project_a.mpp` exists), and `qt_stored_table_corpus` follows the same
rule -- 17 locally, 16 in CI.

## The recalc golden is opt-in

`plan_recalc_tests` no longer rewrites `tests/data/project_a_recalc_golden.tsv`.
It compares by default and fails if the measured schedule differs; refreshing
the committed golden is a deliberate act:

```bat
set PLAN_RECALC_UPDATE_GOLDEN=1
build\...\plan_recalc_tests.exe
set PLAN_RECALC_UPDATE_GOLDEN=
```

The writer is diff-before-write and refuses to run after a failed check.  CI and
the local gate run `ctest -E plan_recalc`; the test itself skips its corpus
checks when `MPP_NO_CORPUS=1` is set.

## macOS

Xcode command-line tools are enough (no full Xcode for the core; the Swift app
will need Xcode later):

```sh
xcode-select --install        # once, if not installed
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

- The default generator (Unix Makefiles) is fine; there is no need for Ninja.
- `-C Release` is not needed with a single-config generator; if you pass it
  anyway ctest accepts it.
- `cmake` may come from Homebrew (`brew install cmake`) or from the CLT;
  either works. Python 3 ships with the CLT, so the generator check runs.
- AppleClang is strict about the same things GCC is; `-Wpedantic` is on. A
  warning here is a CI failure, so fix it rather than silencing it.
- `macos-latest` on GitHub is **arm64** (Apple Silicon). If the target machine is
  an Intel Mac, that is a second architecture still unproven — add `macos-13`
  (the last Intel image) to the matrix when it matters.

## Linux

Same commands as macOS, with GCC:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Options

| option | default | meaning |
|---|---|---|
| `CORE_BUILD_TESTS` | `ON` | build and register the top-level tests |
| `CORE_WARNINGS_AS_ERRORS` | `OFF` | add `/WX` (MSVC) or `-Werror` (GCC/Clang). CI sets it `ON` |
| `MPP_BUILD_TESTS` | `ON` | owned by `core/mpp` (W6); builds the reader tests |

Warnings are configured once, directory-wide, in the top-level `CMakeLists.txt`:
MSVC gets `/W4 /permissive- /utf-8`; GCC/Clang get `-Wall -Wextra -Wpedantic`.
Every target created in a subdirectory inherits them, which is the point — a
subdirectory CMakeLists cannot accidentally opt out of the portability check.
A `core_set_warnings(<target>)` function still exists for subdirectories that
call it; it is redundant now, not harmful.

## Adding a subdirectory (streams W6/W7 and later)

The top-level file is owned by the build/CI stream. A stream adds a
**self-contained** `core/<name>/CMakeLists.txt` — its own targets, include
directories, `enable_testing()` and `add_test()` — and the top level picks it up:

```cmake
foreach(stream_subdir mpp sched)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/${stream_subdir}/CMakeLists.txt")
        add_subdirectory("core/${stream_subdir}")
    else()
        message(STATUS "core/${stream_subdir}: no CMakeLists.txt, skipped")
    endif()
endforeach()
```

Rules that follow from that:

- The `if(EXISTS)` guard is deliberate: a missing subdirectory must not break
  the build, because the streams land asynchronously. A tree with the reader but
  not the scheduler still configures, builds and tests. `core/sched` was in
  exactly that state while this file was written.
- The guard does **not** cover an existing-but-incomplete subdirectory: if
  `core/mpp/CMakeLists.txt` exists and references a source file that has not
  landed yet, configure fails. That is intentional — a half-landed stream should
  be visible.
- Subdirectories are added *after* `core_model`, so they can link
  `core_model` / `msproject::core` by target name.
- `enable_testing()` is called at the top level before `add_subdirectory()`, so
  `add_test()` in a subdirectory registers with the top-level `ctest`.
- Honour `CORE_BUILD_TESTS` (or a documented per-directory option) so a
  CI/debug configure can build libraries without the tests.
- To add a new directory, message the build stream/coordinator: the list above
  is the one line that has to change, and only the build stream edits this file.

## Regenerating the field table

`core/model/field_table.gen.{h,cpp}` are generated from `analysis/*.tsv` and
committed. Never edit them by hand.

```sh
python scripts/gen_field_tables.py            # regenerate in place
python scripts/gen_field_tables.py --check    # verify the committed copy is current
cmake --build build --target regenerate_field_table   # same, through CMake
```

`--check` is also registered as the `core_field_table_generator_check` test, so
stale generated files fail `ctest`.

## Portability rules (what CI is checking)

1. **C++20, standard library only.** No `<windows.h>`, no COM, no MFC in
   `core/`. That is what lets the same code run on macOS.
2. **Every public header is self-contained.** Include what you use
   (`<cstdint>`, `<cstring>`, `<cstddef>`, `<string>`, …) instead of relying on
   another header having been included first. Verified by compiling each header
   as the only include in its own translation unit (see "Verification").
3. **The C ABI stays valid C.** `core/include/core_c.h` is the boundary the
   macOS app consumes; it must compile as C99 as well as C++. Do not add C++
   constructs to it (`namespace`, templates, `<cstdint>`-only names, …).
4. **`#pragma once` is fine** on all three CI compilers (MSVC, Clang, GCC). The
   generated header uses it and does not need to change. What is *not* fine is
   `#pragma warning`, `__declspec`, `__forceinline`, `#pragma comment(lib)`, or
   any other MSVC-only pragma/keyword outside `#ifdef _MSC_VER`.
5. **Do not define `_CRT_SECURE_NO_WARNINGS` to silence MSVC.** If MSVC
   deprecates a CRT function (`strcpy`, `sprintf`, `localtime`, …), use the ISO
   C++ equivalent (`std::string`, `std::format`, `std::chrono`). The code does
   not currently need that macro and should not grow it.
6. **Narrowing and signedness.** `/W4` and `-Wall -Wextra` both flag lossy
   implicit conversions and signed/unsigned comparisons in argument positions;
   write the `static_cast` with the intended type. Warnings are errors in CI.
7. **Paths.** In C++ use `std::filesystem` and `/`; in CMake use
   `file(TO_CMAKE_PATH ...)` for any path that becomes a C string literal (see
   `core/mpp/CMakeLists.txt`). Never concatenate a Windows `\` into a path that
   must work on macOS.
8. **Text files are UTF-8 with LF line endings.** MSVC is told this explicitly
   with `/utf-8`; do not introduce CRLF into sources or `.cmake` files.
9. **Static libraries stay position independent** (`CMAKE_POSITION_INDEPENDENT_CODE
   ON`) because they will be linked into a macOS dylib/framework.
10. **A test that needs data which is not committed must skip, not fail.**
    `corpus/` is git-ignored, so a test reading a real `.mpp` will not find it on
    a runner. Print `SKIP: …` and return success, and commit a synthetic fixture
    under `tests/data/` for the part that must run everywhere —
    `mpp_reader` does exactly that: `tests/data/synthetic-base.mpp` always runs,
    the PROJECT_A/PROJECT_B/`corpus/probe` checks skip. Never commit a client `.mpp`.

## What CI runs

`.github/workflows/ci.yml`: one job per platform, no caching, no packaging.
This is the first place portability is actually demonstrated: the 2026-10-22 run
was the first time this code met clang (macOS) and GCC (Linux), and both passed;
Windows failed only because the runner had moved on (below).

- **Windows builds with Ninja, not the Visual Studio generator.**
  `windows-latest` stopped exposing a Visual Studio installation that the
  "Visual Studio 17 2022" generator could find (`CMake Error … Generator Visual
  Studio 17 2022 could not find any instance of Visual Studio`), so the job runs
  `ilammy/msvc-dev-cmd@v1` — which sets up the same environment as `vcvars64.bat`
  — and configures with `-G Ninja`. Same compiler, same flags, no VS installation
  lookup. Locally the Visual Studio generator is still the right choice; see
  Traps.
- macOS and Linux use `Unix Makefiles`.
- Commands: `cmake -S . -B build -G "<generator>" -DCMAKE_BUILD_TYPE=Release
  -DCORE_WARNINGS_AS_ERRORS=ON`, `cmake --build build --config Release`,
  `ctest --test-dir build -C Release --output-on-failure --timeout 60`.
- **Warnings are errors everywhere** (`-DCORE_WARNINGS_AS_ERRORS=ON`), so a
  warning on any one compiler is a red build.
- `--timeout 60` per test: a test that wedges (a modal dialog on Windows, for
  example) fails in 60 seconds instead of hanging the runner for 6 hours. The
  job also has `timeout-minutes: 30`.
- **No test is excluded.** `mpp_reader` runs on a clean checkout because
  `tests/data/synthetic-base.mpp` is committed; the checks that need the real
  corpus print `SKIP:` and pass. A second step re-runs the reader with
  `MPP_NO_CORPUS=1` so the committed-fixture path is proved on every run rather
  than assumed. Details and the local equivalents: `docs/ci.md`.
- No `--parallel` anywhere, for the reason below.

## Traps (Windows, this machine)

All of these were reproduced on 2026-10-22; they are environmental, not defects in
`CMakeLists.txt`.

| symptom | cause | do this |
|---|---|---|
| `cmake -G Ninja …` prints `-- Detecting CXX compiler ABI info` and never returns (killed at 90 s) | observed: the Ninja generator's compiler probe never completes here; inference: the confined agent shell blocks something it needs. The same hang left `logs/w2-build/` with a compiler log and no `CMakeCache.txt` | use `-G "Visual Studio 17 2022"`. Never use Ninja on this box |
| `cmake --build … --parallel` exits **1 after 4–6 s** with only an MSBuild banner and no error, and leaves dozens of orphaned `MSBuild.exe` worker processes behind (≈90 counted) | observed: any `/m` value fails this way, while the same build without `--parallel` succeeds; inference: MSBuild's out-of-process worker nodes cannot talk to the parent in this shell | drop `--parallel`. The VS generator still parallelises internally; a clean rebuild takes ~10 s |
| `ctest` is not recognised | `ctest.exe` sits next to `cmake.exe`, which `vcvars64.bat` adds to PATH | run `vcvars64.bat` first, or call the full path |
| `ctest -C Release` fails with "unable to find executable" | build tree is Debug (multi-config default) | `cmake --build logs\build --config Release` |
| a test opens a modal dialog and the runner hangs | Windows CRT assertion in the test | add `--timeout`, and make the test write its diagnostics to stderr (`_set_error_mode(_OUT_TO_STDERR)`) so it fails instead of prompting |

A clean rebuild in the current tree (`cmake --build logs/w8-build --config Release
--clean-first`, no `--parallel`) takes ~10 s; the full configure ~6 s.

## Verification (re-runnable)

Windows, from the repo root, in a shell started with `vcvars64.bat`:

```bat
cmake -S . -B logs\w8-verify -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build logs\w8-verify --config Release
ctest --test-dir logs\w8-verify -C Release --output-on-failure --timeout 60
REM prove the reader works with no corpus at all (what a CI runner sees):
set MPP_NO_CORPUS=1
ctest --test-dir logs\w8-verify -C Release -R "^mpp_reader$" -V
set MPP_NO_CORPUS=
python scripts/gen_field_tables.py --check
```

The second `ctest` must print `SYNTHETIC: 3 tasks`, `SKIP:` for PROJECT_A/PROJECT_B/the
`corpus/probe` sweep, and `PASS: 52 checks`. Never rename or edit `corpus/` to
simulate a runner; `MPP_NO_CORPUS` is the supported way (see `docs/ci.md`).

Header isolation and C compatibility (the check behind portability rules 2 and 3)
— each file is a one-line translation unit whose only content is the include
shown in the comment:

```bat
REM t1.c   contains: #include "core_c.h"
cl /W4 /WX /TC /I core\include /I core\model /c t1.c
REM t2.cpp contains: #include "field_table.h"
cl /W4 /WX /std:c++20 /I core\include /I core\model /c t2.cpp
REM t4.cpp contains: #include "core_c.h"
cl /W4 /WX /std:c++20 /I core\include /I core\model /c t4.cpp
```

Where this was verified: MSVC 19.44.35229, CMake 3.31.6, 2026-10-22. There is no
Mac and no GCC on the development box; the 2026-10-22 CI run is what proved those
two (both green), which is the milestone that turned "portable by construction"
into "portable, demonstrated". Only the Windows CI job remains to be seen.
