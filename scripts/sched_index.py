#!/usr/bin/env python3
"""Static index of the Ghidra decompile for scheduling work.

The decompile is 2.59 M lines across decompiled/part_*.c.  This script builds
an in-memory index (address -> body span) once, then answers the questions a
spec writer actually asks:

  python scripts/sched_index.py func 1402b558c
  python scripts/sched_index.py find "GetEffectiveWeekExtents" [-C 3]
  python scripts/sched_index.py refs 0xb400011
  python scripts/sched_index.py callers 14024e7c0
  python scripts/sched_index.py callees 1402b558c
  python scripts/sched_index.py vt TBkndTaskElem [--slot 65]
  python scripts/sched_index.py vt 141010638
  python scripts/sched_index.py names CalendarReader

Everything is read-only.  No MS Project process is touched.
"""
import argparse
import csv
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(ROOT, "decompiled")
META = os.path.join(ROOT, "analysis", "meta")

HDR = re.compile(r"// ==== (FUN_[0-9a-fA-F]+) @ ([0-9a-fA-F]+) ====")


def load_parts():
    """Return list of (part_name, [lines])."""
    parts = []
    for name in sorted(os.listdir(DEC)):
        if name.startswith("part_") and name.endswith(".c"):
            with open(os.path.join(DEC, name), encoding="utf-8", errors="replace") as f:
                parts.append((name, f.readlines()))
    return parts


def build_index(parts):
    """addr -> (part_name, start_idx, end_idx); 0-based line indices."""
    idx = {}
    for name, lines in parts:
        cur = None
        start = 0
        for i, line in enumerate(lines):
            m = HDR.match(line)
            if m:
                if cur is not None:
                    idx[cur] = (name, start, i)
                cur = m.group(2).lower()
                start = i
        if cur is not None:
            idx[cur] = (name, start, len(lines))
    return idx


def norm(a):
    a = a.strip().lower()
    return a[2:] if a.startswith("0x") else a


def get_body(parts_by_name, idx, addr):
    addr = norm(addr)
    if addr not in idx:
        raise SystemExit(f"function {addr} not found")
    name, s, e = idx[addr]
    lines = parts_by_name[name]
    return name, s, lines[s:e]


def load_tsv(path):
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        return list(csv.reader(f, delimiter="\t"))


def cmd_func(args):
    name, s, body = get_body(PARTS_BY_NAME, INDEX, args.addr)
    print(f"# {name}:{s+1}  ({len(body)} decompiled lines)")
    for i, line in enumerate(body):
        print(f"{s+1+i:7d}  {line}", end="")


def cmd_find(args):
    pat = re.compile(args.pattern, re.I)
    hits = 0
    for addr in sorted(INDEX):
        name, s, body = get_body(PARTS_BY_NAME, INDEX, addr)
        for i, line in enumerate(body):
            if pat.search(line):
                ctx = body[max(0, i - args.context): i + args.context + 1]
                print(f"--- {addr} {name}:{s+1+i}")
                for j, c in enumerate(ctx):
                    print(f"{s+1+i-args.context+j:7d}  {c}", end="")
                hits += 1
                if hits >= args.limit:
                    print(f"... limit {args.limit} reached")
                    return


def cmd_refs(args):
    """Find literal references to a field id / constant, with function name."""
    val = args.value.lower()
    if val.startswith("0x"):
        variants = {val, str(int(val, 16))}
    else:
        variants = {val}
    found = defaultdict(list)
    for addr in sorted(INDEX):
        name, s, body = get_body(PARTS_BY_NAME, INDEX, addr)
        for i, line in enumerate(body):
            low = line.lower()
            if any(v in low for v in variants):
                found[addr].append((s + 1 + i, line.rstrip()))
    for addr, rows in found.items():
        print(f"--- {addr} {name_of(addr)}  ({len(rows)} refs)")
        for ln, line in rows[:args.per_func]:
            print(f"{ln:7d}  {line}")
        if len(rows) > args.per_func:
            print(f"        ... {len(rows)-args.per_func} more")


def cmd_callers(args):
    addr = norm(args.addr)
    for row in CALLS:
        if row[1] == addr:
            print(row[0], name_of(row[0]))


def cmd_callees(args):
    addr = norm(args.addr)
    for row in CALLS:
        if row[0] == addr:
            print(row[1], name_of(row[1]))


def name_of(addr):
    return NAMES.get(addr, "")


def load_names():
    names = {}
    p = os.path.join(ROOT, "analysis", "named_functions_p1.tsv")
    if os.path.exists(p):
        for row in load_tsv(p)[1:]:
            if len(row) >= 2:
                names[norm(row[0])] = row[1]
    return names


def load_vtables():
    p = os.path.join(META, "vtables.tsv")
    rows = load_tsv(p)[1:]
    by_class = defaultdict(list)
    by_addr = defaultdict(list)
    for r in rows:
        if len(r) < 4:
            continue
        a, c, s, f = r
        try:
            s = int(s)
        except ValueError:
            continue
        by_class[c].append((a, s, norm(f)))
        by_addr[norm(a)].append((c, s, norm(f)))
    return by_class, by_addr


def cmd_vt(args):
    if args.target.startswith("0x") or re.fullmatch(r"[0-9a-fA-F]{6,}", args.target):
        rows = VT_BY_ADDR.get(norm(args.target), [])
        if not rows:
            raise SystemExit("vtable not found")
        for c, s, f in sorted(rows, key=lambda x: x[1]):
            print(f"{c}\tslot {s}\t{f}\t{name_of(f)}{meta_suffix(f)}")
    else:
        rows = VT_BY_CLASS.get(args.target, [])
        if not rows:
            raise SystemExit("class not found")
        by = defaultdict(list)
        for a, s, f in rows:
            by[a].append((s, f))
        for a, lst in sorted(by.items(), key=lambda kv: -max(s for s, _ in kv[1])):
            print(f"### vtable {a}  ({len(lst)} slots, max {max(s for s,_ in lst)})")
            for s, f in sorted(lst):
                if args.slot is None or s == args.slot:
                    print(f"  slot {s}\txref {f}\t{name_of(f)}{meta_suffix(f)}")


def meta_suffix(addr):
    r = FUNCS.get(addr)
    if not r:
        return ""
    return f"\t{r[3]}B callers={r[5]} callees={r[6]}"


def cmd_names(args):
    pat = re.compile(args.pattern, re.I)
    for addr, name in sorted(NAMES.items()):
        if pat.search(name):
            print(addr, name)


def load_functions():
    funcs = {}
    p = os.path.join(META, "functions.tsv")
    for row in load_tsv(p)[1:]:
        if len(row) >= 7:
            funcs[norm(row[0])] = row
    return funcs


def main():
    global PARTS, INDEX, PARTS_BY_NAME, CALLS, NAMES, VT_BY_CLASS, VT_BY_ADDR, FUNCS
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("func"); p.add_argument("addr"); p.set_defaults(fn=cmd_func)
    p = sub.add_parser("find"); p.add_argument("pattern")
    p.add_argument("-C", "--context", type=int, default=2)
    p.add_argument("--limit", type=int, default=40); p.set_defaults(fn=cmd_find)
    p = sub.add_parser("refs"); p.add_argument("value")
    p.add_argument("--per-func", type=int, default=25); p.set_defaults(fn=cmd_refs)
    p = sub.add_parser("callers"); p.add_argument("addr"); p.set_defaults(fn=cmd_callers)
    p = sub.add_parser("callees"); p.add_argument("addr"); p.set_defaults(fn=cmd_callees)
    p = sub.add_parser("vt"); p.add_argument("target")
    p.add_argument("--slot", type=int); p.set_defaults(fn=cmd_vt)
    p = sub.add_parser("names"); p.add_argument("pattern"); p.set_defaults(fn=cmd_names)

    args = ap.parse_args()
    PARTS = load_parts()
    PARTS_BY_NAME = dict(PARTS)
    INDEX = build_index(PARTS)
    CALLS = load_tsv(os.path.join(META, "calls.tsv"))[1:]
    NAMES = load_names()
    FUNCS = load_functions()
    VT_BY_CLASS, VT_BY_ADDR = load_vtables()
    args.fn(args)


if __name__ == "__main__":
    main()
