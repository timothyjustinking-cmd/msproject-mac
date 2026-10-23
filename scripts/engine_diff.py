#!/usr/bin/env python3
"""engine_diff.py -- the differential engine/backend harness for the MS Project port.

WHAT THIS MEASURES.  docs/engine-parity.md states the claim: our engine and
backend behave like MS Project on this machine.  A *scenario* is a base .mpp
plus an ordered operation list.  Running it through MS Project and through our
engine must produce the same canonical state dump, compared field by field with
NO tolerance.

THE TWO HALVES.
  --record <scenario>   COORDINATOR ONLY.  Opens the base in MS Project through
                        scripts/oracle_lock.oracle_session, applies the
                        operations with COM (CalculateProject, aware datetimes,
                        absolute FileSaveAs, FileClose(0)), saves, then dumps
                        Project's saved file with our own reader through
                        `engine_diff_tests --dump` and writes
                        tests/data/scenarios/<name>.oracle.json.  Numbers and
                        ids only: the dump never carries a name.
                        This path was WRITTEN BUT NOT EXERCISED by stream E1 --
                        COM cannot be driven from a stream (docs/coordination.md
                        Revision 3).  Treat it as the coordinator's script; it
                        is not tested.
  --check <scenario>    CI-safe, NO COM.  Applies the same operations through
                        core/mppwrite + core/plan, saves, dumps the same state,
                        and diffs it against the recorded oracle.  Prints one
                        line per differing field and exits non-zero.
  --suite               every scenario: `name: PASS/FAIL/UNRECORDED (n fields)`
                        and a summary.  Exit 1 if a recorded scenario differs,
                        exit 3 if some scenario has no oracle yet (so "0
                        recorded" is visible, not a silent pass).
  --expect-fail NAMES   with --suite: accept exactly this comma-separated SET
                        of failing scenarios; everything else must pass.  The
                        ctest lane (engine_diff_suite) runs
                        `--suite --expect-fail constraint-mso --apply-bin
                        $<TARGET_FILE:engine_diff_tests>`: constraint-mso is
                        deliberately LEFT FAILING until a held-out Project
                        measurement earns 8/8.  Pinning by name (wave 24,
                        W24-7) means a different scenario breaking cannot hide
                        behind a 7/8 count.
  --expect PASS/TOTAL   with --suite: the legacy count-only expectation (kept
                        for old commands; the ctest lane no longer uses it).
  --selftest            proves the machinery without MS Project: every
                        committed scenario applies and dumps; a self-recorded
                        oracle passes --check; a deliberately tampered oracle
                        makes --check fail naming the field.  This is the ctest
                        lane (engine_diff_harness).

THE C++ HALF.  tests/engine_diff_tests.cpp is the one place that touches .mpp
bytes for this harness:
    engine_diff_tests --apply <ops.tsv> <base.mpp> <out.mpp>
    engine_diff_tests --dump  <file.mpp> <out.json>
The dump uses core/mpp (the same reader tools/reader_dump links) and core/cal,
so both halves compare the same observable state.  scripts/mpp_rows.py remains
the independent byte-level oracle check; it is not used to build the dump
because it covers the task FixedData rows only.

SCENARIO GRAMMAR (tests/scenarios/*.json):
    {
      "name": "duration-change",       # unique; names the oracle JSON
      "base": "synthetic-base",        # a name in BASE_FILES below
      "operations": [ ... ],           # ordered; must end with save
      "compare": "all"                 # every dumped field is compared
    }

OPERATIONS (JSON objects; field ids are PjField ids from
analysis/field_id_map.tsv / core/mpp/field_value.h, never invented names):
    {"op": "set_task_field", "uid": 1, "field": 188743709, "value": 14400}
    {"op": "add_link", "predecessor_uid": 1, "successor_uid": 2,
     "type": "FS", "lag_ticks": 4800}
    {"op": "delete_task", "uid": 2}
    {"op": "add_assignment", "task_uid": 1, "resource_uid": 1,
     "units_percent": 50}
    {"op": "set_calendar", "task_uid": 1, "calendar_uid": 2}
    {"op": "recalculate"}
    {"op": "save"}

Field ids the our-side implements (see the C++ header comment for the value
type of each):
    188743709  Task.Duration        188743697  Task.ConstraintType
    188743698  Task.ConstraintDate  188743715  Task.Start
    188743712  Task.PercentComplete 188743705  Task.Priority
    PjTask Number/Duration/Date/Cost/Flag1 custom ids.  Text fields are refused
    on purpose so a committed scenario cannot carry a name.

Link types: FF=0, FS=1, SF=2, SS=3.  Lag is in 6-second file ticks
(4800 = one 8-hour working day); it must be a whole number of minutes because
that is what COM takes.

EXAMPLES (wave 23: always name the CURRENT build tree's applier; there is no
build/e1 default, and omitting --apply-bin picks the newest helper under build/
with a warning).
    python scripts/engine_diff.py --check tests/scenarios/02-duration-change.json \
        --apply-bin build/w23-core/Release/engine_diff_tests.exe
    python scripts/engine_diff.py --suite --expect-fail constraint-mso \
        --apply-bin build/w23-core/Release/engine_diff_tests.exe
    python scripts/engine_diff.py --selftest \
        --apply-bin build/w23-core/Release/engine_diff_tests.exe
    python scripts/engine_diff.py --record tests/scenarios/01-recalculate.json

The oracle output path is tests/data/scenarios/<name>.oracle.json (committed,
numbers only).  All generated .mpp files and intermediate dumps go under
build/<tree>/engine-diff/ (git-ignored); the committed fixture is never modified.
"""

import argparse
import copy
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCENARIO_DIR = ROOT / "tests" / "scenarios"
ORACLE_DIR = ROOT / "tests" / "data" / "scenarios"
DEFAULT_WORKDIR = ROOT / "build" / "e1" / "engine-diff"

SCHEMA = "engine-diff/2"

# Base fixtures that scenarios may name.  These are the only .mpp files a
# committed scenario may read: synthetic, committed, never client data.  Each
# entry carries the SHA-256 the scenarios were written against; every mode
# verifies it before measuring and --record refuses on a mismatch, because an
# oracle is only a measurement of the exact base it was recorded from.  (The
# fixture has been corrupted in place twice by a Dropbox sync trap; this guard
# turns that into a loud failure instead of a silently wrong oracle.)
BASE_FILES = {
    "synthetic-base": {
        "path": ROOT / "tests" / "data" / "synthetic-base.mpp",
        "sha256": "55c109f2f62fd6b9f05789ab002c35991a424aab18c78f92e4200df7b3fc6069",
    },
    "synthetic-calendars": {
        "path": ROOT / "tests" / "data" / "synthetic-calendars.mpp",
        "sha256": "f486f0c3235bd8a592542e942141fe873c4262e6afa19e029a9c4c79386017ca",
        # calendar uid -> name.  Built by scripts/build_calendar_fixture.py:
        # synthetic-base's three tasks converted to automatic scheduling and a
        # second NAMED base calendar added (Mon-Fri 09:00-13:00 + 14:00-18:00).
        # Project.BaseCalendars can be searched by name but exposes NO UniqueID
        # property (measured wave 19), so the uid->name map is pinned here with
        # the fixture hash -- the name is synthetic, never client data, and the
        # scenario JSON keeps ids only.
        "calendars": {4: "Shift A"},
    },
}

# Every generated .mpp / dump / .ops file must stay under build/.
BUILD_ROOT = ROOT / "build"

LINK_TYPES = {"FF": 0, "FS": 1, "SF": 2, "SS": 3}

FIELD_DURATION = 188743709        # 0x0b40001d
FIELD_CONSTRAINT_TYPE = 188743697  # 0x0b400011
FIELD_CONSTRAINT_DATE = 188743698  # 0x0b400012
FIELD_START = 188743715           # 0x0b400023
FIELD_PERCENT_COMPLETE = 188743712  # 0x0b400020
FIELD_PRIORITY = 188743705        # 0x0b400019

# The names COM exposes for the custom families the writer stores, mirroring
# core/mpp/field_value.h.  Used only by --record.
CUSTOM_FAMILIES = {
    "number": (1, 20, lambda i: f"Number{i}"),
    "date": (1, 10, lambda i: f"Date{i}"),
    "duration": (1, 10, lambda i: f"Duration{i}"),
    "cost": (1, 10, lambda i: f"Cost{i}"),
    "flag": (1, 20, lambda i: f"Flag{i}"),
    "text": (1, 30, lambda i: f"Text{i}"),
}


class HarnessError(RuntimeError):
    """A usage, fixture or subprocess failure (not a parity difference)."""


# ---------------------------------------------------------------------------
# scenario loading and validation
# ---------------------------------------------------------------------------


def rel(path):
    """A path relative to the repository root when possible, for messages."""
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def base_entry(name):
    if name not in BASE_FILES:
        raise HarnessError(
            f"unknown base '{name}'; known bases: {', '.join(sorted(BASE_FILES))}")
    return BASE_FILES[name]


def verify_base(name):
    """The base path and its pinned SHA-256, verified.  Fails loudly on change."""
    entry = base_entry(name)
    path = Path(entry["path"])
    expected = str(entry["sha256"]).lower()
    if not path.is_file():
        raise HarnessError(f"base fixture is missing: {rel(path)}")
    actual = sha256_of(path)
    if actual != expected:
        raise HarnessError(
            f"BASE FIXTURE CHANGED: {rel(path)} is {path.stat().st_size:,} B "
            f"and hashes to {actual}, expected {expected}.  An oracle is only "
            "a measurement of the exact base it was recorded from, so this "
            "run is refused.  Restore the fixture (tests/data/README.md, and "
            "the coordinator's HEAD copy), then re-run.")
    return path, expected


def require_unchanged_base(name, expected, when):
    actual = sha256_of(base_entry(name)["path"])
    if actual != expected:
        raise HarnessError(
            f"THE BASE FIXTURE CHANGED DURING THIS RUN ({when}): it now "
            f"hashes to {actual}, expected {expected}.  Every harness write "
            "must go to its output under build/; refusing the measurement.")
    return actual


def ensure_under_build(path, what):
    """Every generated .mpp/dump/.ops file stays under build/ (hard rule)."""
    resolved = Path(path).resolve()
    try:
        resolved.relative_to(BUILD_ROOT.resolve())
    except ValueError:
        raise HarnessError(
            f"{what} must be under {rel(BUILD_ROOT)} (got {path}); every "
            "generated scenario file goes under build/")
    return resolved


def workdir_for(args, suffix):
    path = Path(args.workdir) if args.workdir else DEFAULT_WORKDIR
    path = path / suffix
    ensure_under_build(path, "the work directory")
    return path


def scenario_paths():
    return sorted(SCENARIO_DIR.glob("*.json"))


def load_scenario(path):
    path = Path(path)
    if not path.is_file():
        raise HarnessError(f"no such scenario: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise HarnessError(f"{rel(path)}: cannot read JSON: {error}") from error
    if not isinstance(data, dict):
        raise HarnessError(f"{rel(path)}: the scenario must be a JSON object")
    for key in ("name", "base", "operations", "compare"):
        if key not in data:
            raise HarnessError(f"{rel(path)}: missing key '{key}'")
    name = data["name"]
    if not isinstance(name, str) or not name.strip():
        raise HarnessError(f"{rel(path)}: 'name' must be a non-empty string")
    if "/" in name or "\\" in name or name in (".", ".."):
        raise HarnessError(f"{rel(path)}: 'name' must be a plain file stem")
    base = data["base"]
    if base not in BASE_FILES:
        raise HarnessError(
            f"{rel(path)}: unknown base '{base}'; known bases: "
            f"{', '.join(sorted(BASE_FILES))}")
    try:
        verify_base(base)
    except HarnessError as error:
        raise HarnessError(f"{rel(path)}: {error}") from error
    if data["compare"] != "all":
        raise HarnessError(
            f"{rel(path)}: 'compare' must be \"all\" (the harness has no "
            f"tolerance and no field subsets)")
    operations = data["operations"]
    if not isinstance(operations, list) or not operations:
        raise HarnessError(f"{rel(path)}: 'operations' must be a non-empty list")
    normalised = []
    for index, operation in enumerate(operations):
        where = f"{rel(path)}: operation {index + 1}"
        normalised.append(validate_operation(operation, where))
    if normalised[-1]["op"] != "save":
        raise HarnessError(f"{rel(path)}: the last operation must be save")
    if any(operation["op"] == "save" for operation in normalised[:-1]):
        raise HarnessError(
            f"{rel(path)}: save may appear only once, as the last operation")
    return {
        "name": name,
        "base": base,
        "operations": normalised,
        "compare": "all",
        "path": path,
    }


def _require_keys(operation, where, keys):
    extra = sorted(set(operation) - set(keys) - {"op"})
    if extra:
        raise HarnessError(f"{where}: unexpected key(s) {', '.join(extra)}")
    missing = [key for key in keys if key not in operation]
    if missing:
        raise HarnessError(f"{where}: missing key(s) {', '.join(missing)}")


def _integer(value, where, minimum=0, maximum=None):
    if isinstance(value, bool) or not isinstance(value, int):
        raise HarnessError(f"{where}: expected an integer, got {value!r}")
    if value < minimum or (maximum is not None and value > maximum):
        raise HarnessError(f"{where}: {value} is out of range")
    return value


def _number(value, where):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise HarnessError(f"{where}: expected a number, got {value!r}")
    return value


def validate_operation(operation, where):
    if not isinstance(operation, dict):
        raise HarnessError(f"{where}: each operation must be a JSON object")
    kind = operation.get("op")
    if not isinstance(kind, str):
        raise HarnessError(f"{where}: missing string 'op'")
    if kind == "recalculate":
        _require_keys(operation, where, [])
        return {"op": kind}
    if kind == "save":
        _require_keys(operation, where, [])
        return {"op": kind}
    if kind == "set_task_field":
        _require_keys(operation, where, ["uid", "field", "value"])
        uid = _integer(operation["uid"], f"{where}: uid", minimum=1)
        field = _integer(operation["field"], f"{where}: field", minimum=1)
        value = operation["value"]
        if isinstance(value, bool):
            pass
        elif isinstance(value, int):
            pass
        elif isinstance(value, float):
            if value != value or value in (float("inf"), float("-inf")):
                raise HarnessError(f"{where}: value must be finite")
        elif isinstance(value, str):
            if not value:
                raise HarnessError(f"{where}: value must not be empty")
            if "\t" in value or "\n" in value or "\r" in value:
                raise HarnessError(
                    f"{where}: a value must not contain a tab or newline")
        else:
            raise HarnessError(
                f"{where}: value must be a number, string or boolean")
        return {"op": kind, "uid": uid, "field": field, "value": value}
    if kind == "add_link":
        _require_keys(operation, where,
                      ["predecessor_uid", "successor_uid", "type", "lag_ticks"])
        predecessor = _integer(operation["predecessor_uid"],
                               f"{where}: predecessor_uid", minimum=1)
        successor = _integer(operation["successor_uid"],
                             f"{where}: successor_uid", minimum=1)
        if predecessor == successor:
            raise HarnessError(f"{where}: a task cannot be linked to itself")
        link_type = operation["type"]
        if link_type not in LINK_TYPES:
            raise HarnessError(
                f"{where}: type must be one of {', '.join(LINK_TYPES)}")
        lag = _integer(operation["lag_ticks"], f"{where}: lag_ticks")
        if lag % 10 != 0:
            raise HarnessError(
                f"{where}: lag_ticks must be a whole number of minutes "
                f"(a multiple of 10) because COM takes minutes, got {lag}")
        if abs(lag) > 175200000:
            raise HarnessError(f"{where}: lag_ticks magnitude is out of range")
        return {"op": kind, "predecessor_uid": predecessor,
                "successor_uid": successor, "type": link_type,
                "lag_ticks": lag}
    if kind == "delete_task":
        _require_keys(operation, where, ["uid"])
        return {"op": kind,
                "uid": _integer(operation["uid"], f"{where}: uid", minimum=1)}
    if kind == "add_assignment":
        _require_keys(operation, where,
                      ["task_uid", "resource_uid", "units_percent"])
        task_uid = _integer(operation["task_uid"], f"{where}: task_uid",
                            minimum=1)
        resource_uid = _integer(operation["resource_uid"],
                                f"{where}: resource_uid", minimum=1)
        units = _number(operation["units_percent"], f"{where}: units_percent")
        if not 0 < units <= 10000:
            raise HarnessError(
                f"{where}: units_percent must be in (0, 10000]")
        return {"op": kind, "task_uid": task_uid,
                "resource_uid": resource_uid, "units_percent": units}
    if kind == "set_calendar":
        _require_keys(operation, where, ["task_uid", "calendar_uid"])
        task_uid = _integer(operation["task_uid"], f"{where}: task_uid",
                            minimum=1)
        calendar_uid = _integer(operation["calendar_uid"],
                                f"{where}: calendar_uid", minimum=1)
        return {"op": kind, "task_uid": task_uid,
                "calendar_uid": calendar_uid}
    raise HarnessError(f"{where}: unknown operation '{kind}'")


# ---------------------------------------------------------------------------
# operations file for the C++ helper
# ---------------------------------------------------------------------------


def _value_text(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        return repr(value)
    return str(value)


def compile_operations(scenario):
    lines = [f"# scenario {scenario['name']} ({rel(scenario['path'])})"]
    for operation in scenario["operations"]:
        kind = operation["op"]
        if kind == "recalculate" or kind == "save":
            lines.append(kind)
        elif kind == "set_task_field":
            lines.append("\t".join([
                kind, str(operation["uid"]), str(operation["field"]),
                _value_text(operation["value"]),
            ]))
        elif kind == "add_link":
            lines.append("\t".join([
                kind, str(operation["predecessor_uid"]),
                str(operation["successor_uid"]),
                str(LINK_TYPES[operation["type"]]),
                str(operation["lag_ticks"]),
            ]))
        elif kind == "delete_task":
            lines.append("\t".join([kind, str(operation["uid"])]))
        elif kind == "add_assignment":
            lines.append("\t".join([
                kind, str(operation["task_uid"]),
                str(operation["resource_uid"]),
                _value_text(operation["units_percent"]),
            ]))
        elif kind == "set_calendar":
            lines.append("\t".join([
                kind, str(operation["task_uid"]),
                str(operation["calendar_uid"]),
            ]))
        else:  # validated already
            raise HarnessError(f"unhandled operation '{kind}'")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# the C++ helper
# ---------------------------------------------------------------------------


def resolve_apply_bin(explicit):
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise HarnessError(f"--apply-bin does not exist: {path}")
        return path
    from_env = os.environ.get("ENGINE_DIFF_APPLY")
    if from_env:
        path = Path(from_env)
        if not path.is_file():
            raise HarnessError(
                f"ENGINE_DIFF_APPLY does not exist: {path}")
        return path
    # Wave 23 (W23-9a, audit A-4): NO fixed `build/e1` default.  The old
    # candidates resolved a stale exe that could not apply `task-calendar`, so
    # the documented command failed for reasons unrelated to the engine.  The
    # ctest lane (engine_diff_suite) and CI pass --apply-bin explicitly from the
    # current build tree; an interactive run gets the NEWEST helper under
    # build/, with a warning naming it.
    names = ["engine_diff_tests.exe", "engine_diff_tests"]
    candidates = []
    for name in names:
        candidates += list(ROOT.glob(f"build/**/{name}"))
    fresh = [c for c in candidates if c.is_file()]
    if fresh:
        chosen = max(fresh, key=lambda p: p.stat().st_mtime)
        print(f"engine_diff: using {rel(chosen)} (newest under build/); "
              f"pass --apply-bin to pin one", file=sys.stderr)
        return chosen
    raise HarnessError(
        "cannot find the engine_diff_tests helper; build it first:\n"
        "  cmake --build <your build tree> --config Release "
        "--target engine_diff_tests\n"
        "then pass --apply-bin <tree>/Release/engine_diff_tests(.exe) "
        "(or set ENGINE_DIFF_APPLY).  CMake's engine_diff_suite test does this "
        "with $<TARGET_FILE:engine_diff_tests> automatically.")


def run_helper(apply_bin, arguments, capture=False):
    command = [str(apply_bin), *[str(argument) for argument in arguments]]
    result = subprocess.run(
        command, cwd=str(ROOT), capture_output=True, text=True)
    if capture:
        return result
    if result.returncode != 0:
        detail = (result.stderr or "").strip() or (result.stdout or "").strip()
        raise HarnessError(
            f"{apply_bin.name} {' '.join(str(a) for a in arguments)} failed "
            f"(exit {result.returncode}):\n{detail}")
    return result


def apply_scenario(scenario, workdir, apply_bin, out_name=None):
    """Apply a scenario through our engine; return the path of the saved .mpp.

    The base is copied under build/ and hashed before and after; the original
    fixture is hashed before and after as well.  A run that changes either is
    refused: this is the guard against a scenario save landing on the fixture.
    """
    workdir = Path(workdir)
    ensure_under_build(workdir, "the scenario work directory")
    workdir.mkdir(parents=True, exist_ok=True)
    base, expected = verify_base(scenario["base"])
    base_copy = workdir / f"{scenario['name']}.base.mpp"
    shutil.copyfile(base, base_copy)
    if sha256_of(base_copy) != expected:
        raise HarnessError(
            f"the base copy {rel(base_copy)} does not match the fixture hash")
    ops_path = workdir / f"{scenario['name']}.ops"
    ops_path.write_text(compile_operations(scenario), encoding="utf-8")
    out_path = workdir / (out_name or f"{scenario['name']}.mpp")
    ensure_under_build(out_path, "the scenario output")
    # The C++ side re-checks this hash before and after applying the ops.
    run_helper(apply_bin, ["--apply", ops_path, base_copy, out_path, expected])
    require_unchanged_base(scenario["base"], expected, "after our engine ran")
    if sha256_of(base_copy) != expected:
        raise HarnessError(
            f"the base copy {rel(base_copy)} changed during the run")
    return out_path


def dump_state(apply_bin, mpp_path, json_path):
    ensure_under_build(json_path, "the state dump")
    run_helper(apply_bin, ["--dump", mpp_path, json_path])
    try:
        return json.loads(Path(json_path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise HarnessError(
            f"the canonical dump at {rel(json_path)} is not JSON: {error}") \
            from error


def ours_state(scenario, workdir, apply_bin):
    workdir = Path(workdir)
    out_path = apply_scenario(scenario, workdir, apply_bin)
    return dump_state(apply_bin, out_path, workdir / f"{scenario['name']}.ours.json")


# ---------------------------------------------------------------------------
# canonical dump comparison
# ---------------------------------------------------------------------------


def flatten(dump):
    """One flat {field path: value} map; the canonical comparison form.

    Families are keyed by UniqueID so the comparison is structural, not row
    order, and every leaf is compared exactly (no tolerance anywhere).
    """
    fields = {}
    fields["schema"] = dump.get("schema")
    project = dump.get("project") or {}
    for key in sorted(project):
        fields[f"project {key}"] = project[key]

    for family, singular in (("tasks", "task"), ("links", "link"),
                             ("assignments", "assignment"),
                             ("resources", "resource")):
        rows = dump.get(family) or []
        for row in rows:
            uid = row.get("uid")
            for key in sorted(row):
                if key == "uid":
                    continue
                if key == "custom":
                    for field_id in sorted(row[key]):
                        fields[
                            f"{singular} {uid} custom.{field_id}"
                        ] = row[key][field_id]
                else:
                    fields[f"{singular} {uid} {key}"] = row[key]

    table = dump.get("calendar_table") or {}
    for key in sorted(table):
        if key == "calendars":
            continue
        fields[f"calendar_table {key}"] = table[key]
    for calendar in table.get("calendars") or []:
        uid = calendar.get("uid")
        for key in sorted(calendar):
            if key == "uid":
                continue
            fields[f"calendar {uid} {key}"] = calendar[key]

    file_section = dump.get("file") or {}
    fields["file stream_count"] = file_section.get("stream_count")
    for entry in file_section.get("streams") or []:
        fields[f"file stream {entry.get('path')}"] = entry.get("size")
    return fields


MISSING = object()


def compare_dumps(ours, oracle):
    """Every differing field, sorted by path: [(path, ours, oracle)].

    Equality is type-strict as well as value-exact: a boolean is not an
    integer and 1 is not 1.0, so a tampered or re-encoded oracle cannot pass by
    Python's numeric coercion.
    """
    ours_fields = flatten(ours)
    oracle_fields = flatten(oracle)
    # `file.*` is Project's save churn, not semantics: Project rewrites 8-10
    # streams on every save (and pads the file, compacting on close), while our
    # recalculate-only save legitimately writes nothing because the fixture
    # already holds Project's own state.  Stream sizes therefore cannot be a
    # parity signal; the writer's own suites prove the "only intended bytes
    # move" invariant against Project's fixture pairs.  The field stays in the
    # dump for diagnostics and is excluded from scoring here.
    ignored_prefixes = ("file.", "file stream")
    paths = [path for path in sorted(set(ours_fields) | set(oracle_fields))
             if not path.startswith(ignored_prefixes)]
    differences = []
    for path in paths:
        left = ours_fields.get(path, MISSING)
        right = oracle_fields.get(path, MISSING)
        if type(left) is not type(right) or left != right:
            differences.append((path, left, right))
    return differences


def format_value(value):
    if value is MISSING:
        return "<missing>"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if value is None:
        return "null"
    if isinstance(value, str):
        return value
    if isinstance(value, (int, float)):
        return str(value)
    return json.dumps(value, separators=(",", ":"), sort_keys=True)


def format_difference(path, ours, oracle):
    return f"{path}: {format_value(ours)} vs {format_value(oracle)}"


def check_scenario(scenario, apply_bin, workdir, oracle_path=None):
    """Return (status, differences, field_count, message, oracle_path)."""
    if oracle_path is None:
        oracle_path = ORACLE_DIR / f"{scenario['name']}.oracle.json"
    oracle_path = Path(oracle_path)
    if not oracle_path.is_file():
        return ("unrecorded", [], 0,
                f"no recorded oracle at {rel(oracle_path)}", oracle_path)
    try:
        oracle = json.loads(oracle_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise HarnessError(
            f"cannot read the oracle at {rel(oracle_path)}: {error}") from error
    ours = ours_state(scenario, workdir, apply_bin)
    differences = compare_dumps(ours, oracle)
    return ("fail" if differences else "pass", differences,
            len(flatten(ours)), "", oracle_path)


# ---------------------------------------------------------------------------
# --record: the coordinator-only COM half
# ---------------------------------------------------------------------------


def _record_imports():
    scripts = str(ROOT / "scripts")
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    try:
        from oracle_lock import oracle_session, log  # noqa: F401
        from oracle_w3 import Guard, calc, utc  # noqa: F401
    except ImportError as error:  # pragma: no cover - coordinator machine only
        raise HarnessError(
            "the --record half needs pywin32 and scripts/oracle_lock.py "
            f"({error}); it is coordinator-only and cannot run in CI") \
            from error
    return oracle_session, log, Guard, calc, utc


def _datetime_from_text(text):
    """Parse the operation grammar's 'YYYY-MM-DD HH:MM[:SS]' (no timezone)."""
    import datetime as dt
    text = str(text).strip()
    if text.endswith("+00:00"):
        text = text[:-6]
    for pattern in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d",
                    "%Y-%m-%dT%H:%M:%S", "%Y-%m-%dT%H:%M"):
        try:
            return dt.datetime.strptime(text, pattern)
        except ValueError:
            continue
    raise HarnessError(f"cannot parse the date '{text}' for COM")


def _custom_attribute(field_id):
    """The COM property name for a PjTask custom field id, or None.

    Mirrors core/mpp/field_value.h's id arithmetic.  Only used by --record.
    """
    for family, (first, count, name) in CUSTOM_FAMILIES.items():
        for index in range(1, count + 1):
            candidate = _custom_field_id(family, index)
            if candidate == field_id:
                return family, name(index)
    return None, None


def _custom_field_id(family, index):
    """The PjTask field id for a custom family index (core/mpp/field_value.h)."""
    if family == "text":
        if index <= 6:
            return 0x0B400033 + 3 * (index - 1)
        if index <= 10:
            return 0x0B400042 + (index - 6)
        if index <= 30:
            return 0x0B40013D + (index - 11)
    elif family == "flag":
        if index <= 10:
            return 0x0B400048 + (index - 1)
        if index <= 20:
            return 0x0B400124 + (index - 11)
    elif family == "number":
        if index <= 5:
            return 0x0B400057 + (index - 1)
        if index <= 20:
            return 0x0B40012E + (index - 6)
    elif family == "duration":
        if index <= 3:
            return 0x0B400067 + (index - 1)
        if index <= 10:
            return 0x0B400113 + (index - 4)
    elif family == "cost":
        if index <= 3:
            return 0x0B40006A + (index - 1)
        if index <= 10:
            return 0x0B400102 + (index - 4)
    elif family == "date":
        if index <= 10:
            return 0x0B400109 + (index - 1)
    return None


def _task_by_uid(proj, uid):
    for index in range(1, proj.Tasks.Count + 1):
        task = proj.Tasks(index)
        if int(task.UniqueID) == uid:
            return task
    raise HarnessError(f"MS Project has no task with UniqueID {uid}")


def _lag_text(lag_ticks):
    if lag_ticks == 0:
        return 0
    minutes = lag_ticks // 10
    return f"{minutes}m"


def _apply_com_operation(app, proj, operation, calc, utc, mpp_out, state):
    """One operation through MS Project COM.  --record only; not exercised."""
    kind = operation["op"]
    if kind == "recalculate":
        calc(app)
        return
    if kind == "save":
        calc(app)
        target = Path(mpp_out).resolve()
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            target.unlink()
        app.FileSaveAs(Name=str(target))  # absolute: Project uses its own cwd
        state["saved"] = target
        return
    if kind == "set_task_field":
        task = _task_by_uid(proj, operation["uid"])
        field = operation["field"]
        value = operation["value"]
        if field == FIELD_DURATION:
            # COM Duration is minutes; the scenario's unit is file ticks.
            task.Duration = f"{int(value) // 10}m"
        elif field == FIELD_CONSTRAINT_TYPE:
            task.ConstraintType = int(value)
        elif field == FIELD_CONSTRAINT_DATE:
            task.ConstraintDate = utc(_datetime_from_text(value))
        elif field == FIELD_START:
            task.Start = utc(_datetime_from_text(value))
        elif field == FIELD_PERCENT_COMPLETE:
            task.PercentComplete = int(value)
        elif field == FIELD_PRIORITY:
            task.Priority = int(value)
        else:
            family, attribute = _custom_attribute(field)
            if attribute is None:
                raise HarnessError(
                    f"set_task_field: field id {field} has no COM property in "
                    f"the documented subset")
            if family == "text":
                raise HarnessError(
                    "set_task_field: Text custom fields are refused; scenarios "
                    "carry ids and numbers only")
            if family == "date":
                setattr(task, attribute, utc(_datetime_from_text(value)))
            elif family == "duration":
                setattr(task, attribute, f"{int(value) // 10}m")
            elif family == "flag":
                setattr(task, attribute, bool(value))
            else:
                setattr(task, attribute, float(value))
        return
    if kind == "add_link":
        predecessor = _task_by_uid(proj, operation["predecessor_uid"])
        successor = _task_by_uid(proj, operation["successor_uid"])
        successor.TaskDependencies.Add(
            predecessor, LINK_TYPES[operation["type"]],
            _lag_text(operation["lag_ticks"]))
        calc(app)
        return
    if kind == "delete_task":
        _task_by_uid(proj, operation["uid"]).Delete()
        calc(app)
        return
    if kind == "add_assignment":
        task = _task_by_uid(proj, operation["task_uid"])
        # INFERENCE, stated as such: COM's Assignments.Add takes a ResourceID
        # (the resource's ID column), while the scenario and our writer address
        # the resource by UniqueID.  Every resource in the committed fixture has
        # ID == UniqueID (reader.h); a file where they differ would need the ID
        # looked up here.
        assignment = task.Assignments.Add(
            TaskID=task.ID, ResourceID=operation["resource_uid"])
        units = float(operation["units_percent"])
        if units != 100.0:
            # COM's Units is a fraction: 0.5 is 50 percent.
            assignment.Units = units / 100.0
        calc(app)
        return
    if kind == "set_calendar":
        task = _task_by_uid(proj, operation["task_uid"])
        uid = operation["calendar_uid"]
        # Task.Calendar is a NAME (get/put) and this Project build has no
        # `Project.Calendars`.  `Project.BaseCalendars` exists but carries no
        # UniqueID (measured wave 19), so the uid->name map is pinned with the
        # fixture in BASE_FILES[...]["calendars"].
        name = (state.get("calendars") or {}).get(uid)
        if not name:
            raise HarnessError(
                f"set_calendar: no calendar uid {uid} in the pinned calendar map "
                f"for base '{state.get('base')}' (Project.BaseCalendars has no "
                "UniqueID, so the map comes from the fixture registry)")
        base_calendars = proj.BaseCalendars
        names = [base_calendars(i).Name
                 for i in range(1, base_calendars.Count + 1)]
        if name not in names:
            raise HarnessError(
                f"set_calendar: the pinned name for uid {uid} is not among "
                f"Project's base calendars {names}; the fixture and the pinned "
                "map disagree")
        task.Calendar = name
        calc(app)
        return
    raise HarnessError(f"unhandled operation '{kind}'")


def _mpp_rows_evidence(saved, workdir, name):
    """Run the independent byte-level row dump as recording evidence.

    scripts/mpp_rows.py reads TBkndTask/FixedData directly (no reader code), so
    saving its output next to the recorded .mpp is a second, independent view of
    the task rows Project wrote.  It prints uids/dates/ticks only, never names.
    Best effort: a missing olefile is not a recording failure.
    """
    script = ROOT / "scripts" / "mpp_rows.py"
    if not script.is_file():
        return None
    try:
        import olefile  # noqa: F401
    except ImportError:
        return None
    result = subprocess.run(
        [sys.executable, str(script), str(saved)],
        cwd=str(ROOT), capture_output=True, text=True)
    if result.returncode != 0:
        return None
    path = Path(workdir) / f"{name}.mpp_rows.txt"
    path.write_text(result.stdout, encoding="utf-8")
    return path


def cmd_record(args):
    oracle_session, log, Guard, calc, utc = _record_imports()
    scenario = load_scenario(args.record)
    base, expected = verify_base(scenario["base"])
    base = base.resolve()
    workdir = workdir_for(args, "oracle")
    workdir.mkdir(parents=True, exist_ok=True)
    mpp_out = workdir / f"{scenario['name']}.oracle.mpp"
    apply_bin = resolve_apply_bin(args.apply_bin)
    print(f"record: {scenario['name']}: base {rel(base)} "
          f"({base.stat().st_size:,} B, sha256 {expected})")
    print(f"record: {len(scenario['operations'])} operation(s); "
          f"COORDINATOR-ONLY path (COM); not exercised by stream E1")
    print("record: the base hash is pinned; a changed fixture is refused "
          "before MS Project is opened and re-checked after the save")

    state = {"base": scenario["base"],
             "calendars": {int(uid): str(name) for uid, name in
                           base_entry(scenario["base"]).get("calendars", {}).items()}}
    # MS Project rewrites a file it opens (observed three times: the committed
    # fixture came back with a different hash after a session).  Record from a
    # copy under build/, never from the committed fixture.
    run_base = workdir / f"{scenario['name']}.base.mpp"
    shutil.copy2(base, run_base)
    project_version = ""
    project_build = ""
    with oracle_session() as app:
        with Guard(600):
            # Wave 23 (W23-9c): provenance for the oracle header.  COM exposes
            # Version ("16.0") and Build (the 20430 in 16.0.20430.20182).
            project_version = str(getattr(app, "Version", "") or "")
            project_build = str(getattr(app, "Build", "") or "")
            app.FileOpen(str(run_base))
            proj = app.ActiveProject
            if proj is None:
                raise HarnessError("MS Project did not open the base fixture")
            for operation in scenario["operations"]:
                _apply_com_operation(app, proj, operation, calc, utc, mpp_out,
                                     state)
            if "saved" not in state:
                raise HarnessError("the operations did not end with save")
            app.FileClose(0)  # Save=0: never leave the project dirty
    saved = state["saved"]
    log(f"Project saved {saved} ({saved.stat().st_size:,} B)")
    require_unchanged_base(scenario["base"], expected,
                           "after the MS Project session")
    evidence = _mpp_rows_evidence(saved, workdir, scenario["name"])
    if evidence is not None:
        log(f"independent row dump: {evidence}")

    oracle = dump_state(apply_bin, saved,
                        workdir / f"{scenario['name']}.oracle.raw.json")
    # W23-9c provenance: Project version/build, the UTC recording instant and
    # the exact source .mpp hash.  compare_dumps() flattens known families only,
    # so this header never enters the field comparison.
    oracle = {
        "provenance": {
            "project_version": project_version,
            "project_build": project_build,
            "recorded_at": datetime.datetime.now(datetime.timezone.utc)
            .strftime("%Y-%m-%dT%H:%M:%SZ"),
            "source": rel(base),
            "source_sha256": str(expected).lower(),
        },
        **oracle,
    }
    # The one tests/data write in this script: the committed oracle JSON.
    # --record is coordinator-only; --check/--suite/--selftest never write
    # under tests/data (they only read the oracle).
    ORACLE_DIR.mkdir(parents=True, exist_ok=True)
    oracle_path = ORACLE_DIR / f"{scenario['name']}.oracle.json"
    oracle_path.write_text(
        json.dumps(oracle, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    print(f"record: wrote {rel(oracle_path)} "
          f"({len(flatten(oracle))} fields, numbers only)")
    print("record: now run: python scripts/engine_diff.py "
          f"--check {rel(scenario['path'])}")
    return 0


# ---------------------------------------------------------------------------
# --check / --suite / --selftest
# ---------------------------------------------------------------------------


def cmd_check(args):
    scenario = load_scenario(args.check)
    apply_bin = resolve_apply_bin(args.apply_bin)
    workdir = workdir_for(args, "check")
    status, differences, field_count, message, oracle_path = check_scenario(
        scenario, apply_bin, workdir, args.oracle)
    if status == "unrecorded":
        print(f"{scenario['name']}: UNRECORDED ({message})")
        print("run the coordinator-only record step first: "
              f"python scripts/engine_diff.py --record {rel(scenario['path'])}")
        return 3
    if status == "fail":
        for path, ours, oracle in differences:
            print(format_difference(path, ours, oracle))
        print(f"{scenario['name']}: FAIL ({len(differences)}/{field_count} "
              f"fields differ)")
        print("reproduce: python scripts/engine_diff.py --check "
              f"{rel(scenario['path'])}")
        print(f"oracle: {rel(oracle_path)}")
        return 1
    print(f"{scenario['name']}: PASS ({field_count} fields)")
    return 0


def cmd_suite(args):
    apply_bin = resolve_apply_bin(args.apply_bin)
    expected = None
    if args.expect:
        try:
            pass_text, total_text = args.expect.split("/", 1)
            expected = (int(pass_text), int(total_text))
        except ValueError as error:
            raise HarnessError(
                f"--expect wants PASS/TOTAL (e.g. 7/8), got {args.expect!r}") from error
    workdir = workdir_for(args, "suite")
    scenarios = scenario_paths()
    if not scenarios:
        raise HarnessError(f"no scenarios found in {rel(SCENARIO_DIR)}")
    for name in sorted(BASE_FILES):
        entry = base_entry(name)
        print(f"suite: base {name} = {entry['sha256']} "
              f"({rel(entry['path'])})")
    rows = []
    recorded = 0
    failed = 0
    unrecorded = 0
    failed_names = []
    for path in scenarios:
        scenario = load_scenario(path)
        status, differences, field_count, message, oracle_path = check_scenario(
            scenario, apply_bin, workdir, None)
        if status == "pass":
            recorded += 1
            rows.append(f"{scenario['name']}: PASS ({field_count} fields)")
        elif status == "fail":
            recorded += 1
            failed += 1
            failed_names.append(scenario["name"])
            rows.append(f"{scenario['name']}: FAIL "
                        f"({len(differences)}/{field_count} fields differ)")
            for diff_path, ours, oracle in differences:
                rows.append("    " + format_difference(diff_path, ours, oracle))
        else:
            unrecorded += 1
            rows.append(f"{scenario['name']}: UNRECORDED (0 fields)")
    for row in rows:
        print(row)
    print()
    print(f"suite: {len(scenarios)} scenario(s), {recorded} recorded "
          f"({recorded - failed} pass, {failed} fail), {unrecorded} unrecorded")
    if unrecorded:
        print("unrecorded scenarios need the coordinator-only record step:")
        for path in scenarios:
            scenario = load_scenario(path)
            oracle = ORACLE_DIR / f"{scenario['name']}.oracle.json"
            if not oracle.is_file():
                print(f"  python scripts/engine_diff.py --record "
                      f"{rel(path)}")
    if getattr(args, "expect_fail", None):
        # Wave 24 (W24-7/R10/V): pin the FAILING SET BY NAME.  `--expect 7/8`
        # only counted passing scenarios, so constraint-mso starting to pass
        # while another scenario broke would still be green.  The set is
        # compared exactly, and every other scenario must PASS.
        wanted = {name.strip() for name in args.expect_fail.split(",")
                  if name.strip()}
        actual_failures = set(failed_names)
        if unrecorded == 0 and actual_failures == wanted:
            print(f"suite: expected failing set reached ({', '.join(sorted(wanted))})")
            return 0
        print(f"suite: expected failing {sorted(wanted)}, got "
              f"{sorted(actual_failures)} with {failed} failing and "
              f"{unrecorded} unrecorded")
        return 1
    if expected is not None:
        actual = (recorded - failed, len(scenarios))
        if unrecorded == 0 and actual == expected:
            print(f"suite: expected state reached ({actual[0]}/{actual[1]})")
            return 0
        print(f"suite: expected {expected[0]}/{expected[1]}, got "
              f"{actual[0]}/{actual[1]} with {unrecorded} unrecorded")
        return 1
    if failed:
        return 1
    if unrecorded:
        return 3
    return 0


def _walk_strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for key, item in value.items():
            yield str(key)
            yield from _walk_strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from _walk_strings(item)


def validate_dump(dump, label):
    """The dump's documented shape.  Returns a list of failure messages."""
    problems = []
    if not isinstance(dump, dict):
        return [f"{label}: the dump is not a JSON object"]
    for key in ("schema", "project", "tasks", "links", "assignments",
                "resources", "calendar_table", "file"):
        if key not in dump:
            problems.append(f"{label}: the dump has no '{key}' section")
    if dump.get("schema") != SCHEMA:
        problems.append(f"{label}: schema is {dump.get('schema')!r}, "
                        f"expected {SCHEMA!r}")
    if not isinstance(dump.get("tasks"), list):
        problems.append(f"{label}: 'tasks' is not a list")
    for row in dump.get("tasks") or []:
        # late_start/late_finish are schema 2 (the stored +112/+116 pair).
        # early start/finish are already carried by start/finish, because on
        # every COM read-back Project reports EarlyStart == Start and
        # EarlyFinish == Finish (32/32 w3-o4 rows, 4/4 w3-o1; see
        # core/mpp/reader.h), so no duplicate early_* key is emitted.
        for key in ("uid", "duration_ticks", "start", "finish",
                    "calculated_start", "calculated_finish",
                    "late_start", "late_finish",
                    "constraint_type", "constraint_date", "calendar_uid"):
            if key not in row:
                problems.append(f"{label}: task {row.get('uid')} has no "
                                f"'{key}'")
    if not isinstance(dump.get("resources"), list):
        problems.append(f"{label}: 'resources' is not a list")
    for row in dump.get("resources") or []:
        # type/max_units_x100 were already there; calendar_uid is schema 2 and
        # is derived from the calendar table's resource column.
        for key in ("uid", "type", "type_stored", "max_units_x100",
                    "calendar_uid"):
            if key not in row:
                problems.append(f"{label}: resource {row.get('uid')} has no "
                                f"'{key}'")
    streams = (dump.get("file") or {}).get("streams")
    if not isinstance(streams, list) or not streams:
        problems.append(f"{label}: the file section has no stream sizes")
    forbidden = ("Task A", "Task B", "Task C", "Crew")
    for text in _walk_strings(dump):
        for name in forbidden:
            if name in text:
                problems.append(
                    f"{label}: the dump leaks the fixture name {name!r} in "
                    f"{text!r}; the dump must carry numbers and ids only")
    return problems


def cmd_selftest(args):
    apply_bin = resolve_apply_bin(args.apply_bin)
    workdir = workdir_for(args, "selftest")
    workdir.mkdir(parents=True, exist_ok=True)
    scenarios = scenario_paths()
    for name in sorted({load_scenario(path)["base"] for path in scenarios}):
        path, digest = verify_base(name)
        print(f"selftest: base {name} {rel(path)} sha256 {digest} "
              f"({path.stat().st_size:,} B)")
    failures = []
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            failures.append(message)
            print(f"FAIL: {message}")
        return condition

    scenarios = scenario_paths()
    check(bool(scenarios), "there is at least one committed scenario")
    check(len(scenarios) >= 5,
          f"the scenario corpus has 5+ files (found {len(scenarios)})")
    for path in scenarios:
        scenario = load_scenario(path)
        check(scenario["compare"] == "all",
              f"{scenario['name']}: compare is all")
        if not check(scenario["operations"][-1]["op"] == "save",
                     f"{scenario['name']}: the last operation is save"):
            continue
        try:
            ours = ours_state(scenario, workdir / "scenarios", apply_bin)
        except HarnessError as error:
            check(False, f"{scenario['name']}: apply/dump failed: {error}")
            continue
        problems = validate_dump(ours, scenario["name"])
        for problem in problems:
            check(False, problem)
        check(not problems, f"{scenario['name']}: the dump has the "
                            f"documented schema and no names")
        if scenario["name"] == "duration-change":
            task = next((row for row in ours["tasks"] if row["uid"] == 1), None)
            if check(task is not None,
                     "duration-change: task 1 is in the dump"):
                check(task["duration_ticks"] == 14400,
                      "duration-change: the dump shows 14400 ticks for task 1")
        if scenario["name"] == "assignment-50pct":
            added = [row for row in ours["assignments"]
                     if row["task_uid"] == 1 and row["resource_uid"] == 1]
            check(len(added) == 1,
                  "assignment-50pct: the dump shows the new assignment")
            if added:
                check(added[0]["units_x100"] == 5000,
                      "assignment-50pct: units_x100 is 5000")
        if scenario["name"] == "link-fs-lag":
            check(len(ours["links"]) == 1,
                  "link-fs-lag: the dump shows one link")
            if ours["links"]:
                check(ours["links"][0]["type"] == 1 and
                      ours["links"][0]["lag_ticks"] == 4800,
                      "link-fs-lag: FS with a 4800-tick lag")
        if scenario["name"] == "delete-task-with-successor":
            check([row["uid"] for row in ours["tasks"]] == [1, 3],
                  "delete-task-with-successor: uids 1 and 3 remain")
            check(not ours["links"],
                  "delete-task-with-successor: the links are gone")
        if scenario["name"] == "recalculate":
            # Schema 2: the stored late pair and the resource calendar uid.
            task1 = next((row for row in ours["tasks"] if row["uid"] == 1), None)
            if check(task1 is not None, "recalculate: task 1 is in the dump"):
                check(task1.get("late_start") == "2026-01-04 13:00:00+00:00",
                      "recalculate: task 1 late_start is the stored +112 word")
                check(task1.get("late_finish") == "2026-01-09 12:00:00+00:00",
                      "recalculate: task 1 late_finish is the stored +116 word")
            task2 = next((row for row in ours["tasks"] if row["uid"] == 2), None)
            if check(task2 is not None, "recalculate: task 2 is in the dump"):
                check(task2.get("late_start") != task2.get("start"),
                      "recalculate: task 2's late_start (+112) is a distinct "
                      "stored word, not a copy of start")
            crew = next((row for row in ours["resources"] if row["uid"] == 1),
                        None)
            if check(crew is not None,
                     "recalculate: resource 1 is in the dump"):
                check(crew.get("calendar_uid") == 3,
                      "recalculate: the resource calendar uid is 3 "
                      "(the TBkndCal row whose resource column is 1)")

    # ---- the diff machinery: a self-recorded oracle passes ---------------
    scenario = load_scenario(SCENARIO_DIR / "02-duration-change.json")
    ours = ours_state(scenario, workdir / "selfcheck", apply_bin)
    self_oracle = workdir / "self-oracle.json"
    self_oracle.write_text(json.dumps(ours, indent=2) + "\n", encoding="utf-8")
    self_workdir = workdir / "selfcheck" / "oracle"
    self_workdir.mkdir(parents=True, exist_ok=True)
    status, differences, field_count, message, _ = check_scenario(
        scenario, apply_bin, self_workdir, self_oracle)
    check(status == "pass" and not differences,
          "a self-recorded oracle passes --check "
          f"(status {status}, {len(differences)} difference(s), {message})")
    check(field_count > 100,
          f"the comparison covers more than 100 fields (got {field_count})")

    # ---- a tampered oracle fails naming the field ------------------------
    tampered = copy.deepcopy(ours)
    target = next((row for row in tampered["tasks"] if row["uid"] == 1), None)
    if check(target is not None, "tamper: task 1 is in the dump"):
        target["duration_ticks"] += 1
    tampered_path = workdir / "tampered-oracle.json"
    tampered_path.write_text(json.dumps(tampered, indent=2) + "\n",
                             encoding="utf-8")
    tamper_workdir = workdir / "tamper" / "oracle"
    tamper_workdir.mkdir(parents=True, exist_ok=True)
    status, differences, field_count, message, _ = check_scenario(
        scenario, apply_bin, tamper_workdir, tampered_path)
    check(status == "fail",
          "a tampered oracle makes --check fail "
          f"(status {status}, {message})")
    check(len(differences) == 1,
          f"the tampered oracle produces exactly one difference "
          f"(got {len(differences)})")
    if differences:
        line = format_difference(*differences[0])
        check(line.startswith("task 1 duration_ticks:"),
              f"the difference names the field: {line!r}")
        check("14400" in line and "14401" in line,
              f"the difference shows both values: {line!r}")
    else:
        check(False, "the tampered oracle produced no difference")

    # ---- a stale schema-1 oracle fails cleanly, naming the new fields -----
    # The committed oracles are re-recorded by the coordinator; until then they
    # are engine-diff/1 and lack the schema-2 leaves.  A --check against one
    # must be a clean FAIL that names what is missing, never a crash: flatten()
    # unions the keys and reports each one-sided leaf as MISSING.
    stale = copy.deepcopy(ours)
    stale["schema"] = "engine-diff/1"
    for row in stale["tasks"]:
        row.pop("late_start", None)
        row.pop("late_finish", None)
    for row in stale["resources"]:
        row.pop("calendar_uid", None)
    stale_path = workdir / "stale-v1-oracle.json"
    stale_path.write_text(json.dumps(stale, indent=2) + "\n", encoding="utf-8")
    stale_workdir = workdir / "stale" / "oracle"
    stale_workdir.mkdir(parents=True, exist_ok=True)
    status, differences, field_count, message, _ = check_scenario(
        scenario, apply_bin, stale_workdir, stale_path)
    check(status == "fail",
          "a stale engine-diff/1 oracle makes --check fail "
          f"(status {status}, {message})")
    stale_paths = [path for path, _, _ in differences]
    check("schema" in stale_paths,
          "the stale oracle difference names the schema")
    check(any(path.endswith("late_start") for path in stale_paths),
          "the stale oracle difference names task late_start")
    check(any(path.endswith("late_finish") for path in stale_paths),
          "the stale oracle difference names task late_finish")
    check(any(path.endswith("calendar_uid") for path in stale_paths),
          "the stale oracle difference names resource calendar_uid")

    print(f"selftest: {checks} checks, {len(failures)} failure(s)")
    if failures:
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("selftest: the our-side apply/dump/diff machinery works; this does "
          "NOT claim parity (the recorded oracle does that)")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Differential engine/backend harness (docs/engine-parity.md).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("EXAMPLES.")[1].split("The oracle output")[0]
        if "EXAMPLES." in __doc__ else None)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--record", metavar="SCENARIO",
                       help="COORDINATOR ONLY: apply a scenario with MS Project "
                            "COM and write its oracle JSON")
    group.add_argument("--check", metavar="SCENARIO",
                       help="apply a scenario through our engine and diff it "
                            "against the recorded oracle (no COM)")
    parser.add_argument("--expect", metavar="PASS/TOTAL",
                        help="with --suite: the exact recorded state to accept, "
                             "e.g. 7/8 (kept for old commands; the ctest lane "
                             "now uses --expect-fail, which names the scenario)")
    parser.add_argument("--expect-fail", metavar="SCENARIOS",
                        help="with --suite: the exact comma-separated set of "
                             "scenarios expected to FAIL (wave 24: "
                             "'constraint-mso').  Every other scenario must "
                             "pass, and the set is compared by name, not by "
                             "count")
    group.add_argument("--suite", action="store_true",
                       help="every scenario, PASS/FAIL/UNRECORDED and a summary")
    group.add_argument("--selftest", action="store_true",
                       help="prove the apply/dump/diff machinery without "
                            "MS Project")
    parser.add_argument("--apply-bin", metavar="PATH",
                        help="the engine_diff_tests helper (default: find it "
                             "under build/e1, or $ENGINE_DIFF_APPLY)")
    parser.add_argument("--workdir", metavar="PATH",
                        help="where generated .mpp/dump files go "
                             "(default build/e1/engine-diff)")
    parser.add_argument("--oracle", metavar="PATH",
                        help="with --check: compare against this oracle JSON "
                             "instead of tests/data/scenarios/<name>.oracle.json")
    args = parser.parse_args(argv)
    try:
        if args.record:
            return cmd_record(args)
        if args.check:
            return cmd_check(args)
        if args.suite:
            return cmd_suite(args)
        if args.selftest:
            return cmd_selftest(args)
        return 2
    except HarnessError as error:
        print(f"engine_diff: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
