"""Shared helpers for the W1 container probes (Task/Rsc table streams).

Everything here is read-only and depends only on `olefile`. The probes that use
it are `probe_fixedmeta.py`, `probe_varmeta.py`, `probe_fixtures.py` and
`probe_names.py`; the findings they produce are written up in
`spec/mpp-records.md`.

Stream paths inside an .mpp carry space-padded storage names (`'   114'`), so
`load()` normalises the first path component by stripping spaces.
"""

import struct

import olefile

# Task fixed table constants, established by the n1..n6 / base fixtures.
FIXED_HEADER = 48          # bytes before the first 202-byte Task row
FIXED_ROW = 202            # bytes per Task row (project row included)
META_HEADER = 16           # FixedMeta / Fixed2Meta prefix
META_ENTRY = 47            # FixedMeta bytes per record
META2_ENTRY = 96           # Fixed2Meta bytes per record
FIXED2_ENTRY = 64          # Fixed2Data bytes per record
VAR_HEADER = 32            # VarMeta prefix
VAR_ENTRY = 12             # VarMeta entry: field_id, mid, offset

# Packed date/time: u32 = (day << 16) | (seconds-since-midnight / 6).
# Day 0 is 1983-12-31 (verified: day 15346 == 2026-01-05, see probe_fixtures).
DATE_EPOCH_ORDINAL = 0     # marker; see decode_date()


def load(path):
    """Return {(storage, table, stream): bytes} with storage names de-spaced."""
    ole = olefile.OleFileIO(path)
    try:
        out = {}
        for parts in ole.listdir(streams=True):
            key = tuple(p.strip() for p in parts)
            out[key] = ole.openstream(parts).read()
        return out
    finally:
        ole.close()


def stream(streams, table, name, storage="114"):
    """Fetch one table stream, raising a clear error when it is absent."""
    key = (storage, table, name)
    if key not in streams:
        raise KeyError(f"no stream {storage}/{table}/{name}")
    return streams[key]


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def rows_of(fixed_data):
    """Split FixedData into the 48-byte header and a list of 202-byte rows."""
    body = fixed_data[FIXED_HEADER:]
    n = len(body) // FIXED_ROW
    if len(body) % FIXED_ROW:
        raise ValueError(
            f"FixedData {len(fixed_data)} B is not 48 + 202*n"
            f" (remainder {len(body) % FIXED_ROW})")
    return fixed_data[:FIXED_HEADER], [body[i * FIXED_ROW:(i + 1) * FIXED_ROW]
                                      for i in range(n)]


def parse_fixed_meta(fixed_meta, row_count=None):
    """Decode FixedMeta into (header_dict, [entry_dict...]).

    Layout (verified across fixtures and both real files):
        16-byte header: magic, 4, entry_count, fixed_data_size
        entry_count x 47-byte entries.

    The first three entries are not rows: their +4 word is 0, 16, 32 and they
    describe the three 16-byte slots of the FixedData 48-byte header. Every
    later entry has +4 = 48 + 202*row (the row's byte offset).
    """
    magic = u32(fixed_meta, 0)
    if magic != 0xFADFADBA:      # stream bytes are ba ad df fa
        raise ValueError(f"FixedMeta magic {magic:08x}")
    header = {
        "version": u32(fixed_meta, 4),
        "entry_count": u32(fixed_meta, 8),
        "fixed_data_size": u32(fixed_meta, 12),
    }
    n = (len(fixed_meta) - META_HEADER) // META_ENTRY
    if META_HEADER + n * META_ENTRY != len(fixed_meta):
        raise ValueError(f"FixedMeta {len(fixed_meta)} B is not 16 + 47*n")
    entries = []
    for i in range(n):
        b = fixed_meta[META_HEADER + i * META_ENTRY:
                       META_HEADER + (i + 1) * META_ENTRY]
        entries.append({
            "index": i,
            "pad": u16(b, 0),
            "count": u16(b, 2),          # small u16; correlates with stored fields
            "offset": u32(b, 4),         # into FixedData
            "blob": b[8:24],             # 16 B, looks like a row/save identity
            "tail": b[24:47],
            "raw": b,
        })
    return header, entries


def family_entries(entries):
    """Split FixedMeta entries into the 3 header descriptors and row entries."""
    head, body = entries[:3], entries[3:]
    for e in body:
        off = e["offset"]
        e["row"] = (off - FIXED_HEADER) // FIXED_ROW
        if FIXED_HEADER + e["row"] * FIXED_ROW != off:
            e["row"] = None
    return head, body


def parse_var_meta(var_meta):
    """Decode VarMeta into (header_dict, [entry_dict...], split_entry).

    Layout:
        32-byte header: magic, 0, count+1, 0, 0, var2_size, 0, extra_offset
        N x 12-byte entries: field_id, mid, offset
        trailing u32: field_id of the 'extra' entry whose mid/offset live in
        the header at +0x18/+0x1c. In the fixtures that extra entry is the
        project row's name/title block.

    `count` (+8) equals N+1 in every consistent file except PROJECT_A, which has
    four more entries than its header claims (see spec anomalies).
    """
    magic = u32(var_meta, 0)
    if magic != 0xFADFADBA:      # stream bytes are ba ad df fa
        raise ValueError(f"VarMeta magic {magic:08x}")
    header = {
        "zero": u32(var_meta, 4),
        "entry_count_plus_one": u32(var_meta, 8),
        "var2_size": u32(var_meta, 0x14),
        "extra_mid": u32(var_meta, 0x18),
        "extra_offset": u32(var_meta, 0x1C),
    }
    n = (len(var_meta) - VAR_HEADER - 4) // VAR_ENTRY
    if VAR_HEADER + n * VAR_ENTRY + 4 != len(var_meta):
        raise ValueError(f"VarMeta {len(var_meta)} B is not 32 + 12*n + 4")
    entries = []
    for i in range(n):
        field_id, mid, offset = struct.unpack_from(
            "<3I", var_meta, VAR_HEADER + i * VAR_ENTRY)
        entries.append({"index": i, "field_id": field_id, "mid": mid,
                        "offset": offset})
    split = {"field_id": u32(var_meta, len(var_meta) - 4),
             "mid": header["extra_mid"], "offset": header["extra_offset"]}
    return header, entries, split


def read_block(var2_data, offset):
    """Return the payload of the length-prefixed block at `offset`.

    A block is `u32 byte_length` followed by exactly that many bytes. Payload
    encodings observed: UTF-16LE NUL-terminated text, 4/8-byte integers,
    RTF/ANSI bytes (Notes), and 16-byte groups of four u32s.
    """
    if offset + 4 > len(var2_data):
        raise ValueError(f"block offset {offset} past Var2Data")
    length = u32(var2_data, offset)
    end = offset + 4 + length
    if end > len(var2_data):
        raise ValueError(f"block at {offset} runs past Var2Data")
    return var2_data[offset + 4:end]


def decode_text(payload):
    """UTF-16LE, NUL-terminated; used by Task.Name (field 0x0b400564)."""
    return payload.decode("utf-16le", errors="replace").rstrip("\x00")


def var_groups(entries):
    """Group VarMeta entries by `mid` (== the row's UniqueID) in file order."""
    groups = {}
    order = []
    for e in entries:
        if e["mid"] not in groups:
            groups[e["mid"]] = []
            order.append(e["mid"])
        groups[e["mid"]].append(e)
    return order, groups


def decode_date(value):
    """Decode a packed date/time u32 into (day_number, seconds_of_day, days).

    value = (day << 16) | (seconds_of_day // 6). Day 15346 is 2026-01-05 in the
    fixtures; seconds_of_day 4800 == 08:00 and 10200 == 17:00 (working-day
    boundaries), which fixes the tick at 6 seconds.
    """
    day = value >> 16
    seconds = (value & 0xFFFF) * 6
    return day, seconds, f"{seconds // 3600:02d}:{seconds % 3600 // 60:02d}"
