// app/menu_model.h -- the menu bar as DATA, so its structure and its enablement
// rules are proved headlessly instead of only on macOS.
//
// WHY THIS FILE EXISTS
//   main.mm is Objective-C++ and cannot be compiled on the development box, so
//   every line added there is unverified until the macOS CI job runs.  The menu
//   bar is exactly the kind of thing that can be data: a title, a list of items
//   with a stable action id, a key equivalent, and a rule saying when the item
//   is enabled.  main.mm then only walks menu_specs() to build NSMenuItems with
//   tag = the action id, and dispatches by a switch on that tag.  app/menu_tests.cpp
//   checks that there is exactly one File/Edit/View/Insert/Help menu, that
//   every action appears, that no two items in one menu share a key equivalent,
//   and that the items this wave cannot support (Insert > Task/Resource, Cut)
//   are permanently disabled rather than faked.
//
// "ONLY WHAT THE MODEL SUPPORTS"
//   Insert > Task needs a writer that can GROW a table: RecordSet has no
//   insert/erase and core/mppwrite cannot add a directory entry or a table row
//   (spec/mpp-writer.md sections 5 and 10.4; wave 10 stream W45 owns it).
//   Insert > Resource needs a resource table (wave 10 stream W43).  Cut needs a
//   SheetModel operation that does not exist (copy_selection and paste do).
//   All three are in the menu and permanently disabled with a reason in
//   menu_disabled_reason(): greyed out, never faked.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace app {

// One command the shell implements.  The value is used as the NSMenuItem's tag
// on macOS, so it must stay small and stable within a build.
enum class MenuAction : std::uint8_t {
    none = 0,
    about,
    preferences,
    hide_app,
    quit_app,
    keyboard_shortcuts,
    open,
    save,
    save_as,
    export_pdf_sheet,
    export_pdf_gantt,
    close_window,
    undo,
    redo,
    cut,
    copy,
    paste,
    select_all,
    zoom_in,
    zoom_out,
    zoom_day,
    zoom_week,
    zoom_month,
    toggle_shading,
    show_filter,
    clear_filter,
    collapse_all,
    expand_all,
    insert_task,
    insert_resource,
};

// Every action, for tests and for the shell's exhaustive dispatch check.
const std::vector<MenuAction>& all_menu_actions();

struct MenuItemSpec {
    MenuAction action = MenuAction::none;
    const char* title = "";
    // The key equivalent, lower case, without modifiers ("" for none).
    const char* key = "";
    bool command_key = true;  // Command is the normal modifier
    bool shift = false;
    bool option = false;
    bool separator_before = false;
};

struct MenuSpec {
    const char* title = "";
    std::vector<MenuItemSpec> items;
};

// The whole menu bar, in order: the application menu, File, Edit, View, Insert,
// Help.  The application menu holds Preferences/Hide/Quit; About is in Help, as
// the brief asks.
const std::vector<MenuSpec>& menu_specs();

// The item for `action`, or nullptr when the menu bar does not list it.
const MenuItemSpec* find_menu_item(MenuAction action);

// What the current document can do.  The shell fills this in before every
// validation pass (menuNeedsUpdate:/validateMenuItem:).
struct MenuState {
    bool has_document = false;
    bool editing = false;      // an in-cell edit session is open
    bool has_selection = false;
    bool can_undo = false;
    bool can_redo = false;
    bool can_copy = false;
    bool can_paste = false;
    bool dirty = false;        // edited starts that a save would write
    bool filter_active = false;
    bool any_collapsed = false;
    bool any_expanded = false;
};

// True when `action` may run in `state`.  An action the model cannot support is
// false in every state (see menu_item_supported()).
bool menu_item_enabled(MenuAction action, const MenuState& state) noexcept;

// False for the three actions the model cannot support yet; true otherwise.
bool menu_item_supported(MenuAction action) noexcept;

// Why an unsupported action is greyed out; "" for a supported one.
const char* menu_disabled_reason(MenuAction action) noexcept;

// A stable name for logging and the status line.
const char* menu_action_name(MenuAction action) noexcept;

}  // namespace app
