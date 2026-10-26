// app/sort_tests.cpp -- the Sort dialog's portable half, proved headlessly.
//
// W31-1.1.  The View tab's Sort command is SESSION-ONLY (wave-31 brief section
// 1): the RecordSet and the file are untouched and only the displayed row list
// is reordered.  These checks cover the five offered fields, the choice ->
// SortDefinition translation (and its errors), the outline-aware order, nulls
// last, the pinned project row, and the restriction to the sheet's visible
// rows -- the exact list PlanDocument hands to the sheet and the Gantt.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "date.h"
#include "layout_model.h"
#include "record_set.h"
#include "sort_model.h"
#include "value.h"

namespace {

using core::view::RecordSet;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

void check_size(std::size_t got, std::size_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu want %llu\n", what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

void check_uids(const RecordSet& records, const std::vector<std::size_t>& rows,
                const std::vector<std::uint32_t>& want, const char* what) {
    ++g_checks;
    if (rows.size() != want.size()) {
        ++g_failures;
        std::printf("FAIL: %s: got %llu rows want %llu\n", what,
                    static_cast<unsigned long long>(rows.size()),
                    static_cast<unsigned long long>(want.size()));
        return;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (records.uid(rows[index]) != want[index]) {
            ++g_failures;
            std::printf("FAIL: %s: row %llu is uid %u, want uid %u\n", what,
                        static_cast<unsigned long long>(index),
                        records.uid(rows[index]), want[index]);
            return;
        }
    }
}

app::TaskRow make_row(std::uint32_t uid, const char* name, int start_day, int priority,
                      int level, std::int64_t finish_day = 0) {
    app::TaskRow row;
    row.uid = uid;
    row.id = uid;
    row.outline_number = std::to_string(uid);
    row.name = name;
    row.start = start_day == 0 ? core::sched::kDateNa
                               : core::sched::from_ymd(2026, 10, start_day);
    row.finish = finish_day == 0 ? row.start
                                 : core::sched::from_ymd(2026, 10, static_cast<int>(finish_day));
    row.priority = static_cast<std::uint16_t>(priority);
    row.outline_level = level;
    row.duration_ticks = core::view::kTicksPerDay;
    return row;
}

// The outline: two top-level tasks with two children, then a third top-level
// task whose Start is not set.  The rows are in ID order, as the reader emits
// them.
app::PlanInput make_plan() {
    app::PlanInput input;
    input.title = "Zebra";  // sorts after every task name, so the pin is visible
    input.project_start = core::sched::from_ymd(2026, 10, 19);
    input.tasks.push_back(make_row(1, "Bravo", 20, 600, 0, 22));
    input.tasks.push_back(make_row(2, "Alpha", 19, 500, 0, 23));
    input.tasks.push_back(make_row(3, "Child B", 18, 900, 1, 21));
    input.tasks.push_back(make_row(4, "Child A", 21, 300, 1, 20));
    input.tasks.push_back(make_row(5, "No Start", 0, 100, 0));
    return input;
}

app::SortBuild build(const std::string& title, bool ascending) {
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

std::vector<std::size_t> all_rows(const RecordSet& records) {
    std::vector<std::size_t> rows;
    for (std::size_t row = 0; row < records.row_count(); ++row) {
        rows.push_back(row);
    }
    return rows;
}

std::size_t row_of_uid(const RecordSet& records, std::uint32_t uid) {
    for (std::size_t row = 0; row < records.row_count(); ++row) {
        if (records.uid(row) == uid) {
            return row;
        }
    }
    return records.row_count();
}

void case_fields() {
    const std::vector<app::SortField>& fields = app::sort_fields();
    check_size(fields.size(), 5, "the dialog offers five fields");
    const char* titles[] = {"ID", "Start", "Finish", "Priority", "Name"};
    for (std::size_t index = 0; index < fields.size() && index < 5; ++index) {
        check_text(fields[index].title, titles[index], "the field title and order");
        check(fields[index].field != 0, "the field resolves in the descriptor table");
    }
    check(fields.size() == 5 && fields[3].title == "Priority" &&
              fields[3].field == 0x0b400019u,
          "Priority is Task.Priority 0x0b400019");
}

void case_build_errors() {
    const std::vector<app::SortField>& fields = app::sort_fields();
    app::SortChoice out_of_range;
    out_of_range.field_index = fields.size();
    const app::SortBuild bad = app::build_sort(fields, out_of_range);
    check(!bad.ok, "an out-of-range field index is refused");
    check_text(bad.error, "choose a field to sort by", "the out-of-range message");

    std::vector<app::SortField> unresolved = fields;
    unresolved[0].field = 0;
    app::SortChoice first;
    const app::SortBuild missing = app::build_sort(unresolved, first);
    check(!missing.ok, "a field the table does not resolve is refused");
    check_text(missing.error, "the field \"ID\" is not available",
               "the unresolved-field message");
}

void case_orders(const RecordSet& records) {
    const std::vector<std::size_t> all = all_rows(records);

    const app::SortBuild id_asc = build("ID", true);
    check(id_asc.ok, "ID ascending builds");
    check_text(id_asc.description, "ID ascending", "the ID description");
    check_uids(records, app::sort_display_rows(id_asc.definition, records, all),
               {1, 2, 3, 4, 5}, "ID ascending keeps the file order");

    const app::SortBuild id_desc = build("ID", false);
    check_text(id_desc.description, "ID descending", "the ID descending description");
    check_uids(records, app::sort_display_rows(id_desc.definition, records, all),
               {5, 2, 4, 3, 1},
               "ID descending sorts roots and then only the children of each root");

    check_uids(records, app::sort_display_rows(build("Start", true).definition, records, all),
               {2, 3, 4, 1, 5}, "Start ascending puts the undated row last");
    check_uids(records, app::sort_display_rows(build("Start", false).definition, records, all),
               {1, 2, 4, 3, 5}, "Start descending still puts the null last");
    check_uids(records, app::sort_display_rows(build("Finish", true).definition, records, all),
               {1, 2, 4, 3, 5}, "Finish ascending orders siblings by their own finish");
    check_uids(records,
               app::sort_display_rows(build("Priority", true).definition, records, all),
               {5, 2, 4, 3, 1}, "Priority ascending orders roots and siblings");
    check_uids(records,
               app::sort_display_rows(build("Priority", false).definition, records, all),
               {1, 2, 3, 4, 5}, "Priority descending");
    check_uids(records, app::sort_display_rows(build("Name", true).definition, records, all),
               {2, 4, 3, 1, 5}, "Name ascending orders roots and siblings");
}

void case_project_row(RecordSet& records) {
    const std::vector<std::size_t> all = all_rows(records);
    check(records.uid(0) == 0, "the project-summary row is RecordSet row 0");
    check_uids(records, app::sort_display_rows(build("Name", true).definition, records, all),
               {0, 2, 4, 3, 1, 5},
               "the project row is pinned first even when its name sorts last");
    check_uids(records, app::sort_display_rows(build("ID", false).definition, records, all),
               {0, 5, 2, 4, 3, 1},
               "the project row stays first under a descending sort too");
}

void case_visible_restriction(const RecordSet& records) {
    const std::vector<std::size_t> visible = {
        row_of_uid(records, 1), row_of_uid(records, 3), row_of_uid(records, 4),
        row_of_uid(records, 5),
    };
    check_uids(records,
               app::sort_display_rows(build("Priority", true).definition, records, visible),
               {5, 4, 3, 1},
               "a collapsed row stays out of the result; the rest keep sorted order");

    const std::vector<std::size_t> one = {row_of_uid(records, 2)};
    check_uids(records, app::sort_display_rows(build("Name", false).definition, records, one),
               {2}, "a one-row display list is returned unchanged");

    const std::vector<std::size_t> bad = {records.row_count() + 99, row_of_uid(records, 2)};
    check_uids(records, app::sort_display_rows(build("Name", true).definition, records, bad),
               {2}, "a row outside the RecordSet is dropped");

    core::view::SortDefinition empty;
    check_uids(records, app::sort_display_rows(empty, records, visible), {1, 3, 4, 5},
               "an empty key list returns the visible rows unchanged");
}

void check_record_order_unchanged(const RecordSet& records, const char* what) {
    ++g_checks;
    for (std::size_t row = 0; row < records.row_count(); ++row) {
        if (records.uid(row) != static_cast<std::uint32_t>(row + 1)) {
            ++g_failures;
            std::printf("FAIL: %s (row %llu uid %u)\n", what,
                        static_cast<unsigned long long>(row), records.uid(row));
            return;
        }
    }
}

}  // namespace

int main() {
    case_fields();
    case_build_errors();

    const app::PlanInput plan = make_plan();
    RecordSet records = app::make_record_set(plan);
    check_size(records.row_count(), 5, "the synthetic plan has five rows");
    case_orders(records);
    case_visible_restriction(records);
    check_record_order_unchanged(records, "sorting never reorders the RecordSet");

    app::PlanInput with_project = plan;
    with_project.show_project_summary = true;
    with_project.project_priority = 500;
    RecordSet project_records = app::make_record_set(with_project);
    case_project_row(project_records);

    if (g_failures == 0) {
        std::printf("sort_model_tests: %d checks, 0 failures\n", g_checks);
        return 0;
    }
    std::printf("sort_model_tests: %d checks, %d FAILURES\n", g_checks, g_failures);
    return 1;
}
