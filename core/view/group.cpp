#include "group.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace core::view {
namespace {

/* Howard Hinnant's civil-date algorithms; day 0 is 1970-01-01. */
std::int64_t days_from_civil(int year, unsigned month, unsigned day) noexcept {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - static_cast<int>(era * 400));
    const unsigned doy =
        (153u * (month > 2u ? month - 3u : month + 9u) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

DateValue civil_from_days(std::int64_t z) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int year = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned day = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned month = mp < 10u ? mp + 3u : mp - 9u;
    DateValue date;
    date.year = year + (month <= 2 ? 1 : 0);
    date.month = static_cast<std::uint8_t>(month);
    date.day = static_cast<std::uint8_t>(day);
    return date;
}

DateValue midnight(const DateValue& date) noexcept {
    DateValue copy = date;
    copy.hour = 0;
    copy.minute = 0;
    copy.second = 0;
    return copy;
}

/* Monday of the week containing `date`. */
DateValue week_start(const DateValue& date) noexcept {
    const std::int64_t serial =
        days_from_civil(date.year, date.month, date.day);
    /* 1970-01-01 was a Thursday; Monday-based weekday 0..6. */
    const std::int64_t weekday = ((serial % 7) + 7 + 3) % 7;
    return civil_from_days(serial - weekday);
}

bool numeric_value(const Value& value, double& out) noexcept {
    switch (value.kind) {
        case ValueKind::Integer:
        case ValueKind::Duration:
            out = static_cast<double>(value.integer);
            return true;
        case ValueKind::Number:
            out = value.number;
            return true;
        case ValueKind::Bool:
            out = value.boolean ? 1.0 : 0.0;
            return true;
        case ValueKind::Text: {
            const Value converted = coerce_value(value, ValueKind::Number);
            if (converted.is_null()) {
                return false;
            }
            out = converted.number;
            return true;
        }
        default:
            return false;
    }
}

bool same_key(const Value& a, const Value& b) noexcept {
    return a.kind == b.kind && compare_values(a, b) == 0;
}

struct Derived {
    Value sort_key;
    std::string label;
};

Derived derive(const Value& value, GroupInterval interval) {
    Derived result;
    result.sort_key = value;
    if (value.is_null()) {
        result.label = "(none)";
        return result;
    }
    const bool date_interval = interval == GroupInterval::Day ||
                               interval == GroupInterval::Week ||
                               interval == GroupInterval::Month ||
                               interval == GroupInterval::Quarter ||
                               interval == GroupInterval::Year;
    if (date_interval && value.kind == ValueKind::Date && value.date.valid()) {
        const DateValue date = midnight(value.date);
        switch (interval) {
            case GroupInterval::Day: {
                result.sort_key = Value::of_date(date);
                result.label = format_date(date);
                break;
            }
            case GroupInterval::Week: {
                const DateValue monday = week_start(date);
                result.sort_key = Value::of_date(monday);
                result.label = "week of " + format_date(monday);
                break;
            }
            case GroupInterval::Month: {
                DateValue first = date;
                first.day = 1;
                result.sort_key = Value::of_date(first);
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%04d-%02d", date.year,
                              static_cast<int>(date.month));
                result.label = buffer;
                break;
            }
            case GroupInterval::Quarter: {
                DateValue first = date;
                first.day = 1;
                first.month = static_cast<std::uint8_t>(((date.month - 1) / 3) * 3 + 1);
                result.sort_key = Value::of_date(first);
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%04d-Q%d", date.year,
                              (static_cast<int>(date.month) - 1) / 3 + 1);
                result.label = buffer;
                break;
            }
            case GroupInterval::Year: {
                DateValue first = date;
                first.day = 1;
                first.month = 1;
                result.sort_key = Value::of_date(first);
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%04d", date.year);
                result.label = buffer;
                break;
            }
            default:
                break;
        }
        return result;
    }
    if (interval == GroupInterval::Percent) {
        double number = 0.0;
        if (numeric_value(value, number)) {
            int bucket = static_cast<int>(std::floor(number / 10.0));
            if (bucket < 0) {
                bucket = 0;
            }
            if (bucket > 10) {
                bucket = 10;
            }
            result.sort_key = Value::of_int(bucket);
            if (bucket == 10) {
                result.label = "100";
            } else {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%d-%d", bucket * 10, bucket * 10 + 9);
                result.label = buffer;
            }
            return result;
        }
    }
    /* No interval applies (or the value is not the interval's kind): group on
     * the value itself. */
    switch (value.kind) {
        case ValueKind::Text:
            result.label = value.text.empty() ? "(blank)" : value.text;
            break;
        case ValueKind::Duration:
            result.label = format_duration(value.integer, DurationStyle::Days);
            break;
        default:
            result.label = format_value(value);
            break;
    }
    return result;
}

void build_level(const std::vector<GroupKey>& keys, std::size_t level,
                 const RecordSet& records, const std::vector<std::size_t>& rows,
                 std::vector<GroupBucket>& buckets) {
    if (level >= keys.size()) {
        return;
    }
    const GroupKey& key = keys[level];
    for (const std::size_t row : rows) {
        const Value* pointer = records.get(row, key.field);
        const Value value = pointer != nullptr ? *pointer : Value{};
        const Derived derived = derive(value, key.interval);
        GroupBucket* bucket = nullptr;
        for (auto& candidate : buckets) {
            if (same_key(candidate.key, derived.sort_key)) {
                bucket = &candidate;
                break;
            }
        }
        if (bucket == nullptr) {
            GroupBucket created;
            created.key = derived.sort_key;
            created.label = derived.label;
            buckets.push_back(std::move(created));
            bucket = &buckets.back();
        }
        bucket->rows.push_back(row);
    }
    const bool ascending = key.ascending;
    std::stable_sort(buckets.begin(), buckets.end(),
                     [ascending](const GroupBucket& a, const GroupBucket& b) {
                         if (a.key.is_null() != b.key.is_null()) {
                             return b.key.is_null();  // null bucket always last
                         }
                         const int order = compare_values(a.key, b.key);
                         if (order == 0) {
                             return false;
                         }
                         return ascending ? order < 0 : order > 0;
                     });
    for (auto& bucket : buckets) {
        build_level(keys, level + 1, records, bucket.rows, bucket.children);
    }
}

const char* order_name(bool ascending) noexcept {
    return ascending ? "ascending" : "descending";
}

bool parse_order(const JsonValue& json, bool& ascending, std::string_view path,
                 std::string& error) {
    if (!json.is_string()) {
        error = std::string(path) + ": \"order\" must be a string";
        return false;
    }
    if (json.as_string() == "ascending") {
        ascending = true;
        return true;
    }
    if (json.as_string() == "descending") {
        ascending = false;
        return true;
    }
    error = std::string(path) + ": \"order\" must be \"ascending\" or \"descending\"";
    return false;
}

}  // namespace

const char* group_interval_name(GroupInterval interval) noexcept {
    switch (interval) {
        case GroupInterval::None: return "none";
        case GroupInterval::Day: return "day";
        case GroupInterval::Week: return "week";
        case GroupInterval::Month: return "month";
        case GroupInterval::Quarter: return "quarter";
        case GroupInterval::Year: return "year";
        case GroupInterval::Percent: return "percent";
    }
    return "none";
}

bool group_interval_from_name(std::string_view name, GroupInterval& out) noexcept {
    if (name == "none") { out = GroupInterval::None; return true; }
    if (name == "day") { out = GroupInterval::Day; return true; }
    if (name == "week") { out = GroupInterval::Week; return true; }
    if (name == "month") { out = GroupInterval::Month; return true; }
    if (name == "quarter") { out = GroupInterval::Quarter; return true; }
    if (name == "year") { out = GroupInterval::Year; return true; }
    if (name == "percent") { out = GroupInterval::Percent; return true; }
    return false;
}

std::string group_value_label(const Value& value, GroupInterval interval) {
    return derive(value, interval).label;
}

std::vector<GroupBucket> build_groups(const GroupDefinition& group, const RecordSet& records) {
    std::vector<GroupBucket> buckets;
    if (group.keys.empty() || records.row_count() == 0) {
        return buckets;
    }
    std::vector<std::size_t> rows(records.row_count());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i] = i;
    }
    build_level(group.keys, 0, records, rows, buckets);
    return buckets;
}

JsonValue group_to_json(const GroupDefinition& group) {
    JsonValue json = JsonValue::object();
    json.set("id", JsonValue::string(group.id));
    json.set("name", JsonValue::string(group.name));
    json.set("record_type",
             JsonValue::string(core::model::record_type_name(group.record_type)));
    JsonValue keys = JsonValue::array();
    for (const auto& key : group.keys) {
        JsonValue item = field_ref_to_json(key.field, group.record_type);
        item.set("order", JsonValue::string(order_name(key.ascending)));
        if (key.interval != GroupInterval::None) {
            item.set("interval", JsonValue::string(group_interval_name(key.interval)));
        }
        keys.push(std::move(item));
    }
    json.set("keys", std::move(keys));
    json.set("show_summary", JsonValue::boolean(group.show_summary));
    return json;
}

bool group_from_json(const JsonValue& json, GroupDefinition& group, std::string_view path,
                     std::string& error) {
    const std::string prefix = path.empty() ? std::string() : std::string(path) + ": ";
    if (!json.is_object()) {
        error = prefix + "group must be an object";
        return false;
    }
    const JsonValue* id = json.find("id");
    const JsonValue* name = json.find("name");
    const JsonValue* keys = json.find("keys");
    if (id == nullptr || !id->is_string() || id->as_string().empty()) {
        error = prefix + "group needs a non-empty \"id\"";
        return false;
    }
    if (name == nullptr || !name->is_string()) {
        error = prefix + "group needs a \"name\" string";
        return false;
    }
    if (keys == nullptr || !keys->is_array() || keys->items().empty()) {
        error = prefix + "group needs a non-empty \"keys\" array";
        return false;
    }
    GroupDefinition parsed;
    parsed.id = id->as_string();
    parsed.name = name->as_string();
    if (const JsonValue* record_type = json.find("record_type")) {
        if (!record_type->is_string() ||
            !record_type_from_name(record_type->as_string(), parsed.record_type)) {
            error = prefix + "unknown \"record_type\"";
            return false;
        }
    }
    if (const JsonValue* show_summary = json.find("show_summary")) {
        if (!show_summary->is_bool()) {
            error = prefix + "\"show_summary\" must be a boolean";
            return false;
        }
        parsed.show_summary = show_summary->as_bool();
    }
    for (std::size_t i = 0; i < keys->items().size(); ++i) {
        const std::string at = prefix + "keys[" + std::to_string(i) + "]";
        const JsonValue& item = keys->items()[i];
        if (!item.is_object()) {
            error = at + ": group key must be an object";
            return false;
        }
        GroupKey key;
        if (!field_ref_from_json(item, parsed.record_type, key.field, at, error)) {
            return false;
        }
        if (const JsonValue* order = item.find("order")) {
            if (!parse_order(*order, key.ascending, at, error)) {
                return false;
            }
        }
        if (const JsonValue* interval = item.find("interval")) {
            if (!interval->is_string() ||
                !group_interval_from_name(interval->as_string(), key.interval)) {
                error = at + ": unknown \"interval\"";
                return false;
            }
        }
        parsed.keys.push_back(key);
    }
    group = std::move(parsed);
    return true;
}

}  // namespace core::view
