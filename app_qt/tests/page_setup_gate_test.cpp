// app_qt/tests/page_setup_gate_test.cpp -- wave 29 G3's gate for Page Setup
// (W29-3.1).
//
// The measured source is the local, git-ignored Page Setup capture in build/w29.
// The numbers the gate compares against are committed in
// tests/data/reference/w29-page-setup.measures.json (client-relative rectangles
// of every Page-tab control and of the five action buttons, plus the dialog's
// captured client size and tab count).  Nothing here reads the port's own
// constants for a measured value: the JSON is parsed and the LIVE widget tree is
// measured.  The live widget for each captured control is found by the
// `measuredControlId` dynamic property the dialog sets, so a rectangle cannot
// drift onto a different control.
//
// The functional half drives every live field of the dialog and then the real
// shell path: the backstage Print page's Page Setup button opens the modal, the
// gate edits it, OK applies the PageSpec through MainWindow, and the Print
// page's own preview is compared pixel-for-pixel with the shared print layout
// rendered from the same spec.  The automatic/custom header and footer strings
// are checked against app::expand_print_text() for every page, and the scaled
// virtual page's bands and rows-per-page against the measured arithmetic in
// app/page_layout_tests.cpp.
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPoint>
#include <QPushButton>
#include <QRadioButton>
#include <QRect>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QTabBar>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "backstage.h"
#include "gate_util.h"
#include "main_window.h"  // configure_application()
#include "page_layout.h"
#include "page_setup_dialog.h"
#include "print_page.h"

namespace {

using msqt::MainWindow;
using msqt::PageSetupDialog;
using msqt::page_setup_geometry::kNotImplemented;

const QString kTip = QString::fromUtf8(kNotImplemented);
const char* const kMeasuredId = "measuredControlId";

QJsonObject load_measures(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

void check_rect(QWidget* live, const QJsonArray& want, const QString& name,
                QWidget* dialog) {
    if (live == nullptr) {
        std::fprintf(stderr, "FAIL: the measured control '%s' has no live widget\n",
                     name.toUtf8().constData());
        ++gate::failures;
        return;
    }
    const QPoint origin = live->mapTo(dialog, QPoint(0, 0));
    const QRect got(origin, live->size());
    const bool ok = want.size() == 4 && got.x() == want.at(0).toInt() &&
                    got.y() == want.at(1).toInt() &&
                    got.width() == want.at(2).toInt() &&
                    got.height() == want.at(3).toInt();
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: live rect '%s': got [%d, %d, %d, %d] want [%d, %d, %d, %d]\n",
                     name.toUtf8().constData(), got.x(), got.y(), got.width(),
                     got.height(), want.size() == 4 ? want.at(0).toInt() : -1,
                     want.size() == 4 ? want.at(1).toInt() : -1,
                     want.size() == 4 ? want.at(2).toInt() : -1,
                     want.size() == 4 ? want.at(3).toInt() : -1);
        ++gate::failures;
    }
}

// The live widget carrying a captured Project control id, if exactly one does.
QWidget* live_for_id(QWidget* dialog, int id) {
    QWidget* found = nullptr;
    int count = 0;
    for (QWidget* widget : dialog->findChildren<QWidget*>()) {
        const QVariant value = widget->property(kMeasuredId);
        if (value.isValid() && value.toInt() == id) {
            found = widget;
            ++count;
        }
    }
    return count == 1 ? found : nullptr;
}

// ---------------------------------------------------------------------------// The measured Page tab
// ---------------------------------------------------------------------------
void check_measured_geometry(const QJsonObject& measures) {
    const QJsonObject dialog_spec = measures.value(QStringLiteral("dialog")).toObject();
    gate::check(!dialog_spec.isEmpty(), "the measures file carries the dialog record");
    PageSetupDialog dialog(app::PageSpec::a4_landscape());
    dialog.show();
    QApplication::processEvents();

    gate::check(dialog.windowTitle() == dialog_spec.value(QStringLiteral("title")).toString(),
                "Page Setup has the captured title");
    gate::check(dialog.width() == dialog_spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() ==
                        dialog_spec.value(QStringLiteral("height")).toInt(),
                "Page Setup has the measured 476x362 client");
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "Page Setup is non-resizable");

    QTabBar* tabs = dialog.tabs();
    gate::check(tabs != nullptr &&
                    tabs->count() == dialog_spec.value(QStringLiteral("tab_count")).toInt(),
                "the tab strip has the captured six tabs");
    const QStringList expected_tabs = {
        QStringLiteral("Page"),   QStringLiteral("Margins"),
        QStringLiteral("Header"), QStringLiteral("Footer"),
        QStringLiteral("Legend"), QStringLiteral("View")};
    QStringList live_tabs;
    for (int index = 0; tabs != nullptr && index < tabs->count(); ++index) {
        live_tabs << tabs->tabText(index);
    }
    gate::check(live_tabs == expected_tabs,
                "the six tabs are Project's Page Setup tab set, in order");
    gate::check(dialog.pages() != nullptr && dialog.pages()->count() == 6 &&
                    dialog.pages()->currentIndex() == 0,
                "the stack has the six matching pages, Page selected");
    // A tab really switches the page.
    if (tabs != nullptr) {
        tabs->setCurrentIndex(1);
        gate::check(dialog.pages()->currentIndex() == 1,
                    "the Margins tab brings its page to the front");
        tabs->setCurrentIndex(0);
    }

    const QJsonArray controls =
        measures.value(QStringLiteral("page_tab_controls")).toArray();
    gate::check(controls.size() == 22,
                "the measures file carries all 22 captured Page-tab controls");
    QSet<int> measured_ids;
    for (const QJsonValue& value : controls) {
        const QJsonObject control = value.toObject();
        const int id = control.value(QStringLiteral("id")).toInt();
        const QString name = control.value(QStringLiteral("name")).toString();
        measured_ids.insert(id);
        QWidget* live = live_for_id(&dialog, id);
        gate::check(live != nullptr,
                    "the captured control id names exactly one live widget");
        check_rect(live, control.value(QStringLiteral("rect")).toArray(), name, &dialog);
    }

    // The reverse direction: every marked live widget is in the measures file,
    // so an unmeasured control cannot hide behind a mark.
    QSet<int> live_ids;
    for (QWidget* widget : dialog.findChildren<QWidget*>()) {
        const QVariant value = widget->property(kMeasuredId);
        if (value.isValid()) {
            live_ids.insert(value.toInt());
        }
    }
    gate::check(live_ids == measured_ids,
                "the marked live controls are exactly the captured set");
}

// ---------------------------------------------------------------------------
// The dialog's live fields and its three closing paths
// ---------------------------------------------------------------------------
void check_initial_values_and_edit() {
    app::PageSpec source = app::PageSpec::letter_landscape();
    source.width = 612.0;  // Letter portrait
    source.height = 792.0;
    source.scale_percent = 75.0;
    source.first_page_number = 7;
    source.margin_top = 30.0;
    source.margin_bottom = 20.0;
    source.margin_left = 40.0;
    source.margin_right = 50.0;
    source.show_header = false;
    source.header_text = "stored header";
    source.header_align = 2;
    source.show_footer = true;
    source.footer_text = "stored footer";
    source.footer_align = 1;
    source.show_legend = true;
    source.legend_text = "stored legend";

    PageSetupDialog dialog(source);
    dialog.show();
    QApplication::processEvents();

    gate::check(dialog.portrait_radio()->isChecked() &&
                    !dialog.landscape_radio()->isChecked(),
                "a portrait source selects Portrait");
    gate::check(dialog.paper_combo()->count() == 2 &&
                    dialog.paper_combo()->currentIndex() == 1,
                "a Letter source selects Letter");
    gate::check(dialog.scale_spin()->value() == 75 &&
                    dialog.first_page_spin()->value() == 7,
                "the source's scale and first page number are shown");
    gate::check(dialog.margin_top_spin()->value() == 30.0 &&
                    dialog.margin_bottom_spin()->value() == 20.0 &&
                    dialog.margin_left_spin()->value() == 40.0 &&
                    dialog.margin_right_spin()->value() == 50.0,
                "the four source margins are shown");
    gate::check(!dialog.show_header_box()->isChecked() &&
                    dialog.header_text_edit()->text() == QStringLiteral("stored header") &&
                    dialog.header_align_combo()->currentIndex() == 2,
                "the source's header switch, text and alignment are shown");
    gate::check(dialog.show_footer_box()->isChecked() &&
                    dialog.footer_text_edit()->text() == QStringLiteral("stored footer") &&
                    dialog.footer_align_combo()->currentIndex() == 1,
                "the source's footer switch, text and alignment are shown");
    gate::check(dialog.show_legend_box()->isChecked() &&
                    dialog.legend_text_edit()->text() == QStringLiteral("stored legend"),
                "the source's legend switch and title are shown");
    gate::check(dialog.margin_top_spin()->suffix() == QStringLiteral(" pt") &&
                    dialog.margin_top_spin()->decimals() == 1 &&
                    dialog.margin_top_spin()->maximum() == 200.0,
                "the margin fields are the measured point fields");

    // The inert controls carry decision 9's exact tooltip.
    gate::check(!dialog.options_button()->isEnabled() &&
                    dialog.options_button()->toolTip() == kTip,
                "Options is disabled with the exact tooltip");
    gate::check(!dialog.fit_radio()->isEnabled() &&
                    dialog.fit_radio()->toolTip() == kTip &&
                    !dialog.fit_wide_spin()->isEnabled() &&
                    !dialog.fit_tall_spin()->isEnabled(),
                "Fit to is disabled with the exact tooltip");
    gate::check(!dialog.print_notes_box()->isEnabled() &&
                    dialog.print_notes_box()->toolTip() == kTip &&
                    !dialog.print_blank_box()->isEnabled() &&
                    dialog.print_blank_box()->toolTip() == kTip,
                "the View tab's print options are disabled with the exact tooltip");
    gate::check(dialog.ok_button()->isEnabled() &&
                    dialog.cancel_button()->isEnabled() &&
                    dialog.print_button()->isEnabled() &&
                    dialog.preview_button()->isEnabled(),
                "OK, Cancel, Print and Print Preview are live");
    gate::check(dialog.page_note_label()->text().contains(
                    QStringLiteral("Fit to needs")) &&
                    dialog.view_note_label()->text().contains(
                        QStringLiteral("no engine in this port")),
                "the two inert areas carry their honest inline notes");
    gate::check(dialog.header_text_edit()->placeholderText().contains(
                    QStringLiteral("automatic")) &&
                    dialog.footer_text_edit()->placeholderText().contains(
                        QStringLiteral("Page p of P")),
                "the empty text fields say the automatic text is used");

    // Drive every functional field, then OK.
    dialog.landscape_radio()->setChecked(true);
    dialog.paper_combo()->setCurrentIndex(0);
    dialog.scale_spin()->setValue(50);
    dialog.first_page_spin()->setValue(5);
    dialog.margin_top_spin()->setValue(48.0);
    dialog.margin_bottom_spin()->setValue(36.0);
    dialog.margin_left_spin()->setValue(40.0);
    dialog.margin_right_spin()->setValue(44.0);
    dialog.show_header_box()->setChecked(true);
    dialog.header_text_edit()->setText(QStringLiteral("&d plan &p/&P &&"));
    dialog.header_align_combo()->setCurrentIndex(1);
    dialog.show_footer_box()->setChecked(true);
    dialog.footer_text_edit()->setText(QStringLiteral("&d page &p"));
    dialog.footer_align_combo()->setCurrentIndex(0);
    dialog.show_legend_box()->setChecked(true);
    dialog.legend_text_edit()->setText(QStringLiteral("Key"));

    gate::check(dialog.spec().scale_percent == 75.0,
                "editing the widgets alone does not change spec()");
    gate::check(!dialog.print_requested() && !dialog.preview_requested(),
                "plain OK asks for neither Print nor Print Preview");
    dialog.ok_button()->click();
    gate::check(dialog.result() == QDialog::Accepted, "OK accepts the dialog");
    const app::PageSpec edited = dialog.spec();
    gate::check(edited.width == 842.0 && edited.height == 595.0,
                "A4 landscape is the shared A4 preset");
    gate::check(edited.scale_percent == 50.0 && edited.first_page_number == 5,
                "OK keeps the edited scale and first page number");
    gate::check(edited.margin_top == 48.0 && edited.margin_bottom == 36.0 &&
                    edited.margin_left == 40.0 && edited.margin_right == 44.0,
                "OK keeps all four edited margins");
    gate::check(edited.show_header && edited.header_text == "&d plan &p/&P &&" &&
                    edited.header_align == 1,
                "OK keeps the header switch, text and alignment");
    gate::check(edited.show_footer && edited.footer_text == "&d page &p" &&
                    edited.footer_align == 0,
                "OK keeps the footer switch, text and alignment");
    gate::check(edited.show_legend && edited.legend_text == "Key",
                "OK keeps the legend switch and title");

    // Cancel throws the edits away.
    PageSetupDialog cancelled(source);
    cancelled.show();
    QApplication::processEvents();
    cancelled.scale_spin()->setValue(60);
    cancelled.cancel_button()->click();
    gate::check(cancelled.result() == QDialog::Rejected, "Cancel rejects the dialog");
    gate::check(cancelled.spec().scale_percent == 75.0,
                "Cancel leaves the source spec untouched");
    gate::check(!cancelled.print_requested() && !cancelled.preview_requested(),
                "Cancel asks for no command");

    // Print... and Print Preview... accept the values first and flag the command.
    PageSetupDialog printing(source);
    printing.show();
    QApplication::processEvents();
    printing.first_page_spin()->setValue(9);
    printing.print_button()->click();
    gate::check(printing.result() == QDialog::Accepted && printing.print_requested() &&
                    !printing.preview_requested(),
                "Print accepts the edited values and asks for Print");
    gate::check(printing.spec().first_page_number == 9,
                "Print's accepted spec carries the edit");

    PageSetupDialog previewing(source);
    previewing.show();
    QApplication::processEvents();
    previewing.scale_spin()->setValue(80);
    previewing.preview_button()->click();
    gate::check(previewing.result() == QDialog::Accepted &&
                    previewing.preview_requested() && !previewing.print_requested(),
                "Print Preview accepts the edited values and asks for the preview");
    gate::check(previewing.spec().scale_percent == 80.0,
                "Print Preview's accepted spec carries the edit");
}

// ---------------------------------------------------------------------------
// The shell: Page Setup -> MainWindow -> the shared print layout -> preview
// ---------------------------------------------------------------------------
bool pixmap_matches(QLabel* label, const QImage& want) {
    const QPixmap pixmap = label->pixmap();
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
}

bool images_differ(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) {
        return true;
    }
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixelColor(x, y) != b.pixelColor(x, y)) {
                return true;
            }
        }
    }
    return false;
}

void check_print_layout(const QString& fixture) {
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();
    QString error;
    gate::check(window.open_file(fixture, &error),
                "the Page Setup fixture opens in the shell");
    if (window.sheet() == nullptr) {
        gate::check(false, "the Page Setup gate needs an open document");
        return;
    }
    gate::check(window.backstage() != nullptr &&
                    static_cast<bool>(window.backstage()->page_setup_handler()),
                "the Print page's Page Setup button is wired to the shell");

    // The default PageSpec keeps the pre-wave layout exactly: A4 landscape,
    // 24 pt margins, Gantt's 35 rows per page.
    const app::PageSpec base = window.page_setup();
    gate::check(base.width == 842.0 && base.height == 595.0 &&
                    base.margin_top == 24.0 && base.margin_bottom == 24.0 &&
                    base.margin_left == 24.0 && base.margin_right == 24.0 &&
                    base.scale_percent == 100.0 && base.show_header &&
                    base.show_footer && !base.show_legend &&
                    base.first_page_number == 1,
                "the shell starts on the unchanged default PageSpec");
    const app::PrintJob base_job =
        window.current_print_job(window.print_spec(true, false));
    gate::check(base_job.spec.width == 842.0 && base_job.spec.height == 595.0,
                "the default print job is A4 landscape");
    gate::check(base_job.layout.spec.width == 842.0 &&
                    base_job.layout.spec.height == 595.0,
                "100 % scaling leaves the virtual page equal to the paper");
    gate::check(base_job.layout.rows_per_page == 35,
                "the default Gantt page still holds the pre-wave 35 rows");
    gate::check(!base_job.pages.empty(), "the default job has a page");
    if (base_job.pages.empty()) {
        return;
    }
    gate::check(base_job.pages.front().footer.find("Page 1 of ") != std::string::npos &&
                    base_job.pages.back().footer.find(
                        "Page " + std::to_string(base_job.pages.size()) + " of " +
                        std::to_string(base_job.pages.size())) != std::string::npos,
                "the automatic footer numbers the pages from 1 of N");
    std::string date_text;
    {
        const std::string& footer = base_job.pages.front().footer;
        const std::string separator = "  |  ";
        const std::size_t at = footer.find(separator);
        if (at != std::string::npos) {
            date_text = footer.substr(0, at);
        }
    }
    gate::check(!date_text.empty(),
                "the automatic footer carries the caller's date text");

    // The live Print page and its preview.
    msqt::Backstage* backstage = window.backstage();
    window.show_backstage(true);
    backstage->show_page(msqt::Backstage::Page::print);
    QApplication::processEvents();
    QApplication::processEvents();
    QApplication::processEvents();
    QLabel* preview = backstage->print_preview();
    gate::check(preview != nullptr && preview->width() >= 40 && preview->height() >= 40,
                "the Print page's preview pane is laid out");
    if (preview == nullptr) {
        return;
    }
    gate::check(window.current_print_job(window.print_spec(
                    backstage->print_landscape(), backstage->print_paper_letter()))
                        .spec.width == 842.0,
                "the preview provider and print_plan share print_spec()");
    gate::check(pixmap_matches(preview,
                               msqt::render_print_page(
                                   base_job, 0, *window.sheet(), window.plan(),
                                   window.preferences(), preview->size())),
                "the preview pane is the shared layout's default page");
    const QImage base_image = preview->pixmap().toImage();

    // The real path: the backstage button opens the modal, the gate edits it,
    // OK applies the PageSpec and syncs the Print page's orientation/paper.
    bool saw_dialog = false;
    QString dialog_title;
    QTimer::singleShot(0, [&saw_dialog, &dialog_title]() {
        auto* opened =
            dynamic_cast<PageSetupDialog*>(QApplication::activeModalWidget());
        if (opened == nullptr) {
            return;
        }
        saw_dialog = true;
        dialog_title = opened->windowTitle();
        opened->portrait_radio()->setChecked(true);
        opened->paper_combo()->setCurrentIndex(1);
        opened->scale_spin()->setValue(50);
        opened->first_page_spin()->setValue(5);
        opened->margin_top_spin()->setValue(48.0);
        opened->margin_bottom_spin()->setValue(36.0);
        opened->margin_left_spin()->setValue(40.0);
        opened->margin_right_spin()->setValue(44.0);
        opened->show_header_box()->setChecked(true);
        opened->header_text_edit()->setText(QStringLiteral("&d plan &p/&P &&"));
        opened->header_align_combo()->setCurrentIndex(1);
        opened->show_footer_box()->setChecked(true);
        opened->footer_text_edit()->setText(QStringLiteral("&d page &p"));
        opened->footer_align_combo()->setCurrentIndex(0);
        opened->show_legend_box()->setChecked(true);
        opened->legend_text_edit()->setText(QStringLiteral("Key"));
        opened->ok_button()->click();
    });
    backstage->print_page_setup_button()->click();
    QApplication::processEvents();
    QApplication::processEvents();
    gate::check(saw_dialog,
                "the Print page's Page Setup button opens the Page Setup dialog");
    gate::check(dialog_title == QStringLiteral("Page Setup - Gantt Chart"),
                "the button opens the captured dialog title");

    const app::PageSpec applied = window.page_setup();
    gate::check(applied.width == 612.0 && applied.height == 792.0,
                "OK applies the edited Letter portrait paper");
    gate::check(applied.scale_percent == 50.0 && applied.first_page_number == 5,
                "OK applies the edited scale and first page number");
    gate::check(applied.margin_top == 48.0 && applied.margin_bottom == 36.0 &&
                    applied.margin_left == 40.0 && applied.margin_right == 44.0,
                "OK applies all four edited margins");
    gate::check(applied.show_header && applied.show_footer && applied.show_legend &&
                    applied.header_text == "&d plan &p/&P &&" &&
                    applied.footer_text == "&d page &p" &&
                    applied.legend_text == "Key",
                "OK applies the edited header, footer and legend text");
    gate::check(!backstage->print_landscape() && backstage->print_paper_letter(),
                "OK syncs the Print page's orientation and paper controls");

    // The shared layout read back from the shell's spec.
    const app::PrintJob job = window.current_print_job(window.print_spec(false, true));
    gate::check(job.spec.width == 612.0 && job.spec.height == 792.0,
                "the applied job is Letter portrait");
    gate::check(job.spec.scale_percent == 50.0 &&
                    job.spec.first_page_number == 5 &&
                    job.spec.header_align == 1 && job.spec.footer_align == 0,
                "the applied job carries the scaling and both alignments");
    gate::check_near(job.layout.spec.width, 1140.0, 1e-6,
                     "50 % scaling widens the virtual page to 1140 pt");
    gate::check_near(job.layout.spec.height, 1500.0, 1e-6,
                     "50 % scaling lengthens the virtual page to 1500 pt");
    // The bands are virtual-page points (build_print_job lays out
    // layout_spec()); the painter scales them back onto the 612x792 paper, so
    // the footer rule at 1440 virtual paints at 48 + (1440 - 48) / 2 = 744,
    // which is the physical bottom margin minus the 24 pt band scaled to 12.
    gate::check_near(job.layout.body_top, 83.0, 1e-6,
                     "the 48 pt top margin puts the body at 83 virtual pt");
    gate::check_near(job.layout.footer_rule_y, 1440.0, 1e-6,
                     "the bottom margin puts the virtual footer rule at 1440 pt");
    gate::check_near(job.layout.legend_top, 1410.0, 1e-6,
                     "the virtual legend band starts at 1410 pt");
    gate::check_near(job.layout.body_bottom, 1406.0, 1e-6,
                     "the legend leaves the virtual body 1406 pt at the bottom");
    gate::check(job.layout.rows_per_page == 99,
                "the 50 % virtual page holds 99 Gantt rows");

    // Every page's header/footer is the applied pattern expanded with the
    // applied first page number and the job's real last page number.
    const std::size_t first = 5;
    const std::size_t last = first + job.pages.size() - 1;
    bool headers_ok = !job.pages.empty();
    bool footers_ok = !job.pages.empty();
    for (std::size_t page = 0; page < job.pages.size(); ++page) {
        headers_ok =
            headers_ok &&
            job.pages[page].header ==
                app::expand_print_text("&d plan &p/&P &&", date_text, first + page, last);
        footers_ok = footers_ok &&
                     job.pages[page].footer ==
                         app::expand_print_text("&d page &p", date_text, first + page,
                                                last);
    }
    gate::check(headers_ok,
                "every page's custom header expands &p/&P with the first page number");
    gate::check(footers_ok, "every page's custom footer expands &p with its page number");
    gate::check(job.pages.size() ==
                    app::page_count(job.rows.size(), job.layout.rows_per_page),
                "the applied job paginates at the scaled rows-per-page");

    // The preview re-rendered from the SAME applied spec and differs from the
    // default preview (margins, orientation and legend all moved).
    gate::check(pixmap_matches(preview,
                               msqt::render_print_page(
                                   job, 0, *window.sheet(), window.plan(),
                                   window.preferences(), preview->size())),
                "the Print preview is the applied PageSpec's shared page");
    const QImage applied_image = preview->pixmap().toImage();
    gate::check(images_differ(base_image, applied_image),
                "the preview visibly changed with the applied Page Setup");

    window.show_backstage(false);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_page_setup_gate_tests <w29-page-setup.measures.json> "
                     "<fixture.mpp>\n");
        return 2;
    }
    const QJsonObject measures = load_measures(QString::fromLocal8Bit(argv[1]));
    gate::check(!measures.isEmpty(), "the Page Setup measures file parses");
    const QString fixture = QString::fromLocal8Bit(argv[2]);

    check_measured_geometry(measures);
    check_initial_values_and_edit();
    check_print_layout(fixture);

    std::printf("qt_page_setup_gate: ");
    return gate::finish();
}
