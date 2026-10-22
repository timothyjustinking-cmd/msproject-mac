"""Check the assumptions a byte-identical CFB re-emitter depends on.

For every file given on the command line, verify:

  1. every sector in the file is reachable from the header/directory (no
     orphans), so a re-emitter never has to invent content for a sector;
  2. the FAT is exactly what the chain layout implies (chain links, FATSECT for
     FAT sectors, DIFSECT for DIFAT sectors, FREE elsewhere) - if this holds the
     writer can *derive* the FAT instead of copying it;
  3. the padding after a stream's last byte (inside its final sector) and the
     unused tail of the mini stream are all zero - if not, the writer must
     carry those bytes through;
  4. the mini FAT is what the mini chains imply, and mini sectors are handed out
     in first-fit order so the mini stream is a compaction of the mini streams;
  5. the directory sectors are exactly ceil(entries * 128 / sector), filled in
     entry order, and the free slots are zero except for the name length.

Usage:
    python docs/analysis/writer/cfb_assumptions.py corpus/probe/*.mpp
"""

import glob
import struct
import sys

sys.path.insert(0, "docs/analysis/writer")
from cfb_recon import Cfb, NO_STREAM, SECTOR_END, SECTOR_FREE, SECTOR_FATSECT, SECTOR_DIFSECT


def check(path):
    c = Cfb(path)
    problems = []
    total = (len(c.data) - 512) // c.sector_size

    # -- 1. reachability -------------------------------------------------
    reachable = set()

    def mark(start, kind):
        for sid in c.chain(start):
            if isinstance(sid, tuple):
                problems.append("cycle at %s" % (start,))
                continue
            reachable.add(sid)
            if sid >= total:
                problems.append("%s chain past EOF at %d" % (kind, sid))

    for sid in c.fat_sids:
        reachable.add(sid)
    if c.minifat_start < 0xFFFFFFFA:
        mark(c.minifat_start, "minifat")
    mark(c.dir_start, "dir")
    for sid, _nxt in c.difat_extra:
        reachable.add(sid)
    mark(c.entries[0]["start"], "ministream")
    for e in c.entries:
        if e["type"] == 2 and e["size"] >= c.mini_cutoff:
            mark(e["start"], e["name"])
    orphans = [s for s in range(total) if s not in reachable]
    if orphans:
        problems.append("orphan sectors %s" % (orphans[:10],))

    # -- 2. FAT derived from chains -------------------------------------
    derived = [SECTOR_FREE] * len(c.fat)
    for sid in c.fat_sids:
        derived[sid] = SECTOR_FATSECT
    for sid, _nxt in c.difat_extra:
        derived[sid] = SECTOR_DIFSECT

    def link(start, kind):
        chain = [s for s in c.chain(start) if not isinstance(s, tuple)]
        for a, b in zip(chain, chain[1:]):
            derived[a] = b
        if chain:
            derived[chain[-1]] = SECTOR_END
        return chain

    link(c.dir_start, "dir")
    if c.minifat_start < 0xFFFFFFFA:
        link(c.minifat_start, "minifat")
    link(c.entries[0]["start"], "ministream")
    for e in c.entries:
        if e["type"] == 2 and e["size"] >= c.mini_cutoff:
            link(e["start"], e["name"])
    if derived != c.fat:
        diff = [(i, c.fat[i], derived[i]) for i in range(len(c.fat))
                if c.fat[i] != derived[i]]
        problems.append("FAT differs from chains at %d places %s" % (
            len(diff), diff[:8]))

    # -- 3. padding ------------------------------------------------------
    nonzero_pad = []
    for e in c.entries:
        if e["type"] != 2 or e["size"] < c.mini_cutoff or e["size"] == 0:
            continue
        chain = [s for s in c.chain(e["start"]) if not isinstance(s, tuple)]
        cap = len(chain) * c.sector_size
        if cap == e["size"]:
            continue
        pad = b"".join(c.sector(s) for s in chain)[e["size"]:]
        nz = sum(1 for b in pad if b)
        if nz:
            nonzero_pad.append((e["name"], e["size"], cap - e["size"], nz))
    if nonzero_pad:
        problems.append("non-zero stream padding: %s" % (nonzero_pad[:6],))
    root = c.entries[0]
    mini = c.mini_stream
    used_mini = 0
    for e in c.entries:
        if e["type"] == 2 and e["size"] < c.mini_cutoff and e["size"]:
            chain = mini_chain(c, e)
            used_mini = max(used_mini, (max(chain) + 1) * c.mini_size if chain else 0)
    if root["size"] > used_mini:
        tail = mini[used_mini:]
        nz = sum(1 for b in tail if b)
        if nz:
            problems.append("non-zero mini-stream tail: %d of %d bytes" % (
                nz, len(tail)))

    # -- 4. mini FAT and mini allocation order ---------------------------
    mderived = [SECTOR_FREE] * len(c.minifat)
    def mlink(start):
        chain, sid, seen = [], start, set()
        while sid < 0xFFFFFFFA and sid not in seen:
            seen.add(sid)
            chain.append(sid)
            sid = c.minifat[sid]
        for a, b in zip(chain, chain[1:]):
            mderived[a] = b
        if chain:
            mderived[chain[-1]] = SECTOR_END
        return chain
    expected_start = 0
    for e in c.entries:
        if e["type"] == 2 and e["size"] and e["size"] < c.mini_cutoff:
            chain = mlink(e["start"])
            if chain and chain[0] != expected_start:
                problems.append("mini start %d for %r, expected %d" % (
                    chain[0], e["name"], expected_start))
            expected_start += (e["size"] + c.mini_size - 1) // c.mini_size
    if mderived != c.minifat:
        diff = [(i, c.minifat[i], mderived[i]) for i in range(len(c.minifat))
                if c.minifat[i] != mderived[i]]
        problems.append("miniFAT differs at %d places %s" % (len(diff), diff[:8]))

    # -- 5. directory ----------------------------------------------------
    live = [e for e in c.entries if e["type"] != 0]
    need = (len(c.entries) * 128 + c.sector_size - 1) // c.sector_size
    got = len(c.chain(c.dir_start))
    if need != got:
        problems.append("dir needs %d sectors, has %d (entries %d)" % (
            need, got, len(c.entries)))
    if c.num_dir_sectors not in (0, got):
        problems.append("num_dir_sectors %d vs %d" % (c.num_dir_sectors, got))
    empties = [e for e in c.entries if e["type"] == 0]
    if len(empties) > 1:
        problems.append("%d empty dir slots" % len(empties))
    for e in empties:
        raw = e["name_raw"] + b"\0" * 64
        if any(raw) or any([e["ctime"], e["mtime"]]) or e["slack"].strip(b"\0"):
            problems.append("empty dir slot %d is not zero" % e["index"])

    # -- 6. root entry naming -------------------------------------------
    if root["name"] != "Root Entry":
        problems.append("root name %r" % root["name"])

    print("%-46s sectors %4d  dir %3d  mini %6d  %s" % (
        path.split("\\")[-1].split("/")[-1], total, len(c.entries),
        root["size"],
        "OK" if not problems else "PROBLEMS"))
    for p in problems:
        print("    - " + p)
    return not problems


def mini_chain(c, entry):
    out, sid, seen = [], entry["start"], set()
    while sid < 0xFFFFFFFA and sid not in seen:
        seen.add(sid)
        out.append(sid)
        sid = c.minifat[sid]
    return out


if __name__ == "__main__":
    args = sys.argv[1:] or glob.glob("corpus/probe/*.mpp")
    ok = 0
    for arg in args:
        ok += check(arg)
    print("%d/%d clean" % (ok, len(args)))
