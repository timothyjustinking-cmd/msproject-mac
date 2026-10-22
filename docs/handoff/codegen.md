# W2 handoff — field model → code

_2026-10-22. Worktree model withdrawn by `docs/coordination.md` Revision 2/3:
files are on disk in the main checkout; the coordinator commits. I did not
commit or push. Local branch `wave1/codegen` exists (pointing at
`origin/wave1/codegen`, no new commits) and is left for the coordinator._

## Done

- `scripts/gen_field_tables.py` — deterministic generator, source validator,
  and `--check` mode (regenerate in memory and diff committed bytes). No
  timestamps or absolute paths in output.
- `core/model/field_table.gen.h` / `.cpp` — 3,207 rows / 8 record types,
  generated from the two TSVs, one source row per generated row.
- `core/model/field_table.h` / `.cpp` — C++20 lookup by id, by name, and by
  (name, record type); indexed access; record-type names. No allocation, no
  exceptions, static lifetime.
- `core/include/core_c.h` / `core/model/core_c.cpp` — stable C99 ABI with
  documented ownership/lifetime/miss rules; C API version string.
- `CMakeLists.txt` — static library `core_model`, test executable, CTest
  entries for the test and the generator freshness check; MSVC and
  clang/GCC warning settings; C++20.
- `tests/field_table_tests.cpp` — count, unique ids, full Task id range,
  lookup agreement, C boundary, 11 spot checks, and full row-for-row
  comparison against both TSVs.
- `core/model/README.md` — schema and caveats for consumers.
- `memory/codegen.md` — detailed evidence, commands, traps.

## Verified (actually executed)

Toolchain was not on PATH, but is installed: VS 2022 BuildTools MSVC
19.44.35229, CMake 3.31.6, Ninja. Used `vcvars64.bat` + full paths. **No macOS
toolchain was available; macOS is unverified.**

```powershell
python -m py_compile scripts/gen_field_tables.py
python scripts/gen_field_tables.py --check
# OK: generated table matches the TSVs: 3207 fields, 1763 named, 1444 with storage detail

# configure (Visual Studio generator; Ninja configure hung in this sandbox)
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul ^
 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
 -S . -B logs\w2-build-default -G "Visual Studio 17 2022" -A x64'
cmake --build logs\w2-build-default --config Debug
ctest --test-dir logs\w2-build-default -C Debug --output-on-failure
```

- Debug: library + tests built with **no warnings**; CTest 2/2 passed.
- `CORE_WARNINGS_AS_ERRORS=ON` build also passes (`/W4 /WX`).
- Release: built; CTest 2/2 passed.
- Test binary: `PASS: 119614 checks`.
- C header compiled as C (`cl /TC`) and linked against the C++ library; exit 0.
- Negative validation: changing one metadata id makes the generator exit 2 on
  hex/dec mismatch; changing one metadata name makes it exit 2 on the
  metadata/definitive join. It is not a generator that silently accepts
  anything.
- Build artifacts are in `logs/` (git-ignored); do not commit them.

## Not done

- macOS/clang build not run (no Mac/compiler). CMake and code are portable
  C++20, but this is **assumed**, not verified.
- Storage class/linked field detail exists only for Task in the source
  analysis. Resource/Assignment/Links/Calendar/Project/OutlineCode/Task34
  return -1/0/"" for those fields. This is a data gap, not hidden.
- No COM dispatch-slot table (get/put) from `field_id_map.tsv`; `slot_index`
  is the record offset, not the dispatch slot.
- No PjField symbolic enum or enum-constant table; ids are numeric only.
- No `.mpp` reader, scheduling, or UI code (out of scope for W2).
- Pushing is impossible in this session (no git credentials); coordinator
  commits/pushes per Revision 2/3.

## Data quality (important)

The brief's column list for `analysis/field_metadata_all.tsv` is wrong: the
real file has 11 columns and **no** storage_class/storage_name/byte_offset/
linked_field/secondary_idx/sub_type/vtable_class. Those live in
`analysis/field_definitive_map.tsv` (also 3,207 rows). The generator joins them
1:1 and aborts on any shared-column disagreement.

`field_metadata_all.type_name` is not a usable storage type. The same numeric
code is labelled differently in the two files, e.g. Task.Name code 8 =
`Boolean` (metadata) vs `text` (definitive); code 21 = `Duration2` vs
`order_id`. The public struct exposes both as `legacy_type_name` and
`engine_type_name`; only the latter is coherent with the widths. See
`core/model/README.md` for the full table.

Counts: 3,207 rows; Task 1,444, Resource 883, Assignment 777, Links 30,
Calendar 14, Project 9, OutlineCode 16, Task34 34. Names: 1,763 non-empty,
961 unique. Storage decoded: 1,444 (Task only). All `vtable_class` values are
`PropInfoData` (3,207).

## Next three actions

1. Coordinator: review/commit the files and run the two CTest commands above.
2. Decide whether to generate a COM dispatch-slot table from
   `field_id_map.tsv` (get/put slots are currently not exposed).
3. Before the reader needs non-Task storage routing, extract storage
   class/linked field for the other 1,763 fields (the current analysis did not).

## Traps

- TSVs are CRLF; read with `newline=''` or strip `\r`.
- `cmake --build --parallel` fails with "0 Error(s)" on this box; omit
  `--parallel`.
- Ninja configure hangs at compiler ABI detection; use the Visual Studio
  generator.
- Python's default temp directory is denied by the sandbox; `--check` compares
  in memory and writes nothing.
- An empty, ACL-denied `core/model/.gen-check-koabsd4n` directory remains from
  an early failed run. It is empty, so git will not track it; delete it with an
  elevated shell if it is visible.
- Do not use `legacy_type_name` for storage decisions; see above.
