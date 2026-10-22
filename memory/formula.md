# memory/formula.md — W44: custom fields, formula parser/evaluator, computed columns

_2026-10-22, wave 10, stream W44; updated later the same day for the wave-13
fuzz follow-up (nesting guard). Owner: `core/formula/**`,
`tests/formula_tests.cpp`, this file, `docs/handoff/w44.md`. No git command was
run; the coordinator commits. No COM/MS Project access was used._

## One line

`core::formula` parses Project's custom-field formula language (numbers, strings,
`[Name]` / `[MSPJ<id>]` field refs, `+ - * / &`, comparisons, `And/Or/Not`,
`IIf`, date/duration functions) into a positioned-error AST with an explicit
nesting bound, evaluates it against one `core::view::RecordSet` row through
**one** accessor (`ComputedColumns::value_of_field`), and owns the Task
custom-field registry plus the measured VarMeta encodings
(Text/Number/Duration/Date), tagging the two unmeasured ones (Flag, Cost)
explicitly.

## Files

| path | what |
|---|---|
| `core/formula/expr.h/.cpp` | tokenizer (positions, duration/date/string/bool literals, `[MSPJ...]`), AST (`Expr` value tree), recursive-descent parser with the `kMaxFormulaDepth` nesting guard, positioned errors |
| `core/formula/eval.h/.cpp` | `EvalContext`/`EvalResult`/`evaluate`, `base_value_of_field`, `truthy`, `to_text`; the documented semantic rules and functions; the same depth bound enforced over the AST |
| `core/formula/fields.h/.cpp` | measured custom-field anchors, registry built by scanning the generated descriptor table, `resolve_field_token`/`field_id_from_mspj`, packed-date and UTF-16LE/f64/u32 encode/decode, tick↔COM-minute helpers, storage confidence |
| `core/formula/columns.h/.cpp` | `ComputedColumn{field_id,name,formula}`, `ComputedColumns` (`add`, `evaluate`, `value_of_field`, `add_to_sheet` via `SheetModel` public API only) |
| `core/formula/CMakeLists.txt` | `core_formula` / `msproject::formula`, links PUBLIC `core_sheet`; test target `formula_tests`, ctest name `formula` |
| `tests/formula_tests.cpp` | **1249 checks**, self-contained harness (no corpus, no other stream's test header); every label tagged `[measured]` or `[inference]`; includes the nesting-guard suite |

## Measured ground truth this builds on (source: `docs/analysis/oracle/w10-oracle.md` §2)

Custom-field ids (Task), in `core/formula/fields.h` as `kTask*` constants and
re-derived from the generated table by the registry:

| field | id | storage (measured) |
|---|---|---|
| Text1 | `0x0b400033` | UTF-16LE + NUL (12 B for "alpha") |
| Flag1 | `0x0b400048` | **not in VarMeta — open** |
| Number1 | `0x0b400057` (Number2 `0x0b400058`, … Number20 sequential) | LE f64 (8 B) |
| Duration1 | `0x0b400067` | u32 file ticks (4800 = 1 day; COM minutes ×10) |
| Cost1 | `0x0b40006a` | **not measured — our choice f64** |
| Date1 | `0x0b400109` | u32 packed `(day<<16)|(sec/6)`, day 0 = 1983-12-31 |

Registry counts (from the generated table, asserted in the test):
Text1..30, Number1..20, Date1..10, Flag1..20, Duration1..10, Cost1..10 = **100**.

Formula definition tokens (measured): `[Duration]` → `[MSPJ188744967]` =
Task.DurationText (`0x0b400507`); `[% Complete]` → `[MSPJ188743712]` =
Task.PercentComplete (`0x0b400020`). The parser accepts both the token form and
the UI spelling; `DurationText` evaluates as Task.Duration (fallback in
`base_value_of_field`).

Measured anchors used as byte-level tests:
- packed date `0x3d1812c0` = 2026-10-26 08:00 and that date is a Monday
  (oracle's `GetField` read "Mon 26/10/26"), so `Weekday` = 2 with Sunday=1.
- Number 42.5 → `00 00 00 00 00 40 45 40`; 3.25 → `… 0a 40`.
- Duration 4800 → `c0 12 00 00`; Date1 → `c0 12 18 3d`; Text "alpha" → 12 bytes.
- `duration_ticks_to_com_minutes(4800) == 480` (the x10 COM/file boundary).

## Semantic decisions — all inference, documented in `eval.h`

- Numeric literals are `Number` (Project numbers are doubles); integer-typed
  fields (PercentComplete, Day(),…) stay `Integer`.
- Duration arithmetic: `Duration op Duration` stays Duration except `/` which is
  a Number ratio; a bare number in Duration arithmetic means **days**;
  `Duration * number` and `Duration / number` stay Duration; ×/÷ by zero → Null.
- Date arithmetic is **calendar-free**: whole 4800-tick blocks are calendar
  days, leftover ticks are 6 s/tick wall-clock (so +1d = next day same time,
  +4h = four hours later). `d1 - d2` is the exact inverse: whole calendar days
  ×4800 + time-of-day difference in ticks. Project would use the task calendar;
  this module has none.
- Comparisons use `core::view::compare_values` — the same coercion sort/filter
  use (Text "8" equals Integer 8).
- Null propagates through arithmetic/comparison/functions; `&` treats Null as
  "", `IIf` treats a Null condition as false; `IsNull()` is the explicit test.
  Type mismatches are Null, not an error value (view::Value has none).
- Truth: Bool; Null false; number/duration non-zero; date valid; Text non-empty
  and not "0"/"no"/"false".
- Functions: `IIf` (lazy), `ProjDurValue` (minutes), `ProjDurConv` (d/h/m/w/mo/s
  with 4800/600/10/24000/96000/×6 ticks), `ProjDateAdd`, `ProjDateSub`,
  `ProjDateDiff`, `DateValue`, `Day`, `Month`, `Year`, `Hour`, `Minute`,
  `Weekday` (Sunday=1), `IsNull`. `&` renders values with core/sheet's display
  formatting ("1 day", "Yes").
- Precedence: Or < And < comparisons < `&` < `+ -` < `* /` < unary.
- String escapes use `""` for an embedded quote; date literals are `#...#`
  ISO-only; comma is the argument separator (Project's US form).

## Nesting guard (wave-13 fuzz follow-up; lowered again for ASan)

`core::formula::kMaxFormulaDepth = 64` in `expr.h` is the single shared bound.
Nesting **up to and including 64** is accepted; **65 is the first refused level**
(parser and evaluator agree).

Why 64 and not 128/160.  The recursive-descent parser died with `0xC00000FD` on
**256 nested `IIf`** / **384 nested parentheses** in MSVC Release (W57 measured;
128 was good), so a bound of 160 was added first.  Under AddressSanitizer the
instrumented frames are much larger: in `build\vs-w57-asan` (Debug,
`/fsanitize=address`) **64 and 72-deep parsed but 80/88/96/128/160 overflowed**
before a 160 guard could fire.  64 is below the ASan-measured good depth, so it
is safe on every build and thread; real formulas are fewer than ten levels deep,
so 160 bought nothing.  **Re-measure under ASan before ever raising it.**

Two guards, one constant:
- **Parser recursion depth**: RAII `DepthGuard` in `parse_or` (every
  parenthesised group, function argument and binary operand re-enters there) and
  in `parse_unary` (`- - - ...` / `Not Not ...` chains recurse there).  Both
  boundaries are 64 allowed / 65 refused.  Errors are positioned:
  `"offset N: formula nesting exceeds 64 levels"`.  No exceptions.
- **AST tree depth** (`Expr::depth`, edges, leaf = 0): `finish_node()` computes
  each new node's height and refuses when it exceeds 64.  This exists because a
  long **flat** chain (`1 And 1 And ...`, `1+2+3+...`) parses iteratively but
  builds a left-deep tree whose *recursive destructor* overflowed the stack under
  ASan when the parser returned it.  Parsed output is therefore always shallow
  enough to destroy and evaluate; a >64-operator flat chain is refused at parse
  time.  Do not remove this guard thinking it is redundant with the depth guards.
- **Evaluator**: `check_structure(expr, depth, error)` checks depth BEFORE
  recursing (root = 0, same `> 64` rule), so a hand-built deep tree is refused
  with `ok=false` and the same message; `eval_node` carries a defensive depth.
- Tests (`test_nesting_limits`): 48/64-deep parens and IIf parse+evaluate;
  65/128/256/384/1024/4096 nested `(` refused with the positioned message; 65/128
  IIf refused; 48-deep unbalanced still reports `expected ')'`; right-nested
  arithmetic 64 parses+evaluates to 65 / 128 refused; right-nested `And` 64 ok /
  128 refused; unary chains 64 ok / 65 and 128 refused; 32-term flat chain parses
  and evaluates; 4096-term flat chain refused at parse; hand-built 64-deep AST
  evaluates and 65/80-deep ASTs are refused.

## Storage encoding (fields.h/f.cpp)

`encode_value`/`decode_value` per `FieldKind`; wrong kinds are rejected
(Duration storage will not accept a bare Integer — the ticks/minutes mistake is
made impossible). `storage_block_size` = 0 (Text, variable), 8 Number, 4
Duration, 4 Date, 4 Flag, 8 Cost. `storage_confidence`: Text/Number/Duration/Date
**Measured**; Flag/Cost **Unmeasured**. Flag bytes are our documented choice
(u32 LE 0/1); Cost is f64.

## Verification (MSVC 19.44 Release `/W4 /permissive- /utf-8 /WX`; MSVC ASan

Debug `/fsanitize=address`)

Release tree:

```powershell
cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake -S . -B build\vs-w44b -G "Visual Studio 17 2022" -A x64 -DCORE_WARNINGS_AS_ERRORS=ON'
cmd /c '"...vcvars64.bat" >nul && cmake --build build\vs-w44b --config Release --target formula_tests'
.\build\vs-w44b\core\formula\Release\formula_tests.exe
#   formula_tests: 1249 checks passed          (1179 pre-guard; 1223 at 160; 1249 at 64)
$ctest='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $ctest --test-dir build\vs-w44b -C Release --output-on-failure -R "formula"
#   1/1 Test #40: formula ... Passed
```

ASan tree (this is the gate that was red at 160; 64 passes):

```powershell
cmd /c '"...vcvars64.bat" >nul && cmake -S . -B build\vs-w44-asan -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug -DCORE_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /W3 /GR /EHsc /fsanitize=address"'
cmd /c '"...vcvars64.bat" >nul && cmake --build build\vs-w44-asan --config Debug --target formula_tests fuzz_parsers'
cmd /c '"...vcvars64.bat" >nul && "<repo>\build\vs-w44-asan\core\formula\Debug\formula_tests.exe"'
#   formula_tests: 1249 checks passed          (ASan clean, exit 0)
cmd /c '"...vcvars64.bat" >nul && "<repo>\build\vs-w44-asan\tests\fuzz\Debug\fuzz_parsers.exe" --deep-guard 256 --cases 1'
#   PASS fuzz_parsers: 250 checks, 0 failures
cmd /c '"...vcvars64.bat" >nul && "<repo>\build\vs-w44-asan\tests\fuzz\Debug\fuzz_parsers.exe" --deep-guard 4096 --cases 1'
#   PASS fuzz_parsers: 250 checks, 0 failures
```

Note: the ASan exe must be launched through vcvars (or with the MSVC bin dir on
PATH) so `clang_rt.asan_dynamic-x86_64.dll` resolves, or the loader fails.
`-DCMAKE_BUILD_TYPE` is ignored by the VS generator (harmless warning).

The wave-13 repro, rebuilt against the fixed core (Release):

```powershell
cmd /c '"...vcvars64.bat" >nul && cmake --build build\vs-w44b --config Release --target fuzz_parsers'
.\build\vs-w44b\tests\fuzz\Release\fuzz_parsers.exe --deep-guard 256 --cases 1
#   PASS fuzz_parsers: 250 checks, 0 failures
.\build\vs-w44b\tests\fuzz\Release\fuzz_parsers.exe --deep-guard 1024 --cases 1
#   PASS fuzz_parsers: 250 checks, 0 failures
.\build\vs-w44b\tests\fuzz\Release\fuzz_parsers.exe --cases 1
#   PASS fuzz_parsers: 248 checks, 0 failures   (the 2 extra are the deep-guard probes)
```

W57's own trees were stale (static lib built before this change). I rebuilt the
affected targets there so the audit gate reflects the fix, and they pass:
`build\vs-w57\tests\fuzz\Release\fuzz_parsers.exe --deep-guard 256 --cases 1`
(250/0) plus `build\vs-w57\core\formula\Release\formula_tests.exe` (1249 checks);
after `cmake --build build\vs-w57-asan --config Debug --target fuzz_parsers
formula_tests`, the ASan probe (250/0) and the ASan `formula_tests.exe` (1249
checks passed).

`tests/fuzz/CMakeLists.txt` still registers `fuzz_deep_nesting_guard` with
`DISABLED TRUE` (W57 owns that file; the ctest listing shows test #46 Disabled).
W57 can drop `DISABLED` now — the probe passes in Release and under ASan.

Configure log confirms `core/formula: CMakeLists.txt found, included` (top-level
subdir foreach needed no change; only `core/formula/CMakeLists.txt` was added).

## Open (ranked)

1. **Flag storage is still the open measured question.** Next oracle: a two-file
   diff (Flag1 = Yes vs no Flag1) on the existing `fieldvalues` fixture; also set
   Cost1 once in the same run. Then flip `storage_confidence` and the byte tests.
2. **Evaluator semantics have no oracle.** Project does not compute custom
   fields into `GetField` headlessly; a UI-driven recalculation or a saved-file
   diff of formula results (if Project ever persists them) is the evidence path.
3. **`114/Props` is not read or written.** A formula survives only as text in
   that stream; W44 parses tokens but does not decode Props into
   `ComputedColumns` nor write it (wave brief says do not).
4. **Date arithmetic is calendar-free.** `ProjDateDiff`/date±duration ignore
   working hours/calendars; Project's equivalents consult the task calendar.
   `Row`/`RecordSet` carry no calendar, so this needs a `core/cal` hook.
5. **Comparisons are case-sensitive** for text; Project's formula `=` may be
   case-insensitive (unverified).
6. **Locale separators not supported** (semicolon argument separator, decimal
   comma). The measured Props fragment uses commas.
7. **`add_to_sheet` uses a copy of the set** so it does not mutate `*this`; a
   caller that wants the formula retained should `add()` it first.
8. **Standalone CMake configure** (`cmake -S core/formula`) was not exercised
   (build-dir rule); the root path is the CI path.
9. **The fuzz gate is still `DISABLED`.** `tests/fuzz/CMakeLists.txt` line ~186
   registers `fuzz_deep_nesting_guard` with `DISABLED TRUE`; that file is W57's,
   not mine. The probe passes in Release and under ASan (`--deep-guard 256` ->
   250 checks, 0 failures), so W57 can remove `DISABLED`.
10. **Flat formulas are capped at ~64 operators (AST depth).** The tree-depth
    guard refuses a chain whose left-deep AST would exceed 64 edges, because the
    recursive destructor is as unsafe as the evaluator. A formula legitimately
    needing hundreds of flat terms would need an iterative AST representation,
    not a bigger constant.
11. No app/Gantt/report UI wiring; `value_of_field` is the integration point.

## Traps

- **Do not raise `kMaxFormulaDepth` without re-measuring under ASan.** 64 is the
  largest depth measured safe there (72 parsed, 80 overflowed); Release alone
  tolerates more, which is exactly the trap that made 160 look fine. The tests
  require 64 nested parens/IIf to parse, so the effective floor is 64 too.
- **Do not remove the `finish_node` tree-depth guard** as if it duplicated the
  `parse_or`/`parse_unary` depth guards: those bound parser recursion, while a
  long flat chain builds a deep left-nested AST whose *destructor* overflows.
  Only `finish_node` keeps parsed output safe to destroy.
- **Do not remove the `parse_unary` guard** — `Not Not ...` and `- - -` chains
  recurse there without re-entering `parse_or`.
- **Duration ticks are not wall-clock.** `kTicksPerDay` = 4800 = an 8-hour
  working day; a date's 08:00 was made "one day" by using ticks for date math.
  The fixed rule is in `eval.cpp`: whole days + 6 s/tick remainder. Do not
  "simplify" it back.
- **Member-name hiding**: inside `ComputedColumns`, an unqualified `evaluate(ast,
  ctx)` resolves to the member `evaluate(RecordSet&)` and fails to compile.
  Qualify `core::formula::evaluate`.
- **`Value` has no `operator==`** — tests compare `kind` plus the member.
- **Custom-field ids beyond the six anchors must come from the registry**
  (`find_custom_field(kind, index)`); do not hand-write `0x0b4000xx`.
- Registry scan accepts only `<prefix><digits>` starting at index 1; it
  deliberately excludes names like `EnterpriseText1`/`BaselineDurationText`.
- Dropbox mtime churn makes the edit tool fail with "file changed since read";
  re-read the exact region immediately before each edit.
- The test binary is at `build\vs-w44b\core\formula\Release\formula_tests.exe`,
  the ASan one at `build\vs-w44-asan\core\formula\Debug\`, and
  `fuzz_parsers.exe` at `build\vs-w44b\tests\fuzz\Release\`; ctest name is
  `formula`.

## Next three actions

1. **Coordinator**: commit `core/formula/**`, `tests/formula_tests.cpp`,
   `memory/formula.md`, `docs/handoff/w44.md`; verified in `build\vs-w44b`
   (Release, 1249 checks) and `build\vs-w44-asan` (Debug ASan, 1249 checks +
   fuzz probes 250/0).
2. **W57**: remove `DISABLED TRUE` from `fuzz_deep_nesting_guard` in
   `tests/fuzz/CMakeLists.txt` (W57 owns it); the ctest must now pass. Its
   `build\vs-w57-asan` binaries were rebuilt against this fix and pass; keep the
   ASan gate's build step so they stay current.
3. **Successor (formula)**: decode `114/Props` into `ComputedColumns`, then wire
   a computed column into the app's sheet through `add_to_sheet` and into the
   report path through `value_of_field`.

