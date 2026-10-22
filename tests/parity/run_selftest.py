#!/usr/bin/env python3
"""Run the UI-parity harness self-check and its CLI contract.

The measurements themselves are proved by ``scripts/parity_report.py
--selftest`` (synthetic images with known geometry, asserted to +-1 px).  This
runner adds the command-line contract that CI depends on:

    python tests/parity/run_selftest.py

It needs no reference capture and no Qt build: it generates its images under
``build/parity-selftest/``.  Exit code 0 means the harness, its JSON output and
its exit codes all behaved.
"""

import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "parity_report.py")
SELFTEST_DIR = os.path.join(ROOT, "build", "parity-selftest")


def run(args, expect):
    result = subprocess.run([sys.executable, SCRIPT, *args],
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    ok = result.returncode == expect
    print(f"  {'ok ' if ok else 'FAIL'} exit {result.returncode} "
          f"(want {expect}): parity_report.py {' '.join(args)}")
    if not ok:
        tail = (result.stdout + result.stderr).strip().splitlines()[-8:]
        for line in tail:
            print(f"       {line}")
    return ok


def main():
    failures = []

    print("1. synthetic measurement self-check")
    result = subprocess.run([sys.executable, SCRIPT, "--selftest"],
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    print(result.stdout.rstrip())
    if result.returncode != 0:
        failures.append("--selftest exited non-zero")
        if result.stderr.strip():
            print(result.stderr.rstrip())

    sheet_a = os.path.join(SELFTEST_DIR, "sheet-a.png")
    sheet_b = os.path.join(SELFTEST_DIR, "sheet-b.png")
    blank = os.path.join(SELFTEST_DIR, "blank.png")
    if not os.path.exists(sheet_a):
        print("FAIL: --selftest did not leave its synthetic images in build/")
        return 1

    print("2. report mode")
    if not run([sheet_a], 0):
        failures.append("report mode exit code")

    print("3. JSON output is machine readable")
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "report.json")
        result = subprocess.run([sys.executable, SCRIPT, sheet_a,
                                 "--json", out, "--quiet"],
                                capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        ok = result.returncode == 0 and os.path.exists(out)
        if ok:
            with open(out, encoding="utf-8") as handle:
                report = json.load(handle)
            required = {"image", "background", "layout", "measures", "notes"}
            ok = required.issubset(report) and "row_height_px" in report["measures"]
        print(f"  {'ok ' if ok else 'FAIL'} JSON keys")
        if not ok:
            failures.append("JSON output")
        else:
            value = report["measures"]["row_height_px"]["value"]
            ok = value == 20
            print(f"  {'ok ' if ok else 'FAIL'} JSON row_height_px = {value} (want 20)")
            if not ok:
                failures.append("JSON row height")

    print("4. compare mode")
    if not run([sheet_a, sheet_a], 0):
        failures.append("identical compare should pass")
    if not run([sheet_a, sheet_b], 1):
        failures.append("different geometry should fail")
    if not run([blank, blank], 1):
        failures.append("nothing comparable should fail")

    print("5. compare JSON carries the delta table")
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "compare.json")
        result = subprocess.run([sys.executable, SCRIPT, sheet_a, sheet_b,
                                 "--json", out, "--quiet"],
                                capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        ok = result.returncode == 1 and os.path.exists(out)
        if ok:
            with open(out, encoding="utf-8") as handle:
                payload = json.load(handle)
            ok = ({"reference", "candidate", "comparison", "summary"}
                  .issubset(payload)
                  and payload["summary"]["verdict"] == "FAIL"
                  and len(payload["comparison"]) == 10)
        print(f"  {'ok ' if ok else 'FAIL'} compare JSON")
        if not ok:
            failures.append("compare JSON")

    print("6. usage and IO errors")
    if not run([], 2):
        failures.append("no arguments should exit 2")
    if not run([os.path.join(ROOT, "no-such-file.png")], 2):
        failures.append("missing file should exit 2")

    print()
    if failures:
        print("PARITY CLI CHECK FAILED")
        for item in failures:
            print(f"  {item}")
        return 1
    print("PARITY CLI CHECK PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
