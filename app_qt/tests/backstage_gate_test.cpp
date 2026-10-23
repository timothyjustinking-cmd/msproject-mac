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
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QSpinBox>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

// Qt 6.5.3's qtestmouse.h trips MSVC /W4 C4701 (an uninitialised local in the
// inline double-click helper).  The gate builds with /WX, so scope the warning
// off for this third-party header only (same rule as dialog_gate_test.cpp).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4701)
#endif
#include <QTest>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "backstage.h"
#include "gate_util.h"
#include "main_window.h"
#include "preferences_store.h"
#include "print_page.h"
#include "ribbon_bar.h"
#include "shell_style.h"
#include "theme.h"

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

void check_account(msqt::MainWindow& window, const QJsonObject& measures,
                   const QString& out_dir) {
    msqt::Backstage* backstage = window.backstage();
    if (backstage == nullptr) {
        return;
    }
    const QJsonObject page = measures.value(QStringLiteral("account_page")).toObject();
    const int right_left = page.value(QStringLiteral("right_column_left")).toInt();
    const int combo_left = page.value(QStringLiteral("theme_combo_left")).toInt();
    const int combo_width = page.value(QStringLiteral("theme_combo_width")).toInt();
    const QJsonArray order = page.value(QStringLiteral("theme_combo_order")).toArray();
    const QJsonArray implemented =
        page.value(QStringLiteral("theme_choices_implemented")).toArray();

    window.show_backstage(true);
    backstage->show_page(msqt::Backstage::Page::account);
    QApplication::processEvents();
    QApplication::processEvents();
    gate::check(backstage->pages()->currentIndex() ==
                    static_cast<int>(msqt::Backstage::Page::account),
                "the Account page is on screen for its measurements");
    QWidget* page_widget = backstage->pages()->currentWidget();

    QComboBox* combo = backstage->theme_combo();
    gate::check(combo != nullptr, "the Account page has the Office Theme chooser");
    if (combo != nullptr && page_widget != nullptr) {
        const QPoint origin = combo->mapTo(page_widget, QPoint(0, 0));
        gate::check(origin.x() == combo_left,
                    "the theme chooser starts at the capture's 41-px left");
        gate::check(combo->width() == combo_width,
                    "the theme chooser has the capture's 377-px width");
        gate::check(combo->count() == order.size(),
                    "the chooser offers Project's five theme names");
        const QAbstractItemModel* model = combo->model();
        for (int index = 0; index < order.size() && index < combo->count(); ++index) {
            gate::check(combo->itemText(index) == order.at(index).toString(),
                        "the theme name/order is the capture's");
            bool want_enabled = false;
            for (const QJsonValue& value : implemented) {
                if (value.toString() == order.at(index).toString()) {
                    want_enabled = true;
                }
            }
            const bool live_enabled =
                model != nullptr &&
                (model->flags(model->index(index, 0)) & Qt::ItemIsEnabled) != 0;
            gate::check(live_enabled == want_enabled,
                        "only the implemented theme names are enabled");
            if (!want_enabled) {
                gate::check(combo->itemData(index, Qt::ToolTipRole).toString() ==
                                QStringLiteral("not implemented in this port"),
                            "a disabled theme item carries the exact tooltip");
            }
        }
    }
    QWidget* right = backstage->account_right_column();
    if (right != nullptr && page_widget != nullptr) {
        gate::check(right->mapTo(page_widget, QPoint(0, 0)).x() == right_left,
                    "the right column starts at the capture's measured 494 px");
    }

    gate::check(backstage->product_label() != nullptr &&
                    backstage->product_label()->text() ==
                        page.value(QStringLiteral("product_name")).toString(),
                "the Account page carries the port's own name");
    const QString version = page.value(QStringLiteral("product_version")).toString();
    gate::check(backstage->version_label() != nullptr &&
                    backstage->version_label()->text() ==
                        QStringLiteral("Version ") + version,
                "the Account page carries the port's version");

    // About is mouse-reachable: the Account button opens the real About box.
    QAbstractButton* about = backstage->about_button();
    gate::check(about != nullptr && about->isEnabled(),
                "the Account page has an enabled About button");
    if (about != nullptr) {
        bool saw_dialog = false;
        QString title;
        QTimer::singleShot(0, [&saw_dialog, &title]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                saw_dialog = true;
                title = dialog->windowTitle();
                dialog->close();
            }
        });
        about->click();
        QApplication::processEvents();
        gate::check(saw_dialog, "the About button opens a modal About box");
        gate::check(title == QStringLiteral("About MS Project Shell"),
                    "the About box is the port's own");
    }

    // Every other Account control is laid out and disabled with decision 9's
    // exact tooltip (the capture's user/license services are not in this port).
    if (page_widget != nullptr) {
        const char* const disabled_buttons[] = {
            "Sign out", "Switch account", "Manage Settings", "OneDrive - Personal",
            "Add a service", "Switch License",
        };
        for (const char* text : disabled_buttons) {
            QPushButton* found = nullptr;
            for (QPushButton* button : page_widget->findChildren<QPushButton*>()) {
                if (button != nullptr && button->text() == QString::fromUtf8(text)) {
                    found = button;
                    break;
                }
            }
            gate::check(found != nullptr, "the Account control is laid out");
            if (found != nullptr) {
                gate::check(!found->isEnabled(),
                            "the Account control is disabled (no service behind it)");
                gate::check(found->toolTip() ==
                                QStringLiteral("not implemented in this port"),
                            "the Account control carries the exact tooltip");
            }
        }
    }

    // The chooser applies the implemented palettes through the live handler and
    // persists the resolved light/dark palette.
    if (combo != nullptr) {
        QDir().mkpath(out_dir);
        const QString ini = out_dir + QStringLiteral("/backstage-gate.ini");
        QFile::remove(ini);
        {
            msqt::PreferencesStore store(ini);
            window.set_preferences_store(&store);
            const int dark_index = static_cast<int>(msqt::ThemeChoice::dark_gray);
            combo->setCurrentIndex(dark_index);
            QApplication::processEvents();
            gate::check(window.theme() == msqt::Theme::dark,
                        "choosing Dark Gray applies the dark palette");
            gate::check(window.theme_choice() == msqt::ThemeChoice::dark_gray,
                        "the chooser keeps the Dark Gray choice");
            msqt::Theme stored = msqt::Theme::light;
            gate::check(store.load_theme(stored) && stored == msqt::Theme::dark,
                        "the resolved palette is persisted through the store");
            gate::check(!window.set_theme_choice(msqt::ThemeChoice::black, false),
                        "the unimplemented Black choice is refused");
            gate::check(window.theme() == msqt::Theme::dark,
                        "a refused choice changes no palette");
            combo->setCurrentIndex(static_cast<int>(msqt::ThemeChoice::colorful));
            QApplication::processEvents();
            gate::check(window.theme() == msqt::Theme::light,
                        "choosing Colorful returns the parity palette");
            window.set_preferences_store(nullptr);
        }
        backstage->set_theme_choice(window.theme_choice());
    }
}

void check_new(msqt::MainWindow& window, const QString& fixture, const QString& out_dir) {
    msqt::Backstage* backstage = window.backstage();
    if (backstage == nullptr) {
        return;
    }
    QDir().mkpath(out_dir);
    const QString ini = out_dir + QStringLiteral("/backstage-new.ini");
    QFile::remove(ini);
    msqt::PreferencesStore store(ini);
    store.add_recent_file(fixture);
    window.set_preferences_store(&store);

    window.show_backstage(true);
    backstage->show_page(msqt::Backstage::Page::create_new);
    QApplication::processEvents();
    QApplication::processEvents();
    gate::check(backstage->pages()->currentIndex() ==
                    static_cast<int>(msqt::Backstage::Page::create_new),
                "the New page is on screen");
    QWidget* page = backstage->pages()->currentWidget();

    QAbstractButton* blank = backstage->new_blank_button();
    QAbstractButton* existing = backstage->new_from_existing_button();
    gate::check(blank != nullptr && blank->isEnabled() &&
                    blank->text() == QStringLiteral("Blank Project"),
                "the New page offers the enabled Blank Project tile");
    gate::check(existing != nullptr && existing->isEnabled() &&
                    existing->text() == QStringLiteral("New from existing project"),
                "the New page offers the enabled New-from-existing tile");
    QLineEdit* search = backstage->online_search();
    gate::check(search != nullptr && !search->isEnabled(),
                "the online-template search is laid out and disabled");
    if (search != nullptr) {
        gate::check(search->toolTip() == QStringLiteral("not implemented in this port"),
                    "the online-template search carries the exact tooltip");
        gate::check(search->placeholderText().contains(QStringLiteral("online templates"),
                                                       Qt::CaseInsensitive),
                    "the search box names the online templates");
    }
    QLabel* note = backstage->online_note();
    gate::check(note != nullptr && note->isVisible() &&
                    note->text().contains(QStringLiteral("Online templates")),
                "the disabled search carries the visible local-only note");

    QListWidget* recent = backstage->new_recent_list();
    gate::check(recent != nullptr && recent->count() == 1,
                "the New page lists the local recent file");
    if (recent != nullptr && recent->count() > 0) {
        QListWidgetItem* item = recent->item(0);
        gate::check(item->text() == QFileInfo(fixture).fileName(),
                    "the recent row shows the local file's name");
        gate::check(item->toolTip() == fixture &&
                        !item->toolTip().contains(QStringLiteral("://")),
                    "the recent row is a local path, never a URL");
        // A real double-click through the live view (the connection, not a copy
        // of the rule).  QTest::mouseDClick sends only the DblClick event, so
        // press/release surround it as in dialog_gate_test.cpp.
        const QPoint centre = recent->visualItemRect(item).center();
        QTest::mousePress(recent->viewport(), Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseDClick(recent->viewport(), Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseRelease(recent->viewport(), Qt::LeftButton, Qt::NoModifier, centre);
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(!window.backstage_open(),
                    "the Recent row closes the backstage and opens the project");
        gate::check(window.loaded_task_count() > 0,
                    "the Recent row really loads the local file");
    }

    // Blank Project resets to the new-project state (the same command as the
    // page's main action).
    window.show_backstage(true);
    backstage->show_page(msqt::Backstage::Page::create_new);
    QApplication::processEvents();
    if (blank != nullptr) {
        blank->click();
        QApplication::processEvents();
        gate::check(window.loaded_task_count() == 0,
                    "Blank Project starts an empty project");
    }

    // New from existing is wired to the file picker; the offscreen gate closes
    // the modal without choosing a file.
    bool saw_dialog = false;
    QTimer::singleShot(0, [&saw_dialog]() {
        if (QApplication::activeModalWidget() != nullptr) {
            saw_dialog = true;
            QApplication::activeModalWidget()->close();
        }
    });
    if (existing != nullptr) {
        existing->click();
        QApplication::processEvents();
        gate::check(saw_dialog,
                    "New from existing opens the local file picker");
    }
    (void)page;
    window.set_preferences_store(nullptr);
}

QPushButton* button_named(QWidget* page, const char* name) {
    return page != nullptr ? page->findChild<QPushButton*>(QString::fromUtf8(name)) : nullptr;
}

void check_locations(msqt::MainWindow& window, const QString& fixture, const QString& out_dir) {
    msqt::Backstage* backstage = window.backstage();
    if (backstage == nullptr) {
        return;
    }
    QDir().mkpath(out_dir);
    const QString ini = out_dir + QStringLiteral("/backstage-locations.ini");
    QFile::remove(ini);
    msqt::PreferencesStore store(ini);
    store.add_recent_file(fixture);
    window.set_preferences_store(&store);

    window.show_backstage(true);
    backstage->show_page(msqt::Backstage::Page::open);
    QApplication::processEvents();
    QApplication::processEvents();
    QWidget* open_page = backstage->pages()->currentWidget();

    // The capture's Open page: Recent, This PC and Browse are live; the cloud
    // entries are laid out and disabled with the exact tooltip.
    QPushButton* open_pc = button_named(open_page, "openThisPc");
    QPushButton* open_browse = button_named(open_page, "openBrowse");
    gate::check(open_pc != nullptr && open_pc->isEnabled(),
                "Open > This PC is laid out and enabled");
    gate::check(open_browse != nullptr && open_browse->isEnabled(),
                "Open > Browse is laid out and enabled");
    for (const char* name : {"openOneDrive", "openSharePoint", "openAddPlace"}) {
        QPushButton* button = button_named(open_page, name);
        gate::check(button != nullptr && !button->isEnabled(),
                    "the Open cloud/placeholder entry is disabled");
        if (button != nullptr) {
            gate::check(button->toolTip() ==
                            QStringLiteral("not implemented in this port"),
                        "the Open disabled entry carries the exact tooltip");
        }
    }
    QListWidget* open_recent =
        open_page != nullptr
            ? open_page->findChild<QListWidget*>(QStringLiteral("openRecentList"))
            : nullptr;
    gate::check(open_recent != nullptr && open_recent->count() == 1,
                "the Open page lists the local recent file");
    if (open_recent != nullptr && open_recent->count() > 0) {
        QListWidgetItem* item = open_recent->item(0);
        gate::check(item->text() == QFileInfo(fixture).fileName(),
                    "the Open recent row shows the local file's name");
        const QPoint centre = open_recent->visualItemRect(item).center();
        QTest::mousePress(open_recent->viewport(), Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseDClick(open_recent->viewport(), Qt::LeftButton, Qt::NoModifier, centre);
        QTest::mouseRelease(open_recent->viewport(), Qt::LeftButton, Qt::NoModifier,
                            centre);
        QApplication::processEvents();
        QApplication::processEvents();
        gate::check(!window.backstage_open() && window.loaded_task_count() > 0,
                    "the Open recent row opens the local file");
    }

    // Save As: the same locations, with its own recent list.  The two live
    // buttons' commands are proved through a recording handler (the real ones
    // open the modal save panel, which an offscreen gate cannot drive).
    window.show_backstage(true);
    backstage->show_page(msqt::Backstage::Page::save_as);
    QApplication::processEvents();
    QWidget* save_page = backstage->pages()->currentWidget();
    QPushButton* save_pc = button_named(save_page, "saveAsThisPc");
    QPushButton* save_browse = button_named(save_page, "saveAsBrowse");
    gate::check(save_pc != nullptr && save_pc->isEnabled(),
                "Save As > This PC is laid out and enabled");
    gate::check(save_browse != nullptr && save_browse->isEnabled(),
                "Save As > Browse is laid out and enabled");
    for (const char* name : {"saveAsOneDrive", "saveAsSharePoint", "saveAsAddPlace"}) {
        QPushButton* button = button_named(save_page, name);
        gate::check(button != nullptr && !button->isEnabled(),
                    "the Save As cloud/placeholder entry is disabled");
        if (button != nullptr) {
            gate::check(button->toolTip() ==
                            QStringLiteral("not implemented in this port"),
                        "the Save As disabled entry carries the exact tooltip");
        }
    }
    QListWidget* save_recent =
        save_page != nullptr
            ? save_page->findChild<QListWidget*>(QStringLiteral("saveAsRecentList"))
            : nullptr;
    gate::check(save_recent != nullptr && save_recent->count() == 1,
                "the Save As page lists the local recent files for reference");

    {
        const std::function<void(const QString&)> original =
            backstage->command_handler();
        QStringList recorded;
        backstage->set_command_handler(
            [&recorded](const QString& command) { recorded << command; });
        if (open_pc != nullptr) {
            open_pc->click();
        }
        if (open_browse != nullptr) {
            open_browse->click();
        }
        if (save_pc != nullptr) {
            save_pc->click();
        }
        if (save_browse != nullptr) {
            save_browse->click();
        }
        const QStringList expected = {
            QStringLiteral("open_this_pc"), QStringLiteral("open"),
            QStringLiteral("save_as_this_pc"), QStringLiteral("save_as"),
        };
        gate::check(recorded == expected,
                    "This PC/Browse ask the shell for the measured local commands");
        backstage->set_command_handler(original);
    }
    window.show_backstage(false);
    window.set_preferences_store(nullptr);
}

void check_print(msqt::MainWindow& window) {
    msqt::Backstage* backstage = window.backstage();
    if (backstage == nullptr || window.sheet() == nullptr) {
        gate::check(false, "the Print gate needs an open document");
        return;
    }
    window.show_backstage(true);
    backstage->show_page(msqt::Backstage::Page::print);
    QApplication::processEvents();
    QApplication::processEvents();
    QApplication::processEvents();
    QWidget* page = backstage->pages()->currentWidget();

    // The capture's Print page: copies and the printer list are live; dates,
    // page ranges and Page Setup are laid out and disabled (the shared print
    // job has no date/page selection).
    QSpinBox* copies =
        page != nullptr ? page->findChild<QSpinBox*>(QStringLiteral("printCopies"))
                        : nullptr;
    gate::check(copies != nullptr && copies->isEnabled() && copies->value() == 1,
                "the Print page has the enabled Copies spin");
    QComboBox* printer =
        page != nullptr ? page->findChild<QComboBox*>(QStringLiteral("printPrinter"))
                        : nullptr;
    gate::check(printer != nullptr && printer->count() >= 1,
                "the Print page lists the local printers");
    QAbstractButton* print_button = backstage->print_button();
    gate::check(print_button != nullptr && print_button->isEnabled(),
                "the Print page has the live Print button");
    for (const char* name : {"printSpecificDates", "printDateFrom", "printDateTo",
                             "printPageFrom", "printPageTo", "printPageSetup"}) {
        QWidget* control = page != nullptr
                               ? page->findChild<QWidget*>(QString::fromUtf8(name))
                               : nullptr;
        gate::check(control != nullptr && !control->isEnabled(),
                    "the Print date/page control is laid out and disabled");
        if (control != nullptr) {
            gate::check(control->toolTip() ==
                            QStringLiteral("not implemented in this port"),
                        "the disabled Print control carries the exact tooltip");
        }
    }
    QCheckBox* landscape = backstage->print_landscape_check();
    QComboBox* paper = backstage->print_paper_combo();
    gate::check(landscape != nullptr && landscape->isEnabled() && landscape->isChecked(),
                "the landscape orientation control is live and checked");
    gate::check(paper != nullptr && paper->isEnabled() && paper->count() == 2 &&
                    paper->itemText(0) == QStringLiteral("A4") &&
                    paper->itemText(1) == QStringLiteral("Letter"),
                "the paper control offers the shared A4/Letter specs");

    QLabel* preview = backstage->print_preview();
    gate::check(preview != nullptr, "the Print page has the preview pane");
    if (preview == nullptr || landscape == nullptr || paper == nullptr) {
        return;
    }

    auto expected = [&window, preview](bool is_landscape, bool is_letter) {
        app::PageSpec spec = is_letter ? app::PageSpec::letter_landscape()
                                       : app::PageSpec::a4_landscape();
        if (!is_landscape) {
            std::swap(spec.width, spec.height);
        }
        const app::PrintJob job = window.current_print_job(spec);
        return msqt::render_print_page(job, 0, *window.sheet(), window.plan(),
                                       window.preferences(), preview->size());
    };
    auto pixmap_matches = [preview](const QImage& want) {
        const QPixmap pixmap = preview->pixmap();
        if (pixmap.isNull() || pixmap.size() != want.size()) {
            return false;
        }
        const QImage live = pixmap.toImage();
        if (live.size() != want.size()) {
            return false;
        }
        for (int y = 0; y < want.height(); ++y) {
            for (int x = 0; x < want.width(); ++x) {
                if (live.pixelColor(x, y) != want.pixelColor(x, y)) {
                    return false;
                }
            }
        }
        return true;
    };

    gate::check(pixmap_matches(expected(true, false)),
                "the preview pane is the shared print layout's A4 landscape page");

    // The white page rectangle's own aspect is the shared PageSpec's.
    {
        const QImage image = preview->pixmap().toImage();
        int left = image.width();
        int right = -1;
        int top = image.height();
        int bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor colour = image.pixelColor(x, y);
                if (colour.red() > 245 && colour.green() > 245 && colour.blue() > 245) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        }
        gate::check(right > left && bottom > top,
                    "the preview paints a white page rectangle");
        if (right > left && bottom > top) {
            const double aspect = static_cast<double>(right - left + 1) /
                                  static_cast<double>(bottom - top + 1);
            const app::PageSpec spec = app::PageSpec::a4_landscape();
            gate::check(std::abs(aspect - spec.width / spec.height) < 0.03,
                        "the preview page rectangle has the shared A4 aspect");
        }
    }

    // Live settings: unchecking landscape and choosing Letter each re-render.
    landscape->setChecked(false);
    QApplication::processEvents();
    gate::check(pixmap_matches(expected(false, false)),
                "unchecking landscape re-renders the portrait shared page");
    paper->setCurrentIndex(1);
    QApplication::processEvents();
    gate::check(pixmap_matches(expected(false, true)),
                "choosing Letter re-renders the Letter portrait shared page");
    landscape->setChecked(true);
    paper->setCurrentIndex(0);
    QApplication::processEvents();
    gate::check(pixmap_matches(expected(true, false)),
                "the preview returns to the A4 landscape shared page");
    window.show_backstage(false);
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
                     "usage: backstage_gate_tests <fixture.mpp> <w26-backstage.measures.json> "
                     "[out-dir]\n");
        return 2;
    }
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString measures_path = QString::fromLocal8Bit(argv[2]);
    const QString out_dir = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();

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
    check_account(window, measures, out_dir);
    check_file_tab(window);
    check_document_state(window, fixture);
    check_new(window, fixture, out_dir);
    check_locations(window, fixture, out_dir);
    check_print(window);

    std::fprintf(stderr,
                 "note: W26-4.1..4.5 rail, File-tab, Account, New, location and Print "
                 "checks ran on the live window\n");
    return gate::finish();
}
