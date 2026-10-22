"""Reference reader for MPP14 task tables — the ground truth the C++ reader must match.

Written to settle the format independently of any other stream's parser. Every
field below was verified against a task list exported from MS Project itself
(tests/data/project_a_golden.tsv). If this script and the C++ reader disagree, one of
them is wrong and the golden file says which.

Usage:
    python scripts/ref_reader.py <file.mpp>            # human-readable dump
    python scripts/ref_reader.py <file.mpp> --check    # compare to the golden TSV

Layout notes, all verified (see spec/mpp-records.md for the derivation):
- FixedData: 48-byte header, then 202-byte rows. Row 0 is the project row.
- Row fields: +0 ID (u32), +4 UniqueID (u32), +78 Priority (u16),
  +84 Duration (u32, TENTHS of a minute), +92 PercentComplete (u16 — not u32:
  reading 4 bytes yields 0x0064_0064 and looks like a huge number), +96 Start and
  +100 Finish (u32 packed dates).
- Packed date: (day << 16) | (six-second ticks), day 1 = 1984-01-01.
- VarMeta: 24-byte header then 12-byte entries (uid, var2data offset, key).
  The header's declared count is stale in some files (PROJECT_A says 1076, has 1080);
  trust the stream length.
- Var2Data: u32 byte length then payload; UTF-16LE for text, length includes NUL.
- Join metadata to rows on UniqueID, never on row index: files contain rows for
  deleted tasks (PROJECT_A uid 10856 has no metadata) and FixedData order is not ID order.
"""

import argparse
import hashlib
import os
import struct
import sys
from datetime import datetime, timedelta

import olefile

EPOCH = datetime(1984, 1, 1)
NAME_KEY = 0x0B40000E
NA = 0xFFFFFFFF


def streams(path):
    ole = olefile.OleFileIO(path)
    try:
        return {"/".join(p): ole.openstream(p).read()
                for p in ole.listdir(streams=True, storages=False)}
    finally:
        ole.close()


def stream(streams_by_name, suffix):
    for name, data in streams_by_name.items():
        if name.endswith(suffix):
            return data
    raise KeyError(f"no stream ending {suffix}")


def decode_date(v):
    if v == NA:
        return None
    day, ticks = v >> 16, v & 0xFFFF
    return EPOCH + timedelta(days=day - 1, seconds=ticks * 6)


def decode_var_text(var2data, offset):
    if offset == NA or offset + 4 > len(var2data):
        return None
    length = struct.unpack_from("<I", var2data, offset)[0]
    if length == 0 or offset + 4 + length > len(var2data) or length > 2000:
        return None
    return var2data[offset + 4:offset + 4 + length].decode("utf-16-le", "replace").rstrip("\x00")


def read_tasks(path):
    s = streams(path)
    fixed_data = stream(s, "TBkndTask/FixedData")
    var_meta = stream(s, "TBkndTask/VarMeta")
    var2data = stream(s, "TBkndTask/Var2Data")

    # (uid, offset, key) triples; ignore the stale declared count
    names = {}
    for i in range((len(var_meta) - 24 - 4) // 12):
        uid, offset, key = struct.unpack_from("<3I", var_meta, 24 + i * 12)
        if key == NAME_KEY:
            names[uid] = decode_var_text(var2data, offset)

    tasks, anomalies = [], []
    rows = (len(fixed_data) - 48) // 202
    for r in range(rows):
        o = 48 + 202 * r
        uid = struct.unpack_from("<I", fixed_data, o + 4)[0]
        if uid == 0:                     # project row
            continue
        name = (names.get(uid) or "").strip()
        if not name:
            anomalies.append(f"row {r}: uid {uid} has no Name — treated as a deleted row")
            continue
        tasks.append(dict(
            row=r,
            uid=uid,
            id=struct.unpack_from("<I", fixed_data, o + 0)[0],
            name=name,
            duration_ticks=struct.unpack_from("<I", fixed_data, o + 84)[0],
            priority=struct.unpack_from("<H", fixed_data, o + 78)[0],
            percent=struct.unpack_from("<H", fixed_data, o + 92)[0],
            start=decode_date(struct.unpack_from("<I", fixed_data, o + 96)[0]),
            finish=decode_date(struct.unpack_from("<I", fixed_data, o + 100)[0]),
        ))
    return tasks, anomalies, rows


def check(tasks, golden_path):
    golden = []
    with open(golden_path, encoding="utf-8") as fh:
        fh.readline()
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) < 8:
                continue
            golden.append(dict(uid=int(p[0]), id=int(p[1]), name=p[2], dur=int(p[3]),
                               start=p[4], finish=p[5], prio=int(p[6]), pct=int(p[7])))

    by_uid = {t["uid"]: t for t in tasks}
    counts = {k: 0 for k in ("id", "name", "duration", "start", "finish", "priority", "percent")}
    fails = []
    for g in golden:
        t = by_uid.get(g["uid"])
        if not t:
            fails.append((g["uid"], "missing"))
            continue
        name_hash = "sha256:" + hashlib.sha256(t["name"].encode()).hexdigest()[:16]
        pairs = {
            "id": (t["id"], g["id"]),
            "name": (name_hash, g["name"]),
            "duration": (t["duration_ticks"] // 10, g["dur"]),
            "start": (t["start"].isoformat(sep=" ") + "+00:00" if t["start"] else "", g["start"]),
            "finish": (t["finish"].isoformat(sep=" ") + "+00:00" if t["finish"] else "", g["finish"]),
            "priority": (t["priority"], g["prio"]),
            "percent": (t["percent"], g["pct"]),
        }
        for field, (got, want) in pairs.items():
            if got == want:
                counts[field] += 1
            else:
                fails.append((g["uid"], field, got, want))

    print(f"golden tasks: {len(golden)}   parsed tasks: {len(tasks)}")
    for field, n in counts.items():
        mark = "ok " if n == len(golden) else "FAIL"
        print(f"  {field:10} {n:4}/{len(golden)}  {mark}")
    if fails:
        print(f"\n{len(fails)} mismatches, first 10:")
        for f in fails[:10]:
            print("  ", f)
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--golden", default=None)
    args = ap.parse_args()

    tasks, anomalies, rows = read_tasks(args.path)
    print(f"{os.path.basename(args.path)}: {rows} rows, {len(tasks)} tasks")
    for a in anomalies:
        print("  anomaly:", a)

    if args.check:
        golden = args.golden or os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "tests", "data", "project_a_golden.tsv")
        sys.exit(check(tasks, golden))

    for t in tasks[:20]:
        print(f"  uid={t['uid']:<7} id={t['id']:<4} {t['name'][:44]:<46} "
              f"dur={t['duration_ticks']//10:>7}min  {t['start']}  {t['finish']}"
              f"  prio={t['priority']} pct={t['percent']}")


if __name__ == "__main__":
    main()
