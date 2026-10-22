#!/usr/bin/env python3
"""Oracle request generator for the scheduling stream (W3).

COM to Project hangs in the agents' confined shell (coordination.md rev 3), so
this script does NOT drive Project.  It prints exact recipes the coordinator can
run, plus the fields to read back.  Paste a case into the coordinator's oracle
session (which uses scripts/oracle_lock.py).

    python scripts/sched_oracle_recipes.py            # all recipes, markdown
    python scripts/sched_oracle_recipes.py W3-O2      # one recipe
    python scripts/sched_oracle_recipes.py --json     # machine-readable list

Every recipe assumes: a new blank project, project start 2026-10-19 (Monday),
standard calendar (Mon-Fri 08:00-12:00, 13:00-17:00 unless stated), and
`Application.Calculate` before reading.
"""
import argparse
import json

READ_TASK = ["ID", "Name", "Duration", "DurationText", "Work", "Start", "Finish",
             "EarlyStart", "EarlyFinish", "LateStart", "LateFinish",
             "StartSlack", "FinishSlack", "FreeSlack", "TotalSlack", "Critical",
             "ConstraintType", "ConstraintDate", "Deadline", "Type",
             "EffortDriven", "Milestone"]
READ_RAW = ["GetField(188743709)",   # Duration raw (i32, tenths of a minute)
            "GetField(188743716)",   # Finish raw (date32)
            "GetField(188743715)",   # Start raw
            "GetField(188743717)",   # EarlyStart
            "GetField(188743718)",   # EarlyFinish
            "GetField(188743719)",   # LateStart
            "GetField(188743720)"]   # LateFinish

RECIPES = [
    {
        "id": "W3-O1",
        "title": "Finish instant, weekend skip, duration scale",
        "decides": "spec 1.4 (AddWork), 0.2 (tenths), 2.2 (rounding)",
        "setup": [
            "FileNew; Project.StartDate = '2026-10-19' (Mon)",
            "T1: Duration='1d', Start='2026-10-19 08:00'",
            "T2: Duration='5d', Start='2026-10-20 08:00' (Tue)",
            "T3: Duration='4h', Start='2026-10-23 15:00' (Fri)",
            "T4: Duration='0d' (milestone), Start='2026-10-21 08:00'",
            "Calculate",
        ],
        "read": {"T1..T4": READ_TASK, "raw": READ_RAW},
        "expect_from_spec": {
            "T1.Finish": "2026-10-19 17:00",
            "T2.Finish": "2026-10-26 17:00 (Mon; weekend skipped)",
            "T3.Finish": "2026-10-26 10:00 (Mon)",
            "T1 raw duration": "4800",
            "T1 raw finish": "(15633<<16)|(1020*10), i.e. 0x3D1127D8",
        },
        "why": "Pins the half-open interval rule, the 17:00 finish instant and "
               "the tenth-of-a-minute storage scale in one run.",
    },
    {
        "id": "W3-O2",
        "title": "Duration / work / units triangle and effort-driven",
        "decides": "spec 2.3, 0.3 (units scale)",
        "setup": [
            "Create resource R1, R2 (max units 100%).",
            "T1: assign R1; Type=0 (fixed units); Duration='3d'; read;",
            "     Work='4h'; read; Units='50%'; read.",
            "Repeat T1 for Type=1 (fixed duration) and Type=2 (fixed work).",
            "T2: assign R1 and R2; EffortDriven=False; Duration='3d'; Work='4h'.",
            "T3: same but EffortDriven=True.",
            "After each edit: Calculate; read Duration/Work/Units/DurationText.",
        ],
        "read": {"per edit": ["Duration", "Work", "Units", "DurationText"],
                 "assignment": ["TaskID", "ResourceID", "Units", "Work"]},
        "expect_from_spec": {
            "fixed units, Work='4h'": "Duration becomes 8h (1 day at 50%?) "
                                       "-- record, do not assume",
            "fixed duration, Work='4h'": "Units becomes Work/Duration",
            "fixed work, Duration='3d'": "Units becomes Work/Duration",
        },
        "why": "The nine cells plus two assignments decide everything in 2.3 and "
               "reveal the internal units fixed-point scale from the record dump.",
        "extra": "If possible, dump the assignment record bytes before/after "
                 "Units changes (scripts/phase2d_assignment_verify.py shows how).",
    },
    {
        "id": "W3-O3",
        "title": "Link types, lag anchor and lead",
        "decides": "spec 4.2, 4.1 (enum order)",
        "setup": [
            "T1: Duration='1d', Start='2026-10-19 08:00'.",
            "T2: Duration='1d', no constraint.",
            "For type in {FS=1, SS=3, FF=0, SF=2}:",
            "  link T1->T2 with Lag=0; Calculate; read T1/T2 Start/Finish.",
            "  change Lag='+1d'; Calculate; read.",
            "  change Lag='-1d'; Calculate; read.",
            "  change Lag='+2h'; Calculate; read.",
        ],
        "read": {"T1,T2": READ_TASK},
        "expect_from_spec": {
            "FS lag 0": "T2.Start 2026-10-20 08:00",
            "FS +1d": "T2.Start 2026-10-21 08:00",
            "FS -1d": "T2.Start 2026-10-19 08:00",
            "SS +2h": "T2.Start 2026-10-19 10:00",
        },
        "why": "Settles the lag anchor (next working instant vs predecessor "
               "finish) and confirms 0=FF,1=FS,2=SF,3=SS.",
    },
    {
        "id": "W3-O4",
        "title": "Constraints, deadlines, negative slack",
        "decides": "spec 3.3-3.6, 3.5",
        "setup": [
            "T1: Duration='1d' (Mon). T2: Duration='1d', FS from T1.",
            "For c in 0..7: T2.ConstraintType=c; T2.ConstraintDate='2026-10-26 08:00';",
            "  Calculate; read T1/T2 every field.",
            "Repeat with T1 Duration='10d' so the constraint is infeasible.",
            "Add T2.Deadline='2026-10-27 17:00' and '2026-10-20 17:00'; read slack.",
            "Set project CriticalSlackLimit=2 (days); read T2.Critical.",
        ],
        "read": {"T1,T2": READ_TASK},
        "expect_from_spec": {
            "SNET 10/26": "T2.Start >= 10/26 08:00",
            "MSO 10/26": "T2.Start == 10/26 08:00",
            "SNLT/FNLT infeasible": "early dates unchanged, TotalSlack negative",
            "deadline earlier than finish": "TotalSlack negative, task not moved",
        },
        "why": "This is the one run that turns the constraint table from "
               "inference into fact, including whether upper-bound constraints "
               "move the early dates.",
    },
    {
        "id": "W3-O5",
        "title": "Elapsed durations, options, task calendar",
        "decides": "spec 2.2, 1.5, 1.6",
        "setup": [
            "T1: Duration='1ed', Start='2026-10-23 08:00' (Fri).",
            "T2: Duration='1ew', Start='2026-10-19 08:00'.",
            "T3: Duration='1emon', Start='2026-10-01 08:00'.",
            "Set Application.OptionsCalendar HoursPerDay=10;",
            "T4: Duration='1d', Start='2026-10-19 08:00'.",
            "Create calendar 'FourDay' with Mon-Thu 08:00-17:00, Fri nonworking;",
            "assign to T5 (Duration='4d', Start Mon); read.",
        ],
        "read": {"T1..T5": READ_TASK},
        "expect_from_spec": {
            "1ed from Fri 08:00": "Sat 08:00 (1440 min, ignores weekend)",
            "1ew": "7 calendar days = 10080 min",
            "1emon": "30 days = 43200 min (to confirm)",
            "HoursPerDay=10, 1d": "600 min stored",
            "FourDay 4d from Mon": "finish Thu 17:00",
        },
        "why": "Fixes the elapsed constants and proves hours/day is a "
               "conversion setting, not the calendar's working time.",
    },
    {
        "id": "W3-O6",
        "title": "Summary rollup with a gap",
        "decides": "spec 3.7",
        "setup": [
            "S1 summary; C1 2d starting 10/19, C2 1d starting 10/26 (after a gap).",
            "Calculate; read S1 Duration/Work/Start/Finish/Critical/TotalSlack.",
            "Make C1 critical by adding a successor that starts at its finish.",
            "Read S1.Critical again. Outdent all children and read S1 (blank summary).",
        ],
        "read": {"S1,C1,C2": READ_TASK},
        "expect_from_spec": {
            "S1.Duration": "working span 10/19 08:00 -> 10/26 17:00 (6 days)",
            "S1.Work": "3 days",
            "S1 after children outdented": "blank, no dates",
        },
        "why": "Confirms the rollup formulas and how blank summaries behave.",
    },
    {
        "id": "W3-O7",
        "title": "Free slack with two successors",
        "decides": "spec 3.5",
        "setup": [
            "A 1d; B 1d FS from A; C 1d FF+2d from A (starts two days later).",
            "Calculate; read A.FreeSlack, A.TotalSlack, B/C dates.",
            "Make C start earlier than B; read A.FreeSlack again.",
            "Create a negative-slip case: B SS-1d from A; read A.FreeSlack.",
        ],
        "read": {"A,B,C": READ_TASK},
        "expect_from_spec": {"A.FreeSlack": "difference to the nearer successor",
                             "negative case": "record sign, do not clamp blindly"},
        "why": "FreeSlack is stored; the sign/clamp and the per-link formula are "
               "still inference.",
    },
    {
        "id": "W3-O8",
        "title": "Recalc idempotence and pass order",
        "decides": "spec 5",
        "setup": [
            "Build a 10-task chain with mixed FS/SS, one SNET, one summary.",
            "Calculate; snapshot every date/slack field; Calculate again;",
            "compare snapshots byte-for-byte.",
            "Toggle Project.ScheduleFromStart=False and Calculate; snapshot.",
        ],
        "read": {"all tasks": READ_TASK, "raw": READ_RAW},
        "expect_from_spec": {"second Calculate": "identical snapshot",
                             "from-finish": "pass order flips, dates may change"},
        "why": "Cheap regression net for convergence and for the "
               "schedule-from-finish branch.",
    },
]


def render(recipe):
    out = [f"## {recipe['id']} — {recipe['title']}", "",
           f"**Decides:** {recipe['decides']}", "", "**Setup**", ""]
    out += [f"{i+1}. {step}" for i, step in enumerate(recipe["setup"])]
    out += ["", "**Read back**", ""]
    for group, fields in recipe["read"].items():
        out.append(f"- {group}: " + ", ".join(fields))
    out += ["", "**Expected from spec (falsify these)**", ""]
    for k, v in recipe.get("expect_from_spec", {}).items():
        out.append(f"- {k}: {v}")
    if recipe.get("extra"):
        out += ["", f"**Extra:** {recipe['extra']}"]
    out += ["", f"**Why:** {recipe['why']}", ""]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case", nargs="?")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    if args.json:
        print(json.dumps(RECIPES, indent=2))
        return
    cases = [r for r in RECIPES if not args.case or r["id"] == args.case]
    if not cases:
        raise SystemExit("unknown case; choose " + ", ".join(r["id"] for r in RECIPES))
    print("# W3 oracle recipes")
    print("\nRun in the coordinator's oracle session only (COM hangs in agent shells).")
    print("Read every field before and after `Calculate`; save one JSON per case.\n")
    for r in cases:
        print(render(r))


if __name__ == "__main__":
    main()
