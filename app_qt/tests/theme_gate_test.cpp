// app_qt/tests/theme_gate_test.cpp -- wave 26 (W26-6.2).
//
// THE TARGET (operator decision 10 / Q14, docs/ui-parity-spec.md)
//   Office's Dark Gray and Black themes darken ONLY the chrome.  The sheet and
//   the chart keep the file's colours, pixel for pixel.  The theme switch is
//   applied live (no restart): the Account/Options chooser calls
//   set_theme_choice() and every shell-owned surface repaints.
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
//      Black, and that the sheet viewport and the Gantt canvas are EXACTLY
//      unchanged between all three themes (tolerance 0: QImage equality).
//
// Usage: qt_theme_gate <fixture.mpp> <w26-theme.measures.json> <out-dir>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QStatusBar>
#include <QString>
#include <QTableView>
#include <QWidget>

#include <cstdio>

#include "gantt_canvas.h"
#include "gate_util.h"
#include "main_window.h"
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

struct Render {
    QImage ribbon;
    QImage status;
    QImage sheet;
    QImage chart;
    QColor ribbon_colour;
    QColor status_colour;
    int ribbon_count = 0;
    int status_count = 0;
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
    render.ribbon_colour = dominant_colour(render.ribbon, &render.ribbon_count);
    render.status_colour = dominant_colour(render.status, &render.status_count);
    return render;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_theme_gate <fixture.mpp> <w26-theme.measures.json> "
                     "[out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString measures_path = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();

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

    // --- 3. the sheet and chart are EXACTLY unchanged -----------------------
    gate::check(!light.sheet.isNull() && !light.chart.isNull(),
                "the sheet and chart rendered");
    gate::check(light.sheet == dark_gray.sheet,
                "Dark Gray leaves every sheet pixel unchanged (tolerance 0)");
    gate::check(light.sheet == black.sheet,
                "Black leaves every sheet pixel unchanged (tolerance 0)");
    gate::check(light.chart == dark_gray.chart,
                "Dark Gray leaves every chart pixel unchanged (tolerance 0)");
    gate::check(light.chart == black.chart,
                "Black leaves every chart pixel unchanged (tolerance 0)");
    gate::check(light.ribbon != dark_gray.ribbon,
                "the chrome render itself changed, not just a sampled pixel");

    if (!out_dir.isEmpty()) {
        QDir().mkpath(out_dir);
        gate::check(light.ribbon.save(out_dir + QStringLiteral("/theme-colorful-ribbon.png")),
                    "the Colorful ribbon render is written");
        gate::check(dark_gray.ribbon.save(
                        out_dir + QStringLiteral("/theme-dark-gray-ribbon.png")),
                    "the Dark Gray ribbon render is written");
        gate::check(black.ribbon.save(out_dir + QStringLiteral("/theme-black-ribbon.png")),
                    "the Black ribbon render is written");
    }

    return gate::finish();
}
