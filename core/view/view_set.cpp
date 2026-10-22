#include "view_set.h"

namespace core::view {
namespace {

JsonValue to_json(const TableDefinition& item) { return table_to_json(item); }
JsonValue to_json(const FilterDefinition& item) { return filter_to_json(item); }
JsonValue to_json(const GroupDefinition& item) { return group_to_json(item); }
JsonValue to_json(const SortDefinition& item) { return sort_to_json(item); }

bool from_json(const JsonValue& json, TableDefinition& out, std::string_view path,
               std::string& error) {
    return table_from_json(json, out, path, error);
}
bool from_json(const JsonValue& json, FilterDefinition& out, std::string_view path,
               std::string& error) {
    return filter_from_json(json, out, path, error);
}
bool from_json(const JsonValue& json, GroupDefinition& out, std::string_view path,
               std::string& error) {
    return group_from_json(json, out, path, error);
}
bool from_json(const JsonValue& json, SortDefinition& out, std::string_view path,
               std::string& error) {
    return sort_from_json(json, out, path, error);
}

template <typename T>
const T* find_by_id(const std::vector<T>& items, std::string_view id) noexcept {
    for (const auto& item : items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

template <typename T>
bool parse_array(const JsonValue& root, const char* key, std::vector<T>& out,
                 std::string& error) {
    const JsonValue* array = root.find(key);
    if (array == nullptr) {
        return true;
    }
    if (!array->is_array()) {
        error = std::string(key) + ": must be an array";
        return false;
    }
    out.clear();
    out.reserve(array->items().size());
    for (std::size_t i = 0; i < array->items().size(); ++i) {
        T item;
        const std::string path = std::string(key) + "[" + std::to_string(i) + "]";
        if (!from_json(array->items()[i], item, path, error)) {
            return false;
        }
        out.push_back(std::move(item));
    }
    return true;
}

template <typename T>
JsonValue array_of(const std::vector<T>& items) {
    JsonValue array = JsonValue::array();
    for (const auto& item : items) {
        array.push(to_json(item));
    }
    return array;
}

}  // namespace

const TableDefinition* ViewSet::find_table(std::string_view id) const noexcept {
    return find_by_id(tables, id);
}

const FilterDefinition* ViewSet::find_filter(std::string_view id) const noexcept {
    return find_by_id(filters, id);
}

const GroupDefinition* ViewSet::find_group(std::string_view id) const noexcept {
    return find_by_id(groups, id);
}

const SortDefinition* ViewSet::find_sort(std::string_view id) const noexcept {
    return find_by_id(sorts, id);
}

JsonValue view_set_to_json(const ViewSet& set) {
    JsonValue json = JsonValue::object();
    json.set("version", JsonValue::integer(1));
    json.set("tables", array_of(set.tables));
    if (!set.filters.empty()) {
        json.set("filters", array_of(set.filters));
    }
    if (!set.groups.empty()) {
        json.set("groups", array_of(set.groups));
    }
    if (!set.sorts.empty()) {
        json.set("sorts", array_of(set.sorts));
    }
    return json;
}

bool view_set_from_json(const JsonValue& json, ViewSet& set, std::string& error) {
    if (!json.is_object()) {
        error = "view set must be a JSON object";
        return false;
    }
    if (const JsonValue* version = json.find("version")) {
        if (!version->is_number() || version->as_integer(-1) != 1) {
            error = "unsupported view set version (expected 1)";
            return false;
        }
    }
    ViewSet parsed;
    if (!parse_array(json, "tables", parsed.tables, error) ||
        !parse_array(json, "filters", parsed.filters, error) ||
        !parse_array(json, "groups", parsed.groups, error) ||
        !parse_array(json, "sorts", parsed.sorts, error)) {
        return false;
    }
    set = std::move(parsed);
    return true;
}

bool view_set_parse(std::string_view text, ViewSet& set, std::string& error) {
    const JsonParseResult parsed = parse_json(text);
    if (!parsed.ok) {
        error = "JSON line " + std::to_string(parsed.line) + ": " + parsed.error;
        return false;
    }
    return view_set_from_json(parsed.value, set, error);
}

std::string view_set_write(const ViewSet& set, bool pretty) {
    return write_json(view_set_to_json(set), pretty);
}

}  // namespace core::view
