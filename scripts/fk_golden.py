"""W5 external cross-check: parser view vs MS Project's own export.

    python scripts/fk_golden.py

Compares `corpus/project_a.mpp`, parsed with the corrected VarMeta
framing (`fk_lib`), against `tests/data/project_a_golden.tsv` (83 live tasks read out
of MS Project over COM).  Read-only; writes nothing.

Field sources in the 202-byte FixedData row (all verified by the fixtures):
    +0   u32 id             +4   u32 uid
    +78  u16 priority       +84  u32 duration ticks (4800/day), minutes = /10
    +92  u16 percent complete (the u32 there is pct | pct-work << 16)
    +96  u32 start          +100 u32 finish      (packed dates, mpp-records §3)
    name: VarMeta key 0x0b40000e for the row uid (UTF-16LE)

The golden dates are ISO-8601 with a `+00:00` offset; the packed-date decoder
yields the same wall-clock time, so the comparison ignores the offset (it is the
same on every golden row).
"""

import datetime
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_lib as L
from fk_lib import load_task

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GOLDEN = os.path.join(ROOT, "tests", "data", "project_a_golden.tsv")
MPP = os.path.join(ROOT, "corpus", "project_a.mpp")


def date_str(v):
    day, secs, _ = L.decode_date(v)
    d = datetime.date(1983, 12, 31) + datetime.timedelta(days=day)
    return (f"{d.isoformat()} {secs // 3600:02d}:{secs % 3600 // 60:02d}:"
            f"{secs % 60:02d}+00:00")


def name_hash(name):
    return hashlib.sha256(name.encode("utf-8")).hexdigest()[:16]


def read_golden():
    out = {}
    with open(GOLDEN, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        for line in f:
            r = dict(zip(head, line.rstrip("\n").split("\t")))
            out[int(r["uid"])] = r
    return out


def parser_view():
    t = load_task(MPP)
    names = {}
    for e in t["entries"]:
        if e["field_id"] == 0x0B40000E:
            names[e["mid"]] = L.decode_text(e["block"] or b"")
    rows = []
    for i, r in enumerate(t["rows"][1:], start=1):
        uid = L.u32(r, 4)
        rows.append({
            "row": i,
            "id": L.u32(r, 0),
            "uid": uid,
            "name": names.get(uid),
            "duration_min": L.u32(r, 84) // 10,
            "start": date_str(L.u32(r, 96)),
            "finish": date_str(L.u32(r, 100)),
            "priority": L.u16(r, 78),
            "pct": L.u16(r, 92),
            "has_varmeta": any(e["mid"] == uid for e in t["entries"]),
        })
    return rows, names


def main():
    golden = read_golden()
    rows, _ = parser_view()
    by_uid = {r["uid"]: r for r in rows}

    print(f"golden tasks {len(golden)}; parser task rows {len(rows)}")
    extra = [r for r in rows if r["uid"] not in golden]
    missing = [u for u in golden if u not in by_uid]
    print(f"parser rows not in golden: {[(r['row'], r['uid']) for r in extra]}")
    print(f"golden uids not in parser: {missing}")

    fields = ("id", "name", "duration_min", "start", "finish", "priority", "pct")
    ok = {f: 0 for f in fields}
    all_ok = 0
    mismatches = []
    name_raw = 0
    name_strip = 0
    for uid, g in golden.items():
        p = by_uid.get(uid)
        if p is None:
            mismatches.append((uid, "row", "absent", "present"))
            continue
        bad = []
        if p["name"] is None:
            bad.append(("name", "<no VarMeta group>", g["name"]))
        else:
            h = "sha256:" + name_hash(p["name"])
            if h == g["name"]:
                name_raw += 1
            else:
                hs = "sha256:" + name_hash(p["name"].rstrip())
                if hs == g["name"]:
                    name_strip += 1
                bad.append(("name", f"raw {h}", f"golden {g['name']}"
                            f"; rstrip -> {hs}"
                            + (" MATCH" if hs == g["name"] else "")))
        for f in ("id", "duration_min", "start", "finish", "priority", "pct"):
            pv = p[f]
            gv = g[f]
            if f in ("id", "duration_min", "priority", "pct"):
                gv = int(gv)
                if int(pv) != gv:
                    bad.append((f, pv, gv))
            elif pv != gv:
                bad.append((f, pv, gv))
        for f in fields:
            if not any(b[0] == f for b in bad):
                ok[f] += 1
        if bad:
            mismatches.append((uid, bad))
        else:
            all_ok += 1

    print(f"\nall seven fields match: {all_ok}/{len(golden)}")
    for f in fields:
        print(f"  {f:12s} {ok[f]}/{len(golden)}")
    print(f"name hashes exact {name_raw}, trailing-whitespace only {name_strip}")
    print(f"\nmismatches: {len(mismatches)}")
    for m in mismatches:
        print(" ", m)

    for r in extra:
        print(f"\nextra row {r['row']}: id {r['id']} uid {r['uid']}"
              f" name={r['name']!r} dur_min={r['duration_min']}"
              f" start={r['start']} finish={r['finish']}"
              f" pri={r['priority']} pct={r['pct']}")
        print(f"  has any VarMeta entry: {r['has_varmeta']}")


if __name__ == "__main__":
    main()
