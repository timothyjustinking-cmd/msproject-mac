# memory/fuzz.md — wave 13, W57 (adversarial robustness/fuzz)

_Updated 2026-10-22. Successor: read this, then `docs/handoff/w57.md`,
`docs/audit-findings.md` and `tests/fuzz/README.md`. The audit brief is
`docs/audit-handoff.md` §3. Nothing under `core/` was changed by this stream._

## What exists (all committed paths)

| path | what |
|---|---|
| `tests/fuzz/CMakeLists.txt` | the three targets (`fuzz_reader`, `fuzz_writer`, `fuzz_parsers`), the two enabled recursion-guard tests (`fuzz_deep_nesting_guard_256/1024`), and a standalone mode that adds core/mpp, core/mppwrite, core/formula(+sheet/view/model) and core/props with their own tests off. Wired into the top level by the coordinator on 2026-10-22 (one `add_subdirectory(tests/fuzz)` guarded by `EXISTS`). |
| `tests/fuzz/fuzz_common.h` | splitmix64 RNG, file IO, byte helpers, `Checker`, `Progress` (the watchdog's progress file), hex formatting. |
| `tests/fuzz/fuzz_watchdog.h` | supervisor/worker child-process watchdog. Windows `CreateProcessW` + `WaitForSingleObject` + `TerminateProcess`; POSIX `fork`/`execv`/`waitpid` + `SIGKILL`. The worker is re-executed with `--worker`; **the supervisor must ADD that flag** (an early bug stripped it and spawned an infinite process tree — fixed, but do not "simplify" it back). |
| `tests/fuzz/fuzz_reader_main.cpp` | 240-case mutation fuzz over `tests/data/synthetic-base.mpp` (`mpp::load_bytes`), 7 mutation families, structured sites from `mppwrite::CfbDocument::stream_file_ranges`, self-consistency checks, unmutated + 16 mutated passes over the two real files (read-only), `--case N` single-case replay. |
| `tests/fuzz/fuzz_writer_main.cpp` | 6 seeds × 40 operations over the public writer APIs (`add_task_row`, `delete_task_row`, `add_task_link`, `delete_task_link`, `add_resource_row`, `add_assignment_row`, `delete_assignment_row`, `save_field_values`, `clear_field_value`), ~20 % deliberately invalid; every success proved from disk + independent reader counts + size invariants; every refusal proved byte-identical; plus the damaged-table (real PROJECT_A) refusal control. |
| `tests/fuzz/fuzz_parsers_main.cpp` | formula tokenize/parse/evaluate over seeded + explicit edge cases (deep-nesting opt-in probes `--deep`, `--deep-guard`), and Props blob mutation over `tests/data/synthetic-props.bin` + the synthetic fixture's `114/Props`. |
| `tests/fuzz/README.md` | design + how to run + what is not covered. |
| `docs/audit-findings.md` | F1..F4, exact commands/seeds/counts, ASan results, Mac/COM gaps. |

## Verified (2026-10-22, MSVC 19.44)

- Release: `ctest -R fuzz` **5/5 passed** (three fuzzers +
  `fuzz_deep_nesting_guard_256/1024`, 1.32 s). Reader 240 cases: 145 ok / 95
  `mpp::Error`, real files 83/88/67 and 73/70/63. Writer 240 planned ops: 133 ok
  / 107 refused, 1478 checks. Parsers: 65,866 checks, Props 187/120.
- ASan Debug, **after W44's guard**: full regex is **6/9** — `formula` SEGFAULTs
  and both guard tests fail on the *guard being too deep for ASan* (80 levels
  overflow; 72 is safe), everything else passes. The clean subset
  (`^(mpp_reader|mppwrite_roundtrip_row|props|fuzz_reader|fuzz_writer|fuzz_parsers)$`)
  is **6/6, 0 failed, 41.0 s**, no sanitizer report. Before the guard landed the
  same nine-test run was 7/7 with the guard test Disabled.
- Longer ASan campaign (2026-10-22, pre-guard; inputs stay ≤64): `fuzz_reader --cases 2000` = 1,260 ok /
  740 `mpp::Error`, 0 failures, max case 43 ms; `fuzz_parsers --cases 1500` =
  354,600 checks, 0 failures (Props 926/581); `fuzz_writer --seeds 8 --ops 80` =
  343 ok / 297 refused, 3,841 checks, 0 failures. No ASan report. Numbers and
  logs are in `docs/audit-findings.md`.
- Fixtures were never written: `tests/data/synthetic-base.mpp` is 237,568 bytes
  and `mpp_reader` passes under ASan. **Its SHA256 does not match
  `tests/data/README.md`** (F3) — see "Traps".

## Findings

1. **F1 (Medium, product): Release FIXED; ASan margin still open.**
   W44 added `core::formula::kMaxFormulaDepth = 160` in `parse()`
   (`core/formula/expr.cpp`, `DepthGuard`) and `evaluate()` (`eval.cpp`), with a
   positioned error. Release now refuses 256/1024 nested parens and `IIf`
   ("offset 160: formula nesting exceeds 160 levels") and both guard tests
   pass. **Residual:** under ASan Debug 160 levels overflow before the guard
   fires (64 and 72 parse; 80–160 crash), so `formula_tests` SEGFAULTs and the
   guard tests fail in `build\vs-w57-asan`. Suggest lowering the constant to 64
   (or an iterative parser). Repro:
   `build\vs-w57-asan\tests\fuzz\Debug\fuzz_parsers.exe --deep-guard 160 --cases 1`.
   Reachable from `app/fields_model.cpp:166` (stored formula columns →
   `114/Props`).
2. **F2 (Info, tooling):** the brief's `-DCMAKE_CXX_FLAGS=/fsanitize=address`
   REPLACES CMake's default MSVC flags and loses `/EHsc`; with `/WX` the build
   dies on C4530. Use
   `-DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /W3 /GR /EHsc /fsanitize=address"`.
3. **F3 (Low, data provenance, coordinator action):** `tests/data/README.md`
   documents `SHA256 55C109F2…` for `synthetic-base.mpp`; on disk it is
   `092C51BD57F72E85B4700101E7C4D977DCBED68A3E2392E8CE76CDF580BBD04C`
   (237,568 B, mtime 20:00:50, predates this session) and
   `corpus/probe/base.mpp` is `FD2CF198…`. Content still behaves as documented;
   the hash (or the file) needs re-pinning. W57 did not write `tests/data`.
4. **F4 (Info, tree, FIXED):** top-level CMakeLists did not add `tests/fuzz`.
   The coordinator added
   `if(CORE_BUILD_TESTS AND EXISTS ".../tests/fuzz/CMakeLists.txt") add_subdirectory(tests/fuzz) endif()`.

No reader, writer or Props-parser defect was found. An early Props "payload past
the blob" hit was a **test bug**: a record header is 12 bytes, not 16.

## Traps (do not relearn)

- The supervisor must pass `--worker` to the child. Stripping it recursively
  spawns supervisors (seen once: hundreds of `fuzz_reader` processes). If that
  happens: `Get-Process fuzz_reader | Stop-Process -Force`.
- **Standalone builds of this lane put the exes in `<build>\Release\`; top-level
  builds put them in `<build>\tests\fuzz\Release\`.** Scripts in the docs use the
  top-level path.
- ASan Debug frames are much larger than Release: keep the default nested-formula
  cases at ≤64 or the ASan fuzz test stack-overflows (that is F1, and it will
  make `fuzz_parsers` red rather than reporting the finding cleanly).
- `core/mppwrite/table.h` shadows `core/view/table.h` for quoted includes. Never
  put `core_mppwrite` on the include path of a target that also compiles
  `core/formula` (sheet.h includes `"table.h"`). The CMakeLists links
  `fuzz_parsers` against `core_formula;core_props;core_mpp` only, for that
  reason.
- MSVC ASan has **no LeakSanitizer** and this tree has no UBSan/MSan; leak/UB
  coverage needs clang/macOS. Do not claim leaks were checked.
- Dropbox rewrites mtimes: edits fail with "file changed since it was read";
  re-read + edit in the same message, or retry.
- `fuzz_writer` writes only under the build tree (`FUZZ_OUT_DIR`) and copies the
  real PROJECT_A file there for the damaged-table control; never write `corpus/` or
  `tests/data/`.
- `fuzz_writer`'s first operation may run before any file exists on disk:
  `state.file_size` starts at the *pristine* size, not 0, or the delete size
  invariant false-fails.
- Writer test runs mirror fixtures to `core/mppwrite/build/vs-w54/test-out`
  (other stream's build dir). `mppwrite_roundtrip_row` under ASan does this;
  harmless build output, but concurrent with W54.
- `MPPWRITE_TEST_OUT` mirrors are git-ignored build output; do not commit them.
- ctest runs the fuzz tests from the repo root (`WORKING_DIRECTORY`); the macros
  are absolute paths so the cwd does not matter, but the progress files land in
  the build tree.

## Next three actions

1. **F1's ASan margin is open.** When W44 lowers `kMaxFormulaDepth` to 64 (or
   makes the parser iterative), re-run the full ASan regex; it must go 9/9.
   Today the guard tests and `formula` fail there (see F1). Do not weaken the
   guard tests to make the ASan gate green.
2. Run a clang/macOS ASan (with LeakSanitizer) pass over `fuzz_reader`,
   `fuzz_writer`, `fuzz_parsers` — the leak/UB half of the audit is still open,
   and it is also the only place the POSIX branch of `fuzz_watchdog.h` has ever
   been compiled.
3. Coordinator: re-pin F3's hash (or restore the fixture) and, if desired, run
   one long seeded campaign on the build box:
   `fuzz_reader --cases 20000`, `fuzz_writer --seeds 200 --ops 200`,
   `fuzz_parsers --cases 20000`, then open one written fixture in MS Project
   read-only (the fuzzers prove self-consistency, not Project acceptance).
