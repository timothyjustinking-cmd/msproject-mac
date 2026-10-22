// app/export_pdf.cpp -- see export_pdf.h for what the report includes and the
// two places the shell has to translate (duration text, shading off).

#include "export_pdf.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "filter_model.h"
#include "layout_model.h"
#include "value.h"

namespace app {
namespace {

using core::view::Value;
using core::view::ValueKind;

std::vector<std::size_t> every_row(const core::view::RecordSet& records) {
    std::vector<std::size_t> rows;
    rows.reserve(records.row_count());
    for (std::size_t row = 0; row < records.row_count(); ++row) {
        rows.push_back(row);
    }
    return rows;
}

std::int64_t ticks_of(const Value* value) noexcept {
    if (value == nullptr) {
        return 0;
    }
    switch (value->kind) {
        case ValueKind::Duration:
        case ValueKind::Integer:
            return value->integer;
        case ValueKind::Number:
            return static_cast<std::int64_t>(value->number);
        default:
            return 0;
    }
}

void fill_base(core::render::ReportRequestBase& base, core::view::RecordSet& records,
               const std::vector<core::sheet::SheetColumn>& columns,
               const ExportRequest& request) {
    base.records = &records;
    base.columns = columns;
    base.format = request.format;
    base.metrics = request.metrics;
    base.title = request.title;
    base.subtitle = request.subtitle;
    base.footer = request.footer;
}

}  // namespace

std::shared_ptr<const core::sched::Calendar> shading_calendar(bool working_day_shading) {
    if (working_day_shading) {
        return nullptr;  // the layout's weekend mask is the shading source
    }
    // Every day works from 00:00 to 24:00, so complementing the working
    // intervals yields no non-working span at all.
    const core::sched::DayPattern all_day = core::sched::day_pattern({{0, 24 * 60}});
    core::sched::Week week{};
    for (core::sched::DayPattern& day : week) {
        day = all_day;
    }
    return std::make_shared<const core::sched::Calendar>(week);
}

std::string export_subtitle(ReportKind kind, const std::string& filter_description,
                            std::size_t shown, std::size_t total) {
    std::string text = kind == ReportKind::sheet ? "Sheet report" : "Gantt report";
    text += " - ";
    text += std::to_string(shown);
    text += " of ";
    text += std::to_string(total);
    text += total == 1 ? " task" : " tasks";
    if (!filter_description.empty()) {
        text += "  |  filter: ";
        text += filter_description;
    }
    return text;
}

std::string export_footer(const Preferences& preferences, const std::string& selection_text) {
    std::string text;
    if (!selection_text.empty()) {
        text += "Selected: ";
        text += selection_text;
        text += "  |  ";
    }
    text += preferences_summary(preferences);
    return text;
}

std::string export_default_name(const std::string& source_path, ReportKind kind) {
    std::string stem = std::filesystem::path(source_path).stem().string();
    if (stem.empty()) {
        stem = "project";
    }
    return stem + (kind == ReportKind::sheet ? "-sheet.pdf" : "-gantt.pdf");
}

ExportOutcome export_report(const ExportRequest& request, const std::string& path) {
    ExportOutcome outcome;
    outcome.path = path;
    if (request.records == nullptr) {
        outcome.error = "no records to print";
        return outcome;
    }
    if (path.empty()) {
        outcome.error = "no target path";
        return outcome;
    }

    try {
        const std::vector<std::size_t> rows =
            request.rows.empty() ? every_row(*request.records) : request.rows;
        core::view::RecordSet printed = filtered_record_set(*request.records, rows);

        std::vector<core::sheet::SheetColumn> columns = request.columns;
        if (columns.empty()) {
            columns = core::render::task_report_columns();
        }

        // A duration preference of hours/days is rendered here and carried into
        // the report as text, because core/render formats through its own
        // SheetModel (see the header comment).
        if (request.preferences.duration_style != DurationStyle::auto_units) {
            const core::view::FieldId duration_field = task_field_id("Duration");
            if (duration_field != 0) {
                printed.add_field(kDurationTextField);
                for (std::size_t row = 0; row < printed.row_count(); ++row) {
                    const Value* value = printed.get(row, duration_field);
                    printed.set_field(
                        row, kDurationTextField,
                        Value::of_text(format_duration(ticks_of(value), request.format,
                                                      request.preferences.duration_style)));
                }
                for (core::sheet::SheetColumn& column : columns) {
                    if (column.field != duration_field) {
                        continue;
                    }
                    column.field = kDurationTextField;
                    column.kind = core::sheet::CellKind::Text;
                    column.align = core::view::ColumnAlign::Left;
                }
            }
        }

        core::render::PdfInfo info;
        info.title = request.title;
        core::render::PdfWriter pdf(info);

        std::size_t pages = 0;
        if (request.kind == ReportKind::sheet) {
            core::render::SheetReportRequest sheet{};
            fill_base(sheet, printed, columns, request);
            pages = core::render::render_sheet_report(pdf, sheet);
        } else {
            core::render::GanttReportRequest gantt{};
            fill_base(gantt, printed, columns, request);
            gantt.links = request.links;
            gantt.timescale = request.timescale;
            // The shell's week columns are Monday-first; an all-working shading
            // calendar must not move them to the core calendar's Sunday anchor.
            gantt.timescale.adopt_calendar_week_start = false;
            gantt.auto_timescale = request.auto_timescale;
            gantt.calendar = shading_calendar(request.working_day_shading);
            pages = core::render::render_gantt_report(pdf, gantt);
        }

        std::string error;
        if (!pdf.save(path, error)) {
            outcome.error = error.empty() ? "the PDF writer refused the path" : error;
            return outcome;
        }
        outcome.ok = true;
        outcome.pages = pages;
        outcome.rows = printed.row_count();
    } catch (const std::exception& exception) {
        outcome.error = exception.what();
    }
    return outcome;
}

}  // namespace app
