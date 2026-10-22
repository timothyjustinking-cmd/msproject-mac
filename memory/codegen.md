# W2 — field model → code: working memory

_Last updated: 2026-10-22 ~09:10 (Australia/Sydney)._

## Where the work is

Per `docs/coordination.md` Revision 2/3 the worktree model is withdrawn. I worked
directly in the main checkout and did **not** commit; the coordinator commits.
The old `wave1/codegen` branch exists but is unused and has no commits from me.

Files on disk (all under the main checkout):

| Path | What |
|---|---|
| `scripts/gen_field_tables.py` | generator + validator + `--check` mode |
| `core/model/field_table.gen.h` | generated table declarations/counts |
| `core/model/field_table.gen.cpp` | generated 3,207-row table (467,820 bytes, 3,241 lines) |
| `core/model/field_table.h` | C++20 lookup API |
| `core/model/field_table.cpp` | lookup implementation |
| `core/model/core_c.cpp` | C ABI implementation |
| `core/include/core_c.h` | stable C boundary, ownership/lifetime contract |
| `tests/field_table_tests.cpp` | 119,614-check test executable |
| `tests/test_paths.h.in` | CMake-configured TSV paths for the test |
| `CMakeLists.txt` | static library + test + generator check |
| `core/model/README.md` | consumer-facing schema/data-quality notes |

## Sources (verified by reading the actual headers, not the brief)

- `analysis/field_metadata_all.tsv`: 3,208 lines = header + **3,207 rows**;
  actual columns are 11:
  `record_type, index, field_id_hex, field_id_dec, property_name, vtable_type,
  type_code, type_name, record_offset, size, flags_hex`.
  It does **not** contain storage_class/storage_name/byte_offset/linked_field/
  secondary_idx/sub_type/vtable_class despite the stream brief's column list.
- `analysis/field_definitive_map.tsv`: 3,208 lines = header + **3,207 rows**;
  18 columns including the storage columns and `engine_type_name`.
- Both files use CRLF. Python must read with `newline=''`.

Record counts, verified from row boundaries and asserted by the generator:

| record | rows | id range |
|---|---:|---|
| Task | 1,444 | 0x0b400000..0x0b4005a3 |
| Resource | 883 | 0x0c400000..0x0c400372 |
| Assignment | 777 | 0x0f400000..0x0f400308 |
| Links | 30 | 0x0e400000..0x0e40001d |
| Calendar | 14 | 0x3c400000..0x3c40000d |
| Project | 9 | 0x07400000..0x07400008 |
| OutlineCode | 16 | 0x3e400000..0x3e40000f |
| Task34 | 34 | 0x16400000..0x16400021 |

Names: 1,763 non-empty `property_name` values, 961 unique names; no duplicate
name within a single record type. The 3,207 ids are unique and globally
non-overlapping; every Task id in the range above is present.

Storage: 1,444 rows have a decoded storage class — exactly the Task rows. The
other 1,763 rows are blank in `field_definitive_map.tsv`; the table encodes
`storage_class = -1`, `class_slot/secondary_idx/sub_type = -1`,
`linked_field_id = 0`, `storage_name = ""`. Nothing invented.

All 3,207 `vtable_type`/`vtable_class` values are `PropInfoData` (grep count
3,207); the generator asserts this and does not store a redundant string.

## Data-quality finding: two different type labels

`field_metadata_all.type_name` and `field_definitive_map.engine_type_name`
label the same numeric code differently. Examples (verified rows):

| field | code | metadata `type_name` | definitive `engine_type_name` |
|---|---:|---|---|
| Task.Name 0x0b40000e | 8 | Boolean | text |
| Task.ConstraintType 0x0b400011 | 2 | Date | i16 |
| Task.Critical 0x0b400013 | 11 | Cost | bool |
| Task.Start 0x0b400023 | 19 | Image | date32 |
| code 21 field(s) | 21 | Duration2 | order_id |

The `definitive` mapping is coherent with the storage widths
(0=null, 2=i16, 3=i32, 5=i64, 8=text, 9=obj_ref, 11=bool, 19=date32,
20=comp_dur, 21=order_id, 29=virtual, 72=guid). The metadata labels are not.
The public struct therefore exposes **both**:
`engine_type_name` (trusted storage type) and `legacy_type_name` (provenance,
explicitly marked unverified in `core_c.h`). Do not use `legacy_type_name` for
storage decisions without re-deriving it.

Storage-class distribution (from definitive map; Task only):
`unk_3` 671, `unk_13` 207, `baseline` 188, `unk_9` 120, `direct` 104,
`computed` 61, `unk_11` 42, `unk_2` 16, `virtual` 12, `special` 7, `text` 4,
`unk_14` 4, `unk_6` 3, `dyn_text` 2, `unk_12` 2, `unk_15` 1.

## API / ownership decisions

- `core_field_info` is a C99 POD; strings are ASCII, NUL-terminated, static,
  process-lifetime, never NULL (empty `""` when unknown).
- Lookup by id: unique, linear scan (3,207 is small).
- Lookup by name: exact/case-sensitive; empty/NULL returns NULL; unscoped
  lookup returns the first match in table order (Task first), scoped lookup
  disambiguates. 961 unique names, 802 rows share a name with another record.
- `slot_index` is the definitive map's `slot_index`, which equals
  `field_metadata_all.record_offset` for every row (generator asserts). It is a
  record offset, not the COM dispatch slot in `field_id_map.tsv`.
- `linked_field_id == 0` means none; no real field id is 0.
- C ABI version macro `CORE_C_API_VERSION 1`; `core_c_api_version()` returns
  `core-field-table/1`.
- No allocation, no exceptions across `extern "C"`, thread-safe.

## Verification actually run

Toolchain found despite being off PATH: VS 2022 BuildTools MSVC 19.44.35229,
CMake 3.31.6, Ninja. `cl/cmake/clang/g++` were not on PATH; full paths via
`vcvars64.bat` work. **No macOS toolchain was available.**

```powershell
# generator syntax + freshness
python -m py_compile scripts/gen_field_tables.py
python scripts/gen_field_tables.py --check
# -> OK: generated table matches the TSVs: 3207 fields, 1763 named, 1444 with storage detail

# configure/build (Visual Studio generator; Ninja configure hung in this sandbox)
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul ^
  && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" ^
  -S . -B logs\w2-build-default -G "Visual Studio 17 2022" -A x64'
cmake --build logs\w2-build-default --config Debug
ctest --test-dir logs\w2-build-default -C Debug --output-on-failure
```

Results:
- Debug build: `core_model.lib`, `core_field_table_tests.exe`; no warnings.
- `CORE_WARNINGS_AS_ERRORS=ON` build passes.
- Debug CTest: 2/2 passed.
- Release build + CTest: 2/2 passed.
- Test binary: `PASS: 119614 checks` (counts, unique ids, Task range, name/id
  agreement, C boundary, spot checks, full row-for-row TSV comparison).
- C header smoke test compiled as C (`cl /TC`) and linked against the C++ static
  library; exit 0. (Warning LNK4098 only because the smoke test used the debug
  CRT while the library was built Release; not a library issue.)
- `--check` is wired into CTest as `core_field_table_generator_check`.
- Negative validation: changing one metadata id -> exit 2 (hex/dec mismatch);
  changing one metadata name -> exit 2 (metadata/definitive join mismatch).

Build artifacts are under `logs/` (git-ignored). Do not commit them.

## Next three actions

1. Coordinator: review and commit the files above; run the two CTest commands.
2. Decide whether `field_id_map.tsv`'s COM dispatch slots (get/put) belong in a
   separate generated table; they are not in `core_field_info` today.
3. Extract storage class/linked field for Resource/Assignment/etc. (current
   analysis only decoded Task) before the reader needs storage routing for them.

## Traps

- Do not trust the brief's column list for `field_metadata_all.tsv`; read the
  header. The storage columns are in `field_definitive_map.tsv`.
- TSVs are CRLF; a naive `line.split('\t')` leaves `\r` on the last column.
- Do not use `legacy_type_name` as the storage type; see table above.
- `cmake --build ... --parallel` returns 1 with "0 Error(s)" on this box
  (MSBuild/sandbox). Build without `--parallel`.
- Ninja configure hung at compiler ABI detection; the Visual Studio generator
  worked. This is environmental, not a CMakeLists defect.
- Python's default `tempfile` directory is not writable under this sandbox; the
  generator's `--check` compares in memory and writes nothing.
- A failed early run left an empty, ACL-denied directory
  `core/model/.gen-check-koabsd4n`; it is empty so git will not track it, but
  the coordinator may want to remove it with an elevated shell.
