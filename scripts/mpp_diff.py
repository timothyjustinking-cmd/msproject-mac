#!/usr/bin/env python3
"""Stream-by-stream diff of two .mpp files, with structure decoding.

Written for the wave-10 add/delete oracle (W45's design input): Project saves a
project, we add or delete a task row, Project saves again, and this tool says
exactly which streams grew, shrank or changed — and, for the table streams, which
row / metadata entry / variable block did.

    python scripts/mpp_diff.py before.mpp after.mpp
    python scripts/mpp_diff.py before.mpp after.mpp --json out.json
    python scripts/mpp_diff.py before.mpp after.mpp --only TBkndTask

Read-only, depends only on `olefile`. Output is numbers, byte offsets and hex
only: no task text ever leaves the file, so it is safe to run on a client file
(though the fixtures are synthetic).

Per-table layouts (spec/mpp-records.md, core/mpp/reader.h, core/mppwrite/table.cpp):

  table      FixedData            FixedMeta        Fixed2Data        Fixed2Meta
  TBkndTask  48 + 202*n           16 + 47*n        192 + 64*n        16 + 96*n
  TBkndRsc   48 + 172*n           16 + ?*n         ?                 ?
  TBkndAssn  110*n (no header)    16 + 34*n        48*n (no header)  16 + 53*n
  TBkndCons  20*n (no header)     16 + 10*n        ?                 ?

VarMeta is a **24-byte** header followed by 12-byte `(uid, offset, key)` triples
(the W5/W21 correction; scripts/probe_lib.py's 32-byte reading is the old one).
"""

import argparse
import json
import struct
import sys

import olefile

VAR_HEADER = 24
VAR_ENTRY = 12

TABLE_SPECS = {
    "TBkndTask": {"fixed_header": 48, "fixed_row": 202, "uid_off": 4,
                  "meta_entry": 47, "fixed2_header": 192, "fixed2_row": 64,
                  "meta2_entry": 96},
    "TBkndRsc": {"fixed_header": 48, "fixed_row": 172, "uid_off": 4,
                 "meta_entry": 0, "fixed2_header": 0, "fixed2_row": 0,
                 "meta2_entry": 0},
    "TBkndAssn": {"fixed_header": 0, "fixed_row": 110, "uid_off": 0,
                  "meta_entry": 34, "fixed2_header": 0, "fixed2_row": 48,
                  "meta2_entry": 53},
    "TBkndCons": {"fixed_header": 0, "fixed_row": 20, "uid_off": 0,
                  "meta_entry": 10, "fixed2_header": 0, "fixed2_row": 0,
                  "meta2_entry": 0},
}


def load(path):
    ole = olefile.OleFileIO(path)
    try:
        out = {}
        for parts in ole.listdir(streams=True):
            key = "/".join(p.strip() for p in parts)
            out[key] = ole.openstream(parts).read()
        return out
    finally:
        ole.close()


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def i32(buf, off):
    return struct.unpack_from("<i", buf, off)[0]


def first_diffs(a, b, limit=12):
    n = min(len(a), len(b))
    out = []
    for i in range(n):
        if a[i] != b[i]:
            out.append(i)
            if len(out) >= limit:
                return out
    return out


def common_span(a, b):
    n = min(len(a), len(b))
    prefix = 0
    while prefix < n and a[prefix] == b[prefix]:
        prefix += 1
    suffix = 0
    while suffix < n - prefix and a[n - 1 - suffix] == b[n - 1 - suffix]:
        suffix += 1
    return prefix, suffix


def hexs(buf, start=0, count=24):
    return buf[start:start + count].hex(" ")


def table_of(path):
    parts = path.split("/")
    return parts[1] if len(parts) > 1 else ""


def spec_of(path):
    return TABLE_SPECS.get(table_of(path), {})


# ---------------------------------------------------------------- tables


def fixed_rows(data, spec):
    header = spec.get("fixed_header", 0)
    row_size = spec["fixed_row"]
    uid_off = spec["uid_off"]
    rows = []
    body = data[header:]
    for i in range(len(body) // row_size):
        row = body[i * row_size:(i + 1) * row_size]
        rows.append({"index": i, "uid": u32(row, uid_off), "raw": row})
    return data[:header], rows


def diff_fixed_data(a, b, spec):
    ha, ra = fixed_rows(a, spec)
    hb, rb = fixed_rows(b, spec)
    print(f"  rows {len(ra)} -> {len(rb)}")
    if ha != hb:
        print(f"  header ({len(ha)} B) differs at {first_diffs(ha, hb)}")
    ua = {r["uid"]: r for r in ra}
    ub = {r["uid"]: r for r in rb}
    added = [u for u in ub if u not in ua]
    removed = [u for u in ua if u not in ub]
    print(f"  uids added {added}  removed {removed}")
    for uid in added:
        print(f"  new row uid {uid} (index {ub[uid]['index']}): "
              f"{hexs(ub[uid]['raw'], 0, 48)}")
    for uid in removed:
        print(f"  removed row uid {uid} (index {ua[uid]['index']}): "
              f"{hexs(ua[uid]['raw'], 0, 48)}")
    for uid in sorted(set(ua) & set(ub)):
        x, y = ua[uid]["raw"], ub[uid]["raw"]
        offs = first_diffs(x, y, limit=10)
        if offs:
            desc = ", ".join(f"+{o}:{x[o]:02x}->{y[o]:02x}" for o in offs)
            print(f"  uid {uid}: {desc}")


def meta_entries(data, entry_size):
    if not entry_size or len(data) < 16:
        return [], []
    n = (len(data) - 16) // entry_size
    header = [u32(data, 0), u32(data, 4), u32(data, 8), u32(data, 12)]
    entries = [data[16 + i * entry_size:16 + (i + 1) * entry_size]
               for i in range(n)]
    return header, entries


def diff_meta(label, a, b, entry_size, name_of_offset):
    ha, ea = meta_entries(a, entry_size)
    hb, eb = meta_entries(b, entry_size)
    print(f"  {label} header {ha} -> {hb}")
    print(f"  entries {len(ea)} -> {len(eb)}")
    common = min(len(ea), len(eb))
    for i in range(common):
        if ea[i] != eb[i]:
            offs = first_diffs(ea[i], eb[i], limit=8)
            desc = ", ".join(f"+{o}:{ea[i][o]:02x}->{eb[i][o]:02x}"
                             for o in offs)
            print(f"   entry {i}: {desc}")
    for i in range(common, len(eb)):
        off = u32(eb[i], 4)
        print(f"   new entry {i}: off={off} {name_of_offset(off)} "
              f"blob={hexs(eb[i], 0, 24)}")
    for i in range(common, len(ea)):
        off = u32(ea[i], 4)
        print(f"   dropped entry {i}: off={off} {name_of_offset(off)} "
              f"blob={hexs(ea[i], 0, 24)}")


def diff_var_meta(a, b):
    n = (len(a) - VAR_HEADER) // VAR_ENTRY
    m = (len(b) - VAR_HEADER) // VAR_ENTRY
    ha = struct.unpack_from("<6I", a, 0)
    hb = struct.unpack_from("<6I", b, 0)
    print(f"  header {ha} -> {hb}")
    ea = [struct.unpack_from("<3I", a, VAR_HEADER + i * VAR_ENTRY)
          for i in range(n)]
    eb = [struct.unpack_from("<3I", b, VAR_HEADER + i * VAR_ENTRY)
          for i in range(m)]
    print(f"  entries {n} -> {m}  (uid, offset, key)")
    ka = {(e[0], e[2]): e for e in ea}
    kb = {(e[0], e[2]): e for e in eb}
    added = [kb[k] for k in kb if k not in ka]
    removed = [ka[k] for k in ka if k not in kb]
    if added:
        print(f"  entries added: {added}")
    if removed:
        print(f"  entries removed: {removed}")
    same_order = all(ea[i][0] == eb[i][0] and ea[i][2] == eb[i][2]
                     for i in range(min(n, m)))
    print(f"  entry order of first {min(n, m)} unchanged: {same_order}")
    changed = [(i, ea[i], eb[i]) for i in range(min(n, m)) if ea[i] != eb[i]]
    for i, x, y in changed[:12]:
        print(f"   entry {i}: {x} -> {y}")


def diff_var2(a, b):
    print(f"  Var2Data {len(a)} -> {len(b)} B")
    if len(a) == len(b):
        print(f"  first differing bytes: {first_diffs(a, b, limit=10)}")
        return
    prefix, suffix = common_span(a, b)
    print(f"  common prefix {prefix} B, common suffix {suffix} B")
    if len(b) > len(a):
        start, end = prefix, len(b) - suffix
        if end - start <= 256:
            print(f"  inserted [{start}:{end}] = {b[start:end].hex(' ')}")
        else:
            print(f"  inserted [{start}:{end}] ({end - start} B)")
    else:
        start, end = prefix, len(a) - suffix
        print(f"  removed [{start}:{end}] ({end - start} B)")


def diff_fixed2_data(a, b, spec):
    header = spec.get("fixed2_header", 0)
    row_size = spec.get("fixed2_row", 0)
    if not row_size:
        prefix, suffix = common_span(a, b)
        print(f"  Fixed2Data {len(a)} -> {len(b)} B (layout unknown); "
              f"prefix {prefix}, suffix {suffix}")
        return
    ra = (len(a) - header) // row_size
    rb = (len(b) - header) // row_size
    print(f"  Fixed2Data {len(a)} -> {len(b)} B, records {ra} -> {rb}")
    if header and len(a) >= header and len(b) >= header:
        print(f"  special {header} B equal: "
              f"{a[:header] == b[:header]}")
    for i in range(min(ra, rb)):
        x = a[header + i * row_size:header + (i + 1) * row_size]
        y = b[header + i * row_size:header + (i + 1) * row_size]
        if x != y:
            offs = first_diffs(x, y, limit=8)
            desc = ", ".join(f"+{o}:{x[o]:02x}->{y[o]:02x}" for o in offs)
            print(f"   record {i}: {desc}")
    for i in range(min(ra, rb), rb):
        rec = b[header + i * row_size:header + (i + 1) * row_size]
        print(f"   new record {i}: {hexs(rec, 0, 32)}")
    for i in range(min(ra, rb), ra):
        rec = a[header + i * row_size:header + (i + 1) * row_size]
        print(f"   dropped record {i}: {hexs(rec, 0, 32)}")


def describe(path, a, b):
    name = path.rsplit("/", 1)[-1]
    spec = spec_of(path)
    delta = len(b) - len(a)
    print(f"\n== {path}: {len(a)} -> {len(b)} B ({delta:+d})")
    if name == "FixedData" and spec.get("fixed_row"):
        diff_fixed_data(a, b, spec)
    elif name == "FixedMeta" and spec.get("meta_entry"):
        diff_meta("FixedMeta", a, b, spec["meta_entry"],
                  lambda off: f"row {(off - spec.get('fixed_header', 0)) // spec['fixed_row']}"
                  if off >= spec.get("fixed_header", 0) and
                  (off - spec.get("fixed_header", 0)) % spec["fixed_row"] == 0
                  else "")
    elif name == "Fixed2Meta" and spec.get("meta2_entry"):
        diff_meta("Fixed2Meta", a, b, spec["meta2_entry"],
                  lambda off: f"record {off // spec['fixed2_row']}"
                  if spec.get("fixed2_row") and off % spec["fixed2_row"] == 0
                  else "")
    elif name == "Fixed2Data":
        diff_fixed2_data(a, b, spec)
    elif name == "VarMeta":
        diff_var_meta(a, b)
    elif name == "Var2Data":
        diff_var2(a, b)
    else:
        prefix, suffix = common_span(a, b)
        print(f"  common prefix {prefix} B, common suffix {suffix} B")
        if len(a) == len(b):
            offs = first_diffs(a, b, limit=12)
            for off in offs:
                print(f"    +{off:5d}: {a[off]:02x} -> {b[off]:02x}")
        else:
            print(f"  first 24 B: {hexs(a)} -> {hexs(b)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--json", default=None)
    ap.add_argument("--only", default=None,
                    help="substring filter on stream paths")
    args = ap.parse_args()

    a = load(args.before)
    b = load(args.after)
    paths = sorted(set(a) | set(b))
    changed = []
    for path in paths:
        if args.only and args.only.lower() not in path.lower():
            continue
        if path not in a or path not in b or a[path] != b[path]:
            changed.append(path)
            describe(path, a.get(path, b""), b.get(path, b""))

    print(f"\n== summary: {len(paths)} streams, {len(changed)} changed")
    for path in changed:
        print(f"  {path}: {len(a.get(path, b''))} -> "
              f"{len(b.get(path, b''))}")

    if args.json:
        payload = {
            "before": args.before,
            "after": args.after,
            "streams": len(paths),
            "changed": [{"path": p,
                         "before": len(a.get(p, b"")),
                         "after": len(b.get(p, b""))}
                        for p in changed],
        }
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2)
        print(f"wrote {args.json}")


if __name__ == "__main__":
    main()
