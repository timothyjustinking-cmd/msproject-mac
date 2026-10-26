#!/usr/bin/env python3
"""Wave 31 (G2, W31-2.2): Project's Entire Project / Selected Tasks windows.

The port's View tab fits the chart's timescale (W31-2.1).  This script measures
what Project 2021 does for the same two commands on the two committed fixtures,
so the comparison test (app_qt/tests/zoom_commands_test.cpp) and the reference
tests/data/reference/w31-zoom.measures.json rest on a re-runnable measurement:

  1. copy each fixture under the neutral root C:/w31/zoom;
  2. open it, resize the window to 1500 x 800 pt (COM reports width 1452) and
     record the pre-command TimescaleStart/TimescaleFinish;
  3. select task row 2 and run Selected Tasks (`ZoomSelectedTasks`), recording
     the window;
  4. run View > Zoom > Entire Project (`ZoomEntireProject`) and record it.

Why that order and a single row.  Project 2021's `ZoomSelectedTasks` command
raises a COM exception (E_FAIL) when a zoom command has already run in the
session, and on synthetic-base it also raises for a two-row selection; a
single-row selection works first and is the only shape reproducible across both
fixtures.  Row 2 is a leaf on each fixture whose span is much shorter than the
project's (1.958 vs 4.958 days on synthetic-base, 1.375 vs 7.375 on
wave23-outline), so Selected Tasks cannot be confused with Entire Project.
Each fixture is measured twice in fresh Project sessions; the committed values
are the ones both runs agree on.

The chart-pane width the fit is a percentage of is not a COM property; the
committed measures file records the 1020 px measured from the same window's
clean capture.  The script writes only under C:/w31 and build/w31; the
coordinator copies the numbers into the reference file by hand.

    python -I scripts/oracle_w31_zoom.py
"""

import datetime as dt
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = r"C:/w31/zoom"
OUT = os.path.join(ROOT, "build", "w31")
RESULT = os.path.join(OUT, "W31-2.2-zoom-oracle.json")
FIXTURES = [
    ("base", os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")),
    ("outline", os.path.join(ROOT, "tests", "data", "wave23-outline.mpp")),
]


def val(value):
    if isinstance(value, dt.datetime):
        return value.strftime("%Y-%m-%d %H:%M:%S")
    return value


def prop(obj, name):
    try:
        return val(getattr(obj, name))
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def facts(app, label):
    project = app.ActiveProject
    return {
        "label": label,
        "project_start": prop(project, "ProjectStart"),
        "project_finish": prop(project, "ProjectFinish"),
        "timescale_start": prop(app, "TimescaleStart"),
        "timescale_finish": prop(app, "TimescaleFinish"),
        "window_width_pt": prop(app.ActiveWindow, "Width"),
        "window_height_pt": prop(app.ActiveWindow, "Height"),
    }


def task_facts(app):
    tasks = []
    for task in app.ActiveProject.Tasks:
        if task is None:
            continue
        tasks.append({
            "id": task.ID,
            "start": prop(task, "Start"),
            "finish": prop(task, "Finish"),
        })
    return tasks


def selection_facts(app):
    """The tasks Project says are selected after SelectRow(2)."""
    tasks = []
    try:
        for task in app.ActiveSelection.Tasks:
            if task is None:
                continue
            tasks.append({
                "id": task.ID,
                "outline_number": prop(task, "OutlineNumber"),
                "summary": prop(task, "Summary"),
                "start": prop(task, "Start"),
                "finish": prop(task, "Finish"),
            })
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"
    return tasks


def main():
    os.makedirs(NEUTRAL, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    result = {"version": None, "neutral_root": NEUTRAL, "cases": []}
    for tag, source in FIXTURES:
        work = os.path.join(NEUTRAL, f"{tag}.mpp")
        if os.path.exists(work):
            os.remove(work)
        shutil.copyfile(source, work)
        entry = {"fixture": os.path.basename(source), "tag": tag}
        with oracle_session() as app:
            with Guard(600):
                result["version"] = str(app.Version)
                app.FileOpen(work)
                time.sleep(2.0)
                app.ActiveWindow.Width = 1500
                app.ActiveWindow.Height = 800
                time.sleep(1.0)
                entry["open"] = facts(app, "open")
                entry["tasks"] = task_facts(app)
                entry["selected_row"] = 2

                # Selected Tasks FIRST, one row: see the module doc block for
                # the COM exception that makes any other order/shape
                # unreproducible.
                app.SelectRow(2)
                time.sleep(1.5)
                entry["selection"] = selection_facts(app)
                app.CommandBars.ExecuteMso("ZoomSelectedTasks")
                time.sleep(2.0)
                entry["selected"] = facts(app, "selected")

                app.CommandBars.ExecuteMso("ZoomEntireProject")
                time.sleep(2.0)
                entry["entire"] = facts(app, "entire")
                app.FileClose(0)
        result["cases"].append(entry)
        log(f"{tag}: selected {entry['selected']['timescale_start']} .. "
            f"{entry['selected']['timescale_finish']}; entire "
            f"{entry['entire']['timescale_start']} .. "
            f"{entry['entire']['timescale_finish']}")

    with open(RESULT, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    print("wrote", os.path.relpath(RESULT, ROOT))


if __name__ == "__main__":
    main()
