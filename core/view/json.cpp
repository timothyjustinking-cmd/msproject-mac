#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace core::view {
namespace {

constexpr int kMaxDepth = 128;

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    JsonParseResult run() {
        JsonParseResult result;
        skip_whitespace();
        if (!parse_value(result.value, 0)) {
            result.error = error_;
            result.line = line_;
            return result;
        }
        skip_whitespace();
        if (pos_ != text_.size()) {
            fail("unexpected trailing text");
            result.error = error_;
            result.line = line_;
            return result;
        }
        result.ok = true;
        result.line = line_;
        return result;
    }

  private:
    bool at_end() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return text_[pos_]; }

    void fail(const std::string& message) {
        if (error_.empty()) {
            error_ = message;
        }
    }

    void skip_whitespace() {
        while (!at_end()) {
            const char c = peek();
            if (c == '\n') {
                ++line_;
                ++pos_;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool expect(char c, const char* what) {
        if (at_end() || peek() != c) {
            fail(std::string("expected ") + what);
            return false;
        }
        ++pos_;
        return true;
    }

    bool parse_value(JsonValue& out, int depth) {
        if (depth > kMaxDepth) {
            fail("nesting too deep");
            return false;
        }
        skip_whitespace();
        if (at_end()) {
            fail("expected a value");
            return false;
        }
        switch (peek()) {
            case '{': return parse_object(out, depth + 1);
            case '[': return parse_array(out, depth + 1);
            case '"': {
                std::string text;
                if (!parse_string(text)) {
                    return false;
                }
                out = JsonValue::string(std::move(text));
                return true;
            }
            case 't':
                if (!consume_literal("true")) return false;
                out = JsonValue::boolean(true);
                return true;
            case 'f':
                if (!consume_literal("false")) return false;
                out = JsonValue::boolean(false);
                return true;
            case 'n':
                if (!consume_literal("null")) return false;
                out = JsonValue::null();
                return true;
            default:
                return parse_number(out);
        }
    }

    bool consume_literal(const char* literal) {
        const std::string_view wanted(literal);
        if (text_.substr(pos_, wanted.size()) != wanted) {
            fail(std::string("expected ") + literal);
            return false;
        }
        pos_ += wanted.size();
        return true;
    }

    bool parse_number(JsonValue& out) {
        const std::size_t start = pos_;
        if (!at_end() && peek() == '-') {
            ++pos_;
        }
        bool digits = false;
        while (!at_end() && peek() >= '0' && peek() <= '9') {
            ++pos_;
            digits = true;
        }
        if (!digits) {
            fail("expected a number");
            return false;
        }
        if (!at_end() && peek() == '.') {
            ++pos_;
            bool fraction = false;
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                ++pos_;
                fraction = true;
            }
            if (!fraction) {
                fail("expected digits after '.'");
                return false;
            }
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!at_end() && (peek() == '+' || peek() == '-')) {
                ++pos_;
            }
            bool exponent = false;
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                ++pos_;
                exponent = true;
            }
            if (!exponent) {
                fail("expected digits in exponent");
                return false;
            }
        }
        const char* begin = text_.data() + start;
        const char* end = text_.data() + pos_;
        double value = 0.0;
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            fail("invalid number");
            return false;
        }
        out = JsonValue::number(value);
        return true;
    }

    bool parse_hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) {
            fail("truncated \\u escape");
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') {
                out |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("invalid hex digit in \\u escape");
                return false;
            }
        }
        return true;
    }

    bool parse_string(std::string& out) {
        if (!expect('"', "'\"'")) {
            return false;
        }
        out.clear();
        while (!at_end()) {
            const char c = peek();
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("unescaped control character in string");
                return false;
            }
            if (c != '\\') {
                out.push_back(c);
                ++pos_;
                continue;
            }
            ++pos_;
            if (at_end()) {
                fail("unterminated escape");
                return false;
            }
            const char escape = peek();
            ++pos_;
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t code_point = 0;
                    if (!parse_hex4(code_point)) {
                        return false;
                    }
                    if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                        if (pos_ + 2 > text_.size() || text_[pos_] != '\\' ||
                            text_[pos_ + 1] != 'u') {
                            fail("unpaired UTF-16 surrogate");
                            return false;
                        }
                        pos_ += 2;
                        std::uint32_t low = 0;
                        if (!parse_hex4(low)) {
                            return false;
                        }
                        if (low < 0xDC00 || low > 0xDFFF) {
                            fail("invalid low surrogate");
                            return false;
                        }
                        code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                        fail("unpaired UTF-16 surrogate");
                        return false;
                    }
                    append_utf8(out, code_point);
                    break;
                }
                default:
                    fail("invalid escape sequence");
                    return false;
            }
        }
        fail("unterminated string");
        return false;
    }

    bool parse_array(JsonValue& out, int depth) {
        ++pos_;  // '['
        out = JsonValue::array();
        skip_whitespace();
        if (!at_end() && peek() == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            JsonValue item;
            if (!parse_value(item, depth)) {
                return false;
            }
            out.push(std::move(item));
            skip_whitespace();
            if (at_end()) {
                fail("unterminated array");
                return false;
            }
            if (peek() == ',') {
                ++pos_;
                skip_whitespace();
                if (!at_end() && peek() == ']') {
                    fail("trailing comma in array");
                    return false;
                }
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                return true;
            }
            fail("expected ',' or ']'");
            return false;
        }
    }

    bool parse_object(JsonValue& out, int depth) {
        ++pos_;  // '{'
        out = JsonValue::object();
        skip_whitespace();
        if (!at_end() && peek() == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip_whitespace();
            if (at_end() || peek() != '"') {
                fail("expected a string key");
                return false;
            }
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            if (out.has(key)) {
                fail("duplicate object key \"" + key + "\"");
                return false;
            }
            skip_whitespace();
            if (!expect(':', "':'")) {
                return false;
            }
            JsonValue value;
            if (!parse_value(value, depth)) {
                return false;
            }
            out.set(std::move(key), std::move(value));
            skip_whitespace();
            if (at_end()) {
                fail("unterminated object");
                return false;
            }
            if (peek() == ',') {
                ++pos_;
                skip_whitespace();
                if (!at_end() && peek() == '}') {
                    fail("trailing comma in object");
                    return false;
                }
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                return true;
            }
            fail("expected ',' or '}'");
            return false;
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::string error_;
};

void write_string(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void write_value(std::string& out, const JsonValue& value, bool pretty, int indent);

void write_indent(std::string& out, int indent) {
    out.append(static_cast<std::size_t>(indent) * 2, ' ');
}

void write_number(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    if (std::floor(value) == value && value >= -9.0e15 && value <= 9.0e15) {
        out += std::to_string(static_cast<std::int64_t>(value));
        return;
    }
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) {
        out += "null";
        return;
    }
    out.append(buffer, result.ptr);
}

void write_value(std::string& out, const JsonValue& value, bool pretty, int indent) {
    switch (value.type()) {
        case JsonValue::Type::Null:
            out += "null";
            break;
        case JsonValue::Type::Bool:
            out += value.as_bool() ? "true" : "false";
            break;
        case JsonValue::Type::Number:
            write_number(out, value.as_number());
            break;
        case JsonValue::Type::String:
            write_string(out, value.as_string());
            break;
        case JsonValue::Type::Array: {
            const auto& items = value.items();
            if (items.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                if (pretty) {
                    out.push_back('\n');
                    write_indent(out, indent + 1);
                }
                write_value(out, items[i], pretty, indent + 1);
            }
            if (pretty) {
                out.push_back('\n');
                write_indent(out, indent);
            }
            out.push_back(']');
            break;
        }
        case JsonValue::Type::Object: {
            const auto& members = value.members();
            if (members.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (std::size_t i = 0; i < members.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                if (pretty) {
                    out.push_back('\n');
                    write_indent(out, indent + 1);
                }
                write_string(out, members[i].first);
                out.push_back(':');
                if (pretty) {
                    out.push_back(' ');
                }
                write_value(out, members[i].second, pretty, indent + 1);
            }
            if (pretty) {
                out.push_back('\n');
                write_indent(out, indent);
            }
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

JsonValue JsonValue::null() { return JsonValue{}; }

JsonValue JsonValue::boolean(bool value) {
    JsonValue json;
    json.type_ = Type::Bool;
    json.bool_ = value;
    return json;
}

JsonValue JsonValue::number(double value) {
    JsonValue json;
    json.type_ = Type::Number;
    json.number_ = value;
    return json;
}

JsonValue JsonValue::integer(std::int64_t value) {
    return number(static_cast<double>(value));
}

JsonValue JsonValue::string(std::string value) {
    JsonValue json;
    json.type_ = Type::String;
    json.string_ = std::move(value);
    return json;
}

JsonValue JsonValue::array() {
    JsonValue json;
    json.type_ = Type::Array;
    return json;
}

JsonValue JsonValue::object() {
    JsonValue json;
    json.type_ = Type::Object;
    return json;
}

bool JsonValue::as_bool(bool fallback) const noexcept {
    return type_ == Type::Bool ? bool_ : fallback;
}

double JsonValue::as_number(double fallback) const noexcept {
    return type_ == Type::Number ? number_ : fallback;
}

std::int64_t JsonValue::as_integer(std::int64_t fallback) const noexcept {
    if (type_ != Type::Number) {
        return fallback;
    }
    if (number_ < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        number_ > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return fallback;
    }
    return static_cast<std::int64_t>(number_);
}

const std::string& JsonValue::as_string() const noexcept {
    static const std::string kEmpty;
    return type_ == Type::String ? string_ : kEmpty;
}

const std::vector<JsonValue>& JsonValue::items() const noexcept {
    static const std::vector<JsonValue> kEmpty;
    return type_ == Type::Array ? array_ : kEmpty;
}

const std::vector<std::pair<std::string, JsonValue>>& JsonValue::members() const noexcept {
    static const std::vector<std::pair<std::string, JsonValue>> kEmpty;
    return type_ == Type::Object ? object_ : kEmpty;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

void JsonValue::set(std::string key, JsonValue value) {
    if (type_ != Type::Object) {
        return;
    }
    for (auto& member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
}

void JsonValue::push(JsonValue value) {
    if (type_ == Type::Array) {
        array_.push_back(std::move(value));
    }
}

JsonParseResult parse_json(std::string_view text) {
    Parser parser(text);
    return parser.run();
}

std::string write_json(const JsonValue& value, bool pretty) {
    std::string out;
    out.reserve(256);
    write_value(out, value, pretty, 0);
    return out;
}

}  // namespace core::view
