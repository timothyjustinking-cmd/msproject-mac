// app_qt/tests/usage_gate_test.cpp -- wave 31, G4: Task Usage and Resource
// Usage at Gantt fidelity (W31-4.4).
//
// The gate loads the wave-31 synthetic fixture, whose Task Usage view stores
// Work + Actual Work and whose Resource Usage view stores Work + Cost, and
// pins the presentation Project's own captures measured
// (tests/data/reference/wave31-usage.measures.json):
//
//   * the stored Details lists and timescale tiers, read through PlanDocument;
//   * the app grid's logical rows, their parent IDs/assignment uids and every
//     per-period Work / Actual Work / Cost bucket against Project's COM
//     TimeScaleData values (com.per_period);
//   * the sheet shape: 21 px detail sub-rows stacked two per logical row, the
//     147/123 px labels, the 48 px Details column, 64 px day periods (84/105
//     with Cost), the 84 px port Total column, the measured Details captions
//     ("Work"/"Act. W"/"Cost"), the parent-only gutter IDs, the label merge
//     and the two-tier week/day header;
//
// Usage: qt_usage_gate_tests <wave31-usage.mpp> <wave31-usage.measures.json>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QModelIndex>
#include <QString>
#include <QTableView>
#include <QVariant>

#include "layout_model.h"
#include "main_window.h"
#include "plan_document.h"
#include "shell_style.h"
#include "sheet.h"
#include "sheet_table_model.h"
#include "sheet_views.h"
#include "usage_grid.h"
#include "usage_sheet.h"
#include "value.h"
#include "view_model.h"

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

using msproject::viewstate::UsageDetailKind;
using msproject::viewstate::UsageViewStyle;

std::size_t detail_index(const app::UsageGrid& grid, UsageDetailKind kind) {
    for (std::size_t index = 0; index < grid.details.size(); ++index) {
        if (grid.details[index].kind == kind) {
            return index;
        }
    }
    return grid.details.size();
}

std::vector<std::optional<std::int64_t>> read_series(const QJsonArray& array) {
    std::vector<std::optional<std::int64_t>> out;
    out.reserve(static_cast<std::size_t>(array.size()));
    for (const QJsonValue value : array) {
        if (value.isNull() || value.isUndefined()) {
            out.emplace_back(std::nullopt);
        } else {
            out.emplace_back(static_cast<std::int64_t>(value.toInt()));
        }
    }
    return out;
}

QJsonObject read_measures(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "FAIL: cannot open the measures JSON\n");
        ++g_failures;
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        std::fprintf(stderr, "FAIL: cannot parse the measures JSON: %s\n",
                     qPrintable(error.errorString()));
        ++g_failures;
        return {};
    }
    return document.object();
}

QJsonObject object_at(const QJsonObject& parent, const char* key) {
    return parent.value(QLatin1String(key)).toObject();
}

QJsonObject com_per_period(const QJsonObject& measures) {
    return object_at(object_at(measures, "com"), "per_period");
}

// One logical grid row's COM key: the task/resource id for a parent, the
// assignment uid for a child (com.per_period.* uses exactly those keys).
QString com_key(const app::UsageGridRow& row, const QJsonObject& measured) {
    switch (row.kind) {
        case app::UsageGridRowKind::task:
            return QString::number(measured.value(QLatin1String("task_id")).toInt());
        case app::UsageGridRowKind::resource:
            return QString::number(measured.value(QLatin1String("resource_id")).toInt());
        case app::UsageGridRowKind::assignment:
            return QString::number(
                measured.value(QLatin1String("assignment_uid")).toInt());
        case app::UsageGridRowKind::total:
            break;
    }
    return {};
}

// Project's per-period object for one row: com.per_period groups the buckets
// by record type (tasks / assignments / resources).
QJsonObject com_buckets(const QJsonObject& per_period,
                        const app::UsageGridRow& row,
                        const QJsonObject& measured) {
    const char* group = nullptr;
    switch (row.kind) {
        case app::UsageGridRowKind::task: group = "tasks"; break;
        case app::UsageGridRowKind::resource: group = "resources"; break;
        case app::UsageGridRowKind::assignment: group = "assignments"; break;
        case app::UsageGridRowKind::total: return {};
    }
    return per_period.value(QLatin1String(group))
        .toObject()
        .value(com_key(row, measured))
        .toObject();
}

app::UsageGridRowKind kind_of(const QString& text) {    if (text == QLatin1String("task")) {
        return app::UsageGridRowKind::task;
    }
    if (text == QLatin1String("resource")) {
        return app::UsageGridRowKind::resource;
    }
    if (text == QLatin1String("assignment")) {
        return app::UsageGridRowKind::assignment;
    }
    return app::UsageGridRowKind::total;
}

void check_series(const std::vector<std::int64_t>& got,
                  const std::vector<std::optional<std::int64_t>>& want,
                  const char* what) {
    check(got.size() >= want.size(), what);
    for (std::size_t index = 0; index < want.size() && index < got.size(); ++index) {
        const std::int64_t expected = want[index].value_or(0);
        if (got[index] != expected) {
            std::fprintf(stderr, "FAIL: %s: bucket %zu is %lld, want %lld\n", what,
                         index, static_cast<long long>(got[index]),
                         static_cast<long long>(expected));
            ++g_failures;
        }
    }
}

// The fixture's two usage views, as PlanDocument decoded them.
void check_stored_views(const msqt::PlanDocument& document,
                        const QJsonObject& measures) {
    const QJsonObject stored = object_at(measures, "stored_views");
    const auto check_one = [&stored](const app::UsageViewStyle& style,
                                     const char* key,
                                     const UsageDetailKind* kinds,
                                     const std::uint8_t* ids,
                                     std::size_t count,
                                     const char* middle_name,
                                     const char* bottom_name) {
        const QJsonObject want = object_at(stored, key);
        check(style.decoded, "usage gate: the stored usage view decoded");
        check(style.stored.details.size() == count,
              "usage gate: the stored Details list has two entries");
        if (style.stored.details.size() != count) {
            return;
        }
        for (std::size_t index = 0; index < count; ++index) {
            check(style.stored.details[index].kind == kinds[index],
                  "usage gate: the stored detail kind is the measured one");
            check(style.stored.details[index].stored_id == ids[index],
                  "usage gate: the stored detail id is the measured one");
        }
        const QJsonArray measured_ids = want.value(QLatin1String("detail_ids")).toArray();
        check(measured_ids.size() == static_cast<qsizetype>(count),
              "usage gate: the measures record the same Details count");
        // The stored middle tier renders on TOP and the bottom tier below it
        // (app::UsageGrid::major_tier/minor_tier).  The committed unit names
        // ("weeks"/"days") are the measured MPXJ names; the decoded enum's own
        // values are not the stored bytes.
        // Compare the NAMES as strings: two const char* compared with == would
        // compare pointers, which only happens to work when the linker merges
        // the two translation units' literals.
        check(std::string_view(msproject::viewstate::timescale_unit_name(
                  style.stored.major.unit)) == middle_name,
              "usage gate: the stored middle unit is the measured top tier");
        check(std::string_view(msproject::viewstate::timescale_unit_name(
                  style.stored.minor.unit)) == bottom_name,
              "usage gate: the stored bottom unit is the measured bottom tier");
        check(style.stored.major.count ==
                  want.value(QLatin1String("middle_count")).toInt() &&
                  style.stored.minor.count ==
                      want.value(QLatin1String("bottom_count")).toInt(),
              "usage gate: the stored tier counts are the measured ones");
        check(style.stored.major.format ==
                  want.value(QLatin1String("middle_format")).toInt() &&
                  style.stored.minor.format ==
                      want.value(QLatin1String("bottom_format")).toInt(),
              "usage gate: the stored tier formats are the measured ones");
        check(style.stored.major.align ==
                  want.value(QLatin1String("middle_align")).toInt() &&
                  style.stored.minor.align ==
                      want.value(QLatin1String("bottom_align")).toInt(),
              "usage gate: the stored tier alignments are the measured ones");
    };
    const UsageDetailKind task_kinds[] = {UsageDetailKind::work,
                                          UsageDetailKind::actual_work};
    const std::uint8_t task_ids[] = {0, 2};
    check_one(document.task_usage_style(), "task_usage", task_kinds, task_ids, 2,
              "weeks", "days");
    const UsageDetailKind resource_kinds[] = {UsageDetailKind::work,
                                              UsageDetailKind::cost};
    const std::uint8_t resource_ids[] = {0, 9};
    check_one(document.resource_usage_style(), "resource_usage", resource_kinds,
              resource_ids, 2, "weeks", "days");
}

// The grid's parent/assignment rows against Project's rendered row order and
// per-period values.
void check_grid_against_com(const app::UsageGrid& grid,
                            const QJsonObject& measures, const char* key,
                            const char* what) {
    const QJsonObject rendered = object_at(object_at(measures, "rendered_grid"), key);
    const QJsonArray rows = rendered.value(QLatin1String("logical_rows")).toArray();
    check(grid.rows.size() == static_cast<std::size_t>(rows.size()) + 1,
          "usage gate: the grid is the measured logical rows plus the port Total");
    if (grid.rows.size() != static_cast<std::size_t>(rows.size()) + 1) {
        return;
    }
    check(grid.periods.size() == 5,
          "usage gate: the fixture spans Project's five measured day buckets");
    const QJsonObject per_period = com_per_period(measures);
    const std::size_t work = detail_index(grid, UsageDetailKind::work);
    const std::size_t actual = detail_index(grid, UsageDetailKind::actual_work);
    const std::size_t cost = detail_index(grid, UsageDetailKind::cost);
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject want = rows[index].toObject();
        const app::UsageGridRow& row = grid.rows[static_cast<std::size_t>(index)];
        if (row.kind != kind_of(want.value(QLatin1String("kind")).toString())) {
            std::fprintf(stderr, "FAIL: %s: logical row %d has the wrong kind\n",
                         what, index);
            ++g_failures;
            continue;
        }
        check(row.totals.size() == grid.details.size(),
              "usage gate: every row carries a total per detail");
        if (row.totals.size() == grid.details.size() && work < row.totals.size()) {
            check(row.totals[work] ==
                      want.value(QLatin1String("work_minutes")).toInt(),
                  "usage gate: the row's Work total is Project's");
        }
        const QJsonObject buckets = com_buckets(per_period, row, want);
        check(!buckets.isEmpty(),
              "usage gate: Project measured this row's time-phased buckets");
        if (buckets.isEmpty()) {
            continue;
        }
        if (work < row.values.size()) {
            check_series(row.values[work],
                         read_series(buckets.value(QLatin1String("work_minutes")).toArray()),
                         "usage gate: Work per period");
        }
        if (actual < row.values.size()) {
            check_series(
                row.values[actual],
                read_series(
                    buckets.value(QLatin1String("actual_work_minutes")).toArray()),
                "usage gate: Actual Work per period");
        }
        if (cost < row.values.size()) {
            check_series(row.values[cost],
                         read_series(buckets.value(QLatin1String("cost_cents")).toArray()),
                         "usage gate: Cost per period");
        }
    }
    const app::UsageGridRow& total = grid.rows.back();
    check(total.kind == app::UsageGridRowKind::total,
          "usage gate: the port's Total row closes the grid");
    check(total.totals.size() == grid.details.size() &&
              work < total.totals.size() && total.totals[work] == 9600,
          "usage gate: the Total row sums the fixture's 9600 Work minutes");
}

// The presentation shape both views share: 21 px detail sub-rows (two per
// logical row), the measured pixel widths, the parent-only gutter and the
// Details captions.
void check_sheet_shape(const app::UsageGrid& grid, const msqt::UsageSheet& sheet,
                       bool resource) {
    check(!sheet.rows().empty(), "usage gate: the usage sheet has rows");
    check(sheet.detail_count() == 2,
          "usage gate: the sheet draws Project's two stored details");
    check(sheet.logical_row_count() == grid.rows.size(),
          "usage gate: the sheet keeps one logical row per grid row");
    check(sheet.rows().size() == grid.rows.size() * 2,
          "usage gate: each logical row stacks two 21 px detail sub-rows");
    check(sheet.period_count() == 5,
          "usage gate: the sheet has the fixture's five day columns");

    // Widths: measured pixels, not the character convention.
    std::vector<int> want_widths;
    want_widths.push_back(resource ? 123 : 147);
    want_widths.push_back(48);
    const int period_width = resource ? 105 : 64;
    for (std::size_t period = 0; period < sheet.period_count(); ++period) {
        want_widths.push_back(period_width);
    }
    want_widths.push_back(84);
    check(sheet.column_widths() == want_widths,
          "usage gate: the sheet carries the measured pixel widths");

    check(sheet.columns().size() == want_widths.size(),
          "usage gate: label + Details + periods + port Total");
    if (sheet.columns().size() != want_widths.size()) {
        return;
    }
    for (const core::sheet::SheetColumn& column : sheet.columns()) {
        check(column.visible && column.read_only,
              "usage gate: every usage column is visible and read-only");
    }
    check(sheet.columns()[0].title == (resource ? "Resource Name" : "Task Name") &&
              sheet.columns()[1].title == "Details" &&
              sheet.columns().back().title == "Total",
          "usage gate: the measured captions name the label/Details/Total columns");

    // The Details captions and the label merge.
    for (std::size_t logical = 0; logical < grid.rows.size(); ++logical) {
        const std::size_t first_row = logical * 2;
        const app::UsageGridRow& source = grid.rows[logical];
        check(sheet.row_numbers()[first_row] ==
                  (source.numbered ? std::to_string(source.display_id)
                                   : std::string()),
              "usage gate: only the parent rows carry the stored ID");
        check(sheet.row_numbers()[first_row + 1].empty(),
              "usage gate: the second detail sub-row leaves the gutter blank");
        const core::view::Value* first_caption =
            sheet.records().get(first_row, msqt::kUsageDetailsField);
        const core::view::Value* second_caption =
            sheet.records().get(first_row + 1, msqt::kUsageDetailsField);
        check(first_caption != nullptr && first_caption->text == "Work",
              "usage gate: the first detail sub-row is captioned Work");
        check(second_caption != nullptr &&
                  second_caption->text == (resource ? "Cost" : "Act. W"),
              "usage gate: the second detail sub-row carries Project's caption");
        const core::view::Value* label =
            sheet.records().get(first_row, app::kUsageLabelField);
        check(label != nullptr && label->text == source.label,
              "usage gate: the label sits on the first detail sub-row");
        const core::view::Value* second_label =
            sheet.records().get(first_row + 1, app::kUsageLabelField);
        check(second_label == nullptr || second_label->is_null(),
              "usage gate: the second sub-row leaves the merged label empty");
        const msqt::UsageRowSpan& span = sheet.label_spans()[logical];
        check(span.row == static_cast<int>(first_row) && span.rows == 2,
              "usage gate: the label column merges each logical row's two sub-rows");
        check(sheet.rows()[first_row].detail == 0 &&
                  sheet.rows()[first_row + 1].detail == 1,
              "usage gate: the physical rows keep the stored detail order");
    }

    // The two-tier header: the stored week label over the measured day letters.
    const std::vector<msqt::UsageHeaderCell>& header = sheet.header_cells();
    check(header.size() == sheet.columns().size(),
          "usage gate: one header cell per column");
    if (header.size() != sheet.columns().size()) {
        return;
    }
    check(!header[0].tiered && !header[1].tiered && !header.back().tiered,
          "usage gate: only the period columns carry the two-tier header");
    const char* const letters[] = {"M", "T", "W", "T", "F"};
    for (std::size_t period = 0; period < 5; ++period) {
        const msqt::UsageHeaderCell& cell =
            header[static_cast<std::size_t>(msqt::UsageSheet::kFirstPeriodColumn) +
                   period];
        check(cell.tiered, "usage gate: every period column is two-tier");
        check(cell.minor == letters[period],
              "usage gate: the bottom tier is the measured day letter");
        check(cell.minor_align == 1,
              "usage gate: the stored bottom alignment centres the day letter");
        check(cell.major_align == 0,
              "usage gate: the stored middle alignment is left");
        if (period == 0) {
            check(cell.major == "5 Jan '26",
                  "usage gate: the top tier labels the week's Monday column");
        } else {
            check(cell.major.empty(),
                  "usage gate: only the week's first column carries the label");
            check(!cell.group_start,
                  "usage gate: one week draws no interior group boundary");
        }
    }

    // Value kinds: Work/Actual Work are Duration, Cost is Cost through the
    // shell's per-cell lookup.
    check(sheet.value_kind(0, 0) == core::sheet::CellKind::Text &&
              sheet.value_kind(0, 1) == core::sheet::CellKind::Text,
          "usage gate: label and Details are text cells");
    check(sheet.value_kind(0, 2) == core::sheet::CellKind::Duration &&
              sheet.value_kind(1, 2) ==
                  (resource ? core::sheet::CellKind::Cost
                            : core::sheet::CellKind::Duration),
          "usage gate: the detail sub-rows carry their own value kinds");
    check(sheet.value_kind(0, sheet.total_column()) ==
              core::sheet::CellKind::Duration,
          "usage gate: the Total column follows the detail's value kind");
}

// The typed cells hold the COM numbers: Duration ticks are minutes x 10, Cost
// is stored in dollars so CellKind::Cost can render the measured "$" text.
void check_sheet_values(const app::UsageGrid& grid, const msqt::UsageSheet& sheet,
                        const QJsonObject& measures, const char* key) {
    const QJsonObject rendered = object_at(object_at(measures, "rendered_grid"), key);
    const QJsonArray rows = rendered.value(QLatin1String("logical_rows")).toArray();
    const QJsonObject per_period = com_per_period(measures);
    const std::size_t work = detail_index(grid, UsageDetailKind::work);
    const std::size_t actual = detail_index(grid, UsageDetailKind::actual_work);
    const std::size_t cost = detail_index(grid, UsageDetailKind::cost);
    for (int index = 0; index < rows.size(); ++index) {
        const QJsonObject want = rows[index].toObject();
        const app::UsageGridRow& source = grid.rows[static_cast<std::size_t>(index)];
        const QJsonObject buckets = com_buckets(per_period, source, want);
        if (buckets.isEmpty()) {
            continue;
        }
        const std::size_t physical = static_cast<std::size_t>(index) * 2;
        const auto check_bucket = [&](std::size_t detail,
                                      const QJsonArray& expected, bool dollars) {
            const std::vector<std::optional<std::int64_t>> series =
                read_series(expected);
            for (std::size_t period = 0; period < series.size(); ++period) {
                const core::view::Value* value = sheet.records().get(
                    physical + detail,
                    static_cast<core::view::FieldId>(app::kUsagePeriodFieldBase +
                                                     period));
                if (!series[period].has_value()) {
                    check(value == nullptr || value->is_null(),
                          "usage gate: a blank COM bucket stays a blank cell");
                    continue;
                }
                check(value != nullptr,
                      "usage gate: a measured COM bucket has a cell");
                if (value == nullptr) {
                    continue;
                }
                const std::int64_t got =
                    dollars ? static_cast<std::int64_t>(std::llround(value->number * 100.0))
                            : value->integer / core::view::kTicksPerMinute;
                if (got != *series[period]) {
                    std::fprintf(stderr,
                                 "FAIL: usage gate: period %zu of logical row %d "
                                 "holds %lld, want %lld\n",
                                 period, index, static_cast<long long>(got),
                                 static_cast<long long>(*series[period]));
                    ++g_failures;
                }
            }
        };
        if (work < grid.details.size()) {
            check_bucket(work,
                         buckets.value(QLatin1String("work_minutes")).toArray(),
                         false);
        }
        if (actual < grid.details.size()) {
            check_bucket(actual,
                         buckets.value(QLatin1String("actual_work_minutes")).toArray(),
                         false);
        }
        if (cost < grid.details.size()) {
            check_bucket(cost, buckets.value(QLatin1String("cost_cents")).toArray(),
                         true);
        }
    }
}

// The Task Usage width pair: Project's measured 84 px when the stored detail
// set includes Cost (the probe capture), against the fixture's 64 px without it.
void check_cost_width_probe(const msqt::PlanDocument& document) {
    app::UsageViewStyle style = document.task_usage_style();
    style.stored.details.clear();
    style.stored.details.push_back({0, UsageDetailKind::work});
    style.stored.details.push_back({9, UsageDetailKind::cost});
    const app::UsageGrid grid = app::make_usage_grid(
        document.plan(), style, false,
        app::standard_calendar(), core::usage::Period::day);
    msqt::UsageSheet sheet(grid);
    check(sheet.column_widths()[2] == 84,
          "usage gate: Task Usage widens its periods to 84 px with Cost stored");
    check(sheet.detail_count() == 2 &&
              sheet.header_cells()[1].tiered == false,
          "usage gate: the Cost probe keeps the two-detail shape");

    core::sheet::SheetFormat format;
    format.currency_symbol = "$";
    core::view::RecordSet records = sheet.records();
    core::sheet::SheetModel model(records, sheet.columns());
    model.set_format(format);
    msqt::SheetTableModel adapter;
    adapter.set_sheet(&model);
    adapter.set_generic(true);
    adapter.set_value_kind_lookup([&sheet](int row, int column) {
        return sheet.value_kind(row, column);
    });
    // Task 1's first period: 8 h on the $50/h resource (40000 cents) plus 8 h
    // on the $60/h resource (48000 cents) = the measured 88000 cents.
    check(adapter.index(1, 2).data(Qt::DisplayRole).toString() ==
              QStringLiteral("$880.00"),
          "usage gate: the Cost sub-row renders Project's $880.00 bucket");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: qt_usage_gate_tests <wave31-usage.mpp> "
                     "<wave31-usage.measures.json>\n");
        return 2;
    }
    const std::string fixture = argv[1];
    const QJsonObject measures = read_measures(argv[2]);
    if (measures.isEmpty()) {
        return 1;
    }
    QApplication application(argc, argv);

    msqt::PlanDocument document;
    std::string error;
    if (!document.load(fixture, app::ZoomLevel::week, error)) {
        std::fprintf(stderr, "FAIL: cannot load the fixture: %s\n", error.c_str());
        return 1;
    }
    check_stored_views(document, measures);

    const app::PlanInput& plan = document.plan();
    check(plan.tasks.size() == 3 && plan.resources.size() == 3 &&
              plan.assignments.size() == 6,
          "usage gate: the fixture carries three tasks, three resources and six "
          "assignments");
    for (const app::AssignmentRow& assignment : plan.assignments) {
        check(assignment.uid != 0 && assignment.task_uid != 0 &&
                  assignment.resource_uid != 0,
              "usage gate: every assignment names its live task and resource");
    }

    const app::UsageGrid task_grid = app::make_usage_grid(
        plan, document.task_usage_style(), false,
        app::standard_calendar(), core::usage::Period::day);
    check(task_grid.details.size() == 2 &&
              task_grid.details[0].kind == UsageDetailKind::work &&
              task_grid.details[1].kind == UsageDetailKind::actual_work,
          "usage gate: Task Usage draws Work then Actual Work, as stored");
    check_grid_against_com(task_grid, measures, "task_usage", "task usage");
    const msqt::UsageSheet task_sheet(task_grid);
    check_sheet_shape(task_grid, task_sheet, false);
    check_sheet_values(task_grid, task_sheet, measures, "task_usage");

    const app::UsageGrid resource_grid = app::make_usage_grid(
        plan, document.resource_usage_style(), true,
        app::standard_calendar(), core::usage::Period::day);
    check(resource_grid.details.size() == 2 &&
              resource_grid.details[0].kind == UsageDetailKind::work &&
              resource_grid.details[1].kind == UsageDetailKind::cost,
          "usage gate: Resource Usage draws Work then Cost, as stored");
    check_grid_against_com(resource_grid, measures, "resource_usage",
                           "resource usage");
    const msqt::UsageSheet resource_sheet(resource_grid);
    check_sheet_shape(resource_grid, resource_sheet, true);
    check_sheet_values(resource_grid, resource_sheet, measures, "resource_usage");

    check_cost_width_probe(document);

    std::fprintf(stderr, "qt_usage_gate: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
