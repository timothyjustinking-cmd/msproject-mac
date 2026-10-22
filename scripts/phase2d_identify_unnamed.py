#!/usr/bin/env python3
"""Phase 2d: Identify unnamed fields by cross-referencing with known PjField constants.

Uses the Microsoft PjField enumeration to match our field_ids against known constants.
Many unnamed fields likely correspond to internal versions of named fields or
fields that are not exposed through the COM task properties but are through GetField.
"""

import os
import sys

# Known PjField enum ranges from Microsoft documentation
# These are the ranges that should be valid for Task fields
# Format: (start_dec, end_dec, prefix_description)
PJFIELD_TASK_RANGES = {
    # Standard task fields: 0x0B400000 - 0x0B4005FF
    'TaskWork': (188743680, 'pjTaskWork'),
    'TaskBaselineWork': (188743681, 'pjTaskBaselineWork'),
    'TaskActualWork': (188743682, 'pjTaskActualWork'),
}

# Build a reverse map from field_id_dec to known PjField constant names
# Source: Microsoft documentation PjField Enumeration
KNOWN_FIELDS = {}

# Read from our field_id_map which has COM names
def load_field_id_map():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_id_map.tsv')

    id_to_name = {}
    if os.path.exists(path):
        with open(path, 'r', encoding='utf-8') as f:
            for line in f:
                parts = line.strip().split('\t')
                if len(parts) >= 9:
                    try:
                        field_id_hex = parts[4].strip()
                        name = parts[8].strip() if len(parts) > 8 else ''
                        dispatcher = parts[7].strip() if len(parts) > 7 else ''
                        if field_id_hex and name and dispatcher == 'get':
                            field_id_dec = int(field_id_hex, 16)
                            id_to_name[field_id_dec] = name
                    except (ValueError, IndexError):
                        pass
    return id_to_name


def load_definitive_map():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_definitive_map.tsv')

    fields = []
    with open(path, 'r', encoding='utf-8') as f:
        header = f.readline().strip().split('\t')
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18 and parts[0] == 'Task':
                fields.append({
                    'field_id_hex': parts[1],
                    'field_id_dec': int(parts[2]),
                    'com_name': parts[3],
                    'type_code': int(parts[4]),
                    'type_name': parts[5],
                    'slot_index': int(parts[6]),
                    'storage_class': int(parts[9]) if parts[9].isdigit() else -1,
                    'storage_name': parts[10],
                })
    return fields


def load_unnamed_values():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'unnamed_probe.tsv')

    values = {}
    if os.path.exists(path):
        with open(path, 'r', encoding='utf-8') as f:
            f.readline()  # skip header
            for line in f:
                parts = line.strip().split('\t')
                if len(parts) >= 6:
                    field_id = parts[0]
                    val = parts[5]
                    values[field_id] = val
    return values


def main():
    fields = load_definitive_map()
    id_map = load_field_id_map()
    unnamed_values = load_unnamed_values()

    print(f"Loaded {len(fields)} Task fields")
    print(f"Loaded {len(id_map)} field ID -> COM name mappings")
    print(f"Loaded {len(unnamed_values)} unnamed field values")

    unnamed = [f for f in fields if not f['com_name']]

    # Check if any unnamed fields have names in the id_map that we missed
    print(f"\n{'='*90}")
    print(f"UNNAMED FIELDS WITH POTENTIAL COM NAMES (from field_id_map)")
    print(f"{'='*90}")

    newly_named = 0
    for f in unnamed:
        fid = f['field_id_dec']
        if fid in id_map:
            name = id_map[fid]
            val = unnamed_values.get(f['field_id_hex'], '')
            val_safe = val.encode('ascii', errors='replace').decode('ascii')[:40]
            print(f"  {f['field_id_hex']:<12s} -> {name:<35s} type={f['type_name']:<8s} store={f['storage_name']:<12s} val={val_safe}")
            newly_named += 1

    print(f"\n  Found {newly_named} previously unnamed fields with COM names")

    # Try to identify fields by their position patterns
    # Many unnamed fields follow regular patterns (e.g., Baseline1-10 variants)
    print(f"\n{'='*90}")
    print(f"PATTERN-BASED IDENTIFICATION")
    print(f"{'='*90}")

    # Group named fields by their base name (strip trailing digits)
    import re
    named_patterns = {}
    for f in fields:
        if f['com_name']:
            m = re.match(r'^(.*?)(\d+)$', f['com_name'])
            if m:
                base = m.group(1)
                num = int(m.group(2))
                if base not in named_patterns:
                    named_patterns[base] = {}
                named_patterns[base][num] = f['field_id_dec']

    # Find patterns where we have gaps (numbered field exists for some numbers but not all)
    print(f"\n  Numbered field patterns with gaps:")
    for base in sorted(named_patterns.keys()):
        nums = sorted(named_patterns[base].keys())
        if len(nums) >= 3 and max(nums) - min(nums) + 1 > len(nums):
            missing = [n for n in range(min(nums), max(nums)+1) if n not in nums]
            if missing:
                print(f"    {base}[{min(nums)}-{max(nums)}]: present={nums}, missing={missing}")

    # Group unnamed fields by storage class and slot to find series
    print(f"\n  Unnamed field series (same storage/type, consecutive IDs):")

    unnamed_sorted = sorted(unnamed, key=lambda f: f['field_id_dec'])

    current_series = []
    for f in unnamed_sorted:
        if current_series:
            last = current_series[-1]
            if (f['type_name'] == last['type_name'] and
                f['storage_name'] == last['storage_name'] and
                f['field_id_dec'] - last['field_id_dec'] <= 5):
                current_series.append(f)
            else:
                if len(current_series) >= 5:
                    first = current_series[0]
                    last = current_series[-1]
                    print(f"    {first['field_id_hex']}-{last['field_id_hex']}: {len(current_series)} fields, type={first['type_name']}, store={first['storage_name']}")
                current_series = [f]
        else:
            current_series = [f]

    if len(current_series) >= 5:
        first = current_series[0]
        last = current_series[-1]
        print(f"    {first['field_id_hex']}-{last['field_id_hex']}: {len(current_series)} fields, type={first['type_name']}, store={first['storage_name']}")

    # Identify linked field pairs
    print(f"\n{'='*90}")
    print(f"FIELD ID ADJACENCY ANALYSIS")
    print(f"{'='*90}")

    # Build field_id -> field map
    id_to_field = {f['field_id_dec']: f for f in fields}

    # Find unnamed fields adjacent to named fields
    print(f"\n  Unnamed fields adjacent to named fields:")
    for f in unnamed:
        fid = f['field_id_dec']
        # Check neighbors
        for delta in [-1, 1]:
            neighbor_id = fid + delta
            if neighbor_id in id_to_field:
                nb = id_to_field[neighbor_id]
                if nb['com_name']:
                    direction = "after" if delta == 1 else "before"
                    val = unnamed_values.get(f['field_id_hex'], '')
                    val_safe = val.encode('ascii', errors='replace').decode('ascii')[:30]
                    print(f"    {f['field_id_hex']} ({f['type_name']:>7s}/{f['storage_name']:<10s}) is {direction:5s} {nb['com_name']:<30s} val={val_safe}")
                    break

    # Summary statistics
    print(f"\n{'='*90}")
    print(f"SUMMARY")
    print(f"{'='*90}")

    total_identifiable = newly_named
    print(f"  Total unnamed Task fields: {len(unnamed)}")
    print(f"  Identifiable from COM name map: {newly_named}")
    print(f"  With non-zero values: {sum(1 for f in unnamed if unnamed_values.get(f['field_id_hex'], '') not in ('', '0', 'False', 'No', 'NA', '0 hrs', '0 days', '$0.00', '0%', '0:00'))}")
    print(f"  Virtual (type 29, no data): {sum(1 for f in unnamed if f['type_code'] == 29)}")
    print(f"  Null (type 0, no data): {sum(1 for f in unnamed if f['type_code'] == 0)}")


if __name__ == '__main__':
    main()
