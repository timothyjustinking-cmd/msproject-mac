// app/reader_adapter.h -- mpp::Document -> app::PlanInput, header only.
//
// This is the ONLY file in the app that includes core/mpp/reader.h.  Keeping it
// separate means app/layout_model.{h,cpp} stays a pure date32/RecordSet/Gantt
// mapping (testable without the reader) and the shell's load path is one call.
//
// The reader's derivations are taken as they are, not recomputed: summary is
// Task::summary (child_count > 0), milestone is Task::is_milestone() (duration
// == 0), critical is Task::critical (the reader's slack rule), outline_level is
// Project's 1-based OutlineLevel minus one (core::sheet wants 0-based), and
// OutlineNumber is the reader's sibling path.  See core/mpp/reader.h for the
// evidence behind each.
//
// Links whose endpoint is not one of the live tasks (PROJECT_A has links to a deleted
// task) are dropped and counted in PlanInput::skipped_links: the Gantt cannot
// route a link with no bar, and inventing one would be worse.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cfb.h"
#include "layout_model.h"
#include "reader.h"
#include "usage_mpp.h"

namespace app {

// Wave 12: the `114/Props` stream of an already-written .mpp, for the formula
// decoder (core/props).  Returns an empty vector when the file has no such
// stream or the container cannot be re-read: a file without a Props stream simply
// has no stored formulas, which is not an error.  The bytes are the raw stream,
// exactly as core/props::parse_props expects them.
inline std::vector<std::uint8_t> props_stream_from_file(const std::string& path) {
    try {
        const mpp::CompoundFile file = mpp::CompoundFile::from_file(path);
        if (file.has_stream("114/Props")) {
            return file.read_stream("114/Props");
        }
    } catch (const std::exception&) {
        // A container the CFB reader refuses is reported by load_file(), which
        // runs first; there is nothing to add here.
    }
    return {};
}

// Wave 11: the measured assignment -> usage bridge lives in core/usage
// (usage_mpp.h).  Re-exported here so main.mm still touches the reader in one
// place and app/view_model.cpp stays reader-free.
inline core::usage::UsageInput usage_input_from_document(const mpp::Document& document) {
    return core::usage::usage_input_from_mpp(document);
}

inline core::gantt::LinkType gantt_link_type(mpp::LinkType type) noexcept {
    switch (type) {
        case mpp::LinkType::finish_finish: return core::gantt::LinkType::finish_to_finish;
        case mpp::LinkType::finish_start: return core::gantt::LinkType::finish_to_start;
        case mpp::LinkType::start_finish: return core::gantt::LinkType::start_to_finish;
        case mpp::LinkType::start_start: return core::gantt::LinkType::start_to_start;
    }
    return core::gantt::LinkType::finish_to_start;
}

// Wave 11: the reader's resource type uses the same PjResourceTypes order, so
// this is a straight mapping (the reader reports Work for every corpus row
// today and marks that with Resource::type_stored == false).
inline core::view::ResourceType resource_type_from_mpp(mpp::ResourceType type) noexcept {
    switch (type) {
        case mpp::ResourceType::work: return core::view::ResourceType::work;
        case mpp::ResourceType::material: return core::view::ResourceType::material;
        case mpp::ResourceType::cost: return core::view::ResourceType::cost;
    }
    return core::view::ResourceType::work;
}

inline ResourceRow resource_row_from_mpp(const mpp::Resource& resource) {
    ResourceRow row;
    row.uid = resource.uid;
    row.id = resource.row_id;
    row.name = resource.name;
    row.initials = resource.initials;
    row.type = resource_type_from_mpp(resource.type);
    // Resource::max_units() is already the view convention (100.0 == 100 %,
    // from the stored x100 value); the builder wants the same quantity.
    row.max_units_percent = resource.max_units();
    return row;
}

inline AssignmentRow assignment_row_from_mpp(const mpp::Assignment& assignment) {
    AssignmentRow row;
    row.uid = assignment.uid;
    row.task_uid = assignment.task_uid;
    row.resource_uid = assignment.resource_uid();  // 0 when the row has no resource
    // W30-C5: keep the raw +8 word; 0 and 0xFFFF0001 both read as "no
    // resource" but only the sentinel is the measured placeholder.
    row.resource_uid_raw = assignment.resource_uid_raw;
    row.work_ticks = static_cast<std::int64_t>(assignment.work_ticks());
    row.units_percent = assignment.units();
    row.start = date32_from_packed(assignment.start.raw);
    row.finish = date32_from_packed(assignment.finish.raw);
    return row;
}

inline TaskRow task_row_from_mpp(const mpp::Task& task) {
    TaskRow row;
    row.uid = task.uid;
    row.id = task.row_id;
    row.file_row = task.row;
    row.outline_number = task.outline_number;
    row.name = task.name;
    row.duration_ticks = static_cast<std::int64_t>(task.duration_ticks);
    row.percent_complete = task.percent_complete;
    // Wave 31 (W31-1.1): the stored Priority word; the View tab's Sort dialog
    // sorts on it without any new decode.
    row.priority = task.priority;
    row.start = date32_from_packed(task.start.raw);
    row.finish = date32_from_packed(task.finish.raw);
    row.outline_level = task.outline_level > 0 ? static_cast<int>(task.outline_level) - 1 : 0;
    row.summary = task.summary;
    row.milestone = task.is_milestone() && !task.summary;
    row.critical = task.critical;
    row.flag10 = task.flag10;
    row.flag10_stored = task.flag10_stored;
    // Wave 14: the manually-scheduled bit, so the app can warn that save_date_edits
    // writes an auto-scheduled pin (docs/audit-handoff.md section 4.1/4.2).
    row.manually_scheduled = task.manually_scheduled;
    // W30-0.6 (R29-6): the task's own calendar (-1 = none, the project
    // calendar applies).  The resource-loaded percent shape is measured only on
    // the project calendar.
    row.calendar_uid = task.calendar_uid;
    // Wave 12: the stored custom fields (W48's measured decodings, in the
    // reader's order).  Absent values are absent here too; app::make_record_set
    // turns each into a RecordSet value.
    row.custom_fields = task.custom_fields;
    return row;
}

inline PlanInput plan_input_from_document(const mpp::Document& document) {
    PlanInput input;
    input.title = document.project.title;
    input.project_start = date32_from_packed(document.project.start.raw);
    input.project_finish = date32_from_packed(document.project.finish.raw);
    input.project_duration_ticks = static_cast<std::int64_t>(document.project.duration_ticks);
    input.project_percent_complete = document.project.percent_complete;
    // Wave 26 (W26-5.1): the project row's Priority.  The reader decodes it from
    // the same FixedData row a task uses (ProjectRow::priority); Project
    // Information shows it and the save path writes it back.
    input.project_priority = document.project.priority;
    // Wave 24 (W24-2/W24-3): the two view settings the file actually stores.
    // A file without the summary record means "hidden" (Project's new-project
    // default); a file without the Timeline record keeps the shown default.
    input.show_project_summary = document.project.summary_row_shown;
    if (document.project.timeline_stored) {
        input.show_timeline = document.project.timeline_shown;
    }
    input.project_flag10 = document.project.flag10;
    input.project_flag10_stored = document.project.flag10_stored;
    // Wave 28 (W28-1.3): the stored project status date.  A file without the
    // record reports not-stored; the value is then kDateNa (date32_from_packed
    // maps 0xffffffff to kDateNa), never a defaulted date.
    input.status_date = date32_from_packed(document.project.status_date.raw);
    input.status_date_stored = document.project.status_date_stored;

    input.tasks.reserve(document.tasks.size());
    // W30-C5 (R29-8): the shape checks count a task's PHYSICAL assignment rows
    // -- the ones the writers walk, skipped tombstones included -- while
    // PlanInput carries only the live rows.  Count both here, then attach the
    // count to the task row so the edit model can refuse a save the writers
    // would fail on before any edit is staged.
    std::unordered_map<std::uint32_t, std::size_t> physical_assignments;
    for (const mpp::Assignment& assignment : document.assignments) {
        ++physical_assignments[assignment.task_uid];
    }
    for (const mpp::SkippedRow& skipped : document.skipped_assignments) {
        if (skipped.task_uid != 0) {
            ++physical_assignments[skipped.task_uid];
        }
    }
    for (const mpp::Task& task : document.tasks) {
        TaskRow row = task_row_from_mpp(task);
        const auto physical = physical_assignments.find(task.uid);
        row.assignment_rows =
            physical == physical_assignments.end() ? 0 : physical->second;
        input.tasks.push_back(std::move(row));
    }
    // Wave 23 (B-1): Project displays tasks in ID order (the ID column is the
    // row order), while the file stores them in FixedData/creation order -- on
    // corpus/project_a.mpp the first stored row is ID 13.  The reader already
    // decodes the stored ID (+0) and the outline chain; sorting here makes the
    // RecordSet, the sheet, the Gantt and every row-wise gate consume Project's
    // order.  Equal IDs keep the reader's order (a file cannot normally have
    // two live rows with the same ID).
    std::stable_sort(input.tasks.begin(), input.tasks.end(),
                     [](const TaskRow& left, const TaskRow& right) {
                         return left.id < right.id;
                     });

    input.links.reserve(document.links.size());
    for (const mpp::Link& link : document.links) {
        if (!link.predecessor_known || !link.successor_known) {
            ++input.skipped_links;
            // W30-C5 (C3): this count names the links a FILE row still holds;
            // the edit model refuses shapes it cannot see through it.  A link
            // a pending edit drops is counted in skipped_links only.
            ++input.unreadable_links;
            continue;
        }
        LinkRow row;
        row.uid = link.uid;
        row.predecessor_uid = link.predecessor_uid;
        row.successor_uid = link.successor_uid;
        row.type = gantt_link_type(link.type());
        row.lag_ticks = link.lag_ticks;
        row.lag_units = link.lag_units;
        // The reader exposes no per-link critical flag; the Gantt's optional
        // "critical hint" stays false rather than being guessed from slack.
        row.critical = false;
        input.links.push_back(std::move(row));
    }

    // Wave 11: the resource and assignment rows the resource sheet and the usage
    // views consume.  A dead assignment endpoint is kept: core/usage totals it
    // but invents no row for an unknown task/resource uid (usage.h decision 5).
    input.resources.reserve(document.resources.size());
    for (const mpp::Resource& resource : document.resources) {
        input.resources.push_back(resource_row_from_mpp(resource));
    }
    input.assignments.reserve(document.assignments.size());
    for (const mpp::Assignment& assignment : document.assignments) {
        input.assignments.push_back(assignment_row_from_mpp(assignment));
    }
    return input;
}

}  // namespace app
