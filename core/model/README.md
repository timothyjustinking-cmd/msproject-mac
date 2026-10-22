# `core/model` — field descriptor table

`field_table.gen.h` / `field_table.gen.cpp` are generated from the analysis
TSVs and committed. Do not edit them by hand. `field_table.h` is the C++20
lookup API; `core/include/core_c.h` is the stable C boundary (and the header
the Swift app should import).

## Generate / verify

```powershell
python scripts/gen_field_tables.py          # rewrite the two generated files
python scripts/gen_field_tables.py --check  # fail if the committed files differ
```

`--check` is also a CTest test. The generator validates the source counts,
unique ids, per-record contiguity, the Task id range, the 1:1 join, every
shared column, and every non-zero `linked_field` reference before emitting.

## Sources

- `analysis/field_metadata_all.tsv` — authoritative inventory, 3,207 rows,
  11 columns. It has no storage columns.
- `analysis/field_definitive_map.tsv` — same 3,207 rows plus storage detail
  (`storage_class`, `storage_name`, `class_slot`, `linked_field_hex`,
  `secondary_idx`, `sub_type`, `engine_type_name`).

The generator joins by `(record_type, field_id)` and aborts if the two files
disagree on record type, id, property name, type code, record offset/slot,
field size, or flags.

## Row semantics / caveats

- `engine_type_name` is the storage type decoded in the definitive map:
  `null`, `i16`, `i32`, `i64`, `text`, `obj_ref`, `bool`, `date32`,
  `comp_dur`, `order_id`, `virtual`, `guid`.
- `legacy_type_name` is `field_metadata_all.type_name` and is **not reliable**
  as a storage type. Code 8 is `Boolean` there but `text` in the definitive
  map (e.g. Task.Name); code 21 is `Duration2` vs `order_id`. It is kept only
  for provenance.
- `storage_class`, `storage_name`, `class_slot`, `linked_field_id`,
  `secondary_index`, `sub_type` are decoded only for the 1,444 Task rows.
  The other 1,763 rows use `-1` / `0` / `""`; no values were invented.
- `slot_index` is the definitive map's slot index, equal to
  `field_metadata_all.record_offset`. It is a record offset, not the COM
  dispatch slot (`field_id_map.tsv` stores get/put slots separately).
- `linked_field_id == 0` means "none" (no real field id is 0).

## Lookup

```cpp
const core::model::FieldInfo* f = core::model::find_field_by_id(0x0B400000u);
const core::model::FieldInfo* g = core::model::find_field_by_name("Name", core::model::RecordType::Task);
```

Name lookup is case-sensitive. An unscoped lookup returns the first match in
table order (Task, Resource, Assignment, Links, Calendar, Project,
OutlineCode, Task34); use the record-type overload for ambiguous names such as
`ID` or `Work`.

All returned pointers are immutable static data valid for the process
lifetime. Nothing allocates, and no exception crosses the C boundary.
