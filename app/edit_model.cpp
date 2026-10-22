// app/edit_model.cpp -- see edit_model.h for the contract, the display plan and
// the undo/redo rules.
//
// No AppKit: this is the pure half of "Insert Task" / "Delete Task" so the whole
// pending-edit model and the writer sequence can be proved on Windows
// (app/app_edit_tests.cpp).

#include "edit_model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "add_delete.h"
#include "cfb_writer.h"
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

}  // namespace

const StructuralEdit* StructuralEdits::next_undo() const noexcept {
    return applied_.empty() ? nullptr : &applied_.back();
}

const StructuralEdit* StructuralEdits::next_redo() const noexcept {
    return undone_.empty() ? nullptr : &undone_.back();
}

bool StructuralEdits::push(StructuralEdit edit) {
    if (edit.kind == StructuralKind::insert_task) {
        edit.name = trim_ascii(std::move(edit.name));
        if (edit.name.empty() || edit.uid == 0) {
            return false;
        }
        for (const StructuralEdit& existing : applied_) {
            if (existing.uid == edit.uid) {
                return false;
            }
        }
    } else {
        if (edit.uid == 0) {
            return false;
        }
        for (const StructuralEdit& existing : applied_) {
            if (existing.kind == StructuralKind::delete_task && existing.uid == edit.uid) {
                return false;
            }
        }
    }
    edit.dependants = edit.kind == StructuralKind::delete_task ? edit.dependants : 0;
    applied_.push_back(std::move(edit));
    undone_.clear();
    return true;
}

bool StructuralEdits::undo() {
    if (applied_.empty()) {
        return false;
    }
    undone_.push_back(applied_.back());
    applied_.pop_back();
    return true;
}

bool StructuralEdits::redo() {
    if (undone_.empty()) {
        return false;
    }
    applied_.push_back(undone_.back());
    undone_.pop_back();
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
    return applied_.size() - inserted_count();
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

    // Links and assignments naming a deleted task go with it: the writer's delete
    // cascade tombstones exactly these dependent rows (w11-oracle section 2), so
    // the display plan must not keep a link or an assignment the saved file will
    // not have.
    std::vector<LinkRow> links;
    links.reserve(effective.links.size());
    std::size_t dropped_links = 0;
    for (const LinkRow& link : effective.links) {
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
    // (deleted rows excluded), found before the inserts are placed.
    const TaskRow* template_row = nullptr;
    for (const TaskRow& row : loaded.tasks) {
        if (deleted.count(row.uid) == 0) {
            template_row = &row;
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
    effective.tasks = std::move(tasks);
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
    // them again.
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
                                           bool allow_source_overwrite) {
    StructuralSaveResult result;
    result.path = std::string(target_path);
    if (edits.empty()) {
        result.error = "no structural edit to save";
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

    const bool finish_with_start_edits = !writable.empty();
    const std::string intermediate = result.path + ".w51-structural-tmp";
    std::error_code erase_error;
    auto cleanup = [&intermediate, &erase_error]() {
        std::filesystem::remove(intermediate, erase_error);
    };

    try {
        mppwrite::CfbDocument document =
            mppwrite::CfbDocument::from_file(std::string(source_path));
        const std::vector<StructuralEdit>& applied = edits.applied();
        for (std::size_t index = 0; index < applied.size(); ++index) {
            const StructuralEdit& edit = applied[index];
            // The LAST structural write goes straight to the target (so its own
            // all-or-nothing check covers the file the user asked for); every
            // earlier one, and all of them when a start-date write follows, use
            // the intermediate path.
            const std::string& write_path =
                (finish_with_start_edits || index + 1 == applied.size()) ? result.path
                                                                        : intermediate;
            if (edit.kind == StructuralKind::delete_task) {
                const mppwrite::DeletedRow deleted =
                    mppwrite::delete_task_row(document, edit.uid, write_path);
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
            } else {
                mppwrite::NewTaskRow row;
                row.name = edit.name;
                row.uid = 0;  // the writer allocates; the app never patches a uid
                const mppwrite::AddedRow added =
                    mppwrite::add_task_row(document, row, write_path);
                ++result.added;
                result.added_uids.push_back(added.uid);
                for (const std::string& warning : added.warnings) {
                    result.warnings.push_back(warning);
                }
                result.notes.push_back("added task uid " + std::to_string(added.uid) +
                                       " (id " + std::to_string(added.id) + ")");
            }
        }
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
                mppwrite::save_date_edits(document, date_edits, result.path);
            result.start_rows_written = saved.rows_written;
            for (const std::string& note : saved.warnings) {
                result.notes.push_back(note);
            }
        }
        cleanup();
        result.ok = true;
        result.api = "mppwrite::delete_task_row/add_task_row";
        if (finish_with_start_edits) {
            result.api += " + mppwrite::save_date_edits";
        }
    } catch (const std::exception& error) {
        cleanup();
        result.ok = false;
        result.error = error.what();
    }
    return result;
}

}  // namespace app
