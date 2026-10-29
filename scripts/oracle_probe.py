"""Build known-good .mpp fixtures by driving MS Project (the oracle) over COM.

Purpose
-------
Settle what reading hexdumps cannot: the FixedData row stride, the FixedMeta
layout, and which table a field lands in. Build a project whose contents are
known exactly, save it, change one value, save again, then diff the two files
stream by stream. With a known record count, FixedData size / count gives the
row stride directly.

Notes on Project automation
---------------------------
- Project's CLSID has no `/automation` switch, unlike Excel and Word, and
  CoCreateInstance fails with 0x80080005 unless the app is already running.
  So: start WINPROJ.EXE with `/automation` first, then dispatch.
- `Application.Version` does not exist in Project's object model.

Safety
------
- Writes only under corpus/probe/ and never touches corpus/*.mpp.
- Only stops a WINPROJ process that this script started, and only if it is
  still alive after a polite Quit.

Usage
-----
    python scripts/oracle_probe.py
"""

import argparse
import os
import subprocess
import sys
import time
from datetime import datetime

import olefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from oracle_privacy import neutralise  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "corpus", "probe")
EXE = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
PROGID = "MSProject.Application"

# What the fixture contains, so the diff can be read against known counts.
EXPECTED = {"TBkndTask": 3, "TBkndRsc": 1, "TBkndAssn": 1, "TBkndCons": 2}


def log(msg):
    print(msg, flush=True)


def project_pids():
    try:
        out = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq WINPROJ.EXE", "/FO", "CSV", "/NH"],
            capture_output=True, text=True, timeout=20).stdout
        pids = []
        for line in out.splitlines():
            parts = [p.strip('"') for p in line.split('","')]
            if len(parts) > 1 and parts[0].lower() == "winproj.exe":
                pids.append(int(parts[1]))
        return pids
    except Exception:                                  # noqa: BLE001
        return []


def ensure_app(started):
    """Return a dispatched Application, starting Project if needed."""
    import win32com.client

    if not project_pids():
        if not os.path.exists(EXE):
            sys.exit(f"WINPROJ.EXE not found at {EXE}")
        log("  starting Project (no arguments)")
        subprocess.Popen([EXE])
        started.add("launched")
        for _ in range(30):
            time.sleep(1)
            if project_pids():
                break
        time.sleep(3)                                  # let it register in the ROT

    last = None
    for _ in range(20):
        try:
            app = win32com.client.Dispatch(PROGID)
            _ = app.Projects
            return app
        except Exception as exc:                       # noqa: BLE001
            last = exc
            time.sleep(4)
    sys.exit(f"COM dispatch failed after retries: {last}")


def build(app, path):
    app.FileNew()
    for _ in range(20):
        if app.ActiveProject is not None:
            break
        time.sleep(1)
    proj = app.ActiveProject

    try:
        proj.ProjectStart = "2026-01-05"
        log(f"  project start = {proj.ProjectStart}")
    except Exception as exc:                           # noqa: BLE001
        log(f"  ! ProjectStart not set: {exc}")

    tasks = []
    for name, dur in (("Task A", "5d"), ("Task B", "3d"), ("Task C", "2d")):
        t = proj.Tasks.Add(name)
        t.Duration = dur
        tasks.append(t)
        log(f"  task {t.ID}: {name} dur={dur} start={t.Start} finish={t.Finish}")

    for a, b in ((tasks[0], tasks[1]), (tasks[1], tasks[2])):
        try:
            proj.TaskDependencies.Add(a, b)
            log(f"  link {a.ID} -> {b.ID}")
        except Exception as exc:                       # noqa: BLE001
            log(f"  ! link failed: {exc}")

    try:
        res = proj.Resources.Add("Crew")
        proj.Assignments.Add(TaskID=tasks[0].ID, ResourceID=res.ID)
        log(f"  resource {res.ID}: Crew -> task {tasks[0].ID}")
    except Exception as exc:                           # noqa: BLE001
        log(f"  ! resource/assignment failed: {exc}")

    neutralise(app, proj, "Synthetic Probe Fixture")
    app.FileSaveAs(Name=path)
    log(f"  saved v1 ({os.path.getsize(path):,} bytes)")
    return proj


def mutate(app, proj, path):
    t = proj.Tasks(1)
    before = t.Duration
    t.Duration = "7d"
    log(f"  task 1 duration {before} -> {t.Duration}   (finish now {t.Finish})")
    try:
        proj.Tasks(2).Priority = 700
        log("  task 2 Priority -> 700")
    except Exception as exc:                           # noqa: BLE001
        log(f"  ! priority not set: {exc}")
    neutralise(app, proj, "Synthetic Probe Fixture")
    app.FileSaveAs(Name=path)
    log(f"  saved v2 ({os.path.getsize(path):,} bytes)")


def read_streams(path):
    ole = olefile.OleFileIO(path)
    try:
        return {"/".join(p): ole.openstream(p).read()
                for p in ole.listdir(streams=True, storages=False)}
    finally:
        ole.close()


def report_strides(streams):
    log("\nrow stride from known record counts (FixedData size / records):")
    for table, n in EXPECTED.items():
        name = f"114/{table}/FixedData"
        if name in streams and n:
            size = len(streams[name])
            log(f"  {table:12} {size:8,} B / {n} = {size / n:8.2f} B per record"
                f"   (remainder {size % n})")


def diff(a, b):
    ka, kb = set(a), set(b)
    log(f"\nstreams v1={len(a)} v2={len(b)}  only-v1={len(ka - kb)} only-v2={len(kb - ka)}")
    if ka - kb:
        log(f"  removed: {sorted(ka - kb)}")
    if kb - ka:
        log(f"  added  : {sorted(kb - ka)}")

    rows = []
    for name in sorted(ka & kb):
        x, y = a[name], b[name]
        n = min(len(x), len(y))
        offs = [i for i in range(n) if x[i] != y[i]]
        if offs or len(x) != len(y):
            rows.append((name, len(x), len(y), len(offs), offs))
    rows.sort(key=lambda r: -r[3])

    log(f"\n{'stream':46} {'v1':>7} {'v2':>7} {'diff':>5}  first changed offsets")
    for name, s1, s2, nd, offs in rows:
        log(f"{name:46} {s1:7,} {s2:7,} {nd:5}  {','.join(str(o) for o in offs[:6])}")

    for name, s1, s2, nd, offs in rows:
        if "FixedData" in name and offs:
            deltas = [y - x for x, y in zip(offs, offs[1:])]
            counts = {}
            for d in deltas:
                counts[d] = counts.get(d, 0) + 1
            top = sorted(counts.items(), key=lambda kv: -kv[1])[:3]
            log(f"\n{name}: {nd} bytes differ at {len(offs)} offsets")
            log(f"  most common gaps between changed bytes: {top}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--allow-running", action="store_true",
                    help="attach even if WINPROJ is already open")
    args = ap.parse_args()

    try:
        import win32com.client  # noqa: F401
    except ImportError:
        sys.exit("pywin32 is not installed: python -m pip install pywin32")

    existing = project_pids()
    if existing and not args.allow_running:
        sys.exit(f"WINPROJ already running (pids {existing}). Close it first, or "
                 f"pass --allow-running if you started it for this purpose.")

    os.makedirs(OUT, exist_ok=True)
    v1 = os.path.join(OUT, "probe-v1.mpp")
    v2 = os.path.join(OUT, "probe-v2.mpp")
    for p in (v1, v2):
        if os.path.exists(p):
            os.remove(p)

    started = set()
    t0 = datetime.now()
    log(f"oracle probe {t0:%H:%M:%S}")
    app = ensure_app(started)
    try:
        try:
            app.Visible = False
        except Exception:                              # noqa: BLE001
            pass
        try:
            app.DisplayAlerts = False
        except Exception:                              # noqa: BLE001
            pass

        proj = build(app, v1)
        mutate(app, proj, v2)
    finally:
        try:
            app.FileClose()
        except Exception:                              # noqa: BLE001
            pass
        try:
            app.Quit()
        except Exception:                              # noqa: BLE001
            pass
        time.sleep(2)
        if started:
            for pid in project_pids():
                try:
                    subprocess.run(["taskkill", "/PID", str(pid), "/F"],
                                   capture_output=True, timeout=20)
                    log(f"  stopped automation instance pid {pid}")
                except Exception:                      # noqa: BLE001
                    log(f"  ! could not stop pid {pid}")
    log(f"elapsed {(datetime.now() - t0).total_seconds():.1f}s")

    if not (os.path.exists(v1) and os.path.exists(v2)):
        sys.exit("fixtures were not written")

    streams1, streams2 = read_streams(v1), read_streams(v2)
    log(f"\nv1 {os.path.getsize(v1):,} B   v2 {os.path.getsize(v2):,} B")
    report_strides(streams1)
    diff(streams1, streams2)


if __name__ == "__main__":
    main()
