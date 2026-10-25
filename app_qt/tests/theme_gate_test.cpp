// app_qt/tests/theme_gate_test.cpp -- wave 26 (W26-6.2), extended by W29-5.1.
//
// THE TARGET (operator decision 10 / Q14, docs/ui-parity-spec.md)
//   Office's Dark Gray and Black themes darken ONLY the chrome.  The sheet and
//   the chart keep the file's colours, pixel for pixel.  The theme switch is
//   applied live (no restart): the Account/Options chooser calls
//   set_theme_choice() and every shell-owned surface repaints.
//   WAVE 29 (W29-5.1) closes the two gaps wave 26 recorded: the sheet's caption
//   band and frozen gutter, and the chart's timescale band, are chrome and now
//   follow the theme; the ribbon's one-ink glyphs take a per-theme ink.  The
//   sheet's CELLS and the chart's BODY still keep the file's colours.
//
// WHAT THIS GATE DOES
//   1. Opens a committed fixture, clears the selection and renders the window
//      once per theme: Colorful (the parity reference), Dark Gray and Black.
//   2. Reads the COMMITTED Project measurements
//      (tests/data/reference/w26-theme.measures.json): the light chrome is the
//      parity measurement, the two dark palettes were sampled from the local
//      audit frames of Project 2021 in those themes.  It takes the dominant
//      rendered colour of the LIVE ribbon and status bar and requires an exact
//      match with those measured values, and it checks the measured dark values
//      ARE the capture's sampled tab/group-row/status colours.
//   3. Asserts the chrome changed and got darker from light -> Dark Gray ->
//      Black, and that the sheet viewport is EXACTLY unchanged between all
//      three themes (tolerance 0: QImage equality).
//   4. W29-5.1: reads tests/data/reference/w29-dark-header.measures.json and
//      requires the live caption band, frozen gutter, corner and chart
//      timescale band to be the measured fills in every theme, the caption ink
//      to be present and legible, the live ribbon glyph to be drawn in the
//      theme's one ink, and the chart BODY below the band to be EXACTLY
//      unchanged between all three themes.
//
// Usage: qt_theme_gate <fixture.mpp> <w26-theme.measures.json>
//                     <w29-dark-header.measures.json> [out-dir]
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QStatusBar>
#include <QString>
#include <QTableView>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "gantt_canvas.h"
#include "gate_util.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "ribbon_glyphs.h"
#include "shell_style.h"
#include "theme.h"

namespace {

QImage grab_image(QWidget* widget) {
    if (widget == nullptr) {
        return QImage();
    }
    return widget->grab().toImage().convertToFormat(QImage::Format_ARGB32);
}

// The most frequent opaque pixel colour: the shell paints each chrome surface
// with one flat fill, so the fill is the dominant colour and a stray label or
// icon cannot pass for it.
QColor dominant_colour(const QImage& image, int* count = nullptr) {
    QHash<QRgb, int> counts;
    int best = 0;
    QRgb best_rgb = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (qAlpha(rgb) == 0) {
                continue;
            }
            const int now = counts.value(rgb, 0) + 1;
            counts.insert(rgb, now);
            if (now > best) {
                best = now;
                best_rgb = rgb;
            }
        }
    }
    if (count != nullptr) {
        *count = best;
    }
    return QColor(qRed(best_rgb), qGreen(best_rgb), qBlue(best_rgb));
}

QString hex(const QColor& colour) {
    return colour.name(QColor::HexRgb).toUpper();
}

QColor measured(const QJsonObject& chrome, const QString& theme, const QString& surface) {
    return QColor(chrome.value(theme).toObject().value(surface).toString());
}

int luminance(const QColor& colour) {
    return (colour.red() * 299 + colour.green() * 587 + colour.blue() * 114) / 1000;
}

// W29-5.1: the header band measurements (tests/data/reference/
// w29-dark-header.measures.json), keyed by theme name then colour name.
QColor header_value(const QJsonObject& header, const QString& theme, const QString& key) {
    return QColor(header.value(theme).toObject().value(key).toString());
}

QColor sample_value(const QJsonObject& samples, const QString& theme, const QString& key) {
    return QColor(samples.value(theme).toObject().value(key).toString());
}

// How many pixels are EXACTLY one colour.  The chrome surfaces are flat fills,
// so this is the surface's area (no antialiasing at a solid fill's interior).
int exact_count(const QImage& image, const QColor& colour) {
    const QRgb want = colour.rgba();
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) == want) {
                ++count;
            }
        }
    }
    return count;
}

// Pixels within `tolerance` of a colour on every channel: glyph cores survive
// a colour change against a contrasting fill even when the outermost
// antialiased edge is a blend toward the fill.
int near_count(const QImage& image, const QColor& colour, int tolerance) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb rgb = image.pixel(x, y);
            if (qAlpha(rgb) != 255) {
                continue;
            }
            if (std::abs(qRed(rgb) - colour.red()) <= tolerance &&
                std::abs(qGreen(rgb) - colour.green()) <= tolerance &&
                std::abs(qBlue(rgb) - colour.blue()) <= tolerance) {
                ++count;
            }
        }
    }
    return count;
}

// WCAG 2.x relative luminance and contrast ratio: the legibility check for the
// caption and glyph inks against their measured surfaces.
double relative_luminance(const QColor& colour) {
    auto channel = [](int value) {
        const double c = static_cast<double>(value) / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(colour.red()) + 0.7152 * channel(colour.green()) +
           0.0722 * channel(colour.blue());
}

double contrast_ratio(const QColor& a, const QColor& b) {
    const double la = relative_luminance(a);
    const double lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QImage band(const QImage& image, int top, int height) {
    if (image.isNull() || top < 0 || top >= image.height()) {
        return QImage();
    }
    return image.copy(0, top, image.width(), std::min(height, image.height() - top));
}

// How many pixels two same-sized renders differ in (-1 when the sizes differ):
// the gate's diagnosis when a "tolerance 0" surface check fails.
int diff_pixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) {
        return -1;
    }
    int count = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y)) {
                ++count;
            }
        }
    }
    return count;
}

struct Render {
    QImage ribbon;
    QImage status;
    QImage sheet;
    QImage chart;
    QImage sheet_header;
    QImage row_header;
    QImage view;  // the whole sheet view: its (0,0) is the corner widget
    QColor ribbon_colour;
    QColor status_colour;
    int ribbon_count = 0;
    int status_count = 0;
    // W29-5.1: one live icon-bearing ribbon command, captured while this theme
    // is active, so the gate can prove its glyph is drawn in the theme's ink.
    QString icon_key;
    int icon_pixels = 0;
    QImage icon;
};

Render render_theme(msqt::MainWindow& window, msqt::ThemeChoice choice) {
    Render render;
    window.set_theme_choice(choice, false);
    QApplication::processEvents();
    QApplication::processEvents();
    render.ribbon = grab_image(window.ribbon());
    render.status = grab_image(window.statusBar());
    render.sheet = grab_image(window.sheet_view()->viewport());
    render.chart = grab_image(window.gantt_view());
    render.sheet_header = grab_image(window.sheet_view()->horizontalHeader());
    render.row_header = grab_image(window.sheet_view()->verticalHeader());
    render.view = grab_image(window.sheet_view());
    render.ribbon_colour = dominant_colour(render.ribbon, &render.ribbon_count);
    render.status_colour = dominant_colour(render.status, &render.status_count);
    // The first command on the active tab that carries a glyph key, in ribbon
    // order.  Its live QIcon is compared with the theme-ink render below.
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon != nullptr) {
        const int groups = ribbon->group_titles().size();
        for (int group = 0; group < groups && render.icon_key.isEmpty(); ++group) {
            const int controls = ribbon->group_control_count(group);
            for (int index = 0; index < controls; ++index) {
                const QString key = ribbon->group_control_icon_key(group, index);
                auto* button = qobject_cast<QToolButton*>(ribbon->group_control(group, index));
                if (key.isEmpty() || button == nullptr || button->icon().isNull()) {
                    continue;
                }
                render.icon_key = key;
                render.icon_pixels = button->iconSize().width() >= 24 ? 32 : 16;
                render.icon = button->icon()
                                  .pixmap(QSize(render.icon_pixels, render.icon_pixels))
                                  .toImage()
                                  .convertToFormat(QImage::Format_ARGB32);
                break;
            }
        }
    }
    return render;
}

msqt::Theme theme_of(const char* name) {
    if (std::string(name) == "dark_gray") {
        return msqt::Theme::dark;
    }
    if (std::string(name) == "black") {
        return msqt::Theme::black;
    }
    return msqt::Theme::light;
}

// The wave-26 chrome JSON names the light theme "colorful" (the chooser's
// label); the W29 header JSON names the palette itself, "light".
QString header_theme_name(const QString& chrome_name) {
    return chrome_name == QStringLiteral("colorful") ? QStringLiteral("light") : chrome_name;
}

// The committed measurement's 15 header-band fields, in palette order, so the
// compiled ChromePalette can be compared with the JSON field by field.
struct PaletteField {
    const char* key;
    QColor msqt::ChromePalette::* member;
};

const PaletteField kHeaderPaletteFields[] = {
    {"sheet_header_background", &msqt::ChromePalette::sheet_header_background},
    {"sheet_header_text", &msqt::ChromePalette::sheet_header_text},
    {"sheet_header_text_muted", &msqt::ChromePalette::sheet_header_text_muted},
    {"sheet_header_separator", &msqt::ChromePalette::sheet_header_separator},
    {"sheet_header_arrow", &msqt::ChromePalette::sheet_header_arrow},
    {"sheet_header_grip", &msqt::ChromePalette::sheet_header_grip},
    {"row_header_background", &msqt::ChromePalette::row_header_background},
    {"row_header_text", &msqt::ChromePalette::row_header_text},
    {"chart_header_background", &msqt::ChromePalette::chart_header_background},
    {"chart_header_background_alt", &msqt::ChromePalette::chart_header_background_alt},
    {"chart_header_weekend", &msqt::ChromePalette::chart_header_weekend},
    {"chart_header_text", &msqt::ChromePalette::chart_header_text},
    {"chart_header_rule", &msqt::ChromePalette::chart_header_rule},
    {"chart_header_rule_strong", &msqt::ChromePalette::chart_header_rule_strong},
    {"ribbon_ink", &msqt::ChromePalette::ribbon_ink},
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: qt_theme_gate <fixture.mpp> <w26-theme.measures.json> "
                     "<w29-dark-header.measures.json> [out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString measures_path = QString::fromLocal8Bit(argv[2]);
    const QString header_path = QString::fromLocal8Bit(argv[3]);
    const QString out_dir = argc > 4 ? QString::fromLocal8Bit(argv[4]) : QString();

    QFile file(measures_path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "error: cannot read %s\n",
                     measures_path.toLocal8Bit().constData());
        return 2;
    }
    const QJsonObject measures = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject chrome =
        measures.value(QStringLiteral("chrome")).toObject();
    const QJsonObject samples =
        measures.value(QStringLiteral("capture_samples")).toObject();
    gate::check(!chrome.isEmpty(), "the committed theme measurements load");
    gate::check(!samples.isEmpty(), "the capture samples load");

    QFile header_file(header_path);
    if (!header_file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "error: cannot read %s\n",
                     header_path.toLocal8Bit().constData());
        return 2;
    }
    const QJsonObject header_doc =
        QJsonDocument::fromJson(header_file.readAll()).object();
    const QJsonObject header = header_doc.value(QStringLiteral("header")).toObject();
    const QJsonObject header_samples =
        header_doc.value(QStringLiteral("capture_samples")).toObject();
    gate::check(!header.isEmpty() && !header_samples.isEmpty(),
                "the committed header-band measurements load");

    // The measured dark palettes must BE the capture's sampled surfaces, not the
    // port's own constants: group row -> the ribbon surface, status -> the bar.
    for (const char* theme_name : {"dark_gray", "black"}) {
        const QString name = QString::fromLatin1(theme_name);
        const QJsonObject sample = samples.value(name).toObject();
        gate::check(hex(measured(chrome, name, QStringLiteral("ribbon"))) ==
                        sample.value(QStringLiteral("group_row")).toString().toUpper(),
                    "the measured dark ribbon surface is the capture's group row");
        gate::check(hex(measured(chrome, name, QStringLiteral("status"))) ==
                        sample.value(QStringLiteral("status")).toString().toUpper(),
                    "the measured dark status bar is the capture's status band");
    }

    // --- 1b. the compiled header palette is the committed measurement -------
    // Field by field, so a value edited in theme.cpp alone (or in the JSON
    // alone) fails here instead of passing a dominant-colour sample.
    for (const char* theme_name : {"light", "dark_gray", "black"}) {
        const QString name = QString::fromLatin1(theme_name);
        const msqt::ChromePalette palette = msqt::chrome_palette(theme_of(theme_name));
        QString mismatch;
        for (const PaletteField& field : kHeaderPaletteFields) {
            const QColor want = header_value(header, name, QString::fromLatin1(field.key));
            if (!want.isValid() || (palette.*(field.member)) != want) {
                mismatch += QStringLiteral("%1(compiled %2, measured %3) ")
                                .arg(QString::fromLatin1(field.key),
                                     (palette.*(field.member)).name(QColor::HexRgb),
                                     want.name(QColor::HexRgb));
            }
        }
        if (!mismatch.isEmpty()) {
            std::fprintf(stderr, "note: %s palette mismatch: %s\n", theme_name,
                         mismatch.toLatin1().constData());
        }
        gate::check(mismatch.isEmpty(),
                    "the compiled header palette is the committed measurement");
    }

    // The dark fills and rules came from the audit frames, so they must be the
    // frames' sampled colours, not the port's own invention.
    const struct {
        const char* field;
        const char* sampled;
    } from_capture[] = {
        {"sheet_header_background", "sheet_header_fill"},
        {"row_header_background", "row_gutter_fill"},
        {"row_header_background", "row_gutter_corner_fill"},
        {"chart_header_background", "chart_header_fill"},
        {"sheet_header_separator", "sheet_separator_darkest"},
        {"chart_header_rule", "chart_rule"},
        {"chart_header_rule_strong", "chart_rule_strong"},
    };
    for (const char* theme_name : {"dark_gray", "black"}) {
        const QString name = QString::fromLatin1(theme_name);
        for (const auto& pair : from_capture) {
            gate::check(header_value(header, name, QString::fromLatin1(pair.field)) ==
                            sample_value(header_samples, name,
                                         QString::fromLatin1(pair.sampled)),
                        "the dark header palette value is the capture's sample");
        }
    }

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture loads");
    window.show();
    QApplication::processEvents();
    // A render is a structural snapshot; no selection may tint a cell or bar.
    window.clear_render_selection();
    QApplication::processEvents();

    const Render light = render_theme(window, msqt::ThemeChoice::colorful);
    const Render dark_gray = render_theme(window, msqt::ThemeChoice::dark_gray);
    const Render black = render_theme(window, msqt::ThemeChoice::black);

    // --- 1. the chrome palette matches Project's measured values ------------
    const struct {
        const char* name;
        const Render* render;
    } themes[3] = {
        {"colorful", &light},
        {"dark_gray", &dark_gray},
        {"black", &black},
    };
    for (const auto& theme : themes) {
        const QString name = QString::fromLatin1(theme.name);
        const QColor want_ribbon = measured(chrome, name, QStringLiteral("ribbon"));
        const QColor want_status = measured(chrome, name, QStringLiteral("status"));
        gate::check(theme.render->ribbon_colour == want_ribbon,
                    "the rendered ribbon surface is the measured chrome colour");
        gate::check(theme.render->status_colour == want_status,
                    "the rendered status bar is the measured chrome colour");
        // A flat surface, not one stray pixel: at least 30 % of the widget.
        const int ribbon_pixels = theme.render->ribbon.width() *
                                  theme.render->ribbon.height();
        const int status_pixels = theme.render->status.width() *
                                  theme.render->status.height();
        gate::check(theme.render->ribbon_count * 10 >= ribbon_pixels * 3,
                    "the measured ribbon colour is the surface's dominant fill");
        gate::check(theme.render->status_count * 10 >= status_pixels * 3,
                    "the measured status colour is the bar's dominant fill");
        std::fprintf(stderr, "note: %s chrome ribbon %s (%d px), status %s (%d px)\n",
                     theme.name, hex(theme.render->ribbon_colour).toLatin1().constData(),
                     theme.render->ribbon_count,
                     hex(theme.render->status_colour).toLatin1().constData(),
                     theme.render->status_count);
    }

    // --- 2. the chrome changed and got darker -------------------------------
    gate::check(light.ribbon_colour != dark_gray.ribbon_colour,
                "Dark Gray changes the ribbon chrome");
    gate::check(dark_gray.ribbon_colour != black.ribbon_colour,
                "Black is darker than Dark Gray on the ribbon");
    gate::check(light.status_colour != dark_gray.status_colour,
                "Dark Gray changes the status chrome");
    gate::check(dark_gray.status_colour != black.status_colour,
                "Black is darker than Dark Gray on the status bar");
    gate::check(luminance(dark_gray.ribbon_colour) < luminance(light.ribbon_colour),
                "Dark Gray darkens the ribbon");
    gate::check(luminance(black.ribbon_colour) < luminance(dark_gray.ribbon_colour),
                "Black darkens the ribbon further");
    gate::check(luminance(dark_gray.status_colour) < luminance(light.status_colour),
                "Dark Gray darkens the status bar");
    gate::check(luminance(black.status_colour) < luminance(dark_gray.status_colour),
                "Black darkens the status bar further");
    // The live palette followed the choice, not just the painted widgets.
    gate::check(window.theme() == msqt::Theme::black,
                "the live theme is the last choice (Black)");

    // --- 3. the sheet cells and chart body are EXACTLY unchanged -----------
    // W29-5.1 narrows wave 26's "the whole chart is unchanged": the timescale
    // band is chrome and must follow the theme; everything below it keeps the
    // file's colours.
    gate::check(!light.sheet.isNull() && !light.chart.isNull(),
                "the sheet and chart rendered");
    gate::check(light.sheet == dark_gray.sheet,
                "Dark Gray leaves every sheet pixel unchanged (tolerance 0)");
    gate::check(light.sheet == black.sheet,
                "Black leaves every sheet pixel unchanged (tolerance 0)");
    const msqt::GanttHeaderInfo chart_header =
        window.gantt_view()->header_info(window.gantt_view()->width());
    gate::check(chart_header.header_height == msqt::kGanttHeaderHeight,
                "the chart's timescale band is the measured 42 px");
    gate::check(chart_header.tier_height > 0, "the timescale band has tiers");
    const int header_height = chart_header.header_height;
    const auto chart_body = [header_height](const QImage& image) {
        return band(image, header_height, image.height() - header_height);
    };
    gate::check(chart_body(light.chart) == chart_body(dark_gray.chart),
                "Dark Gray leaves every chart-body pixel unchanged (tolerance 0)");
    gate::check(chart_body(light.chart) == chart_body(black.chart),
                "Black leaves every chart-body pixel unchanged (tolerance 0)");
    std::fprintf(stderr,
                 "note: chart body differs light->dark_gray in %d px, light->black in %d px\n",
                 diff_pixels(chart_body(light.chart), chart_body(dark_gray.chart)),
                 diff_pixels(chart_body(light.chart), chart_body(black.chart)));
    gate::check(band(light.chart, 0, header_height) !=
                    band(dark_gray.chart, 0, header_height),
                "Dark Gray paints the measured timescale band");
    gate::check(band(dark_gray.chart, 0, header_height) !=
                    band(black.chart, 0, header_height),
                "Black paints its own measured timescale band");
    gate::check(light.ribbon != dark_gray.ribbon,
                "the chrome render itself changed, not just a sampled pixel");

    // --- 4. the header bands are chrome, and their fills are measured ------
    gate::check(!light.sheet_header.isNull() && !light.row_header.isNull() &&
                    !light.view.isNull(),
                "the caption band, gutter and corner rendered");
    gate::check(!light.icon_key.isEmpty() && light.icon_key == dark_gray.icon_key &&
                    light.icon_key == black.icon_key,
                "the gate captured the same live ribbon command in every theme");
    gate::check(msqt::shell_ribbon_glyph_known(light.icon_key),
                "the captured command has its own wave-26 glyph, not a placeholder");
    // The timescale label ink is the FILE's, so Project (and the port) leave it
    // dark even on Black's dark band: the ink is not a per-theme value.
    gate::check(header_value(header, QStringLiteral("light"),
                             QStringLiteral("chart_header_text")) ==
                        header_value(header, QStringLiteral("dark_gray"),
                                     QStringLiteral("chart_header_text")) &&
                    header_value(header, QStringLiteral("light"),
                                 QStringLiteral("chart_header_text")) ==
                        header_value(header, QStringLiteral("black"),
                                     QStringLiteral("chart_header_text")),
                "the timescale label ink stays the file's colour under every theme");
    gate::check(header_value(header, QStringLiteral("light"),
                             QStringLiteral("chart_header_weekend")) !=
                    header_value(header, QStringLiteral("light"),
                                 QStringLiteral("chart_header_background_alt")),
                "light keeps the measured weekend tint in the timescale band");
    for (const char* theme_name : {"dark_gray", "black"}) {
        const QString name = QString::fromLatin1(theme_name);
        gate::check(header_value(header, name, QStringLiteral("chart_header_weekend")) ==
                        header_value(header, name, QStringLiteral("chart_header_background")),
                    "the dark timescale band has no distinguishable weekend tint");
    }

    for (const auto& theme : themes) {
        const QString chrome_name = QString::fromLatin1(theme.name);
        const QString name = header_theme_name(chrome_name);
        const Render& render = *theme.render;

        // The caption band's fill is the measured header colour and the band's
        // dominant pixel, not one stray sample.
        int caption_pixels = 0;
        const QColor caption_fill = dominant_colour(render.sheet_header, &caption_pixels);
        const QColor want_caption =
            header_value(header, name, QStringLiteral("sheet_header_background"));
        gate::check(caption_fill == want_caption,
                    "the caption band fill is the measured header colour");
        gate::check(caption_pixels * 2 >=
                        render.sheet_header.width() * render.sheet_header.height(),
                    "the measured caption fill is the band's dominant colour");
        std::fprintf(stderr, "note: %s caption %dx%d dominant %s (%d px), want %s\n",
                     theme.name, render.sheet_header.width(),
                     render.sheet_header.height(),
                     hex(caption_fill).toLatin1().constData(), caption_pixels,
                     hex(want_caption).toLatin1().constData());

        // The frozen gutter and the corner above it share one measured fill.
        int gutter_pixels = 0;
        const QColor gutter_fill = dominant_colour(render.row_header, &gutter_pixels);
        const QColor want_gutter =
            header_value(header, name, QStringLiteral("row_header_background"));
        gate::check(gutter_fill == want_gutter,
                    "the frozen gutter fill is the measured header colour");
        gate::check(gutter_pixels * 2 >=
                        render.row_header.width() * render.row_header.height(),
                    "the measured gutter fill is the gutter's dominant colour");
        gate::check(render.view.pixelColor(msqt::kGutterWidth / 2, msqt::kSheetHeaderHeight / 2) ==
                        want_gutter,
                    "the band's corner is the gutter fill");
        std::fprintf(stderr, "note: %s gutter %dx%d dominant %s (%d px), want %s; corner %s\n",
                     theme.name, render.row_header.width(), render.row_header.height(),
                     hex(gutter_fill).toLatin1().constData(), gutter_pixels,
                     hex(want_gutter).toLatin1().constData(),
                     hex(render.view.pixelColor(msqt::kGutterWidth / 2,
                                               msqt::kSheetHeaderHeight / 2))
                         .toLatin1()
                         .constData());

        // The chart's top tier and its lower tier(s) are each one flat fill.
        const QImage top_tier = band(render.chart, 0, chart_header.tier_height);
        int top_pixels = 0;
        gate::check(dominant_colour(top_tier, &top_pixels) ==
                        header_value(header, name,
                                     QStringLiteral("chart_header_background")),
                    "the chart's top timescale tier is the measured fill");
        gate::check(top_pixels * 2 >= top_tier.width() * top_tier.height(),
                    "the measured top-tier fill is the tier's dominant colour");
        const QImage lower_tiers = band(render.chart, chart_header.tier_height,
                                        header_height - chart_header.tier_height);
        int lower_pixels = 0;
        gate::check(!lower_tiers.isNull() &&
                        dominant_colour(lower_tiers, &lower_pixels) ==
                            header_value(header, name,
                                         QStringLiteral("chart_header_background_alt")),
                    "the chart's lower timescale tiers are the measured fill");
        gate::check(lower_pixels * 2 >= lower_tiers.width() * lower_tiers.height(),
                    "the measured lower-tier fill is the tier's dominant colour");
        std::fprintf(stderr,
                     "note: %s tier0 %s (%d px), want %s; lower %s (%d px), want %s\n",
                     theme.name,
                     hex(dominant_colour(top_tier)).toLatin1().constData(), top_pixels,
                     hex(header_value(header, name,
                                      QStringLiteral("chart_header_background")))
                         .toLatin1()
                         .constData(),
                     hex(dominant_colour(lower_tiers)).toLatin1().constData(),
                     lower_pixels,
                     hex(header_value(header, name,
                                      QStringLiteral("chart_header_background_alt")))
                         .toLatin1()
                         .constData());

        // Inks: present in the render, and legible on their measured fill.
        const QColor caption_ink =
            header_value(header, name, QStringLiteral("sheet_header_text"));
        gate::check(near_count(render.sheet_header, caption_ink, 40) >= 20,
                    "the caption band draws its captions in the theme's ink");
        gate::check(contrast_ratio(caption_ink, want_caption) >= 4.5,
                    "the caption ink is legible on its measured fill");
        const QColor row_ink = header_value(header, name, QStringLiteral("row_header_text"));
        gate::check(near_count(render.row_header, row_ink, 40) >= 8,
                    "the gutter draws its row numbers in the theme's ink");
        gate::check(contrast_ratio(row_ink, want_gutter) >= 4.5,
                    "the row-number ink is legible on its measured fill");
        const QColor chart_ink =
            header_value(header, name, QStringLiteral("chart_header_text"));
        gate::check(near_count(band(render.chart, 0, header_height), chart_ink, 40) >= 20,
                    "the timescale labels stay drawn in the file's ink");
        std::fprintf(stderr,
                     "note: %s ink px caption %d, row %d, chart %d (inks %s / %s / %s)\n",
                     theme.name, near_count(render.sheet_header, caption_ink, 40),
                     near_count(render.row_header, row_ink, 40),
                     near_count(band(render.chart, 0, header_height), chart_ink, 40),
                     hex(caption_ink).toLatin1().constData(),
                     hex(row_ink).toLatin1().constData(),
                     hex(chart_ink).toLatin1().constData());

        // The LIVE ribbon glyph: exactly the theme-ink render, and one-ink art.
        const QColor ink = header_value(header, name, QStringLiteral("ribbon_ink"));
        const QImage want_icon =
            msqt::shell_ribbon_icon(render.icon_key, render.icon_pixels, ink)
                .pixmap(QSize(render.icon_pixels, render.icon_pixels))
                .toImage()
                .convertToFormat(QImage::Format_ARGB32);
        gate::check(!render.icon.isNull() && render.icon == want_icon,
                    "the live ribbon glyph is drawn in the theme's one ink");
        int opaque = 0;
        int off_ink = 0;
        for (int y = 0; y < render.icon.height(); ++y) {
            for (int x = 0; x < render.icon.width(); ++x) {
                const QRgb rgb = render.icon.pixel(x, y);
                if (qAlpha(rgb) != 255) {
                    continue;
                }
                ++opaque;
                if (qRed(rgb) != ink.red() || qGreen(rgb) != ink.green() ||
                    qBlue(rgb) != ink.blue()) {
                    ++off_ink;
                }
            }
        }
        gate::check(opaque > 0 && off_ink == 0,
                    "the live ribbon glyph is monochrome: no second colour");
        std::fprintf(stderr, "note: %s live icon '%s' %d px, %d opaque px, %d off-ink\n",
                     theme.name, render.icon_key.toLatin1().constData(),
                     render.icon_pixels, opaque, off_ink);
        gate::check(contrast_ratio(ink, measured(chrome, chrome_name,
                                                 QStringLiteral("ribbon"))) >= 4.5,
                    "the ribbon ink is legible on the measured ribbon surface");
    }

    gate::check(light.sheet_header != dark_gray.sheet_header &&
                    dark_gray.sheet_header != black.sheet_header,
                "the caption band repaints with every theme choice");
    gate::check(light.row_header != dark_gray.row_header &&
                    dark_gray.row_header != black.row_header,
                "the gutter repaints with every theme choice");

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(light.ribbon.save(out_dir + QStringLiteral("/theme-colorful-ribbon.png")),
                    "the Colorful ribbon render is written");
        gate::check(dark_gray.ribbon.save(
                        out_dir + QStringLiteral("/theme-dark-gray-ribbon.png")),
                    "the Dark Gray ribbon render is written");
        gate::check(black.ribbon.save(out_dir + QStringLiteral("/theme-black-ribbon.png")),
                    "the Black ribbon render is written");
        // The band renders themselves, so a failing fill can be looked at.
        for (const auto& theme : themes) {
            const QString base =
                out_dir + QStringLiteral("/theme-") + QString::fromLatin1(theme.name);
            gate::check(theme.render->sheet_header.save(base +
                                                        QStringLiteral("-sheet-header.png")),
                        "the caption band render is written");
            gate::check(theme.render->row_header.save(base + QStringLiteral("-row-header.png")),
                        "the gutter render is written");
            gate::check(theme.render->chart.save(base + QStringLiteral("-chart.png")),
                        "the chart render is written");
        }
    }

    return gate::finish();
}
