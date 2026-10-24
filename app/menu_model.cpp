// app/menu_model.cpp -- see menu_model.h for why the menu bar is data.

#include "menu_model.h"

#include <utility>
#include <vector>

namespace app {
namespace {

MenuItemSpec item(MenuAction action, const char* title, const char* key = "",
                  bool shift = false, bool option = false, bool separator_before = false) {
    MenuItemSpec spec;
    spec.action = action;
    spec.title = title;
    spec.key = key;
    spec.command_key = true;
    spec.shift = shift;
    spec.option = option;
    spec.separator_before = separator_before;
    return spec;
}

// WAVE 26 (W26-6.1): a drop-down container.  It has no action or key of its
// own; the children are ordinary MenuItemSpecs and are what the builders
// create actions for.
// W28-2.1: Project binds Indent/Outdent to Alt+Shift+Right / Left -- no
// Command.  `item` always sets the Command modifier, so outline items take
// this variant.
MenuItemSpec item_no_command(MenuAction action, const char* title, const char* key,
                             bool shift, bool option) {
    MenuItemSpec spec = item(action, title, key, shift, option);
    spec.command_key = false;
    return spec;
}

MenuItemSpec submenu(const char* title, std::vector<MenuItemSpec> children,
                     bool separator_before = false) {
    MenuItemSpec spec;
    spec.title = title;
    spec.children = std::move(children);
    spec.command_key = false;
    spec.separator_before = separator_before;
    return spec;
}

std::vector<MenuSpec> build_menu_specs() {
    std::vector<MenuSpec> menus;

    MenuSpec application;
    application.title = "MS Project Shell";
    application.items = {
        item(MenuAction::preferences, "Preferences...", ","),
        item(MenuAction::hide_app, "Hide MS Project Shell", "h", false, false, true),
        item(MenuAction::quit_app, "Quit MS Project Shell", "q", false, false, true),
    };
    menus.push_back(std::move(application));

    MenuSpec file;
    file.title = "File";
    file.items = {
        item(MenuAction::open, "Open...", "o"),
        item(MenuAction::save, "Save", "s"),
        item(MenuAction::save_as, "Save As...", "s", true),
        // Wave 14: printing.  Print... opens the standard print panel; Save as
        // PDF... runs the SAME NSPrintOperation with a save job, so the two
        // cannot lay the page out differently.  The wave-10 Export PDF commands
        // stay (they go through core/render with the same PageSpec).
        item(MenuAction::print, "Print...", "p", false, false, true),
        item(MenuAction::save_as_pdf, "Save as PDF...", "p", true),
        item(MenuAction::export_pdf_sheet, "Export PDF (Sheet)...", "e", true, false, true),
        item(MenuAction::export_pdf_gantt, "Export PDF (Gantt)...", "e", false, true),
        item(MenuAction::close_window, "Close", "w", false, false, true),
    };
    menus.push_back(std::move(file));

    MenuSpec edit;
    edit.title = "Edit";
    edit.items = {
        item(MenuAction::undo, "Undo", "z"),
        item(MenuAction::redo, "Redo", "z", true),
        item(MenuAction::cut, "Cut", "x", false, false, true),
        item(MenuAction::copy, "Copy", "c"),
        item(MenuAction::paste, "Paste", "v"),
        item(MenuAction::select_all, "Select All", "a"),
        // Wave 12.  Cmd-Backspace is the Mac's "delete"; Delete Task is a
        // structural edit and asks for confirmation before it runs.
        item(MenuAction::delete_task, "Delete Task", "\b", false, false, true),
        // Wave 28 (W28-2.1): Project's indent/outdent keys.  A shape Project's
        // three save pairs did not measure is refused with a status message,
        // never queued, so the item is enabled with a selected saved task.
        item_no_command(MenuAction::indent, "Indent Task", "right", true, true),
        item_no_command(MenuAction::outdent, "Outdent Task", "left", true, true),
        // Wave 13: the two pending-edit dialogs.  They act on the selected task;
        // no key equivalents (they are forms, not blind commands).
        // Wave 23 (B-13): Task > Information opens the six-tab Task Information
        // dialog.  The old Task Fields dialog moves to its Project home,
        // Custom Fields... (operator Q10).
        item(MenuAction::task_fields, "Task Information...", "", false, false, true),
        item(MenuAction::custom_fields, "Custom Fields...", "", false, false, true),
        item(MenuAction::predecessors, "Predecessors..."),
        // Wave 16: set/clear a `114/Props` custom-field formula.  There is no
        // Task/Field menu in this shell; this and Task Fields are its two
        // custom-field commands, so it lives here.
        item(MenuAction::custom_field_formula, "Custom Field Formula...", "", false,
             false, true),
    };
    menus.push_back(std::move(edit));

    MenuSpec view;
    view.title = "View";
    view.items = {
        // WAVE 26 (W26-6.1 / W25-15 / operator Q17): Project 2021 puts the view
        // switches in two drop-downs, not as flat View-menu commands.  The
        // entries and their order are the clean View-strip readback
        // (docs/handoff/audit-w22/A2-fable-readback.md lines 35-36):
        //   Task Views:     Gantt Chart, Task Usage, Network Diagram, Calendar,
        //                   Other Views
        //   Resource Views: Team Planner, Resource Usage, Resource Sheet,
        //                   Other Views
        // The four views this port has keep the wave-11 Cmd-Option-1..4 keys;
        // the four it does not have are present, disabled and carry the
        // decision-9 tooltip (never faked, never dropped).
        submenu("Task Views",
                {
                    item(MenuAction::view_task_sheet, "Gantt Chart", "1", false, true),
                    item(MenuAction::view_task_usage, "Task Usage", "3", false, true),
                    item(MenuAction::none, "Network Diagram"),
                    item(MenuAction::none, "Calendar"),
                    item(MenuAction::none, "Other Views"),
                }),
        submenu("Resource Views",
                {
                    item(MenuAction::none, "Team Planner"),
                    item(MenuAction::view_resource_usage, "Resource Usage", "4", false,
                         true),
                    item(MenuAction::view_resource_sheet, "Resource Sheet", "2", false,
                         true),
                    item(MenuAction::none, "Other Views"),
                }),
        // The usage grids' period width.  No key equivalents: the period is a
        // display scale, not a command users reach for blind.
        item(MenuAction::usage_day, "Usage Period: Day", "", false, false, true),
        item(MenuAction::usage_week, "Usage Period: Week"),
        item(MenuAction::usage_month, "Usage Period: Month"),
        item(MenuAction::zoom_in, "Zoom In", "=", false, false, true),
        item(MenuAction::zoom_out, "Zoom Out", "-"),
        item(MenuAction::zoom_day, "Day Timescale", "1", false, false, true),
        item(MenuAction::zoom_week, "Week Timescale", "2"),
        item(MenuAction::zoom_month, "Month Timescale", "3"),
        item(MenuAction::toggle_shading, "Working-Day Shading", "d", true, false, true),
        item(MenuAction::show_filter, "Filter...", "f"),
        item(MenuAction::clear_filter, "Clear Filter", "f", true),
        item(MenuAction::collapse_all, "Collapse All", "", false, false, true),
        item(MenuAction::expand_all, "Expand All", ""),
        // Wave 16: set the active table's `214/CTable` date-format byte.  It is a
        // pending document edit Save writes with the start pins; a form, so no key
        // equivalent.
        item(MenuAction::table_date_format, "Table Date Format...", "", false, false,
             true),
    };
    menus.push_back(std::move(view));

    MenuSpec insert;
    insert.title = "Insert";
    insert.items = {
        // Wave 12: Task asks for a name and records a pending insert.  Resource
        // stays disabled, not faked: see menu_disabled_reason().
        item(MenuAction::insert_task, "Task...", "i"),
        item(MenuAction::insert_resource, "Resource..."),
    };
    menus.push_back(std::move(insert));

    MenuSpec help;
    help.title = "Help";
    help.items = {
        item(MenuAction::keyboard_shortcuts, "Keyboard Shortcuts"),
        item(MenuAction::about, "About MS Project Shell", "", false, false, true),
    };
    menus.push_back(std::move(help));

    return menus;
}

}  // namespace

const std::vector<MenuAction>& all_menu_actions() {
    static const std::vector<MenuAction> actions = {
        MenuAction::about,
        MenuAction::preferences,
        MenuAction::hide_app,
        MenuAction::quit_app,
        MenuAction::keyboard_shortcuts,
        MenuAction::open,
        MenuAction::save,
        MenuAction::save_as,
        MenuAction::export_pdf_sheet,
        MenuAction::export_pdf_gantt,
        MenuAction::close_window,
        MenuAction::undo,
        MenuAction::redo,
        MenuAction::cut,
        MenuAction::copy,
        MenuAction::paste,
        MenuAction::select_all,
        MenuAction::zoom_in,
        MenuAction::zoom_out,
        MenuAction::zoom_day,
        MenuAction::zoom_week,
        MenuAction::zoom_month,
        MenuAction::toggle_shading,
        MenuAction::show_filter,
        MenuAction::clear_filter,
        MenuAction::collapse_all,
        MenuAction::expand_all,
        MenuAction::view_task_sheet,
        MenuAction::view_resource_sheet,
        MenuAction::view_task_usage,
        MenuAction::view_resource_usage,
        MenuAction::usage_day,
        MenuAction::usage_week,
        MenuAction::usage_month,
        MenuAction::insert_task,
        MenuAction::delete_task,
        MenuAction::indent,
        MenuAction::outdent,
        MenuAction::task_fields,
        MenuAction::custom_fields,
        MenuAction::predecessors,
        MenuAction::table_date_format,
        MenuAction::custom_field_formula,
        MenuAction::insert_resource,
        MenuAction::print,
        MenuAction::save_as_pdf,
    };
    return actions;
}

const std::vector<MenuSpec>& menu_specs() {
    static const std::vector<MenuSpec> menus = build_menu_specs();
    return menus;
}

const MenuItemSpec* find_menu_item(MenuAction action) {
    // W26-6.1: the View drop-downs put commands one level down, so the lookup
    // walks into a submenu and reports the container itself for a drop-down
    // action (there is none today; the parents carry no action).
    for (const MenuSpec& menu : menu_specs()) {
        for (const MenuItemSpec& spec : menu.items) {
            if (spec.action == action) {
                return &spec;
            }
            for (const MenuItemSpec& child : spec.children) {
                if (child.action == action) {
                    return &child;
                }
            }
        }
    }
    return nullptr;
}

bool menu_item_supported(MenuAction action) noexcept {
    switch (action) {
        case MenuAction::cut:
        case MenuAction::insert_resource:
            return false;
        default:
            return action != MenuAction::none;
    }
}

const char* menu_disabled_reason(MenuAction action) noexcept {
    switch (action) {
        case MenuAction::cut:
            return "the sheet model has no cut operation; Copy and Paste are wired";
        case MenuAction::insert_resource:
            return "adding a resource needs a resource writer; the resource sheet is read-only "
                   "(wave 11), insert/delete is wave 12";
        // W26-6.1: Project's View drop-downs list views this port does not
        // build.  They are present and disabled with decision 9's exact text.
        case MenuAction::none:
            return "not implemented in this port";
        default:
            return "";
    }
}

bool menu_item_enabled(MenuAction action, const MenuState& state) noexcept {
    if (!menu_item_supported(action)) {
        return false;
    }
    switch (action) {
        case MenuAction::none:
            return false;
        case MenuAction::about:
        case MenuAction::preferences:
        case MenuAction::hide_app:
        case MenuAction::quit_app:
        case MenuAction::keyboard_shortcuts:
        case MenuAction::open:
            return true;
        case MenuAction::save:
        case MenuAction::save_as:
        case MenuAction::close_window:
            // Saving is a task-start operation; the view mode does not change
            // what is writable, so Save stays available in every view.
            return state.has_document;
        case MenuAction::export_pdf_sheet:
        case MenuAction::export_pdf_gantt:
        case MenuAction::print:
        case MenuAction::save_as_pdf:
            // All four reports are built from the task sheet and the Gantt,
            // which do not exist in the resource/usage views.
            return state.has_document && state.task_view;
        case MenuAction::undo:
            return state.task_view && state.can_undo;
        case MenuAction::redo:
            return state.task_view && state.can_redo;
        case MenuAction::copy:
            return state.task_view && state.has_document && state.can_copy && !state.editing;
        case MenuAction::paste:
            return state.task_view && state.has_document && state.can_paste && !state.editing;
        case MenuAction::select_all:
            return state.task_view && state.has_document && !state.editing;
        case MenuAction::zoom_in:
        case MenuAction::zoom_out:
        case MenuAction::zoom_day:
        case MenuAction::zoom_week:
        case MenuAction::zoom_month:
            return state.has_document && state.task_view;
        case MenuAction::toggle_shading:
            // Shading is a Gantt property; the toggle stays enabled everywhere so
            // a user can set it before switching back, and it is applied on draw.
            return state.has_document;
        case MenuAction::show_filter:
            return state.has_document && state.task_view;
        case MenuAction::clear_filter:
            return state.has_document && state.task_view && state.filter_active;
        case MenuAction::collapse_all:
            return state.has_document && state.task_view && state.any_expanded;
        case MenuAction::expand_all:
            return state.has_document && state.task_view && state.any_collapsed;
        case MenuAction::view_task_sheet:
        case MenuAction::view_resource_sheet:
        case MenuAction::view_task_usage:
        case MenuAction::view_resource_usage:
            return state.has_document;
        case MenuAction::usage_day:
        case MenuAction::usage_week:
        case MenuAction::usage_month:
            return state.has_document;
        case MenuAction::insert_task:
        case MenuAction::delete_task:
        case MenuAction::indent:
        case MenuAction::outdent:
        case MenuAction::task_fields:
        case MenuAction::custom_fields:
        case MenuAction::predecessors:
            // A structural edit is a Task Sheet command: it needs a document, a
            // row to work from and no open cell edit.  The three selection
            // commands also need a selected row; Insert Task can append at the
            // end without one.
            if (!state.has_document || !state.task_view || state.editing) {
                return false;
            }
            return action == MenuAction::insert_task ? true : state.has_selection;
        case MenuAction::table_date_format:
            // A table definition is a document property (`214/CTable`), not the
            // view on screen, so it stays enabled in the resource/usage views too.
            return state.has_document;
        case MenuAction::custom_field_formula:
            // `114/Props` is project-level and the field is chosen in the dialog, so
            // no selected row is needed; an open cell edit and a non-task view still
            // disable it (the command edits Task custom-field formulas).
            return state.has_document && state.task_view && !state.editing;
        case MenuAction::cut:
        case MenuAction::insert_resource:
            return false;
    }
    return false;
}

const char* menu_action_name(MenuAction action) noexcept {
    switch (action) {
        case MenuAction::none: return "none";
        case MenuAction::about: return "about";
        case MenuAction::preferences: return "preferences";
        case MenuAction::hide_app: return "hide_app";
        case MenuAction::quit_app: return "quit_app";
        case MenuAction::keyboard_shortcuts: return "keyboard_shortcuts";
        case MenuAction::open: return "open";
        case MenuAction::save: return "save";
        case MenuAction::save_as: return "save_as";
        case MenuAction::export_pdf_sheet: return "export_pdf_sheet";
        case MenuAction::export_pdf_gantt: return "export_pdf_gantt";
        case MenuAction::close_window: return "close_window";
        case MenuAction::undo: return "undo";
        case MenuAction::redo: return "redo";
        case MenuAction::cut: return "cut";
        case MenuAction::copy: return "copy";
        case MenuAction::paste: return "paste";
        case MenuAction::select_all: return "select_all";
        case MenuAction::zoom_in: return "zoom_in";
        case MenuAction::zoom_out: return "zoom_out";
        case MenuAction::zoom_day: return "zoom_day";
        case MenuAction::zoom_week: return "zoom_week";
        case MenuAction::zoom_month: return "zoom_month";
        case MenuAction::toggle_shading: return "toggle_shading";
        case MenuAction::show_filter: return "show_filter";
        case MenuAction::clear_filter: return "clear_filter";
        case MenuAction::collapse_all: return "collapse_all";
        case MenuAction::expand_all: return "expand_all";
        case MenuAction::view_task_sheet: return "view_task_sheet";
        case MenuAction::view_resource_sheet: return "view_resource_sheet";
        case MenuAction::view_task_usage: return "view_task_usage";
        case MenuAction::view_resource_usage: return "view_resource_usage";
        case MenuAction::usage_day: return "usage_day";
        case MenuAction::usage_week: return "usage_week";
        case MenuAction::usage_month: return "usage_month";
        case MenuAction::insert_task: return "insert_task";
        case MenuAction::delete_task: return "delete_task";
        case MenuAction::indent: return "indent";
        case MenuAction::outdent: return "outdent";
        case MenuAction::task_fields: return "task_fields";
        case MenuAction::custom_fields: return "custom_fields";
        case MenuAction::predecessors: return "predecessors";
        case MenuAction::table_date_format: return "table_date_format";
        case MenuAction::custom_field_formula: return "custom_field_formula";
        case MenuAction::insert_resource: return "insert_resource";
        case MenuAction::print: return "print";
        case MenuAction::save_as_pdf: return "save_as_pdf";
    }
    return "none";
}

}  // namespace app
