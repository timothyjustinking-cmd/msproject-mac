"""
Phase 2b: Enum resolution — replace magic numbers with named enum values.

The typelib contains 183 enums with 5,884 named values.  This script scans
decompiled code for switch cases and comparisons that match enum value ranges,
then outputs a mapping of (function, constant) → enum member.

Strategy:
  1. Build a value→enum lookup from the typelib (handling collisions — the same
     integer can appear in multiple enums).
  2. Scan decompiled C for:
     a. switch/case constants
     b. == / != comparisons with literal constants
     c. Assignment of literal constants to VARIANT/int fields
  3. Disambiguate collisions using context:
     - If a function handles a known field (from field_id_map.tsv), prefer the
       enum whose domain matches that field's type.
     - If multiple cases in one switch all belong to the same enum, the
       remaining cases likely do too.
  4. Also scan for the field-ID constants themselves (0x0b4NNNNN etc.) and
     annotate with property names.

Reads:
  analysis/typelib.tsv
  analysis/field_id_map.tsv       (from Phase 2a)
  analysis/named_functions_p1.tsv
  decompiled/part_NNNN.c

Produces:
  analysis/enum_constants.tsv     (func_addr, constant, enum_name, member_name, context)
  analysis/enum_summary.tsv       (enum_name, n_values, n_resolved, coverage%)
  analysis/phase2b_summary.txt
"""

import csv
import os
import re
import glob
import collections

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_enums():
    """Load all enum definitions from typelib. Returns:
      - enums: {enum_name: {value: member_name, ...}}
      - value_to_enums: {int_value: [(enum_name, member_name), ...]}
    """
    enums = collections.defaultdict(dict)
    value_to_enums = collections.defaultdict(list)
    with open(os.path.join(BASE, "analysis/typelib.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            if row["typekind"] != "ENUM":
                continue
            enum_name = row["typeinfo"]
            value = int(row["memid"])
            member = row["name"]
            enums[enum_name][value] = member
            value_to_enums[value].append((enum_name, member))
    return dict(enums), dict(value_to_enums)


def load_field_id_map():
    """Load field ID → property mappings from Phase 2a."""
    fid_map = {}
    path = os.path.join(BASE, "analysis/field_id_map.tsv")
    if not os.path.exists(path):
        return fid_map
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            fid_map[row["func_addr"]] = row
    return fid_map


def load_named_functions():
    names = {}
    with open(os.path.join(BASE, "analysis/named_functions_p1.tsv"), encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            names[row["func_addr"]] = row["assigned_name"]
    return names


def scan_for_enum_usage():
    """Scan decompiled code for switch cases and comparisons."""
    print("Scanning decompiled code for enum-like constants...")

    named_funcs = load_named_functions()
    enums, value_to_enums = load_enums()

    # Pre-compute the set of interesting values (enum values)
    enum_values = set(value_to_enums.keys())
    # Remove very common values that cause false positives
    noise_values = {0, 1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xff, 0xffff, 0xffffffff}

    func_header_pattern = re.compile(r"^// ==== (FUN_[0-9a-f]+) @ ([0-9a-f]+) ====$")

    # case 0xNN: pattern
    case_pattern = re.compile(r"case\s+(0x[0-9a-fA-F]+|-?\d+)\s*:")
    # == or != comparison with constant
    cmp_pattern = re.compile(r"(?:==|!=)\s*(0x[0-9a-fA-F]+|-?\d+)\b")
    # Also look for hex field IDs: 0x0b4NNNNN, 0x0c4NNNNN, etc.
    field_id_pattern = re.compile(r'\b(0x[0-9a-fA-F]{7,8})\b')

    results = []
    switch_groups = collections.defaultdict(list)  # func_addr → list of case values
    current_func_addr = None

    total_files = sorted(glob.glob(os.path.join(BASE, "decompiled/part_*.c")))
    for fi, fpath in enumerate(total_files):
        if fi % 5 == 0:
            print(f"  Scanning {os.path.basename(fpath)} ({fi+1}/{len(total_files)})...")

        with open(fpath, encoding="utf-8", errors="replace") as f:
            in_switch = False
            for line in f:
                m = func_header_pattern.match(line)
                if m:
                    # Flush previous function's switch groups
                    if current_func_addr and switch_groups.get(current_func_addr):
                        _resolve_switch_group(
                            current_func_addr, switch_groups[current_func_addr],
                            value_to_enums, enums, named_funcs, results
                        )
                    current_func_addr = m.group(2)
                    switch_groups[current_func_addr] = []
                    continue

                if current_func_addr is None:
                    continue

                # Detect switch cases
                for cm in case_pattern.finditer(line):
                    val_str = cm.group(1)
                    val = int(val_str, 16) if val_str.startswith("0x") else int(val_str)
                    if val not in noise_values and val in enum_values:
                        switch_groups[current_func_addr].append(val)

                # Detect comparisons
                for cm in cmp_pattern.finditer(line):
                    val_str = cm.group(1)
                    val = int(val_str, 16) if val_str.startswith("0x") else int(val_str)
                    if val not in noise_values and val in enum_values:
                        func_name = named_funcs.get(current_func_addr, f"FUN_{current_func_addr}")
                        candidates = value_to_enums[val]
                        if len(candidates) == 1:
                            ename, mname = candidates[0]
                            results.append({
                                "func_addr": current_func_addr,
                                "func_name": func_name,
                                "constant": val,
                                "constant_hex": f"0x{val:x}" if val >= 0 else str(val),
                                "enum_name": ename,
                                "member_name": mname,
                                "context": "comparison",
                                "confidence": 80,
                            })

    # Flush last function
    if current_func_addr and switch_groups.get(current_func_addr):
        _resolve_switch_group(
            current_func_addr, switch_groups[current_func_addr],
            value_to_enums, enums, named_funcs, results
        )

    return results, enums


def _resolve_switch_group(func_addr, case_values, value_to_enums, enums, named_funcs, results):
    """Given a set of case values from one switch statement in one function,
    try to determine which enum they belong to."""
    if not case_values:
        return

    func_name = named_funcs.get(func_addr, f"FUN_{func_addr}")

    # Count how many case values match each enum
    enum_scores = collections.Counter()
    for val in case_values:
        if val in value_to_enums:
            for ename, _ in value_to_enums[val]:
                enum_scores[ename] += 1

    if not enum_scores:
        return

    best_enum, best_count = enum_scores.most_common(1)[0]
    total_cases = len(case_values)

    # If most cases belong to one enum, assign all matching values to it
    confidence = 90 if best_count >= total_cases * 0.6 else 70

    for val in case_values:
        if val not in value_to_enums:
            continue
        candidates = value_to_enums[val]
        # Prefer the best-scoring enum if this value belongs to it
        assigned = None
        for ename, mname in candidates:
            if ename == best_enum:
                assigned = (ename, mname)
                break
        if assigned is None and len(candidates) == 1:
            assigned = candidates[0]
        if assigned is None:
            assigned = (best_enum, f"?{val}")
            confidence = 50

        results.append({
            "func_addr": func_addr,
            "func_name": func_name,
            "constant": val,
            "constant_hex": f"0x{val:x}" if val >= 0 else str(val),
            "enum_name": assigned[0],
            "member_name": assigned[1],
            "context": f"switch({best_count}/{total_cases} cases match {best_enum})",
            "confidence": confidence,
        })


def write_results(results, enums):
    # Deduplicate: keep highest confidence per (func_addr, constant)
    best = {}
    for r in results:
        key = (r["func_addr"], r["constant"])
        if key not in best or r["confidence"] > best[key]["confidence"]:
            best[key] = r

    deduped = sorted(best.values(), key=lambda r: (r["enum_name"], r["constant"]))

    outpath = os.path.join(BASE, "analysis/enum_constants.tsv")
    with open(outpath, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["func_addr", "func_name", "constant", "constant_hex",
                     "enum_name", "member_name", "context", "confidence"])
        for r in deduped:
            w.writerow([
                r["func_addr"], r["func_name"], r["constant"], r["constant_hex"],
                r["enum_name"], r["member_name"], r["context"], r["confidence"],
            ])
    print(f"Wrote {len(deduped)} enum constant mappings to {outpath}")

    # Enum coverage summary
    enum_resolved = collections.Counter(r["enum_name"] for r in deduped)
    summary_path = os.path.join(BASE, "analysis/enum_summary.tsv")
    with open(summary_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["enum_name", "n_defined_values", "n_resolved_usages", "n_distinct_values_seen"])
        for ename in sorted(enums.keys()):
            n_defined = len(enums[ename])
            n_resolved = enum_resolved.get(ename, 0)
            distinct_seen = len(set(
                r["constant"] for r in deduped if r["enum_name"] == ename
            ))
            w.writerow([ename, n_defined, n_resolved, distinct_seen])
    print(f"Wrote enum summary to {summary_path}")

    # Text summary
    txt_path = os.path.join(BASE, "analysis/phase2b_summary.txt")
    with open(txt_path, "w", encoding="utf-8") as f:
        f.write("=" * 70 + "\n")
        f.write("Phase 2b: Enum Resolution Summary\n")
        f.write("=" * 70 + "\n\n")
        f.write(f"Enums in typelib: {len(enums)}\n")
        f.write(f"Total enum values defined: {sum(len(v) for v in enums.values())}\n")
        f.write(f"Enum constant usages resolved: {len(deduped)}\n")
        f.write(f"Distinct (func, constant) pairs: {len(deduped)}\n")
        f.write(f"Enums with at least one resolved usage: {len(enum_resolved)}\n\n")

        f.write("Top 20 most-referenced enums:\n")
        for ename, count in enum_resolved.most_common(20):
            n_def = len(enums.get(ename, {}))
            f.write(f"  {ename}: {count} usages ({n_def} defined values)\n")

        # Confidence distribution
        conf_counts = collections.Counter(r["confidence"] for r in deduped)
        f.write(f"\nConfidence distribution:\n")
        for conf in sorted(conf_counts.keys(), reverse=True):
            f.write(f"  {conf}%: {conf_counts[conf]} entries\n")

    print(f"Wrote summary to {txt_path}")
    return deduped


def main():
    print("Phase 2b: Enum Resolution")
    print("=" * 50)
    results, enums = scan_for_enum_usage()
    write_results(results, enums)
    print("\nDone!")


if __name__ == "__main__":
    main()
