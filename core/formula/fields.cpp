#include "fields.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core::formula {
namespace {

bool equals_ci(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        };
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

/*
 * The formula UI's spelling of a field, reduced to a key: lower case, spaces,
 * '-' and '_' dropped, and '%' expanded to "percent".  So "% Complete",
 * "PercentComplete" and "percent complete" all normalise to "percentcomplete".
 */
std::string normalize_field_name(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 8);
    for (char c : name) {
        if (c == '%') {
            out += "percent";
        } else if (c == ' ' || c == '-' || c == '_' || c == '\t') {
            continue;
        } else if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool kind_from_prefix(std::string_view name, FieldKind& kind, int& index) noexcept {
    struct Prefix {
        const char* text;
        FieldKind kind;
    };
    static constexpr Prefix kPrefixes[] = {
        {"text", FieldKind::Text},         {"number", FieldKind::Number},
        {"date", FieldKind::Date},         {"flag", FieldKind::Flag},
        {"duration", FieldKind::Duration}, {"cost", FieldKind::Cost},
    };
    for (const Prefix& prefix : kPrefixes) {
        const std::string_view text(prefix.text);
        if (name.size() <= text.size()) {
            continue;
        }
        if (!equals_ci(name.substr(0, text.size()), text)) {
            continue;
        }
        const std::string_view digits = name.substr(text.size());
        int value = 0;
        bool all_digits = true;
        for (char c : digits) {
            if (!is_digit(c)) {
                all_digits = false;
                break;
            }
            value = value * 10 + (c - '0');
            if (value > 1000) {
                all_digits = false;
                break;
            }
        }
        if (all_digits && value >= 1) {
            kind = prefix.kind;
            index = value;
            return true;
        }
    }
    return false;
}

std::vector<CustomField> build_registry() {
    std::vector<CustomField> fields;
    for (std::size_t i = 0; i < core::model::field_count(); ++i) {
        const core::model::FieldInfo* info = core::model::field_at(i);
        if (info == nullptr) {
            continue;
        }
        if (info->record_type != static_cast<std::uint8_t>(core::model::RecordType::Task)) {
            continue;
        }
        const std::string_view name(info->name);
        FieldKind kind = FieldKind::Text;
        int index = 0;
        if (!kind_from_prefix(name, kind, index)) {
            continue;
        }
        CustomField field;
        field.field = info->field_id;
        field.kind = kind;
        field.index = index;
        field.name = std::string(name);
        fields.push_back(std::move(field));
    }
    std::sort(fields.begin(), fields.end(), [](const CustomField& a, const CustomField& b) {
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        return a.field < b.field;
    });
    return fields;
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

std::uint16_t read_u16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(data[i]) << (8 * i);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* data) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0x7fu) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffu) {
        out.push_back(static_cast<char>(0xc0u | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else if (code_point <= 0xffffu) {
        out.push_back(static_cast<char>(0xe0u | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else {
        out.push_back(static_cast<char>(0xf0u | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3fu)));
        out.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    }
}

void append_utf16(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0xffffu) {
        out.push_back(static_cast<char>(code_point & 0xffu));
        out.push_back(static_cast<char>((code_point >> 8) & 0xffu));
        return;
    }
    const std::uint32_t value = code_point - 0x10000u;
    const auto high = static_cast<std::uint16_t>(0xd800u + (value >> 10));
    const auto low = static_cast<std::uint16_t>(0xdc00u + (value & 0x3ffu));
    out.push_back(static_cast<char>(high & 0xffu));
    out.push_back(static_cast<char>((high >> 8) & 0xffu));
    out.push_back(static_cast<char>(low & 0xffu));
    out.push_back(static_cast<char>((low >> 8) & 0xffu));
}

bool number_value(const core::view::Value& value, double& out) noexcept {
    switch (value.kind) {
        case core::view::ValueKind::Number:
            out = value.number;
            return true;
        case core::view::ValueKind::Integer:
            out = static_cast<double>(value.integer);
            return true;
        case core::view::ValueKind::Bool:
            out = value.boolean ? 1.0 : 0.0;
            return true;
        default:
            return false;
    }
}

bool flag_value(const core::view::Value& value, bool& out) noexcept {
    switch (value.kind) {
        case core::view::ValueKind::Bool:
            out = value.boolean;
            return true;
        case core::view::ValueKind::Integer:
            if (value.integer == 0 || value.integer == 1) {
                out = value.integer == 1;
                return true;
            }
            return false;
        case core::view::ValueKind::Number:
            if (value.number == 0.0 || value.number == 1.0) {
                out = value.number == 1.0;
                return true;
            }
            return false;
        default:
            return false;
    }
}

}  // namespace

const char* field_kind_name(FieldKind kind) noexcept {
    switch (kind) {
        case FieldKind::Text: return "text";
        case FieldKind::Number: return "number";
        case FieldKind::Date: return "date";
        case FieldKind::Flag: return "flag";
        case FieldKind::Duration: return "duration";
        case FieldKind::Cost: return "cost";
    }
    return "?";
}

const char* field_kind_prefix(FieldKind kind) noexcept {
    switch (kind) {
        case FieldKind::Text: return "Text";
        case FieldKind::Number: return "Number";
        case FieldKind::Date: return "Date";
        case FieldKind::Flag: return "Flag";
        case FieldKind::Duration: return "Duration";
        case FieldKind::Cost: return "Cost";
    }
    return "?";
}

bool field_kind_from_name(std::string_view name, FieldKind& out) noexcept {
    for (int i = 0; i <= static_cast<int>(FieldKind::Cost); ++i) {
        const auto kind = static_cast<FieldKind>(i);
        if (equals_ci(name, field_kind_name(kind))) {
            out = kind;
            return true;
        }
    }
    return false;
}

const char* storage_confidence_name(StorageConfidence confidence) noexcept {
    switch (confidence) {
        case StorageConfidence::Measured: return "measured";
        case StorageConfidence::Inferred: return "inferred";
        case StorageConfidence::Unmeasured: return "unmeasured";
    }
    return "?";
}

const std::vector<CustomField>& custom_fields() {
    static const std::vector<CustomField> fields = build_registry();
    return fields;
}

const CustomField* find_custom_field(FieldId field) noexcept {
    for (const CustomField& candidate : custom_fields()) {
        if (candidate.field == field) {
            return &candidate;
        }
    }
    return nullptr;
}

const CustomField* find_custom_field(FieldKind kind, int index) noexcept {
    for (const CustomField& candidate : custom_fields()) {
        if (candidate.kind == kind && candidate.index == index) {
            return &candidate;
        }
    }
    return nullptr;
}

bool field_id_from_mspj(std::string_view token, FieldId& out) noexcept {
    if (token.size() <= 4 || !equals_ci(token.substr(0, 4), "mspj")) {
        return false;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 4; i < token.size(); ++i) {
        if (!is_digit(token[i])) {
            return false;
        }
        value = value * 10 + static_cast<std::uint64_t>(token[i] - '0');
        if (value > 0xffffffffu) {
            return false;
        }
    }
    if (value == 0) {
        return false;
    }
    const FieldId candidate = static_cast<FieldId>(value);
    if (core::model::find_field_by_id(candidate) == nullptr) {
        return false;
    }
    out = candidate;
    return true;
}

FieldId resolve_field_token(std::string_view token, core::model::RecordType record_type) noexcept {
    if (token.empty()) {
        return 0;
    }
    if (token.size() > 4 && equals_ci(token.substr(0, 4), "mspj")) {
        FieldId by_token = 0;
        return field_id_from_mspj(token, by_token) ? by_token : 0;
    }
    const std::string key(token);
    if (const core::model::FieldInfo* info =
            core::model::find_field_by_name(key.c_str(), record_type)) {
        return info->field_id;
    }
    const std::string target = normalize_field_name(token);
    if (target.empty()) {
        return 0;
    }
    for (std::size_t i = 0; i < core::model::field_count(); ++i) {
        const core::model::FieldInfo* info = core::model::field_at(i);
        if (info == nullptr ||
            info->record_type != static_cast<std::uint8_t>(record_type)) {
            continue;
        }
        if (normalize_field_name(info->name) == target) {
            return info->field_id;
        }
    }
    return 0;
}

std::uint32_t encode_packed_date(const core::view::DateValue& date, bool& ok) noexcept {
    ok = false;
    if (!date.valid()) {
        return 0;
    }
    const std::int64_t day =
        static_cast<std::int64_t>(days_from_civil(date.year, date.month, date.day)) -
        kPackedDateEpochDay;
    if (day < 0 || day > 0xfffe) {
        return 0;
    }
    const std::int64_t seconds = static_cast<std::int64_t>(date.hour) * 3600 +
                                 static_cast<std::int64_t>(date.minute) * 60 + date.second;
    const std::int64_t word = (seconds + 3) / 6;
    if (word < 0 || word >= 14400) {
        return 0;
    }
    ok = true;
    return (static_cast<std::uint32_t>(day) << 16) | static_cast<std::uint32_t>(word);
}

core::view::DateValue decode_packed_date(std::uint32_t raw, bool& ok) noexcept {
    ok = false;
    const std::uint32_t day = raw >> 16;
    const std::uint32_t word = raw & 0xffffu;
    if (day > 0xfffeu || word >= 14400u) {
        return {};
    }
    core::view::DateValue out =
        civil_from_days(static_cast<std::int32_t>(day) + kPackedDateEpochDay);
    const std::int64_t seconds = static_cast<std::int64_t>(word) * 6;
    out.hour = static_cast<std::uint8_t>(seconds / 3600);
    out.minute = static_cast<std::uint8_t>((seconds % 3600) / 60);
    out.second = static_cast<std::uint8_t>(seconds % 60);
    if (!out.valid()) {
        return {};
    }
    ok = true;
    return out;
}

std::size_t storage_block_size(FieldKind kind) noexcept {
    switch (kind) {
        case FieldKind::Text: return 0;  /* variable: UTF-16 + NUL */
        case FieldKind::Number: return 8;
        case FieldKind::Date: return 4;
        case FieldKind::Flag: return 4;
        case FieldKind::Duration: return 4;
        case FieldKind::Cost: return 8;
    }
    return 0;
}

StorageConfidence storage_confidence(FieldKind kind) noexcept {
    switch (kind) {
        case FieldKind::Text:
        case FieldKind::Number:
        case FieldKind::Date:
        case FieldKind::Duration:
            return StorageConfidence::Measured;
        case FieldKind::Flag:
        case FieldKind::Cost:
            return StorageConfidence::Unmeasured;
    }
    return StorageConfidence::Unmeasured;
}

std::string encode_text_utf16le(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size() * 2 + 2);
    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto byte = static_cast<unsigned char>(utf8[index]);
        std::uint32_t code_point = 0;
        std::size_t extra = 0;
        if (byte < 0x80u) {
            code_point = byte;
        } else if ((byte & 0xe0u) == 0xc0u) {
            code_point = byte & 0x1fu;
            extra = 1;
        } else if ((byte & 0xf0u) == 0xe0u) {
            code_point = byte & 0x0fu;
            extra = 2;
        } else if ((byte & 0xf8u) == 0xf0u) {
            code_point = byte & 0x07u;
            extra = 3;
        } else {
            code_point = 0xfffdu;
            extra = 0;
        }
        if (extra > 0 && index + extra >= utf8.size()) {
            code_point = 0xfffdu;
            extra = 0;
        } else {
            bool valid = true;
            for (std::size_t i = 1; i <= extra && valid; ++i) {
                if (index + i >= utf8.size()) {
                    valid = false;
                    break;
                }
                const auto continuation = static_cast<unsigned char>(utf8[index + i]);
                if ((continuation & 0xc0u) != 0x80u) {
                    valid = false;
                    break;
                }
                code_point = (code_point << 6) | (continuation & 0x3fu);
            }
            if (!valid || code_point > 0x10ffffu ||
                (code_point >= 0xd800u && code_point <= 0xdfffu)) {
                code_point = 0xfffdu;
                extra = 0;
            }
        }
        append_utf16(out, code_point);
        index += extra + 1;
    }
    out.push_back('\0');
    out.push_back('\0');
    return out;
}

bool decode_text_utf16le(const std::uint8_t* data, std::size_t size, std::string& out) noexcept {
    if (data == nullptr || size % 2 != 0) {
        return false;
    }
    out.clear();
    std::size_t index = 0;
    while (index + 1 < size) {
        const std::uint16_t unit = read_u16(data + index);
        index += 2;
        if (unit == 0) {
            return true;
        }
        std::uint32_t code_point = unit;
        if (unit >= 0xd800u && unit <= 0xdbffu) {
            if (index + 1 < size) {
                const std::uint16_t low = read_u16(data + index);
                if (low >= 0xdc00u && low <= 0xdfffu) {
                    index += 2;
                    code_point = 0x10000u + ((static_cast<std::uint32_t>(unit) - 0xd800u) << 10) +
                                 (static_cast<std::uint32_t>(low) - 0xdc00u);
                } else {
                    code_point = 0xfffdu;
                }
            } else {
                code_point = 0xfffdu;
            }
        } else if (unit >= 0xdc00u && unit <= 0xdfffu) {
            code_point = 0xfffdu;
        }
        append_utf8(out, code_point);
    }
    /* No NUL terminator: accept the block, it is still valid text. */
    return true;
}

EncodedValue encode_value(FieldKind kind, const core::view::Value& value) {
    EncodedValue out;
    if (value.is_null()) {
        out.error = "cannot encode a blank value";
        return out;
    }
    switch (kind) {
        case FieldKind::Text: {
            if (value.kind != core::view::ValueKind::Text) {
                out.error = "Text storage needs a Text value";
                return out;
            }
            const std::string bytes = encode_text_utf16le(value.text);
            out.bytes.assign(bytes.begin(), bytes.end());
            out.ok = true;
            return out;
        }
        case FieldKind::Number:
        case FieldKind::Cost: {
            double number = 0.0;
            if (!number_value(value, number)) {
                out.error = "numeric storage needs a Number/Integer/Bool value";
                return out;
            }
            std::uint64_t bits = 0;
            std::memcpy(&bits, &number, sizeof(bits));
            append_u64(out.bytes, bits);
            out.ok = true;
            return out;
        }
        case FieldKind::Duration: {
            if (value.kind != core::view::ValueKind::Duration) {
                out.error = "Duration storage needs a Duration value (6-second ticks)";
                return out;
            }
            if (value.integer < 0 ||
                static_cast<std::uint64_t>(value.integer) > 0xffffffffull) {
                out.error = "duration ticks do not fit the u32 file field";
                return out;
            }
            append_u32(out.bytes, static_cast<std::uint32_t>(value.integer));
            out.ok = true;
            return out;
        }
        case FieldKind::Date: {
            if (value.kind != core::view::ValueKind::Date) {
                out.error = "Date storage needs a Date value";
                return out;
            }
            bool ok = false;
            const std::uint32_t packed = encode_packed_date(value.date, ok);
            if (!ok) {
                out.error = "date is outside the packed-date range";
                return out;
            }
            append_u32(out.bytes, packed);
            out.ok = true;
            return out;
        }
        case FieldKind::Flag: {
            bool flag = false;
            if (!flag_value(value, flag)) {
                out.error = "Flag storage needs a Bool or an integer 0/1";
                return out;
            }
            /* OUR CHOICE, not measured: u32 little-endian 0/1. */
            append_u32(out.bytes, flag ? 1u : 0u);
            out.ok = true;
            return out;
        }
    }
    out.error = "unknown field kind";
    return out;
}

DecodedValue decode_value(FieldKind kind, const std::uint8_t* data, std::size_t size) {
    DecodedValue out;
    if (data == nullptr) {
        out.error = "no storage bytes";
        return out;
    }
    switch (kind) {
        case FieldKind::Text: {
            if (size == 0) {
                out.error = "empty Text block";
                return out;
            }
            std::string text;
            if (!decode_text_utf16le(data, size, text)) {
                out.error = "Text block is not valid UTF-16LE";
                return out;
            }
            out.value = core::view::Value::of_text(std::move(text));
            out.ok = true;
            return out;
        }
        case FieldKind::Number:
        case FieldKind::Cost: {
            if (size != 8) {
                out.error = "numeric storage is 8 bytes";
                return out;
            }
            const std::uint64_t bits = read_u64(data);
            double number = 0.0;
            std::memcpy(&number, &bits, sizeof(number));
            out.value = core::view::Value::of_number(number);
            out.ok = true;
            return out;
        }
        case FieldKind::Duration: {
            if (size != 4) {
                out.error = "Duration storage is 4 bytes";
                return out;
            }
            out.value = core::view::Value::of_duration(static_cast<std::int64_t>(read_u32(data)));
            out.ok = true;
            return out;
        }
        case FieldKind::Date: {
            if (size != 4) {
                out.error = "Date storage is 4 bytes";
                return out;
            }
            bool ok = false;
            const core::view::DateValue date = decode_packed_date(read_u32(data), ok);
            if (!ok) {
                out.error = "packed date is out of range";
                return out;
            }
            out.value = core::view::Value::of_date(date);
            out.ok = true;
            return out;
        }
        case FieldKind::Flag: {
            if (size != 4) {
                out.error = "Flag storage is 4 bytes";
                return out;
            }
            out.value = core::view::Value::of_bool(read_u32(data) != 0);
            out.ok = true;
            return out;
        }
    }
    out.error = "unknown field kind";
    return out;
}

}  // namespace core::formula
