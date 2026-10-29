#!/usr/bin/env python3
"""Wave 34 (W34-0.1): stream-by-stream anatomy of one Project save pair.

A pair is three files Project 2021 saved in one session:

    <control>          the project before the edit, saved;
    <control>-null     the SAME control reopened and saved with NO edit;
    <edited>           the control, the one edit applied, then saved.

The null save is the noise mask (the W29-1.0b discipline in
tests/save_pair_noise.h): a byte that differs control -> null is per-save
noise; a byte that differs null -> edit at a position the mask does not cover
is the EDIT'S EFFECT, and the port must reproduce it byte for byte.

This tool prints the anatomy the decode notes cite: every changed stream, the
effect positions, the effect positions decoded to table rows, FixedMeta /
Fixed2Meta entries, VarMeta entries (uid, key, field name, offset) and Var2Data
blocks.  It never prints task text: Var2Data payloads that decode as text are
reported by length and shape only.

    python scripts/pair_anatomy.py --out spec/writers/evidence
    python scripts/pair_anatomy.py --pair wave30-textstyle --print

Run without -I: olefile lives in the user site-packages, which -I drops.
Everything here is read-only; the committed fixtures are never opened by
Project.
"""

import argparse
import os
import struct
import sys

import olefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "tests", "data")
FIELD_META = os.path.join(ROOT, "analysis", "field_metadata_all.tsv")

# pair key -> (control, null save, edited).  The null save is always
# "<control stem>-null.mpp" (save_pair::null_fixture_name).
PAIRS = {
    "wave30-assign-add": (
        "wave30-assign-add-control.mpp",
        "wave30-assign-add-control-null.mpp",
        "wave30-assign-add.mpp"),
    "wave30-assign-delete": (
        "wave30-assign-delete-control.mpp",
        "wave30-assign-delete-control-null.mpp",
        "wave30-assign-delete.mpp"),
    "wave30-baseline": (
        "wave30-baseline-control.mpp",
        "wave30-baseline-control-null.mpp",
        "wave30-baseline-set.mpp"),
    "wave30-calculate": (
        "wave30-calculate-control.mpp",
        "wave30-calculate-control-null.mpp",
        "wave30-calculate.mpp"),
    "wave30-inactivate": (
        "wave30-inactivate-control.mpp",
        "wave30-inactivate-control-null.mpp",
        "wave30-inactivate.mpp"),
    "wave30-inactivate-second": (
        "wave30-inactivate-second-control.mpp",
        "wave30-inactivate-second-control-null.mpp",
        "wave30-inactivate-second.mpp"),
    "wave30-insert-milestone": (
        "wave30-insert-milestone-control.mpp",
        "wave30-insert-milestone-control-null.mpp",
        "wave30-insert-milestone.mpp"),
    "wave30-insert-summary": (
        "wave30-insert-summary-control.mpp",
        "wave30-insert-summary-control-null.mpp",
        "wave30-insert-summary.mpp"),
    "wave30-level": (
        "wave30-level-control.mpp",
        "wave30-level-control-null.mpp",
        "wave30-level-all.mpp"),
    "wave30-move-back": (
        "wave30-move-back-control.mpp",
        "wave30-move-back-control-null.mpp",
        "wave30-move-back.mpp"),
    "wave30-move-forward": (
        "wave30-move-forward-control.mpp",
        "wave30-move-forward-control-null.mpp",
        "wave30-move-forward.mpp"),
    "wave30-notes": (
        "wave30-notes-control.mpp",
        "wave30-notes-control-null.mpp",
        "wave30-notes.mpp"),
    "wave30-resource-add": (
        "wave30-resource-add-control.mpp",
        "wave30-resource-add-control-null.mpp",
        "wave30-resource-add.mpp"),
    "wave30-resource-notes": (
        "wave30-resource-notes-control.mpp",
        "wave30-resource-notes-control-null.mpp",
        "wave30-resource-notes.mpp"),
    "wave30-resource-rename": (
        "wave30-resource-rename-control.mpp",
        "wave30-resource-rename-control-null.mpp",
        "wave30-resource-rename.mpp"),
    "wave30-respect-links-auto": (
        "wave30-respect-links-auto-control.mpp",
        "wave30-respect-links-auto-control-null.mpp",
        "wave30-respect-links-auto-after-first.mpp"),
    "wave30-respect-links-manual": (
        "wave30-respect-links-manual-control.mpp",
        "wave30-respect-links-manual-control-null.mpp",
        "wave30-respect-links-manual-after-first.mpp"),
    "wave30-task-mode-auto": (
        "wave30-task-mode-auto-control.mpp",
        "wave30-task-mode-auto-control-null.mpp",
        "wave30-task-mode-manual.mpp"),
    "wave30-task-mode-manual": (
        "wave30-task-mode-manual-control.mpp",
        "wave30-task-mode-manual-control-null.mpp",
        "wave30-task-mode-auto.mpp"),
    "wave30-textstyle": (
        "wave30-textstyle-control.mpp",
        "wave30-textstyle-control-null.mpp",
        "wave30-textstyle.mpp"),
    "wave30-timeline": (
        "wave30-timeline-control.mpp",
        "wave30-timeline-control-null.mpp",
        "wave30-timeline.mpp"),
    "wave30-wbs": (
        "wave30-wbs-control.mpp",
        "wave30-wbs-control-null.mpp",
        "wave30-wbs-code.mpp"),
    "wave31-fill": (
        "wave31-fill-control.mpp",
        "wave31-fill-control-null.mpp",
        "wave31-fill.mpp"),
}

# ---- table geometry (core/mpp/reader.cpp).  Rows exclude FixedData header.
TABLES = {
    "TBkndTask": {"header": 48, "row": 202, "uid": 4,
                  "meta_entry": 47, "meta_rows": 3,
                  "fixed2_header": 192, "fixed2_row": 64,
                  "fixed2_meta_entry": 96},
    "TBkndRsc": {"header": 48, "row": 172, "uid": 4,
                 "meta_entry": 37, "meta_rows": 3,
                 "fixed2_header": 0, "fixed2_row": 0,
                 "fixed2_meta_entry": 51},
    "TBkndAssn": {"header": 0, "row": 110, "uid": 0,
                  "meta_entry": 34, "meta_rows": 0,
                  "fixed2_header": 0, "fixed2_row": 48,
                  "fixed2_meta_entry": 53},
    "TBkndCons": {"header": 0, "row": 20, "uid": 0,
                  "meta_entry": 10, "meta_rows": 0,
                  "fixed2_header": 0, "fixed2_row": 0,
                  "fixed2_meta_entry": 0},
    "TBkndCal": {"header": 64, "row": 12, "uid": 8,
                 "meta_entry": 10, "meta_rows": 4,
                 "fixed2_header": 0, "fixed2_row": 0,
                 "fixed2_meta_entry": 0},
}

# 114/214 Props record ids named by core/mpp/reader.cpp (the rest print hex).
PROPS_NAMES = {
    0x35400000: "PASSWORD_FLAG",
    0x35400004: "PROTECTION_HASH",
    0x35400005: "WRITE_RESERVATION_HASH",
    0x35400007: "ENCRYPTION_CODE",
    0x3540000C: "VERSION",
    0x024000A2: "SUBPROJECT_DATA",
    0x02400084: "SUBPROJECT_COUNT",
    0x02400045: "STATUS_DATE",
    0x024013C8: "NEW_TASKS_MANUAL",
    0x03400009: "SUMMARY_ROW_SHOWN",
    0x03400008: "TIMELINE_PANE",
    0x3540000A: "EDIT_MARKER",
    0x3540000B: "SUMMARY_TITLE",
}

PREFIX_TABLE = {
    0x0B40: "Task", 0x0C40: "Resource", 0x0D40: "Calendar",
    0x0F40: "Assignment", 0x0740: "Project", 0x0A40: "Project",
}

MAX_BYTES_PER_RUN = 32
MAX_RUNS_PER_STREAM = 40
MAX_BLOCKS_LISTED = 60
MAX_RUNS_PER_PAYLOAD = 16
MAX_MIDDLE_HEX = 48


# ---------------------------------------------------------------- helpers


def u32(buf, off):
    if off < 0 or off + 4 > len(buf):
        return None
    return struct.unpack_from("<I", buf, off)[0]


def i32(buf, off):
    if off < 0 or off + 4 > len(buf):
        return None
    return struct.unpack_from("<i", buf, off)[0]


def hexs(buf, start=0, count=MAX_BYTES_PER_RUN):
    return buf[start:start + count].hex(" ")


def load_streams(path):
    ole = olefile.OleFileIO(path)
    try:
        out = {}
        for parts in ole.listdir(streams=True):
            # OLE stream names carry a leading control character (0x05 on the
            # property-set streams).  Drop control characters so the report
            # stays plain text and comparisons across files stay keyed alike.
            key = "/".join("".join(ch for ch in part if ord(ch) >= 32)
                           for part in parts)
            out[key] = ole.openstream(parts).read()
        return out
    finally:
        ole.close()


def normalise_record(raw, head, ident):
    """The save_pair_noise.h record normalisation, byte for byte.

    The C++ walks from the first byte and replaces the FIRST record that
    carries `ident`; it does not test uniqueness, so neither does this.
    """
    out = bytearray(raw)
    for i in range(min(head, len(out))):
        out[i] = 0
    at = raw.find(ident)
    if at < 0:
        return bytes(out)
    record = at - 4
    if record < 0 or record + 12 > len(raw):
        return bytes(out)
    length = u32(raw, record)
    if length is None or record + 12 + length > len(raw):
        return bytes(out)
    sentinel = (struct.pack("<I", 8) + raw[record + 4:record + 8] +
                struct.pack("<I", 0) + "PATH".encode("utf-16le"))
    return bytes(out[:record]) + sentinel + bytes(out[record + 12 + length:])


def normalise_props14(raw):
    return normalise_record(raw, 8, bytes.fromhex("08004035"))


def normalise_cedl(raw):
    return normalise_record(raw, 12, bytes.fromhex("31004026"))


def kind_for(path):
    if path.endswith("DocumentSummaryInformation"):
        return "exact"
    if path.endswith("Props14"):
        return "props14"
    if path.endswith("214/CEdl/Var2Data"):
        return "cedl"
    return "fixed"


def normalise(kind, raw):
    if kind == "props14":
        return normalise_props14(raw)
    if kind == "cedl":
        return normalise_cedl(raw)
    return raw


def runs(positions):
    """Group a sorted position list into contiguous [start, end) runs."""
    out = []
    for pos in positions:
        if out and pos == out[-1][1]:
            out[-1][1] = pos + 1
        else:
            out.append([pos, pos + 1])
    return out


def parse_blocks(buf):
    """Walk a Var2Data stream: u32 length then that many payload bytes.

    Mirrors core/viewstate/viewstate.cpp parse_var2().  Returns
    {offset: (length, payload)} and whether the walk consumed the stream
    exactly.  A stream that does not walk is reported, not guessed at.
    """
    blocks = {}
    offset = 0
    exact = True
    while offset < len(buf):
        if offset + 4 > len(buf):
            exact = False
            break
        length = u32(buf, offset)
        if length > len(buf) - offset - 4:
            exact = False
            break
        blocks[offset] = (length, buf[offset + 4:offset + 4 + length])
        offset += 4 + length
    return blocks, exact


def common_prefix(a, b):
    limit = min(len(a), len(b))
    i = 0
    while i < limit and a[i] == b[i]:
        i += 1
    return i


def common_suffix(a, b):
    limit = min(len(a), len(b))
    i = 0
    while i < limit and a[len(a) - 1 - i] == b[len(b) - 1 - i]:
        i += 1
    return i


def span_has_effect(a, start, end):
    """True when any unmasked null-vs-edit effect lies in [start, end)."""
    if start >= end:
        return False
    effects = a["effect_set"]
    return any(start <= p < end for p in effects)


def payload_delta(null, edit):
    """Compact structural description of one block payload change."""
    common = min(len(null), len(edit))
    diffs = [i for i in range(common) if null[i] != edit[i]]
    if len(null) != len(edit) and \
            (common == 0 or len(diffs) * 8 <= common):
        # The shorter payload is the longer one with an insertion/removal at
        # (or near) its end: only a handful of in-place bytes differ.
        out = ["common %d B: %d in-place byte(s) differ" % (common,
                                                            len(diffs))]
        for s, t in runs(diffs)[:MAX_RUNS_PER_PAYLOAD]:
            out.append("  run [%d:%d) null `%s` edit `%s`" % (
                s, t, hexs(null, s, min(t - s, MAX_BYTES_PER_RUN)),
                hexs(edit, s, min(t - s, MAX_BYTES_PER_RUN))))
        if len(runs(diffs)) > MAX_RUNS_PER_PAYLOAD:
            out.append("  ... %d more in-place runs" % (
                len(runs(diffs)) - MAX_RUNS_PER_PAYLOAD))
        if len(edit) > len(null):
            out.append("edit appends %d B after the common part: `%s`" % (
                len(edit) - len(null),
                safe_hex(edit[common:], 0,
                         min(len(edit) - common, MAX_MIDDLE_HEX))))
        else:
            out.append("edit removes %d B after the common part: `%s`" % (
                len(null) - len(edit),
                safe_hex(null[common:], 0,
                         min(len(null) - common, MAX_MIDDLE_HEX))))
        return out
    prefix = common_prefix(null, edit)
    suffix = common_suffix(null[prefix:], edit[prefix:])
    end_n = len(null) - suffix
    end_e = len(edit) - suffix
    mid_n = null[prefix:end_n]
    mid_e = edit[prefix:end_e]
    out = ["common prefix %d B, common suffix %d B; middle %d B -> %d B"
           % (prefix, suffix, len(mid_n), len(mid_e))]
    if len(mid_n) <= MAX_MIDDLE_HEX and len(mid_e) <= MAX_MIDDLE_HEX:
        out.append("middle null `%s`" % hexs(mid_n, 0, len(mid_n)))
        out.append("middle edit `%s`" % hexs(mid_e, 0, len(mid_e)))
        return out
    if len(mid_n) == len(mid_e):
        mid_diffs = [j for j in range(len(mid_n)) if mid_n[j] != mid_e[j]]
        for s, t in runs([prefix + j for j in mid_diffs])[:MAX_RUNS_PER_PAYLOAD]:
            out.append("  run [%d:%d) null `%s` edit `%s`" % (
                s, t, hexs(null, s, min(t - s, MAX_BYTES_PER_RUN)),
                hexs(edit, s, min(t - s, MAX_BYTES_PER_RUN))))
        if len(runs(mid_diffs)) > MAX_RUNS_PER_PAYLOAD:
            out.append("  ... %d more in-place runs" % (
                len(runs(mid_diffs)) - MAX_RUNS_PER_PAYLOAD))
    else:
        out.append("middle null starts `%s`" % hexs(mid_n, 0, 24))
        out.append("middle edit starts `%s`" % hexs(mid_e, 0, 24))
        if mid_n:
            out.append("middle null ends `%s`" % hexs(
                mid_n, max(0, len(mid_n) - 24), 24))
        if mid_e:
            out.append("middle edit ends `%s`" % hexs(
                mid_e, max(0, len(mid_e) - 24), 24))
    return out


def is_text_like(payload):
    if len(payload) < 2 or len(payload) % 2:
        return False
    try:
        text = payload.decode("utf-16le")
    except UnicodeDecodeError:
        return False
    if not text.endswith("\x00"):
        return False
    chars = [ch for ch in text if ch != "\x00"]
    if not chars:
        return False  # all NUL: a binary zero field, not text
    return all(ch.isprintable() for ch in chars)


def is_sensitive(payload):
    """A payload that could carry a name, note, path or title.

    The report must never print client task names, project titles, people's
    names or company paths, and a name can sit in a text, RTF or plain-ASCII
    payload.  Such payloads are reported by shape and length only.
    """
    if payload is None:
        return False
    if is_text_like(payload):
        return True
    if payload[:2] == b"{\\":
        return True
    if len(payload) >= 8:
        printable = sum(1 for b in payload
                        if 32 <= b < 127 or b in (9, 10, 13))
        if printable * 10 >= len(payload) * 9:
            return True
    return False


def safe_hex(payload, start=0, count=MAX_BYTES_PER_RUN):
    """Hex bytes, or a marker for a payload that could carry text.

    A text payload could carry a task name; the report never prints it, not
    even as code units.
    """
    if payload is None:
        return "(absent)"
    if is_sensitive(payload):
        return "(text-like payload, not shown)"
    return hexs(payload, start, count)


# ---------------------------------------------------------------- field names

class FieldNames:
    def __init__(self, path):
        self.by_id = {}
        if not os.path.exists(path):
            return
        with open(path, encoding="utf-8", errors="replace") as fh:
            header = fh.readline().rstrip("\n").split("\t")
            idx = {name: i for i, name in enumerate(header)}
            for line in fh:
                cols = line.rstrip("\n").split("\t")
                try:
                    fid = int(cols[idx["field_id_dec"]])
                except (KeyError, ValueError):
                    continue
                self.by_id.setdefault(fid, cols[idx["property_name"]])

    def name(self, key):
        table = PREFIX_TABLE.get((key >> 16) & 0xFFFF, "?")
        return self.by_id.get(key, ""), table


# ---------------------------------------------------------------- decoding


def parse_var_meta(buf):
    if len(buf) < 24:
        return None
    header = struct.unpack_from("<6I", buf, 0)
    n = (len(buf) - 24) // 12
    entries = []
    for i in range(n):
        uid, off, key = struct.unpack_from("<3I", buf, 24 + i * 12)
        entries.append({"index": i, "uid": uid, "offset": off, "key": key})
    return {"header": header, "entries": entries}


def span_change_is_effect(a, *spans):
    for span in spans:
        if not span:
            continue
        start, end = span
        if any(start <= p < end for p in a["effect_set"]):
            return True
        if any(start <= p < end for p in a["tail_effect_set"]):
            return True
    return False


def entry_slot(index):
    return (24 + index * 12, 24 + index * 12 + 12)


def var_meta_diff(a, names):
    """Markdown lines for one VarMeta stream.

    Effects are null -> edit (what the edit did beyond per-save churn); the
    control -> null entry churn is reported separately as noise.  An entry
    change whose bytes all sit at masked positions is noise, not an effect.
    """
    control, null, edit = a["c"], a["n"], a["e"]
    pc = parse_var_meta(control)
    pn = parse_var_meta(null)
    pe = parse_var_meta(edit)
    if pe is None:
        return ["  (VarMeta too short to parse)"]
    lines = []
    for label, parsed in (("control", pc), ("null", pn), ("edit", pe)):
        if parsed is not None:
            lines.append("  header %-7s %s" % (label, parsed["header"]))
    lines.append("  entries control %d, null %d, edit %d" % (
        len(pc["entries"]) if pc else 0,
        len(pn["entries"]) if pn else 0, len(pe["entries"])))

    def map_of(parsed):
        if parsed is None:
            return {}
        return {(e["uid"], e["key"]): e for e in parsed["entries"]}

    base = map_of(pn)
    after = map_of(pe)
    before = map_of(pc)
    added = [after[k] for k in after if k not in base]
    removed = [base[k] for k in base if k not in after]
    repointed = [(base[k], after[k]) for k in after
                 if k in base and base[k]["offset"] != after[k]["offset"]]
    added_effects = [e for e in added
                     if span_change_is_effect(a, entry_slot(e["index"]))]
    removed_effects = [e for e in removed
                       if span_change_is_effect(a, entry_slot(e["index"]))]
    repointed_effects = [(o, n) for o, n in repointed
                         if span_change_is_effect(a, entry_slot(n["index"]),
                                                  entry_slot(o["index"]))]
    if added_effects:
        lines.append("  EFFECT: entries ADDED (index, uid, key, name, offset):")
        for e in added_effects:
            name, table = names.name(e["key"])
            lines.append("    [%d] uid %d key 0x%08x %s/%s off %d" % (
                e["index"], e["uid"], e["key"], table, name or "?",
                e["offset"]))
            before_entry = (pe["entries"][e["index"] - 1] if e["index"] else
                            None)
            lines.append("    insertion point %d: neighbour before %s" % (
                e["index"],
                "none" if before_entry is None else
                "uid %d key 0x%08x" % (before_entry["uid"],
                                       before_entry["key"])))
    if len(added) > len(added_effects):
        lines.append("  entries ADDED at masked positions only (noise): %d" % (
            len(added) - len(added_effects)))
    if removed_effects:
        lines.append("  EFFECT: entries REMOVED (index, uid, key, name, "
                     "offset):")
        for e in removed_effects:
            name, table = names.name(e["key"])
            lines.append("    [%d] uid %d key 0x%08x %s/%s off %d" % (
                e["index"], e["uid"], e["key"], table, name or "?",
                e["offset"]))
    if len(removed) > len(removed_effects):
        lines.append("  entries REMOVED at masked positions only (noise): %d"
                     % (len(removed) - len(removed_effects)))
    if repointed_effects:
        deltas = sorted({new["offset"] - old["offset"]
                         for old, new in repointed_effects})
        lines.append("  EFFECT: entries RE-POINTED (same uid+key, offset "
                     "moved): %d, delta(s) %s" % (
                         len(repointed_effects), deltas))
        for old, new in repointed_effects[:40]:
            name, table = names.name(new["key"])
            lines.append("    uid %d key 0x%08x %s/%s off %d -> %d" % (
                new["uid"], new["key"], table, name or "?",
                old["offset"], new["offset"]))
        if len(repointed_effects) > 40:
            lines.append("    ... %d more" % (len(repointed_effects) - 40))
    if len(repointed) > len(repointed_effects):
        lines.append("  entries RE-POINTED at masked positions only (noise): "
                     "%d" % (len(repointed) - len(repointed_effects)))
    if not added_effects and not removed_effects and not repointed_effects:
        lines.append("  EFFECT: none")
    # Per-save identity churn: the same comparison control -> null.
    n_added = [k for k in base if k not in before]
    n_removed = [k for k in before if k not in base]
    n_repointed = [k for k in base if k in before and
                   before[k]["offset"] != base[k]["offset"]]
    lines.append("  noise (control -> null): entries %d added, %d removed, "
                 "%d re-pointed" % (len(n_added), len(n_removed),
                                    len(n_repointed)))
    order = [(e["uid"], e["key"]) for e in pe["entries"]]
    lines.append("  edit order sorted by (uid, key): %s" % (order == sorted(order)))
    return lines


def var2_entry_diff(a, control_meta, null_meta, edit_meta, names):
    """Payload-level Var2Data diff keyed by (uid, key).

    Each VarMeta entry names a block by its offset; the block payload is the
    bytes after the u32 length word (core/viewstate/viewstate.cpp parse_var2).
    An effect is a block whose payload differs null -> edit at an unmasked
    position.  A block that differs only control -> null is per-save noise and
    is counted, not listed.  A block that moved because an earlier block grew
    has the same payload and is not a change.
    """
    lines = []
    pc = parse_var_meta(control_meta)
    pn = parse_var_meta(null_meta)
    pe = parse_var_meta(edit_meta)
    if pe is None:
        return lines
    bc, exact_c = parse_blocks(a["c"])
    bn, exact_n = parse_blocks(a["n"])
    be, exact_e = parse_blocks(a["e"])
    lines.append("  Var2Data walk: control %d block(s)%s, null %d%s, edit %d%s"
                 % (len(bc), "" if exact_c else " (INEXACT)",
                    len(bn), "" if exact_n else " (INEXACT)",
                    len(be), "" if exact_e else " (INEXACT)"))

    def indexed(parsed):
        """key -> entries in stream order (a key may name two blocks)."""
        out = {}
        if parsed is None:
            return out
        for e in parsed["entries"]:
            out.setdefault((e["uid"], e["key"]), []).append(e)
        return out

    ic = indexed(pc)
    inn = indexed(pn)
    ie = indexed(pe)

    def block_at(blocks, entry):
        return blocks.get(entry["offset"])

    changed = []
    noise_only = 0
    missing = 0
    for key, els in sorted(ie.items(), key=lambda kv: kv[1][0]["index"]):
        nls = inn.get(key, [])
        cls = ic.get(key, [])
        for occurrence, el in enumerate(els):
            nl = nls[occurrence] if occurrence < len(nls) else None
            cl = cls[occurrence] if occurrence < len(cls) else None
            b_e = block_at(be, el)
            b_n = block_at(bn, nl) if nl is not None else None
            b_c = block_at(bc, cl) if cl is not None else None
            if b_e is None:
                missing += 1
                continue
            if nl is None:
                changed.append(("ADDED", key, cl, nl, el, b_c, b_n, b_e))
                continue
            if b_n is None:
                missing += 1
                continue
            payload_e, payload_n = b_e[1], b_n[1]
            if payload_e == payload_n:
                if b_c is not None and b_c[1] != payload_n:
                    noise_only += 1
                continue
            span_n = (nl["offset"] + 4, nl["offset"] + 4 + len(payload_n))
            span_e = (el["offset"] + 4, el["offset"] + 4 + len(payload_e))
            if not span_change_is_effect(a, span_n, span_e):
                noise_only += 1
                continue
            changed.append(("CHANGED", key, cl, nl, el, b_c, b_n, b_e))
    # Blocks in the null save that the edit save no longer has.
    for key, nls in sorted(inn.items(), key=lambda kv: kv[1][0]["index"]):
        els = ie.get(key, [])
        for occurrence in range(len(els), len(nls)):
            nl = nls[occurrence]
            b_n = block_at(bn, nl)
            cl = ic.get(key, [])
            cl = cl[occurrence] if occurrence < len(cl) else None
            b_c = block_at(bc, cl) if cl is not None else None
            if b_n is None:
                continue
            span_n = (nl["offset"] + 4, nl["offset"] + 4 + len(b_n[1]))
            if not span_change_is_effect(a, span_n):
                noise_only += 1
                continue
            changed.append(("REMOVED", key, cl, nl, None, b_c, b_n, None))
    if noise_only:
        lines.append("  Var2Data blocks with per-save noise only (masked "
                     "positions): %d" % noise_only)
    if missing:
        lines.append("  VarMeta entries whose named offset is not a block "
                     "start (not decoded): %d" % missing)
    if changed:
        lines.append("  EFFECT: Var2Data blocks changed (uid, key, name, "
                     "kind, offsets null -> edit):")
        for kind, (uid, key), cl, nl, el, b_c, b_n, b_e in \
                changed[:MAX_BLOCKS_LISTED]:
            name, table = names.name(key)
            off_n = "-" if nl is None else str(nl["offset"])
            off_e = "-" if el is None else str(el["offset"])
            len_n = "-" if b_n is None else str(b_n[0])
            len_e = "-" if b_e is None else str(b_e[0])
            lines.append("    %s uid %d key 0x%08x %s/%s block %s (%s B) -> "
                         "%s (%s B)" % (
                             kind, uid, key, table, name or "?", off_n, len_n,
                             off_e, len_e))
            if kind == "CHANGED":
                if is_sensitive(b_n[1]) or is_sensitive(b_e[1]):
                    lines.append("      text payload (not shown); lengths "
                                 "%d -> %d" % (len(b_n[1]), len(b_e[1])))
                else:
                    lines += ["      " + line for line in
                              payload_delta(b_n[1], b_e[1])]
            elif kind == "ADDED" and b_e is not None:
                lines.append("      edit payload %s (len %d)" % (
                    safe_hex(b_e[1], 0, 24), len(b_e[1])))
            elif kind == "REMOVED" and b_n is not None:
                lines.append("      null payload %s (len %d)" % (
                    safe_hex(b_n[1], 0, 24), len(b_n[1])))
        if len(changed) > MAX_BLOCKS_LISTED:
            lines.append("    ... %d more" % (len(changed) -
                                               MAX_BLOCKS_LISTED))
    if not changed and noise_only == 0 and not missing:
        lines.append("  EFFECT: no Var2Data payload changed")
    return lines


def parse_props(buf):
    """Props blocks: 16-byte header, u16 count at +12, records at 16.

    Records are back to back with NO padding and stop at the first id whose
    byte 2 is not 0x40 (core/props/props.cpp parse_props).  Everything after
    the last record is the tail.
    """
    if len(buf) < 16:
        return None
    records = []
    offset = 16
    while offset + 12 <= len(buf):
        length = u32(buf, offset)
        key = u32(buf, offset + 4)
        kind = u32(buf, offset + 8)
        if length is None or key is None or kind is None:
            break
        if (key & 0x00FF0000) != 0x00400000:
            break
        if length > len(buf) - offset - 12:
            break
        payload = buf[offset + 12:offset + 12 + length]
        records.append({"index": len(records), "offset": offset,
                        "length": length, "key": key, "kind": kind,
                        "payload": payload})
        offset += 12 + length
    return {"records": records, "tail_offset": offset}


def parse_typed_properties(buf, tail):
    """Tail typed properties: 16-byte entries (lead 4, key, lead 4, value)."""
    out = []
    pos = tail
    while pos + 16 <= len(buf):
        lead = u32(buf, pos)
        key = u32(buf, pos + 4)
        lead_two = u32(buf, pos + 8)
        value = u32(buf, pos + 12)
        if lead != 4 or lead_two != 4 or key == 0:
            break
        out.append({"offset": pos, "key": key, "value": value})
        pos += 16
    return out


def parse_field_refs(buf, tail):
    """Tail field-reference entries, as core/props/props.cpp scans them.

    A field id 0x0b40xxxx at pos names an entry at pos-8 holding
    index_word, length_word (4..4096), the field id and four words.
    """
    out = []
    pos = tail
    while pos + 4 <= len(buf):
        field_id = u32(buf, pos)
        if (field_id & 0xFFFF0000) != 0x0B400000 or pos < tail + 8:
            pos += 2
            continue
        entry = pos - 8
        if entry + 28 > len(buf):
            pos += 2
            continue
        length_word = u32(buf, entry + 4)
        if not 4 <= length_word <= 4096:
            pos += 2
            continue
        out.append({"offset": entry, "index_word": u32(buf, entry),
                    "length_word": length_word, "field_id": field_id,
                    "words": [u32(buf, entry + 12 + w * 4)
                              for w in range(4)]})
        pos += 2
    return out


def props_diff(a, table):
    lines = []
    pc = parse_props(a["c"])
    pn = parse_props(a["n"])
    pe = parse_props(a["e"])
    if pe is None:
        return ["  (Props stream too short to parse)"]
    rc, rn, re_ = pc["records"], pn["records"], pe["records"]
    lines.append("  records control %d, null %d, edit %d; tail offsets "
                 "control %d (%d B), null %d (%d B), edit %d (%d B)" % (
                     len(rc), len(rn), len(re_), pc["tail_offset"],
                     len(a["c"]) - pc["tail_offset"], pn["tail_offset"],
                     len(a["n"]) - pn["tail_offset"], pe["tail_offset"],
                     len(a["e"]) - pe["tail_offset"]))

    def record_span(record):
        return (record["offset"], record["offset"] + 12 + record["length"])

    def by_id(records):
        out = {}
        for record in records:
            out.setdefault(record["key"], []).append(record)
        return out

    kc = by_id(rc)
    kn = by_id(rn)
    ke = by_id(re_)
    noise_only = 0
    for key in sorted(ke):
        for occurrence, r in enumerate(ke[key]):
            name = PROPS_NAMES.get(key, "")
            olds = kn.get(key, [])
            if occurrence >= len(olds):
                if span_change_is_effect(a, record_span(r)):
                    lines.append(
                        "    EFFECT: ADDED id 0x%08x %s kind %d len %d at "
                        "offset %d payload %s" % (
                            key, name, r["kind"], r["length"], r["offset"],
                            safe_hex(r["payload"])))
                else:
                    noise_only += 1
                continue
            old = olds[occurrence]
            if old["payload"] == r["payload"] and \
                    old["kind"] == r["kind"]:
                continue
            if not span_change_is_effect(a, record_span(old),
                                         record_span(r)):
                noise_only += 1
                continue
            lines.append(
                "    EFFECT: CHANGED id 0x%08x %s kind %d len %d -> %d" % (
                    key, name, r["kind"], old["length"], r["length"]))
            if is_sensitive(old["payload"]) or is_sensitive(r["payload"]):
                lines.append("      text payload (not shown); lengths "
                             "%d -> %d" % (len(old["payload"]),
                                           len(r["payload"])))
            elif len(old["payload"]) <= 64 and len(r["payload"]) <= 64:
                lines.append("      null `%s`" % safe_hex(old["payload"]))
                lines.append("      edit `%s`" % safe_hex(r["payload"]))
            else:
                lines += ["      " + line for line in
                          payload_delta(old["payload"], r["payload"])]
    for key in sorted(kn):
        for occurrence in range(len(ke.get(key, [])), len(kn[key])):
            r = kn[key][occurrence]
            if span_change_is_effect(a, record_span(r)):
                lines.append("    EFFECT: REMOVED id 0x%08x %s kind %d len %d "
                             "payload %s" % (
                                 key, PROPS_NAMES.get(key, ""), r["kind"],
                                 r["length"], safe_hex(r["payload"])))
            else:
                noise_only += 1
    if noise_only:
        lines.append("    records changed only at masked positions (noise): "
                     "%d" % noise_only)

    # ---- tail: typed properties, then field-reference entries
    tc = parse_typed_properties(a["c"], pc["tail_offset"])
    tn = parse_typed_properties(a["n"], pn["tail_offset"])
    te = parse_typed_properties(a["e"], pe["tail_offset"])
    lines.append("  tail typed properties control %d, null %d, edit %d" % (
        len(tc), len(tn), len(te)))

    def typed_map(props):
        return {p["key"]: p for p in props}

    mc, mn, me = typed_map(tc), typed_map(tn), typed_map(te)
    typed_noise = 0
    for key in sorted(me):
        p = me[key]
        if key not in mn:
            if span_change_is_effect(a, (p["offset"], p["offset"] + 16)):
                lines.append("    EFFECT: tail property ADDED key 0x%08x "
                             "value %d" % (key, p["value"]))
            else:
                typed_noise += 1
        elif mn[key]["value"] != p["value"]:
            old = mn[key]
            if span_change_is_effect(a, (old["offset"], old["offset"] + 16),
                                     (p["offset"], p["offset"] + 16)):
                lines.append("    EFFECT: tail property key 0x%08x value "
                             "%d -> %d" % (key, old["value"], p["value"]))
            else:
                typed_noise += 1
    for key in sorted(mn):
        if key not in me:
            old = mn[key]
            if span_change_is_effect(a, (old["offset"], old["offset"] + 16)):
                lines.append("    EFFECT: tail property REMOVED key 0x%08x "
                             "value %d" % (key, old["value"]))
            else:
                typed_noise += 1
    if typed_noise:
        lines.append("    tail properties changed only at masked positions "
                     "(noise): %d" % typed_noise)

    fc = parse_field_refs(a["c"], pc["tail_offset"])
    fn = parse_field_refs(a["n"], pn["tail_offset"])
    fe = parse_field_refs(a["e"], pe["tail_offset"])
    lines.append("  tail field-reference entries control %d, null %d, "
                 "edit %d" % (len(fc), len(fn), len(fe)))

    def ref_key(ref):
        return (ref["field_id"], ref["index_word"], ref["length_word"],
                tuple(ref["words"]))

    if [ref_key(r) for r in fn] == [ref_key(r) for r in fe]:
        lines.append("    field-reference sequence: identical null -> edit")
    else:
        shown = 0
        for i in range(max(len(fn), len(fe))):
            if i < len(fn) and i < len(fe) and \
                    ref_key(fn[i]) == ref_key(fe[i]):
                continue
            old = fn[i] if i < len(fn) else None
            new = fe[i] if i < len(fe) else None
            span_n = ((old["offset"], old["offset"] + 28) if old else None)
            span_e = ((new["offset"], new["offset"] + 28) if new else None)
            kind = "CHANGED" if old and new else (
                "ADDED" if new else "REMOVED")
            if not span_change_is_effect(a, span_n, span_e):
                continue
            lines.append("    EFFECT: field reference %s at index %d" % (
                kind, i))
            if old:
                lines.append("      null field 0x%08x index %d len %d words "
                             "%s" % (old["field_id"], old["index_word"],
                                     old["length_word"],
                                     [hex(w) for w in old["words"]]))
            if new:
                lines.append("      edit field 0x%08x index %d len %d words "
                             "%s" % (new["field_id"], new["index_word"],
                                     new["length_word"],
                                     [hex(w) for w in new["words"]]))
            shown += 1
            if shown >= 20:
                lines.append("    ... further field-reference differences "
                             "not listed")
                break
        if shown == 0:
            lines.append("    field-reference differences sit at masked "
                         "positions only")
    return lines


def fixed_rows(buf, spec):
    header = spec["header"]
    row = spec["row"]
    rows = []
    if row == 0:
        return rows
    n = max(0, (len(buf) - header) // row)
    for i in range(n):
        base = header + i * row
        rows.append({"index": i, "uid": u32(buf, base + spec["uid"]),
                     "base": base, "raw": buf[base:base + row]})
    return rows


def table_row_diff(a, spec):
    lines = []
    control, null, edit = a["c"], a["n"], a["e"]
    rc = fixed_rows(control, spec)
    rn = fixed_rows(null, spec)
    re_ = fixed_rows(edit, spec)
    lines.append("  rows control %d, null %d, edit %d" % (
        len(rc), len(rn), len(re_)))
    for row in re_:
        i = row["index"]
        if i >= len(rn):
            lines.append("  row %d uid %s is NEW: %s" % (
                i, row["uid"], hexs(row["raw"], 0, 48)))
            continue
        base = row["base"]
        nraw = rn[i]["raw"]
        eraw = row["raw"]
        diffs = [j for j in range(min(len(nraw), len(eraw)))
                 if nraw[j] != eraw[j]]
        if not diffs:
            continue
        effects = [j for j in diffs if not is_masked(a, base + j)]
        noise = len(diffs) - len(effects)
        if effects:
            lines.append("  row %d uid %s: %d EFFECT bytes (noise %d)" % (
                i, row["uid"], len(effects), noise))
            shown = effects[:24]
            parts = ["    +%d 0x%02x->0x%02x" % (j, nraw[j], eraw[j])
                     for j in shown]
            lines.append(";".join(parts) + (" ..." if len(effects) > 24
                                            else ""))
        elif noise:
            lines.append("  row %d uid %s: %d noise bytes only" % (
                i, row["uid"], noise))
    for i in range(len(re_), len(rn)):
        lines.append("  row %d uid %s was REMOVED (null had it)" % (
            i, rn[i]["uid"]))
    return lines


def meta_entry_diff(a, spec, label):
    lines = []
    entry = spec["meta_entry"]
    if not entry:
        return lines
    control, null, edit = a["c"], a["n"], a["e"]
    def entries(buf):
        if len(buf) < 16:
            return []
        return [buf[16 + i * entry:16 + (i + 1) * entry]
                for i in range((len(buf) - 16) // entry)]
    ec = entries(control)
    en = entries(null)
    ee = entries(edit)
    lines.append("  %s entries control %d, null %d, edit %d" % (
        label, len(ec), len(en), len(ee)))
    for i, e in enumerate(ee):
        if i >= len(en):
            lines.append("  %s entry %d is NEW: %s" % (label, i, hexs(e, 0, 32)))
            continue
        base = 16 + i * entry
        diffs = [j for j in range(min(len(en[i]), len(e)))
                 if en[i][j] != e[j]]
        effects = [j for j in diffs if not is_masked(a, base + j)]
        if effects:
            parts = ["    +%d 0x%02x->0x%02x" % (j, en[i][j], e[j])
                     for j in effects[:16]]
            lines.append("  %s entry %d: %s%s" % (
                label, i, ";".join(parts),
                " ..." if len(effects) > 16 else ""))
        elif diffs:
            lines.append("  %s entry %d: %d noise bytes only" % (
                label, i, len(diffs)))
    return lines


def fixed2_data_diff(a, spec):
    lines = []
    if not spec.get("fixed2_row"):
        return lines
    control, null, edit = a["c"], a["n"], a["e"]
    header = spec["fixed2_header"]
    row = spec["fixed2_row"]
    def records(buf):
        if len(buf) < header:
            return []
        return [buf[header + i * row:header + (i + 1) * row]
                for i in range((len(buf) - header) // row)]
    nc = records(control)
    nn = records(null)
    ne = records(edit)
    lines.append("  Fixed2Data records control %d, null %d, edit %d" % (
        len(nc), len(nn), len(ne)))
    for i, rec in enumerate(ne):
        if i >= len(nn):
            lines.append("  record %d is NEW: %s" % (i, hexs(rec, 0, 32)))
            continue
        base = header + i * row
        diffs = [j for j in range(min(len(nn[i]), len(rec)))
                 if nn[i][j] != rec[j]]
        effects = [j for j in diffs if not is_masked(a, base + j)]
        if effects:
            parts = ["    +%d 0x%02x->0x%02x" % (j, nn[i][j], rec[j])
                     for j in effects[:16]]
            lines.append("  record %d: %s%s" % (
                i, ";".join(parts), " ..." if len(effects) > 16 else ""))
        elif diffs:
            lines.append("  record %d: %d noise bytes only" % (i, len(diffs)))
    return lines


# ---------------------------------------------------------------- stream diff


def analyse(kind, control, null, edit):
    """Return dict of lengths, mask, effect runs and tail positions."""
    c = normalise(kind, control)
    n = normalise(kind, null)
    e = normalise(kind, edit)
    mask = bytearray(len(c))
    for i in range(min(len(c), len(n))):
        if c[i] != n[i]:
            mask[i] = 1
    effects = []
    for i in range(len(n)):
        if i < len(mask) and mask[i]:
            continue
        if i >= len(e) or n[i] != e[i]:
            effects.append(i)
    tail_effects = []
    tail_start = len(n)
    for i in range(tail_start, max(len(c), len(e))):
        cb = c[i] if i < len(c) else None
        eb = e[i] if i < len(e) else None
        if cb != eb:
            tail_effects.append(i)
    noise = [i for i in range(len(mask)) if mask[i]]
    return {"c": c, "n": n, "e": e, "noise": noise, "effects": effects,
            "effect_set": set(effects), "tail_effects": tail_effects,
            "tail_effect_set": set(tail_effects), "tail_start": tail_start,
            "mask": mask}


def is_masked(a, pos):
    mask = a["mask"]
    return pos < len(mask) and mask[pos] != 0


def stream_section(path, control, null, edit, names, metas=None):
    kind = kind_for(path)
    if control == null == edit:
        return []
    a = analyse(kind, control, null, edit)
    lines = ["### `%s`" % path, ""]
    lines.append("kind `%s`; control %d B, null %d B, edit %d B" % (
        kind, len(control), len(null), len(edit)))
    lines.append("")
    lines.append("noise %d B (control vs null, masked); effects %d B "
                 "(null vs edit, unmasked); tail from %d, tail effects %d B"
                 % (len(a["noise"]), len(a["effects"]), a["tail_start"],
                    len(a["tail_effects"])))
    lines.append("")
    if len(a["n"]) != len(a["e"]):
        prefix = common_prefix(a["n"], a["e"])
        suffix = common_suffix(a["n"][prefix:], a["e"][prefix:])
        lines.append("Shift note: null %d B -> edit %d B (delta %+d); first "
                     "difference at %d; common suffix %d B.  When the edit "
                     "inserts or removes bytes, every later byte differs "
                     "positionally, so the raw effect count below includes "
                     "the shifted suffix.  The decoded structure below is "
                     "the authoritative view of the writer's work." % (
                         len(a["n"]), len(a["e"]), len(a["e"]) - len(a["n"]),
                         prefix, suffix))
        lines.append("")
    if a["noise"]:
        nruns = runs(a["noise"])
        lines.append("Noise runs (masked; not the edit): %d, first: %s"
                     % (len(nruns), ", ".join(
                         "[%d:%d)" % (s, t) for s, t in nruns[:12])))
        lines.append("")
    if a["effects"]:
        eruns = runs(a["effects"])
        lines.append("| run | bytes | null | edit |")
        lines.append("|---|---|---|---|")
        for s, t in eruns[:MAX_RUNS_PER_STREAM]:
            npart = a["n"][s:t]
            epart = a["e"][s:t]
            lines.append("| [%d:%d) | %d | `%s` | `%s` |" % (
                s, t, t - s,
                hexs(npart) + (" ..." if t - s > MAX_BYTES_PER_RUN else ""),
                (hexs(epart) if epart else "(short)")
                + (" ..." if t - s > MAX_BYTES_PER_RUN else "")))
        if len(eruns) > MAX_RUNS_PER_STREAM:
            lines.append("")
            lines.append("%d further effect runs not listed." % (
                len(eruns) - MAX_RUNS_PER_STREAM))
        lines.append("")
    if a["tail_effects"]:
        lines.append("Tail effect positions: %s%s" % (
            ", ".join(str(p) for p in a["tail_effects"][:32]),
            "" if len(a["tail_effects"]) <= 32 else " ... (%d total)" %
            len(a["tail_effects"])))
        lines.append("")
    base = path.rsplit("/", 1)[-1]
    table_name = path.split("/")[1] if "/" in path else ""
    spec = TABLES.get(table_name)
    if spec and base == "FixedData":
        lines += table_row_diff(a, spec)
    elif spec and base == "FixedMeta":
        lines += meta_entry_diff(a, spec, "FixedMeta")
    elif spec and base == "Fixed2Meta":
        lines += meta_entry_diff(a, {"meta_entry": spec["fixed2_meta_entry"]},
                                 "Fixed2Meta")
    elif spec and base == "Fixed2Data":
        lines += fixed2_data_diff(a, spec)
    elif base == "VarMeta":
        lines += var_meta_diff(a, names)
    elif base == "Var2Data":
        table_path = path.rsplit("/", 1)[0]
        if table_path.endswith("CEdl") and kind == "cedl":
            lines.append("  CEdl shape: one or more Var2Data blocks, each a "
                         "complete Props document; the save-path record "
                         "0x26400031 is the record the cedl normalisation "
                         "replaces with \"PATH\", so its per-save bytes are "
                         "not effects.  The runs above are raw offsets before "
                         "normalisation.")
        else:
            meta_c, meta_n, meta_e = (metas or (b"", b"", b""))
            lines += var2_entry_diff(a, meta_c, meta_n, meta_e, names)
    elif base == "Props":
        lines += props_diff(a, table_name)
    lines.append("")
    return lines


def document(pair, files, names):
    control, null, edit = files
    lines = ["# Pair anatomy: `%s`" % pair, ""]
    lines.append("_Generated by `scripts/pair_anatomy.py` (W34-0.1).  All "
                 "values are stream offsets and bytes; no task text is "
                 "printed._")
    lines.append("")
    lines.append("| file | bytes |")
    lines.append("|---|---|")
    for label, path in (("control", control), ("null save", null),
                        ("edit", edit)):
        lines.append("| %s `%s` | %d |" % (
            label, os.path.basename(path), os.path.getsize(path)
            if os.path.exists(path) else -1))
    lines.append("")
    cstreams = load_streams(control)
    nstreams = load_streams(null)
    estreams = load_streams(edit)
    # For each Var2Data stream, the three files' VarMeta streams (the block
    # offsets differ between saves, so each side must be read beside its own
    # Var2Data).
    metas = {"control": {}, "null": {}, "edit": {}}
    for role, streams in (("control", cstreams), ("null", nstreams),
                          ("edit", estreams)):
        for stem_path in streams:
            if stem_path.endswith("VarMeta"):
                stem = stem_path[:-len("VarMeta")]
                metas[role][stem + "Var2Data"] = streams[stem_path]
    lines.append("## Stream summary")
    lines.append("")
    lines.append("| stream | control | null | edit | noise | effects | "
                 "tail effects |")
    lines.append("|---|---|---|---|---|---|---|")
    sections = []
    total_effect = 0
    for path in sorted(set(cstreams) | set(nstreams) | set(estreams)):
        c = cstreams.get(path, b"")
        n = nstreams.get(path, b"")
        e = estreams.get(path, b"")
        if c == n == e:
            continue
        kind = kind_for(path)
        a = analyse(kind, c, n, e)
        total_effect += len(a["effects"]) + len(a["tail_effects"])
        lines.append("| `%s` | %d | %d | %d | %d | %d | %d |" % (
            path, len(c), len(n), len(e), len(a["noise"]),
            len(a["effects"]), len(a["tail_effects"])))
        sections.append((path, c, n, e))
    lines.append("")
    lines.append("Streams with a measured change: %d; effect bytes: %d."
                 % (len(sections), total_effect))
    lines.append("")
    for path, c, n, e in sections:
        meta_triplet = (metas["control"].get(path, b""),
                        metas["null"].get(path, b""),
                        metas["edit"].get(path, b""))
        lines += stream_section(path, c, n, e, names, meta_triplet)
    return "\n".join(lines).rstrip("\n") + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "spec", "writers",
                                                  "evidence"))
    ap.add_argument("--pair", default=None,
                    help="one pair key, or 'all' (default)")
    ap.add_argument("--print", dest="to_stdout", action="store_true")
    args = ap.parse_args()
    names = FieldNames(FIELD_META)
    keys = sorted(PAIRS) if not args.pair or args.pair == "all" else \
        [args.pair]
    os.makedirs(args.out, exist_ok=True)
    missing = []
    for key in keys:
        paths = [os.path.join(DATA, p) for p in PAIRS[key]]
        if not all(os.path.exists(p) for p in paths):
            missing.append(key)
            print("MISSING: %s -> %s" % (key, paths), file=sys.stderr)
            continue
        text = document(key, paths, names)
        if args.to_stdout:
            print(text)
        else:
            target = os.path.join(args.out, key + ".md")
            with open(target, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)
            print("%s -> %s (%d lines)" % (key, target,
                                            text.count("\n")))
    if missing:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
