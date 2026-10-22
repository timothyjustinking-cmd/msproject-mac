/*
 * field_table_tests.cpp -- verification for the generated descriptor table.
 *
 * This test deliberately has no third-party framework.  It checks:
 *   1. exact count (3,207), unique ids, per-record counts;
 *   2. every Task id from 0x0b400000 through 0x0b4005a3 is present;
 *   3. lookup by id and by name agree;
 *   4. the C boundary works and obeys its miss/NULL contract;
 *   5. the committed generated table matches both source TSVs row-for-row,
 *      including the storage columns from field_definitive_map.tsv;
 *   6. spot-checked values by id, including the type-label disagreement.
 *
 * The TSV paths are configured by CMake into core_test_paths.h.  They point at
 * the repository's analysis/ directory, which is committed.
 */

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_c.h"
#include "core_test_paths.h"
#include "field_table.h"

namespace {

class TestContext {
  public:
    void check(bool condition, const std::string& what) {
        if (condition) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << '\n';
    }

    template <typename A, typename B>
    void equal(const A& actual, const B& expected, const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << actual
                  << ", expected=" << expected << ")\n";
    }

    void equal_string(const std::string& actual, const std::string& expected,
                      const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=\"" << actual
                  << "\", expected=\"" << expected << "\")\n";
    }

    int failures() const { return failures_; }
    int checks() const { return checks_; }

  private:
    int failures_ = 0;
    int checks_ = 0;
};

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> cells;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = line.find('\t', start);
        if (pos == std::string::npos) {
            cells.push_back(line.substr(start));
            break;
        }
        cells.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return cells;
}

struct TsvTable {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;

    std::size_t column(const std::string& name) const {
        for (std::size_t i = 0; i < header.size(); ++i) {
            if (header[i] == name) {
                return i;
            }
        }
        return header.size();
    }

    const std::string& cell(std::size_t row, std::size_t column_index) const {
        return rows[row][column_index];
    }
};

bool read_tsv(const std::string& path, const std::vector<std::string>& expected_header,
              TsvTable& out, TestContext& test) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        test.check(false, "cannot open TSV: " + path);
        return false;
    }

    std::string line;
    if (!std::getline(input, line)) {
        test.check(false, "empty TSV: " + path);
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
        line.erase(0, 3);
    }

    out.header = split_tabs(line);
    if (out.header != expected_header) {
        std::ostringstream message;
        message << "unexpected header in " << path << " (found " << out.header.size()
                << " columns)";
        test.check(false, message.str());
        return false;
    }

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            test.check(false, "blank line in " + path);
            return false;
        }
        auto cells = split_tabs(line);
        if (cells.size() != out.header.size()) {
            std::ostringstream message;
            message << path << ": row " << (out.rows.size() + 2) << " has "
                    << cells.size() << " columns, expected " << out.header.size();
            test.check(false, message.str());
            return false;
        }
        out.rows.push_back(std::move(cells));
    }
    return true;
}

bool parse_u32_dec(const std::string& text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, value, 10);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_u32_hex(const std::string& text, std::uint32_t& value) {
    std::string_view view(text);
    if (view.size() > 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
        view.remove_prefix(2);
    }
    if (view.empty()) {
        return false;
    }
    const char* begin = view.data();
    const char* end = begin + view.size();
    auto result = std::from_chars(begin, end, value, 16);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_i32_dec(const std::string& text, std::int32_t& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    auto result = std::from_chars(begin, end, value, 10);
    return result.ec == std::errc{} && result.ptr == end;
}

bool record_type_from_name(const std::string& name, std::uint8_t& value) {
    if (name == "Task") {
        value = CORE_RECORD_TASK;
    } else if (name == "Resource") {
        value = CORE_RECORD_RESOURCE;
    } else if (name == "Assignment") {
        value = CORE_RECORD_ASSIGNMENT;
    } else if (name == "Links") {
        value = CORE_RECORD_LINKS;
    } else if (name == "Calendar") {
        value = CORE_RECORD_CALENDAR;
    } else if (name == "Project") {
        value = CORE_RECORD_PROJECT;
    } else if (name == "OutlineCode") {
        value = CORE_RECORD_OUTLINE_CODE;
    } else if (name == "Task34") {
        value = CORE_RECORD_TASK34;
    } else {
        return false;
    }
    return true;
}

void test_counts_and_ids(TestContext& test) {
    std::cout << "test: counts and unique ids\n";
    test.equal(core::model::field_count(), std::size_t{3207}, "field_count()");
    test.equal(core::model::generated::kFieldCount, std::size_t{3207},
               "generated kFieldCount");
    test.equal(core::model::generated::kTaskFieldCount, std::size_t{1444},
               "generated Task count");

    std::unordered_set<std::uint32_t> ids;
    std::size_t task_count = 0;
    std::size_t named_count = 0;
    std::size_t storage_count = 0;
    for (std::size_t i = 0; i < core::model::field_count(); ++i) {
        const core::model::FieldInfo* field = core::model::field_at(i);
        test.check(field != nullptr, "field_at(" + std::to_string(i) + ")");
        if (field == nullptr) {
            continue;
        }
        test.check(ids.insert(field->field_id).second,
                   "duplicate field id " + std::to_string(field->field_id));
        if (field->record_type == CORE_RECORD_TASK) {
            ++task_count;
        }
        if (field->name[0] != '\0') {
            ++named_count;
        }
        if (field->storage_class != -1) {
            ++storage_count;
        }
        test.check(field->name != nullptr, "name pointer is NULL");
        test.check(field->engine_type_name != nullptr, "engine_type_name is NULL");
        test.check(field->legacy_type_name != nullptr, "legacy_type_name is NULL");
        test.check(field->storage_name != nullptr, "storage_name is NULL");
    }
    test.equal(ids.size(), std::size_t{3207}, "unique field id count");
    test.equal(task_count, std::size_t{1444}, "Task field count");
    test.equal(named_count, std::size_t{1763},
               "named fields (metadata_all property_name non-empty)");
    test.equal(storage_count, std::size_t{1444},
               "fields with decoded storage (Task only)");

    test.equal(core::model::generated::kResourceFieldCount, std::size_t{883},
               "Resource count");
    test.equal(core::model::generated::kAssignmentFieldCount, std::size_t{777},
               "Assignment count");
    test.equal(core::model::generated::kLinksFieldCount, std::size_t{30}, "Links count");
    test.equal(core::model::generated::kCalendarFieldCount, std::size_t{14},
               "Calendar count");
    test.equal(core::model::generated::kProjectFieldCount, std::size_t{9},
               "Project count");
    test.equal(core::model::generated::kOutlineCodeFieldCount, std::size_t{16},
               "OutlineCode count");
    test.equal(core::model::generated::kTask34FieldCount, std::size_t{34},
               "Task34 count");

    test.check(core::model::field_at(3207) == nullptr, "field_at(count) is NULL");
    test.check(core::model::find_field_by_id(0) == nullptr,
               "find_field_by_id(0) is NULL");
    test.check(core::model::find_field_by_id(0xFFFFFFFFu) == nullptr,
               "find_field_by_id(0xffffffff) is NULL");
}

void test_task_range(TestContext& test) {
    std::cout << "test: Task id range 0x0b400000..0x0b4005a3\n";
    constexpr std::uint32_t first = 0x0B400000u;
    constexpr std::uint32_t last = 0x0B4005A3u;
    std::size_t found = 0;
    for (std::uint32_t id = first;; ++id) {
        const core::model::FieldInfo* field = core::model::find_field_by_id(id);
        if (field == nullptr) {
            std::ostringstream message;
            message << "missing Task field id 0x" << std::hex << id;
            test.check(false, message.str());
        } else {
            ++found;
            test.equal(static_cast<unsigned>(field->record_type),
                       static_cast<unsigned>(CORE_RECORD_TASK),
                       "Task id record type at 0x" + std::to_string(id));
            test.equal(field->field_id & 0xFFFF0000u, 0x0B400000u,
                       "Task id prefix at 0x" + std::to_string(id));
        }
        if (id == last) {
            break;
        }
    }
    test.equal(found, std::size_t{1444}, "Task ids present in 0x0b400000..0x0b4005a3");
    test.check(core::model::find_field_by_id(0x0B4005A4u) == nullptr,
               "Task id after the range is absent");
    test.check(core::model::find_field_by_id(0x0B3FFFFFu) == nullptr,
               "Task id before the range is absent");
}

void test_name_lookup(TestContext& test) {
    std::cout << "test: lookup by name agrees with lookup by id\n";

    // Within a record type names are unique in the source; assert that and
    // require the scoped lookup to return exactly the same descriptor.
    std::unordered_map<std::string, std::uint32_t> scoped_names;
    std::unordered_map<std::string, const core::model::FieldInfo*> first_by_name;
    std::size_t named = 0;
    for (std::size_t i = 0; i < core::model::field_count(); ++i) {
        const core::model::FieldInfo* field = core::model::field_at(i);
        if (field->name[0] == '\0') {
            continue;
        }
        ++named;
        const std::string key =
            std::string(core::model::record_type_name(
                            static_cast<core::model::RecordType>(field->record_type))) +
            "\x1f" + field->name;
        const auto inserted = scoped_names.emplace(key, field->field_id);
        test.check(inserted.second,
                   "duplicate name within a record type: " + key);

        const core::model::FieldInfo* by_name = core::model::find_field_by_name(
            field->name, static_cast<core::model::RecordType>(field->record_type));
        test.check(by_name != nullptr, "scoped name lookup failed for " + key);
        if (by_name != nullptr) {
            test.equal(by_name->field_id, field->field_id,
                       "scoped name lookup id for " + key);
        }

        const core::model::FieldInfo* expected_global = field;
        const auto first = first_by_name.find(field->name);
        if (first == first_by_name.end()) {
            first_by_name.emplace(field->name, field);
        } else {
            expected_global = first->second;
        }
        const core::model::FieldInfo* by_name_global =
            core::model::find_field_by_name(field->name);
        test.check(by_name_global != nullptr, "global name lookup failed for " + key);
        if (by_name_global != nullptr) {
            test.equal_string(by_name_global->name, field->name,
                              "global name lookup returned a different name for " + key);
            test.check(by_name_global == expected_global,
                       "global name lookup is not first-in-table for " + key);
        }
    }
    test.equal(named, std::size_t{1763}, "named rows visited by name lookup");
    test.check(core::model::find_field_by_name(nullptr) == nullptr,
               "NULL name returns NULL");
    test.check(core::model::find_field_by_name("") == nullptr,
               "empty name returns NULL");
    test.check(core::model::find_field_by_name("DefinitelyNotAField") == nullptr,
               "unknown name returns NULL");
}

void test_c_boundary(TestContext& test) {
    std::cout << "test: C boundary\n";
    test.equal(core_field_count(), std::size_t{3207}, "core_field_count()");
    test.equal_string(core_c_api_version(), "core-field-table/1",
                      "core_c_api_version()");

    const core_field_info* work = core_field_by_id(0x0B400000u);
    test.check(work != nullptr, "C lookup of 0x0b400000");
    if (work != nullptr) {
        test.equal_string(work->name, "Work", "C Work name");
        test.equal_string(work->engine_type_name, "i64", "C Work engine type");
        test.equal_string(work->legacy_type_name, "Work", "C Work legacy type");
        test.equal_string(work->storage_name, "direct", "C Work storage name");
        test.equal(work->field_id, 0x0B400000u, "C Work id");
        test.equal(static_cast<int>(work->record_type), CORE_RECORD_TASK,
                   "C Work record type");
        test.equal(work->linked_field_id, 0x0B4000BAu, "C Work linked field");
    }

    const core_field_info* by_name = core_field_by_name("Work");
    test.check(by_name != nullptr, "C lookup Work by name");
    if (by_name != nullptr) {
        test.equal(by_name->field_id, 0x0B400000u, "C Work first match id");
    }
    const core_field_info* scoped = core_field_by_name_in("ID", CORE_RECORD_RESOURCE);
    test.check(scoped != nullptr, "C scoped Resource ID");
    if (scoped != nullptr) {
        test.equal(scoped->field_id, 0x0C400000u, "C scoped Resource ID id");
    }
    test.check(core_field_by_name_in("ID", CORE_RECORD_CALENDAR) == nullptr,
               "C scoped Calendar ID is absent");
    test.check(core_field_by_id(0xDEADBEEFu) == nullptr, "C missing id returns NULL");
    test.check(core_field_by_name(nullptr) == nullptr, "C NULL name returns NULL");
    test.check(core_field_by_name("") == nullptr, "C empty name returns NULL");
    test.check(core_field_at(3207) == nullptr, "C field_at out of range");
    test.equal_string(core_record_type_name(CORE_RECORD_TASK), "Task",
                      "C record type Task");
    test.equal_string(core_record_type_name(CORE_RECORD_TASK34), "Task34",
                      "C record type Task34");
    test.equal_string(core_record_type_name(200), "Unknown",
                      "C unknown record type");
}

struct SpotCheck {
    std::uint32_t field_id;
    std::uint8_t record_type;
    const char* name;
    std::uint16_t type_code;
    const char* engine_type_name;
    const char* legacy_type_name;
    int storage_class;
    const char* storage_name;
    std::uint16_t slot_index;
    std::uint32_t field_size;
    std::uint32_t flags;
    std::uint32_t linked_field_id;
    std::int32_t class_slot;
    std::int32_t secondary_index;
    std::int32_t sub_type;
};

void test_spot_checks(TestContext& test) {
    std::cout << "test: spot checks by id\n";
    const SpotCheck spots[] = {
        {0x0B400000u, CORE_RECORD_TASK, "Work", 5, "i64", "Work", 1, "direct",
         8, 8, 0x00020028u, 0x0B4000BAu, 0, 0, 9},
        {0x0B40000Eu, CORE_RECORD_TASK, "Name", 8, "text", "Boolean", 4, "text",
         21, 512, 0x000200C8u, 0, 0, 9, 0},
        {0x0B400014u, CORE_RECORD_TASK, "LevelingDelay", 3, "i32", "Duration", 1,
         "direct", 28, 4, 0x00020028u, 0x0B4000B2u, 10, 15, 0},
        {0x0B400017u, CORE_RECORD_TASK, "ID", 3, "i32", "Duration", 0, "special",
         37, 4, 0x00000020u, 0, 0, 17, 0},
        {0x0C400000u, CORE_RECORD_RESOURCE, "ID", 3, "i32", "Duration", -1, "",
         37, 4, 0x00000020u, 0, -1, -1, -1},
        {0x0F400001u, CORE_RECORD_ASSIGNMENT, "TaskUniqueID", 3, "i32", "Duration",
         -1, "", 1, 4, 0x00000028u, 0, -1, -1, -1},
        {0x0E400000u, CORE_RECORD_LINKS, "", 3, "i32", "Duration", -1, "", 1, 4,
         0x00000020u, 0, -1, -1, -1},
        {0x3C400000u, CORE_RECORD_CALENDAR, "", 3, "i32", "Duration", -1, "", 37,
         4, 0x00000020u, 0, -1, -1, -1},
        {0x07400000u, CORE_RECORD_PROJECT, "", 3, "i32", "Duration", -1, "", 37,
         4, 0x00000020u, 0, -1, -1, -1},
        {0x3E400000u, CORE_RECORD_OUTLINE_CODE, "", 3, "i32", "Duration", -1, "",
         37, 4, 0x00000020u, 0, -1, -1, -1},
        {0x16400000u, CORE_RECORD_TASK34, "", 3, "i32", "Duration", -1, "", 37, 4,
         0x00001020u, 0, -1, -1, -1},
    };

    for (const SpotCheck& spot : spots) {
        std::ostringstream label;
        label << "0x" << std::hex << spot.field_id;
        const core::model::FieldInfo* field = core::model::find_field_by_id(spot.field_id);
        test.check(field != nullptr, "spot id " + label.str() + " is missing");
        if (field == nullptr) {
            continue;
        }
        test.equal(static_cast<unsigned>(field->record_type),
                   static_cast<unsigned>(spot.record_type),
                   "spot record_type " + label.str());
        test.equal_string(field->name, spot.name, "spot name " + label.str());
        test.equal(field->engine_type_code, spot.type_code, "spot type code " + label.str());
        test.equal_string(field->engine_type_name, spot.engine_type_name,
                          "spot engine type " + label.str());
        test.equal_string(field->legacy_type_name, spot.legacy_type_name,
                          "spot legacy type " + label.str());
        test.equal(static_cast<int>(field->storage_class), spot.storage_class,
                   "spot storage class " + label.str());
        test.equal_string(field->storage_name, spot.storage_name,
                          "spot storage name " + label.str());
        test.equal(field->slot_index, spot.slot_index, "spot slot index " + label.str());
        test.equal(field->field_size, spot.field_size, "spot field size " + label.str());
        test.equal(field->flags, spot.flags, "spot flags " + label.str());
        test.equal(field->linked_field_id, spot.linked_field_id,
                   "spot linked field " + label.str());
        test.equal(field->class_slot, spot.class_slot, "spot class slot " + label.str());
        test.equal(field->secondary_index, spot.secondary_index,
                   "spot secondary index " + label.str());
        test.equal(field->sub_type, spot.sub_type, "spot sub type " + label.str());
    }
}

void test_against_tsvs(TestContext& test) {
    std::cout << "test: generated table matches both TSVs row-for-row\n";

    const std::vector<std::string> metadata_header = {
        "record_type", "index", "field_id_hex", "field_id_dec", "property_name",
        "vtable_type", "type_code", "type_name", "record_offset", "size", "flags_hex"};
    const std::vector<std::string> definitive_header = {
        "record_type", "field_id_hex", "field_id_dec", "com_property_name",
        "engine_type_code", "engine_type_name", "slot_index", "field_size",
        "flags_hex", "storage_class", "storage_name", "class_slot",
        "byte_offset_hex", "byte_offset_dec", "linked_field_hex", "secondary_idx",
        "sub_type", "vtable_class"};

    TsvTable metadata;
    TsvTable definitive;
    if (!read_tsv(CORE_FIELD_METADATA_TSV, metadata_header, metadata, test)) {
        return;
    }
    if (!read_tsv(CORE_FIELD_DEFINITIVE_TSV, definitive_header, definitive, test)) {
        return;
    }
    test.equal(metadata.rows.size(), std::size_t{3207}, "metadata TSV row count");
    test.equal(definitive.rows.size(), std::size_t{3207}, "definitive TSV row count");

    const std::size_t m_record = metadata.column("record_type");
    const std::size_t m_index = metadata.column("index");
    const std::size_t m_id_hex = metadata.column("field_id_hex");
    const std::size_t m_id_dec = metadata.column("field_id_dec");
    const std::size_t m_name = metadata.column("property_name");
    const std::size_t m_vtable = metadata.column("vtable_type");
    const std::size_t m_code = metadata.column("type_code");
    const std::size_t m_type_name = metadata.column("type_name");
    const std::size_t m_offset = metadata.column("record_offset");
    const std::size_t m_size = metadata.column("size");
    const std::size_t m_flags = metadata.column("flags_hex");

    const std::size_t d_record = definitive.column("record_type");
    const std::size_t d_id_hex = definitive.column("field_id_hex");
    const std::size_t d_id_dec = definitive.column("field_id_dec");
    const std::size_t d_name = definitive.column("com_property_name");
    const std::size_t d_code = definitive.column("engine_type_code");
    const std::size_t d_engine_name = definitive.column("engine_type_name");
    const std::size_t d_slot = definitive.column("slot_index");
    const std::size_t d_size = definitive.column("field_size");
    const std::size_t d_flags = definitive.column("flags_hex");
    const std::size_t d_storage_class = definitive.column("storage_class");
    const std::size_t d_storage_name = definitive.column("storage_name");
    const std::size_t d_class_slot = definitive.column("class_slot");
    const std::size_t d_byte_hex = definitive.column("byte_offset_hex");
    const std::size_t d_byte_dec = definitive.column("byte_offset_dec");
    const std::size_t d_linked = definitive.column("linked_field_hex");
    const std::size_t d_secondary = definitive.column("secondary_idx");
    const std::size_t d_sub_type = definitive.column("sub_type");
    const std::size_t d_vtable = definitive.column("vtable_class");

    struct DefinitiveRow {
        std::size_t line = 0;
        const std::vector<std::string>* cells = nullptr;
    };
    std::unordered_map<std::uint32_t, DefinitiveRow> by_id;
    for (std::size_t i = 0; i < definitive.rows.size(); ++i) {
        std::uint32_t id = 0;
        if (!parse_u32_hex(definitive.cell(i, d_id_hex), id)) {
            test.check(false, "definitive TSV bad field id at row " + std::to_string(i + 2));
            continue;
        }
        by_id.emplace(id, DefinitiveRow{i + 2, &definitive.rows[i]});
    }
    test.equal(by_id.size(), std::size_t{3207}, "definitive TSV unique id count");

    std::unordered_set<std::uint32_t> visited;
    for (std::size_t i = 0; i < metadata.rows.size(); ++i) {
        const std::size_t line = i + 2;
        std::uint32_t id = 0;
        std::uint32_t id_dec = 0;
        if (!parse_u32_hex(metadata.cell(i, m_id_hex), id) ||
            !parse_u32_dec(metadata.cell(i, m_id_dec), id_dec)) {
            test.check(false, "metadata TSV bad field id at row " + std::to_string(line));
            continue;
        }
        test.equal(id, id_dec, "metadata hex/dec id row " + std::to_string(line));

        const core::model::FieldInfo* field = core::model::find_field_by_id(id);
        test.check(field != nullptr,
                   "metadata id 0x" + std::to_string(id) + " missing from table");
        if (field == nullptr) {
            continue;
        }
        visited.insert(id);

        std::uint8_t record_type = 0;
        if (!record_type_from_name(metadata.cell(i, m_record), record_type)) {
            test.check(false, "unknown record_type at metadata row " + std::to_string(line));
            continue;
        }
        test.equal(static_cast<unsigned>(field->record_type),
                   static_cast<unsigned>(record_type),
                   "record_type row " + std::to_string(line));
        test.equal_string(field->name, metadata.cell(i, m_name),
                          "name row " + std::to_string(line));
        test.equal_string(field->legacy_type_name, metadata.cell(i, m_type_name),
                          "legacy type name row " + std::to_string(line));
        test.equal_string(metadata.cell(i, m_vtable), "PropInfoData",
                          "metadata vtable_type row " + std::to_string(line));

        std::uint32_t index = 0;
        std::uint32_t code = 0;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
        std::uint32_t flags = 0;
        if (!parse_u32_dec(metadata.cell(i, m_index), index) ||
            !parse_u32_dec(metadata.cell(i, m_code), code) ||
            !parse_u32_dec(metadata.cell(i, m_offset), offset) ||
            !parse_u32_dec(metadata.cell(i, m_size), size) ||
            !parse_u32_hex(metadata.cell(i, m_flags), flags)) {
            test.check(false, "metadata numeric field at row " + std::to_string(line));
            continue;
        }
        test.equal(field->cache_index, index, "cache_index row " + std::to_string(line));
        test.equal(field->engine_type_code, code, "type_code row " + std::to_string(line));
        test.equal(field->slot_index, offset, "record_offset row " + std::to_string(line));
        test.equal(field->field_size, size, "size row " + std::to_string(line));
        test.equal(field->flags, flags, "flags row " + std::to_string(line));

        const auto found = by_id.find(id);
        test.check(found != by_id.end(),
                   "definitive TSV missing 0x" + std::to_string(id));
        if (found == by_id.end()) {
            continue;
        }
        const std::vector<std::string>& d = *found->second.cells;
        test.equal_string(d[d_record], metadata.cell(i, m_record),
                          "definitive record_type row " + std::to_string(found->second.line));
        test.equal_string(d[d_name], metadata.cell(i, m_name),
                          "definitive com_property_name row " + std::to_string(found->second.line));
        test.equal_string(d[d_vtable], "PropInfoData",
                          "definitive vtable_class row " + std::to_string(found->second.line));

        std::uint32_t d_code_value = 0;
        std::uint32_t d_slot_value = 0;
        std::uint32_t d_size_value = 0;
        std::uint32_t d_flags_value = 0;
        std::uint32_t d_id_dec_value = 0;
        if (!parse_u32_dec(d[d_code], d_code_value) ||
            !parse_u32_dec(d[d_slot], d_slot_value) ||
            !parse_u32_dec(d[d_size], d_size_value) ||
            !parse_u32_hex(d[d_flags], d_flags_value) ||
            !parse_u32_dec(d[d_id_dec], d_id_dec_value)) {
            test.check(false, "definitive numeric field row " +
                                  std::to_string(found->second.line));
            continue;
        }
        test.equal(d_code_value, code, "definitive engine_type_code row " +
                                          std::to_string(found->second.line));
        test.equal(d_slot_value, offset, "definitive slot_index row " +
                                            std::to_string(found->second.line));
        test.equal(d_size_value, size, "definitive field_size row " +
                                          std::to_string(found->second.line));
        test.equal(d_flags_value, flags, "definitive flags row " +
                                            std::to_string(found->second.line));
        test.equal(d_id_dec_value, id, "definitive field_id_dec row " +
                                          std::to_string(found->second.line));
        test.equal_string(field->engine_type_name, d[d_engine_name],
                          "engine_type_name row " + std::to_string(found->second.line));
        test.equal_string(field->storage_name, d[d_storage_name],
                          "storage_name row " + std::to_string(found->second.line));

        // Storage class: blank in the TSV means "not decoded" (-1).
        int storage_class = -1;
        if (!d[d_storage_class].empty() &&
            !parse_i32_dec(d[d_storage_class], storage_class)) {
            test.check(false, "definitive storage_class row " +
                                  std::to_string(found->second.line));
            continue;
        }
        test.equal(static_cast<int>(field->storage_class), storage_class,
                   "storage_class row " + std::to_string(found->second.line));

        int class_slot = -1;
        if (!d[d_class_slot].empty() && !parse_i32_dec(d[d_class_slot], class_slot)) {
            test.check(false, "definitive class_slot row " +
                                  std::to_string(found->second.line));
            continue;
        }
        test.equal(field->class_slot, class_slot,
                   "class_slot row " + std::to_string(found->second.line));

        int secondary = -1;
        if (!d[d_secondary].empty() && !parse_i32_dec(d[d_secondary], secondary)) {
            test.check(false, "definitive secondary_idx row " +
                                  std::to_string(found->second.line));
            continue;
        }
        test.equal(field->secondary_index, secondary,
                   "secondary_idx row " + std::to_string(found->second.line));

        int sub_type = -1;
        if (!d[d_sub_type].empty() && !parse_i32_dec(d[d_sub_type], sub_type)) {
            test.check(false, "definitive sub_type row " +
                                  std::to_string(found->second.line));
            continue;
        }
        test.equal(field->sub_type, sub_type,
                   "sub_type row " + std::to_string(found->second.line));

        std::uint32_t linked = 0;
        if (!d[d_linked].empty() && !parse_u32_hex(d[d_linked], linked)) {
            test.check(false, "definitive linked_field_hex row " +
                                  std::to_string(found->second.line));
            continue;
        }
        test.equal(field->linked_field_id, linked,
                   "linked_field_hex row " + std::to_string(found->second.line));

        if (d[d_byte_hex].empty() != d[d_byte_dec].empty()) {
            test.check(false, "half-populated byte offset row " +
                                  std::to_string(found->second.line));
        }
        if (!d[d_byte_hex].empty()) {
            std::uint32_t byte_hex = 0;
            std::uint32_t byte_dec = 0;
            if (parse_u32_hex(d[d_byte_hex], byte_hex) &&
                parse_u32_dec(d[d_byte_dec], byte_dec)) {
                test.equal(byte_hex, byte_dec,
                           "byte offset hex/dec row " +
                               std::to_string(found->second.line));
            } else {
                test.check(false, "byte offset parse row " +
                                      std::to_string(found->second.line));
            }
        }
    }
    test.equal(visited.size(), std::size_t{3207}, "every table row was matched to TSV");
}

}  // namespace

int main() {
    TestContext test;

    test_counts_and_ids(test);
    test_task_range(test);
    test_name_lookup(test);
    test_c_boundary(test);
    test_spot_checks(test);
    test_against_tsvs(test);

    if (test.failures() == 0) {
        std::cout << "PASS: " << test.checks() << " checks\n";
        return 0;
    }
    std::cerr << "FAILED: " << test.failures() << " failure(s) in "
              << test.checks() << " checks\n";
    return 1;
}
