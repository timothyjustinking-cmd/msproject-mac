#!/usr/bin/env python3
"""Phase 2d: Investigate flag bitmask storage in TASKNODE.

Mass probe showed Flag1=2, Flag2=4, Flag3=8, ..., Flag20=1048576 in TASKNODE.
These are powers of 2 suggesting a single u32 bitmask.
This script verifies by reading the TASKNODE index to find flag entry offsets.
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
                }
    return fields


def find_tasknode(handle, regions, field_map, task):
    probe_text = "FLAG_BITMASK_PROBE"
    task.SetField(188743731, probe_text)  # Text1
    time.sleep(0.3)

    text_pattern = probe_text.encode('utf-16-le')
    name_pattern = task.Name.encode('utf-16-le')
    text_hits = find_all_pattern(handle, regions, text_pattern)

    for t1_addr in text_hits:
        block_start = t1_addr - 0x8000
        block = read_mem(handle, block_start, 0x14000)
        if not block:
            continue

        name_pos = block.find(name_pattern)
        if name_pos < 0:
            continue

        marker = b'\x40\x0b'
        entries = []
        search_start = max(0, name_pos - 0x800)
        pos = search_start
        while pos < name_pos:
            mpos = block.find(marker, pos, name_pos)
            if mpos == -1:
                break
            if mpos >= 2:
                low16 = struct.unpack_from('<H', block, mpos - 2)[0]
                fid = 0x0b400000 + low16
                if fid in field_map:
                    offset_32 = struct.unpack_from('<I', block, mpos + 2)[0]
                    entries.append((mpos - 2, fid, offset_32))
            pos = mpos + 1

        if len(entries) < 5:
            continue

        clusters = []
        cur = [entries[0]]
        for e in entries[1:]:
            if e[0] - cur[-1][0] <= 10:
                cur.append(e)
            else:
                clusters.append(cur)
                cur = [e]
        clusters.append(cur)
        idx_table = max(clusters, key=len)

        for off, fid, data_off in idx_table:
            if fid == 0x0b40000e:  # Name
                base_off = name_pos - data_off
                tn_addr = block_start + base_off
                return tn_addr, block, block_start, idx_table

    return None, None, None, None


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

    # Build flag FID list
    all_flag_fids = list(range(188743752, 188743762)) + list(range(188743972, 188743982))
    flag_name = {}
    for i, fid in enumerate(all_flag_fids):
        flag_name[fid] = f"Flag{i+1}"

    # Set various flag combinations to study the bitmask
    # First: set flags 1,3,5,7 only
    for fid in all_flag_fids:
        task.SetField(fid, "No")
    time.sleep(0.2)

    for idx in [0, 2, 4, 6]:  # Flag1,3,5,7
        task.SetField(all_flag_fids[idx], "Yes")
    time.sleep(0.3)

    regions = scan_all_regions(handle)
    print(f"  {len(regions)} regions")

    # Find TASKNODE with flags 1,3,5,7 set
    tn_addr, tn_block, tn_start, tn_index = find_tasknode(handle, regions, field_map, task)
    task.SetField(188743731, "")  # clear Text1 probe

    if not tn_addr:
        print("ERROR: TASKNODE not found")
        CloseHandle(handle)
        sys.exit(1)

    base_off = tn_addr - tn_start
    header = tn_block[base_off:base_off + 8]
    total_size = struct.unpack_from('<I', header, 0)[0]
    entry_count = struct.unpack_from('<I', header, 4)[0]
    print(f"  TASKNODE at {tn_addr:#x}: size={total_size}, entries={entry_count}")

    # Find all flag entries in the index
    print(f"\n{'='*120}")
    print("FLAG FIELD INDEX ENTRIES")
    print(f"{'='*120}")

    flag_fids = {}
    for i in range(1, 21):
        fid = 188743751 + i
        flag_fids[fid] = f"Flag{i}"

    flag_entries = []
    for idx_off, fid, data_off in tn_index:
        info = field_map.get(fid, {'name': f'?_{fid:#x}'})
        name = info['name']
        is_flag = fid in flag_fids

        if is_flag or 'Flag' in name:
            val = tn_block[base_off + data_off:base_off + data_off + 8]
            val_u32 = struct.unpack_from('<I', val, 0)[0] if len(val) >= 4 else 0
            val_hex = val[:4].hex() if len(val) >= 4 else val.hex()
            flag_entries.append((fid, name, data_off, val_u32))
            print(f"  {fid:#010x}  {name:<15s}  data_off={data_off:#06x} ({data_off:5d})  "
                  f"value={val_u32:10d}  (0x{val_u32:08x})  binary={val_u32:022b}")

    # Check: do all flags share the SAME data_off?
    unique_offsets = set(e[2] for e in flag_entries)
    print(f"\n  Unique data_offsets among flag entries: {len(unique_offsets)}")
    for off in sorted(unique_offsets):
        names = [e[1] for e in flag_entries if e[2] == off]
        val = [e[3] for e in flag_entries if e[2] == off][0]
        print(f"    offset {off:#06x}: {', '.join(names)} = {val} (0x{val:08x}) = {val:022b}")

    # Read the actual memory at each unique offset to see what's there
    print(f"\n{'='*120}")
    print("RAW MEMORY AT FLAG OFFSETS")
    print(f"{'='*120}")

    for off in sorted(unique_offsets):
        pos = base_off + off
        chunk = tn_block[pos:pos + 16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        u32_0 = struct.unpack_from('<I', chunk, 0)[0] if len(chunk) >= 4 else 0
        u32_1 = struct.unpack_from('<I', chunk, 4)[0] if len(chunk) >= 8 else 0
        print(f"  offset {off:#06x}: {hex_str}")
        print(f"    u32[0] = {u32_0:10d} (0x{u32_0:08x}) binary={u32_0:032b}")
        print(f"    u32[1] = {u32_1:10d} (0x{u32_1:08x})")

        # Test if this is a bitmask
        # Flags 1,3,5,7 set = bits 1,3,5,7 = 2+8+32+128 = 170
        # Or maybe bits 0,2,4,6 = 1+4+16+64 = 85
        expected_bitmask_a = sum(1 << i for i in [1, 3, 5, 7])  # 170
        expected_bitmask_b = sum(1 << (i-1) for i in [1, 3, 5, 7])  # 85
        print(f"    Expected bitmask (bit=flag#): {expected_bitmask_a} (0x{expected_bitmask_a:x})")
        print(f"    Expected bitmask (bit=flag#-1): {expected_bitmask_b} (0x{expected_bitmask_b:x})")

    # Now change flags: set flags 1,2,3,4,5 and re-check
    print(f"\n{'='*120}")
    print("CHANGING FLAGS: set 1-5 only")
    print(f"{'='*120}")

    for fid in all_flag_fids:
        task.SetField(fid, "No")
    for idx in range(5):  # Flag1-5
        task.SetField(all_flag_fids[idx], "Yes")
    time.sleep(0.3)

    # Re-read TASKNODE
    task.SetField(188743731, "FLAG_BITMASK_CHECK2")
    time.sleep(0.3)
    tn_addr2, tn_block2, tn_start2, tn_index2 = find_tasknode(handle, regions, field_map, task)
    task.SetField(188743731, "")

    if tn_addr2:
        base_off2 = tn_addr2 - tn_start2

        for off in sorted(unique_offsets):
            pos = base_off2 + off
            chunk = tn_block2[pos:pos + 8]
            u32_0 = struct.unpack_from('<I', chunk, 0)[0] if len(chunk) >= 4 else 0
            print(f"  offset {off:#06x}: u32 = {u32_0:10d} (0x{u32_0:08x}) binary={u32_0:032b}")
            expected = sum(1 << i for i in range(1, 6))  # bits 1-5 = 62
            print(f"    Expected (bit=flag#): {expected} (0x{expected:x}) = {expected:032b}")

    # Third test: set ONLY Flag17
    print(f"\n{'='*120}")
    print("CHANGING FLAGS: set Flag17 only")
    print(f"{'='*120}")

    for fid in all_flag_fids:
        task.SetField(fid, "No")
    task.SetField(all_flag_fids[16], "Yes")  # Flag17
    time.sleep(0.3)

    task.SetField(188743731, "FLAG_BITMASK_CHECK3")
    time.sleep(0.3)
    tn_addr3, tn_block3, tn_start3, tn_index3 = find_tasknode(handle, regions, field_map, task)
    task.SetField(188743731, "")

    if tn_addr3:
        base_off3 = tn_addr3 - tn_start3
        for off in sorted(unique_offsets):
            pos = base_off3 + off
            chunk = tn_block3[pos:pos + 8]
            u32_0 = struct.unpack_from('<I', chunk, 0)[0] if len(chunk) >= 4 else 0
            print(f"  offset {off:#06x}: u32 = {u32_0:10d} (0x{u32_0:08x}) binary={u32_0:032b}")
            print(f"    Expected Flag17 bit=17: {1<<17} = {1<<17:032b}")

    # Clean up
    for fid in all_flag_fids:
        task.SetField(fid, "No")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
