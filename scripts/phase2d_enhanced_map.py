#!/usr/bin/env python3
"""Phase 2d: Produce enhanced field map with inferred names for unnamed fields.

Heuristics:
1. Adjacent i16 fields after Duration/Work/Cost fields → units fields
2. Series of consecutive unnamed fields with same type → numbered set variants
3. Known field_id patterns (UniqueID, ProjectName, GUID)
4. Value-based identification from unnamed_probe results
"""

import os
import sys
import re
from collections import defaultdict

BASE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')


def load_definitive_map():
    path = os.path.join(BASE_DIR, 'field_definitive_map.tsv')
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
                    'storage_class': int(parts[9]) if parts[9].isdigit() else -1,
                    'storage_name': parts[10],
                    'class_slot': parts[11],
                    'byte_offset_hex': parts[12],
                    'byte_offset_dec': parts[13],
                    'linked_field_hex': parts[14],
                    'secondary_idx': parts[15],
                    'sub_type': parts[16],
                    'vtable_class': parts[17],
                    'inferred_name': '',
                    'inference_method': '',
                    'raw_parts': parts,
                })
    return fields


def load_unnamed_values():
    path = os.path.join(BASE_DIR, 'unnamed_probe.tsv')
    values = {}
    if os.path.exists(path):
        with open(path, 'r', encoding='utf-8') as f:
            f.readline()
            for line in f:
                parts = line.strip().split('\t')
                if len(parts) >= 6:
                    values[parts[0]] = parts[5]
    return values


def infer_units_fields(fields, id_to_field):
    """i16 fields immediately after Duration/Work fields are units fields."""
    count = 0
    duration_keywords = ['Duration', 'Work', 'Delay', 'Overtime', 'RegularWork',
                         'ActualOvertimeWork', 'RemainingOvertimeWork',
                         'LevelingDelay', 'StartSlack', 'FinishSlack',
                         'FreeSlack', 'TotalSlack']

    for f in fields:
        if f['com_name'] or f['inferred_name']:
            continue
        if f['type_code'] != 2:  # i16
            continue

        fid = f['field_id_dec']
        prev = id_to_field.get(fid - 1)
        if prev and prev['com_name']:
            for kw in duration_keywords:
                if kw.lower() in prev['com_name'].lower():
                    f['inferred_name'] = prev['com_name'] + 'Units'
                    f['inference_method'] = 'adjacent_units'
                    count += 1
                    break
            if not f['inferred_name'] and prev['type_code'] in (5, 20):
                f['inferred_name'] = prev['com_name'] + 'Units'
                f['inference_method'] = 'adjacent_i64_units'
                count += 1
    return count


def infer_series_units(fields, id_to_field):
    """Find series of consecutive unnamed i16 fields and identify what they're units for.

    Scans backward from each series member to find the nearest Duration/Work/i64 field
    with matching numbering to determine the actual base name.
    """
    count = 0
    task_fields = [f for f in fields if f['record_type'] == 'Task']
    task_fields_sorted = sorted(task_fields, key=lambda x: x['field_id_dec'])

    duration_work_types = {'Duration', 'Work', 'Delay', 'Overtime', 'Cost'}

    i = 0
    while i < len(task_fields_sorted):
        f = task_fields_sorted[i]
        if not f['com_name'] and not f['inferred_name'] and f['type_code'] == 2:
            series = [f]
            j = i + 1
            while j < len(task_fields_sorted):
                nxt = task_fields_sorted[j]
                if (not nxt['com_name'] and not nxt['inferred_name'] and
                    nxt['type_code'] == 2 and
                    nxt['field_id_dec'] - series[-1]['field_id_dec'] <= 3 and
                    nxt['storage_name'] == f['storage_name']):
                    series.append(nxt)
                    j += 1
                else:
                    break

            if len(series) >= 5:
                first_id = series[0]['field_id_dec']
                base_name = None
                for delta in range(1, 20):
                    prev = id_to_field.get(first_id - delta)
                    if prev and prev['com_name']:
                        m = re.match(r'^(.*?)1$', prev['com_name'])
                        if m:
                            candidate = m.group(1)
                            if any(kw in candidate for kw in duration_work_types) or prev['type_code'] in (5, 20):
                                base_name = candidate
                                break

                if not base_name:
                    hex_range = f"{series[0]['field_id_hex']}-{series[-1]['field_id_hex']}"
                    base_name = f'UnkSeries_{hex_range}'

                for idx, s in enumerate(series):
                    s['inferred_name'] = f'{base_name}{idx+1}Units'
                    s['inference_method'] = 'series_units'
                    count += 1

                i = j
                continue
        i += 1
    return count


def infer_by_known_ids(fields, unnamed_values):
    """Identify specific well-known unnamed fields by their field_id and value."""
    known = {
        0x0b400056: ('UniqueID_Internal', 'known_id'),
        0x0b400183: ('InternalStartDate', 'known_id'),
        0x0b4001C9: ('ProjectName_Ref', 'known_id'),
        0x0b40047F: ('SecondaryGUID', 'known_id'),
        0x0b400479: ('VersionCounter', 'known_id'),
        0x0b4000F1: ('SentinelNone', 'known_id'),
    }
    count = 0
    for f in fields:
        if f['com_name'] or f['inferred_name']:
            continue
        fid = f['field_id_dec']
        if fid in known:
            f['inferred_name'], f['inference_method'] = known[fid]
            count += 1
    return count


def infer_linked_pair(fields, id_to_field):
    """If an unnamed field's linked_field points to a named field, infer relationship."""
    count = 0
    for f in fields:
        if f['com_name'] or f['inferred_name']:
            continue
        if f['linked_field_hex']:
            try:
                linked_id = int(f['linked_field_hex'], 16)
                linked = id_to_field.get(linked_id)
                if linked and linked['com_name']:
                    if f['type_code'] == 2:
                        f['inferred_name'] = linked['com_name'] + 'Units'
                        f['inference_method'] = 'linked_pair_units'
                    else:
                        f['inferred_name'] = linked['com_name'] + '_Linked'
                        f['inference_method'] = 'linked_pair'
                    count += 1
            except ValueError:
                pass
    return count


def find_nearest_named(fid, id_to_field, max_dist=5):
    """Find nearest COM-named field within max_dist, return (name, distance, direction)."""
    for d in range(1, max_dist + 1):
        prev = id_to_field.get(fid - d)
        if prev and prev['com_name']:
            return (prev['com_name'], d, 'after')
        nxt = id_to_field.get(fid + d)
        if nxt and nxt['com_name']:
            return (nxt['com_name'], d, 'before')
    return None


def infer_adjacent_context(fields, id_to_field):
    """For remaining unnamed fields, name from nearest COM-named neighbor (no cascading)."""
    count = 0
    for f in fields:
        if f['com_name'] or f['inferred_name']:
            continue
        if f['record_type'] != 'Task':
            continue

        result = find_nearest_named(f['field_id_dec'], id_to_field, max_dist=3)
        if result:
            name, dist, direction = result
            f['inferred_name'] = f'{name}+{dist}' if direction == 'after' else f'{name}-{dist}'
            f['inference_method'] = f'near_{direction}_{dist}'
            count += 1
    return count


def infer_virtual_computed(fields, id_to_field):
    """Virtual (type 29) and null (type 0) fields — find nearest COM-named neighbor."""
    count = 0
    for f in fields:
        if f['com_name'] or f['inferred_name']:
            continue
        if f['type_code'] not in (0, 29):
            continue
        result = find_nearest_named(f['field_id_dec'], id_to_field, max_dist=5)
        if result:
            name, dist, direction = result
            kind = 'virt' if f['type_code'] == 29 else 'ref'
            sign = '+' if direction == 'after' else '-'
            f['inferred_name'] = f'{name}{sign}{dist}_{kind}'
            f['inference_method'] = f'{kind}_near_{dist}'
            count += 1
    return count


def main():
    fields = load_definitive_map()
    unnamed_values = load_unnamed_values()

    id_to_field = {}
    for f in fields:
        id_to_field[f['field_id_dec']] = f

    task_fields = [f for f in fields if f['record_type'] == 'Task']
    unnamed_task = [f for f in task_fields if not f['com_name']]
    print(f"Total fields: {len(fields)}")
    print(f"Task fields: {len(task_fields)}")
    print(f"Unnamed Task fields: {len(unnamed_task)}")

    c1 = infer_units_fields(fields, id_to_field)
    print(f"\n  Adjacent units fields: {c1}")

    c2 = infer_series_units(fields, id_to_field)
    print(f"  Series units fields: {c2}")

    c3 = infer_by_known_ids(fields, unnamed_values)
    print(f"  Known ID fields: {c3}")

    c4 = infer_linked_pair(fields, id_to_field)
    print(f"  Linked pair fields: {c4}")

    c5 = infer_adjacent_context(fields, id_to_field)
    print(f"  Adjacent context fields: {c5}")

    c6 = infer_virtual_computed(fields, id_to_field)
    print(f"  Virtual/computed fields: {c6}")

    total_inferred = sum(1 for f in fields if f['inferred_name'])
    still_unnamed = sum(1 for f in fields if not f['com_name'] and not f['inferred_name'])
    print(f"\n  TOTAL inferred: {total_inferred}")
    print(f"  Still unnamed: {still_unnamed}")

    # Breakdown by inference method
    methods = defaultdict(int)
    for f in fields:
        if f['inferred_name']:
            methods[f['inference_method']] += 1
    print(f"\n  By method:")
    for m, c in sorted(methods.items(), key=lambda x: -x[1]):
        print(f"    {m:<25s}: {c}")

    # Write enhanced map
    out_path = os.path.join(BASE_DIR, 'field_enhanced_map.tsv')
    with open(out_path, 'w', encoding='utf-8') as fout:
        header = ('record_type\tfield_id_hex\tfield_id_dec\tcom_property_name\t'
                  'inferred_name\tinference_method\t'
                  'engine_type_code\tengine_type_name\tslot_index\tfield_size\t'
                  'flags_hex\tstorage_class\tstorage_name\tclass_slot\t'
                  'byte_offset_hex\tbyte_offset_dec\tlinked_field_hex\t'
                  'secondary_idx\tsub_type\tvtable_class\n')
        fout.write(header)

        for f in fields:
            display_name = f['com_name'] or f['inferred_name'] or ''
            fout.write(f"{f['record_type']}\t{f['field_id_hex']}\t{f['field_id_dec']}\t"
                       f"{f['com_name']}\t{f['inferred_name']}\t{f['inference_method']}\t"
                       f"{f['type_code']}\t{f['type_name']}\t{f['slot_index']}\t{f['field_size']}\t"
                       f"{f['flags_hex']}\t{f['storage_class']}\t{f['storage_name']}\t"
                       f"{f['class_slot']}\t{f['byte_offset_hex']}\t{f['byte_offset_dec']}\t"
                       f"{f['linked_field_hex']}\t{f['secondary_idx']}\t{f['sub_type']}\t"
                       f"{f['vtable_class']}\n")

    print(f"\nSaved enhanced map: {out_path}")

    # Also produce a summary of just the unnamed fields with inferences
    summary_path = os.path.join(BASE_DIR, 'unnamed_inferred.tsv')
    with open(summary_path, 'w', encoding='utf-8') as fout:
        fout.write('field_id_hex\tfield_id_dec\tinferred_name\tmethod\ttype\tstorage\tprobe_value\n')
        for f in fields:
            if f['inferred_name']:
                val = unnamed_values.get(f['field_id_hex'], '')
                val_safe = val.encode('ascii', errors='replace').decode('ascii')[:60] if val else ''
                fout.write(f"{f['field_id_hex']}\t{f['field_id_dec']}\t{f['inferred_name']}\t"
                           f"{f['inference_method']}\t{f['type_name']}\t{f['storage_name']}\t"
                           f"{val_safe}\n")

    print(f"Saved inferred summary: {summary_path}")

    # Show sample inferred names
    print(f"\n{'='*100}")
    print(f"SAMPLE INFERRED NAMES (first 40)")
    print(f"{'='*100}")
    inferred = [f for f in fields if f['inferred_name']]
    for f in inferred[:40]:
        val = unnamed_values.get(f['field_id_hex'], '')
        val_safe = val[:30].encode('ascii', errors='replace').decode('ascii') if val else ''
        print(f"  {f['field_id_hex']:<12s} {f['inferred_name']:<35s} [{f['inference_method']:<20s}] "
              f"type={f['type_name']:<6s} val={val_safe}")


if __name__ == '__main__':
    main()
