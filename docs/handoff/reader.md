# W6 reader — handoff

_2026-10-22. Status: **the reader reads PROJECT_A and matches MS Project on every field
of all 83 live tasks.** Release build, watchdog-clean, no dialogs._

## Done

- `core/mpp/` — self-contained C++20 reader, no third-party code:
  - `cfb.{h,cpp}`: OLE2/CFB v3/v4, FAT + mini FAT + DIFAT, directory tree,
    space-insensitive path matching (`"   114/TBkndTask/FixedData"`).
  - `utf16.h`: UTF-16LE → UTF-8.
  - `reader.{h,cpp}`: Task table → `Document {project, tasks, skipped_rows,
    warnings}`; per-row accessors for id, UniqueID, name, duration (ticks and
    minutes), start, finish, priority, percent complete; dead-row skipping.
- `tools/reader_dump/` — CLI; TSV columns `uid id name duration_min start finish
  priority pct`, exactly MS Project's export shape. Options `--no-header`,
  `--skipped`, `--warnings`.
- `tests/mpp_reader_tests.cpp` — 72 checks. The central one compares every row
  of `tests/data/project_a_golden.tsv` by UniqueID on id / name hash (SHA-256) /
  duration / start / finish / priority / pct, prints a per-field mismatch table
  and the first five disagreements with both values.
- `tests/data/project_a-first10.tsv` and `tests/mpp_golden_probe.py` were **removed**:
  they encoded the pre-oracle 84-task assumption and are superseded by the
  coordinator's COM export.
- Verified commands and results are in `memory/reader.md`; the short version:

```
logs\w6\out\core_mpp\Release\mpp_reader_tests.exe   -> PASS: 72 checks
  PROJECT_A: 83 tasks, 1 skipped row, 7 warnings; golden: all fields 0 mismatches
  PROJECT_B: 73 tasks, 12 skipped rows
reader_dump corpus/project_a.mpp  ==  corpus/probe/project_a_golden_full.tsv  (no diff)
ctest --test-dir logs\w6\out\core_mpp -C Release    -> 1/1 passed
```

## Not done

- Only one VarMeta key is mapped (`0x0b40000e`, Name). Everything else in the
  Task table's variable store is W5's to map — and now worth re-checking,
  because the key is the PjField id (see below).
- No resources, links, assignments, calendars, baselines; no writer.
- `FixedMeta`'s +2 counter and +8 blob; the `0x80000000 | uid` VarMeta family;
  the UTC-vs-local rule for date-only fields (W1's open questions, untouched).
- No synthetic MPP fixture, so on a machine without `corpus/` the tests print
  SKIP and pass with only the formatting checks. W8 should decide whether CI
  needs a committed fixture; the golden file itself is committed and portable.

## Three findings that change other streams' plans

1. **VarMeta is 24 bytes of header + `(uid, offset, key)` triples.** W1's
   `spec/mpp-records.md` §5.1 (32-byte header, `(key, uid, offset)`, 4-byte
   trailer, "split entry") is misaligned by 8 bytes. Evidence: base.mpp's 42
   Var2Data blocks tile exactly under the new parse (42/42), the header's entry
   count equals the parsed count on both undamaged files (base 42, PROJECT_B 1437),
   the stream is 24 + 12n exactly, and key `0x0b40000e` lands on the name block
   in every file including the project row.
   **Consequence: the file key IS the COM PjField id.** `analysis/field_id_map.tsv`
   decodes Var2Data; W1's plan-changing finding #1 ("no file-side key space",
   `data/field_keys.tsv` is the only way) is wrong. W5 should re-check its table
   against this, and W1's spec section needs a correction note.
2. **PercentComplete is u16 at +92.** A u32 read gives 0x00640064 = 6,553,700 on
   every 100 % row (19 of 83 in PROJECT_A). +94 is a second percentage field.
3. **Rows with no VarMeta group are deleted slots, not tasks.** PROJECT_A: row 67
   (uid 10856) → 83 tasks. PROJECT_B: rows 1..12 → **73** live tasks, not 85 (the
   coordinator's note said 11 rows; it is 12, and 86 − 1 project − 12 = 73).
   FixedMeta cannot be the test: PROJECT_B still has FixedMeta entries for its dead
   rows. Liveness = "has a VarMeta group (i.e. a Name entry)".

Also worth knowing: names are **trimmed for display, kept raw for round-trip**.
PROJECT_A row 1's client task name is stored with a trailing space (not quoted); COM returns it trimmed and the
golden was exported that way, so `Task::name` is trimmed (without it one of 83
name hashes mismatches) while `Task::name_raw` holds the stored block verbatim
for a future writer (`ProjectRow::title` / `title_raw` likewise).

## Next three concrete actions

1. **Correct `spec/mpp-records.md` §5.1** (and the sentence in §0.4 about
   0x0b40000e holding notes) — the coordinator owns the file; the evidence is in
   `core/mpp/reader.cpp`'s header comment and `memory/reader.md`. W5's
   `data/field_keys.tsv` premise depends on it.
2. **Extend the reader one table at a time** (`TBkndRsc`, `TBkndCons`,
   `TBkndAssn`) using the same skeleton: parse FixedData rows, join VarMeta on
   uid, add a `Resource`/`Link`/`Assignment` record. The CFB layer already reads
   those streams; the row schemas are not mapped.
3. **Decide CI's corpus story** (W8 + coordinator): either commit a small
   synthetic MPP (hand-built CFB with a 2-task Task table) so the parser runs on
   the macOS/Linux runners, or mark the corpus tests as local-only in CI.

## Traps that cost time here

- **CFB header**: mini FAT start is `0x3C` and its sector count `0x40`. Using
  `0x40`/`0x44` reads the DIFAT terminator (`0xfffffffe`) as a count and asks for
  2.2 TB of mini FAT — that was the original "stream claims 2199023254528 bytes"
  abort, not a VarMeta problem.
- **Never let a test throw out of `main`**: on MSVC that calls `std::terminate`
  → `abort()` → a modal "Debug Error" dialog that blocks the desktop. The test
  calls `_set_error_mode(_OUT_TO_STDERR)` **under `#if defined(_MSC_VER)`** (the
  identifier does not exist in clang/gcc, so unguarded it breaks both CI
  runners), sets `std::cout << std::unitbuf`, and catches everything, returning 2.
- Toolchain is off PATH. `cmake`/`ctest` live under
  `...\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`, and
  `vcvars64.bat` must be sourced first. Use the "Visual Studio 17 2022"
  generator; never `cmake --build --parallel` (fails with 0 errors).
- `enable_testing()` lives in `core/mpp/CMakeLists.txt`, so `ctest` must point at
  `<build>/core_mpp` until the top level enables testing.
- PROJECT_A's VarMeta has entries with offset `0xffffffff`; a "read the block" helper
  must treat that as "not stored" rather than as a huge offset.
- `std::count_if` returns `ptrdiff_t` (C4244 under `/W4` if stored in `long`).
