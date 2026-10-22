#include "sort.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace core::view {
namespace {

bool row_less(const SortDefinition& sort, const RecordSet& records, std::size_t left,
              std::size_t right) {
    for (const auto& key : sort.keys) {
        const Value* left_pointer = records.get(left, key.field);
        const Value* right_pointer = records.get(right, key.field);
        const Value left_value = left_pointer != nullptr ? *left_pointer : Value{};
        const Value right_value = right_pointer != nullptr ? *right_pointer : Value{};
        if (left_value.is_null() != right_value.is_null()) {
            return right_value.is_null();  // nulls last
        }
        const int order = compare_values(left_value, right_value);
        if (order == 0) {
            continue;
        }
        return key.ascending ? order < 0 : order > 0;
    }
    return false;
}

std::vector<std::size_t> identity_order(std::size_t count) {
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = i;
    }
    return order;
}

}  // namespace

std::vector<std::size_t> apply_sort(const SortDefinition& sort, const RecordSet& records) {
    std::vector<std::size_t> order = identity_order(records.row_count());
    if (sort.keys.empty() || records.row_count() < 2) {
        return order;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&sort, &records](std::size_t left, std::size_t right) {
                         return row_less(sort, records, left, right);
                     });
    return order;
}

std::vector<std::size_t> apply_sort(const SortDefinition& sort, const RecordSet& records,
                                    const SortOptions& options) {
    if (!options.keep_outline || sort.keys.empty() || records.row_count() < 2) {
        return apply_sort(sort, records);
    }
    FieldId level_field = options.outline_level_field;
    if (level_field == 0 &&
        !field_id_by_name(records.record_type(), "OutlineLevel", level_field)) {
        return apply_sort(sort, records);
    }
    if (level_field == 0) {
        return apply_sort(sort, records);
    }

    struct Node {
        std::size_t row = 0;
        int level = 1;
        std::vector<std::size_t> children;
    };

    std::vector<Node> nodes;
    nodes.reserve(records.row_count());
    std::vector<std::size_t> roots;
    std::vector<std::size_t> stack;

    for (std::size_t row = 0; row < records.row_count(); ++row) {
        int level = 1;
        const Value* pointer = records.get(row, level_field);
        if (pointer != nullptr && pointer->kind == ValueKind::Integer) {
            const std::int64_t raw = pointer->integer;
            level = raw < 1 ? 1 : raw > 1000 ? 1000 : static_cast<int>(raw);
        }
        nodes.push_back(Node{row, level, {}});
        const std::size_t index = nodes.size() - 1;
        while (!stack.empty() && nodes[stack.back()].level >= level) {
            stack.pop_back();
        }
        if (stack.empty()) {
            roots.push_back(index);
        } else {
            nodes[stack.back()].children.push_back(index);
        }
        stack.push_back(index);
    }

    const auto compare_rows = [&sort, &records, &nodes](std::size_t left, std::size_t right) {
        return row_less(sort, records, nodes[left].row, nodes[right].row);
    };
    std::stable_sort(roots.begin(), roots.end(), compare_rows);

    std::function<void(std::size_t)> sort_node = [&](std::size_t index) {
        std::vector<std::size_t>& children = nodes[index].children;
        std::stable_sort(children.begin(), children.end(), compare_rows);
        for (const std::size_t child : children) {
            sort_node(child);
        }
    };
    for (const std::size_t root : roots) {
        sort_node(root);
    }

    std::vector<std::size_t> order;
    order.reserve(records.row_count());
    std::function<void(std::size_t)> emit = [&](std::size_t index) {
        order.push_back(nodes[index].row);
        for (const std::size_t child : nodes[index].children) {
            emit(child);
        }
    };
    for (const std::size_t root : roots) {
        emit(root);
    }
    return order;
}

JsonValue sort_to_json(const SortDefinition& sort) {
    JsonValue json = JsonValue::object();
    json.set("id", JsonValue::string(sort.id));
    json.set("name", JsonValue::string(sort.name));
    json.set("record_type",
             JsonValue::string(core::model::record_type_name(sort.record_type)));
    JsonValue keys = JsonValue::array();
    for (const auto& key : sort.keys) {
        JsonValue item = field_ref_to_json(key.field, sort.record_type);
        item.set("order", JsonValue::string(key.ascending ? "ascending" : "descending"));
        keys.push(std::move(item));
    }
    json.set("keys", std::move(keys));
    return json;
}

bool sort_from_json(const JsonValue& json, SortDefinition& sort, std::string_view path,
                    std::string& error) {
    const std::string prefix = path.empty() ? std::string() : std::string(path) + ": ";
    if (!json.is_object()) {
        error = prefix + "sort must be an object";
        return false;
    }
    const JsonValue* id = json.find("id");
    const JsonValue* name = json.find("name");
    const JsonValue* keys = json.find("keys");
    if (id == nullptr || !id->is_string() || id->as_string().empty()) {
        error = prefix + "sort needs a non-empty \"id\"";
        return false;
    }
    if (name == nullptr || !name->is_string()) {
        error = prefix + "sort needs a \"name\" string";
        return false;
    }
    if (keys == nullptr || !keys->is_array() || keys->items().empty()) {
        error = prefix + "sort needs a non-empty \"keys\" array";
        return false;
    }
    SortDefinition parsed;
    parsed.id = id->as_string();
    parsed.name = name->as_string();
    if (const JsonValue* record_type = json.find("record_type")) {
        if (!record_type->is_string() ||
            !record_type_from_name(record_type->as_string(), parsed.record_type)) {
            error = prefix + "unknown \"record_type\"";
            return false;
        }
    }
    for (std::size_t i = 0; i < keys->items().size(); ++i) {
        const std::string at = prefix + "keys[" + std::to_string(i) + "]";
        const JsonValue& item = keys->items()[i];
        if (!item.is_object()) {
            error = at + ": sort key must be an object";
            return false;
        }
        SortKey key;
        if (!field_ref_from_json(item, parsed.record_type, key.field, at, error)) {
            return false;
        }
        if (const JsonValue* order = item.find("order")) {
            if (!order->is_string() ||
                (order->as_string() != "ascending" && order->as_string() != "descending")) {
                error = at + ": \"order\" must be \"ascending\" or \"descending\"";
                return false;
            }
            key.ascending = order->as_string() == "ascending";
        }
        parsed.keys.push_back(key);
    }
    sort = std::move(parsed);
    return true;
}

}  // namespace core::view
