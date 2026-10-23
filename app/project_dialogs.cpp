// app/project_dialogs.cpp -- see project_dialogs.h.

#include "project_dialogs.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "cal.h"

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

CalendarNames read_calendar_names(const std::string& mpp_path) {
    CalendarNames out;
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
