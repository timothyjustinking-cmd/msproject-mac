/*
 * render_test.h -- shared harness for tests/render_*.cpp.
 *
 * Two jobs:
 *   1. a tiny check counter (the project's test style: "render_tests: N checks
 *      passed"), and
 *   2. an independent PDF *parser* used to validate the writer's output.  The
 *      parser does not call into core::render: it reads the bytes, follows the
 *      startxref offset, parses the cross-reference table, resolves /Root to
 *      /Pages, counts /Type /Page objects and extracts every content stream so
 *      the tests can check the text that was actually emitted.
 *
 * Header-only and dependency-free so all tests/render_*.cpp share one copy.
 */

#ifndef CORE_RENDER_TEST_H
#define CORE_RENDER_TEST_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace render_test {

struct TestContext {
    const char* suite = "render";
    int checks = 0;
    int failures = 0;

    void check(bool condition, const char* what, int line) {
        ++checks;
        if (!condition) {
            ++failures;
            std::printf("FAIL [%s] line %d: %s\n", suite, line, what);
        }
    }
};

#define RCHECK(ctx, condition) (ctx).check((condition), #condition, __LINE__)

/* ---- a structural view of the produced document ------------------------- */

struct PdfDocument {
    bool header_ok = false;
    std::string version;
    bool eof_last = false;
    bool startxref_found = false;
    long long startxref = -1;
    bool xref_at_startxref = false;

    long long xref_first = -1;      /* first object number in the table */
    long long xref_count = 0;       /* entries in the table */
    std::vector<long long> offsets; /* index = object number (0 unused) */
    std::vector<bool> in_use;

    bool trailer_found = false;
    long long trailer_size = -1;
    long long root_object = -1;
    long long info_object = -1;

    bool pages_found = false;
    long long pages_object = -1;
    long long pages_count = -1;
    std::vector<long long> kids;

    struct Page {
        long long object = 0;
        bool has_media_box = false;
        double media_width = 0.0;
        double media_height = 0.0;
        long long contents_object = -1;
        std::string content;
    };
    std::vector<Page> pages;
    std::vector<std::string> literals; /* decoded literal strings, in file order */
    bool bytes_outside_strings_clean = true;
    bool raw_utf8_in_strings = false;
};

namespace detail {

/* Reads a decimal integer, skipping whitespace first. */
inline long long read_number_at(const std::string& text, std::size_t& index) {
    while (index < text.size()) {
        const char c = text[index];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            ++index;
        } else {
            break;
        }
    }
    long long value = 0;
    bool negative = false;
    if (index < text.size() && text[index] == '-') {
        negative = true;
        ++index;
    }
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        value = value * 10 + (text[index] - '0');
        ++index;
    }
    return negative ? -value : value;
}

/* True when "/Type /Page" occurs in `body` and is not the "/Pages" prefix. */
inline bool has_page_type(const std::string& body) {
    const std::string needle = "/Type /Page";
    std::size_t at = body.find(needle);
    while (at != std::string::npos) {
        const std::size_t after = at + needle.size();
        if (after >= body.size() || body[after] != 's') {
            return true;
        }
        at = body.find(needle, after);
    }
    return false;
}

/*
 * Decodes a literal string body (the bytes between the parentheses, still
 * escaped) into raw bytes.  Handles \\ \( \) \n \r \t \b \f and \ddd.
 */
inline std::string decode_literal(std::string_view body) {
    std::string out;
    out.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        ++i;
        if (i >= body.size()) {
            break;
        }
        const char escaped = body[i];
        switch (escaped) {
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case '(':
                out += '(';
                break;
            case ')':
                out += ')';
                break;
            case '\\':
                out += '\\';
                break;
            default:
                if (escaped >= '0' && escaped <= '7') {
                    int value = 0;
                    int digits = 0;
                    while (digits < 3 && i < body.size() && body[i] >= '0' && body[i] <= '7') {
                        value = value * 8 + (body[i] - '0');
                        ++i;
                        ++digits;
                    }
                    --i;
                    out += static_cast<char>(value & 0xFF);
                } else {
                    out += escaped;
                }
                break;
        }
    }
    return out;
}

/*
 * Walks a content stream and appends every literal string's decoded bytes.
 * Also reports whether any non-ASCII byte occurs OUTSIDE a string literal (a
 * stray WinAnsi/UTF-8 byte in an operator would be a bug).
 */
inline void scan_content(const std::string& content, std::vector<std::string>& literals,
                         bool& clean_outside, bool& raw_utf8_in_strings) {
    std::size_t i = 0;
    while (i < content.size()) {
        const unsigned char byte = static_cast<unsigned char>(content[i]);
        if (byte == '(') {
            const std::size_t start = i + 1;
            std::size_t end = start;
            int depth = 1;
            while (end < content.size()) {
                if (content[end] == '\\') {
                    end += 2;
                    continue;
                }
                if (content[end] == '(') {
                    ++depth;
                } else if (content[end] == ')') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                ++end;
            }
            const std::string decoded =
                decode_literal(std::string_view(content).substr(start, end - start));
            literals.push_back(decoded);
            for (std::size_t k = 0; k + 1 < decoded.size(); ++k) {
                const unsigned char a = static_cast<unsigned char>(decoded[k]);
                const unsigned char b = static_cast<unsigned char>(decoded[k + 1]);
                if (a == 0xC3u && b >= 0x80u && b <= 0xBFu) {
                    raw_utf8_in_strings = true;
                }
            }
            i = (end < content.size()) ? end + 1 : content.size();
            continue;
        }
        if ((byte < 0x20u && byte != '\n' && byte != '\r' && byte != '\t') || byte >= 0x80u) {
            clean_outside = false;
        }
        ++i;
    }
}

/* Body of object `number`, located through the cross-reference table. */
inline bool object_body(const std::string& bytes, const std::vector<long long>& offsets,
                        long long number, std::string& body) {
    if (number <= 0 || static_cast<std::size_t>(number) >= offsets.size()) {
        return false;
    }
    const long long offset = offsets[static_cast<std::size_t>(number)];
    if (offset < 0 || static_cast<std::size_t>(offset) >= bytes.size()) {
        return false;
    }
    const std::string prefix = std::to_string(number) + " 0 obj";
    if (bytes.compare(static_cast<std::size_t>(offset), prefix.size(), prefix) != 0) {
        return false;
    }
    const std::size_t begin = static_cast<std::size_t>(offset) + prefix.size();
    const std::size_t end = bytes.find("endobj", begin);
    if (end == std::string::npos) {
        return false;
    }
    body = bytes.substr(begin, end - begin);
    return true;
}

}  // namespace detail

/*
 * Parses `bytes` as a PDF produced by core::render::PdfWriter.  Returns false and
 * fills `error` on the first structural failure.  It is deliberately strict:
 * this is the independent check the writer has to satisfy.
 */
inline bool parse_pdf(const std::string& bytes, PdfDocument& doc, std::string& error) {
    auto fail = [&error](const std::string& message) {
        error = message;
        return false;
    };

    if (bytes.size() < 32) {
        return fail("file too short");
    }
    if (bytes.compare(0, 5, "%PDF-") != 0) {
        return fail("missing %PDF- header");
    }
    doc.header_ok = true;
    {
        const std::size_t version_end = bytes.find('\n', 0);
        doc.version = bytes.substr(5, version_end == std::string::npos ? 3 : version_end - 5);
    }

    const std::size_t eof = bytes.rfind("%%EOF");
    if (eof == std::string::npos) {
        return fail("missing %%EOF");
    }
    const std::string tail = bytes.substr(eof + 5);
    doc.eof_last = tail.empty() || tail == "\n" || tail == "\r\n";
    if (!doc.eof_last) {
        return fail("%%EOF is not the last thing in the file");
    }

    const std::size_t sx = bytes.rfind("startxref");
    if (sx == std::string::npos) {
        return fail("missing startxref");
    }
    doc.startxref_found = true;
    {
        std::size_t index = sx + 9;
        doc.startxref = detail::read_number_at(bytes, index);
    }
    if (doc.startxref <= 0 || static_cast<std::size_t>(doc.startxref) >= bytes.size()) {
        return fail("startxref offset out of range");
    }
    if (bytes.compare(static_cast<std::size_t>(doc.startxref), 4, "xref") != 0) {
        return fail("startxref does not point at the xref table");
    }
    doc.xref_at_startxref = true;

    {
        std::size_t index = static_cast<std::size_t>(doc.startxref) + 4;
        doc.xref_first = detail::read_number_at(bytes, index);
        doc.xref_count = detail::read_number_at(bytes, index);
        if (doc.xref_first != 0 || doc.xref_count <= 0) {
            return fail("malformed xref subsection header");
        }
        doc.offsets.assign(static_cast<std::size_t>(doc.xref_count), -1);
        doc.in_use.assign(static_cast<std::size_t>(doc.xref_count), false);
        while (index < bytes.size() && (bytes[index] == '\r' || bytes[index] == '\n')) {
            ++index;
        }
        for (long long i = 0; i < doc.xref_count; ++i) {
            if (index + 20 > bytes.size()) {
                return fail("xref table truncated");
            }
            std::size_t entry = index;
            doc.offsets[static_cast<std::size_t>(i)] = detail::read_number_at(bytes, entry);
            doc.in_use[static_cast<std::size_t>(i)] = bytes[index + 17] == 'n';
            index += 20;
        }
    }
    if (doc.in_use.empty() || doc.in_use[0]) {
        return fail("xref entry 0 must be the free entry");
    }
    for (long long i = 1; i < doc.xref_count; ++i) {
        const long long offset = doc.offsets[static_cast<std::size_t>(i)];
        if (offset < 0 || static_cast<std::size_t>(offset) >= bytes.size()) {
            return fail("xref entry " + std::to_string(i) + " has an out-of-range offset");
        }
        const std::string prefix = std::to_string(i) + " 0 obj";
        if (bytes.compare(static_cast<std::size_t>(offset), prefix.size(), prefix) != 0) {
            return fail("xref entry " + std::to_string(i) + " does not land on '" + prefix + "'");
        }
        if (!doc.in_use[static_cast<std::size_t>(i)]) {
            return fail("xref entry " + std::to_string(i) + " is marked free but exists");
        }
    }

    const std::size_t trailer = bytes.find("trailer", static_cast<std::size_t>(doc.startxref));
    if (trailer == std::string::npos) {
        return fail("missing trailer");
    }
    doc.trailer_found = true;
    const std::size_t trailer_end = bytes.find("startxref", trailer);
    const std::string trailer_text = bytes.substr(trailer, trailer_end - trailer);
    {
        const std::size_t size_at = trailer_text.find("/Size");
        if (size_at == std::string::npos) {
            return fail("trailer has no /Size");
        }
        std::size_t index = size_at + 5;
        doc.trailer_size = detail::read_number_at(trailer_text, index);
        if (doc.trailer_size != doc.xref_count) {
            return fail("/Size does not match the xref entry count");
        }
        const std::size_t root_at = trailer_text.find("/Root");
        if (root_at == std::string::npos) {
            return fail("trailer has no /Root");
        }
        std::size_t index_root = root_at + 5;
        doc.root_object = detail::read_number_at(trailer_text, index_root);
        const std::size_t info_at = trailer_text.find("/Info");
        if (info_at == std::string::npos) {
            return fail("trailer has no /Info");
        }
        std::size_t index_info = info_at + 5;
        doc.info_object = detail::read_number_at(trailer_text, index_info);
    }

    /* /Root -> /Catalog -> /Pages, then /Count and /Kids. */
    std::string catalog;
    if (!detail::object_body(bytes, doc.offsets, doc.root_object, catalog)) {
        return fail("/Root object not found");
    }
    if (catalog.find("/Type /Catalog") == std::string::npos) {
        return fail("/Root object is not a /Catalog");
    }
    {
        const std::size_t pages_at = catalog.find("/Pages");
        if (pages_at == std::string::npos) {
            return fail("/Catalog has no /Pages");
        }
        std::size_t index = pages_at + 6;
        doc.pages_object = detail::read_number_at(catalog, index);
    }
    std::string pages_text;
    if (!detail::object_body(bytes, doc.offsets, doc.pages_object, pages_text)) {
        return fail("/Pages object not found");
    }
    doc.pages_found = true;
    if (pages_text.find("/Type /Pages") == std::string::npos) {
        return fail("/Pages object has the wrong type");
    }
    {
        const std::size_t count_at = pages_text.find("/Count");
        if (count_at == std::string::npos) {
            return fail("/Pages has no /Count");
        }
        std::size_t index = count_at + 6;
        doc.pages_count = detail::read_number_at(pages_text, index);
        if (doc.pages_count < 0) {
            return fail("/Count is negative");
        }
        const std::size_t kids_at = pages_text.find("/Kids");
        if (kids_at == std::string::npos) {
            return fail("/Pages has no /Kids");
        }
        std::size_t index_kids = pages_text.find('[', kids_at);
        if (index_kids == std::string::npos) {
            return fail("/Kids has no array");
        }
        ++index_kids;
        while (index_kids < pages_text.size() && pages_text[index_kids] != ']') {
            if (pages_text[index_kids] >= '0' && pages_text[index_kids] <= '9') {
                doc.kids.push_back(detail::read_number_at(pages_text, index_kids));
                /* Skip the generation number and the "R" of "<n> <gen> R". */
                while (index_kids < pages_text.size() &&
                       (pages_text[index_kids] == ' ' || pages_text[index_kids] == '\r' ||
                        pages_text[index_kids] == '\n')) {
                    ++index_kids;
                }
                if (index_kids < pages_text.size() && pages_text[index_kids] >= '0' &&
                    pages_text[index_kids] <= '9') {
                    (void)detail::read_number_at(pages_text, index_kids);
                }
                while (index_kids < pages_text.size() &&
                       (pages_text[index_kids] == ' ' || pages_text[index_kids] == '\r' ||
                        pages_text[index_kids] == '\n')) {
                    ++index_kids;
                }
                if (index_kids < pages_text.size() && pages_text[index_kids] == 'R') {
                    ++index_kids;
                }
            } else {
                ++index_kids;
            }
        }
        if (static_cast<long long>(doc.kids.size()) != doc.pages_count) {
            return fail("/Count does not match the number of /Kids");
        }
    }

    for (const long long object : doc.kids) {
        std::string body;
        if (!detail::object_body(bytes, doc.offsets, object, body)) {
            return fail("page object " + std::to_string(object) + " not found");
        }
        PdfDocument::Page page;
        page.object = object;
        if (!detail::has_page_type(body)) {
            return fail("page object " + std::to_string(object) + " is not /Type /Page");
        }
        const std::size_t box_at = body.find("/MediaBox");
        if (box_at == std::string::npos) {
            return fail("page object " + std::to_string(object) + " has no /MediaBox");
        }
        const std::size_t open_bracket = body.find('[', box_at);
        const std::size_t close_bracket = body.find(']', box_at);
        if (open_bracket == std::string::npos || close_bracket == std::string::npos) {
            return fail("page /MediaBox is malformed");
        }
        {
            const std::string box =
                body.substr(open_bracket + 1, close_bracket - open_bracket - 1);
            std::size_t index = 0;
            (void)detail::read_number_at(box, index); /* x0 */
            (void)detail::read_number_at(box, index); /* y0 */
            page.media_width = static_cast<double>(detail::read_number_at(box, index));
            page.media_height = static_cast<double>(detail::read_number_at(box, index));
            page.has_media_box = page.media_width > 0.0 && page.media_height > 0.0;
        }
        const std::size_t contents_at = body.find("/Contents");
        if (contents_at == std::string::npos) {
            return fail("page object " + std::to_string(object) + " has no /Contents");
        }
        {
            std::size_t index = contents_at + 9;
            page.contents_object = detail::read_number_at(body, index);
        }
        std::string content_object;
        if (!detail::object_body(bytes, doc.offsets, page.contents_object, content_object)) {
            return fail("content stream object not found");
        }
        const std::size_t length_at = content_object.find("/Length");
        const std::size_t stream_at = content_object.find("stream\n");
        if (length_at == std::string::npos || stream_at == std::string::npos) {
            return fail("content stream object has no /Length or stream keyword");
        }
        std::size_t index = length_at + 7;
        const long long length = detail::read_number_at(content_object, index);
        const long long offset = doc.offsets[static_cast<std::size_t>(page.contents_object)];
        const std::string prefix = std::to_string(page.contents_object) + " 0 obj";
        const std::size_t object_begin = static_cast<std::size_t>(offset) + prefix.size();
        const std::size_t data_at = object_begin + stream_at + 7;
        if (length < 0 || data_at + static_cast<std::size_t>(length) > bytes.size()) {
            return fail("content stream length is out of range");
        }
        page.content = bytes.substr(data_at, static_cast<std::size_t>(length));
        detail::scan_content(page.content, doc.literals, doc.bytes_outside_strings_clean,
                              doc.raw_utf8_in_strings);
        doc.pages.push_back(std::move(page));
    }

    if (doc.pages.size() != static_cast<std::size_t>(doc.pages_count)) {
        return fail("page object count does not match /Count");
    }
    return true;
}

/* Full structural validation used by the report tests: one call, one verdict. */
inline bool validate_pdf(const std::string& bytes, std::size_t expected_pages,
                         std::string& error) {
    PdfDocument doc;
    if (!parse_pdf(bytes, doc, error)) {
        return false;
    }
    if (doc.pages.size() != expected_pages) {
        error = "expected " + std::to_string(expected_pages) + " pages, parsed " +
                std::to_string(doc.pages.size());
        return false;
    }
    if (!doc.bytes_outside_strings_clean) {
        error = "non-ASCII or control bytes outside a string literal";
        return false;
    }
    return true;
}

/* True when `needle` occurs anywhere in the document. */
inline bool contains(const std::string& bytes, std::string_view needle) {
    return bytes.find(needle) != std::string::npos;
}

/* Counts non-overlapping occurrences. */
inline std::size_t count_occurrences(const std::string& bytes, std::string_view needle) {
    std::size_t count = 0;
    std::size_t at = 0;
    while ((at = bytes.find(needle, at)) != std::string::npos) {
        ++count;
        at += needle.size();
    }
    return count;
}

/* FNV-1a over the bytes; a deterministic fingerprint for the output. */
inline std::string fnv1a_hex(const std::string& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(buffer);
}

}  // namespace render_test

/* Defined in tests/render_pdf_tests.cpp and tests/render_report_tests.cpp. */
int run_pdf_tests(render_test::TestContext& context);
int run_report_tests(render_test::TestContext& context);

#endif  // CORE_RENDER_TEST_H
