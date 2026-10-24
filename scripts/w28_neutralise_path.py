#!/usr/bin/env python3
"""Wave-28 W28-0.1: neutralise a fixture's embedded save path in place.

`synthetic-calendars.mpp` is the engine suite's calendar base and a recorded
oracle: rebuilding it through Project would change every per-save byte and
invalidate that measurement, so the wave-28 brief allows its embedded save
paths to be replaced in place with a neutral string of exactly the same
length.  The recorded path is the operator's local save path; this script
never contains it -- it finds the UTF-16LE save-path strings that end in the
fixture's own file name and rewrites them to `C:\\wNN\\<name>` padded with
spaces (a trailing marker character, if present, is kept at the end).

Both records (`Props14` and the `214/CEdl/Var2Data` entry) prefix the string
with a byte-length word that counts the text plus its NUL; the replacement is
exactly as long as the original, so every other byte of the file is untouched
and both length words stay valid.  The hash changes only because the path
text changes.

    python scripts/w28_neutralise_path.py tests/data/synthetic-calendars.mpp

Run scripts/check_fixtures.py afterwards and update the documented SHA-256.
"""

import os
import sys

NEUTRAL_ROOT = "C:\\w19\\"


def utf16_run(data, at):
    """The UTF-16LE printable run containing `at`, and its byte bounds."""
    start = at
    while start >= 2 and data[start - 1] == 0 and 0x20 <= data[start - 2] < 0x7F:
        start -= 2
    end = at
    while end + 1 < len(data) and data[end:end + 2] != b"\x00\x00":
        end += 2
    return start, end


def neutralise(path):
    with open(path, "rb") as handle:
        data = bytearray(handle.read())
    name = os.path.basename(path)
    needle = name.encode("utf-16le")
    replacements = 0
    at = 0
    while True:
        found = data.find(needle, at)
        if found < 0:
            break
        start, end = utf16_run(data, found)
        original = bytes(data[start:end]).decode("utf-16le")
        if not (len(original) > 4 and original[1:3] in (":\\", ":/")):
            at = found + 1
            continue
        length = len(original)
        neutral = NEUTRAL_ROOT + name
        if len(neutral) > length:
            raise SystemExit(
                f"{path}: neutral path {neutral!r} does not fit in {length} chars")
        if original.endswith("!"):
            replacement = neutral + " " * (length - len(neutral) - 1) + "!"
        else:
            replacement = neutral + " " * (length - len(neutral))
        encoded = replacement.encode("utf-16le")
        if len(encoded) != end - start:
            raise SystemExit(f"{path}: replacement length mismatch")
        data[start:end] = encoded
        replacements += 1
        at = start + len(encoded)
    if replacements == 0:
        print(f"{path}: no embedded save path found; unchanged")
        return 0
    with open(path, "wb") as handle:
        handle.write(data)
    print(f"{path}: replaced {replacements} embedded save path(s), "
          f"{len(data)} bytes, hash changed")
    return replacements


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: w28_neutralise_path.py <fixture.mpp>")
    neutralise(sys.argv[1])


if __name__ == "__main__":
    main()
