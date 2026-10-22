# Adversarial audit findings — W57 (robustness / fuzz), 2026-10-22

This is the wave-13 adversarial pass the audit brief (`docs/audit-handoff.md` §3)
asked for: deterministic mutation fuzzing of the reader, edit-sequence fuzzing of
the writer, formula/Props fuzzing, and an AddressSanitizer pass over the reader,
writer, formula, Props and fuzz tests on the synthetic fixture and the two real
files (read-only).

Everything here is re-runnable from the repo root with the commands in the last
section. No product source was changed; `tests/data/*.mpp` and `corpus/` were
never written. Build trees: `build\vs-w57` (Release) and `build\vs-w57-asan`
(Debug + ASan).

## Verdict

| id | severity | area | status |
|---|---|---|---|
| F1 | **Medium (product)** | `core/formula` recursion | **Release FIXED** (`kMaxFormulaDepth = 160`); **residual open**: the guard is too deep for ASan/small-stack builds (crash at 80 levels) |
| F2 | Info (tooling) | the documented ASan configure command | worked around here, documentation bug in the brief |
| F3 | Low (data provenance) | `tests/data/README.md` hash vs the committed fixture | open, coordinator action |
| F4 | Info (tree) | `tests/fuzz` not wired into the top-level build | fixed by the coordinator during this audit |

No reader, writer or Props-parser bug was found, and no ASan report outside F1.
The only sanitizer failures are the formula stack-overflow class recorded under
F1.

---

## F1 — `core/formula` recursion: the original crash is FIXED in Release, but the guard depth is still too deep for ASan / small stacks

**Status.** FIXED for Release on 2026-10-22: W44 added
`core::formula::kMaxFormulaDepth = 160`, enforced in `parse()`
(`core/formula/expr.cpp`) and `evaluate()` (`core/formula/eval.cpp`) with a
positioned error. Residual **open** problem: 160 levels cannot be reached under
MSVC ASan Debug (the stack overflows at ~80 levels of ASan frames), so the
sanitizer gate and `formula_tests` are still red. Severity stays **Medium** until
the constant is lowered or the parser/evaluator is made iterative.

**Severity: Medium.** A `.mpp` whose `114/Props` carries a deeply nested formula
could kill the app with `0xC00000FD` (STATUS_STACK_OVERFLOW) when the file
formula columns are built. It is a local, personal-use file, so this is a
crash-on-open / robustness defect, not a remote-code or data-integrity issue. It
is exactly the "deeply nested `IIf`/parentheses (recursion guard)" item in
`docs/audit-handoff.md` §3.

**Expected.** `core::formula::parse` either parses the expression or returns
`ok = false` with a non-empty message and `error_position <= source.size()`; it
must not crash and must not recurse without bound. (The reader's failure contract
is the model: file bytes never get to kill the process.)

### Original defect (pre-fix measurements, MSVC 19.44 Release, 1 MB stack)

| expression | depth | result |
|---|---|---|
| `((((1))))` | 128 | parses, 0 ms |
| `((((1))))` | 256 | parses, 0 ms |
| `((((1))))` | 384 | **0xC00000FD stack overflow** |
| valid nested `IIf(1,0,…)` | 128 | parses + evaluates, 0 ms |
| valid nested `IIf(1,0,…)` | 256 | **0xC00000FD stack overflow** |

Under ASan Debug, 128 nested parentheses already overflowed (this was how the
fuzzer first found it: the default explicit case at 128 crashed the ASan run).
Suspected sites were the recursive-descent chain in `core/formula/expr.cpp`
(eight frames per parenthesis level) and `eval_node` in
`core/formula/eval.cpp`; `EvalContext::max_depth` guards computed-column
resolution, not expression depth.

### Post-fix measurements (current tree, 2026-10-22)

Release — **PASS**, positioned refusal, no crash:

```
fuzz_parsers --cases 1 --deep 256
  deep parens 256: refused: offset 160: formula nesting exceeds 160 levels, 0 ms
  deep IIf 256:    refused: offset 1276: formula nesting exceeds 160 levels, 0 ms
fuzz_parsers --cases 1 --deep 1024
  deep parens 1024: refused: offset 160: ... , 0 ms
  deep IIf 1024:    refused: offset 1276: ... , 1 ms
```

ASan Debug — **still overflows below the guard**, because each recursive level
costs far more stack under ASan:

| depth | ASan Debug result |
|---|---|
| 32 | parsed, 2 ms |
| 64 | parsed, 4 ms |
| 72 | parsed, 3 ms |
| 80 | AddressSanitizer: stack-overflow |
| 88, 96, 128, 160 | AddressSanitizer: stack-overflow |

Consequences measured in `build\vs-w57-asan`:
`ctest -R "...|formula|props"` is **6/9**: `mpp_reader`,
`mppwrite_roundtrip_row`, `props`, `fuzz_reader`, `fuzz_writer`, `fuzz_parsers`
pass; `formula` **SEGFAULTs** (its own new deep-nesting cases, no checks printed
before the ASan report), and `fuzz_deep_nesting_guard_256` /
`_1024` fail with the worker dying of stack overflow. Release is **5/5**
(the same three fuzzers plus both guard tests).

**Exact repros (deterministic, no seed):**

```bat
REM Release: must PASS (it does, after the fix)
build\vs-w57\tests\fuzz\Release\fuzz_parsers.exe --deep-guard 256 --cases 1
build\vs-w57\tests\fuzz\Release\fuzz_parsers.exe --deep-guard 1024 --cases 1
REM ASan: still fails today (guard unreachable below the crash depth)
build\vs-w57-asan\tests\fuzz\Debug\fuzz_parsers.exe --deep-guard 160 --cases 1
build\vs-w57-asan\core\formula\Debug\formula_tests.exe
```

**Reachability.** `app/fields_model.cpp:166` parses every stored formula column
(`formula_columns_from_file`); the formula text comes from
`114/Props` → `msproject::props::extract_formulas`. So the crash is reachable by
opening a corrupt/hostile `.mpp`, not only by calling the parser directly.

**Suggested resolution.** Lower `kMaxFormulaDepth` to 64 (safe with margin: 72
parses under ASan) or make `parse`/`evaluate` iterative. Keeping 160 and raising
the build's stack is worse for the product: any non-main thread with a 512 KB
stack would still crash around depth 40. W44/coordinator owns the constant.

**Regression tests.** `tests/fuzz/fuzz_parsers_main.cpp` (`--deep-guard N`),
registered by `tests/fuzz/CMakeLists.txt` as ctest
`fuzz_deep_nesting_guard_256` and `fuzz_deep_nesting_guard_1024` — **enabled**
(the `DISABLED TRUE` was removed when the guard landed). They require
`!parsed.ok && !parsed.error.empty() && parsed.error_position <= source.size()`
for both nested parentheses and nested `IIf`. The default fuzz run deliberately
stays at depth ≤ 64 (16/32/64 parentheses and 8/16/32 `IIf`) so it stays safe
under ASan.

---

## F2 — the brief's ASan configure command drops MSVC's default flags (`/EHsc`)

**Severity: Info (tooling/documentation).** Not a product defect; it makes the
documented ASan command fail.

The brief's command

```bat
cmake -S . -B build\vs-w57-asan ... -DCMAKE_CXX_FLAGS=/fsanitize=address
```

**replaces** CMake's default MSVC `CMAKE_CXX_FLAGS` (`/DWIN32 /D_WINDOWS /W3 /GR
/EHsc`) instead of appending to it. With `/WX` on, the first build fails:

```
core/mpp/cfb.cpp(120,5): error C2220: the following warning is treated as an error
core/mpp/cfb.cpp(120,5): warning C4530: C++ exception handler used, but unwind
semantics are not enabled. Specify /EHsc
core/mpp/reader.cpp(781,5): same
```

Working configure used for this audit (same tree, same `/fsanitize=address`):

```bat
cmd /c "call vcvars64.bat >nul && cmake -S . -B build\vs-w57-asan -G \"Visual Studio 17 2022\" -A x64 -DCMAKE_BUILD_TYPE=Debug -DCORE_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_FLAGS=\"/DWIN32 /D_WINDOWS /W3 /GR /EHsc /fsanitize=address\""
```

Suggested doc/brief change: pass `-DCMAKE_CXX_FLAGS="/EHsc /fsanitize=address"`
(or `CMAKE_CXX_FLAGS_INIT` equivalent) so a clean checkout follows the same
command. `CMAKE_BUILD_TYPE` is accepted but unused by the Visual Studio
generator (harmless; `--config Debug` selects the configuration).

## F3 — `tests/data/README.md` declares a SHA256 the committed fixture does not have

**Severity: Low (data provenance / documentation).** Coordinator action; W57 did
not write `tests/data`.

`tests/data/README.md` says `synthetic-base.mpp` is
`SHA256 55C109F2F62FD6B9F05789AB002C35991A424AAB18C78F92E4200DF7B3FC6069`,
byte-identical to `corpus/probe/base.mpp`. Measured on 2026-10-22:

| file | SHA256 | bytes | mtime |
|---|---|---|---|
| `tests/data/synthetic-base.mpp` | `092C51BD57F72E85B4700101E7C4D977DCBED68A3E2392E8CE76CDF580BBD04C` | 237,568 | 20:00:50 today |
| `corpus/probe/base.mpp` | `FD2CF19805369B9AF45A68FEC71A21A3B8C1EE1111DC9AD6BD9FC09BBF2DC7F5` | 237,568 | 08:45:29 today |

The fixture still loads as the documented 3 tasks + 1 resource + 3 unassigned
placeholders (fuzz baseline and `mpp_reader` under ASan agree), and the reader
test passes, so this is a stale/incorrect hash or a regenerated file, not a
broken fixture. Command: `Get-FileHash tests\data\synthetic-base.mpp -Algorithm
SHA256`. The fixture's mtime predates this session, so W57 is not the cause;
re-pin the README hash (or restore the documented file) so the "reproducible from
a clean checkout" claim stays true.

## F4 — `tests/fuzz` was not wired into the top-level build (fixed during the audit)

The top-level `CMakeLists.txt` adds only the `core/<stream>` subdirectories, so
`-S . -B build\vs-w57 ... --target fuzz_reader` could not find the targets. W57
is not allowed to edit the top-level file; the coordinator added, during this
audit:

```cmake
if(CORE_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/CMakeLists.txt")
    add_subdirectory(tests/fuzz)
endif()
```

The lane also remains standalone-configurable (`cmake -S tests/fuzz -B ...`), which
is how it was first verified. Both paths build the same three targets.

---

## Investigated and dismissed

- **Props record geometry.** An early fuzz invariant flagged "a record payload
  runs past the blob". That was the *test's* mistake: a Props record header is
  12 bytes (`payload_length`, `id`, `kind`), not 16; `parse_props` checks
  `length <= size - offset - 12` and `PropsDocument::payload()` clamps to the
  blob. With the correct 12-byte check, 300 mutated Props blobs parse/refuse
  consistently and every record is inside the blob. No product change needed.
- **Reader damage classes.** The mutation families the brief lists (truncation,
  FixedMeta/VarMeta count corruption, VarMeta offsets, zero/0xFF fills, CFB
  header/directory/FAT corruption, garbage in stream payloads) produced no
  non-`mpp::Error` exception, no crash, no inconsistent `Document`, and no hang
  across the Release and ASan runs (counts below).
- **Writer refusals.** Every refusal observed (duplicate uid, uid 0, unknown uid,
  empty name, Cost field, duplicate link triple, self-link, damaged-table
  control) left the output file byte-identical and `doc.to_bytes()` unchanged.
- **Damaged-table control.** A copy of the real PROJECT_A file (whose Task `FixedMeta`
  header declares 87 entries and holds 88) is refused by both `add_task_row` and
  `delete_task_row`, and the file stays byte-identical — the writer does not
  guess a repair, as `spec/mpp-writer.md` §5.3 requires.

---

## Coverage — exact commands, seeds and counts

All seeds are decimal unless written `0x`; the default base seed for all three
fuzzers is **0x57**. Case seeds are derived deterministically:
`fuzz_reader` case *i* = `0x57 + i*0x9E3779B97F4A7C15` (real-file mutations add
`0x5EED0000`); `fuzz_parsers` formulas use that same sequence and Props adds
`0xC0FFEE00`; `fuzz_writer` sequence *s* uses `0x57 + s*0xD1B54A32D192ED03`.

### Release (`build\vs-w57`, top-level, `/W4 /WX`)

```bat
cmd /c '"...\vcvars64.bat" >nul && cmake -S . -B build\vs-w57 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"...\vcvars64.bat" >nul && cmake --build build\vs-w57 --config Release --target fuzz_reader fuzz_writer fuzz_parsers'
ctest --test-dir build\vs-w57 -C Release --output-on-failure -R "fuzz"
```

`ctest`: **5/5 passed, 0 failed** (the three fuzzers plus
`fuzz_deep_nesting_guard_256` and `fuzz_deep_nesting_guard_1024`), 1.32 s. The
two guard tests were registered DISABLED while F1 was open and are enabled now
that W44's `kMaxFormulaDepth` guard has landed.

`fuzz_reader` (240 cases, seed 0x57):

```
baseline: 3 tasks, 1 resources, 0 links, 3 assignments, 0 warnings
mutation sites: 564 structured words, 643 stream ranges
cases: 240, ok 145, mpp::Error 95, max case 0 ms, total 105 ms
  kind 0 (bit flip)        ok 24, error 8
  kind 1 (zero fill)       ok 26, error 12
  kind 2 (0xFF fill)       ok 25, error 9
  kind 3 (truncation)      ok 0,  error 44
  kind 4 (structured word) ok 21, error 3
  kind 5 (stream garbage)  ok 23, error 1
  kind 6 (saturated byte)  ok 26, error 18
real file project_a.mpp: 83 tasks, 88 links, 67 assignments (read only)
real file project_a.mpp: 16 mutations, ok 5, error 11
real file project_b.mpp: 73 tasks, 70 links, 63 assignments (read only)
real file project_b.mpp: 16 mutations, ok 5, error 11
PASS fuzz_reader: 12 checks, 0 failures
```

`fuzz_writer` (6 sequences × 40 operations, seed 0x57):

```
damaged-table control: add refused = yes (file left byte-identical)
  add_task: ok 17, refused 7        delete_task: ok 21, refused 14
  add_link: ok 12, refused 24       delete_link: ok 4, refused 21
  add_resource: ok 18, refused 4    add_assignment: ok 17, refused 8
  delete_assignment: ok 9, refused 8
  save_field: ok 21, refused 14     clear_field: ok 14, refused 7
operations: 133 succeeded, 107 refused, 0 failures
PASS fuzz_writer: 1478 checks, 0 failures
```

Every success proved: `CfbDocument::check()` and `check_table()` empty, file on
disk equal to `doc.to_bytes()`, independent reader counts equal to
before+delta, delete size unchanged, non-delete size not shrunk, Text1 visible to
the reader. Every refusal proved: file absent-or-byte-identical and
`doc.to_bytes()` unchanged. Runs at ~1.2 s.

`fuzz_parsers` (300 formula + 300 Props cases, seed 0x57, plus 26 + 6 explicit
edge blobs):

```
formula: tokenize ok 127, error 200; parse ok 29, error 298; evaluate ok 29, error 0
props: parsed 187, refused 120, formula blocks recovered 312
PASS fuzz_parsers: 65866 checks, 0 failures
```

Longer seeded modes use the same binaries with `--cases`/`--seeds`/`--ops`; e.g.
`fuzz_reader --cases 5000`, `fuzz_writer --seeds 50 --ops 200`,
`fuzz_parsers --cases 5000`.

### AddressSanitizer (`build\vs-w57-asan`, Debug, `/fsanitize=address`)

```bat
cmd /c '"...\vcvars64.bat" >nul && cmake -S . -B build\vs-w57-asan -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug -DCORE_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /W3 /GR /EHsc /fsanitize=address"'
cmd /c '"...\vcvars64.bat" >nul && cmake --build build\vs-w57-asan --config Debug'
ctest --test-dir build\vs-w57-asan -C Debug --output-on-failure -R "fuzz|mpp_reader|mppwrite_roundtrip_row|formula|props"
```

Result of the full regex: **6/9 passed, 3 failed** — and all three failures are
F1's guard being unreachable under ASan (see F1):

| test | result |
|---|---|
| `mpp_reader` (synthetic + PROJECT_A + PROJECT_B read-only) | Passed |
| `mppwrite_roundtrip_row` (writer round-trip, emits fixtures in the build tree) | Passed |
| `props` | Passed |
| `fuzz_reader` (240 cases + real-file mutations, 83/73 tasks) | Passed |
| `fuzz_writer` (6 × 40 ops) | Passed |
| `fuzz_parsers` (300 + 300 cases, default depth ≤ 64) | Passed |
| `formula` | **SEGFAULT** (AddressSanitizer: stack-overflow; F1) |
| `fuzz_deep_nesting_guard_256` | **Failed** (stack overflow before the guard; F1) |
| `fuzz_deep_nesting_guard_1024` | **Failed** (same) |

With the three F1 tests excluded
(`ctest -R "^(mpp_reader|mppwrite_roundtrip_row|props|fuzz_reader|fuzz_writer|fuzz_parsers)$"`)
the ASan tree is **6/6 passed, 0 failed**, total 41.01 s, and there is **no
AddressSanitizer report of any kind** outside F1:

| test | time |
|---|---|
| `mpp_reader` | 2.9 s |
| `mppwrite_roundtrip_row` | 1.16 s |
| `props` | 0.06 s |
| `fuzz_reader` | 4.46 s |
| `fuzz_writer` | 30.19 s |
| `fuzz_parsers` | 2.49 s |

The direct ASan runs reproduce the Release counts exactly (`fuzz_writer`: 133
succeeded / 107 refused, 1478 checks; `fuzz_reader`: 145 ok / 95 `mpp::Error`;
real files 83/88/67 and 73/70/63). Logs are left in the build tree:
`build\vs-w57-asan\tests\fuzz\Debug\asan-fuzz_{reader,writer,parsers}.log` and
`build\vs-w57\out\release-fuzz_*.log`.

**Longer ASan campaign (same build).** Also run on 2026-10-22, seed 0x57,
`--timeout-ms 300000`, logs in `build\vs-w57-asan\tests\fuzz\Debug\long-*.log`:

| run | result |
|---|---|
| `fuzz_reader --cases 2000` | 2,000 cases: **1,260** self-consistent Documents / **740** `mpp::Error`, 0 failures, max case 43 ms (ASan), 28.8 s; the two real files load (83/88/67, 73/70/63) and each takes 16 mutations (5 ok / 11 refused) |
| `fuzz_parsers --cases 1500` | 1,500 formula + 1,500 Props cases: **354,600 checks, 0 failures**; tokenize 583 ok / 944 error, parse 85 ok / 1,442 error, evaluate 85 ok / 0 error; Props 926 parsed / 581 refused, 1,830 formula blocks recovered |
| `fuzz_writer --seeds 8 --ops 80` | 640 planned operations: **343 succeeded / 297 refused**, 3,841 checks, 0 failures; damaged-table control refused PROJECT_A and left it byte-identical |

No sanitizer report in any of the three runs. The 2,000-case reader run is the
one that exercises every mutation family at scale (per family: 60–79 refusals,
0–377 for truncation, no crashes).

**ASan limitations to state, not hide.** MSVC's AddressSanitizer does not
implement LeakSanitizer, and this tree has no UBSan/MSan. So this pass covers
out-of-bounds reads/writes, use-after-free and stack overflow (F1 is exactly a
stack overflow, caught by ASan at depth 128), but **not** leaks or undefined
behaviour. Leak coverage needs a clang/macOS ASan (or ASAN_OPTIONS /
`-fsanitize=leak`) run on the Mac.

---

## What only a Mac or COM run can cover

- `app/main.mm` and AppKit/ObjC++: never compiled here (MSVC ignores it). F1's
  reachability through `app/fields_model.cpp` was read from the code, not
  exercised in a UI.
- COM / MS Project: this machine cannot drive the oracle (docs/coordination.md
  rev. 3 — COM hangs in a confined shell). So this audit can prove the fuzzers
  never crash or corrupt, but not that Project would accept any mutated file or
  that a Project reopen reproduces our counts.
- LeakSanitizer/UBSan results, and the real macOS stack size for F1.
- MPP12 (no fixture on disk), and the full `core/usage`, `core/cal`,
  `core/plan`, `core/sheet`, `core/io`, `core/render` units (other streams).

## Re-run cheatsheet (from the repo root)

```bat
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
REM Release gate
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w57 -G `"Visual Studio 17 2022`" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON"
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w57 --config Release --target fuzz_reader fuzz_writer fuzz_parsers"
ctest --test-dir build\vs-w57 -C Release --output-on-failure -R "fuzz"
REM the two nesting-guard regression tests (PASS in Release since the guard)
build\vs-w57\tests\fuzz\Release\fuzz_parsers.exe --deep-guard 256 --cases 1
build\vs-w57\tests\fuzz\Release\fuzz_parsers.exe --deep-guard 1024 --cases 1
REM ASan gate
cmd /c "call `"$vc`" >nul && cmake -S . -B build\vs-w57-asan -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_BUILD_TYPE=Debug -DCORE_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_FLAGS=`"/DWIN32 /D_WINDOWS /W3 /GR /EHsc /fsanitize=address`""
cmd /c "call `"$vc`" >nul && cmake --build build\vs-w57-asan --config Debug"
ctest --test-dir build\vs-w57-asan -C Debug --output-on-failure -R "fuzz|mpp_reader|mppwrite_roundtrip_row|formula|props"
:: today: 6/9 — formula + the two guard tests fail on F1's guard depth; for a
:: green ASan run before that is fixed, use:
::   -R "^(mpp_reader|mppwrite_roundtrip_row|props|fuzz_reader|fuzz_writer|fuzz_parsers)$"
```

Long seeded runs (standalone, any config):

```bat
build\vs-w57\tests\fuzz\Release\fuzz_reader.exe --cases 5000 --seed 0x1234
build\vs-w57\tests\fuzz\Release\fuzz_writer.exe --seeds 50 --ops 200 --seed 0x99
build\vs-w57\tests\fuzz\Release\fuzz_parsers.exe --cases 5000 --seed 0xABCD
```

`fuzz_reader --case N` replays one case after a failure; the failing invocation
prints the exact case seed and mutation list.
