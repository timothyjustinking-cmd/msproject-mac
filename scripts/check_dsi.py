"""W34-3.3 gate: read the port's DocumentSummaryInformation with an independent
reader and compare it with Project's own save, property by property.

    python scripts/check_dsi.py <test-out dir> [data dir]

`mppwrite_roundtrip_summary_info` writes one file per pair to its test-out
directory (build/<tree>/core/mppwrite/test-out/w34-summary-<label>.mpp).  For
each pair this script parses the port's stream and Project's edit save with
scripts/dsi_reader.py -- no code shared with the C++ writer -- and requires:

  * both parse cleanly (byte order, ascending contiguous offsets, section sizes
    that equal the bytes used, no trailing bytes);
  * the decoded sections are identical;
  * the pair is not vacuous: the edit's decoded properties differ from the
    control's exactly when Project's save changes the stream.

Needs olefile (`pip install olefile`).  Exit status 0 only if every pair holds.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dsi_reader  # noqa: E402

# label, control file, edit file, Project's save changes the stream
PAIRS = [
    ("calculate", "wave30-calculate-control.mpp", "wave30-calculate.mpp", True),
    ("calculate-second", "wave34-calculate-second-control.mpp", "wave34-calculate-second.mpp", False),
    ("move-back", "wave30-move-back-control.mpp", "wave30-move-back.mpp", True),
    ("move-forward", "wave30-move-forward-control.mpp", "wave30-move-forward.mpp", False),
    ("inactivate-second", "wave30-inactivate-second-control.mpp", "wave30-inactivate-second.mpp", True),
    ("fill", "wave31-fill-control.mpp", "wave31-fill.mpp", True),
    ("task-mode-manual", "wave30-task-mode-manual-control.mpp", "wave30-task-mode-manual.mpp", True),
]


def self_test(path):
    """The reader must reject a damaged stream, or "both parse" proves nothing."""
    good = dsi_reader.read_stream(path)
    dsi_reader.parse(good)
    damaged = {
        "byte order": bytes([good[0] ^ 0xFF]) + good[1:],
        "truncated": good[:-4],
        "trailing byte": good + bytes(4),
        "section size": good[:68] + bytes([good[68] + 4]) + good[69:],
        "string length": good[:168] + bytes([good[168] + 1]) + good[169:],
    }
    bad = 0
    for what, buf in damaged.items():
        try:
            dsi_reader.parse(buf)
        except dsi_reader.DsiError:
            continue
        print("FAIL self-test: the reader accepted a stream with a damaged %s" % what)
        bad += 1
    return bad


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    out_dir = argv[1]
    data_dir = argv[2] if len(argv) == 3 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tests", "data")
    failures = self_test(os.path.join(data_dir, PAIRS[0][2]))
    for label, control, edit, changed in PAIRS:
        port_path = os.path.join(out_dir, "w34-summary-%s.mpp" % label)
        try:
            port = dsi_reader.load(port_path)
            project = dsi_reader.load(os.path.join(data_dir, edit))
            before = dsi_reader.load(os.path.join(data_dir, control))
        except (dsi_reader.DsiError, OSError) as error:
            print("FAIL %-18s %s" % (label, error))
            failures += 1
            continue
        problems = []
        if port != project:
            problems.append("the port's property set differs from Project's")
        if (project != before) != changed:
            problems.append("Project's save %s the property set, expected %s" % (
                "changes" if project != before else "leaves", "a change" if changed else "no change"))
        n_props = sum(len(s["properties"]) for s in port)
        if problems:
            failures += 1
            print("FAIL %-18s %s" % (label, "; ".join(problems)))
        else:
            print("ok   %-18s %d sections, %d properties, %s" % (
                label, len(port), n_props, "changed" if changed else "unchanged"))
    print("%d pairs, %d failures" % (len(PAIRS), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
