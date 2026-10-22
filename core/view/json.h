/*
 * json.h -- the smallest JSON reader/writer that can carry view definitions.
 *
 * The view model is configuration: a table, a filter tree, a group tree, a sort
 * order.  It has to be describable in tests today and storable later, so the
 * definitions round-trip through JSON.  A third-party JSON library would add a
 * dependency to a core that must build untouched on macOS, Windows and Linux;
 * this one is ~400 lines, has no dependencies, and rejects malformed input with
 * a line number.
 *
 * Deliberate limits:
 *   * objects preserve insertion order (the writer's output is deterministic);
 *   * duplicate object keys are an error (a silent last-wins would hide typos);
 *   * nesting depth is capped at 128;
 *   * numbers are IEEE doubles, which represent every PjField id (< 2^32)
 *     exactly.  Integers print without a decimal point.
 */

#ifndef CORE_VIEW_JSON_H
#define CORE_VIEW_JSON_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core::view {

class JsonValue {
  public:
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;

    static JsonValue null();
    static JsonValue boolean(bool value);
    static JsonValue number(double value);
    static JsonValue integer(std::int64_t value);
    static JsonValue string(std::string value);
    static JsonValue array();
    static JsonValue object();

    Type type() const noexcept { return type_; }
    bool is_null() const noexcept { return type_ == Type::Null; }
    bool is_bool() const noexcept { return type_ == Type::Bool; }
    bool is_number() const noexcept { return type_ == Type::Number; }
    bool is_string() const noexcept { return type_ == Type::String; }
    bool is_array() const noexcept { return type_ == Type::Array; }
    bool is_object() const noexcept { return type_ == Type::Object; }

    bool as_bool(bool fallback = false) const noexcept;
    double as_number(double fallback = 0.0) const noexcept;
    std::int64_t as_integer(std::int64_t fallback = 0) const noexcept;
    /* Empty string when this is not a String. */
    const std::string& as_string() const noexcept;
    /* Empty vector when this is not an Array/Object. */
    const std::vector<JsonValue>& items() const noexcept;
    const std::vector<std::pair<std::string, JsonValue>>& members() const noexcept;

    /* Object lookup; nullptr when absent or when this is not an object. */
    const JsonValue* find(std::string_view key) const noexcept;
    bool has(std::string_view key) const noexcept { return find(key) != nullptr; }

    /* Object mutation: replaces an existing key, otherwise appends. */
    void set(std::string key, JsonValue value);
    /* Array mutation. */
    void push(JsonValue value);

  private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<std::pair<std::string, JsonValue>> object_;
};

struct JsonParseResult {
    bool ok = false;
    JsonValue value;
    std::string error;
    std::size_t line = 0;

    explicit operator bool() const noexcept { return ok; }
};

/* Parses one JSON value and rejects trailing non-whitespace. */
JsonParseResult parse_json(std::string_view text);

/* Deterministic output: keys in insertion order, two-space indent when pretty,
 * no trailing newline.  Invalid UTF-8 is passed through, not repaired. */
std::string write_json(const JsonValue& value, bool pretty = true);

}  // namespace core::view

#endif  // CORE_VIEW_JSON_H
