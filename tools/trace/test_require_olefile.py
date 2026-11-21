"""W36b-0.5 (R36W-5): in CI a missing olefile is a failure, not a skip.

test_trace_gate.py and test_gate_guards_meta.py exit 77 without olefile, which ctest reports as SKIPPED
(SKIP_RETURN_CODE 77).  A CI job that lost its `pip install olefile` would then pass with both tests skipped.
The CI jobs set MPP_REQUIRE_OLEFILE=1: the two scripts then exit 1 with a FAIL line, which ctest reports as
failed.  Locally the variable is unset and the skip stays.

This test hides olefile behind a PYTHONPATH shim (a module that raises ImportError) and runs each script both
ways: with the variable set it expects exit 1 and the FAIL line (never 77), with it unset exit 77 and the
SKIP line.  The scripts stop at the import, so each run takes well under a second.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = ("test_trace_gate.py", "test_gate_guards_meta.py")
REQUIRE = "MPP_REQUIRE_OLEFILE"

failures = 0


def check(label, ok, detail=""):
    global failures
    print("%s %s" % ("PASS" if ok else "FAIL", label) + ("" if ok else " -- %s" % detail))
    if not ok:
        failures += 1


def run(script, shim, require):
    env = dict(os.environ)
    env["PYTHONPATH"] = shim
    env.pop(REQUIRE, None)
    if require is not None:
        env[REQUIRE] = require
    done = subprocess.run([sys.executable, os.path.join(HERE, script)], env=env, capture_output=True,
                          text=True, cwd=HERE, timeout=120)
    return done.returncode, done.stdout + done.stderr


with tempfile.TemporaryDirectory(prefix="olefile-shim-") as shim:
    with open(os.path.join(shim, "olefile.py"), "w", encoding="utf-8", newline="\n") as f:
        f.write("raise ImportError('olefile hidden by test_require_olefile.py')\n")
    for script in SCRIPTS:
        code, out = run(script, shim, "1")
        check("W36b-0.5 %s with olefile hidden and %s=1 fails (exit 1, not the skip code 77)" % (script, REQUIRE),
              code == 1 and "FAIL" in out and REQUIRE in out, "exit %d: %r" % (code, out[:200]))
        code, out = run(script, shim, "0")
        check("W36b-0.5 %s with olefile hidden and %s=0 skips" % (script, REQUIRE),
              code == 77 and "SKIP" in out, "exit %d: %r" % (code, out[:200]))
        code, out = run(script, shim, "")
        check("W36b-0.5 %s with olefile hidden and %s empty skips" % (script, REQUIRE),
              code == 77 and "SKIP" in out, "exit %d: %r" % (code, out[:200]))
        code, out = run(script, shim, None)
        check("W36b-0.5 %s with olefile hidden and %s unset skips (the local run)" % (script, REQUIRE),
              code == 77 and "SKIP" in out, "exit %d: %r" % (code, out[:200]))

print("all passed" if failures == 0 else "%d failed" % failures)
sys.exit(1 if failures else 0)
