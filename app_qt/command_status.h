// app_qt/command_status.h -- which MenuActions this Qt shell actually handles.
//
// Operator decision 9 (docs/ui-parity-spec.md): unimplemented commands are
// DISABLED with the tooltip "not implemented in this port" and listed in the
// generated docs/ui-command-backlog.md.  This header is the single source of
// truth for "implemented": MainWindow::handle_action() covers these actions,
// build_menus()/update_menu_state()/the ribbon use shell_implements(), and
// app_qt/tools/command_backlog.py reads this file plus app::menu_specs() and the
// ribbon spec to generate the backlog.
//
// Keep this list in step with handle_action(): a name here that falls through
// to the "not wired" branch would be a lie.
#pragma once

#include "menu_model.h"

namespace msqt {

inline bool shell_implements(app::MenuAction action) noexcept {
    switch (action) {
        // Application / help.
        case app::MenuAction::about:
        case app::MenuAction::preferences:
        case app::MenuAction::keyboard_shortcuts:
        case app::MenuAction::quit_app:
        case app::MenuAction::close_window:
        // File.
        case app::MenuAction::open:
        case app::MenuAction::save:
        case app::MenuAction::save_as:
        case app::MenuAction::print:
        case app::MenuAction::save_as_pdf:
        case app::MenuAction::export_pdf_sheet:
        case app::MenuAction::export_pdf_gantt:
        // Edit.
        case app::MenuAction::undo:
        case app::MenuAction::redo:
        case app::MenuAction::cut:
        case app::MenuAction::copy:
        case app::MenuAction::paste:
        case app::MenuAction::select_all:
        case app::MenuAction::delete_task:
        case app::MenuAction::task_fields:
        case app::MenuAction::custom_fields:
        case app::MenuAction::predecessors:
        case app::MenuAction::custom_field_formula:
        // View / zoom / filter / views.
        case app::MenuAction::view_task_sheet:
        case app::MenuAction::view_resource_sheet:
        case app::MenuAction::view_task_usage:
        case app::MenuAction::view_resource_usage:
        case app::MenuAction::usage_day:
        case app::MenuAction::usage_week:
        case app::MenuAction::usage_month:
        case app::MenuAction::zoom_in:
        case app::MenuAction::zoom_out:
        case app::MenuAction::zoom_day:
        case app::MenuAction::zoom_week:
        case app::MenuAction::zoom_month:
        case app::MenuAction::toggle_shading:
        case app::MenuAction::show_filter:
        case app::MenuAction::clear_filter:
        case app::MenuAction::collapse_all:
        case app::MenuAction::expand_all:
        case app::MenuAction::table_date_format:
        // Insert.
        case app::MenuAction::insert_task:
            return true;
        // UNIMPLEMENTED -- keep this marker: the backlog generator reads only the
        // cases above it as "implemented".
        case app::MenuAction::insert_resource:  // no resource writer
        case app::MenuAction::hide_app:         // no portable hide command
        case app::MenuAction::none:
            return false;
    }
    return false;
}

// Why an unimplemented command is missing, for the generated backlog.  The
// generator maps these strings; two buckets are enough for the current cut.
inline const char* shell_missing_reason(app::MenuAction action) noexcept {
    switch (action) {
        case app::MenuAction::insert_resource:
            return "model: no resource writer; the resource sheet is read-only";
        case app::MenuAction::hide_app:
            return "chrome: Qt has no hide-application command on this platform";
        case app::MenuAction::none:
            return "future: the ribbon command is not modelled by app::MenuAction";
        default:
            return "";
    }
}

}  // namespace msqt
