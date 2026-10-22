"""W5: build data/field_keys.tsv from the corrected Task VarMeta evidence.

    python scripts/fk_build.py

Sources, in order of confidence:
  1. one-change fixtures (`var-name`, `var-notes`, ...) -- direct proof for a key;
  2. `analysis/field_catalog_task.tsv` / `analysis/field_id_map.tsv` -- the
     repo's PjField name tables, which share the file's id space (checked: the
     file's key 14 carries the task name and key 15 the RTF notes, exactly where
     those tables put Name/Notes);
  3. mpxj `MPPTaskField.FIELD_ARRAY` (external cross-check, see
     `scripts/fk_compare.py`) -- fills 11 baseline-duration-unit ids the repo
     tables leave unnamed, and agrees with the repo names on 140/141 ids.

Keys are emitted for every Task key observed in any corpus file.  `confidence`
is `proven` (fixture), `high` (named by the repo tables and payload shape is
consistent), `medium` (name only from mpxj, or shape not checkable), or `none`.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fk_lib import FILES, key_stats, load_task, merged_names

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MPXJ = os.path.join(ROOT, "data", "mpxj_task_field_ids.tsv")
OUT = os.path.join(ROOT, "data", "field_keys.tsv")

# Keys proven by a one-change fixture: key -> (fixture, quote from fk_diff.py).
PROVEN = {
    0x0B40000E: ("var-name + var-title",
                 "var-name: uid1 text 'Task A'->'Renamed A'; var-title: uid0 "
                 "'base'->'Probe Title' (row 0 = project)"),
    0x0B40000F: ("var-notes",
                 "var-notes: uid2 Notes block added, 180 B starting '{\\rtf1'"),
}

# Observed structure of the large unnamed keys, from `scripts/fk_sample.py`.
# These are descriptions of the bytes, not names.
UNKNOWN_NOTES = {
    0x0B40007C: ("12-byte record", "sample 00 96 00 00 15 00 01 00 00 00 00 00"),
    0x0B4000F4: ("i32 LE = 0", "zero in all 73 PROJECT_B task rows"),
    0x0B40015E: ("76-byte binary block", "prefix 03 00 04 00 10 00; appears "
                 "only after PercentComplete is set (var-pct adds it)"),
    0x0B400173: ("28-byte binary block", "prefix 01 00 04 00 10 00"),
    0x0B4004E1: ("absent (offset 0xFFFFFFFF)", "never has a block"),
    0x0B4004E3: ("record: u32 count + count x u32 uid",
                 "PROJECT_B sample 01 00 00 00 + one uid; 16 B variant = 2 uids"),
    0x0B4004E4: ("record: u32 count + count x u32 uid",
                 "PROJECT_A row 0: 01 00 00 00 + uid 8166"),
    0x0B4004E5: ("8 B: u32 1, u32 0xFFFF0001", "constant on every row that has it"),
    0x0B40050C: ("8-byte LE scalar", "3 rows in PROJECT_A"),
    0x0B400512: ("i32 LE = 0", "zero in all 73 PROJECT_B task rows"),
    0x0B400563: ("i32 LE = -1 (sentinel)", "constant -1 on every row"),
    0x0B400564: ("i32 LE = -1 (sentinel)", "constant -1 on every row"),
    0x0B4005A1: ("absent (offset 0xFFFFFFFF)",
                 "only in the 0x80000000|uid family; never has a block"),
}

# Payload families used to check that a name and its bytes agree.
TEXT_NAMES = {"Name", "WBS", "Contact", "Project", "ResourceNames",
              "ResourceInitials", "Indicators", "OutlineNumber",
              "UniqueIDPredecessors", "UniqueIDSuccessors", "Status",
              "StatusIndicator", "ActivityId", "BarName"}
DATE_SUFFIX = ("Start", "Finish", "Date", "Deadline")
DOUBLE_MARK = ("Cost", "Work", "Peak")


def pascal(s):
    return "".join(p.capitalize() for p in s.split("_"))


def load_mpxj():
    out = {}
    if os.path.exists(MPXJ):
        with open(MPXJ, encoding="utf-8") as f:
            f.readline()          # id / task_field header
            for line in f:
                i, n = line.rstrip("\n").split("\t")
                out[int(i)] = pascal(n)
    return out


def expected_shape(name):
    if name is None:
        return None
    if name in TEXT_NAMES or "Text" in name or name.startswith("Hyperlink"):
        return "text"
    if name.endswith("Units"):
        return "u16"
    if "Accrual" in name or "RateTable" in name:
        return "u16"
    if any(name.endswith(s) for s in DATE_SUFFIX):
        return "i32"
    if any(m in name for m in DOUBLE_MARK) and "Variance" not in name:
        return "scalar8"
    return None


def dominant(shapes):
    if not shapes:
        return "none"
    return max(shapes.items(), key=lambda kv: kv[1])[0]


def encoding(key, names_, shapes):
    if key in UNKNOWN_NOTES:
        return UNKNOWN_NOTES[key][0]
    tot = sum(shapes.values())
    parts = []
    order = ["text", "text(empty)", "rtf", "i32", "u16", "pair/double",
             "quad", "bytes", "nodata"]
    label = {"text": "UTF-16LE text", "text(empty)": "UTF-16LE text (empty)",
             "rtf": "8-bit RTF", "i32": "i32 LE", "u16": "u16 LE",
             "pair/double": "8-byte LE scalar", "quad": "16-byte (4x u32 / GUID)",
             "bytes": "binary", "nodata": "absent (offset 0xFFFFFFFF)"}
    for s in order:
        n = shapes.get(s, 0)
        if not n:
            continue
        hint = ""
        if s == "pair/double" and names_ and any(m in names_ for m in DOUBLE_MARK):
            hint = " (double: work/cost)"
        if s == "i32" and names_ and any(names_.endswith(x) for x in DATE_SUFFIX):
            hint = " (packed date)"
        if s == "u16" and names_ and names_.endswith("Units"):
            hint = " (time units)"
        parts.append(f"{label[s]}{hint} x{n}")
    if len(parts) == 1:
        return parts[0]
    return "mixed: " + ", ".join(parts[:4])


def main():
    files = dict(FILES)
    stats = {}          # key -> merged stats across files
    per_file = {}
    for name, path in FILES:
        if not os.path.exists(path):
            continue
        st = key_stats(load_task(path))
        per_file[name] = st
        for k, v in st.items():
            d = stats.setdefault(k, {"n": 0, "bytes": 0, "shapes":
                                     __import__("collections").Counter(),
                                     "files": []})
            d["n"] += v["n"]
            d["bytes"] += v["bytes"]
            d["shapes"].update(v["shapes"])
            d["files"].append(name)

    catalog = merged_names()
    mpxj = load_mpxj()
    rows = []
    for k in sorted(stats):
        low = k & 0xFFFF
        name = catalog.get(k)
        source = "catalog" if name else ""
        if not name and low in mpxj:
            name = mpxj[low]
            source = "mpxj"
        elif name and low in mpxj:
            source = "catalog+mpxj"
        s = stats[k]
        sh = s["shapes"]

        # confidence
        if k in PROVEN:
            conf = "proven"
        elif name is None:
            conf = "none"
        elif source == "mpxj" and k not in per_file.get("project_a", {}) and \
                k not in per_file.get("project_b", {}):
            conf = "medium"
        elif source == "catalog+mpxj":
            exp = expected_shape(name)
            got = dominant(sh)
            ok = (exp is None or
                  (exp == "text" and got in ("text", "rtf", "text(empty)")) or
                  (exp == "u16" and got in ("u16", "nodata")) or
                  (exp == "i32" and got in ("i32", "nodata")) or
                  (exp == "scalar8" and got in ("pair/double", "nodata")))
            conf = "high" if ok else "medium"
        else:
            conf = "medium"

        # evidence
        bits = []
        if k in PROVEN:
            bits.append(PROVEN[k][1])
        project_a = per_file.get("project_a", {}).get(k)
        ten = per_file.get("project_b", {}).get(k)
        base = per_file.get("base", {}).get(k)
        for nm, d in (("PROJECT_A", project_a), ("TND", ten), ("base", base)):
            if d:
                bits.append(f"{nm} {d['n']}x{dominant(d['shapes'])}")
        if source:
            bits.append("name=" + ("repo PjField tables + mpxj"
                                   if source == "catalog+mpxj"
                                   else "mpxj FIELD_ARRAY" if source == "mpxj"
                                   else "repo PjField tables"))
        else:
            bits.append("no name in either table")
        if k in UNKNOWN_NOTES:
            bits.append("observed " + UNKNOWN_NOTES[k][1])
        if project_a and project_a["shapes"].get("nodata"):
            bits.append("some rows have no block")
        evidence = "; ".join(bits)

        # project-row-only note
        if project_a and project_a["n"] and all(m == 0 for m in project_a["mids"]):
            evidence += "; PROJECT_A row 0 (project) only"

        rows.append({
            "record_type": "Task",
            "file_key": "0x%08x" % k,
            "candidate_name": name or "",
            "value_encoding": encoding(k, name, sh),
            "evidence": evidence,
            "confidence": conf,
        })

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    cols = ["record_type", "file_key", "candidate_name", "value_encoding",
            "evidence", "confidence"]
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(cols) + "\n")
        for r in rows:
            f.write("\t".join(r[c] for c in cols) + "\n")
    print(f"wrote {OUT}: {len(rows)} keys")
    counts = {}
    for r in rows:
        counts[r["confidence"]] = counts.get(r["confidence"], 0) + 1
    print("confidence:", counts)


if __name__ == "__main__":
    main()
