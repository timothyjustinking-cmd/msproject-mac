// app/menu_tests.cpp -- the menu bar as data, proved headlessly.
//
// The macOS shell builds its NSMenuItems from app::menu_specs() and asks
// app::menu_item_enabled() for every validation pass, so these checks are what
// stands in for a click test on the development box (main.mm is compiled only by
// the macOS CI job).
//
// What is pinned: the six menus and their order; every action present; no two
// items in one menu sharing a key equivalent; the documented key equivalents for
// Open/Save/Save As/Undo/Redo/Copy/Paste/Select All/Zoom/Filter; and --
// importantly -- that the three things the model cannot do (Insert > Task,
// Insert > Resource, Cut) are PRESENT, permanently disabled, and carry a reason.

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
    // File: Open, Save, Save As, two exports, Close -- with the standard keys.
    int file_items = 0;
    bool open_cmd_o = false;
    bool save_cmd_s = false;
    bool save_as_shift = false;
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
        if (item.action == MenuAction::export_pdf_sheet ||
            item.action == MenuAction::export_pdf_gantt) {
            exports = true;
        }
    }
    check(file_items == 6, "File has six items");
    check(open_cmd_o, "Open is Cmd-O");
    check(save_cmd_s, "Save is Cmd-S");
    check(save_as_shift, "Save As is Cmd-Shift-S");
    check(exports, "File has both PDF exports");

    // Edit: undo/redo, the clipboard, select all.
    bool undo = false;
    bool redo_shift = false;
    bool copy = false;
    bool paste = false;
    bool select_all = false;
    for (const MenuItemSpec& item : menu_named("Edit").items) {
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
    }
    check(undo, "Undo is Cmd-Z");
    check(redo_shift, "Redo is Cmd-Shift-Z");
    check(copy, "Copy is Cmd-C");
    check(paste, "Paste is Cmd-V");
    check(select_all, "Select All is Cmd-A");

    // View: the three timescales, the shading toggle, the filter.
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
    }
    check(zoom_day && zoom_week && zoom_month, "View offers day/week/month timescales");
    check(shading, "View offers the working-day shading toggle");
    check(filter, "View offers the filter dialog");
    check(clear_filter, "View offers Clear Filter");

    // Insert: exactly the two entries this wave cannot support.
    const MenuSpec& insert = menu_named("Insert");
    check(insert.items.size() == 2, "Insert has two items");
    if (insert.items.size() == 2) {
        check(insert.items[0].action == MenuAction::insert_task, "Insert has Task");
        check(insert.items[1].action == MenuAction::insert_resource, "Insert has Resource");
    }

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

    // The unsupported three, named: this is the brief's "grey out the rest
    // rather than fake it".
    check(!app::menu_item_supported(MenuAction::cut), "Cut is not supported");
    check(!app::menu_item_supported(MenuAction::insert_task), "Insert Task is not supported");
    check(!app::menu_item_supported(MenuAction::insert_resource),
          "Insert Resource is not supported");

    // Document-dependent commands are off with no document.
    check(!app::menu_item_enabled(MenuAction::save, empty), "Save needs a document");
    check(!app::menu_item_enabled(MenuAction::save_as, empty), "Save As needs a document");
    check(!app::menu_item_enabled(MenuAction::export_pdf_sheet, empty),
          "Export needs a document");
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
