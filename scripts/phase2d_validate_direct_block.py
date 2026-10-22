#!/usr/bin/env python3
"""Phase 2d: Validate the direct-storage block structure.

Confirmed: 40-byte entries with [field_id:u32][pad:u32][val_a:u64][size_a:u64][val_b:u64][size_b:u64]
- slot_a = baseline/default value
- slot_b = current value
- size tag: 0=empty, 2=i16/bool, 4=i32/date, 8=double

This script:
1. Locates the block via Priority differential scan
2. Finds the FULL extent of the block by scanning for field_id patterns at 40-byte stride
3. Changes multiple field values and re-reads to validate slot_a vs slot_b
4. Decodes ALL entries with COM cross-reference
"""

import ctypes
import struct
import os
import sys
import time

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
OpenProcess = k32.OpenProcess
ReadProcessMemory = k32.ReadProcessMemory
CloseHandle = k32.CloseHandle


def get_pid():
    import subprocess
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WINPROJ.EXE', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True)
    for line in r.stdout.strip().split('\n'):
        if 'WINPROJ' in line:
            return int(line.split(',')[1].strip('"'))
    return None


def get_image_base(pid):
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    hProcess = OpenProcess(0x0410 | 0x0010, False, pid)
    if not hProcess:
        return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess)
        return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base


def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok:
        return None
    return buf.raw[:n.value]


def scan_all_regions(handle):
    regions = []
    MEM_COMMIT = 0x1000
    READABLE = {0x02, 0x04, 0x20, 0x40}
    class MBI(ctypes.Structure):
        _fields_ = [
            ("BaseAddress", ctypes.c_void_p),
            ("AllocationBase", ctypes.c_void_p),
            ("AllocationProtect", ctypes.c_ulong),
            ("PartitionId", ctypes.c_ushort),
            ("RegionSize", ctypes.c_size_t),
            ("State", ctypes.c_ulong),
            ("Protect", ctypes.c_ulong),
            ("Type", ctypes.c_ulong),
        ]
    addr = 0
    mbi = MBI()
    while addr < 0x7FFFFFFFFFFF:
        result = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
        if result == 0:
            break
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE:
            if 0x1000 <= mbi.RegionSize <= 0x10000000:
                regions.append((mbi.BaseAddress, mbi.RegionSize))
        addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if addr <= (mbi.BaseAddress or 0):
            break
    return regions


def find_all_pattern(handle, regions, pattern):
    hits = []
    for base, size in regions:
        data = read_mem(handle, base, size)
        if not data:
            continue
        offset = 0
        while True:
            pos = data.find(pattern, offset)
            if pos == -1:
                break
            hits.append(base + pos)
            offset = pos + 1
    return hits


def load_field_names():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_definitive_map.tsv')
    names = {}
    with open(path, 'r', encoding='utf-8') as f:
        f.readline()
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18 and parts[0] == 'Task':
                fid = int(parts[2])
                name = parts[3]
                type_name = parts[5]
                storage = parts[10]
                type_code = int(parts[4])
                field_size = int(parts[7])
                names[fid] = (name or f'unnamed_{fid:#x}', type_name, storage, type_code, field_size)
    return names


def decode_value(entry_bytes, size_tag):
    if len(entry_bytes) < 8:
        return "?"
    if size_tag == 0:
        return "EMPTY"
    elif size_tag == 2:
        return str(struct.unpack_from('<i', entry_bytes, 0)[0])
    elif size_tag == 4:
        v = struct.unpack_from('<I', entry_bytes, 0)[0]
        if v == 0xFFFFFFFF:
            return "NO_DATE"
        return str(struct.unpack_from('<i', entry_bytes, 0)[0])
    elif size_tag == 8:
        return f"{struct.unpack_from('<d', entry_bytes, 0)[0]:.6g}"
    else:
        ptr = struct.unpack_from('<Q', entry_bytes, 0)[0]
        return f"ptr={ptr:#x}(len={size_tag})"


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")
    field_names = load_field_names()

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    if task.Name in ('0d', '10d', ''):
        task.Name = "Task 1"
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Step 1: Find Priority via differential scan
    print("\nStep 1: Locate Priority...")
    regions = scan_all_regions(handle)

    task.SetField(188743705, "987")
    time.sleep(0.3)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<i', 987)))

    task.SetField(188743705, "543")
    time.sleep(0.3)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<i', 543)))

    prio_addrs = sorted(hits_a & hits_b)
    if not prio_addrs:
        print("ERROR: Can't find Priority"); CloseHandle(handle); sys.exit(1)

    prio_addr = prio_addrs[0]
    print(f"  Priority at {prio_addr:#x}")

    # Priority value is at entry+0x18 (slot_b). Entry starts at prio_addr - 0x18.
    prio_entry_addr = prio_addr - 0x18

    # Step 2: Walk backward/forward at 40-byte stride to find all entries
    print("\nStep 2: Find full block extent...")

    STRIDE = 40

    # Read a large region
    scan_start = prio_entry_addr - 0x4000
    scan_size = 0x8000
    block = read_mem(handle, scan_start, scan_size)
    if not block:
        print("ERROR: Can't read block"); CloseHandle(handle); sys.exit(1)

    prio_entry_off = prio_entry_addr - scan_start

    # Walk backward from Priority entry
    first_entry_off = prio_entry_off
    while first_entry_off >= STRIDE:
        candidate_off = first_entry_off - STRIDE
        # Check if there's a valid field_id at this position
        if candidate_off + 4 <= len(block):
            fid = struct.unpack_from('<I', block, candidate_off)[0]
            if (fid & 0xFFFF0000) == 0x0b400000:
                first_entry_off = candidate_off
            else:
                break
        else:
            break

    # Walk forward from Priority entry
    last_entry_off = prio_entry_off
    while last_entry_off + 2 * STRIDE <= len(block):
        candidate_off = last_entry_off + STRIDE
        if candidate_off + 4 <= len(block):
            fid = struct.unpack_from('<I', block, candidate_off)[0]
            if (fid & 0xFFFF0000) == 0x0b400000:
                last_entry_off = candidate_off
            else:
                break
        else:
            break

    num_entries = (last_entry_off - first_entry_off) // STRIDE + 1
    block_start_addr = scan_start + first_entry_off
    block_end_addr = scan_start + last_entry_off + STRIDE

    print(f"  Block: {block_start_addr:#x} to {block_end_addr:#x}")
    print(f"  {num_entries} entries at 40-byte stride")
    print(f"  Total block size: {block_end_addr - block_start_addr} bytes")

    # Check if there's a header before the first entry
    pre_header = block[first_entry_off - 16:first_entry_off]
    if pre_header:
        print(f"\n  16 bytes before first entry:")
        hex_str = ' '.join(f'{b:02x}' for b in pre_header)
        vals = struct.unpack_from('<IIII', pre_header, 0) if len(pre_header) >= 16 else ()
        print(f"    {hex_str}")
        if vals:
            print(f"    as u32s: {vals}")
            print(f"    first u32: {vals[0]} (entry count?)")

    # Step 3: Decode all entries
    print(f"\n{'='*120}")
    print(f"FULL BLOCK DECODE: {num_entries} entries")
    print(f"{'='*120}")
    print(f"{'Idx':>4s} {'Address':<18s} {'FieldID':<12s} {'Name':<30s} {'Type':<8s} "
          f"{'Storage':<10s} {'SzA':>3s} {'ValueA':<20s} {'SzB':>3s} {'ValueB':<20s}")
    print(f"{'-'*4:>4s} {'-'*18:<18s} {'-'*12:<12s} {'-'*30:<30s} {'-'*8:<8s} "
          f"{'-'*10:<10s} {'-'*3:>3s} {'-'*20:<20s} {'-'*3:>3s} {'-'*20:<20s}")

    all_entries = []

    for i in range(num_entries):
        off = first_entry_off + i * STRIDE
        entry = block[off:off + STRIDE]
        if len(entry) < STRIDE:
            break

        fid = struct.unpack_from('<I', entry, 0)[0]
        pad = struct.unpack_from('<I', entry, 4)[0]
        val_a_raw = entry[8:16]
        size_a = struct.unpack_from('<Q', entry, 16)[0]
        val_b_raw = entry[24:32]
        size_b = struct.unpack_from('<Q', entry, 32)[0]

        info = field_names.get(fid, (f'unknown_{fid:#x}', '?', '?', 0, 0))
        name, type_name, storage, type_code, field_size = info

        val_a_str = decode_value(val_a_raw, size_a)
        val_b_str = decode_value(val_b_raw, size_b)

        abs_addr = scan_start + off
        marker = " <<" if fid == 0x0b400019 else ""

        print(f"{i:4d} {abs_addr:#018x} {fid:#010x}  {name:<30s} {type_name:<8s} "
              f"{storage:<10s} {size_a:3d} {val_a_str:<20s} {size_b:3d} {val_b_str:<20s}{marker}")

        all_entries.append({
            'index': i, 'addr': abs_addr, 'field_id': fid, 'name': name,
            'type': type_name, 'storage': storage,
            'size_a': size_a, 'val_a': val_a_str,
            'size_b': size_b, 'val_b': val_b_str,
        })

    # Step 4: Validation — change several fields and re-read
    print(f"\n{'='*120}")
    print(f"VALIDATION: Change fields and re-read slot_b")
    print(f"{'='*120}")

    test_changes = [
        (188743705, "Priority", "777"),
        (188743712, "PercentComplete", "73"),
    ]

    for fid, name, new_val in test_changes:
        task.SetField(fid, new_val)
    time.sleep(0.3)

    # Re-read the block
    block2 = read_mem(handle, scan_start, scan_size)
    if block2:
        print(f"\n  After changing Priority=777, PercentComplete=73:")
        for e in all_entries:
            off = e['addr'] - scan_start
            entry_old = block[off:off + STRIDE]
            entry_new = block2[off:off + STRIDE]
            if entry_old != entry_new:
                size_a_new = struct.unpack_from('<Q', entry_new, 16)[0]
                size_b_new = struct.unpack_from('<Q', entry_new, 32)[0]
                val_a_new = decode_value(entry_new[8:16], size_a_new)
                val_b_new = decode_value(entry_new[24:32], size_b_new)
                print(f"    CHANGED: {e['name']:<30s} "
                      f"slot_a: {e['val_a']} -> {val_a_new}  "
                      f"slot_b: {e['val_b']} -> {val_b_new}")

    # Step 5: COM cross-reference for ALL entries
    print(f"\n{'='*120}")
    print(f"COM CROSS-REFERENCE")
    print(f"{'='*120}")
    print(f"{'Name':<30s} {'COM Value':<30s} {'slot_a':<20s} {'slot_b':<20s} {'Match?':<10s}")
    print(f"{'-'*30:<30s} {'-'*30:<30s} {'-'*20:<20s} {'-'*20:<20s} {'-'*10:<10s}")

    match_count = 0
    total_checked = 0

    for e in all_entries:
        try:
            com_val = task.GetField(e['field_id'])
            com_str = str(com_val)[:30]
        except:
            com_str = "ERROR"
            continue

        # Try to match COM value to slot_b
        match = ""
        if com_str and com_str != "ERROR":
            total_checked += 1
            try:
                clean = com_str.replace('%', '').replace('$', '').replace(',', '').strip()
                if clean in ('Yes', 'No'):
                    com_num = 1 if clean == 'Yes' else 0
                elif clean and clean[0].isdigit() or (clean and clean[0] == '-'):
                    com_num = float(clean)
                else:
                    com_num = None

                if com_num is not None and e['val_b'] not in ('EMPTY', 'NO_DATE', '?'):
                    try:
                        slot_b_num = float(e['val_b'].split('(')[0].replace('ptr=', ''))
                    except ValueError:
                        slot_b_num = None

                    if slot_b_num is not None:
                        if e['type'] == 'i64' and 'Cost' in e['name']:
                            if abs(slot_b_num / 100 - com_num) < 0.01:
                                match = "MATCH-B(/100)"
                                match_count += 1
                        elif abs(slot_b_num - com_num) < 0.5:
                            match = "MATCH-B"
                            match_count += 1
            except (ValueError, TypeError):
                pass

        if com_str != "ERROR" and com_str.strip():
            print(f"{e['name']:<30s} {com_str:<30s} {e['val_a']:<20s} {e['val_b']:<20s} {match:<10s}")

    print(f"\n  Matched: {match_count}/{total_checked} numeric fields")

    # Step 6: Size tag analysis
    print(f"\n{'='*120}")
    print(f"SIZE TAG ANALYSIS")
    print(f"{'='*120}")

    from collections import Counter
    size_by_type = {}
    for e in all_entries:
        key = e['type']
        if key not in size_by_type:
            size_by_type[key] = Counter()
        if e['size_b'] > 0:
            size_by_type[key][e['size_b']] += 1
        if e['size_a'] > 0:
            size_by_type[key][e['size_a']] += 1

    for type_name in sorted(size_by_type.keys()):
        counts = size_by_type[type_name]
        print(f"  {type_name:<10s}: {dict(counts)}")

    # Save full decode
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            '..', 'analysis', 'direct_block_full_decode.tsv')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("index\taddr\tfield_id_hex\tfield_id_dec\tname\ttype\tstorage\t"
                "size_a\tval_a\tsize_b\tval_b\n")
        for e in all_entries:
            f.write(f"{e['index']}\t{e['addr']:#x}\t{e['field_id']:#010x}\t"
                    f"{e['field_id']}\t{e['name']}\t{e['type']}\t{e['storage']}\t"
                    f"{e['size_a']}\t{e['val_a']}\t{e['size_b']}\t{e['val_b']}\n")
    print(f"\n  Saved full decode to {out_path}")

    # Clean up
    task.SetField(188743705, "500")
    task.SetField(188743712, "42")
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
