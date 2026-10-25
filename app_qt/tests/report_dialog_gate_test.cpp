// app_qt/tests/report_dialog_gate_test.cpp -- wave 29 G4's gate for the Report
// tab's two dialogs (W29-4.1).
//
// The measured source is the local, git-ignored capture of Project 2021's
// Visual Reports - Create Report and Compare Project Versions dialogs.  The
// numbers and captured UI strings the gate compares against are committed in
// tests/data/reference/w29-report-dialogs.measures.json (client-relative
// rectangles of every control, the two client sizes, the tab/list/combo
// contents and the captured labels).  Nothing here reads the port's own
// constants for a measured value: the JSON is parsed and the LIVE widget tree
// is measured.  The live widget for each captured control is found by the
// `measuredControlId` dynamic property the dialogs set, so a rectangle cannot
// drift onto a different control.
//
// The functional half proves the decision-9 contract: every current-affecting
// control is disabled with the exact tooltip and Close/Cancel really dismisses;
// and it drives the real shell path -- the Report tab's two enabled ribbon
// buttons open their modal dialogs, the first carries the opened file's name in
// its captured caption, and both close with a normal Cancel/Close.
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <cstdio>
#include <string>

#include "gate_util.h"
#include "main_window.h"  // configure_application()
#include "report_dialogs_qt.h"
#include "ribbon_bar.h"

namespace {

using msqt::CompareProjectsDialog;
using msqt::ReportTabStrip;
using msqt::VisualReportsDialog;
using msqt::report_dialog_geometry::kNotImplemented;

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

// The live widget's own caption, whichever class carries it.  Only the classes
// a captured caption can live on are read, so the gate cannot "match" a label
// against, say, a frame's empty text.
QString live_text(QWidget* widget) {
    if (auto* group = qobject_cast<QGroupBox*>(widget)) {
        return group->title();
    }
    if (auto* label = qobject_cast<QLabel*>(widget)) {
        return label->text();
    }
    if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
        return button->text();
    }
    return {};
}

// Every measured rectangle and every measured caption, then the reverse
// direction: every marked live widget is in the measures file, and every
// captured-but-not-painted id has no live widget at all.
void check_controls(QDialog* dialog, const QJsonObject& section,
                    const QString& name_argument) {
    const QJsonArray controls = section.value(QStringLiteral("controls")).toArray();
    gate::check(!controls.isEmpty(), "the measures file carries the captured controls");
    QSet<int> measured_ids;
    for (const QJsonValue& value : controls) {
        const QJsonObject control = value.toObject();
        const int id = control.value(QStringLiteral("id")).toInt();
        const QString name = control.value(QStringLiteral("name")).toString();
        measured_ids.insert(id);
        QWidget* live = live_for_id(dialog, id);
        gate::check(live != nullptr,
                    "the captured control id names exactly one live widget");
        check_rect(live, control.value(QStringLiteral("rect")).toArray(), name, dialog);
        if (live == nullptr) {
            continue;
        }
        if (!control.contains(QStringLiteral("text"))) {
            continue;
        }
        // The port's inline note replaces the captured blank status static:
        // Project's placeholder text ("Text") is not a caption to reproduce.
        if (name == QStringLiteral("status_note")) {
            continue;
        }
        QString expected = control.value(QStringLiteral("text")).toString();
        if (name == QStringLiteral("compare_group")) {
            expected = expected.arg(name_argument);
        }
        const std::string what =
            "the live caption is the capture's for '" + name.toStdString() + "'";
        gate::check(live_text(live) == expected, what.c_str());
    }

    QSet<int> live_ids;
    for (QWidget* widget : dialog->findChildren<QWidget*>()) {
        const QVariant value = widget->property(kMeasuredId);
        if (value.isValid()) {
            live_ids.insert(value.toInt());
        }
    }
    gate::check(live_ids == measured_ids,
                "the marked live controls are exactly the captured set");

    for (const QJsonValue& value :
         section.value(QStringLiteral("not_painted")).toArray()) {
        const int id = value.toObject().value(QStringLiteral("id")).toInt();
        gate::check(live_for_id(dialog, id) == nullptr,
                    "a captured control the port does not paint has no live widget");
    }
}

// Every interactive direct child except the dismiss button is disabled with
// decision 9's exact tooltip.  Direct children only: a combo's internal popup
// view is not a project control.
void check_disabled_with_tip(QDialog* dialog, QWidget* dismiss_a, int expected,
                             QWidget* dismiss_b = nullptr) {
    int swept = 0;
    for (QWidget* widget :
         dialog->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (widget == dismiss_a || widget == dismiss_b) {
            continue;
        }
        const bool interactive = qobject_cast<QAbstractButton*>(widget) != nullptr ||
                                 qobject_cast<QComboBox*>(widget) != nullptr ||
                                 qobject_cast<QLineEdit*>(widget) != nullptr ||
                                 qobject_cast<QAbstractItemView*>(widget) != nullptr ||
                                 dynamic_cast<ReportTabStrip*>(widget) != nullptr;
        if (!interactive) {
            continue;
        }
        ++swept;
        gate::check(!widget->isEnabled(),
                    "a current-affecting control is disabled in this inert dialog");
        gate::check(widget->toolTip() == kTip,
                    "a disabled control carries the exact decision-9 tooltip");
    }
    gate::check(swept == expected,
                "the sweep found exactly the dialog's current-affecting controls");
    for (QWidget* dismiss : {dismiss_a, dismiss_b}) {
        if (dismiss == nullptr) {
            continue;
        }
        gate::check(dismiss->isEnabled(), "the dismiss button is live");
        gate::check(dismiss->toolTip() != kTip,
                    "the live dismiss button does not carry the decision-9 tooltip");
    }
}

// ---------------------------------------------------------------------------
// Visual Reports - Create Report
// ---------------------------------------------------------------------------
void check_visual_reports(const QJsonObject& measures) {
    const QJsonObject spec = measures.value(QStringLiteral("visual_reports")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Visual Reports");

    VisualReportsDialog dialog;
    dialog.show();
    QApplication::processEvents();

    gate::check(dialog.windowTitle() ==
                    spec.value(QStringLiteral("title")).toString(),
                "Visual Reports has the captured title");
    gate::check(dialog.width() == spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() ==
                        spec.value(QStringLiteral("height")).toInt(),
                "Visual Reports has the measured 625x541 client");
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "Visual Reports is non-resizable");

    check_controls(&dialog, spec, QString());

    // The captured two-row tab strip.
    const QJsonObject tabs = spec.value(QStringLiteral("tabs")).toObject();
    ReportTabStrip* strip = dialog.tabs();
    gate::check(strip != nullptr && strip->tab_count() ==
                                        tabs.value(QStringLiteral("count")).toInt(),
                "the tab strip has the captured seven items");
    if (strip != nullptr) {
        const QJsonArray rows = tabs.value(QStringLiteral("rows")).toArray();
        const QJsonArray top = rows.at(0).toArray();
        const QJsonArray bottom = rows.at(1).toArray();
        QStringList top_labels;
        for (const QJsonValue& value : top) {
            top_labels << value.toString();
        }
        QStringList bottom_labels;
        for (const QJsonValue& value : bottom) {
            bottom_labels << value.toString();
        }
        gate::check(strip->top_row() == top_labels &&
                        strip->bottom_row() == bottom_labels,
                    "the two painted rows are the capture's visible layout");
        const QJsonArray labels = tabs.value(QStringLiteral("labels")).toArray();
        const int current = tabs.value(QStringLiteral("current")).toInt();
        gate::check(strip->selected_label() == labels.at(current).toString(),
                    "the captured item is the selected tab");
    }

    // The captured template list.
    const QJsonObject template_spec =
        spec.value(QStringLiteral("template_list")).toObject();
    QListWidget* list = dialog.template_list();
    gate::check(list != nullptr &&
                    list->count() ==
                        template_spec.value(QStringLiteral("count")).toInt(),
                "the template list has the captured ten reports");
    if (list != nullptr) {
        const QJsonArray items = template_spec.value(QStringLiteral("items")).toArray();
        bool texts_ok = list->count() == items.size();
        for (int index = 0; index < list->count() && index < items.size(); ++index) {
            texts_ok = texts_ok &&
                       list->item(index)->text() == items.at(index).toString();
        }
        gate::check(texts_ok, "the template names are the capture's, in order");
        gate::check(list->currentRow() ==
                        template_spec.value(QStringLiteral("current")).toInt(),
                    "the first template is selected as captured");
    }

    // The captured combo shape and visible text (the unseen item strings are
    // owner-drawn and were unreadable).
    const QJsonObject level = spec.value(QStringLiteral("level_combo")).toObject();
    QComboBox* combo = dialog.level_combo();
    gate::check(combo != nullptr &&
                    combo->count() == level.value(QStringLiteral("count")).toInt() &&
                    combo->currentIndex() ==
                        level.value(QStringLiteral("current")).toInt() &&
                    combo->currentText() ==
                        level.value(QStringLiteral("text")).toString(),
                "the usage-level combo has the captured count, index and text");

    const QJsonObject checked = spec.value(QStringLiteral("checked")).toObject();
    gate::check(dialog.excel_box()->isChecked() ==
                    checked.value(QStringLiteral("excel")).toBool() &&
                    dialog.visio_box()->isChecked() ==
                        checked.value(QStringLiteral("visio")).toBool() &&
                    dialog.include_box()->isChecked() ==
                        checked.value(QStringLiteral("include")).toBool(),
                "the three captured check-box states are reproduced");

    // The primary actions are disabled with the exact tooltip, and Close really
    // closes.
    gate::check(!dialog.view_button()->isEnabled() &&
                    dialog.view_button()->toolTip() == kTip,
                "View is disabled with the exact decision-9 tooltip");
    check_disabled_with_tip(&dialog, dialog.close_button(), 14);

    gate::check(dialog.note_label() != nullptr &&
                    dialog.note_label()->text().contains(
                        QStringLiteral("not generated in this port")),
                "the inert dialog says so in its inline note");

    dialog.close_button()->click();
    gate::check(!dialog.isVisible() && dialog.result() == QDialog::Rejected,
                "Close rejects and dismisses the dialog");
}

// ---------------------------------------------------------------------------
// Compare Project Versions
// ---------------------------------------------------------------------------
void check_compare_projects(const QJsonObject& measures, const QString& project_name) {
    const QJsonObject spec =
        measures.value(QStringLiteral("compare_projects")).toObject();
    gate::check(!spec.isEmpty(), "the measures file carries Compare Projects");

    CompareProjectsDialog dialog(project_name);
    dialog.show();
    QApplication::processEvents();

    gate::check(dialog.windowTitle() ==
                    spec.value(QStringLiteral("title")).toString(),
                "Compare Projects has the captured title");
    gate::check(dialog.width() == spec.value(QStringLiteral("width")).toInt() &&
                    dialog.height() ==
                        spec.value(QStringLiteral("height")).toInt(),
                "Compare Projects has the measured 469x251 client");
    gate::check(dialog.minimumSize() == dialog.maximumSize(),
                "Compare Projects is non-resizable");

    check_controls(&dialog, spec, project_name);

    gate::check(dialog.compare_group()->title() ==
                    spec.value(QStringLiteral("group_text"))
                        .toString()
                        .arg(project_name),
                "the group caption carries the project name in Project's pattern");

    const auto check_combo = [&](QComboBox* combo, const char* key, const char* what) {
        const QJsonObject want = spec.value(QLatin1String(key)).toObject();
        gate::check(combo != nullptr &&
                        combo->count() == want.value(QStringLiteral("count")).toInt() &&
                        combo->currentIndex() ==
                            want.value(QStringLiteral("current")).toInt() &&
                        combo->currentText() ==
                            want.value(QStringLiteral("text")).toString(),
                    what);
    };
    check_combo(dialog.previous_combo(), "previous_combo",
                "the previous-version combo is the captured empty combo");
    check_combo(dialog.task_table_combo(), "task_table_combo",
                "the task-table combo has the captured count, index and text");
    check_combo(dialog.resource_table_combo(), "resource_table_combo",
                "the resource-table combo has the captured count, index and text");

    gate::check(!dialog.ok_button()->isEnabled() &&
                    dialog.ok_button()->toolTip() == kTip,
                "OK is disabled with the exact decision-9 tooltip");
    check_disabled_with_tip(&dialog, dialog.cancel_button(), 5);

    gate::check(dialog.note_label() != nullptr &&
                    dialog.note_label()->text().contains(
                        QStringLiteral("not implemented in this port")),
                "the inert dialog says so in its inline note");

    dialog.cancel_button()->click();
    gate::check(!dialog.isVisible() && dialog.result() == QDialog::Rejected,
                "Cancel rejects and dismisses the dialog");
}

// ---------------------------------------------------------------------------
// The shell: the Report tab's enabled buttons open the real modals
// ---------------------------------------------------------------------------
QWidget* ribbon_control_by_label(msqt::MainWindow& window, const QString& tab,
                                 const QString& group, const QString& label) {
    msqt::RibbonBar* ribbon = window.ribbon();
    if (ribbon == nullptr) {
        return nullptr;
    }
    ribbon->set_active_tab(ribbon->tab_titles().indexOf(tab));
    QApplication::processEvents();
    const int group_index = ribbon->group_titles().indexOf(group);
    if (group_index < 0) {
        return nullptr;
    }
    for (int control = 0; control < ribbon->group_control_count(group_index); ++control) {
        QWidget* live = ribbon->group_control(group_index, control);
        if (live == nullptr) {
            continue;
        }
        QString text = live->accessibleName();
        if (auto* button = qobject_cast<QAbstractButton*>(live)) {
            text = button->text().isEmpty() ? button->accessibleName() : button->text();
        }
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (text.simplified() == label) {
            return live;
        }
    }
    return nullptr;
}

void check_shell_wiring(const QJsonObject& measures, const QString& fixture) {
    const QString file_name = QFileInfo(fixture).fileName();
    msqt::MainWindow window;
    window.resize(1400, 900);
    window.show();
    QString error;
    gate::check(window.open_file(fixture, &error),
                "the fixture opens for the Report dialog gate");
    QApplication::processEvents();

    // Compare Projects: the Report tab's Project group button opens the modal,
    // which carries the opened file's name in its captured group caption.
    struct CompareSeen {
        bool seen = false;
        QString title;
        QString group_title;
        bool ok_disabled = false;
        QString ok_tip;
    } compare;
    QWidget* compare_button =
        ribbon_control_by_label(window, QStringLiteral("Report"),
                                QStringLiteral("Project"),
                                QStringLiteral("Compare Projects"));
    gate::check(compare_button != nullptr,
                "the Report tab's Compare Projects button is live");
    if (compare_button != nullptr) {
        QTimer::singleShot(0, [&compare]() {
            QWidget* modal = QApplication::activeModalWidget();
            auto* dialog = dynamic_cast<CompareProjectsDialog*>(modal);
            if (dialog != nullptr) {
                compare.seen = true;
                compare.title = dialog->windowTitle();
                compare.group_title = dialog->compare_group()->title();
                compare.ok_disabled = !dialog->ok_button()->isEnabled();
                compare.ok_tip = dialog->ok_button()->toolTip();
                dialog->cancel_button()->click();
            }
            if (modal != nullptr && modal->isVisible()) {
                modal->close();
            }
        });
        auto* button = qobject_cast<QAbstractButton*>(compare_button);
        gate::check(button != nullptr && button->isEnabled(),
                    "the Compare Projects button is enabled");
        if (button != nullptr) {
            button->click();
        }
        QApplication::processEvents();
    }
    gate::check(compare.seen, "the Compare Projects button opens the dialog");
    gate::check(compare.title ==
                    measures.value(QStringLiteral("compare_projects"))
                        .toObject()
                        .value(QStringLiteral("title"))
                        .toString(),
                "the shell opens the captured Compare Projects title");
    gate::check(compare.group_title ==
                    measures.value(QStringLiteral("compare_projects"))
                        .toObject()
                        .value(QStringLiteral("group_text"))
                        .toString()
                        .arg(file_name),
                "the open file's name fills the captured compare caption");
    gate::check(compare.ok_disabled && compare.ok_tip == kTip,
                "OK stays disabled with the exact tooltip inside the shell");

    // Visual Reports: the Export group's button opens its modal.
    struct ReportsSeen {
        bool seen = false;
        QString title;
        int templates = 0;
        bool view_disabled = false;
        QString view_tip;
    } reports;
    QWidget* reports_button =
        ribbon_control_by_label(window, QStringLiteral("Report"),
                                QStringLiteral("Export"),
                                QStringLiteral("Visual Reports"));
    gate::check(reports_button != nullptr,
                "the Report tab's Visual Reports button is live");
    if (reports_button != nullptr) {
        QTimer::singleShot(0, [&reports]() {
            QWidget* modal = QApplication::activeModalWidget();
            auto* dialog = dynamic_cast<VisualReportsDialog*>(modal);
            if (dialog != nullptr) {
                reports.seen = true;
                reports.title = dialog->windowTitle();
                reports.templates =
                    dialog->template_list() != nullptr ? dialog->template_list()->count()
                                                       : -1;
                reports.view_disabled = !dialog->view_button()->isEnabled();
                reports.view_tip = dialog->view_button()->toolTip();
                dialog->close_button()->click();
            }
            if (modal != nullptr && modal->isVisible()) {
                modal->close();
            }
        });
        auto* button = qobject_cast<QAbstractButton*>(reports_button);
        gate::check(button != nullptr && button->isEnabled(),
                    "the Visual Reports button is enabled");
        if (button != nullptr) {
            button->click();
        }
        QApplication::processEvents();
    }
    gate::check(reports.seen, "the Visual Reports button opens the dialog");
    gate::check(reports.title ==
                    measures.value(QStringLiteral("visual_reports"))
                        .toObject()
                        .value(QStringLiteral("title"))
                        .toString(),
                "the shell opens the captured Visual Reports title");
    gate::check(reports.templates ==
                    measures.value(QStringLiteral("visual_reports"))
                        .toObject()
                        .value(QStringLiteral("template_list"))
                        .toObject()
                        .value(QStringLiteral("count"))
                        .toInt(),
                "the shell's dialog carries the captured template list");
    gate::check(reports.view_disabled && reports.view_tip == kTip,
                "View stays disabled with the exact tooltip inside the shell");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    msqt::configure_application();

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_report_dialog_gate_tests "
                     "<w29-report-dialogs.measures.json> <fixture.mpp>\n");
        return 2;
    }
    const QJsonObject measures = load_measures(QString::fromLocal8Bit(argv[1]));
    gate::check(!measures.isEmpty(), "the Report dialog measures file parses");
    const QString fixture = QString::fromLocal8Bit(argv[2]);

    check_visual_reports(measures);
    check_compare_projects(measures, QFileInfo(fixture).fileName());
    check_shell_wiring(measures, fixture);

    std::printf("qt_report_dialog_gate: ");
    return gate::finish();
}
