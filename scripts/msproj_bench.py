"""Capture MS Project itself as the UI benchmark.

MS Project is installed on this machine, so it — not the AppKit shell — is the
reference the Qt shell must match.  This opens a fixture under the oracle lock,
puts it in the Gantt view with the Entry table, and writes:

    corpus/bench/msproject-gantt.png   the window's client area
    corpus/bench/msproject-gantt.json  geometry read from the UI, plus the
                                       CTable column widths Project stored

The PNG shows a third-party application's UI and the JSON carries numbers only,
so both stay in the git-ignored corpus tree; never commit them.

    python scripts/msproj_bench.py [fixture.mpp]
"""

import json
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from oracle_lock import oracle_session, working_copy
from oracle_w3 import Guard, calc, val

BENCH = os.path.join(ROOT, "corpus", "bench")
FIXTURE = os.path.join(ROOT, "tests", "data", "synthetic-base.mpp")


def project_window():
    import win32gui

    found = []

    def visit(hwnd, _):
        title = win32gui.GetWindowText(hwnd)
        if win32gui.IsWindowVisible(hwnd) and "Project" in title:
            found.append((hwnd, title))

    win32gui.EnumWindows(visit, None)
    return found[0] if found else (None, None)


def capture(path):
    import win32gui
    from PIL import ImageGrab

    hwnd, title = project_window()
    if hwnd is None:
        return {"error": "no Project window"}
    left, top, right, bottom = win32gui.GetClientRect(hwnd)
    origin = win32gui.ClientToScreen(hwnd, (0, 0))
    box = (origin[0], origin[1], origin[0] + right, origin[1] + bottom)
    image = ImageGrab.grab(bbox=box, all_screens=True)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.save(path)
    return {"window": title, "client": [right, bottom], "png": path}


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else FIXTURE
    # Wave 23 (A-7b): open a COPY under build/, never the committed fixture.
    fixture = working_copy(source, "msproj-bench")
    out = {"fixture": os.path.relpath(source, ROOT)}
    with oracle_session() as app:
        with Guard(300):
            app.FileOpen(Name=fixture, ReadOnly=False)
            proj = app.ActiveProject
            out["tasks"] = int(proj.Tasks.Count)
            for view in ("Gantt Chart", "Gantt Chart, Task"):
                try:
                    app.ViewApply(view)
                    out["view"] = view
                    break
                except Exception:                       # noqa: BLE001
                    continue
            try:
                app.TableApply("Entry")
                out["table"] = "Entry"
            except Exception as exc:                    # noqa: BLE001
                out["table_error"] = f"{exc.__class__.__name__}: {exc}"
            calc(app)
            time.sleep(2.0)
            out["screen"] = capture(os.path.join(BENCH, "msproject-gantt.png"))
            app.FileClose(0)
    with open(os.path.join(BENCH, "msproject-gantt.json"), "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=2)
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
