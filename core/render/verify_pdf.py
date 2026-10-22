#!/usr/bin/env python3
"""verify_pdf.py -- optional external validation of core/render's PDF output.

The C++ test (tests/render_pdf_tests.cpp) is the required check and does not
depend on this script.  This one is for a human: it re-parses a written PDF with
a *second* implementation, then, if pypdf (or PyPDF2) happens to be installed,
opens it with a real third-party reader as well.  Nothing is installed.

Usage:
    python core/render/verify_pdf.py <file.pdf> [<file.pdf> ...]

Output is COUNTS ONLY (pages, bytes, xref entries, extracted text length).  The
script never prints page text, so a corpus-derived PDF can be checked without a
client task name reaching the terminal or a log.

Exit status: 0 when every file passes the structural checks and, if a third-party
reader is available, that reader agrees on the page count; 1 otherwise.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

XREF_ENTRY = re.compile(rb"^(\d{10}) (\d{5}) ([nf]) $")


def structural(path: Path) -> tuple[bool, str, dict]:
    """The same invariants the C++ parser checks, implemented independently."""
    data = path.read_bytes()
    info: dict = {"bytes": len(data)}
    if not data.startswith(b"%PDF-"):
        return False, "missing %PDF- header", info
    eol = data.find(b"\n")
    info["version"] = data[5:eol].decode("ascii", "replace")

    if not data.rstrip(b"\r\n").endswith(b"%%EOF"):
        return False, "%%EOF is not the last non-whitespace bytes", info

    sx = data.rfind(b"startxref")
    if sx < 0:
        return False, "missing startxref", info
    match = re.match(rb"startxref\s+(\d+)", data[sx:])
    if not match:
        return False, "malformed startxref", info
    offset = int(match.group(1))
    info["startxref"] = offset
    if offset <= 0 or offset >= len(data) or data[offset:offset + 4] != b"xref":
        return False, "startxref does not point at an xref table", info

    pos = offset + 4
    head = re.match(rb"\s*(\d+)\s+(\d+)\s*", data[pos:])
    if not head:
        return False, "malformed xref subsection header", info
    first = int(head.group(1))
    count = int(head.group(2))
    if first != 0 or count <= 0:
        return False, "unexpected xref subsection", info
    pos += head.end()
    info["xref_entries"] = count
    entries = []
    for _ in range(count):
        line = data[pos:pos + 20]
        match = XREF_ENTRY.match(line)
        if not match:
            return False, f"malformed xref entry at byte {pos}", info
        entries.append((int(match.group(1)), match.group(3) == b"n"))
        pos += 20
    if entries[0][1]:
        return False, "xref entry 0 is not free", info
    for number, (entry_offset, in_use) in enumerate(entries[1:], start=1):
        prefix = f"{number} 0 obj".encode("ascii")
        if not in_use:
            return False, f"xref entry {number} is free but was written", info
        if data[entry_offset:entry_offset + len(prefix)] != prefix:
            return False, f"xref entry {number} does not land on '{prefix.decode()}'", info

    trailer = data.find(b"trailer", offset)
    if trailer < 0:
        return False, "missing trailer", info
    size = re.search(rb"/Size\s+(\d+)", data[trailer:])
    root = re.search(rb"/Root\s+(\d+)\s+0\s+R", data[trailer:])
    info_at = re.search(rb"/Info\s+(\d+)\s+0\s+R", data[trailer:])
    if not size:
        return False, "trailer has no /Size", info
    if int(size.group(1)) != count:
        return False, "/Size does not match the xref entry count", info
    if not root:
        return False, "trailer has no /Root", info
    if not info_at:
        return False, "trailer has no /Info", info

    pages = len(re.findall(rb"/Type\s*/Page(?![s])", data))
    info["page_objects"] = pages
    count_match = re.search(rb"/Type\s*/Pages\b.*?/Count\s+(\d+)", data, re.S)
    if not count_match:
        return False, "/Pages has no /Count", info
    declared = int(count_match.group(1))
    info["declared_pages"] = declared
    if declared != pages:
        return False, f"/Count {declared} != {pages} page objects", info
    return True, "", info


def third_party(path: Path) -> tuple[str, str]:
    """('absent'|'ok'|'error', message) -- never prints page text."""
    try:
        import pypdf  # type: ignore
    except ImportError:
        try:
            import PyPDF2 as pypdf  # type: ignore
        except ImportError:
            return "absent", "pypdf/PyPDF2 not installed (structural check only)"
    try:
        reader = pypdf.PdfReader(str(path))
        pages = len(reader.pages)
        text_chars = 0
        for page in reader.pages:
            text_chars += len(page.extract_text() or "")
        return "ok", f"pypdf pages={pages} text_chars={text_chars}"
    except Exception as exc:  # noqa: BLE001 - report any reader failure
        return "error", f"{type(exc).__name__}: {exc}"


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: verify_pdf.py <file.pdf> [<file.pdf> ...]")
        return 2
    failures = 0
    for name in argv[1:]:
        path = Path(name)
        if not path.is_file():
            print(f"{path.name}: MISSING")
            failures += 1
            continue
        ok, error, info = structural(path)
        if not ok:
            print(f"{path.name}: FAIL structural: {error}")
            failures += 1
            continue
        status, message = third_party(path)
        if status == "error":
            print(f"{path.name}: FAIL reader: {message}")
            failures += 1
            continue
        print(
            f"{path.name}: ok bytes={info['bytes']} pages={info['page_objects']} "
            f"xref={info['xref_entries']} version={info['version']} | {message}"
        )
    if failures:
        print(f"{failures} file(s) failed")
        return 1
    print("all files passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
