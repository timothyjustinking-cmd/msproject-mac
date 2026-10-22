#!/usr/bin/env python3
"""Phase 2d: Analyze engine type codes by cross-referencing with known COM property types.

Uses the field_metadata_all.tsv (0xA8 stride dump) and field_id_map.tsv (COM names)
to determine what each engine type code actually means.
"""

import csv
from pathlib import Path
from collections import Counter, defaultdict

BASE = Path(r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile")
ANALYSIS = BASE / "analysis"


def main():
    # Load field metadata
    entries = []
    with open(ANALYSIS / "field_metadata_all.tsv", 'r') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            entries.append(row)

    print(f"Loaded {len(entries)} field entries")
    print("=" * 80)

    # Group by type_code
    by_type = defaultdict(list)
    for e in entries:
        tc = int(e['type_code'])
        by_type[tc].append(e)

    # For each type code, show representative named fields
    print("\nTYPE CODE ANALYSIS")
    print("=" * 80)

    for tc in sorted(by_type.keys()):
        group = by_type[tc]
        named = [e for e in group if e['property_name']]
        sizes = Counter(int(e['size']) for e in group)
        offsets = Counter(int(e['record_offset']) for e in group)

        print(f"\n--- Type {tc} ({len(group)} fields, {len(named)} named) ---")
        print(f"  Sizes: {dict(sizes.most_common(5))}")
        print(f"  Offsets: {dict(offsets.most_common(10))}")

        # Show up to 15 named examples
        examples = named[:15]
        if examples:
            print(f"  Examples:")
            for e in examples:
                print(f"    {e['property_name']:<35s} off={e['record_offset']:>3s} sz={e['size']:>6s} "
                      f"flags={e['flags_hex']} ({e['record_type']})")

    # =================================================================
    # Cross-reference: fields with same (type, offset) but different names
    # =================================================================
    print(f"\n{'='*80}")
    print("FIELDS SHARING SAME (TYPE, OFFSET) — by type")
    print("=" * 80)

    for tc in sorted(by_type.keys()):
        group = by_type[tc]
        by_offset = defaultdict(list)
        for e in group:
            key = (e['record_type'], int(e['record_offset']))
            by_offset[key].append(e)

        # Find offsets with many fields
        shared = {k: v for k, v in by_offset.items() if len(v) > 1}
        if shared:
            print(f"\n  Type {tc}:")
            for (rt, off), fields in sorted(shared.items()):
                named = [e['property_name'] for e in fields if e['property_name']]
                if named:
                    sample = ', '.join(named[:8])
                    if len(named) > 8:
                        sample += f" (+{len(named)-8} more)"
                    print(f"    {rt} off={off}: {len(fields)} fields: {sample}")

    # =================================================================
    # Flags analysis
    # =================================================================
    print(f"\n{'='*80}")
    print("FLAGS ANALYSIS")
    print("=" * 80)

    flag_counts = Counter(e['flags_hex'] for e in entries)
    print(f"\nMost common flags:")
    for flags, count in flag_counts.most_common(20):
        # Find example fields
        examples = [e for e in entries if e['flags_hex'] == flags and e['property_name']][:3]
        ex_str = ', '.join(e['property_name'] for e in examples) if examples else '(unnamed)'
        print(f"  {flags}: {count:5d} fields  e.g. {ex_str}")

    # Decode flag bits
    print(f"\nFlag bit analysis:")
    all_flags = set()
    for e in entries:
        f = int(e['flags_hex'], 16)
        all_flags.add(f)

    # Check each bit position
    for bit in range(32):
        mask = 1 << bit
        set_count = sum(1 for e in entries if int(e['flags_hex'], 16) & mask)
        if 0 < set_count < len(entries):
            print(f"  Bit {bit:2d} (0x{mask:08x}): set in {set_count:5d}/{len(entries)} fields")

    # =================================================================
    # Tail analysis: the 0x68 bytes between entries
    # =================================================================
    print(f"\n{'='*80}")
    print("RECORD TYPE SUMMARY")
    print("=" * 80)

    for rt in ['Task', 'Resource', 'Assignment', 'Links', 'Calendar', 'Project', 'OutlineCode', 'Task34']:
        group = [e for e in entries if e['record_type'] == rt]
        if not group:
            continue

        named = [e for e in group if e['property_name']]
        types = Counter(int(e['type_code']) for e in group)
        max_off_by_type = defaultdict(int)
        for e in group:
            tc = int(e['type_code'])
            off = int(e['record_offset'])
            max_off_by_type[tc] = max(max_off_by_type[tc], off)

        print(f"\n  {rt}: {len(group)} total, {len(named)} named")
        print(f"    Type distribution: {dict(types.most_common())}")
        print(f"    Max offset by type: {dict(sorted(max_off_by_type.items()))}")


if __name__ == '__main__':
    main()
