#!/usr/bin/env python3
"""Phase 2d: Validate field mapping using COM automation.

Uses win32com to access MS Project COM interface and read actual field values
for a test task, then cross-references with our definitive field mapping.
"""

import os
import sys

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 not installed. Run: pip install pywin32")
    sys.exit(1)


TYPE_NAMES = {
    0: 'null', 2: 'i16', 3: 'i32', 5: 'i64', 8: 'text', 9: 'obj_ref',
    11: 'bool', 19: 'date32', 20: 'comp_dur', 21: 'order_id', 29: 'virtual', 72: 'guid'
}


def load_field_map():
    """Load our definitive field mapping."""
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
                    'storage_class': parts[9],
                    'storage_name': parts[10],
                    'byte_offset': parts[12],
                })
    return fields


def main():
    print("Connecting to MS Project via COM...")
    try:
        app = win32com.client.Dispatch("MSProject.Application")
    except Exception as e:
        print(f"ERROR: Cannot connect to MS Project: {e}")
        sys.exit(1)

    proj = app.ActiveProject
    if proj is None:
        print("ERROR: No project open in MS Project")
        sys.exit(1)

    print(f"Project: {proj.Name}")

    tasks = proj.Tasks
    if tasks.Count == 0:
        print("ERROR: No tasks in project")
        sys.exit(1)

    # Find first non-null task
    task = None
    for i in range(1, tasks.Count + 1):
        t = tasks.Item(i)
        if t is not None:
            task = t
            break

    if task is None:
        print("ERROR: All tasks are null")
        sys.exit(1)

    print(f"Test task: ID={task.ID}, Name='{task.Name}'")

    # Load our field mapping
    fields = load_field_map()
    print(f"\nLoaded {len(fields)} Task field definitions")

    # Try to read values using GetField
    print(f"\n{'='*100}")
    print(f"FIELD VALUE VALIDATION")
    print(f"{'='*100}")

    # PjField constants for GetField
    success_count = 0
    error_count = 0
    value_mismatch = 0
    results = []

    named_fields = [f for f in fields if f['com_name']]

    print(f"\nTesting {len(named_fields)} named fields...")
    print(f"{'field_id':<12s} {'name':<35s} {'type':<8s} {'store':<8s} {'value'}")
    print(f"{'-'*12} {'-'*35} {'-'*8} {'-'*8} {'-'*40}")

    tested = 0
    for f in named_fields:
        fid = f['field_id_dec']

        try:
            val = task.GetField(fid)
            status = 'OK'
            success_count += 1

            # Format value for display
            if val is None:
                val_str = "<None>"
            elif isinstance(val, str):
                val_str = f'"{val[:40]}"' if len(str(val)) > 40 else f'"{val}"'
            else:
                val_str = str(val)[:50]

            results.append({
                'field_id': f['field_id_hex'],
                'name': f['com_name'],
                'type': f['type_name'],
                'store': f['storage_name'],
                'value': val_str,
                'status': status,
                'raw_value': val,
            })

            if tested < 100 or (val is not None and str(val) not in ('0', '', 'False', '0:00')):
                print(f"  {f['field_id_hex']:<12s} {f['com_name']:<35s} {f['type_name']:<8s} {f['storage_name']:<8s} {val_str}")

        except Exception as e:
            error_count += 1
            err_str = str(e)[:60]
            if tested < 50:
                print(f"  {f['field_id_hex']:<12s} {f['com_name']:<35s} {f['type_name']:<8s} {f['storage_name']:<8s} ERROR: {err_str}")

        tested += 1

    print(f"\n{'='*80}")
    print(f"VALIDATION SUMMARY")
    print(f"{'='*80}")
    print(f"  Fields tested:  {tested}")
    print(f"  Success:        {success_count}")
    print(f"  Errors:         {error_count}")

    # Analyze which storage classes have non-zero values
    by_storage = {}
    for r in results:
        sc = r['store']
        if sc not in by_storage:
            by_storage[sc] = {'total': 0, 'nonzero': 0}
        by_storage[sc]['total'] += 1
        v = r['raw_value']
        if v is not None and str(v) not in ('0', '', 'False', '0:00', '"0"', '""'):
            by_storage[sc]['nonzero'] += 1

    print(f"\n  {'Storage':<12s} {'Total':>6s} {'Non-zero':>8s}")
    print(f"  {'-'*12} {'-'*6} {'-'*8}")
    for sc in sorted(by_storage.keys()):
        info = by_storage[sc]
        print(f"  {sc:<12s} {info['total']:>6d} {info['nonzero']:>8d}")

    # Show all non-zero values
    print(f"\n{'='*80}")
    print(f"ALL NON-ZERO/NON-EMPTY VALUES")
    print(f"{'='*80}")

    for r in results:
        v = r['raw_value']
        if v is not None and str(v) not in ('0', '', 'False', '0:00', '"0"', '""'):
            print(f"  {r['field_id']:<12s} {r['name']:<35s} {r['type']:<8s} {r['store']:<8s} = {r['value']}")

    # Save results to TSV
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis', 'com_validation.tsv')
    with open(out_path, 'w', encoding='utf-8') as fout:
        fout.write('field_id_hex\tcom_name\ttype\tstorage\tvalue\tstatus\n')
        for r in results:
            fout.write(f"{r['field_id']}\t{r['name']}\t{r['type']}\t{r['store']}\t{r['value']}\t{r['status']}\n")

    print(f"\nSaved to: {out_path}")


if __name__ == '__main__':
    main()
