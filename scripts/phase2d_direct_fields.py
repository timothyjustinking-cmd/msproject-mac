#!/usr/bin/env python3
"""Phase 2d: Map ALL class 1 (direct) fields to memory.

The mass probe found FixedCost, PercentComplete, and Marked in the direct-storage block,
but most class 1 fields (Duration, Start, Finish, etc.) weren't probed.
This script probes every class 1 field and also searches for a fixed-offset record
(the original "task record" where class 1 fields live at hardcoded byte offsets).
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


def load_field_map():
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'analysis')
    path = os.path.join(base_dir, 'field_definitive_map.tsv')
    fields = {}
    with open(path, 'r', encoding='utf-8') as f:
        f.readline()
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 18 and parts[0] == 'Task':
                fid = int(parts[2])
                fields[fid] = {
                    'name': parts[3] or f'unnamed_{fid:#x}',
                    'type': parts[5],
                    'storage_class': int(parts[9]) if parts[9] else -1,
                    'slot': int(parts[6]),
                    'byte_offset': int(parts[13]) if parts[13] else -1,
                }
    return fields


def find_direct_block(handle, regions, task):
    task.SetField(188743705, "987")
    time.sleep(0.3)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<i', 987)))
    task.SetField(188743705, "543")
    time.sleep(0.3)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<i', 543)))
    task.SetField(188743705, "500")
    time.sleep(0.2)
    prio_addrs = sorted(hits_a & hits_b)
    if not prio_addrs:
        return None, None, []
    prio_addr = prio_addrs[0]
    prio_entry = prio_addr - 0x18
    scan_start = prio_entry - 0x4000
    block = read_mem(handle, scan_start, 0xC000)
    if not block:
        return None, None, []
    known_off = prio_entry - scan_start
    STRIDE = 40
    first_off = known_off
    while first_off >= STRIDE:
        off = first_off - STRIDE
        fid = struct.unpack_from('<I', block, off)[0]
        if (fid & 0xFFFF0000) == 0x0b400000:
            first_off = off
        else:
            break
    last_off = known_off
    while last_off + 2 * STRIDE <= len(block):
        off = last_off + STRIDE
        fid = struct.unpack_from('<I', block, off)[0]
        if (fid & 0xFFFF0000) == 0x0b400000:
            last_off = off
        else:
            break
    entries = []
    for i in range((last_off - first_off) // STRIDE + 1):
        off = first_off + i * STRIDE
        fid = struct.unpack_from('<I', block, off)[0]
        sz_a = struct.unpack_from('<Q', block, off + 16)[0]
        sz_b = struct.unpack_from('<Q', block, off + 32)[0]
        val_a_raw = block[off + 8:off + 16]
        val_b_raw = block[off + 24:off + 32]
        entries.append((fid, val_a_raw, sz_a, val_b_raw, sz_b))
    return scan_start + first_off, block[first_off:last_off + STRIDE], entries


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    field_map = load_field_map()
    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    print(f"PID: {pid}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Get all class 1 fields
    class1_fields = [(fid, info) for fid, info in field_map.items()
                     if info['storage_class'] == 1 and info['name'] != 'unnamed_' + hex(fid)]
    class1_fields.sort(key=lambda x: x[0])

    print(f"\n  Class 1 (direct) fields: {len(class1_fields)} named")

    # Read all COM values first
    print(f"\n{'='*120}")
    print(f"CLASS 1 FIELD VALUES VIA COM")
    print(f"{'='*120}")

    com_values = {}
    for fid, info in class1_fields:
        try:
            val = task.GetField(fid)
            com_values[fid] = str(val)
        except:
            com_values[fid] = "ERROR"

    # Now find the direct-storage block
    regions = scan_all_regions(handle)
    db_addr, db_data, db_entries = find_direct_block(handle, regions, task)

    if not db_addr:
        print("  Direct block not found!")
        CloseHandle(handle)
        sys.exit(1)

    print(f"  Direct block at {db_addr:#x}: {len(db_entries)} entries")

    # Map field_ids in direct block
    db_fids = {}
    for fid, val_a, sz_a, val_b, sz_b in db_entries:
        db_fids[fid] = {'val_a': val_a, 'sz_a': sz_a, 'val_b': val_b, 'sz_b': sz_b}

    # Cross-reference
    print(f"\n{'='*120}")
    print(f"CLASS 1 FIELDS: COM vs DIRECT BLOCK")
    print(f"{'='*120}")
    print(f"{'FID':<12s} {'Name':<26s} {'Type':<8s} {'Slot':>4s} {'ByteOff':>8s} "
          f"{'COM Value':<25s} {'In DB?':>6s} {'DB val_b':<20s} {'DB sz_b':>7s}")
    print("-" * 130)

    found_in_db = 0
    not_found = []

    for fid, info in class1_fields:
        name = info['name']
        ftype = info['type']
        slot = info['slot']
        byte_off = info['byte_offset']
        com_val = com_values.get(fid, "?")[:25]

        in_db = fid in db_fids
        db_str = ""
        db_sz = ""
        if in_db:
            found_in_db += 1
            e = db_fids[fid]
            sz_b = e['sz_b']
            db_sz = str(sz_b)
            if sz_b == 0:
                db_str = "EMPTY"
            elif sz_b == 2:
                db_str = str(struct.unpack_from('<h', e['val_b'], 0)[0])
            elif sz_b == 4:
                v = struct.unpack_from('<I', e['val_b'], 0)[0]
                if v == 0xFFFFFFFF:
                    db_str = "NO_DATE"
                else:
                    db_str = str(struct.unpack_from('<i', e['val_b'], 0)[0])
            elif sz_b == 8:
                db_str = f"{struct.unpack_from('<d', e['val_b'], 0)[0]:.6g}"
            else:
                db_str = f"ptr/blob(sz={sz_b})"
        else:
            not_found.append((fid, info, com_val))

        marker = "*" if in_db else " "
        print(f"{fid:#010x}  {name:<26s} {ftype:<8s} {slot:4d} {byte_off:8d} "
              f"{com_val:<25s} {marker:>6s} {db_str:<20s} {db_sz:>7s}")

    print(f"\n  Found in direct block: {found_in_db}/{len(class1_fields)}")
    print(f"  Not found: {len(not_found)}")

    if not_found:
        print(f"\n  Missing class 1 fields (not in direct block):")
        for fid, info, cv in not_found:
            print(f"    {fid:#010x} {info['name']:<26s} {info['type']:<8s} COM='{cv}'")

    # Save
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            '..', 'analysis', 'class1_direct_mapping.tsv')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write("field_id\tname\ttype\tslot\tbyte_offset\tcom_value\tin_db\tdb_val\tdb_sz\n")
        for fid, info in class1_fields:
            cv = com_values.get(fid, "?")
            in_db = fid in db_fids
            db_str = ""
            db_sz = ""
            if in_db:
                e = db_fids[fid]
                db_sz = str(e['sz_b'])
                if e['sz_b'] == 0:
                    db_str = "EMPTY"
                elif e['sz_b'] == 2:
                    db_str = str(struct.unpack_from('<h', e['val_b'], 0)[0])
                elif e['sz_b'] == 4:
                    v = struct.unpack_from('<I', e['val_b'], 0)[0]
                    db_str = "NO_DATE" if v == 0xFFFFFFFF else str(struct.unpack_from('<i', e['val_b'], 0)[0])
                elif e['sz_b'] == 8:
                    db_str = f"{struct.unpack_from('<d', e['val_b'], 0)[0]:.6g}"
                else:
                    db_str = f"blob({e['sz_b']})"
            f.write(f"{fid:#010x}\t{info['name']}\t{info['type']}\t{info['slot']}\t"
                    f"{info['byte_offset']}\t{cv}\t{in_db}\t{db_str}\t{db_sz}\n")
    print(f"\n  Saved to {out_path}")

    CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
