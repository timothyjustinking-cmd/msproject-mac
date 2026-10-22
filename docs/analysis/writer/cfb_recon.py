"""Structural reconnaissance of .mpp CFB containers, for the writer.

Reads a CFB file with nothing but `struct` (so the numbers can be checked
against the C++ writer in core/mppwrite/ without trusting olefile) and reports
everything a byte-identical re-emitter has to reproduce:

  * header fields, DIFAT (in-header and in continuation sectors), FAT;
  * which sectors are allocated, free, or past the end of the FAT;
  * the directory tree: entry order, colours, links, names, sizes, starts;
  * per-stream chains, slack in the final sector, and the mini stream layout;
  * bytes between the end of the last used sector and EOF.

Usage:
    python docs/analysis/writer/cfb_recon.py corpus/probe/base.mpp [more.mpp]

Read-only.  Evidence for spec/mpp-writer.md.
"""

import struct
import sys

SECTOR_FREE = 0xFFFFFFFF
SECTOR_END = 0xFFFFFFFE
SECTOR_FATSECT = 0xFFFFFFFD
SECTOR_DIFSECT = 0xFFFFFFFC
SECTOR_MAXREG = 0xFFFFFFFA
NO_STREAM = 0xFFFFFFFF


class Cfb:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.data = fh.read()
        self.path = path
        d = self.data
        if d[:8] != b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1":
            raise ValueError("not a CFB file: " + path)
        (self.minor,
         self.major,
         self.byte_order,
         self.sector_shift,
         self.mini_shift,
         self.num_dir_sectors,
         self.num_fat_sectors,
         self.dir_start,
         _txn,
         self.mini_cutoff,
         self.minifat_start,
         self.num_minifat_sectors,
         self.difat_start,
         self.num_difat_sectors,
         ) = struct.unpack_from("<5H6x9I", d, 0x18)
        self.sector_size = 1 << self.sector_shift
        self.mini_size = 1 << self.mini_shift
        self.difat_header = struct.unpack_from("<109I", d, 0x4C)
        self._read_difat()
        self._read_fat()
        self._read_minifat()
        self._read_dir()

    # -- sectors ---------------------------------------------------------
    def sector_off(self, sid):
        return (sid + 1) * self.sector_size

    def sector(self, sid):
        off = self.sector_off(sid)
        return self.data[off:off + self.sector_size]

    def chain(self, start):
        out = []
        sid = start
        seen = set()
        while sid < SECTOR_MAXREG:
            if sid in seen:
                out.append(("CYCLE", sid))
                break
            seen.add(sid)
            out.append(sid)
            sid = self.fat[sid]
        return out

    # -- structures ------------------------------------------------------
    def _read_difat(self):
        self.difat = list(self.difat_header)
        sid = self.difat_start
        extra = []
        while sid < SECTOR_MAXREG:
            sec = self.sector(sid)
            ids = struct.unpack_from("<%dI" % (self.sector_size // 4), sec, 0)
            self.difat.extend(ids[:-1])
            extra.append((sid, ids[-1]))
            sid = ids[-1]
        self.difat_extra = extra

    def _read_fat(self):
        self.fat = []
        fat_sids = [s for s in self.difat
                    if s < SECTOR_MAXREG][:self.num_fat_sectors]
        self.fat_sids = fat_sids
        for sid in fat_sids:
            self.fat.extend(struct.unpack_from(
                "<%dI" % (self.sector_size // 4), self.sector(sid), 0))

    def _read_minifat(self):
        self.minifat = []
        if self.minifat_start < SECTOR_MAXREG:
            for sid in self.chain(self.minifat_start):
                if isinstance(sid, tuple):
                    break
                self.minifat.extend(struct.unpack_from(
                    "<%dI" % (self.sector_size // 4), self.sector(sid), 0))

    def _read_dir(self):
        raw = b"".join(self.sector(s) for s in self.chain(self.dir_start)
                       if not isinstance(s, tuple))
        self.dir_raw_len = len(raw)
        self.entries = []
        for i in range(len(raw) // 128):
            e = raw[i * 128:(i + 1) * 128]
            name_len = struct.unpack_from("<H", e, 64)[0]
            name = e[:max(0, name_len - 2)].decode("utf-16le", "replace")
            left, right, child = struct.unpack_from("<3I", e, 68)
            self.entries.append({
                "index": i,
                "name": name,
                "name_raw": e[:64],
                "name_len": name_len,
                "type": e[66],
                "color": e[67],
                "left": left,
                "right": right,
                "child": child,
                "clsid": e[80:96],
                "state": struct.unpack_from("<I", e, 96)[0],
                "ctime": struct.unpack_from("<Q", e, 100)[0],
                "mtime": struct.unpack_from("<Q", e, 108)[0],
                "start": struct.unpack_from("<I", e, 116)[0],
                "size": struct.unpack_from("<Q", e, 120)[0],
                "slack": e[name_len:128] if 0 < name_len <= 64 else b"",
            })

    def walk(self, index=0, prefix="", out=None):
        if out is None:
            out = []
        e = self.entries[index]
        path = prefix + "/" + e["name"] if e["name"] else prefix
        if index != 0:
            out.append((path, e))
        child = e["child"]
        stack = [child] if child < NO_STREAM else []
        # walk siblings in left/right order via the child link
        pending = [child] if child < NO_STREAM else []
        while pending:
            sib = pending.pop()
            if sib >= NO_STREAM:
                continue
            se = self.entries[sib]
            p = prefix + "/" + e["name"] if e["name"] else prefix
            out.append((p + "/" + se["name"], se))
            if se["left"] < NO_STREAM:
                pending.append(se["left"])
            if se["right"] < NO_STREAM:
                pending.append(se["right"])
            if se["child"] < NO_STREAM and se["type"] == 1:
                pending.append(se["child"])
        return out

    def stream_bytes(self, entry):
        if entry["type"] != 2:
            return b""
        if entry["size"] >= self.mini_cutoff:
            raw = b"".join(self.sector(s) for s in self.chain(entry["start"])
                           if not isinstance(s, tuple))
            return raw[:entry["size"]]
        mini = self.mini_stream
        out = bytearray()
        sid = entry["start"]
        seen = set()
        while sid < SECTOR_MAXREG and sid not in seen:
            seen.add(sid)
            out += mini[sid * self.mini_size:(sid + 1) * self.mini_size]
            sid = self.minifat[sid]
        return bytes(out[:entry["size"]])

    @property
    def mini_stream(self):
        if getattr(self, "_mini", None) is None:
            root = self.entries[0]
            data = b"".join(self.sector(s) for s in self.chain(root["start"])
                            if not isinstance(s, tuple))
            self._mini = data[:root["size"]]
        return self._mini


def report(path):
    c = Cfb(path)
    print("=" * 78)
    print(path)
    print("  file size          %d (0x%x), %% %d = %d" % (
        len(c.data), len(c.data), c.sector_size,
        len(c.data) % c.sector_size))
    print("  version            %d.%d  sector %d  mini %d  cutoff %d" % (
        c.major, c.minor, c.sector_size, c.mini_size, c.mini_cutoff))
    print("  num_dir_sectors    %d" % c.num_dir_sectors)
    print("  num_fat_sectors    %d  (fat sids %s)" % (
        c.num_fat_sectors, c.fat_sids))
    print("  dir_start          %d  chain %s" % (c.dir_start,
                                                 c.chain(c.dir_start)))
    print("  minifat            start %d count %d  chain %s" % (
        c.minifat_start, c.num_minifat_sectors, c.chain(c.minifat_start)))
    print("  difat              start %d count %d  header[0:4]=%s" % (
        c.difat_start, c.num_difat_sectors, c.difat_header[:4]))
    print("  difat extra        %s" % (c.difat_extra,))

    total_sectors = (len(c.data) - 512) // c.sector_size
    print("  sectors in file    %d" % total_sectors)
    print("  FAT entries        %d" % len(c.fat))
    used, free_in_fat, special = [], [], []
    for sid, val in enumerate(c.fat):
        if val == SECTOR_FREE:
            free_in_fat.append(sid)
        elif val >= SECTOR_MAXREG:
            special.append((sid, val))
        else:
            used.append(sid)
    print("  FAT: next<MAX %d, FREE %d, special %d" % (
        len(used), len(free_in_fat), len(special)))
    free_inside = [s for s in free_in_fat if s < total_sectors]
    free_past = [s for s in free_in_fat if s >= total_sectors]
    print("  FREE inside file   %d %s" % (len(free_inside), free_inside[:20]))
    print("  FREE past EOF      %d %s" % (len(free_past), free_past[:8]))
    print("  special sids       %s%s" % (
        special[:12], "..." if len(special) > 12 else ""))

    # which sectors are reachable from a chain?
    reachable = set()

    def mark(start):
        for sid in c.chain(start):
            if not isinstance(sid, tuple):
                reachable.add(sid)

    mark(c.dir_start)
    if c.minifat_start < SECTOR_MAXREG:
        mark(c.minifat_start)
    for sid in c.fat_sids:
        reachable.add(sid)
    for sid, nxt in c.difat_extra:
        reachable.add(sid)
    stack = [0]
    while stack:
        idx = stack.pop()
        e = c.entries[idx]
        if e["type"] == 2:
            mark(e["start"])
        elif e["type"] == 5:
            mark(e["start"])
        for link in ("left", "right", "child"):
            if e[link] < NO_STREAM:
                stack.append(e[link])

    unreachable = [s for s in range(total_sectors) if s not in reachable]
    print("  reachable sectors  %d of %d" % (len(reachable), total_sectors))
    print("  UNREACHABLE        %d %s" % (
        len(unreachable), unreachable[:20]))
    if unreachable:
        for sid in unreachable[:8]:
            sec = c.sector(sid)
            print("     sid %4d nonzero=%d first16=%s" % (
                sid, sum(1 for b in sec if b), sec[:16].hex()))
    past = [s for s in reachable if s >= total_sectors]
    print("  chains past EOF    %s" % (past,))

    # directory
    print("  dir bytes          %d = %d entries (sectors %d)" % (
        c.dir_raw_len, len(c.entries), c.dir_raw_len // c.sector_size))
    live = [e for e in c.entries if e["type"] != 0]
    print("  live entries       %d, empty slots %d" % (
        len(live), len(c.entries) - len(live)))
    root = c.entries[0]
    print("  root: size %d start %d chainlen %d" % (
        root["size"], root["start"], len(c.chain(root["start"]))))
    rootraw = c.sector(root["start"]) if root["size"] else b""
    mini_used = sum(mini_len(e) for e in live if e["type"] == 2
                    and e["size"] < c.mini_cutoff)
    print("  mini stream: size %d, bytes used by streams %d, slack %d" % (
        root["size"], mini_used, root["size"] - mini_used))
    for e in live:
        if e["type"] == 5:
            continue
        if e["type"] == 2:
            ch = c.chain(e["start"]) if e["size"] >= c.mini_cutoff else []
            data = c.stream_bytes(e)
            slack = ""
            if ch and not isinstance(ch[-1], tuple):
                cap = len(ch) * c.sector_size
                slack = " slack %d" % (cap - e["size"])
            print("  %-8s %-46s start %5d size %8d%s" % (
                "DIR" if e["type"] == 1 else "stream", e["name"][:46],
                e["start"], e["size"], slack))
        else:
            print("  %-8s %-46s child %d" % ("storage", e["name"][:46],
                                             e["child"]))
    # entries with non-empty slack after the name
    dirty = [(e["index"], e["name"]) for e in c.entries
             if any(b for b in e.get("slack", b""))]
    print("  dir entries with non-zero slack after name: %d %s" % (
        len(dirty), dirty[:8]))
    # tail bytes
    tail_start = (total_sectors + 1) * c.sector_size
    tail = c.data[tail_start:]
    print("  bytes after last sector: %d %s" % (
        len(tail), tail[:16].hex()))
    return c


def mini_len(entry):
    return entry["size"]


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        report(arg)
