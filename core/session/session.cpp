// core/session/session.cpp -- see session.h for the contract.
//
// The whole pipeline lives here and nowhere else:
//
//   mpp::load_file            (core/mpp)
//   core::plan::from_mpp      (core/plan bridge)
//   core::cal load_project_calendar -> PlanInput::calendar
//   core::plan::recalculate   (core/sched CPM + summary rollup)
//   RecordSet / SheetModel    (core/view, core/sheet)
//   GanttSource               (core/gantt)
//
// No AppKit, no ObjC, no file I/O outside load(), no globals with mutable state,
// and no exception escapes load().  The only function-local static is the
// immutable TaskFields cache below (the same pattern app/layout_model.cpp uses).
//
// CLIENT DATA.  Nothing in the warnings, status_text() or dump() output is a
// task, resource, assignment or calendar name from the file: counts, uids,
// dates, durations and flags only.  Reader warnings (which can quote a name or
// an orphan name) are COUNTED, never copied.  The calendar decoder's error text
// can quote a calendar name, so it is never copied either -- the fallback is
// reported as a count-free sentence.

#include "session.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "cal.h"
#include "field_ref.h"
#include "plan.h"
#include "reader.h"

namespace core::session {
namespace {

// ---------------------------------------------------------------------------
// Field resolution
// ---------------------------------------------------------------------------

core::view::FieldId resolve_field(std::string_view name) {
    core::view::FieldId field = 0;
    if (!core::view::field_id_by_name(core::model::RecordType::Task, name, field)) {
        return 0;
    }
    return field;
}

// Resolved once per process.  Immutable after construction; a table regeneration
// that drops a name yields id 0 and that column/value is simply omitted (the
// other views stay consistent because every caller reads the same struct).
struct TaskFields {
    core::view::FieldId uid = kUidField;
    core::view::FieldId id = 0;
    core::view::FieldId outline_number = 0;
    core::view::FieldId name = 0;
    core::view::FieldId start = 0;
    core::view::FieldId finish = 0;
    core::view::FieldId duration = 0;
    core::view::FieldId percent_complete = 0;
    core::view::FieldId summary = 0;
    core::view::FieldId milestone = 0;
    core::view::FieldId critical = 0;
    core::view::FieldId outline_level = 0;
    core::view::FieldId total_slack = 0;

    TaskFields()
        : id(resolve_field("ID")),
          outline_number(resolve_field("OutlineNumber")),
          name(resolve_field("Name")),
          start(resolve_field("Start")),
          finish(resolve_field("Finish")),
          duration(resolve_field("Duration")),
          percent_complete(resolve_field("PercentComplete")),
          summary(resolve_field("Summary")),
          milestone(resolve_field("Milestone")),
          critical(resolve_field("Critical")),
          outline_level(resolve_field("OutlineLevel")),
          total_slack(resolve_field("TotalSlack")) {}
};

TaskFields const& task_fields() {
    static const TaskFields fields;
    return fields;
}

// ---------------------------------------------------------------------------
// Encodings
// ---------------------------------------------------------------------------

// mpp::PackedDate::raw IS a core::sched::date32 value (see session.h "DATES"):
// only the not-a-date words and values outside date32's usable domain are
// translated.  No day shift.
date32 date32_from_packed(std::uint32_t raw) noexcept {
    if (raw == 0u || raw == 0xFFFFFFFFu) {
        return kDateNa;
    }
    const std::uint32_t day = raw >> 16;
    if (day < static_cast<std::uint32_t>(core::sched::kMinDay) ||
        day > static_cast<std::uint32_t>(core::sched::kMaxDay)) {
        return kDateNa;
    }
    if ((raw & 0xFFFFu) >= static_cast<std::uint32_t>(core::sched::kTenthsPerDay)) {
        return kDateNa;  // damaged time-of-day word
    }
    return static_cast<date32>(raw);
}

core::view::DateValue date_value_from_date32(date32 day) noexcept {
    core::view::DateValue value;
    if (day == kDateNa || core::sched::is_overflow(day)) {
        return value;
    }
    const core::sched::CivilDate civil = core::sched::to_civil(day);
    const std::int32_t tenths = core::sched::time_of(day);
    value.year = civil.year;
    value.month = static_cast<std::uint8_t>(civil.month);
    value.day = static_cast<std::uint8_t>(civil.day);
    value.hour = static_cast<std::uint8_t>(tenths / 600);
    value.minute = static_cast<std::uint8_t>((tenths % 600) / 10);
    value.second = static_cast<std::uint8_t>((tenths % 10) * 6);
    return value;
}

double clamp_unit(std::uint16_t percent) noexcept {
    if (percent >= 100) {
        return 1.0;
    }
    return static_cast<double>(percent) / 100.0;
}

core::gantt::LinkType gantt_link_type(mpp::LinkType type) noexcept {
    switch (type) {
        case mpp::LinkType::finish_finish:
            return core::gantt::LinkType::finish_to_finish;
        case mpp::LinkType::finish_start:
            return core::gantt::LinkType::finish_to_start;
        case mpp::LinkType::start_finish:
            return core::gantt::LinkType::start_to_finish;
        case mpp::LinkType::start_start:
            return core::gantt::LinkType::start_to_start;
    }
    return core::gantt::LinkType::finish_to_start;
}

std::string date_text(date32 day) { return core::plan::format_date(day); }

std::string count_text(std::size_t value) { return std::to_string(value); }

}  // namespace

// ---------------------------------------------------------------------------
// core::plan state, kept out of session.h so a shell never includes core/plan
// ---------------------------------------------------------------------------

struct Session::PlanData {
    core::plan::PlanInput input;
    core::plan::PlanResult result;
    // Set only if core::plan::recalculate() ever threw.  It is documented not to;
    // the session still must not let an exception out of load()/recalculate().
    bool threw = false;
    std::string throw_message;
};

// ---------------------------------------------------------------------------
// Free functions in the public API
// ---------------------------------------------------------------------------

core::view::FieldId task_field_id(std::string_view field_name) {
    return resolve_field(field_name);
}

std::vector<core::sheet::SheetColumn> default_sheet_columns() {
    const TaskFields& fields = task_fields();
    std::vector<core::sheet::SheetColumn> columns;
    columns.reserve(8);

    const auto add = [&columns](core::view::FieldId field, const char* title, int width,
                                core::view::ColumnAlign align, core::sheet::CellKind kind,
                                bool read_only) {
        core::sheet::SheetColumn column;
        column.field = field;
        column.title = title;
        column.width = width;
        column.align = align;
        column.kind = kind;
        column.visible = true;
        column.read_only = read_only;
        columns.push_back(std::move(column));
    };

    add(fields.uid, "UID", 6, core::view::ColumnAlign::Right, core::sheet::CellKind::Integer,
        true);
    add(fields.id, "ID", 5, core::view::ColumnAlign::Right, core::sheet::CellKind::Integer,
        true);
    add(fields.outline_number, "Outline", 8, core::view::ColumnAlign::Left,
        core::sheet::CellKind::Text, true);
    add(fields.name, "Name", 34, core::view::ColumnAlign::Left, core::sheet::CellKind::Text,
        false);
    add(fields.start, "Start", 15, core::view::ColumnAlign::Left, core::sheet::CellKind::Date,
        false);
    add(fields.finish, "Finish", 15, core::view::ColumnAlign::Left, core::sheet::CellKind::Date,
        false);
    add(fields.duration, "Duration", 11, core::view::ColumnAlign::Left,
        core::sheet::CellKind::Duration, false);
    add(fields.total_slack, "Total Slack", 11, core::view::ColumnAlign::Left,
        core::sheet::CellKind::Duration, true);
    return columns;
}

core::sheet::SheetMetrics default_sheet_metrics() {
    core::sheet::SheetMetrics metrics;
    metrics.row_height = 20;
    metrics.header_height = 32;  // the two 16-point Gantt header tiers
    metrics.char_width = 7;
    metrics.indent_per_level = 10;
    metrics.min_column_width = 24;
    return metrics;
}

core::gantt::GanttStyle default_gantt_style() {
    core::gantt::GanttStyle style;
    style.row_height = static_cast<double>(default_sheet_metrics().row_height);
    return style;
}

// ---------------------------------------------------------------------------
// Session construction / destruction
// ---------------------------------------------------------------------------

Session::Session() = default;
Session::~Session() = default;

// ---------------------------------------------------------------------------
// View models
// ---------------------------------------------------------------------------

void Session::write_row_record(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    const Row& row = rows_[index];
    const TaskFields& field = task_fields();
    const auto put = [this, index](core::view::FieldId id, core::view::Value value) {
        if (id != 0) {
            records_.set_field(index, id, std::move(value));
        }
    };

    put(field.uid, core::view::Value::of_int(static_cast<std::int64_t>(row.uid)));
    put(field.id, core::view::Value::of_int(static_cast<std::int64_t>(row.id)));
    if (!row.outline_number.empty()) {
        put(field.outline_number, core::view::Value::of_text(row.outline_number));
    }
    put(field.name, core::view::Value::of_text(row.name));  // empty text is a real value
    if (!core::sched::is_na(row.start)) {
        put(field.start, core::view::Value::of_date(date_value_from_date32(row.start)));
    }
    if (!core::sched::is_na(row.finish)) {
        put(field.finish, core::view::Value::of_date(date_value_from_date32(row.finish)));
    }
    put(field.duration, core::view::Value::of_duration(row.duration_ticks));
    put(field.percent_complete,
        core::view::Value::of_int(static_cast<std::int64_t>(row.percent_complete)));
    put(field.summary, core::view::Value::of_bool(row.summary));
    put(field.milestone, core::view::Value::of_bool(row.milestone));
    put(field.critical, core::view::Value::of_bool(row.critical));
    put(field.outline_level, core::view::Value::of_int(static_cast<std::int64_t>(row.outline_level)));
    put(field.total_slack, core::view::Value::of_duration(row.total_slack));
}

void Session::build_views() {
    const TaskFields& field = task_fields();

    records_.clear();
    records_.set_record_type(core::model::RecordType::Task);
    const core::view::FieldId columns[] = {
        field.uid,        field.id,       field.outline_number, field.name,
        field.start,      field.finish,   field.duration,       field.percent_complete,
        field.summary,    field.milestone, field.critical,      field.outline_level,
        field.total_slack};
    for (const core::view::FieldId id : columns) {
        if (id != 0) {
            records_.add_field(id);
        }
    }
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        records_.add_row(rows_[index].uid);
        write_row_record(index);
    }

    sheet_ = std::make_unique<core::sheet::SheetModel>(records_, default_sheet_columns());
    metrics_ = default_sheet_metrics();
    gantt_style_ = default_gantt_style();

    gantt_ = core::gantt::GanttSource{};
    gantt_.tasks.reserve(rows_.size());
    for (const Row& row : rows_) {
        core::gantt::TaskBarInput task;
        task.uid = row.uid;
        task.label = row.name;
        task.start = row.start;
        task.finish = row.finish;
        task.percent_complete = clamp_unit(row.percent_complete);
        if (row.summary) {
            task.kind = core::gantt::BarKind::summary;
        } else if (row.milestone) {
            task.kind = core::gantt::BarKind::milestone;
        } else {
            task.kind = core::gantt::BarKind::normal;
        }
        task.critical = row.critical;
        gantt_.tasks.push_back(std::move(task));
    }
    gantt_.links.reserve(links_.size());
    for (const LoadedLink& link : links_) {
        core::gantt::LinkInput input;
        input.predecessor_uid = link.predecessor_uid;
        input.successor_uid = link.successor_uid;
        input.type = link.type;
        input.critical = link.critical;
        gantt_.links.push_back(input);
    }
    gantt_.working_week = core::gantt::WorkingWeek::monday_to_friday();
    gantt_.status_date = options_.status_date;
    // Real calendar shading (core/gantt copies it into a shared_ptr it owns).
    gantt_.set_calendar(calendar_);
}

void Session::refresh_views() {
    if (!sheet_) {
        return;  // views not built yet (load calls recalculate() before build_views())
    }
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        write_row_record(index);
    }
    const std::size_t count = std::min(rows_.size(), gantt_.tasks.size());
    for (std::size_t index = 0; index < count; ++index) {
        const Row& row = rows_[index];
        core::gantt::TaskBarInput& task = gantt_.tasks[index];
        task.start = row.start;
        task.finish = row.finish;
        task.percent_complete = clamp_unit(row.percent_complete);
        task.critical = row.critical;
    }
}

// ---------------------------------------------------------------------------
// Applying the reader / the plan result to the rows
// ---------------------------------------------------------------------------

void Session::apply_stored_values() {
    for (Row& row : rows_) {
        row.start = row.stored_start;
        row.finish = row.stored_finish;
        row.total_slack = row.stored_total_slack;
        row.critical = row.stored_critical;
        row.start_is_actual = row.stored_start_is_actual;
        row.finish_is_actual = row.stored_finish_is_actual;
    }
    status_.recalc_start_matches = 0;
    status_.recalc_finish_matches = 0;
    status_.recalc_slack_matches = 0;
    status_.recalc_critical_matches = 0;
    collect_status_counts();
    rebuild_status_dates();
}

void Session::apply_plan_result() {
    status_.recalc_error.clear();
    status_.recalculated = false;
    status_.recalc_tasks = 0;
    status_.recalc_start_matches = 0;
    status_.recalc_finish_matches = 0;
    status_.recalc_slack_matches = 0;
    status_.recalc_critical_matches = 0;
    status_.summary_links = 0;
    summary_flag_disagreements_ = 0;

    if (!plan_ || !recalc_attempted_) {
        apply_stored_values();
        rebuild_warnings();
        return;
    }
    if (plan_->threw) {
        status_.recalc_error = plan_->throw_message;
        apply_stored_values();
        rebuild_warnings();
        return;
    }

    const core::plan::PlanResult& result = plan_->result;
    status_.recalc_tasks = result.tasks.size();
    if (!result.ok()) {
        status_.recalc_error = result.message.empty()
                                   ? std::string("recalculation failed")
                                   : result.message;
        apply_stored_values();
        rebuild_warnings();
        return;
    }

    status_.recalculated = true;
    status_.summary_links = result.summary_links;

    // PlanResult::tasks is in PlanInput::tasks order, which is reader order, so
    // an index lookup normally succeeds; the uid map is the fallback and keeps
    // the association correct if core/plan ever reorders.
    std::map<std::int32_t, std::size_t> by_uid;
    for (std::size_t index = 0; index < result.tasks.size(); ++index) {
        by_uid.emplace(result.tasks[index].uid, index);
    }

    for (std::size_t index = 0; index < rows_.size(); ++index) {
        Row& row = rows_[index];
        const core::plan::PlanTaskResult* scheduled = nullptr;
        if (index < result.tasks.size() &&
            result.tasks[index].uid == static_cast<std::int32_t>(row.uid)) {
            scheduled = &result.tasks[index];
        } else {
            const auto found = by_uid.find(static_cast<std::int32_t>(row.uid));
            if (found != by_uid.end()) {
                scheduled = &result.tasks[found->second];
            }
        }
        if (scheduled == nullptr) {
            // No result for this row: keep what the file said rather than
            // inventing a schedule.  Counted, not named.
            ++summary_flag_disagreements_;
            continue;
        }
        row.start = scheduled->start;
        row.finish = scheduled->finish;
        row.total_slack = scheduled->total_slack;
        row.critical = scheduled->critical;
        row.start_is_actual = scheduled->start_is_actual;
        row.finish_is_actual = scheduled->finish_is_actual;
        if (row.start == row.stored_start) {
            ++status_.recalc_start_matches;
        }
        if (row.finish == row.stored_finish) {
            ++status_.recalc_finish_matches;
        }
        if (row.total_slack == row.stored_total_slack) {
            ++status_.recalc_slack_matches;
        }
        if (row.critical == row.stored_critical) {
            ++status_.recalc_critical_matches;
        }
        if (scheduled->summary != row.summary) {
            ++summary_flag_disagreements_;
        }
    }

    collect_status_counts();
    rebuild_status_dates();
    rebuild_warnings();
}

void Session::collect_status_counts() {
    status_.summaries = 0;
    status_.milestones = 0;
    status_.critical = 0;
    status_.actual_starts = 0;
    status_.actual_finishes = 0;
    status_.tasks_with_calendar = 0;
    for (const Row& row : rows_) {
        if (row.summary) {
            ++status_.summaries;
        }
        if (row.milestone) {
            ++status_.milestones;
        }
        if (row.critical) {
            ++status_.critical;
        }
        if (row.start_is_actual) {
            ++status_.actual_starts;
        }
        if (row.finish_is_actual) {
            ++status_.actual_finishes;
        }
        if (row.calendar_uid >= 0) {
            ++status_.tasks_with_calendar;
        }
    }
}

void Session::rebuild_status_dates() {
    status_.project_start = status_.stored_project_start;
    status_.project_finish = status_.stored_project_finish;
    if (!status_.recalculated || !plan_) {
        return;
    }
    if (!core::sched::is_na(plan_->result.project_start)) {
        status_.project_start = plan_->result.project_start;
    }
    if (!core::sched::is_na(plan_->result.project_finish)) {
        status_.project_finish = plan_->result.project_finish;
    }
}

void Session::rebuild_warnings() {
    status_.warnings.clear();
    if (calendar_fell_back_) {
        status_.warnings.emplace_back(
            "calendar: the file's calendar table could not be decoded; the stock "
            "Mon-Fri 08:00-12:00/13:00-17:00 week is used");
    }
    if (status_.tasks_with_calendar > 0) {
        status_.warnings.push_back("calendar: " + count_text(status_.tasks_with_calendar) +
                                   " task(s) name a task calendar; every task is scheduled "
                                   "on the project calendar");
    }
    if (reader_warnings_ > 0) {
        status_.warnings.push_back("reader: " + count_text(reader_warnings_) +
                                   " recoverable anomaly warning(s) (messages suppressed: "
                                   "they can quote client names)");
    }
    if (status_.skipped_links > 0) {
        status_.warnings.push_back("links: " + count_text(status_.skipped_links) +
                                   " link(s) name an unknown task and are not scheduled or "
                                   "drawn");
    }
    if (status_.summary_links > 0) {
        status_.warnings.push_back("links: " + count_text(status_.summary_links) +
                                   " link(s) touch a summary task and are not scheduled");
    }
    if (summary_flag_disagreements_ > 0) {
        status_.warnings.push_back("plan: " + count_text(summary_flag_disagreements_) +
                                   " row(s) disagree with the reader's Summary flag or have "
                                   "no schedule result");
    }
    if (recalc_attempted_ && !status_.recalculated) {
        status_.warnings.emplace_back(
            "schedule: recalculation failed; the stored dates are shown (see "
            "Status::recalc_error)");
    }
}

// ---------------------------------------------------------------------------
// Row accessors
// ---------------------------------------------------------------------------

Row const& Session::row(std::size_t index) const noexcept {
    static const Row empty;
    if (index >= rows_.size()) {
        return empty;
    }
    return rows_[index];
}

std::uint32_t Session::row_uid(std::size_t row) const noexcept {
    return row < records_.row_count() ? records_.uid(row) : 0u;
}

std::size_t Session::row_of_uid(std::uint32_t uid) const noexcept {
    const int index = records_.row_of_uid(uid);
    return index < 0 ? kNoRow : static_cast<std::size_t>(index);
}

date32 Session::task_start(std::size_t row) const noexcept { return this->row(row).start; }
date32 Session::task_finish(std::size_t row) const noexcept { return this->row(row).finish; }
date32 Session::stored_task_start(std::size_t row) const noexcept {
    return this->row(row).stored_start;
}
date32 Session::stored_task_finish(std::size_t row) const noexcept {
    return this->row(row).stored_finish;
}
ticks Session::task_total_slack(std::size_t row) const noexcept {
    return this->row(row).total_slack;
}
bool Session::task_critical(std::size_t row) const noexcept { return this->row(row).critical; }

// ---------------------------------------------------------------------------
// Recalculation (no file I/O)
// ---------------------------------------------------------------------------

bool Session::recalculate() {
    if (!plan_) {
        return false;
    }
    recalc_attempted_ = true;
    plan_->threw = false;
    plan_->throw_message.clear();
    try {
        plan_->result = core::plan::recalculate(plan_->input);
    } catch (const std::exception& error) {
        plan_->threw = true;
        plan_->throw_message = error.what();
        plan_->result = core::plan::PlanResult{};
    } catch (...) {
        plan_->threw = true;
        plan_->throw_message = "unknown exception from core::plan::recalculate";
        plan_->result = core::plan::PlanResult{};
    }
    apply_plan_result();
    refresh_views();
    return status_.recalculated;
}

// ---------------------------------------------------------------------------
// Gantt layout
// ---------------------------------------------------------------------------

core::gantt::GanttLayout Session::gantt_layout(double pixels_per_day) const {
    core::gantt::Timescale timescale;
    timescale.unit = core::gantt::TimescaleUnit::day;
    double per_day = pixels_per_day;
    if (!(per_day > 0.0)) {
        per_day = options_.pixels_per_day > 0.0 ? options_.pixels_per_day : 20.0;
    }
    timescale.pixels_per_unit = per_day;
    timescale.week_start = 1;  // Monday; build_gantt adopts the calendar's anchor

    // build_gantt() only ever moves the origin EARLIER, so seed it from the
    // project start (else the earliest task start) or the chart keeps the 1984
    // epoch and puts thousands of empty day columns in front of the first bar.
    date32 origin = status_.project_start;
    if (core::sched::is_na(origin)) {
        for (const Row& row : rows_) {
            if (core::sched::is_na(row.start)) {
                continue;
            }
            if (core::sched::is_na(origin) || row.start < origin) {
                origin = row.start;
            }
        }
    }
    if (!core::sched::is_na(origin)) {
        timescale.origin = core::sched::date_only(origin);
    }
    return core::gantt::build_gantt(gantt_, gantt_style_, timescale);
}

// ---------------------------------------------------------------------------
// Headless renderings (name-free: safe to print for a client file)
// ---------------------------------------------------------------------------

std::string Session::status_text() const {
    std::ostringstream out;
    out << "tasks " << status_.tasks << ", links " << status_.links << " ("
        << status_.links_with_live_endpoints << " live), skipped " << status_.skipped_links
        << " (reader rows " << status_.reader_skipped_link_rows << "), summary links "
        << status_.summary_links << '\n';
    out << "resources " << status_.resources << ", assignments " << status_.assignments << '\n';
    out << "summaries " << status_.summaries << ", milestones " << status_.milestones
        << ", critical " << status_.critical << '\n';
    out << "actual starts " << status_.actual_starts << ", actual finishes "
        << status_.actual_finishes << ", tasks with a task calendar "
        << status_.tasks_with_calendar << '\n';
    out << "project " << date_text(status_.project_start) << " .. "
        << date_text(status_.project_finish) << " (stored "
        << date_text(status_.stored_project_start) << " .. "
        << date_text(status_.stored_project_finish) << ")\n";
    if (status_.recalculated) {
        out << "recalc yes: start " << status_.recalc_start_matches << "/" << status_.recalc_tasks
            << ", finish " << status_.recalc_finish_matches << "/" << status_.recalc_tasks
            << ", slack " << status_.recalc_slack_matches << "/" << status_.recalc_tasks
            << ", critical " << status_.recalc_critical_matches << "/" << status_.recalc_tasks
            << '\n';
    } else {
        out << "recalc no: " << status_.recalc_tasks << " row(s) attempted"
            << (status_.recalc_error.empty() ? "" : ", error: ") << status_.recalc_error << '\n';
    }
    out << "warnings " << status_.warnings.size() << '\n';
    for (const std::string& warning : status_.warnings) {
        out << "  " << warning << '\n';
    }
    return out.str();
}

std::string Session::dump() const {
    std::ostringstream out;
    out << status_text();
    out << "uid\tid\tlevel\tstart\tfinish\tduration_min\tslack_min\tflags\n";
    for (const Row& row : rows_) {
        std::string flags;
        if (row.summary) {
            flags += 's';
        }
        if (row.milestone) {
            flags += 'm';
        }
        if (row.critical) {
            flags += 'c';
        }
        if (row.start_is_actual) {
            flags += 'a';
        }
        if (row.finish_is_actual) {
            flags += 'f';
        }
        if (flags.empty()) {
            flags = "-";
        }
        out << row.uid << '\t' << row.id << '\t' << row.outline_level << '\t'
            << date_text(row.start) << '\t' << date_text(row.finish) << '\t'
            << (row.duration_ticks / core::sched::kTenthsPerMinute) << '\t'
            << (row.total_slack / core::sched::kTenthsPerMinute) << '\t' << flags << '\n';
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

LoadResult load(std::string const& path, Options const& options) {
    LoadResult result;
    if (path.empty()) {
        result.error = "session: empty file path";
        return result;
    }

    try {
    std::unique_ptr<Session> session(new Session());
    session->path_ = path;
    session->options_ = options;
    session->status_.path = path;

    mpp::Document document;
    try {
        document = mpp::load_file(path);
    } catch (const std::exception& error) {
        result.error = "session: " + path + ": " + error.what();
        return result;
    } catch (...) {
        result.error = "session: " + path + ": unknown error while reading the file";
        return result;
    }

    session->title_ = document.project.title;
    session->status_.title = document.project.title;
    session->status_.stored_project_start =
        date32_from_packed(document.project.start.raw);
    session->status_.stored_project_finish =
        date32_from_packed(document.project.finish.raw);
    session->status_.tasks = document.tasks.size();
    session->status_.links = document.links.size();
    session->status_.links_with_live_endpoints = document.link_count_with_live_endpoints();
    session->status_.reader_skipped_link_rows = document.skipped_links.size();
    session->status_.resources = document.resources.size();
    session->status_.assignments = document.assignments.size();
    session->reader_warnings_ = document.warnings.size();

    session->rows_.reserve(document.tasks.size());
    for (const mpp::Task& task : document.tasks) {
        Row row;
        row.uid = task.uid;
        row.id = task.row_id;
        row.outline_number = task.outline_number;
        row.name = task.name;
        row.duration_ticks = static_cast<ticks>(task.duration_ticks);
        row.percent_complete = task.percent_complete;
        row.stored_start = date32_from_packed(task.start.raw);
        row.stored_finish = date32_from_packed(task.finish.raw);
        row.start = row.stored_start;
        row.finish = row.stored_finish;
        row.stored_total_slack = static_cast<ticks>(task.total_slack_ticks);
        row.total_slack = row.stored_total_slack;
        row.stored_critical = task.critical;
        row.critical = task.critical;
        row.stored_start_is_actual = task.actual_start.valid();
        row.stored_finish_is_actual = task.actual_finish.valid();
        row.start_is_actual = row.stored_start_is_actual;
        row.finish_is_actual = row.stored_finish_is_actual;
        row.outline_level =
            task.outline_level > 0 ? static_cast<int>(task.outline_level) - 1 : 0;
        row.summary = task.summary;
        row.milestone = task.is_milestone() && !task.summary;
        row.calendar_uid = task.calendar_uid;
        session->rows_.push_back(std::move(row));
    }

    session->links_.reserve(document.links.size());
    for (const mpp::Link& link : document.links) {
        if (!link.endpoints_live()) {
            ++session->status_.skipped_links;
            continue;
        }
        LoadedLink mapped;
        mapped.predecessor_uid = link.predecessor_uid;
        mapped.successor_uid = link.successor_uid;
        mapped.type = gantt_link_type(link.type());
        mapped.lag = static_cast<ticks>(link.lag_ticks);
        mapped.critical = false;  // no per-link critical flag in the reader
        session->links_.push_back(mapped);
    }

    // The plan bridge and the file's own calendar.
    session->plan_ = std::make_unique<Session::PlanData>();
    session->plan_->input = core::plan::from_mpp(document);
    if (options.use_file_calendar) {
        try {
            session->calendar_ =
                core::cal::load_project_calendar(path, options.calendar_name);
        } catch (const std::exception&) {
            // Never copy the decoder's message: it can quote a calendar name.
            session->calendar_ = core::plan::standard_calendar();
            session->calendar_fell_back_ = true;
        } catch (...) {
            session->calendar_ = core::plan::standard_calendar();
            session->calendar_fell_back_ = true;
        }
    } else {
        session->calendar_ = core::plan::standard_calendar();
    }
    session->plan_->input.calendar = session->calendar_;

    session->apply_stored_values();
    session->rebuild_warnings();
    if (options.recalculate) {
        session->recalculate();
    }
    try {
        session->build_views();
    } catch (const std::exception& error) {
        result.error = "session: " + path + ": could not build the view models: " +
                       error.what();
        return result;
    } catch (...) {
        result.error = "session: " + path + ": could not build the view models";
        return result;
    }

        result.session = std::move(session);
        return result;
    } catch (const std::exception& error) {
        // The reader has its own catch above; this one covers the bridge, the
        // calendar fallback path and the view models, so nothing escapes load().
        result.session.reset();
        result.error = "session: " + path + ": " + error.what();
        return result;
    } catch (...) {
        result.session.reset();
        result.error = "session: " + path + ": unknown error";
        return result;
    }
}

}  // namespace core::session
