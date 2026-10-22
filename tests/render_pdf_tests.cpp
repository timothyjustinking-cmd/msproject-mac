/*
 * render_pdf_tests.cpp -- structural validation of the PDF writer's bytes.
 *
 * This is the test that keeps the deliverable honest: it does not call any
 * writer helper to check the document.  It parses the produced bytes (header,
 * startxref -> xref, every entry's byte offset, trailer, /Root -> /Pages ->
 * /Type /Page, content streams) and only then looks at drawing operators.
 *
 * Coverage:
 *   (a) structure: header, xref offsets, /Root//Pages /Count, /Type /Page count,
 *       %%EOF last, non-ASCII only inside text strings
 *   (d) escaping: '(' ')' '\' and C0 controls, WinAnsi mapping of non-ASCII,
 *       documented substitution, y-down coordinate conversion
 *   byte determinism: same drawing calls -> identical bytes (FNV-1a)
 */

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "../core/render/pdf_writer.h"
#include "render_test.h"

namespace {

using core::render::PdfFont;
using core::render::PdfInfo;
using core::render::PdfPageFormat;
using core::render::PdfPoint;
using core::render::PdfRgb;
using core::render::PdfWriter;
using render_test::PdfDocument;
using render_test::TestContext;

void test_header_and_xref(TestContext& ctx) {
    PdfWriter writer(PdfInfo{});
    writer.add_page(PdfPageFormat::a4_landscape());
    writer.set_fill_rgb(PdfRgb{0.1, 0.2, 0.3});
    writer.text(40.0, 60.0, "Hello, PDF", PdfFont::Helvetica, 20.0);
    const std::string bytes = writer.finish();

    RCHECK(ctx, bytes.rfind("%PDF-1.7", 0) == 0);
    RCHECK(ctx, bytes.find("%%EOF") != std::string::npos);

    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }

    RCHECK(ctx, doc.header_ok);
    RCHECK(ctx, doc.version == "1.7");
    RCHECK(ctx, doc.eof_last);
    RCHECK(ctx, doc.startxref_found);
    RCHECK(ctx, doc.startxref > 0);
    RCHECK(ctx, doc.xref_at_startxref);
    RCHECK(ctx, doc.trailer_found);
    RCHECK(ctx, doc.trailer_size == doc.xref_count);
    RCHECK(ctx, doc.root_object == 1);
    RCHECK(ctx, doc.info_object == 3);
    RCHECK(ctx, doc.pages_found);
    RCHECK(ctx, doc.pages_count == 1);
    RCHECK(ctx, doc.kids.size() == 1);
    RCHECK(ctx, doc.pages.size() == 1);
    RCHECK(ctx, doc.pages[0].has_media_box);
    RCHECK(ctx, std::abs(doc.pages[0].media_width - 842.0) < 0.001);
    RCHECK(ctx, std::abs(doc.pages[0].media_height - 595.0) < 0.001);
    RCHECK(ctx, !doc.in_use.empty() && !doc.in_use[0]);

    /* Every in-use xref entry must point at "<n> 0 obj" -- checked here again on
     * the parsed offsets, independently of parse_pdf's own loop. */
    bool every_entry_lands = true;
    for (std::size_t object = 1; object < doc.offsets.size(); ++object) {
        const std::string prefix = std::to_string(object) + " 0 obj";
        if (doc.offsets[object] < 0 ||
            bytes.compare(static_cast<std::size_t>(doc.offsets[object]), prefix.size(), prefix) !=
                0) {
            every_entry_lands = false;
        }
    }
    RCHECK(ctx, every_entry_lands);

    /* The table itself counts one more entry than there are objects (entry 0 is
     * the free head). */
    RCHECK(ctx, doc.xref_count == 8); /* 5 fixed objects + 1 page + 1 stream, plus entry 0 */
    RCHECK(ctx, static_cast<long long>(render_test::count_occurrences(bytes, " 0 obj")) ==
                   doc.xref_count - 1);
    RCHECK(ctx, render_test::count_occurrences(bytes, "%%EOF") == 1);
    RCHECK(ctx, render_test::count_occurrences(bytes, "/Type /Page ") == 1);

    RCHECK(ctx, doc.bytes_outside_strings_clean);
    RCHECK(ctx, !doc.raw_utf8_in_strings);

    bool has_hello = false;
    for (const std::string& literal : doc.literals) {
        if (literal == "Hello, PDF") {
            has_hello = true;
        }
    }
    RCHECK(ctx, has_hello);
    RCHECK(ctx, render_test::contains(doc.pages[0].content, "/F1 20 Tf"));
}

void test_two_pages_and_page_formats(TestContext& ctx) {
    PdfWriter writer;
    writer.add_page(PdfPageFormat::a4_landscape());
    writer.text(20.0, 20.0, "page one", PdfFont::Helvetica, 10.0);
    writer.add_page(PdfPageFormat::a4_portrait());
    writer.text(20.0, 20.0, "page two", PdfFont::HelveticaBold, 10.0);

    const std::string bytes = writer.finish();
    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }
    RCHECK(ctx, doc.pages_count == 2);
    RCHECK(ctx, doc.pages.size() == 2);
    RCHECK(ctx, std::abs(doc.pages[0].media_width - 842.0) < 0.001);
    RCHECK(ctx, std::abs(doc.pages[0].media_height - 595.0) < 0.001);
    RCHECK(ctx, std::abs(doc.pages[1].media_width - 595.0) < 0.001);
    RCHECK(ctx, std::abs(doc.pages[1].media_height - 842.0) < 0.001);
    RCHECK(ctx, doc.pages[0].contents_object != doc.pages[1].contents_object);
    RCHECK(ctx, render_test::contains(doc.pages[0].content, "page one"));
    RCHECK(ctx, render_test::contains(doc.pages[1].content, "page two"));
    RCHECK(ctx, render_test::contains(doc.pages[1].content, "/F2 10 Tf"));

    /* Each page has its own /MediaBox, so mixed formats are legal. */
    RCHECK(ctx, doc.pages[0].object != doc.pages[1].object);
}

void test_coordinate_conversion(TestContext& ctx) {
    PdfWriter writer(PdfPageFormat{200.0, 200.0});
    writer.add_page();
    writer.set_stroke_rgb(PdfRgb{0.0, 0.0, 0.0});
    writer.rect(10.0, 20.0, 100.0, 50.0, false, true);
    writer.line(0.0, 0.0, 200.0, 200.0);
    writer.text(40.0, 100.0, "baseline", PdfFont::Helvetica, 8.0);
    const std::string bytes = writer.finish();

    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }
    const std::string& content = doc.pages[0].content;
    /* y-down (10,20,100,50) on a 200-high page is PDF y = 200-(20+50) = 130. */
    RCHECK(ctx, render_test::contains(content, "10 130 100 50 re"));
    /* The line's second endpoint is the bottom-left of the page. */
    RCHECK(ctx, render_test::contains(content, "200 0 l"));
    /* Text baseline y=100 becomes PDF y=100 on a 200-high page. */
    RCHECK(ctx, render_test::contains(content, "1 0 0 1 40 100 Tm"));
}

void test_escaping(TestContext& ctx) {
    RCHECK(ctx, core::render::escape_pdf_string("A(B)C\\D") == "A\\(B\\)C\\\\D");
    RCHECK(ctx, core::render::escape_pdf_string("plain") == "plain");
    RCHECK(ctx, core::render::escape_pdf_string(std::string("\x01", 1)) == "\\001");
    RCHECK(ctx, core::render::escape_pdf_string(std::string("\t", 1)) == "\\t");

    const std::string tricky = "Name (with) parens \\ and \x5C backslash";
    PdfWriter writer;
    writer.add_page();
    writer.text(10.0, 20.0, tricky, PdfFont::Helvetica, 9.0);
    const std::string bytes = writer.finish();

    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }
    bool round_trip = false;
    for (const std::string& literal : doc.literals) {
        if (literal == tricky) {
            round_trip = true;
        }
    }
    RCHECK(ctx, round_trip);
    /* The literal in the file must be escaped, not raw. */
    RCHECK(ctx, render_test::contains(doc.pages[0].content, "\\(with\\)"));
    RCHECK(ctx, render_test::contains(doc.pages[0].content, "\\\\"));
    RCHECK(ctx, doc.bytes_outside_strings_clean);
}

void test_winansi_and_substitution(TestContext& ctx) {
    using core::render::winansi_encode;

    /* Latin-1 direct bytes. */
    RCHECK(ctx, winansi_encode("Caf\xC3\xA9") == "Caf\xE9");
    RCHECK(ctx, winansi_encode("\xC3\x9C" "ber") == "\xDC" "ber");
    /* CP1252 punctuation. */
    RCHECK(ctx, winansi_encode("\xE2\x80\x93") == "\x96"); /* en dash */
    RCHECK(ctx, winansi_encode("\xE2\x80\x94") == "\x97"); /* em dash */
    RCHECK(ctx, winansi_encode("\xE2\x80\x9C") == "\x93"); /* left double quote */
    RCHECK(ctx, winansi_encode("\xE2\x82\xAC") == "\x80"); /* euro */
    RCHECK(ctx, winansi_encode("\xE2\x80\xA6") == "\x85"); /* ellipsis */
    /* Not representable: documented substitution, counted. */
    std::size_t substituted = 0;
    RCHECK(ctx, winansi_encode("a\xE2\x86\x92" "b", &substituted) == "a?b");
    RCHECK(ctx, substituted == 1);
    /* Malformed UTF-8: one replacement per stray byte, never a crash. */
    substituted = 7;
    RCHECK(ctx, winansi_encode("\xFF", &substituted) == "?");
    RCHECK(ctx, substituted == 1);
    /* Control characters become spaces in single-line text. */
    RCHECK(ctx, winansi_encode("a\nb\tc") == "a b c");

    const std::string text = "Caf\xC3\xA9 \xE2\x80\x93 50 \xE2\x82\xAC";
    PdfWriter writer;
    writer.add_page();
    writer.text(10.0, 20.0, text, PdfFont::Helvetica, 9.0);
    const std::string bytes = writer.finish();

    /* The UTF-8 bytes must not appear anywhere (that would be mojibake); the
     * WinAnsi bytes must. */
    RCHECK(ctx, bytes.find("\xC3\xA9") == std::string::npos);
    RCHECK(ctx, bytes.find("\xE2\x80\x93") == std::string::npos);
    RCHECK(ctx, render_test::contains(bytes, "Caf\xE9"));
    RCHECK(ctx, render_test::contains(bytes, "\x96"));
    RCHECK(ctx, render_test::contains(bytes, "50 \x80"));

    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }
    bool exact = false;
    for (const std::string& literal : doc.literals) {
        if (literal == "Caf\xE9 \x96 50 \x80") {
            exact = true;
        }
    }
    RCHECK(ctx, exact);
}

void test_text_width(TestContext& ctx) {
    PdfWriter writer;
    const double hello = writer.text_width("Hello", PdfFont::Helvetica, 10.0);
    const double hello_bold = writer.text_width("Hello", PdfFont::HelveticaBold, 10.0);
    RCHECK(ctx, hello > 0.0);
    RCHECK(ctx, hello_bold > hello);
    RCHECK(ctx, writer.text_width("", PdfFont::Helvetica, 10.0) == 0.0);
    RCHECK(ctx, std::abs(writer.text_width("Hello", PdfFont::Helvetica, 20.0) - 2.0 * hello) <
                   0.001);
    RCHECK(ctx, writer.text_width("ii", PdfFont::Helvetica, 10.0) < hello);
}

void test_clipping_and_polygon(TestContext& ctx) {
    PdfWriter writer(PdfPageFormat{200.0, 200.0});
    writer.add_page();
    writer.begin_clip(10.0, 10.0, 100.0, 100.0);
    RCHECK(ctx, writer.clip_depth() == 1);
    writer.end_clip();
    RCHECK(ctx, writer.clip_depth() == 0);

    writer.polygon({PdfPoint{100.0, 20.0}, PdfPoint{110.0, 30.0}, PdfPoint{90.0, 30.0}}, true,
                   false);
    writer.text(0.0, 0.0, "clipped", PdfFont::Helvetica, 8.0);

    /* A stray end_clip is counted, not a crash. */
    writer.end_clip();
    RCHECK(ctx, writer.unbalanced_clips() == 1);

    const std::string bytes = writer.finish();
    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }
    RCHECK(ctx, render_test::contains(doc.pages[0].content, "10 90 100 100 re"));
    RCHECK(ctx, render_test::contains(doc.pages[0].content, "W n"));
    RCHECK(ctx, render_test::contains(doc.pages[0].content, "100 180 m")); /* 200-20 */
}

void test_open_clip_is_closed(TestContext& ctx) {
    PdfWriter writer(PdfPageFormat{200.0, 200.0});
    writer.add_page();
    writer.begin_clip(10.0, 10.0, 50.0, 50.0);
    /* Deliberately no end_clip: finish() must still balance the graphics state. */
    RCHECK(ctx, writer.clip_depth() == 1);
    const std::string bytes = writer.finish();
    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }
    /* One q for the clip, one Q from finish. */
    RCHECK(ctx, render_test::count_occurrences(doc.pages[0].content, "q\n") ==
                   render_test::count_occurrences(doc.pages[0].content, "Q\n"));
}

void test_empty_writer(TestContext& ctx) {
    PdfWriter writer;
    RCHECK(ctx, writer.page_count() == 0);
    const std::string bytes = writer.finish();
    std::string error;
    PdfDocument doc;
    const bool parsed = render_test::parse_pdf(bytes, doc, error);
    ctx.check(parsed, error.empty() ? "parse_pdf" : error.c_str(), __LINE__);
    if (!parsed) {
        return;
    }
    /* A PDF needs at least one page; finish() supplies a blank one. */
    RCHECK(ctx, doc.pages_count == 1);
    RCHECK(ctx, doc.pages[0].content.find("BT") == std::string::npos);
}

void test_byte_determinism(TestContext& ctx) {
    auto build = []() {
        PdfWriter writer(PdfInfo{"Deterministic", "creator", "producer", "D:20260101000000Z"});
        writer.add_page();
        writer.text(40.0, 60.0, "Same bytes every time", PdfFont::HelveticaBold, 12.0);
        writer.rect(40.0, 70.0, 200.0, 30.0, true, true);
        writer.add_page(PdfPageFormat::a4_portrait());
        writer.line(0.0, 0.0, 100.5, 100.25);
        return writer.finish();
    };
    const std::string first = build();
    const std::string second = build();
    RCHECK(ctx, first == second);
    const std::string hash = render_test::fnv1a_hex(first);
    RCHECK(ctx, hash == render_test::fnv1a_hex(second));
    RCHECK(ctx, first.size() > 300);
    /* No clock: the only date is the parameterised /CreationDate. */
    RCHECK(ctx, render_test::contains(first, "/CreationDate (D:20260101000000Z)"));
}

}  // namespace

int run_pdf_tests(TestContext& ctx) {
    ctx.suite = "render_pdf";
    test_header_and_xref(ctx);
    test_two_pages_and_page_formats(ctx);
    test_coordinate_conversion(ctx);
    test_escaping(ctx);
    test_winansi_and_substitution(ctx);
    test_text_width(ctx);
    test_clipping_and_polygon(ctx);
    test_open_clip_is_closed(ctx);
    test_empty_writer(ctx);
    test_byte_determinism(ctx);
    return ctx.failures;
}
