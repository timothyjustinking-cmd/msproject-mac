// app/menu_tests.cpp -- the menu bar as data, proved headlessly.
//
// The macOS shell builds its NSMenuItems from app::menu_specs() and asks
// app::menu_item_enabled() for every validation pass, so these checks are what
// stands in for a click test on the development box (main.mm is compiled only by
// the macOS CI job).
//
// What is pinned: the six menus and their order; every action present; no two
// items in one menu sharing a key equivalent; the documented key equivalents for
// Open/Save/Save As/Undo/Redo/Copy/Paste/Select All/Zoom/Filter; the wave-12
// Insert > Task (Cmd-I) and Edit > Delete Task (Cmd-Backspace) items and their
// enablement; and -- importantly -- that the two things the model cannot do
// (Insert > Resource, Cut) are PRESENT, permanently disabled, and carry a reason.

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "menu_model.h"

namespace {

using app::MenuAction;
using app::MenuItemSpec;
using app::MenuSpec;
using app::MenuState;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_text(const std::string& got, const std::string& want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL: %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
    }
}

MenuState everything_state() {
    MenuState state;
    state.has_document = true;
    state.editing = false;
    state.has_selection = true;
    state.can_undo = true;
    state.can_redo = true;
    state.can_copy = true;
    state.can_paste = true;
    state.dirty = true;
    state.filter_active = true;
    state.any_collapsed = true;
    state.any_expanded = true;
    return state;
}

MenuState empty_state() {
    return MenuState{};
}

int count_items(MenuAction action) {
    int count = 0;
    for (const MenuSpec& menu : app::menu_specs()) {
        for (const MenuItemSpec& item : menu.items) {
            if (item.action == action) {
                ++count;
            }
        }
    }
    return count;
}

const MenuSpec& menu_named(const std::string& title) {
    static const MenuSpec fallback;
    for (const MenuSpec& menu : app::menu_specs()) {
        if (title == menu.title) {
            return menu;
        }
    }
    return fallback;
}

void test_menus() {
    const std::vector<MenuSpec>& menus = app::menu_specs();
    check(menus.size() == 6, "there are exactly six menus");
    if (menus.size() != 6) {
        return;
    }
    check_text(menus[0].title, "MS Project Shell", "the first menu is the application menu");
    check_text(menus[1].title, "File", "the second menu is File");
    check_text(menus[2].title, "Edit", "the third menu is Edit");
    check_text(menus[3].title, "View", "the fourth menu is View");
    check_text(menus[4].title, "Insert", "the fifth menu is Insert");
    check_text(menus[5].title, "Help", "the sixth menu is Help");
    for (const MenuSpec& menu : menus) {
        check(!menu.items.empty(), "no menu is empty");
        for (const MenuItemSpec& item : menu.items) {
            check(item.action != MenuAction::none, "no item has a null action");
            check(item.title != nullptr && item.title[0] != '\0', "no item has an empty title");
        }
    }
}

void test_actions_present_once_per_menu() {
    for (const MenuAction action : app::all_menu_actions()) {
        check(action != MenuAction::none, "the action list does not contain none");
        check(count_items(action) >= 1, "every action is in the menu bar");
        check(app::find_menu_item(action) != nullptr, "find_menu_item finds it");
        check(app::menu_action_name(action)[0] != '\0', "every action has a name");
        check(std::string(app::menu_action_name(action)) != "none", "no action is named none");
    }
    // No action appears twice inside one menu (the shell uses one tag per item).
    for (const MenuSpec& menu : app::menu_specs()) {
        for (std::size_t left = 0; left < menu.items.size(); ++left) {
            for (std::size_t right = left + 1; right < menu.items.size(); ++right) {
                check(menu.items[left].action != menu.items[right].action,
                      "no action appears twice in one menu");
            }
        }
    }
}

void test_key_equivalents_unique() {
    for (const MenuSpec& menu : app::menu_specs()) {
        for (std::size_t left = 0; left < menu.items.size(); ++left) {
            const MenuItemSpec& first = menu.items[left];
            if (first.key == nullptr || first.key[0] == '\0') {
                continue;
            }
            for (std::size_t right = left + 1; right < menu.items.size(); ++right) {
                const MenuItemSpec& second = menu.items[right];
                if (second.key == nullptr || second.key[0] == '\0') {
                    continue;
                }
                const bool same_key = std::string(first.key) == std::string(second.key);
                const bool same_modifiers = first.command_key == second.command_key &&
                                            first.shift == second.shift &&
                                            first.option == second.option;
                check(!(same_key && same_modifiers),
                      "no two items in one menu share a key equivalent");
            }
        }
    }
}

void test_documented_menu_contents() {
    // File: Open, Save, Save As, Print, Save as PDF, two exports, Close -- with
    // the standard keys.
    int file_items = 0;
    bool open_cmd_o = false;
    bool save_cmd_s = false;
    bool save_as_shift = false;
    bool print_cmd_p = false;
    bool save_pdf_shift = false;
    bool exports = false;
    for (const MenuItemSpec& item : menu_named("File").items) {
        ++file_items;
        if (item.action == MenuAction::open) {
            open_cmd_o = std::string(item.key) == "o" && item.command_key && !item.shift;
        }
        if (item.action == MenuAction::save) {
            save_cmd_s = std::string(item.key) == "s" && item.command_key && !item.shift;
        }
        if (item.action == MenuAction::save_as) {
            save_as_shift = std::string(item.key) == "s" && item.shift && item.command_key;
        }
        if (item.action == MenuAction::print) {
            print_cmd_p = std::string(item.key) == "p" && item.command_key && !item.shift;
        }
        if (item.action == MenuAction::save_as_pdf) {
            save_pdf_shift = std::string(item.key) == "p" && item.command_key && item.shift;
        }
        if (item.action == MenuAction::export_pdf_sheet ||
            item.action == MenuAction::export_pdf_gantt) {
            exports = true;
        }
    }
    check(file_items == 8, "File has eight items");
    check(open_cmd_o, "Open is Cmd-O");
    check(save_cmd_s, "Save is Cmd-S");
    check(save_as_shift, "Save As is Cmd-Shift-S");
    check(print_cmd_p, "Print is Cmd-P");
    check(save_pdf_shift, "Save as PDF is Cmd-Shift-P");
    check(exports, "File has both PDF exports");

    // Edit: undo/redo, the clipboard, select all, wave-12 Delete Task and the
    // two wave-13 pending-edit dialogs.
    bool undo = false;
    bool redo_shift = false;
    bool copy = false;
    bool paste = false;
    bool select_all = false;
    bool delete_task = false;
    bool task_fields = false;
    bool predecessors = false;
    for (const MenuItemSpec& item : menu_named("Edit").items) {
        if (item.action == MenuAction::task_fields) {
            task_fields = std::string(item.key).empty();
        }
        if (item.action == MenuAction::predecessors) {
            predecessors = std::string(item.key).empty();
        }
        if (item.action == MenuAction::undo) {
            undo = std::string(item.key) == "z" && !item.shift;
        }
        if (item.action == MenuAction::redo) {
            redo_shift = std::string(item.key) == "z" && item.shift;
        }
        if (item.action == MenuAction::copy) {
            copy = std::string(item.key) == "c";
        }
        if (item.action == MenuAction::paste) {
            paste = std::string(item.key) == "v";
        }
        if (item.action == MenuAction::select_all) {
            select_all = std::string(item.key) == "a";
        }
        if (item.action == MenuAction::delete_task) {
            // Cmd-Backspace: the Mac's Delete key (0x08 as a key equivalent).
            delete_task = std::string(item.key) == "\b" && item.command_key &&
                          !item.shift && !item.option && item.separator_before;
        }
    }
    check(undo, "Undo is Cmd-Z");
    check(redo_shift, "Redo is Cmd-Shift-Z");
    check(copy, "Copy is Cmd-C");
    check(paste, "Paste is Cmd-V");
    check(select_all, "Select All is Cmd-A");
    check(delete_task, "Delete Task is Cmd-Backspace, after a separator");
    check(task_fields && predecessors,
          "Task Fields and Predecessors are in Edit with no key equivalent");

    // View: the four views (wave 11) with Cmd-Option-1..4, the usage periods,
    // and the three timescales, the shading toggle, the filter.
    bool view_task = false;
    bool view_resource = false;
    bool view_task_usage = false;
    bool view_resource_usage = false;
    bool usage_day = false;
    bool usage_week = false;
    bool usage_month = false;
    bool zoom_day = false;
    bool zoom_week = false;
    bool zoom_month = false;
    bool shading = false;
    bool filter = false;
    bool clear_filter = false;
    for (const MenuItemSpec& item : menu_named("View").items) {
        zoom_day = zoom_day || item.action == MenuAction::zoom_day;
        zoom_week = zoom_week || item.action == MenuAction::zoom_week;
        zoom_month = zoom_month || item.action == MenuAction::zoom_month;
        shading = shading || item.action == MenuAction::toggle_shading;
        filter = filter || item.action == MenuAction::show_filter;
        clear_filter = clear_filter || item.action == MenuAction::clear_filter;
        usage_day = usage_day || item.action == MenuAction::usage_day;
        usage_week = usage_week || item.action == MenuAction::usage_week;
        usage_month = usage_month || item.action == MenuAction::usage_month;
        if (item.action == MenuAction::view_task_sheet) {
            view_task = std::string(item.key) == "1" && item.command_key && item.option;
        }
        if (item.action == MenuAction::view_resource_sheet) {
            view_resource = std::string(item.key) == "2" && item.command_key && item.option;
        }
        if (item.action == MenuAction::view_task_usage) {
            view_task_usage = std::string(item.key) == "3" && item.command_key && item.option;
        }
        if (item.action == MenuAction::view_resource_usage) {
            view_resource_usage = std::string(item.key) == "4" && item.command_key && item.option;
        }
    }
    check(view_task && view_resource && view_task_usage && view_resource_usage,
          "View offers the four views as Cmd-Option-1..4");
    check(usage_day && usage_week && usage_month, "View offers the three usage periods");
    check(zoom_day && zoom_week && zoom_month, "View offers day/week/month timescales");
    check(shading, "View offers the working-day shading toggle");
    check(filter, "View offers the filter dialog");
    check(clear_filter, "View offers Clear Filter");

    // Insert: Task (wave 12, Cmd-I) and Resource (still unsupported).
    const MenuSpec& insert = menu_named("Insert");
    bool insert_task = false;
    bool insert_resource = false;
    for (const MenuItemSpec& item : insert.items) {
        if (item.action == MenuAction::insert_task) {
            insert_task = std::string(item.key) == "i" && item.command_key;
        }
        if (item.action == MenuAction::insert_resource) {
            insert_resource = true;
        }
    }
    check(insert.items.size() == 2, "Insert has two items");
    check(insert_task, "Insert Task is Cmd-I");
    check(insert_resource, "Insert Resource is still offered");

    // The application menu and Help.
    bool preferences = false;
    bool quit = false;
    for (const MenuItemSpec& item : menu_named("MS Project Shell").items) {
        preferences = preferences || item.action == MenuAction::preferences;
        quit = quit || item.action == MenuAction::quit_app;
    }
    check(preferences, "the application menu has Preferences");
    check(quit, "the application menu has Quit");
    bool about = false;
    for (const MenuItemSpec& item : menu_named("Help").items) {
        about = about || item.action == MenuAction::about;
    }
    check(about, "Help has About");
}

void test_enablement() {
    const MenuState full = everything_state();
    const MenuState empty = empty_state();

    for (const MenuAction action : app::all_menu_actions()) {
        if (app::menu_item_supported(action)) {
            check(app::menu_item_enabled(action, full), "a supported action is enabled when possible");
            check(app::menu_disabled_reason(action)[0] == '\0',
                  "a supported action has no disabled reason");
        } else {
            check(!app::menu_item_enabled(action, full),
                  "an unsupported action is disabled even in a full state");
            check(!app::menu_item_enabled(action, empty),
                  "an unsupported action is disabled in an empty state");
            check(app::menu_disabled_reason(action)[0] != '\0',
                  "an unsupported action says why in the menu");
        }
    }

    // The unsupported two, named: this is the brief's "grey out the rest rather
    // than fake it".  Insert Task and Delete Task became real in wave 12.
    check(!app::menu_item_supported(MenuAction::cut), "Cut is not supported");
    check(!app::menu_item_supported(MenuAction::insert_resource),
          "Insert Resource is not supported");
    check(app::menu_item_supported(MenuAction::insert_task), "Insert Task is supported");
    check(app::menu_item_supported(MenuAction::delete_task), "Delete Task is supported");
    check(app::menu_item_supported(MenuAction::task_fields), "Task Fields is supported");
    check(app::menu_item_supported(MenuAction::predecessors), "Predecessors is supported");

    // The wave-12 structural commands: a document, the Task Sheet view and no open
    // edit; Delete also needs a selection.
    check(app::menu_item_enabled(MenuAction::insert_task, everything_state()),
          "Insert Task works in the task sheet");
    check(app::menu_item_enabled(MenuAction::delete_task, everything_state()),
          "Delete Task works with a selection");
    check(!app::menu_item_enabled(MenuAction::insert_task, empty_state()),
          "Insert Task needs a document");
    check(!app::menu_item_enabled(MenuAction::delete_task, empty_state()),
          "Delete Task needs a document");
    MenuState structural = everything_state();
    structural.editing = true;
    check(!app::menu_item_enabled(MenuAction::insert_task, structural),
          "Insert Task is off while a cell edit is open");
    check(!app::menu_item_enabled(MenuAction::delete_task, structural),
          "Delete Task is off while a cell edit is open");
    structural = everything_state();
    structural.has_selection = false;
    check(app::menu_item_enabled(MenuAction::insert_task, structural),
          "Insert Task works without a selection (it appends at the end)");
    check(!app::menu_item_enabled(MenuAction::delete_task, structural),
          "Delete Task needs a selected row");
    structural = everything_state();
    structural.task_view = false;
    check(!app::menu_item_enabled(MenuAction::insert_task, structural),
          "Insert Task is task-view only");
    check(!app::menu_item_enabled(MenuAction::delete_task, structural),
          "Delete Task is task-view only");
    check(!app::menu_item_enabled(MenuAction::task_fields, structural),
          "Task Fields is task-view only");
    check(!app::menu_item_enabled(MenuAction::predecessors, structural),
          "Predecessors is task-view only");

    // The wave-13 dialogs act on the selected task: a selection is required, an
    // open cell edit and an empty state both disable them.
    check(app::menu_item_enabled(MenuAction::task_fields, everything_state()),
          "Task Fields works with a selection");
    check(app::menu_item_enabled(MenuAction::predecessors, everything_state()),
          "Predecessors works with a selection");
    check(!app::menu_item_enabled(MenuAction::task_fields, empty_state()),
          "Task Fields needs a document");
    check(!app::menu_item_enabled(MenuAction::predecessors, empty_state()),
          "Predecessors needs a document");
    MenuState dialogs = everything_state();
    dialogs.has_selection = false;
    check(!app::menu_item_enabled(MenuAction::task_fields, dialogs),
          "Task Fields needs a selected task");
    check(!app::menu_item_enabled(MenuAction::predecessors, dialogs),
          "Predecessors needs a selected task");
    dialogs = everything_state();
    dialogs.editing = true;
    check(!app::menu_item_enabled(MenuAction::task_fields, dialogs),
          "Task Fields is off while a cell edit is open");
    check(!app::menu_item_enabled(MenuAction::predecessors, dialogs),
          "Predecessors is off while a cell edit is open");

    // Document-dependent commands are off with no document.
    check(!app::menu_item_enabled(MenuAction::save, empty), "Save needs a document");
    check(!app::menu_item_enabled(MenuAction::save_as, empty), "Save As needs a document");
    check(!app::menu_item_enabled(MenuAction::export_pdf_sheet, empty),
          "Export needs a document");
    check(!app::menu_item_enabled(MenuAction::print, empty), "Print needs a document");
    check(!app::menu_item_enabled(MenuAction::save_as_pdf, empty),
          "Save as PDF needs a document");
    check(!app::menu_item_enabled(MenuAction::select_all, empty), "Select All needs a document");
    check(app::menu_item_enabled(MenuAction::open, empty), "Open always works");
    check(app::menu_item_enabled(MenuAction::preferences, empty),
          "Preferences always work");

    // Undo/redo follow the model's stacks.
    MenuState state = everything_state();
    state.can_undo = false;
    check(!app::menu_item_enabled(MenuAction::undo, state), "Undo needs an undo step");
    state.can_redo = false;
    check(!app::menu_item_enabled(MenuAction::redo, state), "Redo needs a redo step");

    // Clipboard: needs a selection, and is off while an edit session is open.
    state = everything_state();
    state.has_selection = false;
    state.can_copy = false;
    check(!app::menu_item_enabled(MenuAction::copy, state), "Copy needs a selection");
    state = everything_state();
    state.editing = true;
    check(!app::menu_item_enabled(MenuAction::copy, state), "Copy is off while editing");
    check(!app::menu_item_enabled(MenuAction::paste, state), "Paste is off while editing");
    check(!app::menu_item_enabled(MenuAction::select_all, state),
          "Select All is off while editing");

    // Filter state.
    state = everything_state();
    state.filter_active = false;
    check(!app::menu_item_enabled(MenuAction::clear_filter, state),
          "Clear Filter needs an active filter");
    check(app::menu_item_enabled(MenuAction::show_filter, state),
          "Filter is available without one");

    // Outline collapse state.
    state = everything_state();
    state.any_expanded = false;
    check(!app::menu_item_enabled(MenuAction::collapse_all, state),
          "Collapse All needs something expanded");
    state = everything_state();
    state.any_collapsed = false;
    check(!app::menu_item_enabled(MenuAction::expand_all, state),
          "Expand All needs something collapsed");

    // Wave 11: in the resource/usage views the task-only commands degrade to
    // disabled while the view and usage-period switches stay available.
    MenuState non_task = everything_state();
    non_task.task_view = false;
    check(!app::menu_item_enabled(MenuAction::export_pdf_sheet, non_task),
          "Sheet export is task-view only");
    check(!app::menu_item_enabled(MenuAction::export_pdf_gantt, non_task),
          "Gantt export is task-view only");
    check(!app::menu_item_enabled(MenuAction::print, non_task),
          "Print is task-view only");
    check(!app::menu_item_enabled(MenuAction::save_as_pdf, non_task),
          "Save as PDF is task-view only");
    check(!app::menu_item_enabled(MenuAction::undo, non_task), "Undo is task-view only");
    check(!app::menu_item_enabled(MenuAction::redo, non_task), "Redo is task-view only");
    check(!app::menu_item_enabled(MenuAction::copy, non_task), "Copy is task-view only");
    check(!app::menu_item_enabled(MenuAction::paste, non_task), "Paste is task-view only");
    check(!app::menu_item_enabled(MenuAction::select_all, non_task),
          "Select All is task-view only");
    check(!app::menu_item_enabled(MenuAction::zoom_day, non_task),
          "The Gantt timescale is task-view only");
    check(!app::menu_item_enabled(MenuAction::show_filter, non_task),
          "Filter is task-view only");
    check(!app::menu_item_enabled(MenuAction::collapse_all, non_task),
          "Collapse is task-view only");
    check(!app::menu_item_enabled(MenuAction::task_fields, non_task),
          "Task Fields is task-view only (again, from the non-task state)");
    check(!app::menu_item_enabled(MenuAction::predecessors, non_task),
          "Predecessors is task-view only (again, from the non-task state)");
    check(app::menu_item_enabled(MenuAction::view_task_sheet, non_task),
          "the Task Sheet switch works from the other views");
    check(app::menu_item_enabled(MenuAction::view_resource_usage, non_task),
          "the Resource Usage switch works from the other views");
    check(app::menu_item_enabled(MenuAction::usage_week, non_task),
          "the usage period is available from the other views");
}

}  // namespace

int main() {
    test_menus();
    test_actions_present_once_per_menu();
    test_key_equivalents_unique();
    test_documented_menu_contents();
    test_enablement();

    if (g_failures != 0) {
        std::printf("FAIL: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    std::printf("menu_tests: %d checks passed\n", g_checks);
    return 0;
}
