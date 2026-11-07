"""A minimal, independent reader of the DocumentSummaryInformation property set.

W34-3.3.  It shares no code with core/mppwrite/summary_info.cpp: it follows the
[MS-OLEPS] layout from the specification (header, section descriptors, property
tables) and decodes only the types Project writes.  The CFB container is opened
with olefile (`pip install olefile`); the property-set parsing below is plain
standard library.

    python scripts/dsi_reader.py <file.mpp>        print the decoded sections

Used by scripts/check_dsi.py to compare the port's file with Project's.
"""
import struct
import sys

SUMMARY_STREAM = "DocumentSummaryInformation"
FMTID_SUMMARY = "d5cdd502-2e9c-101b-9397-08002b2cf9ae"
FMTID_USER = "d5cdd505-2e9c-101b-9397-08002b2cf9ae"

VT_I2 = 0x02
VT_I4 = 0x03
VT_BOOL = 0x0B
VT_VARIANT = 0x0C
VT_UI4 = 0x13
VT_LPSTR = 0x1E
VT_LPWSTR = 0x1F
VT_FILETIME = 0x40
VT_VECTOR = 0x1000


class DsiError(Exception):
    pass


def _fmtid(raw):
    """A FMTID in its text form (the first three fields are little-endian)."""
    d1, d2, d3 = struct.unpack("<IHH", raw[:8])
    return "%08x-%04x-%04x-%s-%s" % (d1, d2, d3, raw[8:10].hex(), raw[10:16].hex())


def _need(buf, end, what):
    if end > len(buf):
        raise DsiError(what + " runs past the stream")


def _lpstr(buf, at, limit, element=False):
    """(text, end offset) for a counted CodePage string.

    A simple property value stores a length already rounded up to four and
    NUL-padded, so it ends where the length says.  A string that is an element
    of a VT_LPSTR vector stores its exact length (NUL included) and the next
    element follows immediately, with no padding after the vector either (only
    the section as a whole is padded to four bytes).
    """
    _need(buf, at + 4, "a string length")
    (length,) = struct.unpack_from("<I", buf, at)
    _need(buf, at + 4 + length, "a string")
    raw = buf[at + 4:at + 4 + length]
    if length and raw[-1] != 0:
        raise DsiError("a string is not NUL-terminated")
    text = raw.rstrip(b"\x00").decode("cp1252")
    if not element and length % 4:
        raise DsiError("a string length is not a multiple of four")
    return text, at + 4 + length


def _value(buf, at, limit, nested=False):
    """Decode one typed value at `at`; returns (value, end offset).

    A value nested in a VT_VARIANT vector (HeadingPairs) is packed with no
    alignment at all, so its string length is exact.
    """
    _need(buf, at + 4, "a value type")
    (vt,) = struct.unpack_from("<I", buf, at)
    body = at + 4
    kind = vt & 0xFFF
    if vt & VT_VECTOR:
        _need(buf, body + 4, "a vector count")
        (count,) = struct.unpack_from("<I", buf, body)
        pos = body + 4
        items = []
        for _ in range(count):
            if kind == VT_LPSTR:
                text, pos = _lpstr(buf, pos, limit, element=True)
                items.append(text)
            elif kind == VT_VARIANT:
                item, pos = _value(buf, pos, limit, nested=True)
                items.append(item)
            else:
                raise DsiError("an unmeasured vector element type 0x%x" % kind)
        return ("vector", kind, items), pos
    if kind in (VT_I2, VT_BOOL):
        _need(buf, body + 4, "a 16-bit value")
        return ("int", kind, struct.unpack_from("<h", buf, body)[0]), body + 4
    if kind in (VT_I4, VT_UI4):
        _need(buf, body + 4, "a 32-bit value")
        fmt = "<i" if kind == VT_I4 else "<I"
        return ("int", kind, struct.unpack_from(fmt, buf, body)[0]), body + 4
    if kind == VT_LPSTR:
        text, pos = _lpstr(buf, body, limit, element=nested)
        return ("string", kind, text), pos
    if kind == VT_FILETIME:
        _need(buf, body + 8, "a FILETIME")
        return ("filetime", kind, struct.unpack_from("<Q", buf, body)[0]), body + 8
    raise DsiError("an unmeasured property type 0x%x" % vt)


def _dictionary(buf, at):
    """The pid 0 dictionary: count, then (pid, length, string) entries.

    Project does not pad the entries (a CodePage dictionary needs none), so the
    next property starts at an unaligned offset; olefile reads it the same way.
    """
    _need(buf, at + 4, "the dictionary count")
    (count,) = struct.unpack_from("<I", buf, at)
    pos = at + 4
    entries = []
    for _ in range(count):
        _need(buf, pos + 8, "a dictionary entry")
        pid, length = struct.unpack_from("<II", buf, pos)
        _need(buf, pos + 8 + length, "a dictionary string")
        raw = buf[pos + 8:pos + 8 + length]
        entries.append((pid, raw.rstrip(b"\0").decode("cp1252")))
        pos += 8 + length
    return entries, pos


def parse(buf):
    """Decode a property-set stream into a list of sections.

    Each section is {fmtid, size, properties: [(pid, decoded value)]}.  Raises
    DsiError on any inconsistency a reader would reject: bad byte order,
    offsets that are not ascending or fall outside their section, a declared
    section size that is not the bytes used, non-zero padding.
    """
    if len(buf) < 28:
        raise DsiError("shorter than the 28-byte header")
    byte_order, version, system_id = struct.unpack_from("<HHI", buf, 0)
    if byte_order != 0xFFFE:
        raise DsiError("byte order is not 0xFFFE")
    (count,) = struct.unpack_from("<I", buf, 24)
    if count == 0 or count > 2:
        raise DsiError("%d sections" % count)
    descriptors = []
    for i in range(count):
        at = 28 + 20 * i
        _need(buf, at + 20, "a section descriptor")
        descriptors.append((_fmtid(buf[at:at + 16]), struct.unpack_from("<I", buf, at + 16)[0]))
    sections = []
    for index, (fmtid, offset) in enumerate(descriptors):
        _need(buf, offset + 8, "a section header")
        size, n_props = struct.unpack_from("<II", buf, offset)
        end = offset + size
        _need(buf, end, "section %d" % index)
        table = []
        for k in range(n_props):
            at = offset + 8 + 8 * k
            _need(buf, at + 8, "a property table entry")
            table.append(struct.unpack_from("<II", buf, at))
        offsets = [o for _, o in table]
        if offsets != sorted(offsets) or len(set(offsets)) != len(offsets):
            raise DsiError("property offsets in section %d are not ascending" % index)
        if offsets and offsets[0] != 8 + 8 * n_props:
            raise DsiError("the first value of section %d does not follow the table" % index)
        props = []
        used = offset + 8 + 8 * n_props
        for pid, rel in table:
            at = offset + rel
            if at != used:
                raise DsiError("pid %d is not contiguous with the previous value" % pid)
            if pid == 0:
                value, used = _dictionary(buf, at)
                value = ("dictionary", 0, value)
            else:
                value, used = _value(buf, at, end)
            props.append((pid, value))
        used += (-used) % 4
        if used != end:
            raise DsiError("section %d declares %d bytes but uses %d" % (index, size, used - offset))
        sections.append({"fmtid": fmtid, "size": size, "properties": props})
    last_end = max(d[1] + s["size"] for d, s in zip(descriptors, sections))
    if last_end != len(buf):
        raise DsiError("%d trailing bytes after the last section" % (len(buf) - last_end))
    return sections


def read_stream(path):
    """The DocumentSummaryInformation stream of an .mpp file (needs olefile)."""
    try:
        import olefile
    except ImportError:
        raise SystemExit("dsi_reader: install olefile (pip install olefile)")
    ole = olefile.OleFileIO(path)
    try:
        for entry in ole.listdir():
            if entry[-1].endswith(SUMMARY_STREAM):
                return ole.openstream(entry).read()
    finally:
        ole.close()
    raise DsiError("%s has no %s stream" % (path, SUMMARY_STREAM))


def load(path):
    return parse(read_stream(path))


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    for section in load(argv[1]):
        print("section", section["fmtid"], section["size"], "bytes")
        for pid, value in section["properties"]:
            print("  pid 0x%x" % pid, value)


if __name__ == "__main__":
    main(sys.argv)
