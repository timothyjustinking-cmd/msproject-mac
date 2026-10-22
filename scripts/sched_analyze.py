#!/usr/bin/env python3
"""Static evidence report for spec/scheduling.md (W3).

Reads the decompiled C and the meta TSVs and prints the facts the scheduling
spec relies on, with addresses and line numbers, so a reviewer can re-run one
command instead of trusting prose.

No MS Project process is started; this is pure file analysis.

    python scripts/sched_analyze.py            # full report
    python scripts/sched_analyze.py --section recalc
"""
import argparse
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sched_index as si  # noqa: E402

ROOT = si.ROOT


def tsv(name, under="analysis"):
    return list(csv.reader(open(os.path.join(ROOT, under, name), encoding="utf-8"),
                           delimiter="\t"))


def enum_rows(enum_name):
    rows = tsv("typelib.tsv")
    return [r for r in rows if r and r[0] == enum_name and r[1] == "ENUM"]


def show_enum(enum_name):
    print(f"\n## {enum_name}")
    for r in enum_rows(enum_name):
        # typelib.tsv: 0 enum, 1 ENUM, 2 guid, 3 ?, 4 value, 5 ?, 6 member
        member = r[6] if len(r) > 6 and r[6] else (r[-1] if r else "")
        print(f"  {r[4]:>6}  {member}")


def excerpt(addr, pattern, context=2, limit=6):
    name, start, body = si.get_body(si.PARTS_BY_NAME, si.INDEX, addr)
    pat = re.compile(pattern)
    shown = 0
    for i, line in enumerate(body):
        if pat.search(line):
            print(f"  {name}:{start + 1 + i}")
            for j in range(max(0, i - context), min(len(body), i + context + 1)):
                print(f"    {start + 1 + j:7d}  {body[j].rstrip()}")
            shown += 1
            if shown >= limit:
                break
    if not shown:
        print("  (no match)")


def section_calendar():
    print("\n# Calendar arithmetic")
    print("\nCalendarReader vtable (analysis/meta/vtables.tsv, vtable 140fffa18):")
    for a, c, s, f in [r for r in si.load_tsv(os.path.join(si.META, "vtables.tsv"))[1:]
                       if r and r[0] == "140fffa18"]:
        print(f"  slot {s:>2}  {f}  {si.NAMES.get(si.norm(f), '')}")
    print("\nKey functions and sizes:")
    for addr, why in [
        ("14024e7c0", "GetEndDate / FastDateEnd: add duration (forward)"),
        ("14038bdb0", "vfunc 3: subtract duration (backward)"),
        ("140305e00", "WorkTimeBetween / FastWorkTimeBetween"),
        ("140203ccc", "GetEffectiveWeekExtents"),
        ("140200b10", "add duration inside a day (interval walk)"),
        ("14024ed00", "work time between two days"),
        ("14038c2f0", "end-of-day alignment"),
        ("14038c410", "end of previous working day"),
        ("14038c530", "start time when subtracting inside a day"),
    ]:
        r = si.FUNCS.get(addr)
        print(f"  {addr}  {why}")
        print(f"      {r[3] if r else '?'} bytes, callers={r[5] if r else '?'}, "
              f"callees={r[6] if r else '?'}")
    print("\nDay-pattern field reads (interval layout):")
    excerpt("140200cac", r"\+ 0x1c|\+ 8 \+|param_2 \+ 2", context=1)
    print("\nWeek cache fill (7 x 0x3c day patterns):")
    excerpt("14085ae80", r"0x1c4|0x1c8|lVar14 \* 0x3c", context=1, limit=4)


def section_recalc():
    print("\n# Recalculation")
    print("\nTBkndRecalc vtable slot 11 (public Recalc):")
    r = si.FUNCS.get("1402b5360")
    print(f"  1402b5360  {r[3]} bytes  {si.NAMES.get('1402b5360', '')}")
    print("\nCalls inside FUN_1402b5360 (forward/backward order):")
    excerpt("1402b5360", r"FUN_1402b(558c|afbe4|bb2b0|bc5d4)|0x1a0", context=2, limit=8)
    print("\nPer-element loop FUN_1402b570c (vtable offsets 0x170 / 0x208 / 0x128):")
    excerpt("1402b570c", r"\+ 0x170|\+ 0x208|\+ 0x128|\* plVar7 \+ 0x1", context=1, limit=8)
    print("\nNeeds-another-pass test FUN_1402afbe4:")
    excerpt("1402afbe4", r"\+ 0x130", context=2, limit=3)
    print("\nOutline rollup FUN_1402bb2b0 (outline level walk):")
    excerpt("1402bb2b0", r"\+ 0x1a0|\+ 0xc0|\+ 0x250", context=1, limit=6)


def section_constraints():
    print("\n# Constraints")
    show_enum("PjConstraint")
    print("\nConstraintType setter (FUN_1401f2570, part_0000.c):")
    for ln in (58748, 58749, 58760, 58775, 58832, 58834, 58883, 58890, 58929):
        print(f"  part_0000.c:{ln}: {si.PARTS_BY_NAME['part_0000.c'][ln-1].rstrip()}")
    print("\nConstraint date writer FUN_1406bbf0c:")
    excerpt("1406bbf0c", r"0xb400012|\+ 0x78", context=2, limit=3)
    print("\nConstraint type writer FUN_1406bbfbc:")
    excerpt("1406bbfbc", r"0xb400011|\+ 0x84", context=2, limit=3)
    print("\nSystem constraint writer FUN_14083b3ac:")
    excerpt("14083b3ac", r"0xb400467|system constraint", context=2, limit=3)


def section_units():
    print("\n# Task types, effort-driven, units")
    show_enum("PjTaskFixedType")
    print("\nTaskElement::vfunc_38 = task-type/effort mode (FUN_1404e9400):")
    excerpt("1404e9400", r"0xf2|0x1c\)|expect work|expect effort", context=3, limit=8)
    print("\nSetDependantFields RTTI (the edit propagator):")
    lines = open(os.path.join(si.META, "strings.tsv"), encoding="utf-8",
                 errors="replace").read().splitlines()
    for i, line in enumerate(lines):
        if "SetDependantFields" in line and "lambda" in line:
            print(f"  strings.tsv:{i+1}: {line}")
            break


def section_links():
    print("\n# Links")
    show_enum("PjTaskLinkType")
    rows = tsv("field_metadata_links.tsv")
    print(f"\nLink table: {len(rows)-1} fields (analysis/field_metadata_links.tsv)")
    dur = [r for r in rows[1:] if len(r) > 6 and r[6] == "Duration"]
    print(f"  Duration-typed fields (lag/lead candidates): {len(dur)}")
    for r in dur:
        print(f"    {r[1]}  offset={r[7]} size={r[8]} flags={r[9]}")


def section_fields():
    print("\n# Field ids used by the spec (analysis/com_validation.tsv)")
    wanted = {"Work", "ConstraintType", "ConstraintDate", "Critical", "FreeSlack",
              "TotalSlack", "Duration", "Start", "Finish", "EarlyStart",
              "EarlyFinish", "LateStart", "LateFinish", "Deadline", "StartSlack",
              "FinishSlack", "FixedDuration", "Type"}
    rows = tsv("com_validation.tsv")
    for r in rows[1:]:
        if len(r) >= 4 and r[1] in wanted:
            print(f"  {r[0]}  {r[1]:<16} {r[2]:<8} {r[3]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--section", choices=["calendar", "recalc", "constraints",
                                          "units", "links", "fields"])
    args = ap.parse_args()
    si.PARTS = si.load_parts()
    si.PARTS_BY_NAME = dict(si.PARTS)
    si.INDEX = si.build_index(si.PARTS)
    si.NAMES = si.load_names()
    si.FUNCS = si.load_functions()
    sections = {
        "calendar": section_calendar,
        "recalc": section_recalc,
        "constraints": section_constraints,
        "units": section_units,
        "links": section_links,
        "fields": section_fields,
    }
    print("# W3 static evidence report")
    print("# decompiled dir:", os.path.join(ROOT, "decompiled"))
    if args.section:
        sections[args.section]()
    else:
        for fn in sections.values():
            fn()
    print("\n# done")


if __name__ == "__main__":
    main()
