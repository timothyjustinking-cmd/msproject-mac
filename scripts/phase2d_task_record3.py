#!/usr/bin/env python3
"""Phase 2d: Map task record layout by scanning around known Priority address.

Priority=500 is at 0x23b7ef6dd3c. Scan a wide area around it for ALL
known field values to discover the actual task record layout.
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


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    # Set distinctive values on key fields to make them findable
    print("\nSetting probe values on class 1 fields...")
    task.SetField(188743705, "777")     # Priority
    task.SetField(188743712, "42%")     # PercentComplete
    task.SetField(188743751, "Yes")     # Marked
    task.SetField(188743688, "$12345")  # FixedCost
    time.sleep(0.5)

    # Read current COM values
    com = {}
    fields_to_check = {
        'Priority': (188743705, 'i16'),
        'PercentComplete': (188743712, 'i16'),
        'Marked': (188743751, 'bool'),
        'Critical': (188743699, 'bool'),
        'FixedCost': (188743688, 'i64'),
        'Duration': (188743709, 'i32'),
        'Start': (188743715, 'date32'),
        'Finish': (188743716, 'date32'),
        'Created': (188743773, 'date32'),
        'ConstraintType': (188743697, 'i16'),
        'FreeSlack': (188743701, 'i32'),
        'Work': (188743680, 'i64'),
        'Cost': (188743685, 'i64'),
        'ActualDuration': (188743708, 'i32'),
        'RemainingDuration': (188743711, 'i32'),
        'EarlyStart': (188743717, 'date32'),
        'EarlyFinish': (188743718, 'date32'),
        'LateStart': (188743719, 'date32'),
        'LateFinish': (188743720, 'date32'),
        'Type': (188743808, 'i16'),
        'Active': (188744959, 'bool'),
        'Summary': (188743772, 'bool'),
        'Milestone': (188743704, 'bool'),
    }
    for name, (fid, _) in fields_to_check.items():
        try:
            com[name] = str(task.GetField(fid))
        except:
            com[name] = "ERROR"
    print("COM values:")
    for name, val in com.items():
        print(f"  {name:<25s} = '{val}'")

    # Differential scan on Priority
    regions = scan_all_regions(handle)
    print(f"\n{len(regions)} regions, {sum(s for _,s in regions)/1024/1024:.0f} MB")

    print("\nDifferential scan on Priority (already set to 777)...")
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<h', 777)))
    print(f"  Priority=777: {len(hits_a)} i16 hits")

    task.SetField(188743705, "333")
    time.sleep(0.5)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<h', 333)))
    print(f"  Priority=333: {len(hits_b)} i16 hits")

    # Restore probe value
    task.SetField(188743705, "777")
    time.sleep(0.3)

    prio_addrs = sorted(hits_a & hits_b)
    print(f"  Intersect: {len(prio_addrs)} addresses")

    # Filter out the direct-storage block hit by checking for field_id patterns nearby
    real_prio_addrs = []
    for pa in prio_addrs:
        # Read 128 bytes around this address
        ctx = read_mem(handle, pa - 64, 128)
        if not ctx:
            continue
        # Check if this looks like a direct-storage block entry (field_id at -24 from value)
        fid_at_neg24 = struct.unpack_from('<I', ctx, 64 - 24)[0] if len(ctx) >= 64 else 0
        if (fid_at_neg24 & 0xFFFF0000) == 0x0b400000:
            print(f"  {pa:#x}: in direct-storage block (fid={fid_at_neg24:#010x} at -24)")
            continue
        # Verify current value
        val = struct.unpack_from('<h', ctx, 64)[0]
        print(f"  {pa:#x}: value={val} (possible task record)")
        real_prio_addrs.append(pa)

    if not real_prio_addrs:
        print("ERROR: No real priority addresses found")
        task.SetField(188743705, "500")
        task.SetField(188743712, "0%")
        task.SetField(188743751, "No")
        task.SetField(188743688, "$0")
        CloseHandle(handle)
        return

    # For each real priority address, read 16KB around it and search for ALL probe values
    for pa in real_prio_addrs:
        print(f"\n{'='*100}")
        print(f"ANALYZING PRIORITY AT {pa:#x}")
        print(f"{'='*100}")

        # Read 8KB before and 8KB after
        scan_start = pa - 8192
        scan_size = 16384
        chunk = read_mem(handle, scan_start, scan_size)
        if not chunk or len(chunk) < scan_size:
            print(f"  FAILED to read {scan_size} bytes")
            continue

        prio_off = 8192  # Priority is at this offset in chunk

        # Search for known values
        # Priority = 777 (i16)
        # PercentComplete = 42 (i16)
        # Duration = 24000 (i32, 5 days * 4800)
        # FixedCost = 1234500 (i64, $12345 * 100)
        # Marked = 1 (bool as i16)
        # ConstraintType = 0 (ASAP)

        search_patterns = [
            ('Priority=777', struct.pack('<h', 777), 2),
            ('PercentComplete=42', struct.pack('<h', 42), 2),
            ('Duration=24000', struct.pack('<i', 24000), 4),
            ('FixedCost=1234500', struct.pack('<q', 1234500), 8),
            ('ConstraintType=0', None, 0),  # skip, too common
        ]

        for label, pat, size in search_patterns:
            if pat is None:
                continue
            pos = 0
            hits = []
            while pos < len(chunk) - size:
                p = chunk.find(pat, pos)
                if p == -1:
                    break
                rel = p - prio_off
                hits.append((p, rel))
                pos = p + 1
            if hits:
                print(f"\n  {label}: {len(hits)} hits")
                for p, rel in hits:
                    print(f"    chunk offset {p}, prio{rel:+d} (addr {scan_start + p:#x})")

        # Look for vtable pointers in the range before priority
        print(f"\n  Vtable pointers (within -8192 to +128 of priority):")
        vtable_hits = []
        for off in range(0, min(prio_off + 128, len(chunk) - 8), 8):
            ptr = struct.unpack_from('<Q', chunk, off)[0]
            if IMAGE_BASE < ptr < IMAGE_BASE + 0x2000000:
                rva = ptr - IMAGE_BASE
                rel = off - prio_off
                vtable_hits.append((off, rel, ptr, rva))
                print(f"    prio{rel:+5d} ({scan_start+off:#x}): {ptr:#x} RVA={rva:#x}")

        # Find the nearest vtable pointer BEFORE priority — likely the object base
        before_vtables = [(off, rel, ptr, rva) for off, rel, ptr, rva in vtable_hits if rel < 0]
        if before_vtables:
            nearest = max(before_vtables, key=lambda x: x[0])
            print(f"\n  Nearest vtable before priority: prio{nearest[1]:+d} RVA={nearest[3]:#x}")
            obj_base = scan_start + nearest[0]
            print(f"  -> Likely object base: {obj_base:#x}")
            print(f"  -> Priority at object+{pa - obj_base} (0x{pa - obj_base:x})")

            # Hex dump from object base to priority+64
            print(f"\n  Hex dump from object base:")
            dump_start = nearest[0]
            dump_end = min(prio_off + 128, len(chunk))
            for j in range(dump_start, dump_end, 16):
                c = chunk[j:j+16]
                hex_str = ' '.join(f'{b:02x}' for b in c)
                rel = j - prio_off
                # Annotate interesting positions
                ann = ""
                if j == nearest[0]:
                    ann = " << vtable/obj_base"
                elif j <= prio_off < j + 16:
                    ann = " << PRIORITY"
                addr = scan_start + j
                print(f"    {addr:#x} (prio{rel:+5d}): {hex_str}  {ann}")

        # Try to understand the layout using pointer chains
        # Look for a pointer in the object that might point to priority's area
        if before_vtables:
            obj_start = nearest[0]
            # Read the first 256 bytes of the object and check for pointers
            print(f"\n  Pointer analysis (first 256 bytes of object):")
            for off in range(obj_start, min(obj_start + 256, len(chunk) - 8), 8):
                ptr = struct.unpack_from('<Q', chunk, off)[0]
                # Is this pointer pointing into our scanned area?
                target_chunk_off = ptr - scan_start
                if 0 <= target_chunk_off < len(chunk):
                    rel_to_obj = off - obj_start
                    rel_to_prio = target_chunk_off - prio_off
                    print(f"    obj+{rel_to_obj:3d}: ptr -> prio{rel_to_prio:+5d} ({ptr:#x})")
                    # If this pointer + 0x910 (Priority byte_offset) = priority address
                    if ptr + 0x910 == pa:
                        print(f"      *** ptr + 0x910 = Priority! This is the data block base! ***")
                    if ptr + 0x740 == pa - (0x910 - 0x740):
                        print(f"      *** ptr + 0x740 would reach Duration area ***")

    # Cleanup
    task.SetField(188743705, "500")
    task.SetField(188743712, "0%")
    task.SetField(188743751, "No")
    task.SetField(188743688, "$0")
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
