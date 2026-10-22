#!/usr/bin/env python3
"""Phase 2d: Merge all data sources into the definitive field mapping.

Sources:
1. field_metadata_all.tsv — 3207 base field entries (all 8 record types)
2. field_id_map.tsv — COM dispatch names (1856 get dispatchers)
3. field_master_decoded.tsv — 1444 Task fields with decoded tail data
4. Live process — decode tail for Resource/Assignment if cache accessible

Output: analysis/field_definitive_map.tsv — the complete mapping
"""

import os
import sys


def load_base_fields(path):
    """Load field_metadata_all.tsv"""
    fields = []
    with open(path, 'r', encoding='utf-8') as f:
        header = f.readline().strip().split('\t')
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 11:
                rec = {
                    'record_type': parts[0],
                    'cache_idx': int(parts[1]),
                    'field_id_hex': parts[2],
                    'field_id_dec': int(parts[3]),
                    'base_property_name': parts[4],
                    'vtable_class': parts[5],
                    'type_code': int(parts[6]),
                    'base_type_name': parts[7],
                    'slot_index': int(parts[8]),
                    'field_size': int(parts[9]),
                    'flags_hex': parts[10],
                }
                fields.append(rec)
    return fields


def load_com_names(path):
    """Load field_id_map.tsv — COM dispatch property names"""
    names = {}
    with open(path, 'r', encoding='utf-8') as f:
        header = f.readline()
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 9:
                fid_str = parts[4]
                name = parts[8]
                dispatcher = parts[7] if len(parts) > 7 else ''
                interface = parts[0] if len(parts) > 0 else ''
                try:
                    fid = int(fid_str, 16) if fid_str.startswith('0x') else int(fid_str)
                    if name and dispatcher == 'get':
                        if fid not in names:
                            names[fid] = name
                except ValueError:
                    pass
    return names


def load_decoded_task(path):
    """Load field_master_decoded.tsv — decoded tail data for Task fields"""
    decoded = {}
    with open(path, 'r', encoding='utf-8') as f:
        header = f.readline().strip().split('\t')
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 20:
                fid_hex = parts[2]  # field_id_hex
                try:
                    fid = int(fid_hex, 16)
                except ValueError:
                    continue
                decoded[fid] = {
                    'storage_class': parts[9] if parts[9] else '',
                    'storage_name': parts[10] if parts[10] else '',
                    'class_slot': parts[11] if parts[11] else '',
                    'dirty_bit_pos': parts[12] if parts[12] else '',
                    'decoded_field_size': parts[13] if parts[13] else '',
                    'sub_type': parts[14] if parts[14] else '',
                    'secondary_idx': parts[15] if parts[15] else '',
                    'linked_field_hex': parts[16] if parts[16] else '',
                    'byte_offset_hex': parts[18] if len(parts) > 18 and parts[18] else '',
                    'byte_offset_dec': parts[19] if len(parts) > 19 and parts[19] else '',
                }
    return decoded


TYPE_CODE_TO_ENGINE = {
    0: 'null', 2: 'i16', 3: 'i32', 5: 'i64', 8: 'text', 9: 'obj_ref',
    11: 'bool', 19: 'date32', 20: 'comp_dur', 21: 'order_id', 29: 'virtual', 72: 'guid',
    100: 'bool_ex', 101: 'i64_ex'
}


def main():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')

    # Load all data sources
    base_path = os.path.join(base_dir, 'field_metadata_all.tsv')
    com_path = os.path.join(base_dir, 'field_id_map.tsv')
    decoded_path = os.path.join(base_dir, 'field_master_decoded.tsv')

    print("Loading data sources...")

    base_fields = load_base_fields(base_path)
    print(f"  Base fields: {len(base_fields)}")

    com_names = load_com_names(com_path)
    print(f"  COM names: {len(com_names)}")

    decoded_task = {}
    if os.path.exists(decoded_path):
        decoded_task = load_decoded_task(decoded_path)
        print(f"  Decoded Task fields: {len(decoded_task)}")

    # Merge and produce output
    out_path = os.path.join(base_dir, 'field_definitive_map.tsv')

    # Statistics
    stats = {
        'total': 0,
        'named': 0,
        'with_offset': 0,
        'direct_storage': 0,
        'computed': 0,
        'baseline': 0,
    }

    by_record = {}

    with open(out_path, 'w', encoding='utf-8') as fout:
        header = '\t'.join([
            'record_type', 'field_id_hex', 'field_id_dec',
            'com_property_name', 'engine_type_code', 'engine_type_name',
            'slot_index', 'field_size', 'flags_hex',
            'storage_class', 'storage_name', 'class_slot',
            'byte_offset_hex', 'byte_offset_dec',
            'linked_field_hex', 'secondary_idx', 'sub_type',
            'vtable_class'
        ])
        fout.write(header + '\n')

        for rec in base_fields:
            fid = rec['field_id_dec']
            fid_hex = rec['field_id_hex']
            rec_type = rec['record_type']

            # COM name: prefer from dispatch table, fall back to base metadata
            com_name = com_names.get(fid, '')
            if not com_name:
                com_name = rec['base_property_name']

            # Engine type name from type code
            engine_type = TYPE_CODE_TO_ENGINE.get(rec['type_code'], f"unk_{rec['type_code']}")

            # Decoded tail data (if available)
            d = decoded_task.get(fid, {})
            storage_class = d.get('storage_class', '')
            storage_name = d.get('storage_name', '')
            class_slot = d.get('class_slot', '')
            byte_offset_hex = d.get('byte_offset_hex', '')
            byte_offset_dec = d.get('byte_offset_dec', '')
            linked_field = d.get('linked_field_hex', '')
            secondary_idx = d.get('secondary_idx', '')
            sub_type = d.get('sub_type', '')

            row = '\t'.join([
                rec_type, fid_hex, str(fid),
                com_name, str(rec['type_code']), engine_type,
                str(rec['slot_index']), str(rec['field_size']), rec['flags_hex'],
                storage_class, storage_name, class_slot,
                byte_offset_hex, byte_offset_dec,
                linked_field, secondary_idx, sub_type,
                rec['vtable_class']
            ])
            fout.write(row + '\n')

            stats['total'] += 1
            if com_name:
                stats['named'] += 1
            if byte_offset_hex:
                stats['with_offset'] += 1
            if storage_name == 'direct':
                stats['direct_storage'] += 1
            elif storage_name == 'computed':
                stats['computed'] += 1
            elif storage_name == 'baseline':
                stats['baseline'] += 1

            by_record.setdefault(rec_type, {'total': 0, 'named': 0, 'with_offset': 0})
            by_record[rec_type]['total'] += 1
            if com_name:
                by_record[rec_type]['named'] += 1
            if byte_offset_hex:
                by_record[rec_type]['with_offset'] += 1

    print(f"\nOutput: {out_path}")

    print(f"\n{'='*60}")
    print(f"DEFINITIVE MAPPING STATISTICS")
    print(f"{'='*60}")
    print(f"\n  Total fields:           {stats['total']:>5d}")
    print(f"  With COM name:          {stats['named']:>5d} ({100*stats['named']//stats['total']}%)")
    print(f"  With byte offset:       {stats['with_offset']:>5d}")
    print(f"  Direct storage (Task):  {stats['direct_storage']:>5d}")
    print(f"  Computed (Task):        {stats['computed']:>5d}")
    print(f"  Baseline variants:      {stats['baseline']:>5d}")

    print(f"\n  {'Record':<14s} {'Total':>6s} {'Named':>6s} {'Offset':>6s}")
    print(f"  {'-'*14} {'-'*6} {'-'*6} {'-'*6}")
    for rn in ['Task', 'Resource', 'Assignment', 'Links', 'Calendar', 'Project', 'OutlineCode', 'Task34']:
        r = by_record.get(rn, {'total': 0, 'named': 0, 'with_offset': 0})
        print(f"  {rn:<14s} {r['total']:>6d} {r['named']:>6d} {r['with_offset']:>6d}")

    # Sample output: first 20 Task fields with names
    print(f"\n{'='*60}")
    print(f"SAMPLE: First 20 named Task fields")
    print(f"{'='*60}")
    print(f"  {'field_id':<12s} {'name':<30s} {'type':<8s} {'slot':>4s} {'store':<10s} {'offset':<8s}")

    count = 0
    for rec in base_fields:
        if rec['record_type'] != 'Task':
            continue
        fid = rec['field_id_dec']
        com_name = com_names.get(fid, rec['base_property_name'])
        if not com_name:
            continue

        d = decoded_task.get(fid, {})
        engine_type = TYPE_CODE_TO_ENGINE.get(rec['type_code'], '?')
        storage = d.get('storage_name', '?')
        offset = d.get('byte_offset_hex', '')

        print(f"  {rec['field_id_hex']:<12s} {com_name:<30s} {engine_type:<8s} {rec['slot_index']:>4d} {storage:<10s} {offset:<8s}")
        count += 1
        if count >= 20:
            break

    print(f"\nDone!")


if __name__ == '__main__':
    main()
