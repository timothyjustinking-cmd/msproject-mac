#!/usr/bin/env python3
"""Wave-28 W28-0.1 privacy re-save: rebuild the leaking fixtures neutrally.

    python scripts/oracle_w28_privacy.py

Wave 27's review (finding 1) found seven committed .mpp fixtures carrying the
absolute save path of the working tree (UTF-16LE, in `Props14` and
`214/CEdl/Var2Data`), which names the operator and the company Dropbox folder.
The six wave-23/27 fixtures are re-derived here EXACTLY as their wave-23/27
recipes did it, but every save goes to `C:/w28/` (neutral, no person and no
company folder), and the committed copies are made from those neutral saves:

  * wave27-percent-actuals.mpp -- synthetic-base, Task ID 1 set to 50 %
    PercentComplete (the W27-0.1 measurement).
  * wave27-priority-control.mpp / wave27-priority-750.mpp -- one session's
    control and Project Summary Task Priority = 750 saves (W27-0.3).
  * wave27-unlink-control.mpp / wave27-unlink.mpp -- one session's control and
    EditGoTo(4) + UnlinkTasks saves of wave23-outline.mpp (W27-3.3).
  * wave23-flags.mpp -- wave23-outline.mpp with Task ID 3 Flag10 = True
    (W23-6/W24-15's single-bit control).

`tests/data/synthetic-calendars.mpp` is NOT rebuilt: the engine suite's
calendar oracle and its pinned hashes depend on that exact file, so the brief
allows its two path strings to be replaced in place with neutral strings of
exactly the same length (scripts/w28_neutralise_path.py records the hashes).

The saves are copied into tests/data only after this script (and the caller)
checks every produced file for a non-neutral drive path.  facts go to
build/w28/privacy-facts.json.

usage: python scripts/oracle_w28_privacy.py
"""

import datetime as dt
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard  # noqa: E402

NEUTRAL = "C:/w28"
OUT = os.path.join(ROOT, "build", "w28")
os.makedirs(NEUTRAL, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

DATA = os.path.join(ROOT, "tests", "data")

TARGETS = {
    "percent_actuals": os.path.join(NEUTRAL, "wave27-percent-actuals.mpp"),
    "priority_control": os.path.join(NEUTRAL, "wave27-priority-control.mpp"),
    "priority_750": os.path.join(NEUTRAL, "wave27-priority-750.mpp"),
    "unlink_control": os.path.join(NEUTRAL, "wave27-unlink-control.mpp"),
    "unlink": os.path.join(NEUTRAL, "wave27-unlink.mpp"),
    "flags": os.path.join(NEUTRAL, "wave23-flags.mpp"),
}

BASE_ACTUALS = os.path.join(NEUTRAL, "base-actuals.mpp")
BASE_PRIORITY = os.path.join(NEUTRAL, "base-priority.mpp")
BASE_UNLINK = os.path.join(NEUTRAL, "base-unlink.mpp")
BASE_FLAGS = os.path.join(NEUTRAL, "base-flags.mpp")


def prop(obj, name, default=None):
    try:
        return getattr(obj, name)
    except Exception as exc:  # noqa: BLE001
        return f"<unreadable: {exc}>"


def val(v):
    if isinstance(v, dt.datetime):
        return v.strftime("%Y-%m-%d %H:%M:%S")
    if isinstance(v, (int, float, bool, str)) or v is None:
        return v
    return repr(v)


def task_facts(project):
    rows = []
    for task in project.Tasks:
        if task is None:
            continue
        rows.append(
            {
                "id": val(prop(task, "ID")),
                "uid": val(prop(task, "UniqueID")),
                "percent_complete": val(prop(task, "PercentComplete")),
                "actual_duration": val(prop(task, "ActualDuration")),
                "remaining_duration": val(prop(task, "RemainingDuration")),
                "duration": val(prop(task, "Duration")),
                "actual_start": val(prop(task, "ActualStart")),
                "actual_finish": val(prop(task, "ActualFinish")),
                "start": val(prop(task, "Start")),
                "finish": val(prop(task, "Finish")),
                "flag10": val(prop(task, "Flag10")),
            }
        )
    return rows


def neutral_path_hits(path):
    """Every drive path outside C:\\wNN and every user-profile path.

    A drive letter glued to a preceding letter is not a path: the stock
    `gbui://mainpage.htm` record contains "i:/" and is not a drive path.  Scan
    each stream decoded as UTF-16LE at both byte alignments, and as raw bytes.
    """
    import olefile

    drive = re.compile(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/]")
    profile_text = (
        "\x5cUsers\x5c",
        "\x2fUsers\x2f",
        "\x2fhome\x2f",
        "\x5c\x44ocuments and Settings\x5c",
    )
    profile = re.compile("|".join(re.escape(item)
                                  for item in profile_text))
    hits = []
    o = olefile.OleFileIO(path)
    for entry in o.listdir(streams=True, storages=False):
        data = o.openstream(entry).read()
        label = "/".join(entry)
        for align in (0, 1):
            text = data[align:].decode("utf-16le", "replace")
            for regex, kind in ((drive, "drive"), (profile, "profile")):
                for match in regex.finditer(text):
                    at = match.start()
                    a = text.rfind("\x00", 0, at) + 1
                    j = text.find("\x00", at)
                    if j < 0:
                        j = len(text)
                    value = text[a:j]
                    if kind == "profile" or not re.match(
                            r"^[Cc]:[\\/]w\d\d[\\/]", value):
                        hits.append((label, kind, "utf16", align + at * 2, value))
        drive8 = re.compile(rb"(?<![A-Za-z0-9])[A-Za-z]:[\\/]")
        profile8 = re.compile(
            rb"\x5cUsers\x5c|\x2fUsers\x2f|\x2fhome\x2f|"
            rb"\x5c\x44ocuments and Settings\x5c")
        for regex, kind in ((drive8, "drive"), (profile8, "profile")):
            for match in regex.finditer(data):
                at = match.start()
                a = at
                while a > 0 and 0x20 <= data[a - 1] < 0x7F:
                    a -= 1
                j = at
                while j < len(data) and 0x20 <= data[j] < 0x7F:
                    j += 1
                value = data[a:j].decode("ascii", "replace")
                if kind == "profile" or not re.match(
                        r"^[Cc]:[\\/]w\d\d[\\/]", value):
                    hits.append((label, kind, "ascii", at, value))
    o.close()
    return hits


def main():
    for source, copy in (
            (os.path.join(DATA, "synthetic-base.mpp"), BASE_ACTUALS),
            (os.path.join(DATA, "synthetic-base.mpp"), BASE_PRIORITY),
            (os.path.join(DATA, "wave23-outline.mpp"), BASE_UNLINK),
            (os.path.join(DATA, "wave23-outline.mpp"), BASE_FLAGS)):
        shutil.copyfile(source, copy)

    facts = {"version": None, "actuals": {}, "priority": {}, "unlink": {},
             "flags": {}, "paths": {}}
    with oracle_session() as app:
        with Guard(600):
            facts["version"] = prop(app, "Version")
            app.UserName = "Fixture Builder"

            # ---- wave27-percent-actuals: Project's own 50 % edit -----------
            app.FileOpen(BASE_ACTUALS)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Actuals Fixture")
            project.Tasks(1).PercentComplete = 50
            facts["actuals"]["after_edit"] = task_facts(project)
            app.FileSaveAs(TARGETS["percent_actuals"])
            app.FileClose(0)
            app.FileOpen(TARGETS["percent_actuals"])
            facts["actuals"]["reopen"] = task_facts(app.ActiveProject)
            app.FileClose(0)

            # ---- wave27-priority pair: one session, control then 750 ------
            app.FileOpen(BASE_PRIORITY)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Priority Fixture")
            facts["priority"]["control_before_edit"] = prop(
                project.ProjectSummaryTask, "Priority")
            app.FileSaveAs(TARGETS["priority_control"])
            project.ProjectSummaryTask.Priority = 750
            facts["priority"]["control_after_edit"] = prop(
                project.ProjectSummaryTask, "Priority")
            app.FileSaveAs(TARGETS["priority_750"])
            app.FileClose(0)
            for label in ("priority_control", "priority_750"):
                app.FileOpen(TARGETS[label])
                facts["priority"][f"reopen_{label}"] = prop(
                    app.ActiveProject.ProjectSummaryTask, "Priority")
                app.FileClose(0)

            # ---- wave27-unlink pair: one session, control then Unlink ------
            app.FileOpen(BASE_UNLINK)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Unlink Fixture")
            app.FileSaveAs(TARGETS["unlink_control"])
            app.EditGoTo(4)
            app.UnlinkTasks()
            app.FileSaveAs(TARGETS["unlink"])
            app.FileClose(0)
            app.FileOpen(TARGETS["unlink"])
            facts["unlink"]["reopen"] = [
                (prop(dep.From, "UniqueID"), prop(dep.To, "UniqueID"))
                for task in app.ActiveProject.Tasks
                for dep in task.TaskDependencies]
            app.FileClose(0)

            # ---- wave23-flags: Flag10 True on ID 3 -------------------------
            app.FileOpen(BASE_FLAGS)
            project = app.ActiveProject
            neutralise(app, project, "Synthetic Flags Fixture")
            project.Tasks(3).Flag10 = True
            facts["flags"]["after_edit"] = task_facts(project)
            app.FileSaveAs(TARGETS["flags"])
            app.FileClose(0)
            app.FileOpen(TARGETS["flags"])
            facts["flags"]["reopen"] = task_facts(app.ActiveProject)
            app.FileClose(0)

    for key, path in TARGETS.items():
        hits = neutral_path_hits(path)
        facts["paths"][key] = hits
        log(f"{path}: {'CLEAN' if not hits else hits}")
        if hits:
            raise SystemExit(f"{path} still carries a non-neutral path: {hits}")

    with open(os.path.join(OUT, "privacy-facts.json"), "w", encoding="utf-8") as handle:
        json.dump(facts, handle, indent=2, sort_keys=True)
    log("wrote build/w28/privacy-facts.json")
    for key, path in TARGETS.items():
        log(f"fixture {key} {path} ({os.path.getsize(path)} bytes)")


if __name__ == "__main__":
    main()
