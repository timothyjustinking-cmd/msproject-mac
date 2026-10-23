// app/project_dialogs.cpp -- see project_dialogs.h.

#include "project_dialogs.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "cal.h"
#include "field_value.h"  // mpp::custom_field_id / storage measured
#include "fields_model.h"  // app::read_file_formulas

namespace app {
namespace {

std::string date_text(date32 day, const core::sheet::SheetFormat& format) {
    if (day == kDateNa) {
        return "NA";
    }
    return core::sheet::format_date_text(date_value_from_date32(day), format);
}

}  // namespace

ProjectInformation project_information_from(const PlanInput& plan,
                                            std::vector<std::string> calendars,
                                            std::string default_calendar, date32 today,
                                            const core::sheet::SheetFormat& format) {
    ProjectInformation info;
    info.title = plan.title;
    info.start_text = date_text(plan.project_start, format);
    info.finish_text = date_text(plan.project_finish, format);
    info.current_text = date_text(today, format);
    info.start_day = plan.project_start;
    info.finish_day = plan.project_finish;
    info.current_day = today;
    info.has_start = plan.project_start != kDateNa;
    info.has_finish = plan.project_finish != kDateNa;
    info.calendars = std::move(calendars);
    info.calendar = std::move(default_calendar);
    info.has_calendar = !info.calendar.empty();
    // The file's project row always carries the priority word; the reader
    // defaults it to 0 for a producer that does not.  Project's new-project
    // value is 500, and PlanInput's default is 500 for exactly that reason.
    info.priority = static_cast<int>(plan.project_priority);
    // Status date has no decoded field in this port (core/mpp reader v0); the
    // capture's value is the file's project status date, not ours to invent.
    info.status_text = "NA";
    info.schedule_from = "Project Start Date";
    return info;
}

StructuralEdit make_project_priority_edit(int priority) {
    StructuralEdit edit;
    edit.kind = StructuralKind::set_project_priority;
    edit.uid = 0;  // the project row
    edit.priority = static_cast<std::uint16_t>(priority);
    return edit;
}

bool valid_project_priority(int priority) noexcept {
    return priority >= kMinPriority && priority <= kMaxPriority;
}

namespace {

const char* month_name(int month) {
    static const char* const kNames[] = {"January", "February", "March",     "April",
                                         "May",     "June",     "July",      "August",
                                         "September", "October", "November", "December"};
    if (month < 1 || month > 12) {
        return "";
    }
    return kNames[month - 1];
}

int day_number(int year, int month, int day) {
    return core::sched::day_of(core::sched::from_ymd(year, month, day));
}

int month_length(int year, int month) {
    const int next_year = month == 12 ? year + 1 : year;
    const int next_month = month == 12 ? 1 : month + 1;
    return day_number(next_year, next_month, 1) - day_number(year, month, 1);
}

std::string date_text_from_day(int day, const core::sheet::SheetFormat& format) {
    return core::sheet::format_date_text(
        app::date_value_from_date32(static_cast<app::date32>(day) << 16), format);
}

// "8:00 AM" from tenths of a minute (core::sched's interval unit).
std::string clock_text(std::int32_t tenths) {
    const std::int32_t minutes = tenths / 10;
    std::int32_t hour = minutes / 60;
    const std::int32_t minute = minutes % 60;
    const char* suffix = hour < 12 ? "AM" : "PM";
    if (hour == 0) {
        hour = 12;
    } else if (hour > 12) {
        hour -= 12;
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%d:%02d %s", static_cast<int>(hour),
                  static_cast<int>(minute), suffix);
    return text;
}

std::string intervals_text(const core::sched::DayPattern& pattern) {
    if (pattern.empty()) {
        return "Nonworking";
    }
    std::string text;
    for (std::size_t index = 0; index < pattern.count; ++index) {
        if (!text.empty()) {
            text += "\n";
        }
        text += clock_text(pattern.start_ticks(index));
        text += " to ";
        text += clock_text(pattern.end_ticks(index));
    }
    return text;
}

// A dated exception covers `day`: a plain range directly, a recurring one
// through core/cal's measured expansion (which refuses rather than guesses).
bool exception_covers(const core::cal::DatedException& exception, int day) {
    if (exception.recurrence_type >= 2 && exception.recurrence_type <= 7) {
        const core::cal::RecurrenceExpansion expanded =
            core::cal::expand_recurring_exception(exception);
        return expanded.applied &&
               std::binary_search(expanded.days.begin(), expanded.days.end(), day);
    }
    return day >= exception.start_day && day <= exception.end_day;
}

bool work_week_covers(const core::cal::DatedWorkWeek& week, int day) {
    return day >= week.start_day && day <= week.end_day;
}

}  // namespace

CalendarNames read_calendar_names(const std::string& mpp_path) {    CalendarNames out;
    try {
        const core::cal::CalendarTable table = core::cal::decode_file(mpp_path);
        out.decoded = true;
        for (const core::cal::CalendarDefinition& calendar : table.calendars) {
            if (calendar.base_uid > 0 || calendar.name.empty()) {
                continue;  // a derived/resource calendar is not in the picker
            }
            out.names.push_back(calendar.name);
        }
        out.default_name = table.default_calendar_name;
        // core/cal's own resolution rule for a missing default name: the one
        // base calendar, when there is exactly one.
        if (out.default_name.empty() && out.names.size() == 1) {
            out.default_name = out.names.front();
        }
    } catch (const std::exception&) {
        out = CalendarNames{};
    }
    return out;
}

bool load_working_time(const std::string& mpp_path, std::string_view calendar_name,
                       int year, int month, int selected_day,
                       const core::sheet::SheetFormat& format, WorkingTimeModel& out,
                       std::string* error) {
    out = WorkingTimeModel{};
    if (error != nullptr) {
        error->clear();
    }
    core::cal::CalendarTable table;
    try {
        table = core::cal::decode_file(mpp_path);
    } catch (const std::exception& failure) {
        if (error != nullptr) {
            *error = std::string("the file's calendar table does not decode: ") +
                     failure.what();
        }
        return false;
    }

    for (const core::cal::CalendarDefinition& calendar : table.calendars) {
        if (calendar.base_uid <= 0 && !calendar.name.empty()) {
            out.calendars.push_back(calendar.name);
        }
    }
    std::string selected(calendar_name);
    if (selected.empty()) {
        selected = table.default_calendar_name;
    }
    const auto contains = [&out](const std::string& name) {
        return std::find(out.calendars.begin(), out.calendars.end(), name) !=
               out.calendars.end();
    };
    if (!contains(selected)) {
        selected = out.calendars.empty() ? std::string() : out.calendars.front();
    }
    const core::cal::CalendarDefinition* definition =
        selected.empty() ? nullptr : core::cal::find_calendar(table, selected);
    if (definition == nullptr) {
        if (error != nullptr) {
            *error = "the file has no readable base calendar";
        }
        return false;
    }
    out.selected_calendar = definition->name;
    out.selected_is_base = definition->base_uid <= 0;
    if (out.selected_is_base) {
        out.based_on_text = "Default work week on calendar " + definition->name;
    } else {
        const core::cal::CalendarDefinition* base =
            core::cal::find_calendar_by_uid(table, definition->base_uid);
        out.based_on_text = "Default work week on calendar " +
                            (base != nullptr ? base->name : definition->name);
    }

    const core::sched::Calendar calendar = core::cal::build_calendar(table, *definition);
    if (month < 1 || month > 12) {
        month = 1;
    }
    out.year = year;
    out.month = month;
    const int length = month_length(year, month);
    const int first = day_number(year, month, 1);
    out.first_weekday_monday0 = (core::sched::weekday_of_day(first) + 6) % 7;
    out.days.reserve(static_cast<std::size_t>(length));
    for (int day = 1; day <= length; ++day) {
        const int number = first + day - 1;
        WorkingTimeDay cell;
        cell.day = day;
        cell.working = calendar.is_working_day(number);
        for (const core::cal::DatedException& exception : definition->exceptions) {
            if (exception_covers(exception, number)) {
                cell.exception = true;
                break;
            }
        }
        for (const core::cal::DatedWorkWeek& week : definition->work_weeks) {
            if (work_week_covers(week, number)) {
                cell.nondefault_work_week = true;
                break;
            }
        }
        out.days.push_back(cell);
    }

    if (selected_day < 1 || selected_day > length) {
        selected_day = std::min(std::max(selected_day, 1), length);
    }
    const int selected_number = first + selected_day - 1;
    out.selected_day_heading = std::string("Working times for ") +
                               std::to_string(selected_day) + " " + month_name(month) + " " +
                               std::to_string(year);
    out.selected_day_hours = intervals_text(calendar.pattern_for_day(selected_number));

    for (std::size_t index = 0; index < definition->exceptions.size(); ++index) {
        const core::cal::DatedException& exception = definition->exceptions[index];
        WorkingTimeRow row;
        row.index = static_cast<int>(index) + 1;
        row.start_day = exception.start_day;
        row.finish_day = exception.end_day;
        row.start_text = date_text_from_day(exception.start_day, format);
        row.finish_text = date_text_from_day(exception.end_day, format);
        out.exceptions.push_back(std::move(row));
    }
    for (std::size_t index = 0; index < definition->work_weeks.size(); ++index) {
        const core::cal::DatedWorkWeek& week = definition->work_weeks[index];
        WorkingTimeRow row;
        row.index = static_cast<int>(index) + 1;
        row.start_day = week.start_day;
        row.finish_day = week.end_day;
        row.start_text = date_text_from_day(week.start_day, format);
        row.finish_text = date_text_from_day(week.end_day, format);
        out.work_weeks.push_back(std::move(row));
    }
    out.decoded = true;
    return true;
}

const std::vector<std::string>& working_time_weekday_header() {
    static const std::vector<std::string> kHeader = {"M", "T", "W", "Th", "F", "S", "S"};
    return kHeader;
}

std::vector<CustomFieldDefinition> custom_field_definitions() {
    struct Family {
        mpp::FieldKind kind;
        const char* name;
        unsigned count;
    };
    static const Family kFamilies[] = {
        {mpp::FieldKind::duration, "Duration", mpp::kDurationFieldCount},
        {mpp::FieldKind::cost, "Cost", mpp::kCostFieldCount},
        {mpp::FieldKind::date, "Date", mpp::kDateFieldCount},
        {mpp::FieldKind::flag, "Flag", mpp::kFlagFieldCount},
        {mpp::FieldKind::number, "Number", mpp::kNumberFieldCount},
        {mpp::FieldKind::text, "Text", mpp::kTextFieldCount},
    };
    std::vector<CustomFieldDefinition> fields;
    for (const Family& family : kFamilies) {
        for (unsigned index = 1; index <= family.count; ++index) {
            const std::uint32_t id = mpp::custom_field_id(family.kind, index);
            if (id == 0) {
                continue;
            }
            CustomFieldDefinition field;
            field.field_id = id;
            field.name = std::string(family.name) + std::to_string(index);
            field.kind_name = family.name;
            field.storage_measured = mpp::custom_field_storage_measured(family.kind, index);
            fields.push_back(std::move(field));
        }
    }
    return fields;
}

void attach_custom_field_formulas(std::vector<CustomFieldDefinition>& fields,
                                  const std::vector<std::uint8_t>& props_bytes) {
    if (props_bytes.empty()) {
        return;
    }
    const FormulaColumnSet set = read_file_formulas(props_bytes);
    for (CustomFieldDefinition& field : fields) {
        for (const FormulaColumn& column : set.columns) {
            if (column.field == field.field_id) {
                field.formula = column.formula;
                break;
            }
        }
    }
}

const char* move_project_prompt_text() {
    return "This project contains tasks with actual values.\nDo you still want to move it?";
}

const std::vector<std::string>& wbs_menu_items() {
    static const std::vector<std::string> kItems = {"Define Code...", "Renumber..."};
    return kItems;
}

const std::vector<std::string>& baseline_menu_items() {
    static const std::vector<std::string> kItems = {"Set Baseline...", "Clear Baseline..."};
    return kItems;
}

const std::vector<std::string>& update_project_captions() {
    static const std::vector<std::string> kCaptions = {
        "Update work as complete through:",
        "0% - 100% complete",
        "100% complete",
        "Reschedule uncompleted work to start after:",
        "Entire project",
        "Selected tasks",
    };
    return kCaptions;
}

bool project_has_actual_values(const PlanInput& plan) noexcept {    if (plan.project_percent_complete != 0) {
        return true;
    }
    for (const TaskRow& row : plan.tasks) {
        if (row.percent_complete != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace app
