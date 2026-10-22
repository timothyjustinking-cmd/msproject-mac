/*
 * pdf_writer.cpp -- see pdf_writer.h for the contract.
 *
 * Structure of the file this writes (object numbers are fixed so output is
 * deterministic):
 *
 *   1  /Catalog   /Pages 2 0 R
 *   2  /Pages     /Kids [...] /Count n, inheritable /Resources with /F1 /F2
 *   3  /Info      title / creator / producer / creation date
 *   4  Helvetica  /Type1, /WinAnsiEncoding
 *   5  Helvetica-Bold
 *   6, 8, ...     one /Page per page, /Contents <next object> 0 R
 *   7, 9, ...     one content stream per page
 *
 * No object streams and no compression: the cross-reference table is a classic
 * table with absolute byte offsets, which is what the structural test parses.
 */

#include "pdf_writer.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace core::render {
namespace {

/* ---- deterministic number formatting ----------------------------------- */

constexpr std::int64_t kMaxScaled = 1000000000000LL; /* |value| < 1e9 at 3 dp */

std::int64_t pow10_int(int decimals) {
    std::int64_t value = 1;
    for (int i = 0; i < decimals; ++i) {
        value *= 10;
    }
    return value;
}

/*
 * Appends `value` in fixed point with `decimals` digits, trimming trailing
 * zeros.  Written by hand because iostreams and even snprintf honour LC_NUMERIC
 * -- a report must not change bytes when the app's locale uses ',' as the
 * decimal separator.
 */
void append_fixed(std::string& out, double value, int decimals) {
    if (!std::isfinite(value)) {
        value = 0.0;
    }
    if (decimals < 0) {
        decimals = 0;
    }
    if (decimals > 6) {
        decimals = 6;
    }
    const std::int64_t scale = pow10_int(decimals);
    double scaled = value * static_cast<double>(scale);
    if (scaled > static_cast<double>(kMaxScaled)) {
        scaled = static_cast<double>(kMaxScaled);
    } else if (scaled < -static_cast<double>(kMaxScaled)) {
        scaled = -static_cast<double>(kMaxScaled);
    }
    std::int64_t fixed = static_cast<std::int64_t>(std::llround(scaled));
    if (fixed == 0) {
        out += '0';
        return;
    }
    if (fixed < 0) {
        out += '-';
        fixed = -fixed;
    }
    out += std::to_string(fixed / scale);
    if (decimals == 0) {
        return;
    }
    const std::int64_t fraction = fixed % scale;
    if (fraction == 0) {
        return;
    }
    std::string digits;
    for (int place = decimals - 1; place >= 0; --place) {
        const std::int64_t divisor = pow10_int(place);
        digits += static_cast<char>('0' + (fraction / divisor) % 10);
    }
    while (!digits.empty() && digits.back() == '0') {
        digits.pop_back();
    }
    if (!digits.empty()) {
        out += '.';
        out += digits;
    }
}

/* ---- UTF-8 ----------------------------------------------------------------- */

struct Decoded {
    std::uint32_t codepoint = 0;
    std::size_t length = 1; /* bytes consumed */
    bool valid = false;
};

Decoded decode_utf8(std::string_view text, std::size_t index) {
    Decoded result;
    const unsigned char first = static_cast<unsigned char>(text[index]);
    if (first < 0x80u) {
        result.codepoint = first;
        result.length = 1;
        result.valid = true;
        return result;
    }
    std::size_t extra = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if ((first & 0xE0u) == 0xC0u) {
        extra = 1;
        value = first & 0x1Fu;
        minimum = 0x80u;
    } else if ((first & 0xF0u) == 0xE0u) {
        extra = 2;
        value = first & 0x0Fu;
        minimum = 0x800u;
    } else if ((first & 0xF8u) == 0xF0u) {
        extra = 3;
        value = first & 0x07u;
        minimum = 0x10000u;
    } else {
        return result; /* continuation byte or invalid lead: one '?' */
    }
    if (index + extra >= text.size()) {
        /* Not enough bytes left for the continuation bytes. */
        return result;
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const unsigned char byte = static_cast<unsigned char>(text[index + i]);
        if ((byte & 0xC0u) != 0x80u) {
            return result;
        }
        value = (value << 6) | (byte & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
        return result;
    }
    result.codepoint = value;
    result.length = extra + 1;
    result.valid = true;
    return result;
}

/* CP1252 0x80..0x9F.  0 marks an undefined slot. */
constexpr std::uint32_t kCp1252High[32] = {
    0x20ACu, 0x0000u, 0x201Au, 0x0192u, 0x201Eu, 0x2026u, 0x2020u, 0x2021u,
    0x02C6u, 0x2030u, 0x0160u, 0x2039u, 0x0152u, 0x0000u, 0x017Du, 0x0000u,
    0x0000u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2013u, 0x2014u,
    0x02DCu, 0x2122u, 0x0161u, 0x203Au, 0x0153u, 0x0000u, 0x017Eu, 0x0178u,
};

/* One code point -> one WinAnsi byte, or false when not representable. */
bool winansi_byte(std::uint32_t codepoint, unsigned char& byte) {
    if (codepoint >= 0x20u && codepoint <= 0x7Eu) {
        byte = static_cast<unsigned char>(codepoint);
        return true;
    }
    if (codepoint >= 0xA0u && codepoint <= 0xFFu) {
        byte = static_cast<unsigned char>(codepoint);
        return true;
    }
    for (std::size_t i = 0; i < 32; ++i) {
        if (kCp1252High[i] != 0u && kCp1252High[i] == codepoint) {
            byte = static_cast<unsigned char>(0x80u + i);
            return true;
        }
    }
    return false;
}

/* ---- font metrics ---------------------------------------------------------- */
/*
 * Helvetica / Helvetica-Bold advance widths for ASCII 0x20..0x7E, in 1/1000 em,
 * from the standard Adobe AFM metrics.  Codes outside this range use 556, which
 * is exactly Helvetica's digit width, so a Latin-1 word is within a couple of
 * points.  This is a layout aid: no shaping, no kerning.  Declared `inline` so
 * a header-only user of the table would not get an unused-variable warning in a
 * translation unit that only links the library.
 */
constexpr short kHelveticaWidths[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584,
};

constexpr short kHelveticaBoldWidths[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584,
};

constexpr double kFallbackWidth = 556.0;

short glyph_width(PdfFont font, unsigned char byte) {
    if (byte >= 0x20u && byte <= 0x7Eu) {
        const std::size_t index = static_cast<std::size_t>(byte - 0x20u);
        return font == PdfFont::HelveticaBold ? kHelveticaBoldWidths[index]
                                              : kHelveticaWidths[index];
    }
    return static_cast<short>(kFallbackWidth);
}

std::string literal_string(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() + 8);
    out += '(';
    out += escape_pdf_string(bytes);
    out += ')';
    return out;
}

}  // namespace

const char* pdf_font_name(PdfFont font) noexcept {
    return font == PdfFont::HelveticaBold ? "Helvetica-Bold" : "Helvetica";
}

std::string winansi_encode(std::string_view utf8, std::size_t* substituted) {
    std::string out;
    out.reserve(utf8.size());
    std::size_t replaced = 0;
    std::size_t index = 0;
    while (index < utf8.size()) {
        const Decoded decoded = decode_utf8(utf8, index);
        index += decoded.length;
        if (!decoded.valid) {
            out += '?';
            ++replaced;
            continue;
        }
        unsigned char byte = 0;
        if (decoded.codepoint < 0x20u) {
            /* Single-line text: a control character would break the string. */
            out += ' ';
        } else if (winansi_byte(decoded.codepoint, byte)) {
            out += static_cast<char>(byte);
        } else {
            out += '?';
            ++replaced;
        }
    }
    if (substituted != nullptr) {
        *substituted = replaced;
    }
    return out;
}

std::string escape_pdf_string(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() + 8);
    for (const char raw : bytes) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '(':
                out += "\\(";
                break;
            case ')':
                out += "\\)";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (byte < 0x20u || byte == 0x7Fu) {
                    char buffer[5] = {'\\', 0, 0, 0, 0};
                    buffer[1] = static_cast<char>('0' + ((byte >> 6) & 0x7u));
                    buffer[2] = static_cast<char>('0' + ((byte >> 3) & 0x7u));
                    buffer[3] = static_cast<char>('0' + (byte & 0x7u));
                    out += buffer;
                } else {
                    out += raw;
                }
                break;
        }
    }
    return out;
}

PdfWriter::PdfWriter(PdfInfo info) : default_format_(PdfPageFormat::a4_landscape()), info_(std::move(info)) {}

PdfWriter::PdfWriter(PdfPageFormat default_format, PdfInfo info)
    : default_format_(default_format.valid() ? default_format : PdfPageFormat::a4_landscape()),
      info_(std::move(info)) {}

void PdfWriter::set_info(PdfInfo info) { info_ = std::move(info); }

void PdfWriter::set_default_page_format(PdfPageFormat format) {
    if (format.valid()) {
        default_format_ = format;
    }
}

std::size_t PdfWriter::add_page(PdfPageFormat format) {
    if (!format.valid()) {
        format = default_format_;
    }
    pages_.push_back(Page{format, std::string()});
    clip_depth_ = 0;
    return pages_.size();
}

PdfPageFormat PdfWriter::page_format(std::size_t index) const noexcept {
    if (index >= pages_.size()) {
        return PdfPageFormat{0.0, 0.0};
    }
    return pages_[index].format;
}

PdfPageFormat PdfWriter::current_page_format() const noexcept {
    if (pages_.empty()) {
        return default_format_;
    }
    return pages_.back().format;
}

PdfWriter::Page& PdfWriter::current_page() {
    if (pages_.empty()) {
        add_page(default_format_);
    }
    return pages_.back();
}

void PdfWriter::append_colour(PdfRgb colour, bool fill) {
    auto clamp = [](double value) {
        if (!std::isfinite(value)) {
            return 0.0;
        }
        if (value < 0.0) {
            return 0.0;
        }
        if (value > 1.0) {
            return 1.0;
        }
        return value;
    };
    Page& page = current_page();
    append_fixed(page.content, clamp(colour.r), 3);
    page.content += ' ';
    append_fixed(page.content, clamp(colour.g), 3);
    page.content += ' ';
    append_fixed(page.content, clamp(colour.b), 3);
    page.content += fill ? " rg\n" : " RG\n";
}

void PdfWriter::set_fill_rgb(PdfRgb colour) { fill_ = colour; }
void PdfWriter::set_stroke_rgb(PdfRgb colour) { stroke_ = colour; }

void PdfWriter::set_line_width(double points) {
    if (std::isfinite(points) && points > 0.0) {
        line_width_ = points;
    }
}

void PdfWriter::set_dash(const std::vector<double>& pattern, double phase) {
    dash_pattern_.clear();
    for (const double value : pattern) {
        if (std::isfinite(value) && value > 0.0) {
            dash_pattern_.push_back(value);
        }
    }
    if (dash_pattern_.size() % 2 != 0) {
        dash_pattern_.push_back(dash_pattern_.back());
    }
    dash_phase_ = (std::isfinite(phase) && phase >= 0.0) ? phase : 0.0;
}

namespace {

/* Y-down page coordinate -> PDF user-space y. */
double pdf_y(double height, double y) { return height - y; }

}  // namespace

void PdfWriter::line(PdfPoint from, PdfPoint to) {
    Page& page = current_page();
    page.content += "q\n";
    append_colour(stroke_, false);
    append_fixed(page.content, line_width_, 3);
    page.content += " w\n";
    if (!dash_pattern_.empty()) {
        page.content += '[';
        for (std::size_t i = 0; i < dash_pattern_.size(); ++i) {
            if (i != 0) {
                page.content += ' ';
            }
            append_fixed(page.content, dash_pattern_[i], 3);
        }
        page.content += "] ";
        append_fixed(page.content, dash_phase_, 3);
        page.content += " d\n";
    } else {
        page.content += "[] 0 d\n";
    }
    append_fixed(page.content, from.x, 3);
    page.content += ' ';
    append_fixed(page.content, pdf_y(page.format.height, from.y), 3);
    page.content += " m\n";
    append_fixed(page.content, to.x, 3);
    page.content += ' ';
    append_fixed(page.content, pdf_y(page.format.height, to.y), 3);
    page.content += " l\nS\nQ\n";
}

void PdfWriter::rect(double x, double y, double w, double h, bool fill, bool stroke) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)) {
        return;
    }
    if (w < 0.0) {
        x += w;
        w = -w;
    }
    if (h < 0.0) {
        y += h;
        h = -h;
    }
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    Page& page = current_page();
    page.content += "q\n";
    if (fill) {
        append_colour(fill_, true);
    }
    if (stroke) {
        append_colour(stroke_, false);
        append_fixed(page.content, line_width_, 3);
        page.content += " w\n";
    }
    append_fixed(page.content, x, 3);
    page.content += ' ';
    append_fixed(page.content, pdf_y(page.format.height, y + h), 3);
    page.content += ' ';
    append_fixed(page.content, w, 3);
    page.content += ' ';
    append_fixed(page.content, h, 3);
    page.content += " re\n";
    if (fill && stroke) {
        page.content += "B\n";
    } else if (fill) {
        page.content += "f\n";
    } else {
        page.content += "S\n";
    }
    page.content += "Q\n";
}

void PdfWriter::polygon(const std::vector<PdfPoint>& points, bool fill, bool stroke) {
    if (points.size() < 3) {
        return;
    }
    Page& page = current_page();
    page.content += "q\n";
    if (fill) {
        append_colour(fill_, true);
    }
    if (stroke) {
        append_colour(stroke_, false);
        append_fixed(page.content, line_width_, 3);
        page.content += " w\n";
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        append_fixed(page.content, points[i].x, 3);
        page.content += ' ';
        append_fixed(page.content, pdf_y(page.format.height, points[i].y), 3);
        page.content += (i == 0) ? " m\n" : " l\n";
    }
    page.content += "h\n";
    if (fill && stroke) {
        page.content += "B\n";
    } else if (fill) {
        page.content += "f\n";
    } else {
        page.content += "S\n";
    }
    page.content += "Q\n";
}

void PdfWriter::emit_text(double x, double baseline_y, std::string_view utf8, PdfFont font,
                          double size) {
    if (utf8.empty() || !(size > 0.0)) {
        return;
    }
    const std::string bytes = winansi_encode(utf8);
    if (bytes.empty()) {
        return;
    }
    Page& page = current_page();
    page.content += "BT\n/F";
    page.content += (font == PdfFont::HelveticaBold) ? '2' : '1';
    page.content += ' ';
    append_fixed(page.content, size, 2);
    page.content += " Tf\n";
    append_colour(fill_, true);
    page.content += "1 0 0 1 ";
    append_fixed(page.content, x, 3);
    page.content += ' ';
    append_fixed(page.content, pdf_y(page.format.height, baseline_y), 3);
    page.content += " Tm\n";
    page.content += literal_string(bytes);
    page.content += " Tj\nET\n";
}

void PdfWriter::text(double x, double baseline_y, std::string_view utf8, PdfFont font,
                     double size) {
    emit_text(x, baseline_y, utf8, font, size);
}

double PdfWriter::text_width(std::string_view utf8, PdfFont font, double size) const {
    if (!(size > 0.0)) {
        return 0.0;
    }
    const std::string bytes = winansi_encode(utf8);
    long total = 0;
    for (const char raw : bytes) {
        total += glyph_width(font, static_cast<unsigned char>(raw));
    }
    return static_cast<double>(total) * size / 1000.0;
}

void PdfWriter::begin_clip(double x, double y, double w, double h) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)) {
        return;
    }
    if (w < 0.0) {
        x += w;
        w = -w;
    }
    if (h < 0.0) {
        y += h;
        h = -h;
    }
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    Page& page = current_page();
    page.content += "q\n";
    append_fixed(page.content, x, 3);
    page.content += ' ';
    append_fixed(page.content, pdf_y(page.format.height, y + h), 3);
    page.content += ' ';
    append_fixed(page.content, w, 3);
    page.content += ' ';
    append_fixed(page.content, h, 3);
    page.content += " re\nW n\n";
    ++clip_depth_;
}

void PdfWriter::end_clip() {
    if (clip_depth_ == 0) {
        ++unbalanced_clips_;
        return;
    }
    current_page().content += "Q\n";
    --clip_depth_;
}

std::string PdfWriter::content_object_body(const std::string& content) const {
    /* The stream data is `content` plus the newline that follows it. */
    const std::size_t length = content.size() + 1;
    std::string body;
    body.reserve(content.size() + 48);
    body += "<< /Length ";
    body += std::to_string(length);
    body += " >>\nstream\n";
    body += content;
    body += "\nendstream";
    return body;
}

std::string PdfWriter::page_object_body(const PdfPageFormat& format,
                                       std::size_t content_object) const {
    std::string body = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ";
    append_fixed(body, format.width, 3);
    body += ' ';
    append_fixed(body, format.height, 3);
    body += "] /Contents ";
    body += std::to_string(content_object);
    body += " 0 R >>";
    return body;
}

bool PdfWriter::save(const std::string& path, std::string& error) const {
    const std::string bytes = finish();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot open '" + path + "' for writing";
        return false;
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) {
        error = "short write to '" + path + "'";
        return false;
    }
    return true;
}

std::string PdfWriter::finish() const {
    std::vector<Page> pages = pages_;
    if (pages.empty()) {
        pages.push_back(Page{default_format_, std::string()});
    }
    /* Balance any clip the caller left open.  Only the current (last) page can
     * have one, because add_page() resets the depth. */
    for (std::size_t i = 0; i < clip_depth_; ++i) {
        pages.back().content += "Q\n";
    }
    const std::size_t page_count = pages.size();
    const std::size_t object_count = 5 + 2 * page_count;

    /* Builds the body of object `number` (1-based). */
    auto object_body = [&](std::size_t number) -> std::string {
        if (number == 1) {
            return "<< /Type /Catalog /Pages 2 0 R >>";
        }
        if (number == 2) {
            std::string body = "<< /Type /Pages /Kids [";
            for (std::size_t i = 0; i < page_count; ++i) {
                if (i != 0) {
                    body += ' ';
                }
                body += std::to_string(6 + 2 * i);
                body += " 0 R";
            }
            body += "] /Count ";
            body += std::to_string(page_count);
            body += " /Resources << /Font << /F1 4 0 R /F2 5 0 R >> >> >>";
            return body;
        }
        if (number == 3) {
            std::string body = "<< /Title ";
            body += literal_string(winansi_encode(info_.title));
            body += " /Creator ";
            body += literal_string(winansi_encode(info_.creator));
            body += " /Producer ";
            body += literal_string(winansi_encode(info_.producer));
            body += " /CreationDate ";
            body += literal_string(winansi_encode(info_.creation_date));
            body += " >>";
            return body;
        }
        if (number == 4) {
            return "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                   "/Encoding /WinAnsiEncoding >>";
        }
        if (number == 5) {
            return "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold "
                   "/Encoding /WinAnsiEncoding >>";
        }
        const std::size_t offset = number - 6;
        const std::size_t index = offset / 2;
        if (offset % 2 == 0) {
            return page_object_body(pages[index].format, 7 + 2 * index);
        }
        return content_object_body(pages[index].content);
    };

    std::string out;
    out.reserve(1024 + page_count * 512);
    out += "%PDF-1.7\n";
    out += "% core/render PdfWriter -- dependency-free, no compression\n";

    std::vector<std::size_t> offsets(object_count + 1, 0);
    for (std::size_t number = 1; number <= object_count; ++number) {
        offsets[number] = out.size();
        out += std::to_string(number);
        out += " 0 obj\n";
        out += object_body(number);
        out += "\nendobj\n";
    }

    const std::size_t xref_offset = out.size();
    out += "xref\n0 ";
    out += std::to_string(object_count + 1);
    out += "\n0000000000 65535 f \n";
    for (std::size_t number = 1; number <= object_count; ++number) {
        std::string entry = std::to_string(offsets[number]);
        if (entry.size() < 10) {
            entry.insert(0, 10 - entry.size(), '0');
        }
        out += entry;
        out += " 00000 n \n";
    }
    out += "trailer\n<< /Size ";
    out += std::to_string(object_count + 1);
    out += " /Root 1 0 R /Info 3 0 R >>\nstartxref\n";
    out += std::to_string(xref_offset);
    out += "\n%%EOF\n";
    return out;
}

}  // namespace core::render
