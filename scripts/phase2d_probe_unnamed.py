#!/usr/bin/env python3
"""Phase 2d: Probe unnamed fields via COM to discover their purpose.

For each unnamed Task field, tries to read it via COM GetField and checks
if the value gives any clue about the field's purpose. Also checks if
unnamed fields map to known PjField enum constants.
"""

import os
import sys

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 not installed"); sys.exit(1)


def load_field_map():
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
                    'field_size': int(parts[7]),
                    'storage_class': int(parts[9]) if parts[9].isdigit() else -1,
                    'storage_name': parts[10],
                    'byte_offset_hex': parts[12],
                })
    return fields


def main():
    print("Connecting to MS Project...")
    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    if not proj:
        print("ERROR: No project open"); sys.exit(1)

    tasks = proj.Tasks
    task = None
    for i in range(1, tasks.Count + 1):
        t = tasks.Item(i)
        if t is not None:
            task = t
            break

    if not task:
        print("ERROR: No tasks"); sys.exit(1)

    print(f"Project: {proj.Name}, Task: ID={task.ID} Name='{task.Name}'")

    fields = load_field_map()
    unnamed = [f for f in fields if not f['com_name']]
    print(f"\nProbing {len(unnamed)} unnamed Task fields...")

    # Try to read each unnamed field
    readable = []
    errors = []
    non_zero = []

    for f in unnamed:
        fid = f['field_id_dec']
        try:
            val = task.GetField(fid)
            readable.append(f)
            f['value'] = val

            if val is not None and str(val) not in ('0', '', 'False', '0:00', '0%', '$0.00', 'NA', 'No', '0 hrs', '0 days'):
                non_zero.append(f)
        except Exception as e:
            errors.append((f, str(e)[:80]))

    print(f"\n  Readable: {len(readable)}")
    print(f"  Errors:   {len(errors)}")
    print(f"  Non-zero: {len(non_zero)}")

    # Show non-zero unnamed fields - these are the interesting ones
    if non_zero:
        print(f"\n{'='*90}")
        print(f"NON-ZERO UNNAMED FIELDS (potential discoveries)")
        print(f"{'='*90}")

        for f in non_zero:
            val = f['value']
            val_str = f'"{val}"' if isinstance(val, str) else str(val)
            val_safe = val_str[:60].encode('ascii', errors='replace').decode('ascii')
            print(f"  {f['field_id_hex']:<12s} type={f['type_name']:<8s} store={f['storage_name']:<12s} slot={f['slot_index']:<4d} = {val_safe}")

    # Group errors by type
    print(f"\n{'='*90}")
    print(f"ERROR PATTERNS FOR UNNAMED FIELDS")
    print(f"{'='*90}")

    err_groups = {}
    for f, err in errors:
        key = err[:40]
        if key not in err_groups:
            err_groups[key] = []
        err_groups[key].append(f)

    for key in sorted(err_groups.keys()):
        flds = err_groups[key]
        print(f"\n  Error: {key}")
        print(f"  Fields: {len(flds)}")
        for ff in flds[:5]:
            print(f"    {ff['field_id_hex']} type={ff['type_name']} store={ff['storage_name']}")

    # Analyze readable unnamed fields by type/storage patterns
    print(f"\n{'='*90}")
    print(f"READABLE UNNAMED FIELDS BY PATTERN")
    print(f"{'='*90}")

    patterns = {}
    for f in readable:
        key = (f['type_name'], f['storage_name'])
        if key not in patterns:
            patterns[key] = []
        patterns[key].append(f)

    for key in sorted(patterns.keys()):
        flds = patterns[key]
        vals = [str(f.get('value', ''))[:20].encode('ascii', errors='replace').decode('ascii') for f in flds[:5]]
        print(f"\n  {key[0]:<8s} / {key[1]:<12s}: {len(flds)} fields")
        print(f"    Sample values: {', '.join(vals)}")
        print(f"    Sample IDs: {', '.join(f['field_id_hex'] for f in flds[:5])}")

    # Save results
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis', 'unnamed_probe.tsv')
    with open(out_path, 'w', encoding='utf-8') as fout:
        fout.write('field_id_hex\tfield_id_dec\ttype\tstorage\tslot\tvalue\tstatus\n')
        for f in readable:
            val = str(f.get('value', '')).replace('\t', ' ')[:100]
            val = val.encode('utf-8', errors='replace').decode('utf-8')
            fout.write(f"{f['field_id_hex']}\t{f['field_id_dec']}\t{f['type_name']}\t{f['storage_name']}\t{f['slot_index']}\t{val}\tOK\n")
        for f, err in errors:
            fout.write(f"{f['field_id_hex']}\t{f['field_id_dec']}\t{f['type_name']}\t{f['storage_name']}\t{f['slot_index']}\t\tERROR: {err[:60]}\n")

    print(f"\nSaved to: {out_path}")


if __name__ == '__main__':
    main()
