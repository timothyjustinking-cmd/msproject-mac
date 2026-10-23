#!/usr/bin/env python3
"""Wave 24 (W24-11): recapture tests/data/wave23-outline.mpp with the bars in
view, then regenerate the committed measures JSON.

The wave-23 reference capture had the chart scrolled to October while the tasks
are in December, so `colour.task_bar` was not found and the parity gate could
not compare a bar colour.  This opens a COPY of the fixture through the oracle
lock, applies the Gantt view, selects the first task and zooms the timescale to
the selection (Project's Scroll-to-Task effect), captures the CLIENT area
locally, and writes the parity report.  The PNG stays under build/ (git-ignored,
operator ruling 2026-10-23); only the numbers are committed.

    python scripts/capture_w24_reference.py
    python scripts/parity_report.py build/w24/wave23-outline-bars.png \
        --json build/w24/wave23-outline-bars.measures.json
"""

import json
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from oracle_lock import oracle_session, working_copy  # noqa: E402
from oracle_w3 import Guard, calc  # noqa: E402

FIXTURE = os.path.join(ROOT, "tests", "data", "wave23-outline.mpp")
OUT = os.path.join(ROOT, "build", "w24", "wave23-outline-bars.png")


def project_window():
    import win32gui

    found = []

    def visit(hwnd, _):
        title = win32gui.GetWindowText(hwnd)
        if win32gui.IsWindowVisible(hwnd) and "Project" in title:
            found.append(hwnd)

    win32gui.EnumWindows(visit, None)
    return found[0] if found else None


def capture(path):
    import win32gui
    from PIL import ImageGrab

    hwnd = project_window()
    if hwnd is None:
        raise RuntimeError("no Project window")
    left, top, right, bottom = win32gui.GetClientRect(hwnd)
    origin = win32gui.ClientToScreen(hwnd, (0, 0))
    box = (origin[0], origin[1], origin[0] + right, origin[1] + bottom)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    ImageGrab.grab(bbox=box, all_screens=True).save(path)
    return [right, bottom]


def main():
    fixture = working_copy(FIXTURE, "w24-reference")
    facts = {}
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=fixture, ReadOnly=False)
            project = app.ActiveProject
            # Do NOT ViewApply/TableApply/ZoomTimescale: the fixture's own
            # stored Gantt view and table are what wave 23 captured, and
            # applying the Entry table would replace the file's stored widths
            # (the last column absorbs the pane).  Only scroll.
            facts["view"] = str(getattr(project, "CurrentView", "<unreadable>"))
            calc(app)
            # Scroll to Task: select the first task and zoom the timescale to
            # the selection so December's bars are inside the chart pane.
            # Scroll to Task WITHOUT changing the zoom: pan the chart to the
            # first task's start date.  (ZoomTimescale(Selection) would zoom the
            # timescale to hours, changing the day width the parity gate
            # measures; the wave-23 reference is the weekly 24 px/day view.)
            app.SelectRow(1)
            try:
                app.EditGoTo(ID=1)
                facts["edit_goto"] = "ok"
            except Exception as exc:  # noqa: BLE001
                facts["edit_goto"] = str(exc)
            try:
                import datetime as dt
                start = project.Tasks(1).Start
                # Pan so the bars sit inside the pane, not clipped at its left
                # edge: 10 days of lead-in at the measured 24 px/day.
                pan_to = start - dt.timedelta(days=10)
                app.PanZoomPanTo(Start=pan_to)
                facts["pan_to"] = str(pan_to)
            except Exception as exc:  # noqa: BLE001
                facts["pan_to"] = str(exc)
            time.sleep(2.0)
            facts["client"] = capture(OUT)
            app.FileClose(0)
    facts["png"] = OUT
    print(json.dumps(facts, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
