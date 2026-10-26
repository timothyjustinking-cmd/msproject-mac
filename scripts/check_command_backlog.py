#!/usr/bin/env python3
"""Cross-check docs/ui-command-backlog.md against the independent ribbon pin.

W31-C4 / review U3: the generator used to skip the command buttons built with
small_cmd()/add(Kind::button), and the old one-way check only walked the rows
the generator had emitted.  This check is bidirectional:

  * every pin command control (kind button/small/split) must have exactly one
    backlog row, keyed by (tab, group, label), and vice versa;
  * a pin whose `enabled` column is 1 must be `implemented` in the backlog, and
    a pin with 0 must be `disabled`, except that a split button may be enabled
    while its menu items are disabled (the backlog row describes the menu).

Run from anywhere: `python -I scripts/check_command_backlog.py`.
Exit 0 = agree, 1 = mismatch, 2 = unreadable input.
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = ROOT / "app_qt" / "ribbon_pin.tsv"
BACKLOG = ROOT / "docs" / "ui-command-backlog.md"

COMMAND_KINDS = {"button", "small", "split"}


def pin_rows() -> list[tuple[str, str, str, str, str]]:
    """(tab, group, label, kind, enabled) for every command control."""
    rows: list[tuple[str, str, str, str, str]] = []
    for line in PIN.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        fields = line.split("\t")
        if len(fields) < 9 or fields[3] not in COMMAND_KINDS:
            continue
        rows.append(
            (
                fields[0],
                fields[2],
                fields[4].replace("\\n", " "),
                fields[3],
                fields[5],
            )
        )
    return rows


def backlog_rows() -> list[tuple[str, str, str, str]]:
    """(tab, group, label, status) for every ribbon row."""
    rows: list[tuple[str, str, str, str]] = []
    pattern = re.compile(
        r"\| ([^|]+) \| ([^|]+) \| ([^|]+) \| (implemented|disabled) \|"
    )
    for line in BACKLOG.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            rows.append(tuple(value.strip() for value in match.groups()))
    return rows


def main() -> int:
    try:
        pin = pin_rows()
        backlog = backlog_rows()
    except OSError as error:
        print(f"check_command_backlog: cannot read: {error}", file=sys.stderr)
        return 2

    failures = 0
    pin_keys = Counter((tab, group, label) for tab, group, label, _, _ in pin)
    backlog_keys = Counter((tab, group, label) for tab, group, label, _ in backlog)
    for key in sorted(pin_keys - backlog_keys):
        print(f"MISSING from backlog: {key[0]} / {key[1]} / {key[2]}")
        failures += pin_keys[key]
    for key in sorted(backlog_keys - pin_keys):
        print(f"NOT IN the pin: {key[0]} / {key[1]} / {key[2]}")
        failures += backlog_keys[key]
    for key in sorted(pin_keys & backlog_keys):
        if pin_keys[key] != 1 or backlog_keys[key] != 1:
            print(
                f"DUPLICATE row: {key[0]} / {key[1]} / {key[2]} "
                f"(pin {pin_keys[key]}, backlog {backlog_keys[key]})"
            )
            failures += 1

    # The enabled column and the row's status must tell the same story.  A
    # split is the one exception: its button opens a menu whose items can all
    # be disabled (WBS, Set Baseline, Format, Drawing).
    status_by_key = {
        (tab, group, label): status for tab, group, label, status in backlog
    }
    for tab, group, label, kind, enabled in pin:
        status = status_by_key.get((tab, group, label))
        if status is None:
            continue
        if enabled == "1" and status == "disabled" and kind != "split":
            print(f"PIN ENABLED but backlog disabled: {tab} / {group} / {label}")
            failures += 1
        if enabled == "0" and status == "implemented":
            print(f"PIN DISABLED but backlog implemented: {tab} / {group} / {label}")
            failures += 1

    print(
        f"check_command_backlog: {len(pin)} pin command controls, "
        f"{len(backlog)} backlog ribbon rows, {failures} mismatch(es)"
    )
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
