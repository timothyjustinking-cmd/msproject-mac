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

#include <cstddef>
#include <utility>

#include "layout_model.h"
#include "reader.h"

namespace app {

inline core::gantt::LinkType gantt_link_type(mpp::LinkType type) noexcept {
    switch (type) {
        case mpp::LinkType::finish_finish: return core::gantt::LinkType::finish_to_finish;
        case mpp::LinkType::finish_start: return core::gantt::LinkType::finish_to_start;
        case mpp::LinkType::start_finish: return core::gantt::LinkType::start_to_finish;
        case mpp::LinkType::start_start: return core::gantt::LinkType::start_to_start;
    }
    return core::gantt::LinkType::finish_to_start;
}

inline TaskRow task_row_from_mpp(const mpp::Task& task) {
    TaskRow row;
    row.uid = task.uid;
    row.id = task.row_id;
    row.outline_number = task.outline_number;
    row.name = task.name;
    row.duration_ticks = static_cast<std::int64_t>(task.duration_ticks);
    row.percent_complete = task.percent_complete;
    row.start = date32_from_packed(task.start.raw);
    row.finish = date32_from_packed(task.finish.raw);
    row.outline_level = task.outline_level > 0 ? static_cast<int>(task.outline_level) - 1 : 0;
    row.summary = task.summary;
    row.milestone = task.is_milestone() && !task.summary;
    row.critical = task.critical;
    return row;
}

inline PlanInput plan_input_from_document(const mpp::Document& document) {
    PlanInput input;
    input.title = document.project.title;
    input.project_start = date32_from_packed(document.project.start.raw);
    input.project_finish = date32_from_packed(document.project.finish.raw);

    input.tasks.reserve(document.tasks.size());
    for (const mpp::Task& task : document.tasks) {
        input.tasks.push_back(task_row_from_mpp(task));
    }

    input.links.reserve(document.links.size());
    for (const mpp::Link& link : document.links) {
        if (!link.predecessor_known || !link.successor_known) {
            ++input.skipped_links;
            continue;
        }
        LinkRow row;
        row.predecessor_uid = link.predecessor_uid;
        row.successor_uid = link.successor_uid;
        row.type = gantt_link_type(link.type());
        // The reader exposes no per-link critical flag; the Gantt's optional
        // "critical hint" stays false rather than being guessed from slack.
        row.critical = false;
        input.links.push_back(std::move(row));
    }
    return input;
}

}  // namespace app
