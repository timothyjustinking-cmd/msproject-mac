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

#include "add_delete.h"
#include "cfb_writer.h"
#include "field_values.h"
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
    if (uid == 0) {
        error = "uid 0 is the project summary row and has no task % complete to write";
        return false;
    }
    if (percent > 100) {
        error = "percent complete " + std::to_string(percent) + " is above 100";
        return false;
    }
    const TaskRow* row = nullptr;
    for (const TaskRow& task : plan.tasks) {
        if (task.uid == uid) {
            row = &task;
            break;
        }
    }
    if (row == nullptr) {
        error = "task uid " + std::to_string(uid) +
                " is not a live task in the file (a pending insert has no saved row)";
        return false;
    }
    if (row->summary) {
        error = "task uid " + std::to_string(uid) +
                " is a summary task; Project computes its % complete from its children, "
                "so this port refuses to write a stored value for it";
        return false;
    }
    error.clear();
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
        case StructuralKind::set_project_priority: {
            if (edit.uid != 0 || edit.priority > 1000) {
                return false;
            }
            // Last one wins, like set_percent: the display plan and the save
            // both take the newest edit per target.
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
        } else if (existing.kind == StructuralKind::add_link) {
            names_uid = existing.uid == uid || existing.predecessor_uid == uid;
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
    // Wave 26: the ribbon's %-complete edits, same last-one-wins rule.
    for (const StructuralEdit& edit : applied_) {
        if (edit.kind != StructuralKind::set_percent) {
            continue;
        }
        for (TaskRow& row : tasks) {
            if (row.uid == edit.uid) {
                row.percent_complete = edit.percent;
                break;
            }
        }
    }
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

    // Wave 26: the %-complete edits.  Last edit per task wins, exactly as the
    // display plan applies them; all rows go through one table write.
    std::map<std::uint32_t, std::size_t> last_percent;
    for (std::size_t index = 0; index < edits.applied().size(); ++index) {
        const StructuralEdit& edit = edits.applied()[index];
        if (edit.kind == StructuralKind::set_percent) {
            last_percent[edit.uid] = index;
        }
    }
    std::vector<const StructuralEdit*> percent_edits;
    for (const auto& entry : last_percent) {
        percent_edits.push_back(&edits.applied()[entry.second]);
    }
    std::sort(percent_edits.begin(), percent_edits.end(),
              [&last_percent](const StructuralEdit* left, const StructuralEdit* right) {
                  return last_percent.at(left->uid) < last_percent.at(right->uid);
              });

    // Wave 26 (W26-5.1): the project priority, last edit wins.  It lives in the
    // same FixedData table as PercentComplete, so both ride ONE table write.
    const StructuralEdit* priority_edit = nullptr;
    for (const StructuralEdit& edit : edits.applied()) {
        if (edit.kind == StructuralKind::set_project_priority) {
            priority_edit = &edit;
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
    if (!percent_edits.empty() || priority_edit != nullptr) {
        ++write_ops;  // one table write for every percent/priority edit
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

        // 5a (wave 26).  Project Priority has one stored copy (FixedData +78 of
        // row 0, measured in wave 27 W27-0.3), so its table write needs no
        // cross-stream fix-up.  PercentComplete is NOT single-copy -- Project's
        // own 50 % save (tests/data/wave27-percent-actuals.mpp) also writes the
        // actuals, the dates, a progress flag, the project-row roll-up and a
        // Var2Data field -- so this path stays for its test only; the UI is
        // disabled (W27-0.1).  All winning edits ride one write.
        if (!percent_edits.empty() || priority_edit != nullptr) {
            mppwrite::TaskTable table = mppwrite::TaskTable::from_document(document);
            for (const StructuralEdit* edit : percent_edits) {
                // W27-3.1: a percent edit whose uid the source table does not
                // carry is a REFUSED edit, not a failed save.  push() already
                // refuses to queue one (make_percent_edit needs a live,
                // non-summary file task), so this is the save layer's
                // defence-in-depth for a hand-built or stale queue: report the
                // refusal and keep the rest of the cascade, so this edit can
                // never make every later Save fail.
                std::size_t row = 0;
                if (!task_table_row_for_uid(table, edit->uid, row)) {
                    result.warnings.push_back(
                        "percent complete for task uid " + std::to_string(edit->uid) +
                        " was refused: the source file has no task row with that UniqueID "
                        "(a pending insert has no saved row), so it was not written");
                    continue;
                }
                table.set_percent_complete(row, edit->percent);
                ++result.percent_rows_written;
                result.notes.push_back("task uid " + std::to_string(edit->uid) +
                                       ": percent complete " +
                                       std::to_string(edit->percent));
            }
            if (priority_edit != nullptr) {
                // Row 0 is the project row (uid 0), matched like any other.
                const std::size_t row = table.row_for_uid(0);
                table.set_priority(row, priority_edit->priority);
                result.priority_rows_written = 1;
                result.notes.push_back("project priority " +
                                       std::to_string(priority_edit->priority));
            }
            table.store_into(document);
            document.save(next_write_path());
        }

        // 5b (wave 17).  The document-edit seam: the `214/CTable` view state and
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
