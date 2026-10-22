/*
 * pdf_writer.h -- a small, dependency-free PDF 1.7 writer.
 *
 * WHY THIS EXISTS
 *   The macOS app needs to print/export a sheet or a Gantt without a third-party
 *   PDF library (no zlib, no image assets, no MuPDF/PDFium).  This writer emits
 *   the subset of PDF that a report needs and nothing else:
 *
 *     * pages (A4 landscape by default, A4 portrait, or any point size)
 *     * text in Helvetica and Helvetica-Bold (two of the standard 14 fonts, so
 *       nothing is embedded and no font file is ever read)
 *     * lines, filled/stroked rectangles, filled/stroked polygons
 *     * clipping to a page box
 *     * a complete document: %PDF-1.7 header, numbered objects, a correct
 *       cross-reference table with byte offsets, trailer with /Root and /Info,
 *       startxref and %%EOF
 *
 *   The output is BYTE-DETERMINISTIC: there is no clock, no random value and no
 *   pointer value anywhere.  The /Info /CreationDate is an explicit parameter
 *   with a fixed default, so the same drawing calls always produce the same
 *   bytes and a test can hash them.  Numbers are formatted by hand in fixed
 *   point, never through a locale-sensitive stream.
 *
 * COORDINATES (deliberate: y grows DOWN)
 *   Every coordinate in this API is in PDF points (1/72 inch) with the origin at
 *   the TOP-LEFT of the page and y growing DOWNWARD.  That is the coordinate
 *   space core::sheet and core::gantt already use (geometry from the top of the
 *   page, like Core Graphics), so a report does not have to flip anything.  The
 *   writer converts to PDF user space (origin bottom-left, y up) internally:
 *   X = x, Y = page_height - y.  A rectangle (x, y, w, h) therefore covers the
 *   page area starting at y and going DOWN `h` points.
 *
 *   `text(x, baseline_y, ...)` places the text origin (the baseline, left edge)
 *   at (x, baseline_y) in those same y-down points.
 *
 * TEXT AND NON-ASCII
 *   Input text is UTF-8.  The standard 14 fonts are used with /WinAnsiEncoding,
 *   which is a single-byte CP1252-ish encoding, so the writer decodes the UTF-8
 *   and maps each code point to one WinAnsi byte.  It never passes UTF-8 bytes
 *   through (that would render "CafÃ©", the classic mojibake).  A code point
 *   WinAnsi cannot represent is replaced with '?' and counted, never silently
 *   dropped or truncated: see winansi_encode().  Latin-1 letters, the CP1252
 *   punctuation block (en/em dash, curly quotes, ellipsis, euro, bullet, ...)
 *   all round-trip exactly.
 *
 *   Inside a PDF string, '(', ')' and '\' are escaped with a backslash and the
 *   remaining C0 controls become three-digit octal escapes.
 *
 * WHAT IS NOT HERE
 *   Images, transparency, embedded fonts, compression, annotations, outlines,
 *   tagged PDF/accessibility, encryption, hyperlinks, colour spaces beyond
 *   DeviceRGB.  All of those are out of scope for this stream; the file is
 *   valid without them.
 */

#ifndef CORE_RENDER_PDF_WRITER_H
#define CORE_RENDER_PDF_WRITER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace core::render {

/* Points are 1/72 inch.  A4 landscape is 842 x 595 pt (297 x 210 mm). */
struct PdfPageFormat {
    double width = 842.0;
    double height = 595.0;

    static PdfPageFormat a4_landscape() noexcept { return PdfPageFormat{842.0, 595.0}; }
    static PdfPageFormat a4_portrait() noexcept { return PdfPageFormat{595.0, 842.0}; }

    bool valid() const noexcept { return width > 0.0 && height > 0.0; }
    bool operator==(const PdfPageFormat& other) const noexcept {
        return width == other.width && height == other.height;
    }
};

/* DeviceRGB, each component clamped to 0..1. */
struct PdfRgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    bool operator==(const PdfRgb& other) const noexcept {
        return r == other.r && g == other.g && b == other.b;
    }
};

struct PdfPoint {
    double x = 0.0;
    double y = 0.0;
};

/* The two standard fonts this writer exposes.  Both are unembedded (the
 * standard 14 are guaranteed to be available to any conforming reader). */
enum class PdfFont : std::uint8_t { Helvetica = 0, HelveticaBold = 1 };

/* Helvetica's /BaseFont name, for diagnostics and tests. */
const char* pdf_font_name(PdfFont font) noexcept;

/*
 * Document information dictionary.  The defaults are fixed so output is
 * reproducible; a caller that wants a real creation time passes its own
 * PDF date string ("D:YYYYMMDDHHmmSSOHH'mm'" or the "Z" UTC form used here).
 */
struct PdfInfo {
    std::string title;
    std::string creator = "MS Project Decompile (core/render)";
    std::string producer = "core/render PdfWriter (dependency-free)";
    std::string creation_date = "D:20260101000000Z";
};

/*
 * Decodes UTF-8 to WinAnsi bytes for the standard fonts.
 *
 *   * ASCII 0x20..0x7E encode to themselves.
 *   * U+00A0..U+00FF encode to the same byte (Latin-1).
 *   * The CP1252 0x80..0x9F block (euro, quotes, dashes, ellipsis, ...) encodes
 *     to its CP1252 byte.
 *   * A C0 control (including newline and tab) becomes a space: this writer
 *     draws single-line text, so a control character must not corrupt the PDF
 *     string.
 *   * Anything else becomes '?' and, when `substituted` is non-null, increments
 *     it.  This is the documented substitution: no code point is dropped, no
 *     partial UTF-8 sequence is ever emitted, and the caller can report how many
 *     characters could not be represented.
 *
 * Malformed UTF-8 is treated byte by byte: a stray byte becomes one '?'.
 */
std::string winansi_encode(std::string_view utf8, std::size_t* substituted = nullptr);

class PdfWriter {
  public:
    explicit PdfWriter(PdfInfo info = {});
    PdfWriter(PdfPageFormat default_format, PdfInfo info = {});

    PdfWriter(const PdfWriter&) = delete;
    PdfWriter& operator=(const PdfWriter&) = delete;

    /* Changes /Info.  Takes effect at finish(), so it may be called at any time
     * before the document is serialised. */
    void set_info(PdfInfo info);

    /* Format used when a drawing call auto-creates the first page (and by
     * add_page's default argument). */
    void set_default_page_format(PdfPageFormat format);
    PdfPageFormat default_page_format() const noexcept { return default_format_; }

    /* Appends a page and makes it current.  Returns the 1-based page number. */
    std::size_t add_page(PdfPageFormat format);
    std::size_t add_page() { return add_page(default_format_); }

    std::size_t page_count() const noexcept { return pages_.size(); }
    /* Out-of-range returns a zero-size format rather than throwing. */
    PdfPageFormat page_format(std::size_t index) const noexcept;
    /* Format of the page drawing calls currently apply to. */
    PdfPageFormat current_page_format() const noexcept;

    /* ---- graphics state ------------------------------------------------ */
    void set_fill_rgb(PdfRgb colour);
    void set_stroke_rgb(PdfRgb colour);
    void set_line_width(double points);
    /* Dash pattern in points; an empty vector restores a solid line. */
    void set_dash(const std::vector<double>& pattern, double phase = 0.0);

    PdfRgb fill_rgb() const noexcept { return fill_; }
    PdfRgb stroke_rgb() const noexcept { return stroke_; }
    double line_width() const noexcept { return line_width_; }

    /* ---- drawing ------------------------------------------------------- */
    void line(PdfPoint from, PdfPoint to);
    void line(double x0, double y0, double x1, double y1) {
        line(PdfPoint{x0, y0}, PdfPoint{x1, y1});
    }

    /* (x, y) is the TOP-LEFT corner in y-down points; the rectangle extends
     * right and DOWN. */
    void rect(double x, double y, double w, double h, bool fill, bool stroke);

    /* Closed polygon; `fill` uses the non-zero winding rule, `stroke` draws the
     * outline.  At least three points are required (fewer draws nothing). */
    void polygon(const std::vector<PdfPoint>& points, bool fill, bool stroke);

    /* Draws one line of text with its baseline origin at (x, baseline_y).
     * Empty text draws nothing (no state is emitted). */
    void text(double x, double baseline_y, std::string_view utf8, PdfFont font, double size);

    /* Width of the text at `size` points, in points, from the standard font's
     * glyph widths.  Approximate for code points outside the ASCII range: a
     * code point without a known metric uses 556 units (the width of Helvetica's
     * digits).  This is a layout aid, not a text-shaping engine. */
    double text_width(std::string_view utf8, PdfFont font, double size) const;

    /* ---- clipping ------------------------------------------------------ */
    /*
     * Restricts drawing to the rectangle until end_clip().  Calls nest: each
     * begin_clip pushes one graphics state, each end_clip pops it.  finish()
     * closes any clip the caller left open, so an unbalanced call cannot produce
     * invalid output (it is still a bug in the caller, and the writer records it
     * in unbalanced_clips()).
     */
    void begin_clip(double x, double y, double w, double h);
    void end_clip();
    std::size_t clip_depth() const noexcept { return clip_depth_; }
    std::size_t unbalanced_clips() const noexcept { return unbalanced_clips_; }

    /* ---- output -------------------------------------------------------- */
    /*
     * Serialises the document.  `finish()` does not consume the writer: calling
     * it twice returns identical bytes, so a test can hash the output and a
     * caller can save it after inspecting it.
     */
    std::string finish() const;

    /* Writes finish() to `path`.  False and an error message on failure. */
    bool save(const std::string& path, std::string& error) const;

  private:
    struct Page {
        PdfPageFormat format;
        std::string content;
    };

    Page& current_page();                        /* auto-creates page 1 */
    void append_colour(PdfRgb colour, bool fill);
    void emit_text(double x, double baseline_y, std::string_view utf8, PdfFont font,
                   double size);
    std::string page_object_body(const PdfPageFormat& format,
                                std::size_t content_object) const;
    std::string content_object_body(const std::string& content) const;

    PdfPageFormat default_format_;
    PdfInfo info_;
    std::vector<Page> pages_;

    PdfRgb fill_{0.0, 0.0, 0.0};
    PdfRgb stroke_{0.0, 0.0, 0.0};
    double line_width_ = 1.0;
    std::vector<double> dash_pattern_{};
    double dash_phase_ = 0.0;

    std::size_t clip_depth_ = 0;
    std::size_t unbalanced_clips_ = 0;
};

/*
 * Escapes one already-WinAnsi-encoded byte string for a PDF literal string,
 * WITHOUT the surrounding parentheses.  '(' -> "\\(", ')' -> "\\)",
 * '\\' -> "\\\\", controls -> "\\ooo".  Exposed so a test can check the escaping
 * rule directly instead of scraping a content stream.
 */
std::string escape_pdf_string(std::string_view bytes);

}  // namespace core::render

#endif  // CORE_RENDER_PDF_WRITER_H
