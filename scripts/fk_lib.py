"""W5 shared code: correct Task VarMeta parsing, name sources, payload shapes.

The VarMeta layout used here is the one from MS Project's own writer and from
mpxj's `VarMeta12` reader (the Task table uses the MPP14 variant):

    24-byte header: magic 0xBAADFADF | 0 | entry_count | 0 | 0 | Var2Data size
    entry (12 B):   u32 uid | u32 Var2Data offset | u32 field id

The entry count in the header is stale in one corpus file (`project_a`
says 1076 while 1081 entries are present), so the stream length is
authoritative.

The field id is a full `0x0B40xxxx` PjField value; the low 16 bits are what
mpxj keys its field map on.
"""

import collections
import os
import struct

import probe_lib as L

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE = os.path.join(ROOT, "corpus", "probe")

VAR_HEADER = 24
VAR_ENTRY = 12

FILES = [
    ("base", os.path.join(PROBE, "base.mpp")),
    ("var-name", os.path.join(PROBE, "var-name.mpp")),
    ("var-notes", os.path.join(PROBE, "var-notes.mpp")),
    ("var-pct", os.path.join(PROBE, "var-pct.mpp")),
    ("var-duration", os.path.join(PROBE, "var-duration.mpp")),
    ("var-priority", os.path.join(PROBE, "var-priority.mpp")),
    ("var-projstart", os.path.join(PROBE, "var-projstart.mpp")),
    ("var-title", os.path.join(PROBE, "var-title.mpp")),
    ("var-rsc2", os.path.join(PROBE, "var-rsc2.mpp")),
    ("var-baseline", os.path.join(PROBE, "var-baseline.mpp")),
    ("probe-v1", os.path.join(PROBE, "probe-v1.mpp")),
    ("probe-v2", os.path.join(PROBE, "probe-v2.mpp")),
    ("project_a", os.path.join(ROOT, "corpus", "project_a.mpp")),
    ("project_b", os.path.join(ROOT, "corpus", "project_b.mpp")),
]


def parse_var_meta(vm):
    """Return (header dict, [entry dict]) for the Task VarMeta stream."""
    if L.u32(vm, 0) != 0xFADFADBA:
        raise ValueError(f"bad VarMeta magic {L.u32(vm, 0):08x}")
    if (len(vm) - VAR_HEADER) % VAR_ENTRY:
        raise ValueError(f"VarMeta {len(vm)} B is not 24 + 12*n")
    header = {
        "header_count": L.u32(vm, 8),
        "var2_size": L.u32(vm, 20),
    }
    n = (len(vm) - VAR_HEADER) // VAR_ENTRY
    entries = []
    for i in range(n):
        uid, off, typ = struct.unpack_from("<3I", vm, VAR_HEADER + i * VAR_ENTRY)
        entries.append({"index": i, "mid": uid, "offset": off, "field_id": typ})
    return header, entries


def load_task(path):
    """Load the Task fixed table and its variable store, fully parsed."""
    s = L.load(path)
    fd = L.stream(s, "TBkndTask", "FixedData")
    vm = L.stream(s, "TBkndTask", "VarMeta")
    v2 = L.stream(s, "TBkndTask", "Var2Data")
    header, entries = parse_var_meta(vm)
    _, rows = L.rows_of(fd)
    for e in entries:
        e["block"] = None
        if e["offset"] != 0xFFFFFFFF and e["offset"] + 4 <= len(v2):
            e["block"] = L.read_block(v2, e["offset"])
    return dict(path=path, rows=rows, entries=entries, header=header, v2=v2,
                fixed=fd)


def shape(payload):
    """Classify a Var2Data block by its observed bytes."""
    if payload is None:
        return "nodata"
    n = len(payload)
    if n == 0:
        return "empty"
    if payload[:2] == b"{\\":
        return "rtf"
    try:
        t = payload.decode("utf-16le")
    except UnicodeDecodeError:
        t = None
    if t is not None and n >= 2 and t.endswith("\x00"):
        body = t[:-1]
        if body == "":
            return "text(empty)"
        if body.isprintable():
            return "text"
    if n == 2:
        return "u16"
    if n == 4:
        return "i32"
    if n == 8:
        return "pair/double"
    if n == 16:
        return "quad"
    return "bytes"


def key_stats(task):
    """key -> {n, bytes, mids, shapes, samples}."""
    st = collections.OrderedDict()
    for e in sorted(task["entries"], key=lambda e: e["field_id"]):
        k = e["field_id"]
        d = st.setdefault(k, {"n": 0, "bytes": 0, "mids": set(),
                              "shapes": collections.Counter(), "samples": []})
        d["n"] += 1
        d["bytes"] += len(e["block"] or b"")
        d["mids"].add(e["mid"])
        sh = shape(e["block"])
        d["shapes"][sh] += 1
        if len(d["samples"]) < 3 and sh in ("text", "rtf"):
            try:
                txt = (e["block"] or b"").decode("utf-16le").rstrip("\x00")
            except UnicodeDecodeError:
                txt = ""
            if txt:
                d["samples"].append(txt[:40])
    return st


def _tsv(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        header = f.readline().rstrip("\n").split("\t")
        for line in f:
            yield dict(zip(header, line.rstrip("\n").split("\t")))


def catalog_names():
    """id -> property_name from analysis/field_catalog_task.tsv (task table)."""
    out = {}
    for r in _tsv(os.path.join(ROOT, "analysis", "field_catalog_task.tsv")):
        name = r.get("property_name", "")
        if name:
            out[int(r["field_id_dec"])] = name
    return out


def pjfield_names():
    """id -> property_name from analysis/field_id_map.tsv (Task interface only)."""
    out = {}
    for r in _tsv(os.path.join(ROOT, "analysis", "field_id_map.tsv")):
        if not r.get("field_id", "").startswith("0x0b40"):
            continue
        name = r.get("property_name", "")
        if name:
            out[int(r["field_id_dec"])] = name
    return out


def merged_names():
    """Best name per id: catalog first, typelib as fallback."""
    out = dict(pjfield_names())
    out.update(catalog_names())
    return out
