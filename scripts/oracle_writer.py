#!/usr/bin/env python3
"""Writer §8 oracle: does MS Project accept the files core/mppwrite produced?

Coordinator-only. One subcommand per file, so a hang (modal repair dialog) kills
only that case.

    python scripts/oracle_writer.py patch    # inplace-patched.mpp
    python scripts/oracle_writer.py rebuild  # replace-fixeddata.mpp
    python scripts/oracle_writer.py name     # replace-name.mpp
    python scripts/oracle_writer.py base     # open base.mpp, Save As unchanged

Each run: copy the writer's output into corpus/probe/, open it, read every task
BEFORE and AFTER CalculateProject, Save As <name>-resaved.mpp, close.

`python scripts/oracle_writer.py diff` then compares every stream of a resaved
file against its source (this is what settles whether Project rewrites 214/).
"""

import argparse
import datetime as dt
import json
import os
import shutil
import struct
import sys
from datetime import timedelta

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_lock import oracle_session, log, project_pids  # noqa: E402
from oracle_privacy import neutralise  # noqa: E402
from oracle_w3 import Guard, calc, read_task, rows_of, save, val  # noqa: E402
import olefile  # noqa: E402

OUT = os.path.join(ROOT, "docs", "analysis", "oracle")
PROBE = os.path.join(ROOT, "corpus", "probe")
TEST_OUT = os.path.join(ROOT, "core", "mppwrite", "build", "vs", "test-out")

CASES = {
    "patch":   ("inplace-patched.mpp",  "oracle-patch.mpp"),
    "rebuild": ("replace-fixeddata.mpp", "oracle-rebuild.mpp"),
    "name":    ("replace-name.mpp",     "oracle-name.mpp"),
    "base":    ("../../base.mpp",       "resaved-base.mpp"),
}

EPOCH = dt.datetime(1983, 12, 31)


def date32(v):
    if not v or v == 0xFFFFFFFF:
        return None
    return EPOCH + timedelta(days=v >> 16, seconds=(v & 0xFFFF) * 6)


def stored_rows(path):
    """What the file itself says, independent of Project."""
    ole = olefile.OleFileIO(path)
    try:
        fixed = None
        for p in ole.listdir(streams=True, storages=False):
            if "/".join(x.strip() for x in p).endswith("TBkndTask/FixedData"):
                fixed = ole.openstream(p).read()
        names = {}
        for p in ole.listdir(streams=True, storages=False):
            if "/".join(x.strip() for x in p).endswith("TBkndTask/Var2Data"):
                names = ole.openstream(p).read()
    finally:
        ole.close()
    if fixed is None:
        return []
    rows = []
    for r in range((len(fixed) - 48) // 202):
        b = fixed[48 + r * 202:48 + (r + 1) * 202]
        row_id, uid = struct.unpack_from("<II", b, 0)
        rows.append({
            "row": r, "id": row_id, "uid": uid,
            "priority": struct.unpack_from("<H", b, 78)[0],
            "duration_ticks": struct.unpack_from("<I", b, 84)[0],
            "percent": struct.unpack_from("<H", b, 92)[0],
            "start_raw": f"0x{struct.unpack_from('<I', b, 96)[0]:08x}",
            "finish_raw": f"0x{struct.unpack_from('<I', b, 100)[0]:08x}",
            "start_file": str(date32(struct.unpack_from("<I", b, 96)[0])),
            "finish_file": str(date32(struct.unpack_from("<I", b, 100)[0])),
        })
    return rows




def window_texts(target_pid=None):
    """Read what Project is showing (dialogs are invisible to COM)."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    out = []
    CB = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def child_cb(hwnd, lp):
        n = user32.GetWindowTextLengthW(hwnd)
        if n:
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            cls = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(hwnd, cls, 128)
            out.append(f"      child {cls.value}: {buf.value}")
        return True

    def cb(hwnd, lp):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if target_pid is not None and pid.value != target_pid:
            return True
        if not user32.IsWindowVisible(hwnd):
            return True
        cls = ctypes.create_unicode_buffer(128)
        user32.GetClassNameW(hwnd, cls, 128)
        n = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        out.append(f"    {cls.value}: {buf.value}")
        user32.EnumChildWindows(hwnd, CB(child_cb), 0)
        return True

    user32.EnumWindows(CB(cb), 0)
    return out


def run_case(case):
    src_name, dst_name = CASES[case]
    if case == "base":
        src = os.path.join(PROBE, "base.mpp")
        dst = src
        saved = os.path.join(PROBE, "resaved-base.mpp")
        for p in (saved,):
            if os.path.exists(p):
                os.remove(p)
    else:
        src = os.path.join(TEST_OUT, src_name)
        if not os.path.exists(src):
            sys.exit(f"missing source {src}")
        dst = os.path.join(PROBE, dst_name)
        saved = os.path.join(PROBE, dst_name.replace(".mpp", "-resaved.mpp"))
        for p in (dst, saved):
            if os.path.exists(p):
                os.remove(p)
        shutil.copyfile(src, dst)
    log(f"copied {os.path.relpath(src, ROOT)} -> {os.path.relpath(dst, ROOT)} "
        f"({os.path.getsize(dst):,} B)")

    payload = {"case": case, "source": os.path.relpath(dst, ROOT),
               "run_at": dt.datetime.now().isoformat(timespec="seconds"),
               "stored_rows": stored_rows(dst)}

    with oracle_session() as app:
        with Guard(150):
            try:
                app.FileOpen(dst)
            except Exception as exc:                    # noqa: BLE001
                payload["open_error"] = f"{exc.__class__.__name__}: {exc}"
                save(f"writer-{case}", payload)
                return
            proj = app.ActiveProject
            if proj is None:
                payload["opened_name"] = None
                payload["windows"] = window_texts(
                    (project_pids() or [None])[0])
                log("  ActiveProject is None -- Project refused the file; UI says:")
                for line in payload["windows"]:
                    log(line)
                payload["quitting"] = "no project open; killing WINPROJ"
                for pid in project_pids():
                    os.system(f"taskkill /PID {pid} /F >nul 2>&1")
                save(f"writer-{case}", payload)
                return
            payload["opened_name"] = val(getattr(proj, "Name", None))
            payload["tasks_on_open"] = rows_of(proj)
            log("  opened; values as Project shows them:")
            show_rows(payload["tasks_on_open"])
            try:
                calc(app)
            except Exception as exc:                    # noqa: BLE001
                payload["calc_error"] = f"{exc.__class__.__name__}: {exc}"
            payload["tasks_after_calc"] = rows_of(proj)
            log("  after Calculate:")
            show_rows(payload["tasks_after_calc"])
            try:
                neutralise(app, proj)
                app.FileSaveAs(Name=saved)
                payload["resaved"] = os.path.relpath(saved, ROOT)
                payload["resaved_bytes"] = os.path.getsize(saved)
                log(f"  saved {payload['resaved']} ({payload['resaved_bytes']:,} B)")
            except Exception as exc:                    # noqa: BLE001
                payload["save_error"] = f"{exc.__class__.__name__}: {exc}"
    save(f"writer-{case}", payload)


def show_rows(rows):
    for r in rows:
        print(f"    id {r.get('ID', '?')!s:>4} {str(r.get('Name'))[:34]:34} "
              f"prio={r.get('Priority')} pct={r.get('PercentComplete')} "
              f"dur={r.get('Duration')} start={r.get('Start')} "
              f"finish={r.get('Finish')}")


def cmd_diff(args):
    """Byte-diff every stream of a resaved file against its source."""
    pairs = [("oracle-patch.mpp", "oracle-patch-resaved.mpp"),
             ("oracle-rebuild.mpp", "oracle-rebuild-resaved.mpp"),
             ("oracle-name.mpp", "oracle-name-resaved.mpp"),
             ("base.mpp", "resaved-base.mpp")]
    report = {}
    for a, b in pairs:
        pa, pb = os.path.join(PROBE, a), os.path.join(PROBE, b)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            log(f"skip {a}: missing {os.path.relpath(pb, ROOT)}")
            continue
        sa, sb = streams(pa), streams(pb)
        keys = sorted(set(sa) | set(sb))
        changed, added, removed = [], [], []
        for k in keys:
            if k not in sa:
                added.append(k)
            elif k not in sb:
                removed.append(k)
            elif sa[k] != sb[k]:
                x, y = sa[k], sb[k]
                n = min(len(x), len(y))
                first = next((i for i in range(n) if x[i] != y[i]), None)
                changed.append({"stream": k, "src": len(x), "dst": len(y),
                                "differing": sum(1 for i in range(n) if x[i] != y[i]),
                                "first": first})
        report[a] = {"bytes_src": os.path.getsize(pa), "bytes_resaved": os.path.getsize(pb),
                     "streams_src": len(sa), "streams_resaved": len(sb),
                     "changed": changed, "added": added, "removed": removed}
        log(f"\n{a}: {os.path.getsize(pa):,} -> {os.path.getsize(pb):,} B, "
            f"{len(sa)} -> {len(sb)} streams, {len(changed)} changed, "
            f"{len(added)} added, {len(removed)} removed")
        for c in sorted(changed, key=lambda c: -c["differing"])[:40]:
            log(f"    {c['stream'][:52]:52} {c['src']:>8} -> {c['dst']:>8} "
                f"diff {c['differing']:>7} first {c['first']}")
        for k in added:
            log(f"    + {k}")
        for k in removed:
            log(f"    - {k}")
        if a == "base.mpp":
            st214 = [c for c in changed if "/214/" in c["stream"]]
            st114 = [c for c in changed if "/114/" in c["stream"]]
            report[a]["summary"] = {
                "214_streams_changed": len(st214), "114_streams_changed": len(st114),
                "214_total": sum(1 for k in sa if "/214/" in k)}
            log(f"    of {report[a]['summary']['214_total']} 214/ streams, "
                f"{len(st214)} changed; {len(st114)} of the 114/ streams changed")
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "writer-diff.json"), "w", encoding="utf-8",
              newline="\n") as fh:
        json.dump(report, fh, indent=2)
    log(f"\nwrote docs/analysis/oracle/writer-diff.json")


def streams(path):
    ole = olefile.OleFileIO(path)
    try:
        return {"/".join(p): ole.openstream(p).read()
                for p in ole.listdir(streams=True, storages=False)}
    finally:
        ole.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("case", choices=sorted(CASES) + ["diff"])
    args = ap.parse_args()
    if args.case == "diff":
        cmd_diff(args)
    else:
        run_case(args.case)


if __name__ == "__main__":
    main()
