/*
 * field_table.h -- C++20 accessors over the generated descriptor table.
 *
 * The table itself lives in field_table.gen.cpp (generated; do not edit).
 * This header only adds type-safe lookups.  The same ownership and lifetime
 * rules as core_c.h apply: returned pointers refer to immutable static data
 * valid for the process lifetime, and no function allocates or throws.
 */

#ifndef CORE_MODEL_FIELD_TABLE_H
#define CORE_MODEL_FIELD_TABLE_H

#include <cstddef>
#include <cstdint>

#include "core_c.h"
#include "field_table.gen.h"

namespace core::model {

using FieldInfo = core_field_info;

enum class RecordType : std::uint8_t {
    Unknown = CORE_RECORD_UNKNOWN,
    Task = CORE_RECORD_TASK,
    Resource = CORE_RECORD_RESOURCE,
    Assignment = CORE_RECORD_ASSIGNMENT,
    Links = CORE_RECORD_LINKS,
    Calendar = CORE_RECORD_CALENDAR,
    Project = CORE_RECORD_PROJECT,
    OutlineCode = CORE_RECORD_OUTLINE_CODE,
    Task34 = CORE_RECORD_TASK34,
};

inline constexpr std::size_t kFieldCount = generated::kFieldCount;

/* Table order: Task, Resource, Assignment, Links, Calendar, Project,
 * OutlineCode, Task34.  This is also the precedence for unscoped lookup. */
std::size_t field_count() noexcept;
const FieldInfo* field_at(std::size_t index) noexcept;

const FieldInfo* find_field_by_id(std::uint32_t field_id) noexcept;

/* Case-sensitive, exact.  nullptr or "" returns nullptr.  When a name occurs
 * in several record types, the first match in table order is returned; use the
 * overload below to disambiguate. */
const FieldInfo* find_field_by_name(const char* name) noexcept;
const FieldInfo* find_field_by_name(const char* name, RecordType record_type) noexcept;

/* "Task", "Resource", ..., "Unknown". */
const char* record_type_name(RecordType record_type) noexcept;

}  // namespace core::model

#endif  // CORE_MODEL_FIELD_TABLE_H
