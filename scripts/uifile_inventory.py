#!/usr/bin/env python3
"""Build the UIFILE inventory: resource id -> likely UI surface.

Evidence used per blob:
  * the LDXF class record (class name after the 4-byte 'LDXF' tag), when present
  * every identifier recovered from the leading 0x01 <u16 len> <ascii> pool
    (and the u8-length pool used in LDXF records)
  * absence of UTF-16 text (checked here, not assumed)

The "surface" column is an explicit inference from those names, with a
confidence marker.  It is not read from the binary; see spec/ui-inventory.md.

Usage:
  python scripts/uifile_inventory.py [--res RESDIR] [--tsv out.tsv]
"""
from __future__ import annotations

import argparse
import csv
import pathlib
import re
import struct
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_RES = REPO_ROOT / "analysis" / "res" / "pjintl"
DEFAULT_TSV = REPO_ROOT / "docs" / "analysis" / "ui"

# Class-name rules win over keyword rules: the LDXF class is direct evidence.
CLASS_RULES = [
    ("CompareProjects", "Compare Projects legend pane", "high"),
    ("ReportsDialog", "Reports dialog", "high"),
    ("ReportPane", "Reports pane (visual report designer)", "high"),
    ("ReportField", "Report field settings", "high"),
    ("ImportProjectWizard", "Import Project wizard", "high"),
    ("ImportResourcesWizard", "Import Resources wizard", "high"),
    ("TrustCenter", "Trust Center", "high"),
    ("Options", "Options / calculation settings", "high"),
    ("Filter", "Filter menus and filter dialog", "high"),
    ("BarLabel", "Gantt bar label dialog", "high"),
    ("TaskBoard", "Task Board (board) view", "high"),
    ("SelectAgile", "Board setup dialog", "medium"),
    ("ProjectPrintSettings", "Page Setup dialog", "high"),
    ("OutDoc", "Save As / document properties", "medium"),
    ("OutSaveAs", "Save As", "medium"),
    ("GroupDS", "Group By dialog data source", "medium"),
    ("ResourceItem", "Resource picker / sheet data", "medium"),
    ("Deliverable", "Deliverables pane", "medium"),
    ("Inspector", "Assignment inspector", "medium"),
    ("Calendar", "Calendar options", "medium"),
    ("AlertDialog", "Alert dialog template", "medium"),
    ("ExternalTask", "External task link UI", "low"),
    ("TimePhased", "Time-phased format", "low"),
    ("Generic", "Generic list/choice control", "low"),
]

# Keyword rules run on the recovered identifier pool when the class is not enough.
RULES = [
    ("OfficeCenter", "Backstage / File view", "medium"),
    ("Backstage", "Backstage / File view", "high"),
    ("CardsList", "Board view card layout", "high"),
    ("TaskBoard", "Task Board (board) view", "high"),
    ("CompareProjects", "Compare Projects legend pane", "high"),
    ("ReportPane", "Reports pane (visual report designer)", "high"),
    ("ImportProjectWizard", "Import Project wizard", "high"),
    ("ImportResourcesWizard", "Import Resources wizard", "high"),
    ("TrustCenter", "Trust Center", "high"),
    ("Filter", "Filter menus and filter dialog", "high"),
    ("BarLabel", "Gantt bar label dialog", "high"),
    ("CalendarValues", "Calendar options", "medium"),
    ("PageSetup", "Page Setup dialog", "medium"),
    ("Comments", "Task comments", "medium"),
    ("Deliverable", "Deliverables pane", "medium"),
    ("OutDoc", "Save As / document properties", "medium"),
    ("Audience::", "Feature/audience config document", "low"),
]

PLACEHOLDER = re.compile(r"^(?:[A-Z]\d+|A\.B\.C|ADEFG|auto|best|center|left|right|top|bottom"
                         r"|horizontal|vertical|ascending|descending|none|default|normal)$",
                         re.I)


def u8_names(b: bytes):
    out = []
    for m in re.finditer(rb"([\x03-\xc8])([ -~]+)", b, re.S):
        n = m.group(1)[0]
        s = m.group(2)
        if len(s) >= n:
            cand = s[:n]
            if all(32 <= c < 127 for c in cand):
                txt = cand.decode("ascii")
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.:]*", txt):
                    out.append((m.start(), txt))
    return out


def prefix_names(b: bytes):
    out = []
    for m in re.finditer(rb"\x01(..)", b, re.S):
        n = struct.unpack("<H", m.group(1))[0]
        if 3 <= n <= 200:
            s = b[m.end() : m.end() + n]
            if len(s) == n and all(32 <= c < 127 for c in s):
                txt = s.decode("ascii")
                if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.:]*", txt):
                    out.append((m.start(), txt))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default=str(DEFAULT_RES))
    ap.add_argument("--tsv", default=str(DEFAULT_TSV / "uifile_inventory.tsv"))
    args = ap.parse_args()

    files = sorted((pathlib.Path(args.res) / "UIFILE").glob("*.bin"),
                   key=lambda p: int(p.stem.split("_")[0]))
    rows = []
    for p in files:
        b = p.read_bytes()
        rid = int(p.stem.split("_")[0])
        ldxf = b.find(b"LDXF\x00")
        pre, u8 = prefix_names(b), u8_names(b)
        cls = ""
        if ldxf >= 0:
            ln = b[ldxf + 5]
            cls = b[ldxf + 6 : ldxf + 6 + ln].decode("ascii", "replace")
        names = sorted({s for _, s in pre + u8}, key=str.lower)
        names = [n for n in names if not PLACEHOLDER.match(n)] or names
        surface, conf = "unidentified", "low"
        joined = " ".join(names)
        for kw, sfc, c in CLASS_RULES:
            if cls.startswith(kw):
                surface, conf = sfc, c
                break
        for kw, sfc, c in ([] if surface != "unidentified" else RULES):
            if kw in joined:
                surface, conf = sfc, c
        has_dlg = "DialogView" in names
        kind = "LDXF-class" if cls else ("lua+pool" if b.find(b"\x1bLua") >= 0 else "pool")
        rows.append(dict(
            rid=rid, size=len(b), kind=kind, cls=cls,
            names=names, n=len(names), surface=surface, conf=conf,
            dialog="yes" if has_dlg else "",
            lua=b.find(b"\x1bLua"),
        ))
        print(f"{rid:5} {len(b):7} {kind:9} cls={cls[:32]:32} "
              f"n={len(names):3} dlg={'Y' if has_dlg else '.'} "
              f"{conf:6} {surface}")

    with open(args.tsv, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["rid", "bytes", "kind", "class", "n_names", "has_dialog_view",
                    "lua_at", "surface", "confidence", "names"])
        for r in rows:
            w.writerow([r["rid"], r["size"], r["kind"], r["cls"], r["n"],
                        r["dialog"], r["lua"], r["surface"], r["conf"],
                        ";".join(r["names"])])
    print(f"\nwrote {args.tsv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
