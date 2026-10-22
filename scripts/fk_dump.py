"""W5: parse Task VarMeta with the CORRECT layout and dump by row.

Layout established against mpxj `VarMeta12` and the raw bytes of
corpus/probe/base.mpp:

    24-byte header: magic, 0, entry_count, 0, 0, Var2Data size
    entry_count x 12 bytes: u32 uid | u32 Var2Data offset | u32 field type

The field type is the full `0x0B40xxxx` id.  mpxj keeps only the low 16
bits (`readShort` after the offset), which is why its field map is keyed by
values such as 14 for Name.

    python scripts/fk_dump.py corpus/probe/base.mpp
"""

import os
import sys

import probe_lib as L


def parse(vm):
    magic = L.u32(vm, 0)
    if magic != 0xFADFADBA:
        raise ValueError(f"bad VarMeta magic {magic:08x}")
    header_n = L.u32(vm, 8)
    data_size = L.u32(vm, 20)
    if (len(vm) - 24) % 12:
        raise ValueError(f"VarMeta {len(vm)} B is not 24 + 12*n")
    # PROJECT_A's header count is stale (1076 while 1081 entries are present), so the
    # stream length is authoritative.
    n = (len(vm) - 24) // 12
    ent = []
    for i in range(n):
        uid, off, typ = (L.u32(vm, 24 + i * 12), L.u32(vm, 28 + i * 12),
                         L.u32(vm, 32 + i * 12))
        ent.append(dict(index=i, mid=uid, offset=off, field_id=typ))
    return dict(count=n, data_size=data_size, entries=ent)


def guess(payload):
    if len(payload) == 0:
        return "empty"
    if len(payload) == 4:
        v = int.from_bytes(payload, "little", signed=True)
        return f"i32 {v} (0x{int.from_bytes(payload, 'little'):08x})"
    if len(payload) == 8:
        a, b = int.from_bytes(payload[:4], "little"), int.from_bytes(payload[4:], "little")
        return f"2xu32 {a},{b}"
    if len(payload) == 16:
        vs = [int.from_bytes(payload[i:i + 4], "little") for i in range(0, 16, 4)]
        return f"4xu32 {vs}"
    if payload[:2] == b"{\\\\":
        return f"RTF {len(payload)}B"
    try:
        t = payload.decode("utf-16le")
        if t.endswith("\x00") and (t[:-1] == "" or t[:-1].isprintable()):
            return f"TEXT {len(payload)}B {t[:-1]!r}"
    except UnicodeDecodeError:
        pass
    return f"bytes {len(payload)}B {payload[:12].hex(' ')}"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    path = args[0] if args else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "corpus", "probe", "base.mpp")
    limit = None
    for a in sys.argv[1:]:
        if a.startswith("--limit"):
            limit = int(a.split("=")[-1])
    s = L.load(path)
    fd = L.stream(s, "TBkndTask", "FixedData")
    vm = L.stream(s, "TBkndTask", "VarMeta")
    v2 = L.stream(s, "TBkndTask", "Var2Data")
    meta = parse(vm)
    _, rows = L.rows_of(fd)
    uid_row = {L.u32(r, 4): i for i, r in enumerate(rows)}
    print(f"{os.path.basename(path)}: {len(rows)} rows, {meta['count']} entries,"
          f" var2 {len(v2)} B")
    order = []
    groups = {}
    for e in meta["entries"]:
        if e["mid"] not in groups:
            groups[e["mid"]] = []
            order.append(e["mid"])
        groups[e["mid"]].append(e)
    for n, mid in enumerate(order):
        es = sorted(groups[mid], key=lambda e: (e["field_id"], e["index"]))
        print(f"\nmid {mid} (row {uid_row.get(mid)}): {len(es)} entries")
        for e in es:
            pay = L.read_block(v2, e["offset"])
            print(f"  0x{e['field_id']:08x} ({e['field_id'] & 0xffff:5d})"
                  f" off {e['offset']:5d}  {guess(pay)}")
        if limit is not None and n + 1 >= limit:
            break


if __name__ == "__main__":
    main()
