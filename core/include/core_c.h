/*
 * core_c.h -- stable C boundary for the portable MS Project engine.
 *
 * This header is the surface the future Swift application (or any other
 * non-C++ caller) uses.  It is intentionally C99-compatible and must not
 * include any C++ headers.
 *
 * OWNERSHIP AND LIFETIME
 * ----------------------
 * Every core_field_info and every string reachable from it points into
 * immutable static storage inside the engine library.  Nothing is allocated
 * per call.  The pointers remain valid for the lifetime of the process; there
 * is no teardown, no free, no reference counting, and callers must not modify
 * or free them.  A returned pointer may be NULL (lookup miss); it is never a
 * pointer to a temporary.
 *
 * The API is thread-safe: the table is const and the functions do not mutate
 * global state.
 *
 * The functions in this header never throw.  The C++ implementation is built
 * with exceptions enabled for its own internals, but no exception can cross
 * these extern "C" functions; input validation returns NULL instead.
 *
 * LOOKUP SEMANTICS
 * ----------------
 * - Ids are unique across all 3,207 fields.
 * - Name lookup is case-sensitive and exact.
 * - An empty name ("") is not a lookup key.  Passing NULL or "" returns NULL.
 * - When several record types share a name (for example "ID" in Task and
 *   Resource), core_field_by_name() returns the first match in table order:
 *   Task, Resource, Assignment, Links, Calendar, Project, OutlineCode,
 *   Task34.  Use core_field_by_name_in() to scope the search.
 *
 * SOURCE AND DATA QUALITY
 * -----------------------
 * Generated from analysis/field_metadata_all.tsv (3,207 authoritative rows)
 * joined 1:1 with analysis/field_definitive_map.tsv (same 3,207 rows, storage
 * detail).  See scripts/gen_field_tables.py.
 *
 * - storage_class is -1 for the 1,763 non-Task rows: the analysis only decoded
 *   storage routing for Task.  No value has been invented for the others.
 * - linked_field_id is 0 when the source has no linked field.  There is no
 *   real field id 0, so 0 is an unambiguous "none".
 * - class_slot, secondary_index and sub_type are -1 when the source is blank.
 * - slot_index is the definitive map's slot_index.  For every row it equals
 *   field_metadata_all.tsv's record_offset; it is a record offset, not the COM
 *   dispatch slot from field_id_map.tsv.
 * - engine_type_name is the storage/value type decoded in definitive_map
 *   (i16/i32/i64/text/date32/bool/virtual/guid/obj_ref/null).
 * - legacy_type_name is field_metadata_all.tsv's type_name, kept for
 *   provenance.  It disagrees with engine_type_name for the same type code
 *   (for example Task.Name: code 8, legacy "Boolean", engine "text").  Treat
 *   legacy_type_name as an unverified label and use engine_type_name for
 *   storage decisions.
 */

#ifndef CORE_C_H
#define CORE_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump only for an incompatible change to core_field_info or the function
 * signatures.  Additive fields require a new version and a new accessor. */
#define CORE_C_API_VERSION 1

/* Stable numeric record-type ids.  These values are part of the C ABI. */
typedef enum core_record_type {
    CORE_RECORD_UNKNOWN = 0,
    CORE_RECORD_TASK = 1,
    CORE_RECORD_RESOURCE = 2,
    CORE_RECORD_ASSIGNMENT = 3,
    CORE_RECORD_LINKS = 4,
    CORE_RECORD_CALENDAR = 5,
    CORE_RECORD_PROJECT = 6,
    CORE_RECORD_OUTLINE_CODE = 7,
    CORE_RECORD_TASK34 = 8
} core_record_type;

/*
 * A flattened descriptor row.  This is a value snapshot; copying it is safe.
 * The four char pointers are borrowed from static storage, never owned by the
 * caller, and never NULL.
 */
typedef struct core_field_info {
    uint32_t field_id;          /* PjField id, e.g. 0x0b400000 */
    uint8_t record_type;        /* core_record_type */
    uint16_t cache_index;       /* descriptor index within the record type */
    uint16_t engine_type_code;  /* raw descriptor type code, e.g. 5 */
    int8_t storage_class;       /* -1 = not decoded in the analysis */
    uint16_t slot_index;        /* record offset; see note above */
    uint32_t field_size;        /* raw descriptor size, e.g. 8, 512, 131068 */
    uint32_t flags;             /* raw flags_hex, e.g. 0x00020028 */
    uint32_t linked_field_id;   /* 0 = none, otherwise a field id */
    int32_t class_slot;         /* -1 = not decoded */
    int32_t secondary_index;    /* -1 = none/not decoded */
    int32_t sub_type;           /* -1 = none/not decoded */
    const char* name;           /* COM property name; "" when unknown */
    const char* engine_type_name;   /* storage type, e.g. "text", "i32" */
    const char* legacy_type_name;   /* unverified metadata_all label */
    const char* storage_name;       /* e.g. "direct"; "" when unknown */
} core_field_info;

/* Number of descriptors in the table (currently 3,207). */
size_t core_field_count(void);

/* Indexed access in table order.  Returns NULL when index >= count. */
const core_field_info* core_field_at(size_t index);

/* Lookup by PjField id.  Returns NULL when the id is not in the table. */
const core_field_info* core_field_by_id(uint32_t field_id);

/* Unscoped name lookup.  NULL/"" returns NULL; see ambiguity note above. */
const core_field_info* core_field_by_name(const char* name);

/* Name lookup restricted to one core_record_type.  NULL/"" returns NULL. */
const core_field_info* core_field_by_name_in(const char* name, uint8_t record_type);

/* "Task", "Resource", ..., or "Unknown" for an unrecognised value. */
const char* core_record_type_name(uint8_t record_type);

/* "core-field-table/1" -- identifies the C ABI, not the MS Project version. */
const char* core_c_api_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* CORE_C_H */
