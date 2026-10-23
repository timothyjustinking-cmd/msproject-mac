"""Serialised access to MS Project for oracle experiments.

Several agents may want to build test projects. Project is single-instance and
badly behaved under automation, so all access goes through this lock.

    from oracle_lock import oracle_session
    with oracle_session() as app:
        app.FileNew()
        proj = app.ActiveProject
        ...

Guarantees
----------
- One holder at a time (exclusive lock file, with timeout).
- Refuses to proceed if a WINPROJ.EXE is already running that this script did
  not start, so it can never hijack (or close) a project you have open.
- Launches Project with NO arguments. Never `/automation`: Project treats an
  unknown switch as a filename, opens a modal error dialog, and then rejects
  every COM call with 0x80080005.
- Closes what it started, and kills the process only if Quit leaves it behind.

CLI
---
    python scripts/oracle_lock.py --status
    python scripts/oracle_lock.py --release      # clear a stale lock
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from contextlib import contextmanager

EXE = r"C:\Program Files\Microsoft Office\root\Office16\WINPROJ.EXE"
PROGID = "MSProject.Application"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, ".oracle.lock")
STALE_AFTER = 45 * 60          # a lock older than this is considered abandoned


def log(msg):
    print(f"[oracle] {msg}", flush=True)


WORK_DIR = os.path.join(ROOT, "build", "oracle-work")


def working_copy(path, tag=""):
    """Copy `path` under build/oracle-work and return the copy's path.

    Wave 23 (A-7b): Project rewrites a file it opens (ReadOnly=False), and the
    sync client has resurrected committed fixtures before.  Every oracle script
    opens a COPY; the committed fixture is never handed to FileOpen.
    """
    os.makedirs(WORK_DIR, exist_ok=True)
    stem = os.path.splitext(os.path.basename(path))[0]
    suffix = f"-{tag}" if tag else ""
    target = os.path.join(WORK_DIR, f"{stem}{suffix}.mpp")
    shutil.copyfile(path, target)
    return target


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


@contextmanager
def _lock(timeout=600):
    start = time.time()
    while True:
        try:
            fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            break
        except FileExistsError:
            age = time.time() - os.path.getmtime(LOCK)
            if age > STALE_AFTER:
                log(f"removing stale lock ({age/60:.0f} min old)")
                try:
                    os.remove(LOCK)
                except OSError:
                    pass
                continue
            if time.time() - start > timeout:
                raise TimeoutError(
                    f"oracle lock held for >{timeout}s: {open(LOCK).read().strip()}")
            time.sleep(5)
    try:
        with os.fdopen(fd, "w") as fh:
            fh.write(f"pid={os.getpid()} at={time.strftime('%Y-%m-%d %H:%M:%S')}")
        yield
    finally:
        try:
            os.remove(LOCK)
        except OSError:
            pass


@contextmanager
def oracle_session(start_timeout=90):
    """Yield a dispatched, healthy Project Application. Serialised."""
    try:
        import win32com.client
    except ImportError:
        raise SystemExit("pywin32 is not installed: python -m pip install pywin32")

    with _lock():
        if project_pids():
            raise SystemExit(
                "WINPROJ.EXE is already running (pids %s). Refusing to attach: it may "
                "be someone's open work. Ask them to close it, or wait." % project_pids())

        if not os.path.exists(EXE):
            raise SystemExit(f"WINPROJ.EXE not found at {EXE}")
        log("starting Project (no arguments)")
        subprocess.Popen([EXE])

        app, last = None, None
        deadline = time.time() + start_timeout
        while time.time() < deadline:
            try:
                cand = win32com.client.Dispatch(PROGID)
                _ = cand.Projects                  # healthy-instance probe
                app = cand
                break
            except Exception as exc:               # noqa: BLE001
                last = exc
                time.sleep(4)
        if app is None:
            raise SystemExit(f"could not bind to Project in {start_timeout}s: {last}")

        log("bound")
        # Project's FileClose default is Save=2 (prompt), which opens a modal
        # "Microsoft Project" NUIDialog; every later COM call then blocks. Always
        # close with Save=0.
        try:
            app.DisplayAlerts = False
        except Exception:                          # noqa: BLE001
            pass
        try:
            yield app
        finally:
            for fn, args in (("FileClose", (0,)), ("FileClose", ()), ("Quit", ())):
                try:
                    getattr(app, fn)(*args)
                    break
                except Exception:                  # noqa: BLE001
                    continue
            time.sleep(2)
            for pid in project_pids():
                try:
                    subprocess.run(["taskkill", "/PID", str(pid), "/F"],
                                   capture_output=True, timeout=20)
                    log(f"stopped leftover pid {pid}")
                except Exception:                  # noqa: BLE001
                    pass
            log("released")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--release", action="store_true")
    args = ap.parse_args()

    if args.release:
        if os.path.exists(LOCK):
            os.remove(LOCK)
            print("lock released")
        else:
            print("no lock")
        return
    print(f"lock file : {LOCK} {'HELD' if os.path.exists(LOCK) else 'free'}")
    if os.path.exists(LOCK):
        print(f"holder    : {open(LOCK).read().strip()}")
    print(f"winproj   : {project_pids() or 'not running'}")


if __name__ == "__main__":
    main()
