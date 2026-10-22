#!/usr/bin/env python3
"""Phase 2d: Find task record by examining priority hits directly.

We found 2 addresses where Priority changes between i16 scans.
This script dumps memory around those addresses and looks for
other known field values nearby.
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

    # Get COM field values for cross-reference
    com_vals = {}
    field_fids = {
        'Duration': 188743709, 'Start': 188743715, 'Finish': 188743716,
        'Priority': 188743705, 'PercentComplete': 188743712,
        'Cost': 188743685, 'Work': 188743680, 'Created': 188743773,
        'ConstraintType': 188743697, 'FreeSlack': 188743701,
        'Critical': 188743699, 'Milestone': 188743704,
        'Summary': 188743772, 'Marked': 188743751,
        'Type': 188743808, 'Active': 188744959,
    }
    for name, fid in field_fids.items():
        try:
            com_vals[name] = str(task.GetField(fid))
        except:
            com_vals[name] = "ERROR"
    print("\nCOM field values:")
    for name, val in com_vals.items():
        print(f"  {name:<25s} = '{val}'")

    regions = scan_all_regions(handle)
    total_mb = sum(s for _, s in regions) / 1024 / 1024
    print(f"\n{len(regions)} regions, {total_mb:.0f} MB")

    # Differential scan on Priority
    print(f"\n{'='*100}")
    print("Differential scan on Priority (i16)")
    print(f"{'='*100}")

    task.SetField(188743705, "987")
    time.sleep(0.5)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<h', 987)))
    print(f"  Priority=987: {len(hits_a)} hits")

    task.SetField(188743705, "123")
    time.sleep(0.5)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<h', 123)))
    print(f"  Priority=123: {len(hits_b)} hits")

    prio_addrs = sorted(hits_a & hits_b)
    print(f"  Intersect: {len(prio_addrs)} addresses")

    task.SetField(188743705, "500")
    time.sleep(0.3)

    # For each Priority hit, dump a wide area around it and search for other known values
    for idx, pa in enumerate(prio_addrs):
        print(f"\n{'='*100}")
        print(f"PRIORITY HIT #{idx}: {pa:#x}")
        print(f"{'='*100}")

        # Read 8KB before and 4KB after the priority address
        scan_before = 4096
        scan_after = 2048
        scan_start = pa - scan_before
        scan_size = scan_before + scan_after
        chunk = read_mem(handle, scan_start, scan_size)
        if not chunk or len(chunk) < scan_size:
            print(f"  FAILED to read memory at {scan_start:#x}")
            continue

        prio_offset = scan_before  # Priority is at this offset in chunk

        # Verify Priority value
        prio_val = struct.unpack_from('<h', chunk, prio_offset)[0]
        print(f"  Priority at offset {prio_offset}: {prio_val} (expect 500)")

        # Search for Duration=24000 (5 days * 4800 min) as i32 within this chunk
        dur_expected = 24000
        dur_bytes = struct.pack('<i', dur_expected)
        dur_positions = []
        pos = 0
        while pos < len(chunk) - 4:
            p = chunk.find(dur_bytes, pos)
            if p == -1:
                break
            dur_positions.append(p)
            pos = p + 1

        print(f"  Duration=24000 found at offsets: {[f'{p-prio_offset:+d}' for p in dur_positions]}")

        # Search for date values (Start date should be a u32 plausible date)
        # Also search for i16 value 0 (PercentComplete=0), bool flags

        # Check if there's a vtable pointer nearby (look at 8-byte aligned addresses before priority)
        print(f"\n  Checking for vtable pointers (looking backward from priority):")
        for back in range(0, scan_before, 8):
            addr_to_check = prio_offset - back
            if addr_to_check < 0:
                break
            ptr = struct.unpack_from('<Q', chunk, addr_to_check)[0]
            if IMAGE_BASE < ptr < IMAGE_BASE + 0x2000000:
                rva = ptr - IMAGE_BASE
                rel_to_prio = addr_to_check - prio_offset
                actual_addr = scan_start + addr_to_check
                print(f"    offset {rel_to_prio:+5d} ({actual_addr:#x}): ptr={ptr:#x} RVA={rva:#x}")

        # Hex dump: 256 bytes around priority
        print(f"\n  Hex dump around Priority ({pa:#x}):")
        for j in range(max(0, prio_offset - 128), min(len(chunk), prio_offset + 128), 16):
            c = chunk[j:j+16]
            hex_str = ' '.join(f'{b:02x}' for b in c)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in c)
            rel = j - prio_offset
            marker = " <<< PRIORITY" if j == prio_offset or (j < prio_offset < j + 16) else ""
            print(f"    {rel:+5d}: {hex_str}  {ascii_str}{marker}")

        # Check for byte_offset hypothesis: is Priority at base+2320?
        # If so, base = pa - 2320 and Duration should be at base+1856 = pa-464
        hyp_base = pa - 2320
        dur_off = 1856
        dur_rel = dur_off - 2320  # = -464

        dur_check_offset = prio_offset + dur_rel
        if 0 <= dur_check_offset < len(chunk) - 4:
            dur_check = struct.unpack_from('<i', chunk, dur_check_offset)[0]
            print(f"\n  Hypothesis: base={hyp_base:#x}")
            print(f"    Duration at base+{dur_off} (prio{dur_rel:+d}): {dur_check} (expect {dur_expected})")

            # Start at base+416 (= prio-1904)
            start_rel = 416 - 2320  # = -1904
            start_check_off = prio_offset + start_rel
            if 0 <= start_check_off < len(chunk) - 4:
                start_check = struct.unpack_from('<I', chunk, start_check_off)[0]
                print(f"    Start at base+416 (prio{start_rel:+d}): {start_check} ({start_check:#x})")

            # Created at base+1988 (= prio-332)
            created_rel = 1988 - 2320  # = -332
            created_check_off = prio_offset + created_rel
            if 0 <= created_check_off < len(chunk) - 4:
                created_check = struct.unpack_from('<I', chunk, created_check_off)[0]
                print(f"    Created at base+1988 (prio{created_rel:+d}): {created_check} ({created_check:#x})")

            # ConstraintType at base+1840 (= prio-480)
            ct_rel = 1840 - 2320
            ct_off = prio_offset + ct_rel
            if 0 <= ct_off < len(chunk) - 2:
                ct = struct.unpack_from('<h', chunk, ct_off)[0]
                print(f"    ConstraintType at base+1840 (prio{ct_rel:+d}): {ct}")

            # Check vtable at base+0 (= prio-2320)
            vtable_off = prio_offset - 2320
            if 0 <= vtable_off < len(chunk) - 8:
                vt = struct.unpack_from('<Q', chunk, vtable_off)[0]
                if IMAGE_BASE < vt < IMAGE_BASE + 0x2000000:
                    rva = vt - IMAGE_BASE
                    print(f"    vtable at base+0 (prio-2320): {vt:#x} (RVA={rva:#x}) ** MATCH **")
                else:
                    print(f"    vtable at base+0: {vt:#x} (not in image)")

    # Restore
    task.SetField(188743705, "500")
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
