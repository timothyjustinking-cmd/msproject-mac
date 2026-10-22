#!/usr/bin/env python3
"""Phase 2d: Investigate storage classes to understand field storage mechanisms.

Goal: Decode what storage classes 0-15 mean, especially class 3 (671 fields)
and class 13 (207 fields) which together cover ~60% of Task fields.

Approach:
1. Load decoded Task data and group by storage_class
2. For each class, show COM name patterns, type distributions, slot ranges
3. Look for patterns in getter2 functions per class
4. Examine linked_field relationships
"""

import os
import sys
from collections import Counter, defaultdict


def load_definitive(path):
    fields = []
    with open(path, 'r', encoding='utf-8') as f:
        header = f.readline().strip().split('\t')
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18:
                fields.append({
                    'record_type': parts[0],
                    'field_id_hex': parts[1],
                    'field_id_dec': int(parts[2]),
                    'com_name': parts[3],
                    'type_code': int(parts[4]),
                    'type_name': parts[5],
                    'slot_index': int(parts[6]),
                    'field_size': int(parts[7]),
                    'flags_hex': parts[8],
                    'storage_class': int(parts[9]) if parts[9] else -1,
                    'storage_name': parts[10],
                    'class_slot': int(parts[11]) if parts[11] else -1,
                    'byte_offset_hex': parts[12],
                    'byte_offset_dec': parts[13],
                    'linked_field_hex': parts[14],
                    'secondary_idx': parts[15],
                    'sub_type': parts[16],
                    'vtable_class': parts[17],
                })
    return fields


def analyze_storage_classes(fields):
    task_fields = [f for f in fields if f['record_type'] == 'Task' and f['storage_class'] >= 0]

    by_class = defaultdict(list)
    for f in task_fields:
        by_class[f['storage_class']].append(f)

    print(f"{'='*80}")
    print(f"STORAGE CLASS ANALYSIS — {len(task_fields)} Task fields")
    print(f"{'='*80}")

    for sc in sorted(by_class.keys()):
        flds = by_class[sc]
        named = [f for f in flds if f['com_name']]

        # Type distribution
        type_dist = Counter(f['type_name'] for f in flds)

        # Slot ranges
        slots = [f['slot_index'] for f in flds]
        min_slot, max_slot = min(slots), max(slots)

        # Class slots
        class_slots = [f['class_slot'] for f in flds if f['class_slot'] >= 0]

        # Linked fields
        linked = [f for f in flds if f['linked_field_hex'] and f['linked_field_hex'] != '0xffffffff']

        # Fields with byte offsets
        with_offset = [f for f in flds if f['byte_offset_hex']]

        # Flags distribution
        flags_dist = Counter(f['flags_hex'] for f in flds)

        name = flds[0].get('storage_name', f'class_{sc}')

        print(f"\n{'-'*80}")
        print(f"Storage Class {sc} ({name}): {len(flds)} fields, {len(named)} named, {len(with_offset)} with offset")
        print(f"  Slot range: {min_slot}-{max_slot}")
        if class_slots:
            print(f"  Class slot range: {min(class_slots)}-{max(class_slots)} (count: {len(class_slots)})")

        print(f"  Type distribution:")
        for tn, cnt in type_dist.most_common(10):
            print(f"    {tn:<12s}: {cnt:>4d}")

        print(f"  Linked fields: {len(linked)}")

        if len(flags_dist) <= 5:
            print(f"  Flags: {dict(flags_dist)}")
        else:
            print(f"  Flags: {len(flags_dist)} unique values")

        # Show sample named fields
        samples = [f for f in flds if f['com_name']][:15]
        if samples:
            print(f"  Sample fields:")
            for f in samples:
                offset_str = f['byte_offset_hex'] if f['byte_offset_hex'] else ''
                linked_str = f['linked_field_hex'] if f['linked_field_hex'] and f['linked_field_hex'] != '0xffffffff' else ''
                cs = f['class_slot'] if f['class_slot'] >= 0 else ''
                print(f"    {f['field_id_hex']:<12s} {f['com_name']:<35s} {f['type_name']:<10s} cs={str(cs):<4s} {offset_str:<8s} {linked_str}")

        # For class 3 and 13, show unnamed field ID patterns
        if sc in (3, 13) and len(flds) > len(named):
            unnamed = [f for f in flds if not f['com_name']]
            print(f"  Unnamed field count: {len(unnamed)}")
            fid_prefixes = Counter(f['field_id_hex'][:6] for f in unnamed)
            print(f"  Unnamed field_id prefix distribution:")
            for prefix, cnt in fid_prefixes.most_common(10):
                print(f"    {prefix}xxxx: {cnt}")


def analyze_linked_fields(fields):
    """Trace linked_field relationships to understand field groupings."""
    task_fields = [f for f in fields if f['record_type'] == 'Task' and f['storage_class'] >= 0]

    fid_to_field = {f['field_id_hex']: f for f in task_fields}

    print(f"\n{'='*80}")
    print(f"LINKED FIELD CHAINS")
    print(f"{'='*80}")

    linked_pairs = []
    for f in task_fields:
        lf = f['linked_field_hex']
        if lf and lf != '0xffffffff' and lf != '0x00000000':
            target = fid_to_field.get(lf)
            target_name = target['com_name'] if target else '?'
            linked_pairs.append((f, lf, target_name))

    print(f"\nTotal linked fields: {len(linked_pairs)}")

    # Show first 30 linked pairs
    print(f"\nSample linked pairs (field -> linked_to):")
    for f, lf, tn in linked_pairs[:30]:
        src_name = f['com_name'] if f['com_name'] else f['field_id_hex']
        print(f"  {src_name:<35s} ({f['type_name']:<8s} sc={f['storage_class']}) -> {lf} ({tn})")

    # Analyze link patterns by storage class
    link_by_sc = defaultdict(list)
    for f, lf, tn in linked_pairs:
        link_by_sc[f['storage_class']].append((f, lf, tn))

    print(f"\nLinks by storage class:")
    for sc in sorted(link_by_sc.keys()):
        pairs = link_by_sc[sc]
        target_classes = Counter()
        for f, lf, tn in pairs:
            target = fid_to_field.get(lf)
            if target:
                target_classes[target['storage_class']] += 1
        print(f"  Class {sc}: {len(pairs)} links -> target classes: {dict(target_classes)}")


def analyze_class3_patterns(fields):
    """Deep dive into class 3 — the biggest unknown."""
    task_fields = [f for f in fields if f['record_type'] == 'Task' and f['storage_class'] == 3]

    print(f"\n{'='*80}")
    print(f"CLASS 3 DEEP DIVE — {len(task_fields)} fields")
    print(f"{'='*80}")

    # Group by type
    by_type = defaultdict(list)
    for f in task_fields:
        by_type[f['type_name']].append(f)

    for tn in sorted(by_type.keys()):
        flds = by_type[tn]
        named = [f for f in flds if f['com_name']]
        print(f"\n  Type {tn}: {len(flds)} fields ({len(named)} named)")

        # Show all named ones
        for f in named[:20]:
            cs = f['class_slot'] if f['class_slot'] >= 0 else ''
            print(f"    {f['field_id_hex']:<12s} {f['com_name']:<40s} cs={str(cs):<4s} flags={f['flags_hex']}")

    # Look for Baseline pattern in names
    baseline_names = [f for f in task_fields if f['com_name'] and 'Baseline' in f['com_name']]
    print(f"\n  Fields with 'Baseline' in name: {len(baseline_names)}")
    if baseline_names:
        for f in baseline_names[:10]:
            print(f"    {f['com_name']}")

    # Look for number suffixes (Baseline1, Baseline2, etc.)
    import re
    numbered = [f for f in task_fields if f['com_name'] and re.search(r'\d+$', f['com_name'])]
    print(f"  Fields ending in numbers: {len(numbered)}")
    if numbered:
        # Group by base name
        base_names = defaultdict(list)
        for f in numbered:
            m = re.match(r'(.+?)(\d+)$', f['com_name'])
            if m:
                base_names[m.group(1)].append(int(m.group(2)))
        for bn, nums in sorted(base_names.items(), key=lambda x: -len(x[1]))[:15]:
            print(f"    {bn}: {sorted(nums)}")


def main():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_definitive_map.tsv')

    print("Loading definitive map...")
    fields = load_definitive(path)
    print(f"  Loaded {len(fields)} fields")

    analyze_storage_classes(fields)
    analyze_linked_fields(fields)
    analyze_class3_patterns(fields)


if __name__ == '__main__':
    main()
