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
    };
    menus.push_back(std::move(edit));

    MenuSpec view;
    view.title = "View";
    view.items = {
        // Wave 11: the four views.  Cmd-Option-1..4 do not collide with the
        // Gantt timescale's Cmd-1..3 (different modifier mask), which is what
        // menu_tests' per-menu key-equivalent uniqueness check compares.
        item(MenuAction::view_task_sheet, "Task Sheet", "1", false, true),
        item(MenuAction::view_resource_sheet, "Resource Sheet", "2", false, true),
        item(MenuAction::view_task_usage, "Task Usage", "3", false, true),
        item(MenuAction::view_resource_usage, "Resource Usage", "4", false, true),
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
    };
    menus.push_back(std::move(view));

    MenuSpec insert;
    insert.title = "Insert";
    insert.items = {
        // Disabled, not faked: see menu_disabled_reason().
        item(MenuAction::insert_task, "Task..."),
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
        MenuAction::insert_resource,
    };
    return actions;
}

const std::vector<MenuSpec>& menu_specs() {
    static const std::vector<MenuSpec> menus = build_menu_specs();
    return menus;
}

const MenuItemSpec* find_menu_item(MenuAction action) {
    for (const MenuSpec& menu : menu_specs()) {
        for (const MenuItemSpec& spec : menu.items) {
            if (spec.action == action) {
                return &spec;
            }
        }
    }
    return nullptr;
}

bool menu_item_supported(MenuAction action) noexcept {
    switch (action) {
        case MenuAction::cut:
        case MenuAction::insert_task:
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
        case MenuAction::insert_task:
            return "the shell has no task-insert UI yet; wave 12 owns add/delete editing "
                   "(the writer API exists)";
        case MenuAction::insert_resource:
            return "adding a resource needs a resource writer; the resource sheet is read-only "
                   "(wave 11), insert/delete is wave 12";
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
            // Both reports are built from the task sheet and the Gantt, which do
            // not exist in the resource/usage views.
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
        case MenuAction::cut:
        case MenuAction::insert_task:
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
        case MenuAction::insert_resource: return "insert_resource";
    }
    return "none";
}

}  // namespace app
