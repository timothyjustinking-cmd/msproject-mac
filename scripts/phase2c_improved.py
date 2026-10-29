#!/usr/bin/env python3
"""Phase 2c Improved: Extract field_id -> record_offset mappings from switch/case blocks.

Strategy:
1. Find all functions with switch cases on PjField IDs (0x0b4xxxxx, 0x0c4xxxxx, 0x0f4xxxxx)
2. For each case block, extract offset access patterns (param + 0xNN)
3. Cross-reference with field_id_map.tsv for property names
4. Use heuristics to distinguish record field offsets from infrastructure offsets
"""

import os
import re
import csv
from collections import defaultdict, Counter
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
DECOMPILED = BASE / "decompiled"
ANALYSIS = BASE / "analysis"

# Field ID ranges
FIELD_RANGES = {
    'Task':       (0x0b400000, 0x0b4fffff),
    'Resource':   (0x0c400000, 0x0c4fffff),
    'Calendar':   (0x0d400000, 0x0d4fffff),
    'Assignment': (0x0f400000, 0x0f4fffff),
}

def is_field_id(val):
    for name, (lo, hi) in FIELD_RANGES.items():
        if lo <= val <= hi:
            return name
    return None

def load_field_id_map():
    """Load field_id -> property_name from field_id_map.tsv"""
    fmap = {}
    path = ANALYSIS / "field_id_map.tsv"
    with open(path, 'r') as f:
        reader = csv.DictReader(f, delimiter='\t')
        for row in reader:
            fid = int(row['field_id'], 16) if row['field_id'].startswith('0x') else int(row['field_id'])
            prop = row['property_name']
            disp = row['dispatcher']
            if disp == 'get':
                fmap[fid] = prop
            elif fid not in fmap:
                fmap[fid] = prop
    return fmap

def find_function_at_line(lines, target_line):
    """Find the function header for a given line number (0-indexed)."""
    func_re = re.compile(r'^// ==== (FUN_[0-9a-f]+) @ ([0-9a-f]+) ====$')
    for i in range(target_line, max(target_line - 2000, -1), -1):
        m = func_re.match(lines[i])
        if m:
            return m.group(1), m.group(2)
    return None, None

def extract_case_blocks(lines, start_idx):
    """Extract case blocks from a switch statement starting near start_idx.
    Returns list of (case_value, block_lines, line_numbers)."""

    blocks = []
    current_cases = []
    current_lines = []
    current_linenos = []
    in_switch = False
    brace_depth = 0

    # Find the switch statement
    for i in range(max(start_idx - 5, 0), min(start_idx + 5, len(lines))):
        if 'switch(' in lines[i] or 'switch (' in lines[i]:
            in_switch = True
            start_idx = i
            break

    if not in_switch:
        return blocks

    # Parse case blocks
    i = start_idx
    while i < len(lines) and i < start_idx + 5000:
        line = lines[i].strip()

        case_match = re.match(r'case (0x[0-9a-fA-F]+):', line)
        if case_match:
            val = int(case_match.group(1), 16)
            current_cases.append(val)
        elif line.startswith('default:'):
            if current_cases and current_lines:
                blocks.append((list(current_cases), list(current_lines), list(current_linenos)))
            current_cases = []
            current_lines = []
            current_linenos = []
        elif current_cases and line and not line.startswith('case '):
            # This is code within the case block
            current_lines.append(line)
            current_linenos.append(i)

            # Check for block termination
            if 'return ' in line or line == 'break;' or line.startswith('goto '):
                blocks.append((list(current_cases), list(current_lines), list(current_linenos)))
                current_cases = []
                current_lines = []
                current_linenos = []

        # Track braces for the switch body
        brace_depth += line.count('{') - line.count('}')
        if in_switch and brace_depth < 0:
            break

        i += 1

    if current_cases and current_lines:
        blocks.append((current_cases, current_lines, current_linenos))

    return blocks

def extract_offsets_from_code(code_lines):
    """Extract struct offset access patterns from code lines.
    Returns list of (offset_hex, access_type, context)."""

    offsets = []

    for line in code_lines:
        # Pattern 1: *(type *)((longlong)varname + 0xNN)
        # Pattern 2: *(type *)(varname + 0xNN)
        # Pattern 3: varname + 0xNN

        # Find all hex offset patterns in pointer arithmetic
        for m in re.finditer(r'[\+]\s*0x([0-9a-fA-F]+)\)', line):
            off = int(m.group(1), 16)
            if 0x04 <= off <= 0x2000:  # reasonable struct offset range
                # Determine access type
                if '*(ushort' in line or '*(short' in line or '*(undefined2' in line:
                    access_type = 'u16'
                elif '*(uint' in line or '*(int' in line or '*(undefined4' in line:
                    access_type = 'u32'
                elif '*(ulonglong' in line or '*(longlong' in line or '*(undefined8' in line:
                    access_type = 'u64'
                elif '*(byte' in line or '*(char' in line or '*(undefined1' in line or '*(bool' in line:
                    access_type = 'u8'
                elif '*(float' in line:
                    access_type = 'f32'
                elif '*(double' in line:
                    access_type = 'f64'
                else:
                    access_type = 'ptr'

                # Check if it's a vtable call (skip those)
                # vtable calls look like: (**(code **)(* something + 0xNN))
                context_before = line[:m.start()]
                if '*(code **)(' in context_before or '*(code **)(*' in context_before:
                    continue

                offsets.append((off, access_type, line.strip()))

        # Pattern for (byte) cast access: *(byte *)((longlong)var + 0xNN) & mask
        # Already handled above

    return offsets

def classify_offset(off, func_addr, access_count_in_func):
    """Heuristic to classify offset as record-field vs infrastructure.
    Returns confidence score 0-100."""

    # Very small offsets (< 0x10) are likely vtable/refcount infrastructure
    if off < 0x10:
        return 10

    # Common infrastructure offsets for TTaskTable members
    infra_offsets = {0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98}
    if off in infra_offsets:
        return 30

    # Offsets in reasonable record range (0x10 - 0x800) are likely field offsets
    if 0x10 <= off <= 0x800:
        return 80

    # Large offsets (> 0x800) could be either
    if off > 0x800:
        return 50

    return 60

def process_file(filepath):
    """Process one decompiled C file, extracting field_id -> offset mappings."""

    results = []

    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    # Find all case lines with field IDs
    case_re = re.compile(r'^\s*case (0x[0-9a-fA-F]+):')
    func_re = re.compile(r'^// ==== (FUN_[0-9a-f]+) @ ([0-9a-f]+) ====$')

    # Build function boundary index
    func_boundaries = []
    for i, line in enumerate(lines):
        m = func_re.match(line)
        if m:
            func_boundaries.append((i, m.group(1), m.group(2)))

    def get_func_for_line(lineno):
        for j in range(len(func_boundaries) - 1, -1, -1):
            if func_boundaries[j][0] <= lineno:
                return func_boundaries[j][1], func_boundaries[j][2]
        return None, None

    # Find switch statements containing field IDs
    switch_re = re.compile(r'^\s*switch\s*\(')

    # Scan for field ID case values and group by switch
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        case_m = case_re.match(line)
        if case_m:
            val = int(case_m.group(1), 16)
            field_type = is_field_id(val)
            if field_type:
                # Found a field ID case - find the surrounding switch
                # Look backwards for the switch statement
                switch_line = None
                for j in range(i, max(i - 100, -1), -1):
                    if switch_re.match(lines[j]):
                        switch_line = j
                        break

                if switch_line is not None:
                    # Extract all case blocks from this switch
                    blocks = extract_case_blocks([l.rstrip('\n') for l in lines], switch_line)
                    func_name, func_addr = get_func_for_line(i)

                    for case_values, block_lines, block_linenos in blocks:
                        # Filter to only field ID cases
                        field_cases = [(v, is_field_id(v)) for v in case_values if is_field_id(v)]
                        if not field_cases:
                            continue

                        # Extract offsets from the case block
                        offsets = extract_offsets_from_code(block_lines)

                        for field_val, ft in field_cases:
                            for off, atype, ctx in offsets:
                                conf = classify_offset(off, func_addr, len(offsets))
                                results.append({
                                    'field_id': field_val,
                                    'field_type': ft,
                                    'offset': off,
                                    'access_type': atype,
                                    'func_name': func_name or 'unknown',
                                    'func_addr': func_addr or 'unknown',
                                    'file': filepath.name,
                                    'line': block_linenos[0] + 1 if block_linenos else 0,
                                    'confidence': conf,
                                    'context': ctx[:120],
                                })

                    # Skip past this switch to avoid re-processing
                    i = max(i + 1, switch_line + len(blocks) * 5 if blocks else i + 1)
                    continue
        i += 1

    return results

def deduplicate_and_rank(all_results, field_map):
    """Deduplicate results and rank by confidence."""

    # Group by (field_id, offset)
    groups = defaultdict(list)
    for r in all_results:
        key = (r['field_id'], r['offset'])
        groups[key].append(r)

    # For each field_id, collect all offsets with their evidence
    field_offsets = defaultdict(list)
    for (fid, off), entries in groups.items():
        ft = entries[0]['field_type']
        max_conf = max(e['confidence'] for e in entries)
        n_funcs = len(set(e['func_addr'] for e in entries))
        access_types = set(e['access_type'] for e in entries)
        prop_name = field_map.get(fid, '')

        field_offsets[fid].append({
            'offset': off,
            'field_type': ft,
            'max_confidence': max_conf,
            'n_funcs': n_funcs,
            'n_entries': len(entries),
            'access_types': ','.join(sorted(access_types)),
            'property_name': prop_name,
            'sample_context': entries[0]['context'],
            'sample_func': entries[0]['func_name'],
        })

    return field_offsets

def compute_field_consensus(field_offsets):
    """For each field_id, pick the best offset using heuristics."""

    consensus = []

    for fid, offset_entries in sorted(field_offsets.items()):
        if not offset_entries:
            continue

        # Sort by: confidence desc, n_funcs desc, n_entries desc
        sorted_entries = sorted(offset_entries,
                                key=lambda e: (e['max_confidence'], e['n_funcs'], e['n_entries']),
                                reverse=True)

        best = sorted_entries[0]
        alternatives = len(sorted_entries) - 1

        # Compute overall confidence
        if best['max_confidence'] >= 80 and best['n_funcs'] >= 2:
            overall_conf = 'high'
        elif best['max_confidence'] >= 60:
            overall_conf = 'medium'
        else:
            overall_conf = 'low'

        consensus.append({
            'field_id': fid,
            'field_id_hex': f'0x{fid:08x}',
            'field_type': best['field_type'],
            'property_name': best['property_name'],
            'offset': best['offset'],
            'offset_hex': f'0x{best["offset"]:x}',
            'offset_dec': best['offset'],
            'access_types': best['access_types'],
            'n_funcs': best['n_funcs'],
            'n_evidence': best['n_entries'],
            'alternatives': alternatives,
            'confidence': overall_conf,
            'sample_func': best['sample_func'],
        })

    return consensus

def build_struct_layouts(consensus):
    """Group consensus results into per-class struct layouts."""

    # Group by (field_type, offset)
    class_fields = defaultdict(list)
    for entry in consensus:
        key = (entry['field_type'], entry['offset'])
        class_fields[key].append(entry)

    layouts = []
    for (ft, off), entries in sorted(class_fields.items()):
        # All entries share the same offset; list all property names
        prop_names = sorted(set(e['property_name'] for e in entries if e['property_name']))
        access_types = sorted(set(t for e in entries for t in e['access_types'].split(',')))
        max_funcs = max(e['n_funcs'] for e in entries)
        max_evidence = max(e['n_evidence'] for e in entries)
        best_conf = 'high' if any(e['confidence'] == 'high' for e in entries) else \
                    'medium' if any(e['confidence'] == 'medium' for e in entries) else 'low'

        layouts.append({
            'class': ft,
            'offset': off,
            'offset_hex': f'0x{off:x}',
            'n_fields': len(entries),
            'property_names': '; '.join(prop_names) if prop_names else '(unnamed)',
            'access_types': ','.join(access_types),
            'max_funcs': max_funcs,
            'max_evidence': max_evidence,
            'confidence': best_conf,
        })

    return layouts

def main():
    print("Phase 2c Improved: Field ID -> Record Offset Cross-Reference")
    print("=" * 70)

    # Load field ID map
    print("\nLoading field_id_map.tsv...")
    field_map = load_field_id_map()
    print(f"  Loaded {len(field_map)} field ID -> property name mappings")

    # Process all decompiled files
    print("\nScanning decompiled files for field ID switch/case blocks...")
    all_results = []
    decompiled_files = sorted(DECOMPILED.glob("part_*.c"))

    for filepath in decompiled_files:
        results = process_file(filepath)
        if results:
            print(f"  {filepath.name}: {len(results)} raw mappings")
            all_results.extend(results)

    print(f"\nTotal raw mappings: {len(all_results)}")

    # Write raw results
    raw_path = ANALYSIS / "field_offsets_raw.tsv"
    with open(raw_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, delimiter='\t',
                                fieldnames=['field_id', 'field_type', 'offset', 'access_type',
                                            'func_name', 'func_addr', 'file', 'line',
                                            'confidence', 'context'])
        writer.writeheader()
        for r in all_results:
            row = dict(r)
            row['field_id'] = f'0x{r["field_id"]:08x}'
            row['offset'] = f'0x{r["offset"]:x}'
            writer.writerow(row)
    print(f"  Raw results: {raw_path}")

    # Deduplicate and rank
    print("\nDeduplicating and ranking...")
    field_offsets = deduplicate_and_rank(all_results, field_map)
    print(f"  Unique field IDs with offset data: {len(field_offsets)}")

    # Compute consensus
    consensus = compute_field_consensus(field_offsets)

    # Count by type
    type_counts = Counter(e['field_type'] for e in consensus)
    for ft, cnt in sorted(type_counts.items()):
        named = sum(1 for e in consensus if e['field_type'] == ft and e['property_name'])
        print(f"  {ft}: {cnt} field IDs ({named} named)")

    # Write consensus
    consensus_path = ANALYSIS / "field_offsets_consensus.tsv"
    with open(consensus_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, delimiter='\t',
                                fieldnames=['field_type', 'field_id_hex', 'property_name',
                                            'offset_hex', 'offset_dec', 'access_types',
                                            'n_funcs', 'n_evidence', 'alternatives',
                                            'confidence', 'sample_func'])
        writer.writeheader()
        for e in consensus:
            writer.writerow({k: e[k] for k in writer.fieldnames})
    print(f"  Consensus: {consensus_path}")

    # Build struct layouts
    layouts = build_struct_layouts(consensus)
    layout_path = ANALYSIS / "struct_field_layouts.tsv"
    with open(layout_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, delimiter='\t',
                                fieldnames=['class', 'offset_hex', 'offset', 'n_fields',
                                            'property_names', 'access_types',
                                            'max_funcs', 'max_evidence', 'confidence'])
        writer.writeheader()
        for l in layouts:
            writer.writerow(l)
    print(f"  Struct layouts: {layout_path}")

    # Summary
    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)

    conf_dist = Counter(e['confidence'] for e in consensus)
    print(f"\nTotal field IDs with offset mappings: {len(consensus)}")
    print(f"  High confidence: {conf_dist.get('high', 0)}")
    print(f"  Medium confidence: {conf_dist.get('medium', 0)}")
    print(f"  Low confidence: {conf_dist.get('low', 0)}")

    named_count = sum(1 for e in consensus if e['property_name'])
    print(f"\nNamed (with typelib property): {named_count}")
    print(f"Unnamed: {len(consensus) - named_count}")

    print(f"\nStruct layout entries: {len(layouts)}")
    for cls in ['Task', 'Resource', 'Assignment', 'Calendar']:
        cls_layouts = [l for l in layouts if l['class'] == cls]
        if cls_layouts:
            print(f"\n  {cls}: {len(cls_layouts)} distinct offsets")
            for l in cls_layouts[:15]:
                print(f"    {l['offset_hex']:>6s}  {l['property_names'][:60]:<60s}  [{l['confidence']}]")
            if len(cls_layouts) > 15:
                print(f"    ... and {len(cls_layouts) - 15} more")

    # Write summary
    summary_path = ANALYSIS / "phase2c_improved_summary.txt"
    with open(summary_path, 'w') as f:
        f.write("=" * 70 + "\n")
        f.write("Phase 2c Improved: Field ID -> Record Offset Cross-Reference\n")
        f.write("=" * 70 + "\n\n")
        f.write(f"Total raw mappings extracted: {len(all_results)}\n")
        f.write(f"Unique field IDs with offset data: {len(consensus)}\n")
        f.write(f"  High confidence: {conf_dist.get('high', 0)}\n")
        f.write(f"  Medium confidence: {conf_dist.get('medium', 0)}\n")
        f.write(f"  Low confidence: {conf_dist.get('low', 0)}\n")
        f.write(f"  Named: {named_count}\n")
        f.write(f"  Unnamed: {len(consensus) - named_count}\n\n")

        for cls in ['Task', 'Resource', 'Assignment', 'Calendar']:
            cls_entries = [e for e in consensus if e['field_type'] == cls]
            if not cls_entries:
                continue
            f.write(f"\n--- {cls} ({len(cls_entries)} fields) ---\n")
            cls_layouts = [l for l in layouts if l['class'] == cls]
            for l in cls_layouts:
                f.write(f"  {l['offset_hex']:>6s}  {l['property_names'][:70]:<70s}  [{l['confidence']}]\n")

    print(f"\n  Summary: {summary_path}")
    print("\nDone.")

if __name__ == '__main__':
    main()
