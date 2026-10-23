# tests/fuzz — W57 adversarial lane

Three deterministic fuzzers, plus two recursion-guard regression tests
(`fuzz_deep_nesting_guard_256` / `_1024`; enabled). This directory is
test/tooling code only; it adds no product source.

| target | what it attacks | default cost |
|---|---|---|
| `fuzz_reader` | seeded byte/count/truncation mutations of `tests/data/synthetic-base.mpp` through `mpp::load_bytes`; unmutated + 16 mutated passes over the two real files (read-only) | ~0.1 s (240 cases) |
| `fuzz_writer` | seeded sequences of the public `core/mppwrite` edit APIs over a copy of the synthetic fixture; every operation (and every refusal) proved from the bytes on disk | ~0.8 s (6 × 40 ops) |
| `fuzz_parsers` | mutated formulas through `core/formula` (tokenize → parse → evaluate) and mutated Props blobs through `core/props` | ~0.3 s (300 + 300 cases) |
| `fuzz_deep_nesting_guard_256` / `_1024` | require a positioned parse error at 256 / 1024 nesting levels (PASS in Release; FAIL under ASan until the guard depth is lowered — see F1) | ~0.04 s each |

Two modes, like the other stream subdirectories:

```bat
REM top-level (wired by the coordinator after this lane landed): the brief's
REM exact verify block
cmake -S . -B build\vs-w57 -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\vs-w57 --config Release --target fuzz_reader fuzz_writer fuzz_parsers
ctest --test-dir build\vs-w57 -C Release --output-on-failure -R fuzz
REM binaries land in build\vs-w57\tests\fuzz\Release\
```

```bat
REM standalone (how W57 first verified it; useful when the root configure is
REM busy with another stream's mid-write)
cmake -S tests/fuzz -B build\vs-w57-fuzz -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON
cmake --build build\vs-w57-fuzz --config Release --target fuzz_reader fuzz_writer fuzz_parsers
ctest --test-dir build\vs-w57-fuzz -C Release --output-on-failure -R fuzz
REM binaries land in build\vs-w57-fuzz\Release\
```

Every binary also runs standalone from the repo root and takes a longer seeded
mode. `--help` prints the exact options; the useful ones are `--cases`/`--ops`
(how much to run), `--seed` (reproduce a sequence), and `--case N`
(`fuzz_reader` single-case replay after a failure). `FUZZ_PRODUCT_TESTS=ON`
additionally builds the product tests the fuzzers share a tree with.

```bat
build\vs-w57\tests\fuzz\Release\fuzz_reader.exe --cases 5000 --seed 0x1234
build\vs-w57\tests\fuzz\Release\fuzz_writer.exe --seeds 50 --ops 200 --seed 0x99
build\vs-w57\tests\fuzz\Release\fuzz_parsers.exe --cases 5000 --seed 0xABCD
```

## Design decisions

**Determinism.** Every case is derived from `(seed, case index)` through
splitmix64; nothing reads the clock or the OS RNG. A failure prints the case
seed, the mutation list and the operation log, and the first eight cases are
replayed in-process on every run to prove reproducibility.

**Hang and crash detection (portable).** A tight loop or a stack overflow cannot
be interrupted portably from the same thread, so each fuzzer is a supervisor
process that re-executes itself with `--worker` and waits with a timeout
(`--timeout-ms`, default 30 s reader / 60 s writer+parsers). The worker rewrites
a one-line progress file before every case. On timeout the supervisor terminates
the worker (Windows `TerminateProcess`, POSIX `SIGKILL` after the platform's
`fork`/`waitpid`) and prints the last started case. A crash is reported with the
platform's exit code (`0xC00000FD` = stack overflow, `0xC0000005` = access
violation). The watchdog is in `fuzz_watchdog.h`; it is the only
platform-specific code in this lane and each branch is compiled on its own
platform. This is better than a per-case `alarm()` because `alarm()` is
POSIX-only, and better than a work counter because a count cap inside the reader
would be the very assertion under test. ctest also caps each test at 180 s.

**Reader contract.** A case passes when `mpp::load_bytes` either throws
`mpp::Error` or returns a `Document` that satisfies the invariants listed at the
top of `fuzz_reader_main.cpp`. Any other exception type is a failure ("the
failure contract is `mpp::Error`"). The invariants are deliberately weak enough
that the two real, damaged files still pass: they assert structural
self-consistency (row order, size-derived bounds, link endpoint flags,
outline-number presence, duplicate-uid warnings), not a particular schedule.

**Mutation model.** `fuzz_reader` asks `mppwrite::CfbDocument` for the byte
range of every stream in the pristine file, then mutates *stream-logical*
offsets (FixedMeta/VarMeta/Var2Data header counts, FixedData row words, VarMeta
entries) as well as the CFB header and 128-byte directory entries found by
scanning. The same model is built for the real files when they are present. All
mutations are bounds-checked against the current (possibly truncated) buffer.

**Writer contract.** The expected counts come from the operation log, not from
the writer's receipts: an `add_task` must raise the independent reader's task
count by one; a `delete_task` must lower it by one and lower the link/assignment
counts by the number of live dependent rows the previous reader view named; a
delete must leave the file size exactly unchanged (tombstones shrink nothing);
any successful write must not shrink the file. After every success the file on
disk must equal `doc.to_bytes()`, `check()` and `check_table()` must be clean,
and a written Text1 value must be visible to the reader. After every refusal the
file must be byte-identical (or still absent) and `doc.to_bytes()` unchanged.
Roughly one planned operation in five is deliberately invalid so refusals are
covered; the run fails if any operation kind never succeeds.

**Parser contract.** Formula: a parse failure must carry a message and an error
position inside the source; a parse success must keep field-ref positions inside
the source and evaluate without throwing. Props: a successful parse must keep
every record inside the blob (`offset + 12 + payload_length <= size`), records
monotonic, `tail_offset <= size`, `payload()` consistent, and all of
`extract_formulas`/`tail_properties`/`field_references`/`dump_document`/
`describe_tail` must run; a refusal must carry a message; the parse must be
deterministic. A per-case 2 s ceiling fails a slow case with its seed.

## Known product finding

`core/formula`'s recursive-descent parser and evaluator originally had no
nesting guard: 256 nested `IIf` calls or 384 nested parentheses overflowed the
stack (`0xC00000FD` on MSVC Release; 128 nested parentheses under ASan Debug).
**FIXED in Release on 2026-10-22**, and the ASan residual was closed in wave 23:
`core::formula::kMaxFormulaDepth = 64` is enforced by `parse()` and
`evaluate()` with a positioned error, and the guard tests at 256/1024 levels
pass in Release and under MSVC ASan Debug (the old 160-level guard was deeper
than an ASan stack survives).  The default fuzz run stays at depth ≤ 64, which is
now exactly the guard.  Full detail and repros: `docs/audit-findings.md` (F1).

## What this lane does not cover

- AppKit/ObjC++ (`app/main.mm`) and any UI path: only a macOS build compiles it.
- COM/MS Project behaviour: this machine cannot drive the oracle from a confined
  shell (docs/coordination.md revision 3).
- `core/usage`, `core/cal`, `core/plan`, `core/sheet`, `core/io`, `core/render`:
  other streams' units; the reader/writer fuzzers only exercise what the reader
  and writer expose.
- Whole-file *semantic* oracles: the writer fuzzer proves self-consistency and
  reader/writer agreement, not that Project reopens a mutated file the same way.
  That needs a Project run (coordinator).
- ASan and memory-safety: see `docs/audit-findings.md`; the fuzzers are run
  under `/fsanitize=address` in `build\vs-w57-asan`.
