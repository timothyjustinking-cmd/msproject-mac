// app/edit_model.cpp -- see edit_model.h for the contract, the display plan and
// the undo/redo rules.
//
// No AppKit: this is the pure half of the pending edits (Insert/Delete Task,
// custom-field values, predecessor links) so the whole model and the writer
// sequence can be proved on Windows (app/app_edit_tests.cpp,
// app/app_edits_fields_tests.cpp, app/app_edits_links_tests.cpp).

#include "edit_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "actuals.h"
#include "add_delete.h"
#include "cal.h"
#include "cfb_writer.h"
#include "field_values.h"
#include "outline.h"
#include "project_props.h"
#include "reader.h"
#include "save.h"
#include "table.h"

namespace app {
namespace {

std::string trim_ascii(std::string text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
            text[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::filesystem::path normalised(const std::string& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return std::filesystem::path(path).lexically_normal();
    }
    return absolute.lexically_normal();
}

bool is_task_edit(StructuralKind kind) noexcept {
    return kind == StructuralKind::insert_task || kind == StructuralKind::delete_task;
}

bool is_field_edit(StructuralKind kind) noexcept {
    return kind == StructuralKind::set_field || kind == StructuralKind::clear_field;
}

bool is_link_edit(StructuralKind kind) noexcept {
    return kind == StructuralKind::add_link || kind == StructuralKind::delete_link;
}

// Wave 28 (W28-2.1): Indent/Outdent, the one pair that owns an outline shape.
bool is_outline_edit(StructuralKind kind) noexcept {
    return kind == StructuralKind::outline_indent ||
           kind == StructuralKind::outline_outdent;
}

const TaskRow* task_row_for_uid(const std::vector<TaskRow>& tasks,
                                std::uint32_t uid) {
    for (const TaskRow& row : tasks) {
        if (row.uid == uid) {
            return &row;
        }
    }
    return nullptr;
}

// The index of `uid` in the display order, or tasks.size().
std::size_t task_index_for_uid(const std::vector<TaskRow>& tasks,
                               std::uint32_t uid) {
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        if (tasks[index].uid == uid) {
            return index;
        }
    }
    return tasks.size();
}

// The display row the outline shape names: the previous row at the SAME
// outline level (indent's new summary) or at one level less (outdent's parent).
// `tasks` is in display (ID) order, so this is the row above in the sheet.
const TaskRow* outline_anchor_in_plan(const PlanInput& plan, std::uint32_t uid,
                                      bool indent) {
    const std::size_t moved = task_index_for_uid(plan.tasks, uid);
    if (moved == plan.tasks.size()) {
        return nullptr;
    }
    const int wanted = plan.tasks[moved].outline_level - (indent ? 0 : 1);
    for (std::size_t index = moved; index > 0; --index) {
        const TaskRow& candidate = plan.tasks[index - 1];
        if (candidate.outline_level == wanted) {
            return &candidate;
        }
    }
    return nullptr;
}

// W29-0.0 (the wave-28 review's R28-2/R28-4, High): the row shapes every
// committed Project save pair measured.  All of tests/data/wave28-percent-*.mpp,
// wave28-mark-*.mpp and wave28-update*.mpp are the flat synthetic-base project:
// top-level leaves (outline level 0) with no milestones.  Rule 1 (wave 29 brief
// section 1) says a command is enabled only for the row shapes a pair measured,
// so the edit model refuses the rest here rather than writing bytes Project has
// never been seen to write.
//
// A WORK-BEARING ASSIGNMENT is one that names a real resource and carries work.
// The fixture rows' assignments are Project's UNASSIGNED placeholders: they
// store the task's planned work but their +8 word is the 0xFFFF0001 null-
// resource sentinel (AssignmentRow::resource_uid is 0), and every committed
// pair is that shape -- Project's own save updates the placeholder's actual-work
// block, which W29-0.1 implements.  A resource-loaded assignment (R28-2(b)'s
// "% Work Complete / task actual work / assignment actual work") has no pair at
// all.  The wave-28 headers' "the placeholder assignments carry no work"
// described RESOURCE work; the stored +20 word is non-zero on every fixture row
// (2400/1440/960 minutes on the three tasks).
bool has_work_resource_assignment(const PlanInput& plan, std::uint32_t uid) {
    for (const AssignmentRow& assignment : plan.assignments) {
        if (assignment.task_uid == uid && assignment.resource_uid != 0 &&
            assignment.work_ticks > 0) {
            return true;
        }
    }
    return false;
}

// W29-0.2: the row shapes the wave-29 percent pairs measured, each ALONE on an
// automatic row -- a leaf under a summary (0 -> 40), a task with a work-bearing
// assignment (0 -> 40) and a zero-duration milestone (0 -> 100).  A combination
// of two (a milestone under a summary, a resource-loaded leaf, a milestone with
// resource work) has no pair, so check_actuals_row_shape refuses it.
struct ActualsRowShape {
    bool under_summary = false;
    bool milestone = false;
    bool work_resource = false;
};

ActualsRowShape actuals_row_shape(const PlanInput& plan, const TaskRow& row) {
    ActualsRowShape shape;
    shape.under_summary = row.outline_level != 0;
    shape.milestone = row.milestone;
    shape.work_resource = has_work_resource_assignment(plan, row.uid);
    return shape;
}

bool check_actuals_row_shape(const PlanInput& plan, const TaskRow& row,
                             bool percent_edit, std::string& error) {
    const ActualsRowShape shape = actuals_row_shape(plan, row);
    if (!percent_edit) {
        // W29-0.3 measures date updates; until its pairs land a date update
        // keeps every W29-0.0 refusal.
        if (shape.under_summary) {
            error = "task uid " + std::to_string(row.uid) +
                    " is a leaf under a summary; no Project save pair measures "
                    "an actuals write below a summary, and its stored roll-up "
                    "would be left stale";
            return false;
        }
        if (shape.milestone) {
            error = "task uid " + std::to_string(row.uid) +
                    " is a milestone; no Project save pair measures an actuals "
                    "write on a milestone";
            return false;
        }
        if (shape.work_resource) {
            error = "task uid " + std::to_string(row.uid) +
                    " has a resource assignment that carries work; no Project "
                    "save pair measures an actuals write on a resource-loaded "
                    "task (the measured rows carry only Project's unassigned "
                    "placeholders)";
            return false;
        }
        return true;
    }
    if (shape.under_summary && shape.milestone) {
        error = "task uid " + std::to_string(row.uid) +
                " is a milestone under a summary; no Project save pair measures "
                "that combination (the wave-29 milestone pair is top-level)";
        return false;
    }
    if (shape.under_summary && shape.work_resource) {
        error = "task uid " + std::to_string(row.uid) +
                " is a leaf under a summary AND has a resource assignment that "
                "carries work; no Project save pair measures that combination";
        return false;
    }
    if (shape.milestone && shape.work_resource) {
        error = "task uid " + std::to_string(row.uid) +
                " is a milestone with a resource assignment that carries work; "
                "no Project save pair measures that combination";
        return false;
    }
    return true;
}

// The row rules a percent edit and a date update share: a saved, live,
// non-summary task of a measured shape.  `row` receives the plan row on
// acceptance; `percent_edit` selects the wave-29 shape rules (W29-0.2).
bool check_actuals_target(const PlanInput& plan, std::uint32_t uid,
                          const TaskRow*& row, bool percent_edit,
                          std::string& error) {
    row = nullptr;
    if (uid == 0) {
        error = "uid 0 is the project summary row and has no task % complete to write";
        return false;
    }
    const TaskRow* found = task_row_for_uid(plan.tasks, uid);
    if (found == nullptr) {
        error = "task uid " + std::to_string(uid) +
                " is not a live task in the file (a pending insert has no saved row)";
        return false;
    }
    // W28-1.2: presence in a PlanInput is not evidence that the file has a
    // saved row.  StructuralEdits::apply() puts pending inserts into the plan
    // it returns (with file_row 0), so a shell that hands back the applied
    // plan would otherwise queue a % edit whose value the save silently drops
    // while reporting success (the wave-27 review's finding).
    if (found->file_row == 0) {
        error = "task uid " + std::to_string(uid) +
                " is not a saved task (a pending insert has no file row), so its "
                "% complete cannot be written";
        return false;
    }
    if (found->summary) {
        error = "task uid " + std::to_string(uid) +
                " is a summary task; Project computes its % complete from its children, "
                "so this port refuses to write a stored value for it";
        return false;
    }
    if (!check_actuals_row_shape(plan, *found, percent_edit, error)) {
        return false;
    }
    row = found;
    return true;
}

// W29-0.0 (R28-4): the row that becomes the new summary on an indent must be
// a 0 % row with no resource-loaded assignment work and no actuals edit queued
// in the same save.  The three committed outline pairs are 0 % rows whose only
// assignment is Project's unassigned placeholder (planned work, null resource);
// Project's own save tombstones that placeholder, which W29-0.1 implements.
// With progress -- or with a resource assignment whose work Project would have
// to roll into the summary -- step 5a would write that row's leaf actuals and
// the project roll-up before step 5b turns it into a summary, a shape no pair
// measured.
bool check_new_summary_shape(const PlanInput& plan, std::uint32_t uid,
                             const std::vector<std::uint32_t>& pending_actuals,
                             std::string& error) {
    const TaskRow* row = task_row_for_uid(plan.tasks, uid);
    if (row != nullptr && row->percent_complete > 0) {
        error = "task uid " + std::to_string(uid) + " is already " +
                std::to_string(row->percent_complete) +
                " % complete and would become the summary; no Project save pair "
                "measures an indent under a row with progress";
        return false;
    }
    if (has_work_resource_assignment(plan, uid)) {
        error = "task uid " + std::to_string(uid) +
                " has a resource assignment that carries work and would become "
                "the summary; no Project save pair measures an indent under a "
                "resource-loaded row";
        return false;
    }
    if (std::find(pending_actuals.begin(), pending_actuals.end(), uid) !=
        pending_actuals.end()) {
        error = "task uid " + std::to_string(uid) +
                " has a pending actuals edit and would become the summary; no "
                "Project save pair measures an indent under a row whose actuals "
                "are queued in the same save";
        return false;
    }
    return true;
}

// The percent transitions the committed Project pairs measured
// (tests/data/wave28-percent-*, wave29-percent-*): on a FLAT top-level leaf,
// automatic 0 -> 40, 40 -> 100 and 100 -> 0 and manual 0 -> 40; the wave-29
// shapes measure exactly one transition each -- a leaf under a summary 0 -> 40,
// a resource-loaded task 0 -> 40 and a milestone 0 -> 100, all automatic.
// Project stores different words per transition (the 40 -> 100 save keeps its
// step marker and drops only the final step; 100 -> 0 keeps the progress bits;
// the mid-to-mid pair makes Project reschedule the chain), so a target value
// alone is not a measured shape -- the pair's FROM value and the row shape
// matter too.
bool measured_percent_transition(const ActualsRowShape& shape,
                                 bool manually_scheduled, std::uint16_t from,
                                 std::uint16_t to) noexcept {
    if (!shape.under_summary && !shape.milestone && !shape.work_resource) {
        if (from == 0 && to == 40) {
            return true;
        }
        if (manually_scheduled) {
            return false;
        }
        return (from == 40 && to == 100) || (from == 100 && to == 0);
    }
    if (manually_scheduled) {
        return false; /* the wave-29 pairs are automatic rows */
    }
    if (shape.under_summary && !shape.milestone && !shape.work_resource) {
        return from == 0 && to == 40;
    }
    if (shape.work_resource && !shape.under_summary && !shape.milestone) {
        return from == 0 && to == 40;
    }
    if (shape.milestone && !shape.under_summary && !shape.work_resource) {
        return from == 0 && to == 100;
    }
    return false; /* a combination the shape check refuses */
}

// core::gantt::LinkType orders FS/SS/FF/SF; the FILE orders FF=0, FS=1, SF=2,
// SS=3 (add_delete.h).  The reader maps one way (reader_adapter.h); this is the
// writer direction, and it must not be a cast.
std::uint16_t writer_link_type(core::gantt::LinkType type) noexcept {
    switch (type) {
        case core::gantt::LinkType::finish_to_finish: return mppwrite::kLinkTypeFF;
        case core::gantt::LinkType::finish_to_start: return mppwrite::kLinkTypeFS;
        case core::gantt::LinkType::start_to_finish: return mppwrite::kLinkTypeSF;
        case core::gantt::LinkType::start_to_start: return mppwrite::kLinkTypeSS;
    }
    return mppwrite::kLinkTypeFS;
}

// The app/reader value family (mpp::FieldKind) in the writer's own enum
// (mppwrite::FieldKind, field_values.h).  The two are deliberately separate
// types: core/mppwrite does not link the reader.
mppwrite::FieldKind writer_field_kind(mpp::FieldKind kind) noexcept {
    switch (kind) {
        case mpp::FieldKind::text: return mppwrite::FieldKind::text;
        case mpp::FieldKind::number: return mppwrite::FieldKind::number;
        case mpp::FieldKind::duration: return mppwrite::FieldKind::duration;
        case mpp::FieldKind::date: return mppwrite::FieldKind::date;
        case mpp::FieldKind::flag: return mppwrite::FieldKind::flag;
        case mpp::FieldKind::cost: return mppwrite::FieldKind::cost;
        case mpp::FieldKind::unknown: return mppwrite::FieldKind::unknown;
    }
    return mppwrite::FieldKind::unknown;
}

// The value a set_field edit asks the writer to store, validated against the
// writer's own types (field_values.h): finite f64 number, valid packed date,
// duration ticks (0xffffffff is the writer's "not set" sentinel), NUL-free text.
bool field_value_is_writable(const StructuralEdit& edit, std::string& error) {
    const mpp::FieldKind kind = mpp::custom_field_kind(edit.field_id);
    if (kind == mpp::FieldKind::unknown || !is_measured_task_field(edit.field_id)) {
        error = "field id " + std::to_string(edit.field_id) +
                " has no measured storage/registration; only Text1, Number1, Number2, "
                "Date1, Duration1 and Flag1 can be written";
        return false;
    }
    if (edit.field_kind != mpp::FieldKind::unknown && edit.field_kind != kind) {
        error = "field kind does not match field id";
        return false;
    }
    switch (kind) {
        case mpp::FieldKind::text:
            if (edit.field_value.text.find('\0') != std::string::npos) {
                error = "text contains an embedded NUL";
                return false;
            }
            if (edit.field_value.text.size() > 65535) {
                error = "text is longer than 65535 bytes";
                return false;
            }
            return true;
        case mpp::FieldKind::number:
            if (!std::isfinite(edit.field_value.number)) {
                error = "number is not finite";
                return false;
            }
            return true;
        case mpp::FieldKind::date:
            if (!mppwrite::is_date(edit.field_value.packed_date)) {
                error = "date is not a valid packed date";
                return false;
            }
            return true;
        case mpp::FieldKind::duration:
            if (edit.field_value.duration_ticks == mppwrite::kInvalidDate) {
                error = "duration ticks 0xffffffff is the writer's not-set value";
                return false;
            }
            return true;
        case mpp::FieldKind::flag:
            return true;
        case mpp::FieldKind::cost:
        case mpp::FieldKind::unknown:
            break;
    }
    error = "field kind cannot be written";
    return false;
}

// The placeholder's displayed fields: a copy of the last live task's duration and
// dates, which is what `add_task_row` clones into the real row (its documented
// shape: "the row is a clone of the LAST LIVE task row").  Identity, name,
// outline and the constraint are the app's/writer's own.  A plan with no live
// task gives a row with no dates at all.
TaskRow placeholder_row(const StructuralEdit& edit, const TaskRow* template_row) {
    TaskRow row;
    row.uid = edit.uid;
    row.name = edit.name;
    row.outline_level = 0;
    // W28-3.1: the project's stored mode is what the row is scheduled with.
    row.manually_scheduled = edit.manually_scheduled;
    if (template_row != nullptr) {
        row.duration_ticks = template_row->duration_ticks;
        row.percent_complete = template_row->percent_complete;
        row.start = template_row->start;
        row.finish = template_row->finish;
    }
    return row;
}

// Applies one field edit to the display row: a set replaces (or appends) the
// stored value, a clear removes it so app::value_of_custom_field reads Null.
void apply_field_to_row(TaskRow& row, const StructuralEdit& edit) {
    auto found = std::find_if(row.custom_fields.begin(), row.custom_fields.end(),
                              [&edit](const mpp::CustomFieldValue& value) {
                                  return value.field_id == edit.field_id;
                              });
    if (edit.kind == StructuralKind::clear_field) {
        if (found != row.custom_fields.end()) {
            row.custom_fields.erase(found);
        }
        return;
    }
    mpp::CustomFieldValue value = edit.field_value;
    value.field_id = edit.field_id;
    value.kind = mpp::custom_field_kind(edit.field_id);
    value.present = true;
    value.storage_measured = true;
    if (found != row.custom_fields.end()) {
        *found = std::move(value);
    } else {
        row.custom_fields.push_back(std::move(value));
    }
}

// Wave 28 (W28-2.1): the outline the display plan shows.  Each edit records
// the summary row it touches in anchor_uid, so the replay is deterministic:
// the moved row's subtree shifts by the level difference and the summary row
// takes the queued duration (indent: the calendar span of the new children;
// outdent: the marker's restored duration) and summary flag.  The row's
// start/finish are NOT touched: the writer stores the roll-up in +96/+100 and
// leaves the effective +104/+108 pair alone, and that pair is what TaskRow and
// the sheet show (reader.h Task::start/finish), so the measured saves leave
// the displayed dates where the control had them.  Numbers are recomputed once
// at the end, because every row below the move keeps its ID but changes
// sibling path.
void apply_outline_to_tasks(std::vector<TaskRow>& tasks,
                            const std::vector<StructuralEdit>& applied) {
    bool changed = false;
    for (const StructuralEdit& edit : applied) {
        if (!is_outline_edit(edit.kind)) {
            continue;
        }
        const std::size_t moved = task_index_for_uid(tasks, edit.uid);
        const std::size_t parent = task_index_for_uid(tasks, edit.anchor_uid);
        if (moved == tasks.size() || parent == tasks.size() || moved == parent) {
            continue;  // a pending delete removed one end of the shape
        }
        changed = true;
        const bool indent = edit.kind == StructuralKind::outline_indent;
        const int old_level = tasks[moved].outline_level;
        const int new_level = tasks[parent].outline_level + (indent ? 1 : 0);
        const int delta = new_level - old_level;
        if (delta != 0) {
            // The moved row and every following row deeper than it.
            for (std::size_t index = moved; index < tasks.size(); ++index) {
                if (index != moved && tasks[index].outline_level <= old_level) {
                    break;
                }
                tasks[index].outline_level += delta;
            }
        }
        if (indent) {
            tasks[parent].summary = true;
            tasks[parent].duration_ticks =
                static_cast<std::int64_t>(edit.summary_duration);
        } else {
            tasks[parent].summary = false;
            tasks[parent].duration_ticks =
                static_cast<std::int64_t>(edit.restored_duration);
        }
    }
    if (!changed) {
        return;
    }
    // Project's OutlineNumber is the sibling path ("1", "1.1").  Rows appear
    // in outline order in the display plan, so one forward pass rebuilds it.
    std::vector<int> counters;
    for (TaskRow& row : tasks) {
        if (row.outline_level < 0) {
            continue;  // the synthesized project-summary row keeps its label
        }
        const std::size_t level = static_cast<std::size_t>(row.outline_level);
        if (counters.size() < level + 1) {
            counters.resize(level + 1, 0);
        }
        ++counters[level];
        counters.resize(level + 1);
        std::string number;
        for (std::size_t depth = 0; depth <= level; ++depth) {
            if (depth != 0) {
                number += '.';
            }
            number += std::to_string(counters[depth]);
        }
        row.outline_number = std::move(number);
    }
}

bool task_is_live(const std::vector<TaskRow>& tasks, std::uint32_t uid) {
    for (const TaskRow& row : tasks) {
        if (row.uid == uid) {
            return true;
        }
    }
    return false;
}

// W27-3.1: the non-throwing form of TaskTable::row_for_uid, for a save path
// that must refuse ONE edit rather than fail the whole write.  `row_for_uid`
// itself stays throwing: its other callers treat an absent uid as a bug.
bool task_table_row_for_uid(const mppwrite::TaskTable& table, std::uint32_t uid,
                            std::size_t& row) {
    for (std::size_t candidate = 0; candidate < table.rows(); ++candidate) {
        if (table.uid(candidate) == uid) {
            row = candidate;
            return true;
        }
    }
    return false;
}

// Project's finish for a MANUALLY scheduled row is not a plain AddWork: the
// working time between the start's CLOCK time and the end of the first day at
// or after it with a pattern is charged first.  MEASURED on every manual row of
// the tree -- start Sun 2026-01-04 13:00, 5 d -> Fri 12:00, 3 d -> Wed 12:00,
// 2 d -> Tue 12:00 -- and the manual 0 -> 40 % save pair proves the ACTUALS use
// the same rule: ActualStart Sun 13:00 with 16 h of actual work stops Tue
// 12:00, not the calendar-correct Tue 17:00 (docs/engine-parity.md "THE MANUAL
// FINISH IS NOT PLAIN AddWork"; core/plan/plan.cpp implements the identical
// manual_partial_day_charge/manual_finish_of, which the app build does not
// link).  For a start that is a working instant the charge is 0, so this is
// add_work.
core::sched::ticks manual_day_charge(const core::sched::Calendar& calendar,
                                     core::sched::date32 start) {
    if (core::sched::is_na(start) || calendar.is_working_instant(start)) {
        return 0;
    }
    const std::int32_t clock = core::sched::time_of(start);
    const std::int32_t day = core::sched::day_of(start);
    for (std::int32_t offset = 0; offset < core::sched::kDaysPerWeek; ++offset) {
        const core::sched::DayPattern& pattern =
            calendar.pattern_for_day(day + offset);
        if (pattern.empty()) {
            continue;
        }
        core::sched::ticks charge = 0;
        for (std::size_t i = 0; i < pattern.count; ++i) {
            const std::int32_t begin = pattern.start_ticks(i);
            const std::int32_t end = pattern.end_ticks(i);
            if (end <= clock) {
                continue;
            }
            charge += end - std::max(begin, clock);
        }
        return charge;
    }
    return 0;
}

std::uint32_t manual_finish_date(const core::sched::Calendar& calendar,
                                 std::uint32_t start,
                                 std::uint32_t duration_ticks) {
    if (!mppwrite::is_date(start) || duration_ticks == 0) {
        return start;
    }
    const core::sched::ticks charge = manual_day_charge(calendar, start);
    if (charge >= duration_ticks) {
        // [I] A duration shorter than the start day's tail has no oracle row;
        // core/plan keeps the row at its start rather than guessing, and so
        // does this save path.
        return start;
    }
    return calendar.add_work(start, duration_ticks - charge);
}

}  // namespace

bool is_measured_task_field(std::uint32_t field_id) noexcept {
    return field_id == mpp::custom_field_id(mpp::FieldKind::text, 1) ||
           field_id == mpp::custom_field_id(mpp::FieldKind::number, 1) ||
           field_id == mpp::custom_field_id(mpp::FieldKind::number, 2) ||
           field_id == mpp::custom_field_id(mpp::FieldKind::date, 1) ||
           field_id == mpp::custom_field_id(mpp::FieldKind::duration, 1) ||
           field_id == mpp::custom_field_id(mpp::FieldKind::flag, 1);
}

const StructuralEdit* StructuralEdits::next_undo() const noexcept {
    return applied_.empty() ? nullptr : &applied_.back();
}

const StructuralEdit* StructuralEdits::next_redo() const noexcept {
    return undone_.empty() ? nullptr : &undone_.back();
}

bool make_percent_edit(const PlanInput& plan, std::uint32_t uid, std::uint16_t percent,
                       StructuralEdit& out, std::string& error) {
    out = StructuralEdit{};
    out.kind = StructuralKind::set_percent;
    out.uid = uid;
    out.percent = percent;
    if (percent > 100) {
        error = "percent complete " + std::to_string(percent) + " is above 100";
        return false;
    }
    const TaskRow* row = nullptr;
    if (!check_actuals_target(plan, uid, row, true, error)) {
        return false;
    }
    // W29-0.0/W29-0.2: only the transitions a committed Project pair measured
    // are enabled (rule 1), per row shape.  The current value is the display
    // plan's, so a queued edit is judged against the state the save will see.
    const ActualsRowShape shape = actuals_row_shape(plan, *row);
    if (!measured_percent_transition(shape, row->manually_scheduled,
                                     row->percent_complete, percent)) {
        error = "task uid " + std::to_string(uid) + " is stored at " +
                std::to_string(row->percent_complete) + " % and this edit sets " +
                std::to_string(percent) +
                " %; no Project save pair measures that transition on this row "
                "shape (measured: a flat automatic leaf 0->40, 40->100 and "
                "100->0; a flat manual leaf 0->40; a leaf under a summary 0->40; "
                "a resource-loaded task 0->40; a milestone 0->100)";
        return false;
    }
    error.clear();
    return true;
}

bool percent_through(const core::sched::Calendar& calendar, std::uint32_t start,
                     std::uint32_t duration_ticks, bool manually_scheduled,
                     std::uint32_t through, PercentThrough& out) {
    out = PercentThrough{};
    if (!mppwrite::is_date(start) || !mppwrite::is_date(through)) {
        return false;
    }
    if (duration_ticks == 0) {
        // A milestone has no working time to divide; measured pairs have none,
        // and reaching the start is the only definition that needs no guess.
        out.percent = through >= start ? 100 : 0;
        out.step_percent = static_cast<double>(out.percent);
        return true;
    }
    const std::uint32_t finish =
        manually_scheduled ? manual_finish_date(calendar, start, duration_ticks)
                           : calendar.add_work(start, duration_ticks);
    if (core::sched::is_overflow(static_cast<core::sched::date32>(finish)) ||
        !mppwrite::is_date(finish)) {
        return false;
    }
    if (through >= finish) {
        out.percent = 100;
        out.actual_ticks = duration_ticks;
        out.step_percent = 100.0;
        return true;
    }
    if (through <= start) {
        return true;  // 0 %, no actual time
    }
    std::uint64_t elapsed =
        static_cast<std::uint64_t>(calendar.work_time_between(
            calendar.next_start(start), through));
    if (manually_scheduled) {
        elapsed += static_cast<std::uint64_t>(manual_day_charge(calendar, start));
    }
    if (elapsed >= duration_ticks) {
        out.percent = 100;
        out.actual_ticks = duration_ticks;
        out.step_percent = 100.0;
        return true;
    }
    out.percent = static_cast<std::uint16_t>(elapsed * 100u / duration_ticks);
    out.actual_ticks = static_cast<std::uint32_t>(elapsed);
    out.step_percent =
        static_cast<double>(elapsed) * 100.0 / static_cast<double>(duration_ticks);
    if (out.percent == 0) {
        // The writer's 0 % shape has no actual time; a sub-1 % fraction is
        // not a shape any measured Project save stores.
        out.actual_ticks = 0;
        out.step_percent = 0.0;
    }
    return true;
}

// W29-0.3 (R28-3): the ONE progressed-row shape Project's own save pairs
// measure end to end -- an automatic, top-level leaf whose stored progress is
// Project's 60 % edit (a 0x0B40015E step block, and an ActualStart equal to the
// row's effective Start), updated through a date that computes a DIFFERENT
// mid-range percent.  Both measured directions pass: 60 -> 80 raises and
// 60 -> 40 lowers.  `why` names the first condition the row fails; every other
// progressed row keeps the W29-0.0 refusal (rule 1: lift only measured shapes).
bool measured_progress_update_shape(const mppwrite::TaskTable& table,
                                    const mppwrite::CfbDocument& document,
                                    const TaskRow& row,
                                    const PercentThrough& progress,
                                    std::string& why) {
    if (row.manually_scheduled) {
        why = "the row is manually scheduled and the measured row is automatic";
        return false;
    }
    if (row.percent_complete != 60) {
        why = "its stored percent is " + std::to_string(row.percent_complete) +
              " %, not the measured 60 %";
        return false;
    }
    if (progress.percent == 0 || progress.percent >= 100) {
        why = "the date computes " + std::to_string(progress.percent) +
              " %, and only a mid-range recomputation is measured";
        return false;
    }
    if (progress.percent == row.percent_complete) {
        why = "the date recomputes the percent the row already stores";
        return false;
    }
    std::size_t file_row = 0;
    try {
        file_row = table.row_for_uid(row.uid);
    } catch (const std::exception&) {
        why = "the saved task table has no row for the uid";
        return false;
    }
    if (table.u16(file_row, mppwrite::task_row::kPercentComplete) != 60) {
        why = "the saved row's % Complete is not the measured 60";
        return false;
    }
    const std::uint32_t actual_start =
        table.u32(file_row, mppwrite::task_row::kActualStart);
    if (!mppwrite::is_date(actual_start) ||
        actual_start != table.u32(file_row, mppwrite::task_row::kStart2)) {
        why = "its saved ActualStart is not the measured one (the row's "
              "effective start)";
        return false;
    }
    if (!mppwrite::task_has_progress_block(document, row.uid)) {
        why = "it has no saved 0x0B40015E progress block";
        return false;
    }
    return true;
}

bool make_update_actuals_edit(const PlanInput& plan, std::string_view source_path,
                              const std::vector<std::uint32_t>& uids,
                              date32 through_date, bool updates_status_date,
                              StructuralEdit& out, std::string& error) {
    out = StructuralEdit{};
    error.clear();
    if (uids.empty()) {
        error = "no task was selected for the update";
        return false;
    }
    if (packed_from_date32(through_date) == mppwrite::kInvalidDate) {
        error = "the update date is not a real date";
        return false;
    }
    std::set<std::uint32_t> seen;
    for (std::uint32_t uid : uids) {
        if (!seen.insert(uid).second) {
            error = "task uid " + std::to_string(uid) +
                    " appears twice in one update";
            return false;
        }
    }
    core::sched::Calendar calendar;
    try {
        calendar = core::cal::load_project_calendar(std::string(source_path));
    } catch (const std::exception& calendar_error) {
        error = std::string("the project calendar could not be read (") +
                calendar_error.what() + ")";
        return false;
    }
    // W29-0.0 (R28-3): every measured date-update pair starts from a 0 % row
    // with no actual dates and no progress block.  The commands recompute the
    // percent from the row's start and the date, so on a row that already has
    // progress an earlier date would lower or erase recorded actuals -- a shape
    // no pair measured.  The file is the authority for that state; the plan's
    // percent catches a pending edit the file does not have yet.
    mppwrite::CfbDocument document;
    try {
        document = mppwrite::CfbDocument::from_file(std::string(source_path));
    } catch (const std::exception& open_error) {
        error = std::string("the project file could not be read (") +
                open_error.what() + ")";
        return false;
    }
    mppwrite::TaskTable file_table;
    try {
        file_table = mppwrite::TaskTable::from_document(document);
    } catch (const std::exception& table_error) {
        error = std::string("the saved task table could not be read (") +
                table_error.what() + ")";
        return false;
    }
    const std::uint32_t through = packed_from_date32(through_date);
    StructuralEdit built;
    built.kind = StructuralKind::update_actuals;
    built.through_date = through_date;
    built.updates_status_date = updates_status_date;
    for (std::uint32_t uid : uids) {
        const TaskRow* row = nullptr;
        if (!check_actuals_target(plan, uid, row, false, error)) {
            return false;
        }
        if (row->duration_ticks < 0 ||
            row->duration_ticks > static_cast<std::int64_t>(0xFFFFFFFFu)) {
            error = "task uid " + std::to_string(uid) +
                    " has a duration the file format cannot express";
            return false;
        }
        PercentThrough progress;
        if (!percent_through(
                calendar, static_cast<std::uint32_t>(row->start),
                static_cast<std::uint32_t>(row->duration_ticks),
                row->manually_scheduled, through, progress)) {
            error = "task uid " + std::to_string(uid) +
                    " has no start date, or its finish overflows the calendar, "
                    "so its progress through the date cannot be measured";
            return false;
        }
        bool recorded_progress = false;
        try {
            recorded_progress = mppwrite::task_has_recorded_progress(document, uid);
        } catch (const std::exception& progress_error) {
            error = "task uid " + std::to_string(uid) +
                    ": the saved progress could not be read (" +
                    progress_error.what() + ")";
            return false;
        }
        if (row->percent_complete > 0) {
            // W29-0.3: Project DOES recompute a progressed row's percent from
            // the update date, lowering it when the date is before the recorded
            // progress (60 -> 40 measured).  The lift is confined to the exact
            // shape the five wave-29 pairs measure; anything else keeps the
            // W29-0.0 refusal.
            std::string why_not;
            bool measured_shape = false;
            try {
                measured_shape = measured_progress_update_shape(
                    file_table, document, *row, progress, why_not);
            } catch (const std::exception& shape_error) {
                error = "task uid " + std::to_string(uid) +
                        ": the saved progress could not be read (" +
                        shape_error.what() + ")";
                return false;
            }
            if (!measured_shape) {
                error = "task uid " + std::to_string(uid) + " is already " +
                        std::to_string(row->percent_complete) +
                        " % complete; the only measured date update on a row "
                        "with progress is the automatic top-level 60 % shape "
                        "(W29-0.3), and this row is not it (" + why_not + ")";
                return false;
            }
        } else if (recorded_progress) {
            error = "task uid " + std::to_string(uid) +
                    " already records progress (an actual date or a progress "
                    "block); no Project save pair measures a date update on such "
                    "a row, and this command would recompute the percent";
            return false;
        }
        ActualsTarget target;
        target.uid = uid;
        target.percent = progress.percent;
        target.actual_ticks = progress.actual_ticks;
        target.step_percent = progress.step_percent;
        built.actuals.push_back(target);
    }
    // W29-0.3: Update Project's "Entire project" scope is the only one whose
    // project row takes the earliest per-row Resume/Stop.  `uids` IS the scope,
    // so the flag is true exactly when it names every live, saved, non-summary
    // task of the plan -- the list the app's All scope builds.  A pending insert
    // (file_row 0) has no file row and a pending delete is already out of the
    // plan, so neither changes the comparison.
    built.update_entire_project = true;
    for (const TaskRow& candidate : plan.tasks) {
        if (candidate.uid == 0 || candidate.summary || candidate.file_row == 0) {
            continue;
        }
        if (seen.find(candidate.uid) == seen.end()) {
            built.update_entire_project = false;
            break;
        }
    }
    out = std::move(built);
    return true;
}

bool make_outline_edit(const PlanInput& plan, std::string_view source_path,
                       std::uint32_t uid, bool indent, StructuralEdit& out,
                       std::string& error,
                       const std::vector<std::uint32_t>& pending_actuals) {
    out = StructuralEdit{};
    error.clear();
    out.kind = indent ? StructuralKind::outline_indent
                      : StructuralKind::outline_outdent;
    out.uid = uid;
    if (uid == 0) {
        error = "uid 0 is the project summary row and has no outline to move";
        return false;
    }
    const TaskRow* row = task_row_for_uid(plan.tasks, uid);
    if (row == nullptr) {
        error = "task uid " + std::to_string(uid) +
                " is not a live task in the file (a pending insert has no saved row)";
        return false;
    }
    if (row->file_row == 0) {
        error = "task uid " + std::to_string(uid) +
                " is not a saved task (a pending insert has no file row), so its "
                "outline cannot be written";
        return false;
    }
    if (indent) {
        if (row->outline_level != 0) {
            error = "task uid " + std::to_string(uid) +
                    " is not a top-level task; the measured indent moves a top-level "
                    "row under its preceding sibling";
            return false;
        }
    } else {
        if (row->summary) {
            error = "task uid " + std::to_string(uid) +
                    " is a summary; the measured outdent moves a leaf";
            return false;
        }
        if (row->outline_level < 1) {
            error = "task uid " + std::to_string(uid) +
                    " is already at the top level and cannot be outdented";
            return false;
        }
    }

    // The FILE's stored outline is the authority for the parent link, the
    // sibling, the marker and the working-time dates: mppwrite's writer
    // derives the write from exactly these rows.
    mppwrite::CfbDocument document;
    try {
        document = mppwrite::CfbDocument::from_file(std::string(source_path));
    } catch (const std::exception& open_error) {
        error = std::string("the project file could not be read (") +
                open_error.what() + ")";
        return false;
    }
    std::vector<mppwrite::OutlineRow> rows;
    std::string rows_error;
    if (!mppwrite::read_outline_rows(document, "114/TBkndTask", rows, rows_error)) {
        error = "the saved task outline could not be read (" + rows_error + ")";
        return false;
    }
    std::size_t moved = rows.size();
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (rows[index].uid == uid) {
            moved = index;
            break;
        }
    }
    if (moved == rows.size() || rows[moved].deleted) {
        error = "task uid " + std::to_string(uid) +
                " has no live row in the saved task table";
        return false;
    }
    // 0-based display level vs the file's 1-based +172.  A pending outline edit
    // (or a pending insert above the row) shifts the display without changing
    // the file; queueing another shape from that display would write a parent
    // the user is not looking at.
    if (rows[moved].level != static_cast<std::uint16_t>(row->outline_level + 1)) {
        error = "task uid " + std::to_string(uid) +
                ": the sheet's outline level does not match the saved file (a "
                "pending outline edit or insert has not been saved yet); save or "
                "undo it first";
        return false;
    }
    // W29-0.0 (R28-4): every committed outline pair consists of MANUALLY
    // scheduled rows (Project's COM facts record manual:true on IDs 1-3 for all
    // three pairs; build/w28/W28-2.1-project-outline-edit.json).  An automatic
    // row stores different +164/+174 and duration words, and no pair measured
    // them, so the shape is refused until W29-0.4 measures one.  The file's
    // Fixed2Meta bit is the authority (the plan's flag is the same bit through
    // the reader).
    if (!mppwrite::task_is_manually_scheduled(document, rows[moved].row)) {
        error = "task uid " + std::to_string(uid) +
                " is automatically scheduled; no Project save pair measures an "
                "outline edit on an automatic row (all measured rows are manual)";
        return false;
    }
    core::sched::Calendar calendar;
    try {
        calendar = core::cal::load_project_calendar(std::string(source_path));
    } catch (const std::exception& calendar_error) {
        error = std::string("the project calendar could not be read (") +
                calendar_error.what() + ")";
        return false;
    }

    if (indent) {
        if (rows.empty() || rows[0].uid != 0) {
            error = "the saved task table has no project row, so the outline "
                    "cannot be read";
            return false;
        }
        const std::vector<std::size_t>& project_children = rows[0].child_rows;
        const auto position =
            std::find(project_children.begin(), project_children.end(), moved);
        if (position == project_children.end() || position == project_children.begin()) {
            error = "task uid " + std::to_string(uid) +
                    " has no preceding sibling in the saved file to become its "
                    "summary";
            return false;
        }
        const std::size_t parent_index = *(position - 1);
        const mppwrite::OutlineRow& summary = rows[parent_index];
        if (summary.deleted) {
            error = "task uid " + std::to_string(uid) +
                    ": the preceding sibling is a deleted row";
            return false;
        }
        // W29-0.0: every row the measured indent touches is manually scheduled,
        // and the new summary carries no progress, work or queued actuals.
        if (!mppwrite::task_is_manually_scheduled(document, summary.row)) {
            error = "task uid " + std::to_string(summary.uid) +
                    " would become the summary but is automatically scheduled; "
                    "no Project save pair measures an outline edit on an automatic "
                    "row (all measured rows are manual)";
            return false;
        }
        if (!check_new_summary_shape(plan, summary.uid, pending_actuals, error)) {
            return false;
        }
        if (!summary.child_rows.empty() || summary.stored_children ||
            (summary.fixed2_flags & mppwrite::kOutlineSummaryFlag) != 0) {
            error = "task uid " + std::to_string(uid) + ": the preceding sibling "
                    "(task uid " + std::to_string(summary.uid) +
                    ") is already a summary; indenting under a summary is not measured";
            return false;
        }
        // The display's preceding row at this level must be the file's
        // preceding sibling: the measured shape writes the row the sheet shows.
        const TaskRow* shown = outline_anchor_in_plan(plan, uid, true);
        if (shown == nullptr || shown->uid != summary.uid) {
            error = "task uid " + std::to_string(uid) +
                    ": the saved file's outline order differs from the sheet's row "
                    "order, so the measured indent shape does not apply";
            return false;
        }
        // The roll-up span: the moved subtree's earliest +104 and latest +108.
        // Depth-first through the file's child links, exactly the rows the
        // writer rolls up.
        std::uint32_t earliest = mppwrite::kInvalidDate;
        std::uint32_t latest = mppwrite::kInvalidDate;
        std::vector<std::size_t> stack(1, moved);
        std::vector<bool> seen(rows.size(), false);
        while (!stack.empty()) {
            const std::size_t current = stack.back();
            stack.pop_back();
            if (current >= rows.size() || seen[current]) {
                continue;
            }
            seen[current] = true;
            const mppwrite::OutlineRow& member = rows[current];
            if (!mppwrite::is_date(member.start) ||
                !mppwrite::is_date(member.finish)) {
                error = "task uid " + std::to_string(uid) +
                        ": the moved subtree has a row without a valid start or "
                        "finish, so the summary dates cannot be written";
                return false;
            }
            if (!mppwrite::is_date(earliest) || member.start < earliest) {
                earliest = member.start;
            }
            if (!mppwrite::is_date(latest) || member.finish > latest) {
                latest = member.finish;
            }
            for (const std::size_t child : member.child_rows) {
                stack.push_back(child);
            }
        }
        if (!mppwrite::is_date(earliest) || !mppwrite::is_date(latest) ||
            latest < earliest) {
            error = "task uid " + std::to_string(uid) +
                    ": the moved subtree has no valid date span";
            return false;
        }
        const core::sched::ticks span = calendar.work_time_between(
            static_cast<core::sched::date32>(earliest),
            static_cast<core::sched::date32>(latest));
        if (span < 0 ||
            static_cast<std::uint64_t>(span) > 0xFFFFFFFFull) {
            error = "task uid " + std::to_string(uid) +
                    ": the moved subtree's working-time span cannot be stored";
            return false;
        }
        out.summary_duration = static_cast<std::uint32_t>(span);
        out.anchor_uid = summary.uid;
        return true;
    }

    if (rows[moved].level < 2 || !rows[moved].child_rows.empty()) {
        error = "task uid " + std::to_string(uid) +
                " is not a leaf below the top level in the saved file";
        return false;
    }
    const std::size_t parent_index = rows[moved].parent_row;
    if (parent_index == static_cast<std::size_t>(-1) || parent_index == 0 ||
        parent_index >= rows.size()) {
        error = "task uid " + std::to_string(uid) +
                " has no parent summary in the saved outline";
        return false;
    }
    const mppwrite::OutlineRow& summary = rows[parent_index];
    // W29-0.0: the measured outdent's parent is manually scheduled too (the
    // header used to claim the opposite); an automatic parent has no pair.
    if (!mppwrite::task_is_manually_scheduled(document, summary.row)) {
        error = "task uid " + std::to_string(summary.uid) +
                " is the parent summary and is automatically scheduled; no "
                "Project save pair measures an outline edit on an automatic row "
                "(all measured rows are manual)";
        return false;
    }
    if (summary.child_rows.size() != 1 || summary.child_rows.front() != moved) {
        error = "task uid " + std::to_string(uid) +
                ": its parent has another child; outdenting from a parent that "
                "stays a summary is not measured";
        return false;
    }
    const TaskRow* shown = outline_anchor_in_plan(plan, uid, false);
    if (shown == nullptr || shown->uid != summary.uid) {
        error = "task uid " + std::to_string(uid) +
                ": the saved file's parent does not match the row above the task "
                "on the sheet; the measured outdent shape does not apply";
        return false;
    }
    const mppwrite::OutlineMarker marker =
        mppwrite::read_outline_marker(document, summary.uid);
    if (!marker.present) {
        error = "task uid " + std::to_string(uid) + ": the parent summary (task uid " +
                std::to_string(summary.uid) +
                ") has no 0x0B40007C duration marker, so its restored duration is "
                "unknown";
        return false;
    }
    if (!mppwrite::is_date(summary.start)) {
        error = "task uid " + std::to_string(uid) +
                ": the parent summary has no valid effective start (+104), so the "
                "restored start cannot be computed";
        return false;
    }
    const std::uint32_t restored_start =
        calendar.next_start(static_cast<core::sched::date32>(summary.start));
    const std::uint32_t restored_finish =
        calendar.add_work(static_cast<core::sched::date32>(restored_start),
                          static_cast<core::sched::ticks>(marker.duration_ticks));
    if (!mppwrite::is_date(restored_start) || !mppwrite::is_date(restored_finish) ||
        core::sched::is_overflow(static_cast<core::sched::date32>(restored_finish)) ||
        restored_finish < restored_start) {
        error = "task uid " + std::to_string(uid) +
                ": the restored schedule overflows the project calendar";
        return false;
    }
    out.restored_duration = marker.duration_ticks;
    out.restored_duration_format = marker.duration_format;
    out.restored_start = date32_from_packed(restored_start);
    out.restored_finish = date32_from_packed(restored_finish);
    out.anchor_uid = summary.uid;
    return true;
}

bool make_new_task_mode_edit(std::string_view source_path, bool manual,
                             StructuralEdit& out, std::string& error) {
    out = StructuralEdit{};
    const mpp::NewTaskMode mode =
        mpp::read_new_task_mode(std::string(source_path));
    if (!mode.known) {
        error = "this file does not store the new-task scheduling mode "
                "(114/Props record 0x024013c8), so the option cannot be saved";
        return false;
    }
    out.kind = StructuralKind::set_new_task_mode;
    out.new_tasks_manual = manual;
    return true;
}

bool StructuralEdits::push(StructuralEdit edit, const PlanInput* plan) {
    switch (edit.kind) {
        case StructuralKind::insert_task: {
            edit.name = trim_ascii(std::move(edit.name));
            if (edit.name.empty() || edit.uid == 0) {
                return false;
            }
            for (const StructuralEdit& existing : applied_) {
                if (existing.uid == edit.uid) {
                    return false;
                }
            }
            edit.dependants = 0;
            break;
        }
        case StructuralKind::delete_task: {
            if (edit.uid == 0) {
                return false;
            }
            for (const StructuralEdit& existing : applied_) {
                if (existing.kind == StructuralKind::delete_task &&
                    existing.uid == edit.uid) {
                    return false;
                }
            }
            // A field value or a link add for this task could never reach the
            // file: the delete runs after them in the save sequence and the
            // cascade tombstones the link again.  They are moved into the
            // delete, not destroyed, so undo can restore them (W27-3.2).
            edit.displaced = detach_edits_naming(edit.uid);
            break;
        }
        case StructuralKind::set_field:
        case StructuralKind::clear_field: {
            if (edit.uid == 0 || edit.field_id == 0) {
                return false;
            }
            if (!is_measured_task_field(edit.field_id)) {
                return false;
            }
            edit.field_kind = mpp::custom_field_kind(edit.field_id);
            if (edit.kind == StructuralKind::set_field) {
                std::string error;
                if (!field_value_is_writable(edit, error)) {
                    return false;
                }
            } else {
                edit.field_value = mpp::CustomFieldValue{};
            }
            edit.dependants = 0;
            break;
        }
        case StructuralKind::add_link: {
            if (edit.predecessor_uid == 0 || edit.uid == 0 ||
                edit.predecessor_uid == edit.uid) {
                return false;
            }
            const std::uint16_t raw = static_cast<std::uint16_t>(edit.link_type);
            if (raw > mppwrite::kLinkTypeSS) {
                return false;
            }
            if (edit.lag_ticks > mppwrite::kMaxLinkLagTicks ||
                edit.lag_ticks < -mppwrite::kMaxLinkLagTicks) {
                return false;
            }
            for (const StructuralEdit& existing : applied_) {
                if (existing.kind == StructuralKind::delete_task &&
                    (existing.uid == edit.uid ||
                     existing.uid == edit.predecessor_uid)) {
                    return false;
                }
                if (existing.kind == StructuralKind::add_link &&
                    existing.predecessor_uid == edit.predecessor_uid &&
                    existing.uid == edit.uid && existing.link_type == edit.link_type) {
                    return false;
                }
            }
            edit.link_uid = 0;
            edit.dependants = 0;
            break;
        }
        case StructuralKind::delete_link: {
            if (edit.link_uid == 0 || edit.uid == 0) {
                return false;
            }
            for (const StructuralEdit& existing : applied_) {
                if (existing.kind == StructuralKind::delete_link &&
                    existing.link_uid == edit.link_uid) {
                    return false;
                }
            }
            edit.dependants = 0;
            break;
        }
        case StructuralKind::set_percent: {
            // W27-0.2: the model, not the shell, owns the leaf/summary rule.
            if (plan == nullptr) {
                return false;
            }
            StructuralEdit checked;
            std::string error;
            if (!make_percent_edit(*plan, edit.uid, edit.percent, checked, error)) {
                return false;
            }
            edit = std::move(checked);
            for (const StructuralEdit& existing : applied_) {
                if (existing.kind == StructuralKind::delete_task &&
                    existing.uid == edit.uid) {
                    return false;  // the row will not exist when the save runs
                }
            }
            edit.dependants = 0;
            break;
        }
        case StructuralKind::update_actuals: {
            // W28-1.3: the same plan-aware leaf/saved-row/summary rules as a
            // percent edit, applied to every target, plus the tick/percent
            // consistency the writer relies on and a guard against a pending
            // delete of a target (the row will not be there when the save
            // runs).
            if (plan == nullptr || edit.actuals.empty() ||
                packed_from_date32(edit.through_date) == mppwrite::kInvalidDate) {
                return false;
            }
            std::set<std::uint32_t> seen;
            for (const ActualsTarget& target : edit.actuals) {
                // W29-0.0: the same row-shape rules as a percent edit, and the
                // progress rule a date update owes (the file-side check ran in
                // make_update_actuals_edit; this is the plan-side half, which
                // also sees a pending percent edit).
                const TaskRow* row = nullptr;
                std::string error;
                if (!check_actuals_target(*plan, target.uid, row, false, error)) {
                    return false;
                }
                if (row->percent_complete > 0) {
                    // W29-0.3: the plan-side half of the one measured
                    // progressed-row shape.  The file-side half (stored 60 %,
                    // ActualStart on the effective start, a 0x0B40015E block)
                    // ran when the edit was built; this keeps a hand-built
                    // queue from bypassing the percent/manual rules.
                    if (row->manually_scheduled || row->percent_complete != 60 ||
                        target.percent == 0 || target.percent >= 100 ||
                        target.percent == row->percent_complete) {
                        return false;
                    }
                }
                if (!seen.insert(target.uid).second) {
                    return false;
                }
                if (!std::isfinite(target.step_percent) ||
                    target.step_percent < 0.0) {
                    return false;
                }
                const std::uint64_t duration =
                    static_cast<std::uint64_t>(std::max<std::int64_t>(
                        0, row->duration_ticks));
                if (target.actual_ticks > duration) {
                    return false;
                }
                if (target.percent == 0 && target.actual_ticks != 0) {
                    return false;
                }
                if (target.percent > 0 && target.actual_ticks == 0 && duration != 0) {
                    return false;
                }
                if (target.percent == 100 && target.actual_ticks != duration) {
                    return false;
                }
                for (const StructuralEdit& existing : applied_) {
                    if (existing.kind == StructuralKind::delete_task &&
                        existing.uid == target.uid) {
                        return false;
                    }
                }
            }
            edit.dependants = 0;
            break;
        }
        case StructuralKind::set_project_priority: {
            if (edit.uid != 0 || edit.priority > 1000) {
                return false;
            }
            // Last one wins, like set_percent: the display plan and the save
            // both take the newest edit per target.
            edit.dependants = 0;
            break;
        }
        case StructuralKind::set_new_task_mode: {
            // W28-3.1: plan-free; it names no task.  The file-side refusal (a
            // file with no 0x024013c8 record) is make_new_task_mode_edit's.
            // Last one wins at save, like the priority above.
            edit.uid = 0;
            edit.dependants = 0;
            break;
        }
        case StructuralKind::outline_indent:
        case StructuralKind::outline_outdent: {
            // W28-2.1: the plan can prove the row and the row above it; the
            // file-side rules (marker words, sibling's leaf shape, the stored
            // child lists) belong to make_outline_edit and the writer, which
            // re-check them before writing.
            if (plan == nullptr || edit.uid == 0 || edit.anchor_uid == 0) {
                return false;
            }
            const TaskRow* row = task_row_for_uid(plan->tasks, edit.uid);
            if (row == nullptr || row->file_row == 0) {
                return false;
            }
            const TaskRow* anchor =
                outline_anchor_in_plan(*plan, edit.uid,
                                       edit.kind == StructuralKind::outline_indent);
            if (anchor == nullptr || anchor->uid != edit.anchor_uid ||
                anchor->file_row == 0) {
                return false;
            }
            if (edit.kind == StructuralKind::outline_indent) {
                if (row->outline_level != 0 || anchor->summary) {
                    return false;
                }
                if (!row->manually_scheduled || !anchor->manually_scheduled) {
                    return false;  // W29-0.0: the measured rows are manual
                }
                std::vector<std::uint32_t> pending_actuals;
                for (const StructuralEdit& existing : applied_) {
                    if (existing.kind == StructuralKind::set_percent) {
                        pending_actuals.push_back(existing.uid);
                    } else if (existing.kind == StructuralKind::update_actuals) {
                        for (const ActualsTarget& target : existing.actuals) {
                            pending_actuals.push_back(target.uid);
                        }
                    }
                }
                std::string shape_error;
                if (!check_new_summary_shape(*plan, anchor->uid, pending_actuals,
                                             shape_error)) {
                    return false;
                }
            } else {
                if (row->summary || row->outline_level < 1 || !anchor->summary) {
                    return false;
                }
                if (!row->manually_scheduled || !anchor->manually_scheduled) {
                    return false;  // W29-0.0: the measured rows are manual
                }
                if (edit.restored_start == kDateNa ||
                    edit.restored_finish == kDateNa ||
                    edit.restored_finish < edit.restored_start) {
                    return false;
                }
            }
            for (const StructuralEdit& existing : applied_) {
                if (existing.kind == StructuralKind::delete_task &&
                    (existing.uid == edit.uid ||
                     existing.uid == edit.anchor_uid)) {
                    return false;  // the row will not exist when the save runs
                }
            }
            edit.dependants = 0;
            break;
        }
    }
    applied_.push_back(std::move(edit));
    undone_.clear();
    return true;
}

std::vector<StructuralEdit> StructuralEdits::detach_edits_naming(std::uint32_t uid) {
    // W27-3.2: the set push(delete_task) has always dropped, collected in
    // their original order instead of erased.  The delete keeps the result in
    // StructuralEdit::displaced; undo() puts it back.
    std::vector<StructuralEdit> displaced;
    std::vector<StructuralEdit> kept;
    kept.reserve(applied_.size());
    for (StructuralEdit& existing : applied_) {
        bool names_uid = false;
        if (existing.kind == StructuralKind::set_field ||
            existing.kind == StructuralKind::clear_field ||
            existing.kind == StructuralKind::set_percent) {
            names_uid = existing.uid == uid;
        } else if (existing.kind == StructuralKind::update_actuals) {
            // W28-1.3: a delete of any target invalidates the whole date
            // update; keep the user's edit for undo rather than half-applying
            // it to a row the delete removes.
            for (const ActualsTarget& target : existing.actuals) {
                if (target.uid == uid) {
                    names_uid = true;
                    break;
                }
            }
        } else if (existing.kind == StructuralKind::add_link) {
            names_uid = existing.uid == uid || existing.predecessor_uid == uid;
        } else if (is_outline_edit(existing.kind)) {
            // W28-2.1: deleting either the moved row or the summary it names
            // leaves the writer no shape to apply, so the outline edit is
            // displaced like a percent/actuals edit (undo restores it).
            names_uid = existing.uid == uid || existing.anchor_uid == uid;
        }
        if (names_uid) {
            displaced.push_back(std::move(existing));
        } else {
            kept.push_back(std::move(existing));
        }
    }
    applied_ = std::move(kept);
    return displaced;
}

bool StructuralEdits::undo() {
    if (applied_.empty()) {
        return false;
    }
    StructuralEdit edit = std::move(applied_.back());
    applied_.pop_back();
    if (edit.kind == StructuralKind::delete_task) {
        // W27-3.2: the edits the delete displaced could never have reached the
        // file, but they were the user's work; undo brings them back.
        for (StructuralEdit& displaced : edit.displaced) {
            applied_.push_back(std::move(displaced));
        }
    }
    undone_.push_back(std::move(edit));
    return true;
}

bool StructuralEdits::redo() {
    if (undone_.empty()) {
        return false;
    }
    StructuralEdit edit = std::move(undone_.back());
    undone_.pop_back();
    if (edit.kind == StructuralKind::delete_task) {
        // Undo restored the displaced edits; redoing the delete removes them
        // again and keeps them for a later undo.
        edit.displaced = detach_edits_naming(edit.uid);
    }
    applied_.push_back(std::move(edit));
    return true;
}

bool StructuralEdits::is_pending_insert(std::uint32_t uid) const noexcept {
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::insert_task && edit.uid == uid) {
            return true;
        }
    }
    return false;
}

bool StructuralEdits::drop_insert(std::uint32_t uid) {
    for (std::size_t index = 0; index < applied_.size(); ++index) {
        if (applied_[index].kind != StructuralKind::insert_task || applied_[index].uid != uid) {
            continue;
        }
        applied_.erase(applied_.begin() + static_cast<std::ptrdiff_t>(index));
        undone_.clear();
        return true;
    }
    return false;
}

std::size_t StructuralEdits::inserted_count() const noexcept {
    std::size_t count = 0;
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::insert_task) {
            ++count;
        }
    }
    return count;
}

std::size_t StructuralEdits::deleted_count() const noexcept {
    std::size_t count = 0;
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::delete_task) {
            ++count;
        }
    }
    return count;
}

std::size_t StructuralEdits::field_edit_count() const noexcept {
    std::size_t count = 0;
    for (const StructuralEdit& edit : applied_) {
        if (is_field_edit(edit.kind)) {
            ++count;
        }
    }
    return count;
}

std::size_t StructuralEdits::link_edit_count() const noexcept {
    std::size_t count = 0;
    for (const StructuralEdit& edit : applied_) {
        if (is_link_edit(edit.kind)) {
            ++count;
        }
    }
    return count;
}

std::uint32_t StructuralEdits::next_placeholder_uid(const PlanInput& plan) const noexcept {
    std::uint32_t largest = 0;
    for (const TaskRow& row : plan.tasks) {
        largest = std::max(largest, row.uid);
    }
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::insert_task) {
            largest = std::max(largest, edit.uid);
        }
    }
    if (largest == 0xFFFFFFFFu) {
        return 0;
    }
    return largest + 1u;
}

PlanInput StructuralEdits::apply(const PlanInput& loaded) const {
    PlanInput effective = loaded;
    std::set<std::uint32_t> deleted;
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::delete_task) {
            deleted.insert(edit.uid);
        }
    }
    std::set<std::uint32_t> removed_links;
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::delete_link) {
            removed_links.insert(edit.link_uid);
        }
    }

    // Links and assignments naming a deleted task go with it: the writer's delete
    // cascade tombstones exactly these dependent rows (w11-oracle section 2), so
    // the display plan must not keep a link or an assignment the saved file will
    // not have.  A pending link delete removes its file row by uid.
    std::vector<LinkRow> links;
    links.reserve(effective.links.size());
    std::size_t dropped_links = 0;
    for (const LinkRow& link : effective.links) {
        if (link.uid != 0 && removed_links.count(link.uid) != 0) {
            ++dropped_links;
            continue;
        }
        if (deleted.count(link.predecessor_uid) != 0 || deleted.count(link.successor_uid) != 0) {
            ++dropped_links;
            continue;
        }
        links.push_back(link);
    }
    effective.links = std::move(links);
    std::vector<AssignmentRow> assignments;
    assignments.reserve(effective.assignments.size());
    for (const AssignmentRow& assignment : effective.assignments) {
        if (deleted.count(assignment.task_uid) != 0) {
            continue;
        }
        assignments.push_back(assignment);
    }
    effective.assignments = std::move(assignments);
    effective.skipped_links = loaded.skipped_links + dropped_links;

    // The template the writer's add clones: the last live task in the FILE
    // (deleted rows excluded), found before the inserts are placed.  Wave 24
    // (W24-1/R7): "last in the file" is the largest FixedData row number, not
    // the last vector element -- the adapter sorts tasks into ID order, and on
    // a file stored out of ID order (PROJECT_A's first stored row is ID 13) the
    // vector's last task is not the row core/mppwrite clones.  `file_row` is
    // the reader's stored row number, captured before the sort.
    const TaskRow* template_row = nullptr;
    std::uint32_t template_file_row = 0;
    for (const TaskRow& row : loaded.tasks) {
        if (deleted.count(row.uid) != 0) {
            continue;
        }
        if (template_row == nullptr || row.file_row >= template_file_row) {
            template_row = &row;
            template_file_row = row.file_row;
        }
    }

    std::vector<TaskRow> tasks;
    tasks.reserve(loaded.tasks.size() + applied_.size());
    auto anchor_is_live = [&loaded, &deleted](std::uint32_t anchor) {
        if (anchor == 0) {
            return true;
        }
        for (const TaskRow& row : loaded.tasks) {
            if (row.uid == anchor && deleted.count(row.uid) == 0) {
                return true;
            }
        }
        return false;
    };
    auto append_inserts = [&](std::uint32_t anchor) {
        for (const StructuralEdit& edit : applied_) {
            if (edit.kind != StructuralKind::insert_task || edit.anchor_uid != anchor) {
                continue;
            }
            tasks.push_back(placeholder_row(edit, template_row));
        }
    };
    for (const TaskRow& row : loaded.tasks) {
        if (deleted.count(row.uid) != 0) {
            continue;
        }
        tasks.push_back(row);
        append_inserts(row.uid);
    }
    // anchor 0 is "at the end"; an insert anchored to a task this delete removed
    // lands at the end as well rather than vanishing.
    append_inserts(0);
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind != StructuralKind::insert_task || edit.anchor_uid == 0 ||
            anchor_is_live(edit.anchor_uid)) {
            continue;
        }
        tasks.push_back(placeholder_row(edit, template_row));
    }

    // Field values are applied to the displayed rows in edit order, so a later
    // edit for the same (task, field) wins (the save collapses the same way).
    for (const StructuralEdit& edit : applied_) {
        if (!is_field_edit(edit.kind)) {
            continue;
        }
        for (TaskRow& row : tasks) {
            if (row.uid == edit.uid) {
                apply_field_to_row(row, edit);
                break;
            }
        }
    }
    // Wave 26/28: the ribbon's %-complete edits and the Mark on Track /
    // Update Project date updates, applied in ONE chronological pass so a
    // later edit of either kind is the one the row shows (the save collapses
    // the same way).
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::set_percent) {
            for (TaskRow& row : tasks) {
                if (row.uid == edit.uid) {
                    row.percent_complete = edit.percent;
                    break;
                }
            }
            continue;
        }
        if (edit.kind != StructuralKind::update_actuals) {
            continue;
        }
        for (const ActualsTarget& target : edit.actuals) {
            for (TaskRow& row : tasks) {
                if (row.uid == target.uid) {
                    row.percent_complete = target.percent;
                    break;
                }
            }
        }
    }
    // Wave 28 (W28-2.1): the indented/outdented outline, in edit order, then
    // the OutlineNumber pass.  This is what makes the sheet's indentation and
    // collapse triangles and the Gantt's summary bars follow the move on the
    // next rebuild (W28-2.2); the save writes the same shape.
    apply_outline_to_tasks(tasks, applied_);
    // Wave 26 (W26-5.1): the project priority, last one wins.
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind == StructuralKind::set_project_priority) {
            effective.project_priority = edit.priority;
        }
    }
    effective.tasks = std::move(tasks);

    // Link adds last, only between rows the display still shows: a link whose
    // endpoint the plan does not have would be a bar-less link the Gantt drops.
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind != StructuralKind::add_link) {
            continue;
        }
        if (!task_is_live(effective.tasks, edit.predecessor_uid) ||
            !task_is_live(effective.tasks, edit.uid)) {
            continue;
        }
        LinkRow link;
        link.uid = 0;  // the file's uid is allocated on Save
        link.predecessor_uid = edit.predecessor_uid;
        link.successor_uid = edit.uid;
        link.type = edit.link_type;
        link.lag_ticks = edit.lag_ticks;
        effective.links.push_back(link);
    }
    return effective;
}

PlanInput StructuralEdits::without_structural_edits(const PlanInput& shown,
                                                    const PlanInput& loaded) const {
    PlanInput edited = loaded;
    auto shown_row = [&shown](std::uint32_t uid) -> const TaskRow* {
        for (const TaskRow& row : shown.tasks) {
            if (row.uid == uid) {
                return &row;
            }
        }
        return nullptr;
    };
    std::vector<TaskRow> tasks;
    tasks.reserve(loaded.tasks.size());
    for (const TaskRow& file_row : loaded.tasks) {
        // A pending delete has no displayed row, so the file's own values stay;
        // every other task takes what the sheet currently shows.
        const TaskRow* current = shown_row(file_row.uid);
        tasks.push_back(current != nullptr ? *current : file_row);
    }
    edited.tasks = std::move(tasks);
    // Wave 28 (W28-4.1): a set_percent or update_actuals edit owns the row's
    // percent_complete in the display plan (apply() writes it from the edit).
    // An UNDONE edit is not replayed by apply(), so without this the shown row
    // would keep the percent the undone edit gave it and the sheet -- and the
    // docked Task Form, which reads the same plan -- would keep showing a value
    // the file no longer has.  Restore the file's value for the uids an undone
    // edit touched, the same shape as the outline restore below.
    std::set<std::uint32_t> undone_percent_uids;
    for (const StructuralEdit& edit : undone_) {
        if (edit.kind == StructuralKind::set_percent) {
            undone_percent_uids.insert(edit.uid);
        } else if (edit.kind == StructuralKind::update_actuals) {
            for (const ActualsTarget& target : edit.actuals) {
                undone_percent_uids.insert(target.uid);
            }
        }
    }
    if (!undone_percent_uids.empty()) {
        for (TaskRow& row : edited.tasks) {
            if (undone_percent_uids.count(row.uid) == 0) {
                continue;
            }
            const TaskRow* file_row = task_row_for_uid(loaded.tasks, row.uid);
            if (file_row != nullptr) {
                row.percent_complete = file_row->percent_complete;
            }
        }
    }
    // Wave 28 (W28-2.1): an outline edit owns outline_level/outline_number/
    // summary, and a summary parent's duration, on the rows it touches.  A cell
    // edit cannot produce those values, and apply() replays them from the
    // file's shape, so carrying them from `shown` would shift the moved subtree
    // twice.  The anchor's duration is restored because the outline edit owns
    // +84; every other cell (including a pending Start edit, and start/finish,
    // which the outline writer never changes) keeps what the sheet shows.
    bool outline_pending = false;
    std::set<std::uint32_t> outline_summaries;
    const auto note_outline_edit = [&](const StructuralEdit& edit) {
        if (!is_outline_edit(edit.kind)) {
            return;
        }
        outline_pending = true;
        outline_summaries.insert(edit.anchor_uid);
    };
    for (const StructuralEdit& edit : applied_) {
        note_outline_edit(edit);
    }
    // W28-2.1: an UNDONE outline edit is not replayed by apply(), so the rows it
    // owned must go back to the file's shape too.  Without this, undoing an
    // indent would keep the sheet's summary row, shifted level and roll-up
    // duration: the shown row and the file row differ only in the fields the
    // edit owned, and no applied edit is left to reset them.
    for (const StructuralEdit& edit : undone_) {
        note_outline_edit(edit);
    }
    if (outline_pending) {
        for (TaskRow& row : edited.tasks) {
            const TaskRow* file_row = task_row_for_uid(loaded.tasks, row.uid);
            if (file_row == nullptr) {
                continue;  // a pending insert: apply() places it, no file row
            }
            row.outline_level = file_row->outline_level;
            row.outline_number = file_row->outline_number;
            row.summary = file_row->summary;
            if (outline_summaries.count(row.uid) != 0) {
                row.duration_ticks = file_row->duration_ticks;
            }
        }
    }
    // Cell edits never touch links or assignments; the display plan dropped the
    // deleted tasks' dependants, so restore the file's rows and let apply() drop
    // them again.  Field/link additions are re-derived from the edit list.
    edited.links = loaded.links;
    edited.assignments = loaded.assignments;
    edited.resources = loaded.resources;
    edited.skipped_links = loaded.skipped_links;
    return edited;
}

std::size_t count_delete_dependants(const PlanInput& plan, std::uint32_t uid) noexcept {
    std::size_t count = 0;
    for (const LinkRow& link : plan.links) {
        if (link.predecessor_uid == uid || link.successor_uid == uid) {
            ++count;
        }
    }
    for (const AssignmentRow& assignment : plan.assignments) {
        if (assignment.task_uid == uid) {
            ++count;
        }
    }
    return count;
}

StructuralSaveResult save_structural_edits(const StructuralEdits& edits,
                                           const std::vector<StartEdit>& start_edits,
                                           std::string_view source_path,
                                           std::string_view target_path,
                                           bool allow_source_overwrite,
                                           const StructuralDocumentEdits& document_edits) {
    StructuralSaveResult result;
    result.path = std::string(target_path);
    result.document_edits_requested = document_edits.any();
    if (edits.empty()) {
        result.error = "no pending edit to save";
        return result;
    }
    if (target_path.empty()) {
        result.error = "no target path";
        return result;
    }
    if (!allow_source_overwrite &&
        normalised(std::string(source_path)) == normalised(result.path)) {
        result.error =
            "refusing to overwrite the opened file; save to a new name (the opened .mpp is "
            "never modified)";
        return result;
    }

    // Wave 17: the document-edit seam.  A request with pending view-state/formula
    // edits but no hook could only end by dropping them silently, so it is
    // refused before anything is written.
    const bool has_hook = static_cast<bool>(document_edits.hook);
    if (document_edits.any() && !has_hook) {
        result.error =
            "pending table format / formula edits were passed in without a document-edit "
            "hook; refusing to drop them";
        return result;
    }

    // Only a real date is writable; a cleared start has nothing to write, exactly
    // as app::save_start_edits reports it.
    std::vector<StartEdit> writable;
    writable.reserve(start_edits.size());
    for (const StartEdit& edit : start_edits) {
        if (edit.start == kDateNa) {
            result.warnings.push_back("task uid " + std::to_string(edit.uid) +
                                      ": start cleared in the sheet -- no date to write");
        } else if (edit.uid == 0) {
            result.warnings.push_back("task uid 0 is the project row and is not written");
        } else {
            writable.push_back(edit);
        }
    }

    // Field edits: the last edit for a (task, field) wins, exactly as the display
    // plan applies them.  A set is stored through one save_field_values call; a
    // clear uses the measured empty-value shape, one clear_field_value call.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> last_field;
    for (std::size_t index = 0; index < edits.applied().size(); ++index) {
        const StructuralEdit& edit = edits.applied()[index];
        if (is_field_edit(edit.kind)) {
            last_field[std::make_pair(edit.uid, edit.field_id)] = index;
        }
    }
    std::vector<const StructuralEdit*> field_sets;
    std::vector<const StructuralEdit*> field_clears;
    for (const auto& entry : last_field) {
        const StructuralEdit& edit = edits.applied()[entry.second];
        if (edit.kind == StructuralKind::set_field) {
            field_sets.push_back(&edit);
        } else {
            field_clears.push_back(&edit);
        }
    }
    // Deterministic order: by the application order of the winning edit.
    auto by_index = [&last_field](const StructuralEdit* left, const StructuralEdit* right) {
        return last_field.at(std::make_pair(left->uid, left->field_id)) <
               last_field.at(std::make_pair(right->uid, right->field_id));
    };
    std::sort(field_sets.begin(), field_sets.end(), by_index);
    std::sort(field_clears.begin(), field_clears.end(), by_index);

    // Wave 26/28: the %-complete and date-update edits.  Last write per task
    // wins ACROSS both kinds, exactly as the display plan applies them (one
    // chronological pass); every winning target goes through one actuals
    // write.  `target` is SIZE_MAX for a set_percent edit, else the index in
    // the update edit's target list.
    struct WinningActual {
        std::size_t edit = 0;
        std::size_t target = static_cast<std::size_t>(-1);
    };
    std::map<std::uint32_t, WinningActual> last_actual;
    for (std::size_t index = 0; index < edits.applied().size(); ++index) {
        const StructuralEdit& edit = edits.applied()[index];
        if (edit.kind == StructuralKind::set_percent) {
            last_actual[edit.uid] = WinningActual{index, static_cast<std::size_t>(-1)};
        } else if (edit.kind == StructuralKind::update_actuals) {
            for (std::size_t target = 0; target < edit.actuals.size(); ++target) {
                last_actual[edit.actuals[target].uid] = WinningActual{index, target};
            }
        }
    }
    std::vector<WinningActual> actual_writes;
    // Update Project's project-row roll-up differs from Mark on Track's and from
    // Update Project's own "Selected tasks" scope: only the command that covers
    // the ENTIRE project makes the project row's Resume/Stop the earliest
    // per-row dates.  W29-0.3 measured both Update Project scopes on the same
    // base: the all-rows save takes 3bf31c20, the two-of-three selection keeps
    // the project start 3bf11e78 while still moving the status date.  So the
    // decision is the edit's scope, never updates_status_date.  Any applied
    // entire-project update selects it, even when a later percent edit wins one
    // of its rows (measured, see actuals.h).
    bool update_project_mode = false;
    for (const StructuralEdit& edit : edits.applied()) {
        if (edit.kind == StructuralKind::update_actuals &&
            edit.update_entire_project) {
            update_project_mode = true;
        }
    }
    for (const auto& entry : last_actual) {
        actual_writes.push_back(entry.second);
    }
    std::sort(actual_writes.begin(), actual_writes.end(),
              [](const WinningActual& left, const WinningActual& right) {
                  if (left.edit != right.edit) {
                      return left.edit < right.edit;
                  }
                  return left.target < right.target;
              });

    // Wave 26 (W26-5.1): the project priority, last edit wins.  It lives in the
    // same FixedData table as PercentComplete, so both ride ONE table write.
    const StructuralEdit* priority_edit = nullptr;
    for (const StructuralEdit& edit : edits.applied()) {
        if (edit.kind == StructuralKind::set_project_priority) {
            priority_edit = &edit;
        }
    }

    // Wave 28 (W28-3.1): the project's new-task mode, last edit wins.  It is
    // one Props record, written after the document hook and before the final
    // write (step 5d).
    const StructuralEdit* mode_edit = nullptr;
    for (const StructuralEdit& edit : edits.applied()) {
        if (edit.kind == StructuralKind::set_new_task_mode) {
            mode_edit = &edit;
        }
    }

    // Wave 28 (W28-2.1): the outline edits, all in one writer call, in
    // application order (each edit sees the table the previous one left).
    bool has_outline_edits = false;
    for (const StructuralEdit& edit : edits.applied()) {
        if (is_outline_edit(edit.kind)) {
            has_outline_edits = true;
            break;
        }
    }

    // Count the writer calls first: every one writes a file, and only the LAST one
    // may write the target (so an earlier refusal leaves the target untouched).
    std::size_t write_ops = 0;
    for (const StructuralEdit& edit : edits.applied()) {
        if (is_task_edit(edit.kind) || is_link_edit(edit.kind)) {
            ++write_ops;
        }
    }
    if (!field_sets.empty()) {
        ++write_ops;
    }
    write_ops += field_clears.size();
    if (!actual_writes.empty() || priority_edit != nullptr) {
        ++write_ops;  // one table write for every actuals/priority edit
    }
    if (has_outline_edits) {
        ++write_ops;  // one outline writer call, one file
    }
    if (mode_edit != nullptr) {
        ++write_ops;  // the Props record patch, one file
    }
    const bool finish_with_start_edits = !writable.empty();
    if (finish_with_start_edits) {
        ++write_ops;
    }
    // Wave 17: with a hook the final write must come AFTER it, so the hook's
    // streams are in the container that write emits.  When start pins ride along
    // the date writer already IS that final write; otherwise the cascade ends with
    // one explicit CfbDocument::save of the document the hook just changed.
    const bool finish_with_container_save = has_hook && !finish_with_start_edits;
    if (finish_with_container_save) {
        ++write_ops;
    }

    const std::string intermediate = result.path + ".w51-structural-tmp";
    std::error_code erase_error;
    auto cleanup = [&intermediate, &erase_error]() {
        std::filesystem::remove(intermediate, erase_error);
    };
    std::size_t op = 0;
    auto next_write_path = [&]() -> const std::string& {
        ++op;
        return op == write_ops ? result.path : intermediate;
    };

    try {
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(std::string(source_path));
        const std::vector<StructuralEdit>& applied = edits.applied();

        // 1. link deletes: the row is tombstoned before any link add, so a
        // delete-then-re-add of the same relation is a real re-add.
        for (const StructuralEdit& edit : applied) {
            if (edit.kind != StructuralKind::delete_link) {
                continue;
            }
            const mppwrite::DeletedLink deleted = mppwrite::delete_task_link(
                document, edit.link_uid, next_write_path());
            ++result.links_deleted;
            for (const std::string& warning : deleted.warnings) {
                result.warnings.push_back(warning);
            }
            result.notes.push_back(
                "deleted link uid " + std::to_string(deleted.uid) + " (" +
                std::to_string(deleted.predecessor_uid) + " -> " +
                std::to_string(deleted.successor_uid) + ", type " +
                std::to_string(deleted.type) + ", lag " +
                std::to_string(deleted.lag_ticks) + " ticks)");
        }

        // 2. link adds.
        for (const StructuralEdit& edit : applied) {
            if (edit.kind != StructuralKind::add_link) {
                continue;
            }
            const mppwrite::AddedLink added = mppwrite::add_task_link(
                document, edit.predecessor_uid, edit.uid,
                writer_link_type(edit.link_type), edit.lag_ticks,
                next_write_path());
            ++result.links_added;
            result.added_link_uids.push_back(added.uid);
            for (const std::string& warning : added.warnings) {
                result.warnings.push_back(warning);
            }
            result.notes.push_back("added link uid " + std::to_string(added.uid) + " (" +
                                   std::to_string(edit.predecessor_uid) + " -> " +
                                   std::to_string(edit.uid) + ")");
        }

        // 3. task deletes (the writer cascades into link/assignment rows).
        for (const StructuralEdit& edit : applied) {
            if (edit.kind != StructuralKind::delete_task) {
                continue;
            }
            const mppwrite::DeletedRow deleted =
                mppwrite::delete_task_row(document, edit.uid, next_write_path());
            ++result.deleted;
            result.links_tombstoned += deleted.link_rows_tombstoned;
            result.assignments_tombstoned += deleted.assignment_rows_tombstoned;
            for (const std::string& warning : deleted.warnings) {
                result.warnings.push_back(warning);
            }
            result.notes.push_back("deleted task uid " + std::to_string(edit.uid) +
                                   " and tombstoned " +
                                   std::to_string(deleted.link_rows_tombstoned) +
                                   " link(s), " +
                                   std::to_string(deleted.assignment_rows_tombstoned) +
                                   " assignment(s)");
        }

        // 4. task inserts (the writer allocates uid/id and appends).
        for (const StructuralEdit& edit : applied) {
            if (edit.kind != StructuralKind::insert_task) {
                continue;
            }
            mppwrite::NewTaskRow row;
            row.name = edit.name;
            row.uid = 0;  // the writer allocates; the app never patches a uid
            // W28-3.1: the row is scheduled with the project's stored mode as
            // it stood when Insert Task was pressed.  The Fixed2Meta bit's two
            // save shapes are the W45 measurement; the mode itself is measured:
            // Project 2021's own InsertTask put its new row (the new unique id)
            // at Manual=True under the manual option and Manual=False under
            // auto (build/w28/W28-3.1-project-new-task-mode.json, insert_probe;
            // Project inserts before the active row and renumbers, so the row
            // is identified by its new unique id, not by a new ID).
            row.manually_scheduled = edit.manually_scheduled;
            const mppwrite::AddedRow added =
                mppwrite::add_task_row(document, row, next_write_path());
            ++result.added;
            result.added_uids.push_back(added.uid);
            for (const std::string& warning : added.warnings) {
                result.warnings.push_back(warning);
            }
            result.notes.push_back("added task uid " + std::to_string(added.uid) +
                                   " (id " + std::to_string(added.id) + ")");
        }

        // 5. custom-field values.  All sets go through one save_field_values call
        // (that is the writer's multi-field shape, one file).
        if (!field_sets.empty()) {
            std::vector<mppwrite::FieldEdit> writer_edits;
            writer_edits.reserve(field_sets.size());
            for (const StructuralEdit* edit : field_sets) {
                mppwrite::FieldEdit writer_edit;
                writer_edit.uid = edit->uid;
                writer_edit.value.field_id = edit->field_id;
                writer_edit.value.kind = writer_field_kind(mpp::custom_field_kind(edit->field_id));
                writer_edit.value.text = edit->field_value.text;
                writer_edit.value.number = edit->field_value.number;
                writer_edit.value.packed_date = edit->field_value.packed_date;
                writer_edit.value.duration_ticks = edit->field_value.duration_ticks;
                writer_edit.value.flag = edit->field_value.flag;
                writer_edits.push_back(std::move(writer_edit));
            }
            const mppwrite::FieldWriteResult written = mppwrite::save_field_values(
                document, writer_edits, next_write_path());
            result.fields_written += written.fields_written;
            for (const std::string& warning : written.warnings) {
                result.warnings.push_back(warning);
            }
            result.notes.push_back(std::to_string(written.fields_written) +
                                   " custom-field value(s) written");
        }
        for (const StructuralEdit* edit : field_clears) {
            const mppwrite::ClearedField cleared = mppwrite::clear_field_value(
                document, edit->uid, edit->field_id, next_write_path());
            ++result.fields_written;
            for (const std::string& warning : cleared.warnings) {
                result.warnings.push_back(warning);
            }
            result.notes.push_back("cleared field 0x" + std::to_string(edit->field_id) +
                                   " of task uid " + std::to_string(edit->uid));
        }

        // 5a (wave 28).  PercentComplete, Mark on Track and Update Project are
        // one actuals write: Project's own save pairs
        // (tests/data/wave28-percent-*.mpp, wave28-mark-*.mpp,
        // wave28-update*.mpp) change ActualDuration, RemainingDuration, % Work
        // Complete, four date words, a progress flag, the start anchor, the
        // project-row roll-up and two Var2Data fields, so the single-word table
        // patch the wave-26 path used is gone.  The dates come from the file's
        // own calendar (core_cal, app-side because core_mppwrite is
        // standard-library only): ActualStart, then Stop = the working-time
        // walk over the elapsed ticks -- plain add_work for an automatic row,
        // Project's manual-start charge for a row whose Fixed2Meta manual bit
        // is set (see manual_finish_date above) -- and at 100 % the finish is
        // that stop; below 100 % Resume = next_start(Stop).  A percent edit
        // derives its elapsed ticks from Duration * percent / 100 and ORs the
        // progress bits; a date update carries the exact ticks and unrounded
        // step percent make_update_actuals_edit computed, leaves the progress
        // word alone (measured: +168 stays 0x0080 on both mark and update
        // pairs), and Update Project also stores its date as the project status
        // date.  All winning actuals and the project priority still ride one
        // written file.
        if (!actual_writes.empty() || priority_edit != nullptr) {
            mppwrite::TaskTable table = mppwrite::TaskTable::from_document(document);
            std::vector<mppwrite::PercentActualEdit> actual_edits;
            core::sched::Calendar calendar;
            bool calendar_ready = false;
            bool calendar_failed = false;
            for (const WinningActual& winner : actual_writes) {
                const StructuralEdit& source = edits.applied()[winner.edit];
                const bool from_update = winner.target != static_cast<std::size_t>(-1);
                const std::uint32_t uid =
                    from_update ? source.actuals[winner.target].uid : source.uid;
                const std::uint16_t percent =
                    from_update ? source.actuals[winner.target].percent : source.percent;
                // Name the command that asked for the write, so a refused row
                // is attributable in the message.
                const std::string what =
                    std::string(from_update ? "the date update for task uid "
                                            : "percent complete for task uid ") +
                    std::to_string(uid);
                // W27-3.1: an edit whose uid the source table does not carry
                // is a REFUSED edit, not a failed save.  push() already
                // refuses to queue one (make_percent_edit and
                // make_update_actuals_edit need a live, non-summary file
                // task), so this is the save layer's defence-in-depth for a
                // hand-built or stale queue: report the refusal and keep the
                // rest of the cascade, so this edit can never make every later
                // Save fail.
                std::size_t row = 0;
                if (!task_table_row_for_uid(table, uid, row)) {
                    result.warnings.push_back(
                        what +
                        " was refused: the source file has no task row with that UniqueID "
                        "(a pending insert has no saved row), so it was not written");
                    continue;
                }
                mppwrite::PercentActualEdit actual;
                actual.uid = uid;
                actual.percent = percent;
                if (from_update) {
                    actual.actual_ticks = source.actuals[winner.target].actual_ticks;
                    actual.step_percent = source.actuals[winner.target].step_percent;
                    actual.progress_flags = false;
                }
                if (percent > 0) {
                    if (!calendar_ready && !calendar_failed) {
                        try {
                            calendar = core::cal::load_project_calendar(
                                std::string(source_path));
                            calendar_ready = true;
                        } catch (const std::exception& error) {
                            calendar_failed = true;
                            result.warnings.push_back(
                                what + " was refused: the project calendar could "
                                       "not be read (" + error.what() + ")");
                        }
                    }
                    if (!calendar_ready) {
                        if (calendar_failed) {
                            result.warnings.push_back(
                                what + " was refused: the project calendar is unavailable");
                        }
                        continue;
                    }
                    // Working-time arithmetic needs the row's calendar.  Only
                    // the project calendar is measured (every wave-28 pair has
                    // -1 here), so a task calendar is refused rather than
                    // guessed.
                    if (table.u32(row, mppwrite::task_row::kCalendarUid) !=
                        0xFFFFFFFFu) {
                        result.warnings.push_back(
                            what + " was refused: the task uses its own calendar, "
                                   "which this writer does not measure");
                        continue;
                    }
                    // A manually scheduled row charges the start day's tail
                    // (the manual pair's Stop is Tue 12:00, not Tue 17:00);
                    // an automatic row is a plain working-time walk.  The
                    // manual bit also selects the ActualStart source below.
                    const bool manual =
                        mppwrite::task_is_manually_scheduled(document, row);
                    std::uint32_t actual_start =
                        table.u32(row, mppwrite::task_row::kActualStart);
                    if (!mppwrite::is_date(actual_start)) {
                        // W29-0.2: a manual row's measured start is Start2 (the
                        // manual pairs' Sunday 13:00, NOT their Monday Start);
                        // an automatic row's is Start.  The wave-29 milestone
                        // pair proves the second half: its Start2 still held
                        // the project start (3d4612c0) and Project wrote
                        // ActualStart = Start (3d4d27d8).
                        actual_start = table.u32(
                            row, manual ? mppwrite::task_row::kStart2
                                        : mppwrite::task_row::kStart);
                    }
                    if (!mppwrite::is_date(actual_start)) {
                        actual_start = table.u32(
                            row, manual ? mppwrite::task_row::kStart
                                        : mppwrite::task_row::kStart2);
                    }
                    if (!mppwrite::is_date(actual_start)) {
                        result.warnings.push_back(
                            what + " was refused: the task has no start date to "
                                   "count working time from");
                        continue;
                    }
                    std::uint32_t const duration =
                        table.u32(row, mppwrite::task_row::kDuration);
                    // A date update already knows its elapsed working time; a
                    // percent edit derives it, exactly as W28-1.2 measured.
                    std::uint32_t const actual_ticks =
                        from_update ? actual.actual_ticks
                                    : mppwrite::actual_duration_ticks(duration, percent);
                    std::uint32_t const stop =
                        manual ? manual_finish_date(calendar, actual_start,
                                                    actual_ticks)
                               : calendar.add_work(actual_start, actual_ticks);
                    actual.dates.actual_start = actual_start;
                    actual.dates.stop = stop;
                    if (percent == 100) {
                        // Measured on auto 40->100: ActualFinish = the finish
                        // reached by the whole duration, and Resume = Stop = it.
                        actual.dates.actual_finish = stop;
                        actual.dates.resume = stop;
                    } else {
                        actual.dates.resume = calendar.next_start(stop);
                    }
                }
                actual_edits.push_back(actual);
                if (from_update) {
                    result.notes.push_back(
                        "task uid " + std::to_string(uid) + ": " +
                        std::to_string(percent) + "% complete through " +
                        mppwrite::format_date(packed_from_date32(source.through_date)));
                } else {
                    result.notes.push_back("task uid " + std::to_string(uid) +
                                           ": percent complete " +
                                           std::to_string(percent));
                }
            }
            if (!actual_edits.empty()) {
                mppwrite::PercentActualsOptions options;
                // Update Project's project-row Resume/Stop are the earliest
                // valid per-row values; Mark on Track and the percent pairs keep
                // the project start (measured, see actuals.h).
                options.project_resume_stop_from_rows = update_project_mode;
                const mppwrite::PercentActualsResult written =
                    mppwrite::write_percent_actuals(document, actual_edits,
                                                    "114/TBkndTask", options);
                result.percent_rows_written += written.rows_written;
                for (const std::string& warning : written.warnings) {
                    result.warnings.push_back(warning);
                }
            }
            // Update Project stores its date as the project status date only
            // for the ENTIRE-PROJECT scope (measured: control 0xffffffff ->
            // 0x3bf40000 on the update pair; the two-of-three Selected scope
            // leaves 0xffffffff).  Mark on Track and the percent pairs leave
            // the record alone.  The last applied update that asked for it
            // wins, matching the display plan's order.
            const StructuralEdit* status_edit = nullptr;
            for (const StructuralEdit& edit : edits.applied()) {
                if (edit.kind == StructuralKind::update_actuals &&
                    edit.updates_status_date && edit.update_entire_project) {
                    status_edit = &edit;
                }
            }
            if (status_edit != nullptr) {
                const std::uint32_t packed =
                    packed_from_date32(status_edit->through_date);
                const std::uint32_t previous =
                    mppwrite::write_project_status_date(document, packed);
                result.notes.push_back("project status date " +
                                       mppwrite::format_date(previous) + " -> " +
                                       mppwrite::format_date(packed));
            }
            if (priority_edit != nullptr) {
                // Re-read the table: the actuals writer replaced FixedData.
                mppwrite::TaskTable priority_table =
                    mppwrite::TaskTable::from_document(document);
                // Row 0 is the project row (uid 0), matched like any other.
                const std::size_t row = priority_table.row_for_uid(0);
                priority_table.set_priority(row, priority_edit->priority);
                priority_table.store_into(document);
                result.priority_rows_written = 1;
                result.notes.push_back("project priority " +
                                       std::to_string(priority_edit->priority));
            }
            document.save(next_write_path());
        }

        // 5b (wave 28, W28-2.1).  Indent/Outdent: mppwrite::write_outline_edits
        // owns the outline level/parent/summary flag/roll-up and the Var2Data
        // marker and child lists of one or more edits, in application order.
        // It runs after the actuals write so a date update's changed child
        // dates are the ones the summary dates see, and before the document
        // hook.  A refused edit is a warning and not a failed save (the
        // W27-3.1 rule); the step still emits the file, because the op count
        // above reserved this write and only the last op may write the target.
        if (has_outline_edits) {
            std::vector<mppwrite::OutlineEdit> outline_edits;
            outline_edits.reserve(applied.size());
            for (const StructuralEdit& edit : applied) {
                if (!is_outline_edit(edit.kind)) {
                    continue;
                }
                mppwrite::OutlineEdit writer_edit;
                writer_edit.uid = edit.uid;
                writer_edit.indent =
                    edit.kind == StructuralKind::outline_indent;
                writer_edit.summary_duration = edit.summary_duration;
                writer_edit.restored_duration = edit.restored_duration;
                writer_edit.restored_duration_format =
                    edit.restored_duration_format;
                writer_edit.restored_start =
                    packed_from_date32(edit.restored_start);
                writer_edit.restored_finish =
                    packed_from_date32(edit.restored_finish);
                outline_edits.push_back(writer_edit);
            }
            const mppwrite::OutlineWriteResult written =
                mppwrite::write_outline_edits(document, outline_edits,
                                              "114/TBkndTask");
            result.outline_rows_written = written.rows_written;
            for (const std::string& warning : written.warnings) {
                result.warnings.push_back(warning);
            }
            result.notes.push_back(std::to_string(written.rows_written) +
                                   " outline edit(s) written");
            document.save(next_write_path());
        }

        // 5c (wave 17).  The document-edit seam: the `214/CTable` view state and
        // the `114/Props` formula, applied to THIS document -- after the
        // field-value writers (so the shared 0x02401392 flag decision sees a value
        // written moments ago) and before the one final write.  The hook does no
        // file I/O; the cascade owns the write below.
        if (has_hook) {
            const DocumentEditOutcome edits_outcome =
                document_edits.hook(document, document_edits.view, document_edits.formula);
            result.document_edits = edits_outcome;
            for (const std::string& warning : edits_outcome.warnings) {
                result.warnings.push_back(warning);
            }
            if (!edits_outcome.ok) {
                // A refused formula must never be dropped silently, and nothing has
                // been written to the target yet: fail the whole save, remove the
                // intermediate file, leave every real file as it was.
                cleanup();
                result.ok = false;
                result.error = edits_outcome.error.empty()
                                   ? "the document-edit hook refused the save"
                                   : edits_outcome.error;
                return result;
            }
            result.document_edits_written = true;
        }

        // 5d (wave 28, W28-3.1).  The project's stored new-task scheduling
        // mode: 114/Props record 0x024013c8, written with Project's measured
        // bytes (core/mppwrite/project_props.h).  The last queued choice wins,
        // as in the display.  Unlike the per-row refusals above, a missing or
        // wrongly shaped record FAILS the save: there is no measured record
        // shape to add, so silently skipping it would leave the option showing
        // a value the file does not carry.
        if (mode_edit != nullptr) {
            const std::uint16_t previous = mppwrite::write_new_task_mode(
                document, mode_edit->new_tasks_manual);
            result.new_task_mode_written = 1;
            result.notes.push_back(
                std::string("new-task mode ") +
                (previous != 0 ? "manual" : "auto") + " -> " +
                (mode_edit->new_tasks_manual ? "manual" : "auto"));
            document.save(next_write_path());
        }

        // 6. start dates, through the normal W37 path (one file, last).
        if (finish_with_start_edits) {
            std::vector<mppwrite::DateEdit> date_edits;
            date_edits.reserve(writable.size());
            for (const StartEdit& edit : writable) {
                mppwrite::DateEdit date_edit;
                date_edit.uid = edit.uid;
                date_edit.start = packed_from_date32(edit.start);
                date_edits.push_back(date_edit);
            }
            const mppwrite::SaveResult saved =
                mppwrite::save_date_edits(document, date_edits, next_write_path());
            result.start_rows_written = saved.rows_written;
            for (const std::string& note : saved.warnings) {
                result.notes.push_back(note);
            }
        } else if (finish_with_container_save) {
            // 6b (wave 17).  No start pin to write, but the hook changed the
            // document: emit it once, to the target, and prove the container from
            // disk exactly as the other save paths do.
            const std::vector<std::uint8_t> written = document.to_bytes();
            document.save(next_write_path());
            const mppwrite::CfbDocument reread =
                mppwrite::CfbDocument::from_file(result.path);
            if (reread.to_bytes() != written) {
                cleanup();
                result.ok = false;
                result.error = "the written file does not re-emit byte-identically";
                return result;
            }
            const std::string report = mppwrite::check_table(document);
            if (!report.empty()) {
                result.warnings.push_back("table check: " + report);
            }
        }
        cleanup();
        result.ok = true;
        result.api = "mppwrite";
        if (result.links_deleted != 0) {
            result.api += " delete_task_link";
        }
        if (result.links_added != 0) {
            result.api += " add_task_link";
        }
        if (result.deleted != 0) {
            result.api += " delete_task_row";
        }
        if (result.added != 0) {
            result.api += " add_task_row";
        }
        if (!field_sets.empty()) {
            result.api += " save_field_values";
        }
        if (!field_clears.empty()) {
            result.api += " clear_field_value";
        }
        if (has_outline_edits) {
            result.api += " write_outline_edits";
        }
        if (mode_edit != nullptr) {
            result.api += " write_new_task_mode";
        }
        if (finish_with_start_edits) {
            result.api += " save_date_edits";
        }
        if (result.document_edits_written) {
            result.api += " apply_document_edits";
        }
        if (finish_with_container_save) {
            result.api += " CfbDocument::save";
        }
    } catch (const std::exception& error) {
        cleanup();
        result.ok = false;
        result.error = error.what();
    }
    return result;
}

}  // namespace app
