// app_qt/tests/data_commands_test.cpp -- the View tab's Data group through the
// real PlanDocument (wave 31, G1).
//
// W31-1.1 (Sort): the Sort dialog's definition reaches the DISPLAYED row list
// and the Gantt, and nowhere else.  On wave23-outline.mpp -- six tasks in ID
// order, outline numbers 1, 1.1, 1.1.1, 1.1.2, 1.2, 2 -- sorting by ID
// descending must reorder the top level to IDs {6,1}, keep each child beneath
// its parent and sort only siblings (inside ID 1: {5,2}; inside ID 2's two
// children: {4,3}), leave the RecordSet in ID order, keep the Gantt's bars in
// display-row order, and leave the file's bytes untouched.  Clear Sort
// restores the file's order.
//
// W31-1.2 (Outline): View > Outline > Level N shows only rows whose outline
// level is <= N (the sheet's 0-based port levels, so Level 1 collapses every
// port-level-0 summary) and All Subtasks expands everything.  Both change the
// sheet's collapse state only: the RecordSet and the file's bytes stay as they
// were, and an active sort still orders the rows that remain visible.
//
// The table cases are added by their own commit (W31-1.3).
//
// Usage: qt_data_commands <wave23-outline.mpp>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gantt.h"
#include "layout_model.h"
#include "plan_document.h"
#include "sheet_table_model.h"
#include "sort_model.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

void check_uids(const std::vector<std::uint32_t>& got, const std::vector<std::uint32_t>& want,
                const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s: got", what);
        for (const std::uint32_t uid : got) {
            std::fprintf(stderr, " %u", uid);
        }
        std::fprintf(stderr, ", want");
        for (const std::uint32_t uid : want) {
            std::fprintf(stderr, " %u", uid);
        }
        std::fprintf(stderr, "\n");
    }
}

std::vector<std::uint32_t> record_uids(const msqt::PlanDocument& document) {
    std::vector<std::uint32_t> uids;
    for (std::size_t row = 0; row < document.records().row_count(); ++row) {
        uids.push_back(document.records().uid(row));
    }
    return uids;
}

std::vector<std::uint32_t> display_uids(const msqt::PlanDocument& document) {
    std::vector<std::uint32_t> uids;
    for (const std::size_t row : document.display_rows()) {
        uids.push_back(document.records().uid(row));
    }
    return uids;
}

std::vector<std::uint32_t> row_map_uids(const msqt::PlanDocument& document) {
    return document.rows().uids;
}

std::vector<std::uint32_t> bar_uids(const msqt::PlanDocument& document) {
    std::vector<std::uint32_t> uids;
    for (const core::gantt::Bar& bar : document.gantt().bars) {
        uids.push_back(bar.task_uid);
    }
    return uids;
}

QString file_hash(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray bytes = file.readAll();
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                                   .toHex());
}

app::SortBuild sort_by(const std::string& title, bool ascending) {
    const std::vector<app::SortField>& fields = app::sort_fields();
    app::SortChoice choice;
    choice.ascending = ascending;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (fields[index].title == title) {
            choice.field_index = index;
            break;
        }
    }
    return app::build_sort(fields, choice);
}

void case_sort(const QString& path) {
    const QString before = file_hash(path);
    check(!before.isEmpty(), "the outline fixture is readable");

    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "the outline fixture loads");
    if (!error.empty()) {
        std::fprintf(stderr, "note: load error: %s\n", error.c_str());
        return;
    }

    const std::vector<std::uint32_t> file_order = {1, 2, 3, 4, 5, 6};
    check_uids(record_uids(document), file_order, "the RecordSet is in ID order");
    check_uids(document.rows().uids, file_order, "the baseline display is in ID order");
    check_uids(display_uids(document), file_order, "the baseline display rows are 1..6");
    check_uids(bar_uids(document), file_order, "the baseline bars are in display order");

    const app::SortBuild descending = sort_by("ID", false);
    check(descending.ok, "the ID descending sort builds");
    document.set_sort(descending.definition, descending.description);
    check(document.sort_active(), "the document records the active sort");
    document.rebuild_views(app::ZoomLevel::week);

    const std::vector<std::uint32_t> sorted = {6, 1, 5, 2, 4, 3};
    check_uids(display_uids(document), sorted,
               "ID descending reorders roots and sorts only siblings");
    check_uids(document.rows().uids, sorted,
               "the Gantt RowMap follows the same sorted display order");
    check_uids(bar_uids(document), sorted,
               "the Gantt's bars are the sorted display rows, in order");
    for (std::size_t index = 0; index < document.gantt().bars.size(); ++index) {
        check(document.gantt().bars[index].row == static_cast<int>(index),
              "bar row N is display row N");
    }
    check_uids(record_uids(document), file_order,
               "sorting never reorders the RecordSet");
    check(file_hash(path) == before, "sorting never changes the file's bytes");

    // The shell's table adapter must draw the SAME list: MainWindow passes
    // filter_active() || sort_active() as "a display list is active", so a
    // sort-only document does not fall back to the sheet's file-order rows.
    msqt::SheetTableModel model;
    model.set_sheet(document.sheet());
    model.set_display_rows(document.display_rows(),
                           document.filter_active() || document.sort_active());
    check(model.rowCount() == 6, "the sorted table draws six rows");
    for (int index = 0; index < model.rowCount() && index < 6; ++index) {
        check(model.record_row(index) == document.display_rows()[static_cast<std::size_t>(index)],
              "table row N maps to sorted display row N");
    }
    check(model.record_row(1) == document.display_rows()[1],
          "the second table row is the sorted second row (uid 1)");

    document.clear_sort();
    document.rebuild_views(app::ZoomLevel::week);
    model.set_display_rows(document.display_rows(),
                           document.filter_active() || document.sort_active());
    check(model.record_row(1) == document.sheet()->visible_row(1),
          "with no sort or filter the adapter falls back to file order");
    check_uids(display_uids(document), file_order, "Clear Sort restores the file's order");
    check_uids(bar_uids(document), file_order, "Clear Sort restores the bars' order");
}

void case_outline(const QString& path) {
    const QString before = file_hash(path);
    check(!before.isEmpty(), "the outline fixture is readable");

    msqt::PlanDocument document;
    std::string error;
    check(document.load(path.toStdString(), app::ZoomLevel::week, error),
          "the outline fixture loads");
    if (!error.empty()) {
        std::fprintf(stderr, "note: load error: %s\n", error.c_str());
        return;
    }
    const std::vector<std::uint32_t> file_order = {1, 2, 3, 4, 5, 6};
    check_uids(display_uids(document), file_order, "the baseline shows all six rows");

    // Level 1 leaves the top-level tasks: uid 1 (1) and uid 6 (2).
    check(document.show_outline_level(1), "Level 1 is accepted");
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), {1, 6}, "Level 1 shows only the top-level rows");
    check_uids(document.rows().uids, {1, 6}, "the Gantt RowMap follows Level 1");
    check_uids(record_uids(document), file_order, "Level 1 never reorders the RecordSet");
    check(file_hash(path) == before, "Level 1 never changes the file's bytes");

    // Level 2 adds the children at port level 1: uid 2 (1.1) and uid 5 (1.2).
    check(document.show_outline_level(2), "Level 2 is accepted");
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), {1, 2, 5, 6}, "Level 2 adds the second level");
    check(file_hash(path) == before, "Level 2 never changes the file's bytes");

    // Level 3 keeps every row (the deepest task is at port level 2), and a level
    // beyond the outline's depth shows everything too.
    check(document.show_outline_level(3), "Level 3 is accepted");
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), file_order, "Level 3 shows every row");
    check(document.show_outline_level(9), "Level 9 is accepted");
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), file_order, "Level 9 shows every row");
    check(!document.show_outline_level(0), "Level 0 is refused");
    check(!document.show_outline_level(10), "Level 10 is refused");

    // Collapse something first, then All Subtasks must expand it again.
    check(document.show_outline_level(1), "Level 1 is accepted again");
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), {1, 6}, "Level 1 hides the children again");
    document.show_all_subtasks();
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), file_order, "All Subtasks expands every row");
    check_uids(bar_uids(document), file_order, "All Subtasks restores the bars");
    check(file_hash(path) == before, "the outline commands never change the file's bytes");

    // A sort composes with the outline: Level 1 keeps the sorted top-level order.
    const app::SortBuild descending = sort_by("ID", false);
    check(descending.ok, "the ID descending sort builds");
    document.set_sort(descending.definition, descending.description);
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), {6, 1, 5, 2, 4, 3},
               "the sort orders the full outline");
    check(document.show_outline_level(1), "Level 1 is accepted under a sort");
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), {6, 1}, "Level 1 keeps the sorted order");
    document.show_all_subtasks();
    document.rebuild_views(app::ZoomLevel::week);
    check_uids(display_uids(document), {6, 1, 5, 2, 4, 3},
               "All Subtasks restores the sorted order");
    check_uids(record_uids(document), file_order,
               "the outline commands never reorder the RecordSet");
    check(file_hash(path) == before, "sort plus outline never changes the file's bytes");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: qt_data_commands <wave23-outline.mpp>\n");
        return 2;
    }
    case_sort(QString::fromLocal8Bit(argv[1]));
    case_outline(QString::fromLocal8Bit(argv[1]));
    if (g_failures == 0) {
        std::printf("qt_data_commands: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    std::printf("qt_data_commands: %d checks, %d FAILURES\n", g_checks, g_failures);
    return 1;
}
