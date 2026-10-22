#include "xml.h"

#include <cstdint>
#include <string>

namespace core::io {
namespace {

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_name_start(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':' ||
           static_cast<unsigned char>(c) >= 0x80;
}

bool is_name_char(char c) noexcept {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

std::string strip_prefix(std::string_view name) {
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) {
        return std::string(name);
    }
    return std::string(name.substr(colon + 1));
}

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

bool decode_entities(std::string_view raw, std::string& out, std::string& error) {
    out.clear();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') {
            out.push_back(raw[i]);
            continue;
        }
        const std::size_t semicolon = raw.find(';', i + 1);
        if (semicolon == std::string_view::npos) {
            error = "unterminated entity reference";
            return false;
        }
        const std::string_view entity = raw.substr(i + 1, semicolon - i - 1);
        if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else if (!entity.empty() && entity[0] == '#') {
            std::uint32_t code_point = 0;
            bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
            const std::size_t start = hex ? 2 : 1;
            if (start >= entity.size()) {
                error = "empty character reference";
                return false;
            }
            for (std::size_t j = start; j < entity.size(); ++j) {
                const char c = entity[j];
                code_point *= hex ? 16u : 10u;
                if (c >= '0' && c <= '9') {
                    code_point += static_cast<std::uint32_t>(c - '0');
                } else if (hex && c >= 'a' && c <= 'f') {
                    code_point += static_cast<std::uint32_t>(c - 'a' + 10);
                } else if (hex && c >= 'A' && c <= 'F') {
                    code_point += static_cast<std::uint32_t>(c - 'A' + 10);
                } else {
                    error = "invalid character reference";
                    return false;
                }
                if (code_point > 0x10FFFF) {
                    error = "character reference out of range";
                    return false;
                }
            }
            append_utf8(out, code_point);
        } else {
            error = "unknown entity &" + std::string(entity) + ";";
            return false;
        }
        i = semicolon;
    }
    return true;
}

class XmlParser {
  public:
    explicit XmlParser(std::string_view text) : text_(text) {}

    XmlParseResult run() {
        XmlParseResult result;
        if (text_.substr(0, 3) == "\xEF\xBB\xBF") {
            advance(3);
        }
        skip_misc();
        if (!parse_element(result.root, 0)) {
            result.error = error_;
            result.line = error_line_ != 0 ? error_line_ : line_;
            return result;
        }
        skip_misc();
        if (!at_end()) {
            fail("unexpected content after the document element");
            result.error = error_;
            result.line = error_line_ != 0 ? error_line_ : line_;
            return result;
        }
        result.ok = true;
        result.line = line_;
        return result;
    }

  private:
    bool at_end() const noexcept { return pos_ >= text_.size(); }
    char peek(std::size_t ahead = 0) const noexcept { return text_[pos_ + ahead]; }
    bool starts_with(std::string_view needle) const noexcept {
        return text_.substr(pos_, needle.size()) == needle;
    }

    void advance(std::size_t count) {
        for (std::size_t i = 0; i < count && pos_ < text_.size(); ++i) {
            if (text_[pos_] == '\n') {
                ++line_;
            }
            ++pos_;
        }
    }

    void fail(const std::string& message) {
        if (error_.empty()) {
            error_ = message;
            error_line_ = line_;
        }
    }

    bool expect(std::string_view needle, const char* what) {
        if (!starts_with(needle)) {
            fail(std::string("expected ") + what);
            return false;
        }
        advance(needle.size());
        return true;
    }

    void skip_whitespace() {
        while (!at_end() && is_space(peek())) {
            advance(1);
        }
    }

    /* Whitespace, comments, processing instructions and DOCTYPE. */
    void skip_misc() {
        for (;;) {
            skip_whitespace();
            if (starts_with("<!--")) {
                skip_comment();
            } else if (starts_with("<?")) {
                skip_until("?>");
            } else if (starts_with("<!DOCTYPE") || starts_with("<!doctype")) {
                skip_doctype();
            } else {
                return;
            }
        }
    }

    void skip_comment() {
        advance(4);
        const std::size_t end = text_.find("-->", pos_);
        if (end == std::string_view::npos) {
            fail("unterminated comment");
            pos_ = text_.size();
            return;
        }
        advance(end - pos_ + 3);
    }

    void skip_until(std::string_view terminator) {
        const std::size_t end = text_.find(terminator, pos_);
        if (end == std::string_view::npos) {
            fail("unterminated markup");
            pos_ = text_.size();
            return;
        }
        advance(end - pos_ + terminator.size());
    }

    void skip_doctype() {
        /* Bracket-aware so an internal subset's '>' does not end it early. */
        int depth = 0;
        while (!at_end()) {
            const char c = peek();
            if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
            } else if (c == '>' && depth <= 0) {
                advance(1);
                return;
            } else if (c == '<' && starts_with("<!--")) {
                skip_comment();
                continue;
            }
            advance(1);
        }
        fail("unterminated DOCTYPE");
    }

    bool parse_name(std::string& out) {
        if (at_end() || !is_name_start(peek())) {
            fail("expected an element or attribute name");
            return false;
        }
        const std::size_t start = pos_;
        while (!at_end() && is_name_char(peek())) {
            advance(1);
        }
        out = strip_prefix(text_.substr(start, pos_ - start));
        return true;
    }

    bool parse_element(XmlElement& element, int depth) {
        if (depth > 256) {
            fail("nesting too deep");
            return false;
        }
        if (!expect("<", "'<'")) {
            return false;
        }
        element.line = line_;
        if (!parse_name(element.name)) {
            return false;
        }
        /* Attributes. */
        for (;;) {
            skip_whitespace();
            if (at_end()) {
                fail("unterminated start tag");
                return false;
            }
            if (peek() == '>') {
                advance(1);
                break;
            }
            if (peek() == '/' && pos_ + 1 < text_.size() && peek(1) == '>') {
                advance(2);
                return true;  // self-closing
            }
            XmlAttribute attribute;
            const std::size_t attribute_line = line_;
            if (!parse_name(attribute.name)) {
                return false;
            }
            skip_whitespace();
            if (!expect("=", "'=' after attribute name")) {
                return false;
            }
            skip_whitespace();
            if (at_end() || (peek() != '"' && peek() != '\'')) {
                fail("expected a quoted attribute value");
                return false;
            }
            const char quote = peek();
            advance(1);
            const std::size_t value_start = pos_;
            while (!at_end() && peek() != quote) {
                advance(1);
            }
            if (at_end()) {
                fail("unterminated attribute value");
                return false;
            }
            const std::string_view raw = text_.substr(value_start, pos_ - value_start);
            advance(1);  // closing quote
            std::string decoded;
            std::string entity_error;
            if (!decode_entities(raw, decoded, entity_error)) {
                error_line_ = attribute_line;
                fail(entity_error);
                return false;
            }
            for (const auto& existing : element.attributes) {
                if (existing.name == attribute.name) {
                    fail("duplicate attribute \"" + attribute.name + "\"");
                    return false;
                }
            }
            attribute.value = std::move(decoded);
            element.attributes.push_back(std::move(attribute));
        }
        /* Content. */
        for (;;) {
            if (at_end()) {
                fail("unterminated element <" + element.name + ">");
                return false;
            }
            if (starts_with("</")) {
                advance(2);
                std::string closing;
                if (!parse_name(closing)) {
                    return false;
                }
                if (closing != element.name) {
                    fail("mismatched closing tag </" + closing + "> for <" + element.name +
                         ">");
                    return false;
                }
                skip_whitespace();
                if (!expect(">", "'>' after closing tag")) {
                    return false;
                }
                return true;
            }
            if (starts_with("<!--")) {
                skip_comment();
                continue;
            }
            if (starts_with("<![CDATA[")) {
                advance(9);
                const std::size_t end = text_.find("]]>", pos_);
                if (end == std::string_view::npos) {
                    fail("unterminated CDATA section");
                    return false;
                }
                element.text.append(text_.substr(pos_, end - pos_));
                advance(end - pos_ + 3);
                continue;
            }
            if (starts_with("<?")) {
                skip_until("?>");
                continue;
            }
            if (peek() == '<') {
                element.children.emplace_back();
                if (!parse_element(element.children.back(), depth + 1)) {
                    return false;
                }
                continue;
            }
            /* Character data. */
            const std::size_t start = pos_;
            while (!at_end() && peek() != '<') {
                advance(1);
            }
            std::string decoded;
            std::string entity_error;
            if (!decode_entities(text_.substr(start, pos_ - start), decoded, entity_error)) {
                fail(entity_error);
                return false;
            }
            element.text += decoded;
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::string error_;
    std::size_t error_line_ = 0;
};

}  // namespace

XmlParseResult parse_xml(std::string_view input) {
    XmlParser parser(input);
    return parser.run();
}

const XmlElement* child(const XmlElement& element, std::string_view name) noexcept {
    for (const auto& candidate : element.children) {
        if (candidate.name == name) {
            return &candidate;
        }
    }
    return nullptr;
}

std::vector<const XmlElement*> children(const XmlElement& element, std::string_view name) {
    std::vector<const XmlElement*> matches;
    for (const auto& candidate : element.children) {
        if (candidate.name == name) {
            matches.push_back(&candidate);
        }
    }
    return matches;
}

const XmlAttribute* attribute(const XmlElement& element, std::string_view name) noexcept {
    for (const auto& candidate : element.attributes) {
        if (candidate.name == name) {
            return &candidate;
        }
    }
    return nullptr;
}

std::string text_of(const XmlElement& element) {
    std::size_t begin = 0;
    std::size_t end = element.text.size();
    while (begin < end && is_space(element.text[begin])) {
        ++begin;
    }
    while (end > begin && is_space(element.text[end - 1])) {
        --end;
    }
    return element.text.substr(begin, end - begin);
}

bool parse_bool_text(std::string_view text, bool& out) noexcept {
    if (text == "1" || text == "true" || text == "True") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "False") {
        out = false;
        return true;
    }
    return false;
}

std::string escape_xml(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

}  // namespace core::io
