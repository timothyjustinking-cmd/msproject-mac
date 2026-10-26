#!/usr/bin/env python3
"""Generate docs/ui-command-backlog.md from the command models.

Sources of truth (operator decision 9):
  * app/menu_model.cpp          -- every menu command, in menu order
  * app_qt/ribbon_bar.cpp       -- every ribbon tab/group/button, in ribbon order
  * app_qt/command_status.h     -- which MenuActions the Qt shell implements

The backlog is generated, not hand-written: run

    python app_qt/tools/command_backlog.py

from the repository root after changing any of the three sources.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "ui-command-backlog.md"

MISSING_REASON = {
    "insert_resource": "model: no resource writer; the resource sheet is read-only",
    "hide_app": "chrome: Qt has no portable hide-application command",
    # W26-6.1: the View drop-downs' unimplemented entries are MenuAction::none,
    # and so are the ribbon commands with no MenuAction at all.  One neutral
    # wording covers both tables.
    "none": "future: not implemented in this port",
}

# W28-1.3 / W29-2.1: controls whose click runs a RibbonCommand rather than a
# MenuAction.  A split carries no MenuAction; a large button with a RibbonCommand
# still passes MenuAction::none.  The ribbon table names the implemented ones
# here.  The WBS and Set Baseline splits open menus whose every item is disabled,
# and the Format/Drawing splits have no engine, so those stay "none".
IMPLEMENTED_RIBBON_COMMANDS = {
    "mark_on_track",
    "text_styles",
    "gridlines",
    "bar_styles",
    # W29-4.1: both open the captured dialog; the dialog itself only dismisses.
    "compare_projects",
    "visual_reports",
    # W30-1.0: the Task tab's Manually Schedule button and Mode menu, both on
    # the measured automatic -> manual task-mode writer.
    "manually_schedule",
    "task_mode",
    # W30-1.5: the Task tab's Details opens the Task Form details pane the
    # View tab already docks (decision 16); a view, never a file write.
    "task_details",
    # W30-2.2: the Resource tab's Details opens the read-only Resource Form in
    # the same shared details splitter; a view, never a file write.
    "resource_details",
    # W30-1.7: Scroll to Task scrolls the sheet to the selected row and pans
    # the chart to the task's start; a view, never a file write.
    "scroll_to_task",
    # W30-1.8: Fill Down fills a Start-column selection through the measured
    # start writer; a non-Start column is refused, so the command is live but
    # its measured scope is the Start column only.
    "fill_down",
    # W30-1.10: Inspect opens the read-only Task Inspector pane (the selected
    # row's scheduler facts); a view, never a file write.
    "inspect",
    # W30-3.3: Spelling opens the read-only word-list report over task names.
    "spelling",
}

# W29-2.1 (operator decision 15): a dialog can be live while part of its engine
# is not built.  The command is implemented (it opens, applies what it can and
# never fakes success), and the remaining gap is named here so the backlog still
# carries it.
RIBBON_GAPS = {
    "text_styles": "gap: background patterns not implemented in this port",
    "gridlines": "gap: laid out; gridlines engine not implemented, OK disabled",
    "bar_styles": "gap: appearance/row editing not implemented; colours and text fields only",
    "visual_reports":
        "gap: laid out; report engine not implemented, all current-affecting controls "
        "disabled",
    "compare_projects":
        "gap: laid out; comparison engine not implemented, OK disabled",
    "task_mode":
        "gap: Manually Scheduled only; Auto Schedule would also reschedule the "
        "task and this port has no scheduler",
    "spelling":
        "gap: the port's own word list only; no Microsoft Project dictionary and "
        "no correction",
}


def implemented_actions() -> set[str]:
    text = (ROOT / "app_qt" / "command_status.h").read_text(encoding="utf-8")
    block = text.split("inline bool shell_implements(app::MenuAction", 1)[1].split(
        "// UNIMPLEMENTED", 1
    )[0]
    return set(re.findall(r"case app::MenuAction::(\w+):", block))


def menu_commands() -> list[tuple[str, str, str]]:
    text = (ROOT / "app" / "menu_model.cpp").read_text(encoding="utf-8")
    rows: list[tuple[str, str, str]] = []
    menu = ""
    for line in text.splitlines():
        title = re.search(r'(\w+)\.title\s*=\s*"([^"]+)"', line)
        if title:
            menu = title.group(2)
        # W28-2.1: outline commands have no Command modifier, so menu_model.cpp
        # writes them with item_no_command(); they are ordinary menu commands.
        item = re.search(
            r'(?:item|item_no_command)\(MenuAction::(\w+),\s*"([^"]*)"', line
        )
        if item:
            rows.append((menu, item.group(2), item.group(1)))
    return rows


def ribbon_commands() -> list[tuple[str, str, str, str]]:
    """Every ribbon command, in ribbon order, from the wave-26 spec builders.

    The ribbon spec is code, not a literal table: tabs are built with
    ``add_group(tab_var, "Group", width)`` and commands with
    ``large/small(group_var, "Label", app::MenuAction::x, ...)`` (``split``
    carries no MenuAction; a split that continues onto the next line can carry a
    ``RibbonCommand``).  Only command controls are listed: the combos, check
    boxes, the gallery, dialog launchers and the date field are controls, not
    menu commands, and the inventory pin (app_qt/ribbon_pin.tsv) already
    records them.
    """
    text = (ROOT / "app_qt" / "ribbon_bar.cpp").read_text(encoding="utf-8")
    block = text.split("const std::vector<TabSpec>& tab_specs()", 1)[1].split(
        "return tabs;", 1
    )[0]
    tab_titles: dict[str, str] = {}
    group_titles: dict[str, tuple[str, str]] = {}
    rows: list[tuple[str, str, str, str]] = []
    current_tab = ""
    lines = block.splitlines()
    for index, line in enumerate(lines):
        tab = re.search(r"TabSpec\s+(\w+);", line)
        if tab:
            current_tab = tab.group(1)
            continue
        title = re.search(r"(\w+)\.title\s*=\s*\"([^\"]+)\";", line)
        if title and title.group(1) == current_tab:
            tab_titles[current_tab] = title.group(2)
            continue
        group = re.search(
            r"GroupSpec&\s+(\w+)\s*=\s*add_group\(\w+,\s*\"([^\"]+)\",\s*\d+\)", line
        )
        if group:
            group_titles[group.group(1)] = (current_tab, group.group(2))
            continue
        command = re.search(
            r"(?:large|small)\((\w+),\s*\"((?:[^\"\\]|\\.)*)\",\s*"
            r"app::MenuAction::(\w+)",
            line,
        )
        if command:
            group_var, label, action = command.groups()
            context = group_titles.get(group_var)
            if context is not None:
                # W29-2.1: a large/small call built for a RibbonCommand passes
                # MenuAction::none on the opening line and names the command on
                # a continuation line (the split rule below).
                if action == "none" and line.rstrip().endswith(","):
                    for follow in lines[index + 1:index + 4]:
                        found = re.search(r"RibbonCommand::(\w+)", follow)
                        if found:
                            action = found.group(1)
                            break
                        if ");" in follow:
                            break
                rows.append(
                    (
                        tab_titles.get(context[0], context[0]),
                        context[1],
                        label.replace("\\n", " "),
                        action,
                    )
                )
            continue
        split = re.search(r"split\((\w+),\s*\"([^\"]+)\",", line)
        if split:
            context = group_titles.get(split.group(1))
            if context is not None:
                command = "none"
                if line.rstrip().endswith(","):
                    # The call continues on the next line(s); a split that
                    # states enabled/command puts the command there.
                    for follow in lines[index + 1:index + 4]:
                        found = re.search(r"RibbonCommand::(\w+)", follow)
                        if found:
                            command = found.group(1)
                            break
                        if ");" in follow:
                            break
                rows.append(
                    (
                        tab_titles.get(context[0], context[0]),
                        context[1],
                        split.group(2),
                        command,
                    )
                )
    return rows


def status(action: str, implemented: set[str]) -> tuple[str, str]:
    if action in implemented or action in IMPLEMENTED_RIBBON_COMMANDS:
        return "implemented", RIBBON_GAPS.get(action, "")
    return "disabled", MISSING_REASON.get(action, "future: not implemented in this port")


def main() -> int:
    implemented = implemented_actions()
    ribbon = ribbon_commands()
    menus = menu_commands()
    lines: list[str] = []
    lines.append("# UI command backlog (generated)")
    lines.append("")
    lines.append(
        "Generated by `python app_qt/tools/command_backlog.py` from "
        "`app/menu_model.cpp`, `app_qt/ribbon_bar.cpp` and "
        "`app_qt/command_status.h`. Do not edit by hand: change a source and "
        "regenerate."
    )
    lines.append("")
    lines.append(
        "Every row whose status is `disabled` is greyed in the Qt shell with the "
        "tooltip \"not implemented in this port\" (operator decision 9)."
    )
    lines.append("")
    lines.append("## Ribbon")
    lines.append("")
    lines.append("| Tab | Group | Command | Status | Why missing |")
    lines.append("|---|---|---|---|---|")
    disabled_ribbon = 0
    for tab, group, label, action in ribbon:
        state, why = status(action, implemented)
        if state == "disabled":
            disabled_ribbon += 1
        lines.append(f"| {tab} | {group} | {label} | {state} | {why} |")
    lines.append("")
    lines.append("## Menu bar")
    lines.append("")
    lines.append("| Menu | Command | Status | Why missing |")
    lines.append("|---|---|---|---|")
    disabled_menu = 0
    for menu, label, action in menus:
        state, why = status(action, implemented)
        if state == "disabled":
            disabled_menu += 1
        lines.append(f"| {menu} | {label} | {state} | {why} |")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"* ribbon commands: {len(ribbon)} ({disabled_ribbon} disabled)")
    lines.append(f"* menu commands: {len(menus)} ({disabled_menu} disabled)")
    lines.append(
        "* implementation source: `shell_implements()` in "
        "`app_qt/command_status.h`; keep it in step with "
        "`MainWindow::handle_action()`."
    )
    lines.append("")
    # newline="\n": keep the generated doc LF like the rest of docs/ (a Windows
    # default text write would turn every line into CRLF).
    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(ribbon)} ribbon, {len(menus)} menu rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
