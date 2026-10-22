#!/usr/bin/env python3
"""Phase 2d: Find the header/container of the direct-storage block.

We know the block has 17 entries starting at 0x23b7f585ca0 (relative to ASLR).
This script:
1. Reads a large region around the block to find the allocation boundary
2. Looks for a header with entry_count or total_size
3. Checks what's AFTER the block (more entries? different structure?)
4. Looks for a pointer TO this block (the parent object)
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
                names[fid] = name or f'unnamed_{fid:#x}'
    return names


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

    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Find Priority to anchor
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
    prio_entry = prio_addr - 0x18  # entry start = value addr - 0x18
    print(f"Priority entry at {prio_entry:#x}")

    # The block of 17 entries starts at prio_entry - 10*40 = prio_entry - 400
    block_start_est = prio_entry - 10 * 40  # entry index 10

    # Read a big region: 0x1000 before and after
    region_start = block_start_est - 0x1000
    region = read_mem(handle, region_start, 0x3000)
    if not region:
        print("ERROR: Can't read region"); CloseHandle(handle); sys.exit(1)

    # Part 1: Scan for ALL field_ids in the region at 40-byte intervals
    print(f"\n{'='*100}")
    print("SCANNING FOR FIELD_IDS AT 40-BYTE STRIDE")
    print(f"{'='*100}")

    # Start from a known entry and walk in both directions
    known_off = block_start_est - region_start  # offset of first known entry

    # Walk backward
    off = known_off
    first_off = off
    while off >= 40:
        off -= 40
        fid = struct.unpack_from('<I', region, off)[0]
        if (fid & 0xFFFF0000) == 0x0b400000 and fid in field_names:
            first_off = off
        else:
            break

    # Walk forward from last known entry (entry 16)
    off = known_off + 16 * 40
    last_off = off
    while off + 40 <= len(region):
        off += 40
        if off + 4 <= len(region):
            fid = struct.unpack_from('<I', region, off)[0]
            if (fid & 0xFFFF0000) == 0x0b400000 and fid in field_names:
                last_off = off
            else:
                # Check if it's a field_id we don't have in field_names
                if (fid & 0xFFFF0000) == 0x0b400000:
                    last_off = off
                else:
                    break
        else:
            break

    total_entries = (last_off - first_off) // 40 + 1
    block_addr = region_start + first_off
    print(f"  Block range: {block_addr:#x} to {region_start + last_off + 40:#x}")
    print(f"  Total entries: {total_entries}")

    # Part 2: Look at what's BEFORE the first entry
    print(f"\n{'='*100}")
    print("DATA BEFORE FIRST ENTRY (possible header)")
    print(f"{'='*100}")

    pre_start = max(0, first_off - 0x100)
    for off in range(pre_start, first_off, 16):
        chunk = region[off:off + 16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        abs_addr = region_start + off

        # Annotate interesting values
        ann = ""
        for i in range(0, min(16, len(chunk)) - 3, 4):
            u32 = struct.unpack_from('<I', chunk, i)[0]
            if u32 == total_entries:
                ann += f" << u32={u32} (=entry_count!)"
            elif u32 == total_entries * 40:
                ann += f" << u32={u32} (=block_size!)"
            elif u32 == total_entries * 40 + 8:
                ann += f" << u32={u32} (=block_size+8!)"

        # Check for pointers
        if len(chunk) >= 8:
            for i in range(0, min(16, len(chunk)) - 7, 8):
                qw = struct.unpack_from('<Q', chunk, i)[0]
                if IMAGE_BASE < qw < IMAGE_BASE + 0x2000000:
                    rva = qw - IMAGE_BASE
                    if 0x800000 < rva < 0x1400000:
                        ann += f" << vtable? RVA={rva:#x}"
                if abs(qw - block_addr) < 0x10000:
                    ann += f" << ptr to block? delta={qw - block_addr:+d}"

        print(f"  {abs_addr:#014x}: {hex_str:<48s} {ascii_str}  {ann}")

    # Part 3: What's AFTER the last entry?
    print(f"\n{'='*100}")
    print("DATA AFTER LAST ENTRY")
    print(f"{'='*100}")

    post_start = last_off + 40
    post_end = min(len(region), post_start + 0x100)
    for off in range(post_start, post_end, 16):
        chunk = region[off:min(off + 16, post_end)]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        abs_addr = region_start + off

        # Check for field_ids
        ann = ""
        for i in range(0, min(16, len(chunk)) - 3, 4):
            u32 = struct.unpack_from('<I', chunk, i)[0]
            if (u32 & 0xFFFF0000) == 0x0b400000:
                name = field_names.get(u32, f'unknown')
                ann += f" << field_id {u32:#x}({name})"

        print(f"  {abs_addr:#014x}: {hex_str:<48s} {ascii_str}  {ann}")

    # Part 4: Search for pointers TO the block start
    print(f"\n{'='*100}")
    print(f"SEARCHING FOR POINTERS TO BLOCK ({block_addr:#x})")
    print(f"{'='*100}")

    ptr_pattern = struct.pack('<Q', block_addr)
    ptr_hits = find_all_pattern(handle, regions, ptr_pattern)
    print(f"  Found {len(ptr_hits)} pointers to block start")

    for ptr_addr in ptr_hits[:10]:
        # Read context around the pointer
        ctx = read_mem(handle, ptr_addr - 0x40, 0xC0)
        if ctx:
            print(f"\n  Pointer at {ptr_addr:#x}:")
            for off in range(0, 0xC0, 16):
                chunk = ctx[off:min(off + 16, 0xC0)]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                abs_addr = ptr_addr - 0x40 + off
                ann = ""
                if off == 0x40:
                    ann = " << PTR TO BLOCK"
                # Check for vtables
                if len(chunk) >= 8:
                    for i in range(0, min(16, len(chunk)) - 7, 8):
                        qw = struct.unpack_from('<Q', chunk, i)[0]
                        if IMAGE_BASE < qw < IMAGE_BASE + 0x2000000:
                            rva = qw - IMAGE_BASE
                            if 0x800000 < rva < 0x1400000:
                                ann += f" vtable?RVA={rva:#x}"
                print(f"    {abs_addr:#014x}: {hex_str:<48s} {ascii_str}  {ann}")

    # Also search for pointer to first_off - 8 (in case header includes 8-byte prefix)
    for delta in [-8, -16, -24, -32, -40]:
        alt_addr = block_addr + delta
        alt_ptr = struct.pack('<Q', alt_addr)
        alt_hits = find_all_pattern(handle, regions, alt_ptr)
        if alt_hits:
            print(f"\n  Pointers to block{delta:+d} ({alt_addr:#x}): {len(alt_hits)} hits")
            for h in alt_hits[:3]:
                print(f"    {h:#x}")

    # Part 5: Also look at the TASKNODE to see if it's in the same allocation
    print(f"\n{'='*100}")
    print("CHECKING TASKNODE PROXIMITY")
    print(f"{'='*100}")

    # Find TASKNODE via text probe
    task.SetField(188743731, "HEADER_SCAN_XYZ")
    time.sleep(0.3)
    text_pattern = "HEADER_SCAN_XYZ".encode('utf-16-le')
    text_hits = find_all_pattern(handle, regions, text_pattern)

    if text_hits:
        tn_addr = text_hits[0]
        delta = tn_addr - block_addr
        print(f"  TASKNODE text at {tn_addr:#x}")
        print(f"  Block start at {block_addr:#x}")
        print(f"  Delta: {delta:+d} ({delta:#x})")
        if abs(delta) < 0x10000:
            print(f"  NEARBY! Likely same allocation.")
        else:
            print(f"  FAR APART. Separate allocations.")

    task.SetField(188743731, "")

    # Clean up
    task.SetField(188743705, "500")
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
