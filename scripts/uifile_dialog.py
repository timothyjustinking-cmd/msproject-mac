#!/usr/bin/env python3
"""Decode the PJINTL RT_DIALOG resources as DLGTEMPLATEEX.

The heads in analysis/res/pjintl/index.tsv start
    01 00 ff ff 00000000 00000000 c400c880 <cdit> ...
i.e. dlgVer=1, signature=0xFFFF, helpID=0, exStyle=0, style=0x80C800C4.
That is DLGTEMPLATEEX, not the classic DLGTEMPLATE.  This script walks the
whole template and asserts that it consumes exactly the resource length.

Usage:
  python scripts/uifile_dialog.py [--res RESDIR] [--tsv out.tsv] [--dump]
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_RES = REPO_ROOT / "analysis" / "res" / "pjintl"

CLASS_ORD = {
    0x80: "Button",
    0x81: "Edit",
    0x82: "Static",
    0x83: "ListBox",
    0x84: "ScrollBar",
    0x85: "ComboBox",
}

# A few control styles worth naming in the inventory.
BUTTON_STYLE = {
    0x00000001: "BS_DEFPUSHBUTTON",
    0x00000002: "BS_CHECKBOX",
    0x00000003: "BS_AUTOCHECKBOX",
    0x00000004: "BS_RADIOBUTTON",
    0x00000005: "BS_3STATE",
    0x00000009: "BS_GROUPBOX",
    0x0000000B: "BS_AUTORADIOBUTTON",
    0x00000010: "BS_OWNERDRAW",
}


class Bad(Exception):
    pass


class Reader:
    def __init__(self, b: bytes):
        self.b = b
        self.o = 0

    def u8(self):
        v = self.b[self.o]
        self.o += 1
        return v

    def u16(self):
        if self.o + 2 > len(self.b):
            raise Bad(f"u16 past end at {self.o}")
        v = struct.unpack_from("<H", self.b, self.o)[0]
        self.o += 2
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.b, self.o)[0]
        self.o += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]
        self.o += 4
        return v

    def align4(self):
        self.o = (self.o + 3) & ~3
        if self.o > len(self.b):
            raise Bad("align past end")

    def sz(self) -> str:
        """menu/class/title: 0x0000, 0xFFFF+ordinal, or NUL-terminated UTF-16."""
        if self.o + 2 > len(self.b):
            raise Bad("sz past end")
        first = struct.unpack_from("<H", self.b, self.o)[0]
        if first == 0:
            self.o += 2
            return ""
        if first == 0xFFFF:
            self.o += 2
            ordv = self.u16()
            return f"#{ordv}"
        start = self.o
        while True:
            if self.o + 1 >= len(self.b):
                raise Bad("unterminated sz")
            if self.b[self.o] == 0 and self.b[self.o + 1] == 0:
                s = self.b[start : self.o].decode("utf-16le", "replace")
                self.o += 2
                return s
            self.o += 2


def parse(b: bytes) -> dict:
    r = Reader(b)
    dlgver = r.u16()
    sig = r.u16()
    if dlgver != 1 or sig != 0xFFFF:
        raise Bad(f"not DLGTEMPLATEEX: ver={dlgver} sig={sig:#x}")
    helpid = r.u32()
    exstyle = r.u32()
    style = r.u32()
    cdit = r.u16()
    x, y, cx, cy = r.i16(), r.i16(), r.i16(), r.i16()
    menu = r.sz()
    cls = r.sz()
    title = r.sz()
    font = None
    if style & 0x40:  # DS_SETFONT
        pts, weight, italic, charset = r.u16(), r.u16(), r.u8(), r.u8()
        face = r.sz()
        font = dict(points=pts, weight=weight, italic=italic,
                    charset=charset, face=face)
    items = []
    for _ in range(cdit):
        r.align4()
        ih = r.u32()
        iex = r.u32()
        ist = r.u32()
        ix, iy, icx, icy = r.i16(), r.i16(), r.i16(), r.i16()
        iid = r.u32()
        icls = r.sz()
        ititle = r.sz()
        extra = r.u16()
        data = b[r.o : r.o + extra]
        r.o += extra
        icls_name = CLASS_ORD.get(
            int(icls[1:], 0) if icls.startswith("#") else -1, icls
        )
        items.append(dict(
            id=iid, cls=icls_name, cls_raw=icls, text=ititle,
            x=ix, y=iy, cx=icx, cy=icy,
            style=f"{ist:#010x}", exstyle=f"{iex:#010x}", helpid=ih,
            extra=extra, data=data.hex(),
            button_style=BUTTON_STYLE.get(ist & 0xF, "") if icls_name == "Button" else "",
        ))
    r.align4()
    return dict(helpID=helpid, exStyle=exstyle, style=style, cdit=cdit,
                x=x, y=y, cx=cx, cy=cy, menu=menu, cls=cls, title=title,
                font=font, items=items, consumed=r.o, size=len(b))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--res", default=str(DEFAULT_RES))
    ap.add_argument("--tsv", default="")
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()

    root = pathlib.Path(args.res) / "RT_DIALOG"
    files = sorted(root.glob("*.bin"))
    rows = []
    ok = 0
    for p in files:
        b = p.read_bytes()
        try:
            d = parse(b)
        except Exception as e:  # noqa: BLE001 - report, do not crash the batch
            print(f"{p.name}: FAILED {e}")
            continue
        # Trailing bytes can be 0-3 alignment padding.
        pad = d["size"] - d["consumed"]
        status = "OK" if 0 <= pad <= 3 and not any(b[d["consumed"]:]) else "MISMATCH"
        if status == "OK":
            ok += 1
        print(f"{p.stem:16} {d['size']:5} cdit={d['cdit']:2} "
              f"style={d['style']:#010x} title={d['title']!r} "
              f"consumed={d['consumed']} pad={pad} {status}")
        if d["font"]:
            print(f"                 font={d['font']}")
        if args.dump or status != "OK":
            for it in d["items"]:
                print(f"    id={it['id']:5} {it['cls']:10} "
                      f"({it['x']:4},{it['y']:4},{it['cx']:3},{it['cy']:3}) "
                      f"text={it['text']!r} style={it['style']}")
        rows.append((p.stem, d, status))

    print(f"\n{ok}/{len(files)} dialogs parsed with exact consumption")
    if args.tsv:
        import csv

        with open(args.tsv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f, delimiter="\t")
            w.writerow(["dialog", "dlg_x", "dlg_y", "dlg_cx", "dlg_cy", "title",
                        "helpID", "ctl_id", "ctl_class", "ctl_x", "ctl_y",
                        "ctl_cx", "ctl_cy", "ctl_text", "ctl_style", "status"])
            for stem, d, status in rows:
                if not d["items"]:
                    w.writerow([stem, d["x"], d["y"], d["cx"], d["cy"],
                                d["title"], d["helpID"], "", "", "", "", "", "",
                                "", "", status])
                for it in d["items"]:
                    w.writerow([stem, d["x"], d["y"], d["cx"], d["cy"],
                                d["title"], d["helpID"], it["id"], it["cls"],
                                it["x"], it["y"], it["cx"], it["cy"],
                                it["text"], it["style"], status])
        print(f"wrote {args.tsv}")
    return 0 if ok == len(files) else 1


if __name__ == "__main__":
    raise SystemExit(main())
