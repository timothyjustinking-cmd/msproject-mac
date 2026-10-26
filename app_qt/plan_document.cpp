// app_qt/plan_document.cpp -- see plan_document.h.

#include "plan_document.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "cfb_writer.h"
#include "edit_model.h"
#include "entry_columns.h"  // kPredecessorsField/kSuccessorsField/kResourceNamesField
#include "filter_model.h"
#include "props.h"
#include "reader_adapter.h"
#include "selection_model.h"
#include "shell_style.h"
#include "sort_model.h"
#include "view_style.h"
#include "viewstate.h"

namespace msqt {
namespace {

// The column a row is selected on: the Name column, else the first column.  The
// same rule app/main.mm uses, so the Qt shell opens with the same cell selected.
std::size_t name_column_of(const core::sheet::SheetModel& sheet) {
    for (std::size_t column = 0; column < sheet.column_count(); ++column) {
        if (sheet.column(column).field == app::name_field_id()) {
            return column;
        }
    }
    return 0;
}

// The four parts of `214/CTable`, the collection core/viewstate decodes.
const char* const kTableParts[4] = {"214/CTable/FixedData", "214/CTable/FixedMeta",
                                    "214/CTable/VarMeta", "214/CTable/Var2Data"};

// Wave 31 (W31-1.3): a Project table belongs to the Task Sheet when its columns
// are task fields (0x0b40xxxx).  A table that carries a resource field
// (0x0c40xxxx) is a Resource Sheet/Usage table and is not offered here; a table
// with no task column at all is presentation junk or a shape this reader does
// not understand.
bool is_task_table(const msproject::viewstate::TableDef& table) {
    bool has_task = false;
    for (const msproject::viewstate::ColumnDef& column : table.columns) {
        const std::uint32_t group = column.field_id & 0xffff0000u;
        if (group == 0x0c400000u) {
            return false;
        }
        if (group == 0x0b400000u) {
            has_task = true;
        }
    }
    return has_task;
}
// `214/CEdl` -- the applied view/table records (wave 23, B-2).
const char* const kEdlParts[4] = {"214/CEdl/FixedData", "214/CEdl/FixedMeta",
                                  "214/CEdl/VarMeta", "214/CEdl/Var2Data"};

// Unit label display records in `114/Props`, all MEASURED on 2026-10-23 by
// saving one Options > Edit change at a time and diffing the props records:
// minute 0x02400033, hour 0x02400034, day 0x02400035, week 0x02400036,
// year 0x02400037, month 0x0240138e.  Value 0 is the shortest form, 1 the
// abbreviated form, 2 the full word; COM's Task.Predecessors strings confirm
// each (day 0/1/2 prints "d"/"dys"/"days", hour "h"/"hrs"/"hours", week
// "w"/"wks"/"weeks", month "mo"/"mons"/"months").
struct LabelSet {
    const char* shortest_singular;
    const char* shortest_plural;
    // Value 1 is the abbreviated form; MEASURED through COM (day "dy"/"dys",
    // hour "hr"/"hrs", week "wk"/"wks", month "mon"/"mons", minute "min").
    const char* abbreviated_singular;
    const char* abbreviated_plural;
    const char* full_singular;
    const char* full_plural;
};

void set_unit_labels(const msproject::props::PropsDocument& document, std::uint32_t id,
                     const LabelSet& labels, std::string& singular, std::string& plural) {
    const msproject::props::PropsRecord* record = document.find(id);
    if (record == nullptr || record->kind != static_cast<std::uint32_t>(
                                               msproject::props::ValueKind::int16)) {
        return;
    }
    const std::span<const std::uint8_t> bytes = document.payload(*record);
    if (bytes.size() < 2) {
        return;
    }
    const std::uint16_t display =
        static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
    switch (display) {
        case 0:
            singular = labels.shortest_singular;
            plural = labels.shortest_plural;
            break;
        case 1:
            singular = labels.abbreviated_singular;
            plural = labels.abbreviated_plural;
            break;
        case 2:
            singular = labels.full_singular;
            plural = labels.full_plural;
            break;
        default: break;
    }
}

// Loads the file's Options > Edit unit-label settings into `format`.  A file
// without the records keeps the SheetFormat defaults (the same full words).
void apply_file_labels(const std::string& source_path, core::sheet::SheetFormat& format) {
    const std::vector<std::uint8_t> bytes = app::props_stream_from_file(source_path);
    if (bytes.empty()) {
        return;
    }
    msproject::props::PropsDocument document;
    if (!msproject::props::parse_props(bytes, document, nullptr)) {
        return;
    }
    set_unit_labels(document, 0x02400033u, {"m", "m", "min", "mins", "minute", "minutes"},
                    format.units.minute_singular, format.units.minute_plural);
    set_unit_labels(document, 0x02400034u, {"h", "h", "hr", "hrs", "hour", "hours"},
                    format.units.hour_singular, format.units.hour_plural);
    set_unit_labels(document, 0x02400035u, {"d", "d", "dy", "dys", "day", "days"},
                    format.units.day_singular, format.units.day_plural);
    set_unit_labels(document, 0x02400036u, {"w", "w", "wk", "wks", "week", "weeks"},
                    format.units.week_singular, format.units.week_plural);
    set_unit_labels(document, 0x0240138eu, {"mo", "mo", "mon", "mons", "month", "months"},
                    format.units.month_singular, format.units.month_plural);
}

// Wave 22: the task a uid names, or nullptr when the plan has no live row for it
// (the reader drops links with a dead endpoint, and a structural rebuild keeps
// that rule, so this is a guard rather than a normal path).
const app::TaskRow* task_row_of(const app::PlanInput& plan, std::uint32_t uid) {
    if (uid == 0) {
        return nullptr;
    }
    for (const app::TaskRow& row : plan.tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

// The display id of a link's other endpoint: Project's Predecessors/Successors
// text names a task by its ID column (not its UniqueID).  Empty when the plan has
// no live row, which SKIPS that entry rather than printing an invented uid.
std::string endpoint_id(const app::PlanInput& plan, std::uint32_t uid) {
    const app::TaskRow* row = task_row_of(plan, uid);
    return row != nullptr ? std::to_string(row->id) : std::string();
}

// Project's link-type suffix.  MEASURED from the clean capture and from COM's
// Task.Predecessors strings: a plain finish-to-start link with no lag is the
// BARE id ("9", "33"), but a finish-to-start link with a lag prints FS too
// ("9FS+50 d", "17FS+20 d"), and the other three types always print their
// letters -- COM's task 42 reads "33SS,57" for a lag-free SS predecessor.
const char* link_type_suffix(core::gantt::LinkType type) {
    switch (type) {
        case core::gantt::LinkType::finish_to_start: return "FS";
        case core::gantt::LinkType::start_to_start: return "SS";
        case core::gantt::LinkType::finish_to_finish: return "FF";
        case core::gantt::LinkType::start_to_finish: return "SF";
    }
    return "FS";
}

// Project's lag number: up to two decimals, trailing zeros trimmed.
std::string lag_number(double value, char decimal_separator) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.2f", value);
    std::string text(buffer);
    const std::size_t dot = text.find('.');
    if (dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (decimal_separator != '.') {
        for (char& c : text) {
            if (c == '.') {
                c = decimal_separator;
            }
        }
    }
    return text;
}

// Wave 23 (W23-7): the link's own stored PjFormatUnit decides the unit; the
// number is the stored tick count in that unit.  Elapsed units (4/6/8/10/12)
// are wall-clock and print with Project's "e" prefix.  Percent (19) stores the
// percentage itself, not ticks.
std::string link_lag_text(std::int32_t lag_ticks, std::uint16_t lag_units,
                          const core::sheet::SheetFormat& format) {
    if (lag_ticks == 0) {
        return {};
    }
    const bool negative = lag_ticks < 0;
    const double magnitude =
        static_cast<double>(negative ? -static_cast<std::int64_t>(lag_ticks) : lag_ticks);

    std::string number;
    std::string label;
    const std::uint16_t unit =
        lag_units >= 32 ? static_cast<std::uint16_t>(lag_units - 32) : lag_units;
    const bool elapsed = mpp::format_unit_is_elapsed(lag_units);
    // An hour is 600 file ticks (an 8-hour file day / 8), the same
    // core::sheet::kTicksPerHour the sheet formatter uses; a working day is
    // hours_per_day hours, and an ELAPSED day is 24 wall-clock hours (measured:
    // 2 edays = 28800 ticks).  Do not divide kTicksPerDay by hours_per_day --
    // that disagrees with the sheet whenever the preference changes the day
    // length (review finding).
    const double ticks_per_hour = 600.0;
    const int hours_per_day = format.hours_per_day > 0 ? format.hours_per_day : 8;
    const double ticks_per_day = elapsed ? 24.0 * ticks_per_hour
                                         : ticks_per_hour * static_cast<double>(hours_per_day);
    const double ticks_per_week =
        ticks_per_day * (elapsed ? 7.0 : static_cast<double>(format.days_per_week > 0
                                                                 ? format.days_per_week
                                                                 : 5));
    const double ticks_per_month =
        ticks_per_day * (elapsed ? 30.0 : static_cast<double>(format.days_per_month > 0
                                                                  ? format.days_per_month
                                                                  : 20));
    if (unit == 19 || unit == 20) {
        return (negative ? "-" : "+") + lag_number(magnitude, format.decimal_separator) + "%";
    }
    if (unit == 3 || unit == 4) {
        number = lag_number(magnitude / static_cast<double>(mpp::kTicksPerMinute),
                            format.decimal_separator);
        label = magnitude / static_cast<double>(mpp::kTicksPerMinute) == 1.0
                    ? format.units.minute_singular
                    : format.units.minute_plural;
    } else if (unit == 5 || unit == 6) {
        const double value = magnitude / ticks_per_hour;
        number = lag_number(value, format.decimal_separator);
        label = value == 1.0 ? format.units.hour_singular : format.units.hour_plural;
    } else if (unit == 7 || unit == 8) {
        const double value = magnitude / ticks_per_day;
        number = lag_number(value, format.decimal_separator);
        label = value == 1.0 ? format.units.day_singular : format.units.day_plural;
    } else if (unit == 9 || unit == 10) {
        const double value = magnitude / ticks_per_week;
        number = lag_number(value, format.decimal_separator);
        label = value == 1.0 ? format.units.week_singular : format.units.week_plural;
    } else if (unit == 11 || unit == 12) {
        const double value = magnitude / ticks_per_month;
        number = lag_number(value, format.decimal_separator);
        label = value == 1.0 ? format.units.month_singular : format.units.month_plural;
    } else {
        // An unmeasured PjFormatUnit: fall back to the sheet's magnitude rule
        // rather than claim a unit.
        const std::string text =
            core::sheet::format_duration_text(static_cast<std::int64_t>(magnitude), format);
        return (negative ? "-" : "+") + text;
    }
    if (label.empty()) {
        label = "d";
    }
    return (negative ? "-" : "+") + number + " " + (elapsed ? "e" : "") + label;
}

// One link's entry in the predecessor/successor list: the other endpoint's ID,
// the type letters and the lag.  The letters are dropped only for the measured
// bare case -- finish-to-start AND no lag -- so "3" is a plain FS link while
// "5FS+5 days" and "33SS" both keep theirs.
std::string link_entry(const app::PlanInput& plan, std::uint32_t other_uid,
                       core::gantt::LinkType type, std::int32_t lag_ticks,
                       std::uint16_t lag_units, const core::sheet::SheetFormat& format) {
    const std::string id = endpoint_id(plan, other_uid);
    if (id.empty()) {
        return {};
    }
    const std::string lag = link_lag_text(lag_ticks, lag_units, format);
    const bool bare = type == core::gantt::LinkType::finish_to_start && lag.empty();
    return id + (bare ? std::string() : std::string(link_type_suffix(type))) + lag;
}

// The comma-separated list of one side of every link touching `task_uid`, in the
// plan's own link order -- the order the capture shows ("18,33FS+20").
std::string link_list_text(const app::PlanInput& plan, std::uint32_t task_uid, bool incoming,
                           const core::sheet::SheetFormat& format) {
    std::string text;
    for (const app::LinkRow& link : plan.links) {
        const std::uint32_t mine = incoming ? link.successor_uid : link.predecessor_uid;
        if (mine != task_uid) {
            continue;
        }
        const std::uint32_t other =
            incoming ? link.predecessor_uid : link.successor_uid;
        const std::string entry =
            link_entry(plan, other, link.type, link.lag_ticks, link.lag_units, format);
        if (entry.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += ",";  // Project's list separator in these two columns: no space
        }
        text += entry;
    }
    return text;
}

}  // namespace

std::string predecessors_text(const app::PlanInput& plan, std::uint32_t task_uid,
                              const core::sheet::SheetFormat& format) {
    return link_list_text(plan, task_uid, true, format);
}

std::string successors_text(const app::PlanInput& plan, std::uint32_t task_uid,
                            const core::sheet::SheetFormat& format) {
    return link_list_text(plan, task_uid, false, format);
}

std::string resource_names_text(const app::PlanInput& plan, std::uint32_t task_uid) {
    if (task_uid == 0) {
        return {};
    }
    std::string text;
    for (const app::AssignmentRow& assignment : plan.assignments) {
        if (assignment.task_uid != task_uid || assignment.resource_uid == 0) {
            continue;  // an unassigned row names no resource (the corpus's case)
        }
        std::string name;
        for (const app::ResourceRow& resource : plan.resources) {
            if (resource.uid == assignment.resource_uid) {
                name = resource.name;
                break;
            }
        }
        if (name.empty()) {
            continue;  // a resource the plan does not know: no invented "resource N"
        }
        if (!text.empty()) {
            text += ", ";  // Project's Resource Names separator
        }
        text += name;
    }
    return text;
}

void PlanDocument::reset() {
    sheet_.reset();
    loaded_ = app::PlanInput{};
    base_ = app::PlanInput{};
    view_ = app::PlanInput{};
    records_ = core::view::RecordSet{};
    format_ = core::sheet::SheetFormat{};
    row_map_ = app::RowMap{};
    gantt_ = core::gantt::GanttLayout{};
    filter_definition_ = core::view::FilterDefinition{};
    filter_rows_.clear();
    filter_description_.clear();
    filter_active_ = false;
    sort_definition_ = core::view::SortDefinition{};
    sort_description_.clear();
    sort_active_ = false;
    tables_.clear();
    table_ = msproject::viewstate::TableDef{};
    selected_table_ = 0;
    gantt_style_ = msproject::viewstate::GanttStyle{};
    use_stored_timescale_ = false;
    scroll_origin_ = core::sched::kDateNa;
    scroll_origin_set_ = false;
    applied_view_.clear();
    path_.clear();
    title_.clear();
    warnings_ = 0;
    show_project_summary_ = false;
    project_summary_override_ = false;
}

bool PlanDocument::load(const std::string& source_path, app::ZoomLevel zoom,
                        std::string& error) {
    reset();
    error.clear();
    try {
        const mpp::Document document = mpp::load_file(source_path);
        loaded_ = app::plan_input_from_document(document);
        // Wave 24 (W24-2): the project-summary row and the Timeline strip are
        // FILE state (214/212/Props 0x03400009 and 0x03400008), decoded by the
        // reader into PlanInput.  Wave 23's hard-coded "on" and its
        // application-state timeline conclusion came from a props diff that
        // never decoded the record framing; see core/mpp/reader.h ProjectRow.
        // Nothing is forced here: a fixture that stores 0 stays hidden.
        loaded_.source_name = std::filesystem::path(source_path).filename().string();
        warnings_ = document.warnings.size();
        title_ = loaded_.title;
        path_ = source_path;
        // No pending structural edits yet: the display plan starts as the file.
        base_ = loaded_;
        // Wave 26: the Project Summary Task toggle starts at the file's stored
        // state (a new load discards an earlier toggle).
        show_project_summary_ = loaded_.show_project_summary;
        project_summary_override_ = false;

        // Wave 21 (operator decision 7): the file's own table decides the sheet's
        // columns/order/titles/widths.  Reading it BEFORE the sheet is built is
        // what makes the presentation the file's rather than the Entry default.
        // A decode failure only means the sheet keeps app::default_sheet_columns().
        read_table(source_path);
        // Wave 23 (B-2): which view the file was left in, for the Timeline strip.
        read_applied_view(source_path);
        // Wave 26 (W26-3.1): the applied Gantt view's own style record.  The
        // applied-view name read above selects the view; a decode failure only
        // means the sheet keeps the stock presentation.
        read_style(source_path);
        // Wave 29 (W29-1.4): a load shows the view's saved scroll origin and
        // size; moving the shell's zoom slider (`rebuild_views(..., true)`)
        // hands the axis back to the three zoom levels.
        use_stored_timescale_ = true;

        // The RecordSet is a stable member: SheetModel holds a reference to it,
        // so assignment (not replacement) is what keeps that reference valid.
        records_ = app::make_record_set(base_);
        sheet_ = std::make_unique<core::sheet::SheetModel>(records_, stored_columns());
        // The date format is the SheetFormat default this wave; the preference
        // panel that changes it is a later stream (app/prefs.h's
        // apply_preferences()).
        format_ = core::sheet::SheetFormat{};
        // Wave 23 (W23-7): the file's unit labels (Options > Edit), which decide
        // whether a lag prints "50 d" or "50 days" -- Project_a stores the
        // shortest form and prints "9FS+50 d" in both its grid and COM.
        apply_file_labels(source_path, format_);
        sheet_->set_format(format_);
        // UID, ID and Outline are identity/calculated columns: the layout model
        // declares the policy (core::sheet deliberately does not infer it).
        app::apply_edit_policy(*sheet_);

        // Open with the first visible row selected, so the Gantt shows a
        // highlighted bar and a later keyboard command has somewhere to go.
        if (sheet_->visible_row_count() != 0) {
            sheet_->select_cell(sheet_->visible_row(0), name_column_of(*sheet_));
        }

        build_gantt(zoom);
        return true;
    } catch (const std::exception& failure) {
        reset();
        error = failure.what();
        return false;
    }
}

void PlanDocument::build_gantt(app::ZoomLevel zoom) {
    if (sheet_ == nullptr) {
        return;
    }
    // The RecordSet -- not the loaded plan -- is the source of truth for task
    // fields once a cell is edited, so re-derive the plan the views draw from.
    view_ = app::sync_plan_from_records(base_, records_);
    row_map_ = app::build_row_map(*sheet_);
    // Wave 31 (W31-1.1): one display-order path for the table and the chart.
    // The sort is session-only (the RecordSet and the file are untouched) and
    // the filter restricts the DISPLAYED row list; the same rows stay editable
    // and the Save diff is unchanged (app/filter_model.h, app/sort_model.h).
    row_map_.rows = ordered_display_rows();
    row_map_.uids.clear();
    row_map_.uids.reserve(row_map_.rows.size());
    for (const std::size_t row : row_map_.rows) {
        row_map_.uids.push_back(records_.uid(row));
    }
    // The Gantt is rebuilt from exactly the displayed rows, never panned, so
    // Gantt row N is displayed ordinal N (app/selection_model.h).  Wave 25
    // (W25-1/S1): the displayed rows are RecordSet rows and the source resolves
    // each through its uid -- no display-plan copy with the project row spliced
    // in, so the project row works whether or not the file shows it.
    core::gantt::GanttSource source =
        app::make_gantt_source_for_displayed_rows(view_, records_, row_map_.rows, {},
                                                  core::gantt::kDateNa);
    // Wave 29 (W29-1.3): the applied view's stored bar styles decide which
    // text each bar shows and on which side; see apply_stored_bar_labels().
    apply_stored_bar_labels(source);
    // No status-date marker: app::today_date32() would make the offscreen render
    // change every day, and this wave's render must be deterministic.
    core::gantt::Timescale timescale = app::timescale_for_zoom(view_, app::gantt_zoom(zoom));
    // Wave 29 (W29-1.4): the applied view's saved scroll origin and size win on
    // load.  They are the applied view's, not the preference's, until the user
    // picks one of the shell's zoom levels (rebuild_views(..., true)).
    app::StoredTimescale stored;
    if (use_stored_timescale_) {
        stored = app::apply_stored_timescale(gantt_style_, timescale);
    }
    if (zoom == app::ZoomLevel::week) {
        // The reference's weekly view: the *unit* stays week (so the header is a
        // week tier), but its pixels-per-unit is the measured 24 px/day, and the
        // day row starts on Sunday (MS Project's stock calendar week).  A stored
        // zoom replaces the unit and the width; the week anchor still applies.
        timescale.week_start = 0;
        if (!stored.zoom) {
            timescale.unit = core::gantt::TimescaleUnit::week;
            timescale.pixels_per_unit = kPixelsPerWeek;
        }
    }
    // Wave 30 (W30-1.7): Task > Editing > Scroll to Task pins the chart's left
    // edge to the selected task's start for the rest of the session, through
    // the same Timescale::pin_origin path the file's stored origin uses (the
    // bar sits at x = 0, earlier bars are clipped left).  It runs after the
    // stored/zoom origin, so the command wins over both.
    if (scroll_origin_set_) {
        timescale.origin = scroll_origin_;
        timescale.pin_origin = true;
    }
    // The Gantt's row pitch must equal the sheet's 22 px rows, or the two halves
    // drift apart one row-height style at a time.
    core::gantt::GanttStyle style = app::default_gantt_style();
    style.row_height = static_cast<double>(kRowHeight);
    gantt_ = core::gantt::build_gantt(source, style, timescale);

    // Wave 22: `view_` is now the current plan (file + structural edits + the
    // sheet's committed cells), so this is the one place where the computed
    // column texts can be refreshed without a second derivation path.
    fill_computed_columns();
}

void PlanDocument::apply_stored_bar_labels(core::gantt::GanttSource& source) {
    if (!gantt_style_.decoded) {
        return;
    }
    // A bar-label date is a BARE short date: the file's order, separator and
    // year width, without the sheet's weekday.  The clean capture's milestone
    // prints "14/04/26", not the Date cell's "Tue 14/04/26".
    core::sheet::SheetFormat label_format = format_;
    label_format.date.show_weekday = false;
    label_format.date.show_time_of_day = false;
    const auto date_text = [&label_format](core::sched::date32 day) {
        return core::sheet::format_date_text(app::date_value_from_date32(day),
                                             label_format);
    };
    // The project row is not in view_.tasks: make_gantt_source_for_displayed_rows
    // synthesizes uid 0 from app::project_summary_row(), so the label facts come
    // from the same row.
    const app::TaskRow project_row = app::project_summary_row(view_);
    for (core::gantt::TaskBarInput& bar : source.tasks) {
        const app::TaskRow* row = nullptr;
        if (bar.uid == 0) {
            row = &project_row;
        } else {
            for (const app::TaskRow& candidate : view_.tasks) {
                if (candidate.uid == bar.uid) {
                    row = &candidate;
                    break;
                }
            }
        }
        if (row == nullptr) {
            continue;
        }
        app::BarLabelTask task;
        task.project_summary = bar.uid == 0;
        task.summary = row->summary;
        task.milestone = row->milestone;
        task.critical = row->critical;
        task.manually_scheduled = row->manually_scheduled;
        task.finished = row->percent_complete >= 100;
        task.in_progress = row->percent_complete > 0 && row->percent_complete < 100;

        app::BarLabelValues values;
        values.name = row->name;
        values.id = std::to_string(row->id);
        values.start = date_text(row->start);
        values.finish = date_text(row->finish);
        values.duration =
            core::sheet::format_duration_text(row->duration_ticks, format_);
        values.resource = resource_names_text(view_, row->uid);
        values.percent = core::sheet::format_value_text(
            core::view::Value::of_int(row->percent_complete),
            core::sheet::CellKind::Percent, format_);

        const app::BarLabelTexts texts =
            app::bar_label_texts_for(gantt_style_, task, values);
        bar.sides = {texts.left, texts.right, texts.top, texts.bottom, texts.inside};
        bar.stored_labels = true;
    }
}

void PlanDocument::fill_computed_columns() {
    // The three columns Project computes from the link/assignment tables.  The
    // reader never produces them (no descriptor name, no RecordSet column; see
    // entry_columns.h), so the sheet's stored column for one of those ids would
    // otherwise render blank.  Filling the RecordSet is what makes the model and
    // the delegate paint them like any other text cell -- no special case in the
    // view layer.  A row with nothing to show keeps Null (not ""), so the cell is
    // empty rather than "a value that is the empty string".
    if (records_.row_count() == 0) {
        return;
    }
    for (const core::view::FieldId field : {kPredecessorsField, kSuccessorsField,
                                            kResourceNamesField}) {
        records_.add_field(field);  // a no-op when the file's table already made it
    }
    const std::size_t rows = records_.row_count();
    for (std::size_t row = 0; row < rows; ++row) {
        const std::uint32_t uid = records_.uid(row);
        std::string predecessors = predecessors_text(view_, uid, format_);
        if (!predecessors.empty()) {
            records_.set_field(row, kPredecessorsField,
                               core::view::Value::of_text(std::move(predecessors)));
        }
        std::string successors = successors_text(view_, uid, format_);
        if (!successors.empty()) {
            records_.set_field(row, kSuccessorsField,
                               core::view::Value::of_text(std::move(successors)));
        }
        std::string resources = resource_names_text(view_, uid);
        if (!resources.empty()) {
            records_.set_field(row, kResourceNamesField,
                               core::view::Value::of_text(std::move(resources)));
        }
    }
}

void PlanDocument::set_session_gantt_style(
    const msproject::viewstate::GanttStyle& style, app::ZoomLevel zoom) {
    // Assignment, not replacement: gantt_style_'s address is what SheetTableModel
    // and GanttCanvas hold.  The Gantt is rebuilt so W29-1.3's stored bar labels
    // are derived again from the edited styles; the sheet is repainted by the
    // shell's after_model_change().
    gantt_style_ = style;
    rebuild_views(zoom);
}

void PlanDocument::rebuild_views(app::ZoomLevel zoom, bool from_user_zoom) {
    if (from_user_zoom) {
        // The three shell zoom levels take over from the file's stored zoom.
        use_stored_timescale_ = false;
    }
    build_gantt(zoom);
}

void PlanDocument::apply_preferences(const app::Preferences& preferences) {
    // The date format is a property of the sheet's formatter, not of the data:
    // every date cell, the status line and the PDF report read this one format.
    app::apply_preferences(preferences, format_);
    if (sheet_ != nullptr) {
        sheet_->set_format(format_);
    }
}

void PlanDocument::build_empty_view(app::ZoomLevel zoom, core::sched::date32 today) {
    // No document: a new project.  The chart still needs an axis, and the axis
    // must start on the CURRENT week -- the engine's Timescale default origin is
    // 1984-01-02, which is the defect this replaces.
    use_stored_timescale_ = false;  // there is no file view to take a scale from
    core::gantt::GanttSource source;  // no tasks, no bars, no links
    core::gantt::GanttStyle style = app::default_gantt_style();
    style.row_height = static_cast<double>(kRowHeight);
    core::gantt::Timescale timescale =
        app::timescale_for_zoom(view_, app::gantt_zoom(zoom));
    if (zoom == app::ZoomLevel::week) {
        // The reference's weekly view: week unit, 24 px/day, Sunday-first.
        timescale.unit = core::gantt::TimescaleUnit::week;
        timescale.week_start = 0;
        timescale.pixels_per_unit = kPixelsPerWeek;
    }
    core::sched::date32 day = today;
    if (core::sched::is_na(day) || core::sched::is_overflow(day)) {
        day = app::today_date32();
    }
    day = core::sched::date_only(day);
    // Start the visible axis at the week containing today (Sunday 00:00), so the
    // first header tier is the current week for every zoom level.
    const std::int32_t weekday = core::sched::weekday(day);
    timescale.origin = core::sched::add_days(day, -weekday);
    gantt_ = core::gantt::build_gantt(source, style, timescale);
    row_map_ = app::RowMap{};
}

void PlanDocument::refresh_filter_rows() {
    if (!filter_active_ || sheet_ == nullptr) {
        filter_rows_.clear();
        return;
    }
    filter_rows_ = app::filter_rows(filter_definition_, records_);
}

void PlanDocument::rebuild_from_base(app::ZoomLevel zoom, std::uint32_t keep_uid) {
    // Keep the current selection's task across the rebuild by uid; a pending
    // insert asks for its own placeholder uid instead.
    std::uint32_t wanted = keep_uid;
    if (wanted == 0 && sheet_ != nullptr && sheet_->has_selection()) {
        wanted = records_.uid(sheet_->anchor().row);
    }

    records_ = app::make_record_set(base_);
    sheet_ = std::make_unique<core::sheet::SheetModel>(records_, stored_columns());
    sheet_->set_format(format_);
    app::apply_edit_policy(*sheet_);
    refresh_filter_rows();

    if (sheet_->visible_row_count() != 0) {
        std::size_t target = sheet_->visible_row(0);
        if (wanted != 0) {
            for (std::size_t ordinal = 0; ordinal < sheet_->visible_row_count(); ++ordinal) {
                const std::size_t row = sheet_->visible_row(ordinal);
                if (row != core::sheet::kNoIndex && records_.uid(row) == wanted) {
                    target = row;
                    break;
                }
            }
        }
        sheet_->select_cell(target, name_column_of(*sheet_));
    }
    build_gantt(zoom);
}

void PlanDocument::apply_structural_edits(const app::StructuralEdits& edits, app::ZoomLevel zoom,
                                          std::uint32_t keep_uid) {
    if (sheet_ == nullptr) {
        return;
    }
    // Fold the CURRENT cell values back into the plan before the rebuild: the
    // sheet's edits (a Start, say) are still pending file changes, and building
    // from `loaded_` alone would silently drop them.
    // without_structural_edits() recovers "the file plus those edits", so
    // apply() then re-applies this edit list on top without double-counting.
    const app::PlanInput edited = edits.without_structural_edits(view_, loaded_);
    base_ = edits.apply(edited);
    // Wave 26: a ribbon toggle outranks the file's stored setting across a
    // structural rebuild, which re-derives `base_` from `loaded_`.
    if (project_summary_override_) {
        base_.show_project_summary = show_project_summary_;
    }
    rebuild_from_base(zoom, keep_uid);
}

void PlanDocument::set_project_summary_visible(bool visible, app::ZoomLevel zoom) {
    show_project_summary_ = visible;
    project_summary_override_ = true;
    if (sheet_ == nullptr) {
        return;
    }
    // The RecordSet (and so the sheet, the Gantt rows and every uid lookup) is
    // built from `base_`: flipping this presentation flag and rebuilding is the
    // same path a structural edit takes.
    // Fold the CURRENT cell values back into `base_` first: a pending Start (or
    // any other cell edit) lives only in `records_`, and rebuilding from the
    // old `base_` would silently drop it before Save can write it.
    base_ = app::sync_plan_from_records(base_, records_);
    base_.show_project_summary = visible;
    rebuild_from_base(zoom, 0);
}

bool PlanDocument::scroll_gantt_to(std::uint32_t uid, app::ZoomLevel zoom) {
    if (sheet_ == nullptr) {
        return false;
    }
    // uid 0 is the synthetic project row, which is never a task (W24-1).
    const app::TaskRow* task = task_by_uid(uid);
    if (task == nullptr || core::sched::is_na(task->start) ||
        core::sched::is_overflow(task->start)) {
        return false;
    }
    // A bare day: the chart's left edge, not the bar's time of day.  The bar
    // itself starts at its own time within that day, so it stays visible.
    scroll_origin_ = core::sched::date_only(task->start);
    scroll_origin_set_ = true;
    build_gantt(zoom);
    return true;
}

void PlanDocument::accept_start_edits(const std::vector<app::StartEdit>& edits) {
    app::accept_start_edits(loaded_, edits);
}

void PlanDocument::set_filter(core::view::FilterDefinition definition, std::string description) {
    filter_definition_ = std::move(definition);
    filter_description_ = std::move(description);
    filter_active_ = true;
    refresh_filter_rows();
}

void PlanDocument::clear_filter() {
    filter_active_ = false;
    filter_definition_ = core::view::FilterDefinition{};
    filter_description_.clear();
    filter_rows_.clear();
}

void PlanDocument::set_sort(core::view::SortDefinition definition, std::string description) {
    sort_definition_ = std::move(definition);
    sort_description_ = std::move(description);
    sort_active_ = true;
}

void PlanDocument::clear_sort() {
    sort_active_ = false;
    sort_definition_ = core::view::SortDefinition{};
    sort_description_.clear();
}

bool PlanDocument::show_outline_level(int level) {
    if (sheet_ == nullptr || level < 1 || level > 9) {
        return false;
    }
    // Project's "Level N" is 1-based and this port's outline levels are 0-based:
    // showing through level N means every row at port level N-1 stays, and every
    // summary at that level collapses so its children (port level N) hide.
    sheet_->expand_all();
    const int target = level - 1;
    for (std::size_t row = 0; row < sheet_->row_count(); ++row) {
        if (sheet_->is_summary(row) && sheet_->outline_level(row) == target) {
            sheet_->collapse(row);
        }
    }
    return true;
}

void PlanDocument::show_all_subtasks() {
    if (sheet_ != nullptr) {
        sheet_->expand_all();
    }
}

std::vector<std::size_t> PlanDocument::ordered_display_rows() const {
    if (sheet_ == nullptr) {
        return {};
    }
    std::vector<std::size_t> rows = sheet_->visible_rows();
    if (sort_active_) {
        rows = app::sort_display_rows(sort_definition_, records_, rows);
    }
    if (filter_active_) {
        rows = app::restrict_to_filter(rows, filter_rows_);
    }
    return rows;
}

std::vector<std::size_t> PlanDocument::display_rows() const {
    return ordered_display_rows();
}

void PlanDocument::read_table(const std::string& source_path) {
    tables_.clear();
    table_ = msproject::viewstate::TableDef{};
    selected_table_ = 0;
    try {
        const mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(source_path);
        std::array<std::vector<std::uint8_t>, 4> raw;
        for (std::size_t index = 0; index < raw.size(); ++index) {
            if (!document.has_stream(kTableParts[index])) {
                return;
            }
            raw[index] = document.read_stream(kTableParts[index]);
        }
        msproject::viewstate::PartSet parts;
        parts.fixed_data = raw[0];
        parts.fixed_meta = raw[1];
        parts.var_meta = raw[2];
        parts.var2_data = raw[3];

        msproject::viewstate::ViewState state;
        if (!msproject::viewstate::parse_view_state("CTable", parts, state, nullptr)) {
            return;
        }
        if (state.tables.empty()) {
            return;
        }
        // Wave 31 (W31-1.3): keep every TASK table in file order.  The initial
        // applied table stays the file's first task table -- on the measured
        // fixtures and both corpus files that is the task Entry table, which is
        // also the table the Gantt view names.  Keeping the whole TableDef --
        // caption, date format and every ColumnDef in file order -- is what the
        // sheet, the menu and a later CTable write need.
        for (const msproject::viewstate::TableDef& definition : state.tables) {
            if (is_task_table(definition)) {
                tables_.push_back(definition);
            }
        }
        if (tables_.empty()) {
            return;
        }
        table_ = tables_.front();
    } catch (...) {
        // A view-state decode is presentation metadata: never fail the load.
        tables_.clear();
        table_ = msproject::viewstate::TableDef{};
        selected_table_ = 0;
    }
}

std::string PlanDocument::table_caption(std::size_t index) const {
    if (index >= tables_.size()) {
        return {};
    }
    const msproject::viewstate::TableDef& definition = tables_[index];
    std::string caption =
        definition.display_caption.empty() ? definition.caption : definition.display_caption;
    // Project stores the menu accelerator in the caption ("&Entry"); the menu
    // label itself does not draw it.
    caption.erase(std::remove(caption.begin(), caption.end(), '&'), caption.end());
    return caption;
}

bool PlanDocument::select_table(std::size_t index) {
    if (sheet_ == nullptr || index >= tables_.size()) {
        return false;
    }
    selected_table_ = index;
    table_ = tables_[index];
    // Only the sheet's column set changes: same RecordSet, same collapse state
    // and selection (SheetModel clamps a selection past the new column count).
    // apply_edit_policy keeps the editable-column rule the load applied.
    sheet_->set_columns(stored_columns());
    app::apply_edit_policy(*sheet_);
    return true;
}

void PlanDocument::read_applied_view(const std::string& source_path) {
    applied_view_.clear();
    try {
        const mppwrite::CfbDocument document = mppwrite::CfbDocument::from_file(source_path);
        std::array<std::vector<std::uint8_t>, 4> raw;
        for (std::size_t index = 0; index < raw.size(); ++index) {
            if (!document.has_stream(kEdlParts[index])) {
                return;
            }
            raw[index] = document.read_stream(kEdlParts[index]);
        }
        msproject::viewstate::PartSet parts;
        parts.fixed_data = raw[0];
        parts.fixed_meta = raw[1];
        parts.var_meta = raw[2];
        parts.var2_data = raw[3];
        msproject::viewstate::ViewState state;
        if (!msproject::viewstate::parse_view_state("CEdl", parts, state, nullptr)) {
            return;
        }
        // The CEdl block named 0x26400018 is the applied view.  The stream keeps
        // a per-save cache of older documents too, so only the FIRST decoded
        // name is taken; on every measured file the caches agree.
        for (const msproject::viewstate::EmbeddedProps& props : state.embedded_props) {
            if (props.view_name_decoded && !props.view_name.empty()) {
                applied_view_ = props.view_name;
                break;
            }
        }
    } catch (...) {
        // View state is presentation metadata; a decode failure never fails the
        // load.
        return;
    }
}

// Wave 26 (W26-3.1): the applied view's Gantt style record.  The shared app
// seam (app/view_style.h) reads 214/Props's FONT_BASES, the CV_iew definitions
// and the applied view name from 214/CEdl; a file without a decodable Gantt
// record is presentation metadata only, so a failure never fails the load.
void PlanDocument::read_style(const std::string& source_path) {
    gantt_style_ = msproject::viewstate::GanttStyle{};
    cell_formats_ = msproject::viewstate::CellFormatTable{};
    std::string reason;
    if (!app::read_gantt_style(source_path, gantt_style_, &reason, &cell_formats_)) {
        gantt_style_ = msproject::viewstate::GanttStyle{};
        cell_formats_ = msproject::viewstate::CellFormatTable{};
    }
}

std::vector<core::sheet::SheetColumn> PlanDocument::stored_columns() const {    std::vector<core::sheet::SheetColumn> columns = app::table_sheet_columns(table_);
    // Wave 22: % Complete (0x0b400020) is a PERCENT, and Project prints the sign
    // -- the clean capture's column reads "57%", "100%", "0%".  app::
    // table_sheet_columns gives a field it does not know the sheet's default kind
    // (Auto), which would print the bare stored integer ("57").  Declaring the
    // kind here fixes the display, the clipboard text and any other consumer of
    // the sheet at once, instead of special-casing one painter.
    for (core::sheet::SheetColumn& column : columns) {
        if (column.field == kPercentCompleteField) {
            column.kind = core::sheet::CellKind::Percent;
        }
    }
    return columns;
}

int PlanDocument::table_column_chars(std::uint32_t table_field) const {
    if (table_field == 0) {
        return 0;
    }
    for (const msproject::viewstate::ColumnDef& column : table_.columns) {
        if (column.field_id == table_field) {
            return static_cast<int>(column.opaque_header[0]);
        }
    }
    return 0;
}

std::uint32_t PlanDocument::selected_uid() const {
    if (sheet_ == nullptr) {
        return 0;
    }
    return app::selected_task(*sheet_, row_map_).uid;
}

const app::TaskRow* PlanDocument::task_by_uid(std::uint32_t uid) const {
    if (uid == 0) {
        return nullptr;  // the synthetic project row is not a task
    }
    for (const app::TaskRow& task : view_.tasks) {
        if (task.uid == uid) {
            return &task;
        }
    }
    return nullptr;
}

}  // namespace msqt
