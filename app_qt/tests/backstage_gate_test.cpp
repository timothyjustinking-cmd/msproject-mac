// app_qt/tests/backstage_gate_test.cpp -- wave 26 G4's backstage gate.
//
// The measured source is the local, git-ignored reference set:
//   * corpus/reference/2026-10-23-msproject/project-backstage-account.png
//     (clean 1903x1033 Account page; rail, back arrow, section geometry);
//   * corpus/reference/2026-10-23-audit/ (the New/Open/Save As/Print pages and
//     all eleven Project Options pages);
//   * the independent numbers committed in
//     tests/data/reference/w26-backstage.measures.json.
//
// Nothing in this gate reads the port's own constants for a measured value: it
// parses the committed numbers, measures the LIVE widgets and the RENDERED
// pixels, and compares.  This file grows one section per G4 item (W26-4.1..
// 4.6); a section is added by the item's own commit.
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QAbstractButton>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "backstage.h"
#include "gate_util.h"
#include "main_window.h"
#include "ribbon_bar.h"
#include "shell_style.h"

namespace {

QColor colour_from(const QJsonValue& value) {
    const QColor colour(value.toString());
    gate::check(colour.isValid(), "a measured colour parses");
    return colour;
}

// The first x in [x0, x1) on row `y` whose pixel is darker than `threshold`.
int first_dark_x(const QImage& image, int y, int x0, int x1, int threshold) {
    for (int x = x0; x < x1; ++x) {
        const QColor colour = image.pixelColor(x, y);
        if (colour.red() < threshold && colour.green() < threshold &&
            colour.blue() < threshold) {
            return x;
        }
    }
    return -1;
}

// True when the row contains a pixel within `tolerance` of the measured green.
bool row_has_colour(const QImage& image, const QRect& band, const QColor& want,
                    int tolerance) {
    for (int y = band.top(); y <= band.bottom(); ++y) {
        for (int x = band.left(); x <= band.right(); ++x) {
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
                continue;
            }
            const QColor colour = image.pixelColor(x, y);
            if (std::abs(colour.red() - want.red()) <= tolerance &&
                std::abs(colour.green() - want.green()) <= tolerance &&
                std::abs(colour.blue() - want.blue()) <= tolerance) {
                return true;
            }
        }
    }
    return false;
}

void check_rail(msqt::MainWindow& window, const QJsonObject& measures) {
    msqt::Backstage* backstage = window.backstage();
    gate::check(backstage != nullptr, "the window has a backstage");
    if (backstage == nullptr) {
        return;
    }
    const QJsonObject rail = measures.value(QStringLiteral("rail")).toObject();
    const int rail_width = rail.value(QStringLiteral("width")).toInt();
    const QColor rail_background = colour_from(rail.value(QStringLiteral("background")));
    const QColor content_background =
        colour_from(rail.value(QStringLiteral("content_background")));
    const int nav_text_left = rail.value(QStringLiteral("nav_text_left")).toInt();
    const int entry_height = rail.value(QStringLiteral("nav_entry_height")).toInt();
    const QColor text_enabled = colour_from(rail.value(QStringLiteral("text_enabled")));
    const QColor text_disabled = colour_from(rail.value(QStringLiteral("text_disabled")));
    const QColor text_selected = colour_from(rail.value(QStringLiteral("text_selected")));

    QWidget* rail_widget =
        backstage->findChild<QWidget*>(QStringLiteral("backstageRail"));
    gate::check(rail_widget != nullptr, "the backstage has its rail widget");
    if (rail_widget != nullptr) {
        gate::check(rail_widget->width() == rail_width,
                    "the rail has Project's measured 133-px width");
    }

    // Geometry only exists once the backstage is on screen; measure after the
    // switch, never from an unlaid-out hidden widget (the wave-21 lesson).
    window.show_backstage(true);
    QApplication::processEvents();
    QApplication::processEvents();

    // The rail order is the independent list (Account under Close, Options as
    // the action pinned at the bottom).
    const QJsonObject nav = measures.value(QStringLiteral("nav")).toObject();
    const QJsonArray order = nav.value(QStringLiteral("order")).toArray();
    const std::vector<QAbstractButton*>& buttons = backstage->nav_buttons();
    gate::check(static_cast<int>(buttons.size()) == order.size(),
                "the rail has one button per measured entry");
    for (int index = 0; index < order.size() && index < static_cast<int>(buttons.size());
         ++index) {
        gate::check(buttons[static_cast<std::size_t>(index)] != nullptr &&
                        buttons[static_cast<std::size_t>(index)]->text() ==
                            order.at(index).toString(),
                    "the rail entry is the measured label in order");
        if (buttons[static_cast<std::size_t>(index)] != nullptr) {
            gate::check(buttons[static_cast<std::size_t>(index)]->height() == entry_height,
                        "the rail entry has the measured 40-px height");
        }
    }

    // The back arrow: measured 21x21 at (10, 14) below the shell's top.
    const QJsonObject arrow = measures.value(QStringLiteral("back_arrow")).toObject();
    QToolButton* back = backstage->back_button();
    gate::check(back != nullptr, "the backstage has the back arrow");
    if (back != nullptr) {
        gate::check(back->width() == arrow.value(QStringLiteral("width")).toInt() &&
                        back->height() == arrow.value(QStringLiteral("height")).toInt(),
                    "the back arrow is the measured 21x21");
        const QPoint origin = back->mapTo(backstage, QPoint(0, 0));
        std::fprintf(stderr, "note: back arrow at %d,%d (%dx%d)\n", origin.x(), origin.y(),
                     back->width(), back->height());
        gate::check(origin.x() == arrow.value(QStringLiteral("x")).toInt(),
                    "the back arrow is the measured 10 px from the left");
        gate::check(origin.y() == arrow.value(QStringLiteral("y_below_title_bar")).toInt(),
                    "the back arrow is the measured 14 px below the shell's top");
    }

    window.show_backstage(true);
    QApplication::processEvents();
    QApplication::processEvents();

    const QImage image = backstage->grab().toImage();
    gate::check(image.width() == backstage->width() &&
                    image.height() == backstage->height(),
                "the backstage grab is the live widget size");
    gate::check(image.pixelColor(5, 600) == rail_background,
                "the rail render is the measured #F0F0F0");
    gate::check(image.pixelColor(400, 600) == content_background,
                "the page area render is the measured #F5F5F5");

    // Entry pitch: 40 px inside each group; the separator adds the measured
    // 9-px gap before Info (capture y=207, entries 40 apart).  Home's top is
    // 37 px below the arrow's top (capture 82 - 45).
    if (buttons.size() == 12 && back != nullptr) {
        const int home_y = buttons[0]->mapTo(backstage, QPoint(0, 0)).y();
        const int arrow_y = back->mapTo(backstage, QPoint(0, 0)).y();
        gate::check(home_y - arrow_y == 37,
                    "Home sits the measured 37 px below the arrow's top");
        gate::check(buttons[1]->mapTo(backstage, QPoint(0, 0)).y() - home_y == entry_height,
                    "Home..New use the measured 40-px pitch");
        gate::check(buttons[2]->mapTo(backstage, QPoint(0, 0)).y() -
                            buttons[1]->mapTo(backstage, QPoint(0, 0)).y() ==
                        entry_height,
                    "New..Open use the measured 40-px pitch");
        gate::check(buttons[3]->mapTo(backstage, QPoint(0, 0)).y() -
                            buttons[2]->mapTo(backstage, QPoint(0, 0)).y() ==
                        entry_height + 9,
                    "the separator adds the measured 9-px gap before Info");
        for (int index = 4; index <= 9; ++index) {
            gate::check(buttons[static_cast<std::size_t>(index)]->mapTo(backstage,
                                                                       QPoint(0, 0))
                                    .y() -
                                buttons[static_cast<std::size_t>(index - 1)]
                                    ->mapTo(backstage, QPoint(0, 0))
                                    .y() ==
                            entry_height,
                        "Info..Close use the measured 40-px pitch");
        }
        const int account_y = buttons[10]->mapTo(backstage, QPoint(0, 0)).y();
        const int options_y = buttons[11]->mapTo(backstage, QPoint(0, 0)).y();
        gate::check(options_y - account_y == entry_height,
                    "Account..Options use the measured 40-px pitch");
        gate::check(account_y > buttons[9]->mapTo(backstage, QPoint(0, 0)).y() + 100,
                    "Account sits below Close at the bottom of the rail");
        gate::check(options_y + entry_height <= backstage->height(),
                    "Options is pinned inside the rail's bottom");
    }

    // The text left edge, measured from the RENDER.  Info is the first
    // text-only entry; its first dark pixel must be the measured 38 px.
    if (buttons.size() == 12) {
        const int info_top = buttons[3]->mapTo(backstage, QPoint(0, 0)).y();
        // The capture was measured on the label's mid-glyph row (y=234..237);
        // use the entry's middle row so antialiased ascender tips cannot win.
        const int text_x = first_dark_x(image, info_top + entry_height / 2, 0, rail_width, 160);
        std::fprintf(stderr,
                     "note: Info button geo %d,%d %dx%d, rail text starts at x=%d (want %d)\n",
                     buttons[3]->x(), buttons[3]->y(), buttons[3]->width(),
                     buttons[3]->height(), text_x, nav_text_left);
        gate::check(text_x == nav_text_left,
                    "the rail text starts at the capture's measured x");
        // The enabled text colour: the darkest Info pixel is the measured ink.
        QColor darkest(255, 255, 255);
        for (int y = info_top + 8; y < info_top + entry_height - 6; ++y) {
            for (int x = nav_text_left; x < rail_width - 8; ++x) {
                const QColor colour = image.pixelColor(x, y);
                if (colour.lightness() < darkest.lightness()) {
                    darkest = colour;
                }
            }
        }
        gate::check(darkest == text_enabled,
                    "the enabled rail text is the measured #242424");

        // Account is selected: its render carries the measured green.
        buttons[10]->click();
        QApplication::processEvents();
        const QImage selected = backstage->grab().toImage();
        const int account_top = buttons[10]->mapTo(backstage, QPoint(0, 0)).y();
        gate::check(row_has_colour(selected,
                                   QRect(0, account_top, rail_width, entry_height),
                                   text_selected, 12),
                    "the selected Account entry renders the measured #0F703B");

        // Save..Close are disabled while no document is open: the measured
        // disabled ink #999999 is what their rows render.
        const int save_top = buttons[4]->mapTo(backstage, QPoint(0, 0)).y();
        QColor save_darkest(255, 255, 255);
        for (int y = save_top + 8; y < save_top + entry_height - 6; ++y) {
            for (int x = nav_text_left; x < rail_width - 8; ++x) {
                const QColor colour = selected.pixelColor(x, y);
                if (colour.lightness() < save_darkest.lightness()) {
                    save_darkest = colour;
                }
            }
        }
        gate::check(save_darkest == text_disabled,
                    "Save (no document) renders the measured #999999");
    }

    // The two measured separators: 100 px wide, 15 px from the left, #D1D1D1.
    const QList<QFrame*> separators =
        backstage->findChildren<QFrame*>(QStringLiteral("backstageSeparator"));
    gate::check(separators.size() == 2, "the rail has the two measured separators");
    const QJsonObject rail_measures = measures.value(QStringLiteral("rail")).toObject();
    const int inset_left = rail_measures.value(QStringLiteral("separator_inset_left")).toInt();
    const int inset_right =
        rail_measures.value(QStringLiteral("separator_inset_right")).toInt();
    const QColor separator_colour =
        colour_from(rail_measures.value(QStringLiteral("separator_colour")));
    for (QFrame* separator : separators) {
        if (separator == nullptr) {
            continue;
        }
        gate::check(separator->width() == rail_width - inset_left - inset_right,
                    "the separator has the measured 100-px width");
        const QPoint origin = separator->mapTo(backstage, QPoint(0, 0));
        gate::check(origin.x() == inset_left,
                    "the separator is inset the measured 15 px");
        const QImage live = backstage->grab().toImage();
        gate::check(live.pixelColor(origin.x() + 50, origin.y()) == separator_colour,
                    "the separator renders the measured #D1D1D1");
    }
}

void check_file_tab(msqt::MainWindow& window) {
    window.show_backstage(false);
    QApplication::processEvents();
    msqt::RibbonBar* ribbon = window.ribbon();
    gate::check(ribbon != nullptr, "the window has a ribbon");
    if (ribbon == nullptr) {
        return;
    }
    QToolButton* file_tab =
        window.findChild<QToolButton*>(QStringLiteral("ribbonFileTab"));
    gate::check(file_tab != nullptr && file_tab->text() == QStringLiteral("File"),
                "the ribbon's first tab is File");
    if (file_tab != nullptr) {
        // B-7: Project draws File as a PLAIN tab.  The old dark accent tile
        // (#1f5c99) is the falsification: the live render must be light.  Grab
        // the ribbon (the tab's own grab has no parent surface behind it) and
        // sample the tab's measured rectangle inside it.
        const QImage image = ribbon->grab().toImage();
        const QPoint origin = file_tab->mapTo(ribbon, QPoint(0, 0));
        const QColor left_edge = image.pixelColor(origin.x() + 2, origin.y() + 2);
        const QColor right_edge =
            image.pixelColor(origin.x() + file_tab->width() - 3,
                             origin.y() + file_tab->height() / 2);
        std::fprintf(stderr, "note: File tab render %s / %s at %d,%d %dx%d\n",
                     left_edge.name().toLocal8Bit().constData(),
                     right_edge.name().toLocal8Bit().constData(), origin.x(), origin.y(),
                     file_tab->width(), file_tab->height());
        gate::check(left_edge.lightness() > 180,
                    "the File tab renders plain/light, not the old dark tile");
        gate::check(right_edge.lightness() > 180,
                    "the File tab's text area is the tab row's light surface");
    }
}

void check_document_state(msqt::MainWindow& window, const QString& fixture) {
    // No document: Save..Close disabled (the clean Account capture).
    window.show_backstage(true);
    QApplication::processEvents();
    const std::vector<QAbstractButton*>& buttons = window.backstage()->nav_buttons();
    if (buttons.size() == 12) {
        for (int index = 4; index <= 9; ++index) {
            gate::check(!buttons[static_cast<std::size_t>(index)]->isEnabled(),
                        "Save..Close are disabled without a document");
        }
    }
    window.show_backstage(false);
    QString error;
    gate::check(window.open_file(fixture, &error), "the fixture opens");
    window.show_backstage(true);
    QApplication::processEvents();
    if (buttons.size() == 12) {
        for (int index = 4; index <= 9; ++index) {
            gate::check(buttons[static_cast<std::size_t>(index)]->isEnabled(),
                        "Save..Close are enabled with a document");
        }
    }
    window.show_backstage(false);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: backstage_gate_tests <fixture.mpp> <w26-backstage.measures.json>\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString measures_path = QString::fromLocal8Bit(argv[2]);

    QFile measures_file(measures_path);
    gate::check(measures_file.open(QIODevice::ReadOnly | QIODevice::Text),
                "the measured backstage numbers exist");
    if (!measures_file.isOpen()) {
        return gate::finish();
    }
    const QJsonDocument document = QJsonDocument::fromJson(measures_file.readAll());
    gate::check(document.isObject(), "the measured numbers parse as JSON");
    const QJsonObject measures = document.object();

    gate::check(QFontDatabase::families().contains(QStringLiteral("Carlito"),
                                                   Qt::CaseInsensitive),
                "the bundled Carlito family is registered");

    msqt::MainWindow window;
    window.resize(msqt::kParityWidth, msqt::kParityHeight);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    check_rail(window, measures);
    check_file_tab(window);
    check_document_state(window, fixture);

    std::fprintf(stderr, "note: W26-4.1 rail/File-tab checks ran on the live window\n");
    return gate::finish();
}
