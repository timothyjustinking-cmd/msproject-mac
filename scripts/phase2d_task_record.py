#!/usr/bin/env python3
"""Phase 2d: Find the TASK RECORD — fixed-layout object where class 1 fields live.

The direct-storage block is sparse/overlay. Class 1 fields have byte_offset values
in the descriptor table (Priority=2320, Start=416, Duration=1856, etc.).
These must be offsets into a fixed-layout task record.

This script uses differential scanning on Duration to find this record.
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
    print(f"  Duration: {task.GetField(188743709)}")
    print(f"  Start: {task.GetField(188743715)}")
    print(f"  Priority: {task.GetField(188743705)}")

    regions = scan_all_regions(handle)
    total_mb = sum(s for _, s in regions) / 1024 / 1024
    print(f"  {len(regions)} regions, {total_mb:.0f} MB")

    # Method 1: Differential scan on Duration
    # Duration is stored in minutes internally. Default is 5 days = 24000 min.
    # Change to 7 days = 33600 min, scan, change to 3 days = 14400 min, scan, intersect.
    print(f"\n{'='*100}")
    print("METHOD 1: Differential scan on Duration")
    print(f"{'='*100}")

    # Use Priority for differential scan — it works with SetField and has byte_offset=2320
    task.SetField(188743705, "987")
    time.sleep(0.5)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<h', 987)))
    print(f"  Priority=987: {len(hits_a)} hits (i16)")

    task.SetField(188743705, "123")
    time.sleep(0.5)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<h', 123)))
    print(f"  Priority=123: {len(hits_b)} hits (i16)")

    # Also try as i32
    hits_a32 = set(find_all_pattern(handle, regions, struct.pack('<i', 987)))
    hits_b32 = set(find_all_pattern(handle, regions, struct.pack('<i', 123)))

    task.SetField(188743705, "500")
    time.sleep(0.3)

    prio_addrs_i16 = sorted(hits_a & hits_b)
    prio_addrs_i32 = sorted(hits_a32 & hits_b32)
    print(f"  Intersect i16: {len(prio_addrs_i16)} addresses")
    print(f"  Intersect i32: {len(prio_addrs_i32)} addresses")

    # Priority byte_offset = 2320 from descriptor table
    # If Priority is at record_base + 2320, then record_base = prio_addr - 2320.
    # Duration should be at record_base + 1856 = 24000 (5 days * 4800 min)
    # Start should be at record_base + 416 = a plausible date

    prio_byte_off = 2320
    dur_expected = 5 * 4800  # 24000 min

    print(f"\n{'='*100}")
    print("CROSS-REFERENCING: check other fields at predicted offsets")
    print(f"{'='*100}")

    # Use BOTH i16 and i32 hits
    all_prio_addrs = set(prio_addrs_i16) | set(prio_addrs_i32)

    candidates = []
    for prio_addr in sorted(all_prio_addrs):
        record_base = prio_addr - prio_byte_off
        if record_base < 0:
            continue

        chunk = read_mem(handle, record_base, 4096)
        if not chunk or len(chunk) < 4096:
            continue

        # Check Duration at byte_offset 1856 (should be 24000 as i32)
        dur_off = 1856
        dur_val = struct.unpack_from('<i', chunk, dur_off)[0]

        # Check Start at byte_offset 416 (plausible date)
        start_off = 416
        start_val = struct.unpack_from('<I', chunk, start_off)[0]

        # Check Created at byte_offset 1988
        created_off = 1988
        created_val = struct.unpack_from('<I', chunk, created_off)[0]

        # Re-read priority at 2320 to confirm
        prio_val = struct.unpack_from('<h', chunk, prio_byte_off)[0]

        score = 0
        if dur_val == dur_expected:
            score += 2  # strong signal
        if prio_val == 500:
            score += 1
        if 900000000 < start_val < 1200000000:
            score += 1
        if 900000000 < created_val < 1200000000:
            score += 1

        if score >= 3:
            candidates.append((record_base, score, dur_val, prio_val, start_val, created_val))
            print(f"\n  CANDIDATE record_base={record_base:#x} (score={score})")
            print(f"    Duration(@{dur_off})={dur_val} (expect {dur_expected})")
            print(f"    Priority(@{prio_byte_off})={prio_val} (expect 500)")
            print(f"    Start(@{start_off})={start_val:#x}")
            print(f"    Created(@{created_off})={created_val:#x}")

            vtable_ptr = struct.unpack_from('<Q', chunk, 0)[0]
            if IMAGE_BASE < vtable_ptr < IMAGE_BASE + 0x2000000:
                rva = vtable_ptr - IMAGE_BASE
                print(f"    vtable at +0: {vtable_ptr:#x} (RVA={rva:#x})")
            else:
                print(f"    +0: {vtable_ptr:#x} (not a vtable)")

    if not candidates:
        print("  No candidates found with score >= 3")
        print(f"\n  Trying lower threshold (score >= 2)...")
        for prio_addr in sorted(all_prio_addrs)[:50]:
            record_base = prio_addr - prio_byte_off
            if record_base < 0:
                continue
            chunk = read_mem(handle, record_base, 4096)
            if not chunk or len(chunk) < 4096:
                continue
            dur_val = struct.unpack_from('<i', chunk, 1856)[0]
            start_val = struct.unpack_from('<I', chunk, 416)[0]
            prio_val = struct.unpack_from('<h', chunk, prio_byte_off)[0]
            score = 0
            if dur_val == dur_expected: score += 2
            if prio_val == 500: score += 1
            if 900000000 < start_val < 1200000000: score += 1
            if score >= 2:
                print(f"    {record_base:#x}: dur={dur_val} prio={prio_val} start={start_val:#x} score={score}")

        # Also dump raw addresses
        print(f"\n  First 30 priority i32 addresses:")
        for pa in prio_addrs_i32[:30]:
            print(f"    {pa:#x}")
        print(f"\n  First 30 priority i16 addresses:")
        for pa in prio_addrs_i16[:30]:
            print(f"    {pa:#x}")

    # Method 2: Deep analysis of best candidate
    if candidates:
        best = max(candidates, key=lambda x: x[1])
        record_base = best[0]
        print(f"\n{'='*100}")
        print(f"BEST CANDIDATE: record_base={record_base:#x}")
        print(f"{'='*100}")

        chunk = read_mem(handle, record_base, 8192)
        if chunk and len(chunk) >= 4096:
            # Read fields at known byte_offsets from descriptor table
            # NOTE: Multiple fields share byte_offsets — they're likely in ARRAYS at those offsets,
            # differentiated by slot_index
            known_fields = [
                ('Start',          416, 188743715, 'date32', 4),
                ('EarlyStart',     416, 188743725, 'date32', 4),
                ('LateStart',      416, 188743727, 'date32', 4),
                ('ConstraintType', 1840, 188743697, 'i16', 3),
                ('Duration',       1856, 188743709, 'i32', 6),
                ('FreeSlack',      1856, 188743701, 'i32', 6),
                ('StartSlack',     1856, 188743733, 'i32', 6),
                ('FinishSlack',    1856, 188743734, 'i32', 6),
                ('Created',        1988, 188743773, 'date32', 4),
                ('Priority',       2320, 188743705, 'i16', 2),
            ]

            print(f"\n  Fields at known byte_offsets:")
            print(f"  {'Name':<20s} {'byte_off':>8s} {'slot':>4s} {'mem_val':>12s} {'COM value':<30s}")
            print(f"  {'-'*80}")
            for name, off, fid, typ, slot in known_fields:
                if off + 8 <= len(chunk):
                    if typ == 'i16':
                        val = struct.unpack_from('<h', chunk, off)[0]
                    elif typ == 'i32':
                        val = struct.unpack_from('<i', chunk, off)[0]
                    else:
                        val = struct.unpack_from('<I', chunk, off)[0]
                    try:
                        com_val = str(task.GetField(fid))
                    except:
                        com_val = "ERROR"
                    print(f"  {name:<20s} {off:8d}  {slot:4d}  {val:12d}  COM='{com_val}'")

            # Hex dump: first 64 bytes (vtable + header area)
            print(f"\n  Record header (first 64 bytes):")
            for j in range(0, 64, 8):
                c = chunk[j:j+8]
                hex_str = ' '.join(f'{b:02x}' for b in c)
                u64 = struct.unpack_from('<Q', c, 0)[0]
                rva = u64 - IMAGE_BASE if IMAGE_BASE < u64 < IMAGE_BASE + 0x2000000 else 0
                rva_str = f" (RVA={rva:#x})" if rva else ""
                print(f"    +{j:04x}: {hex_str:<24s} u64={u64:#018x}{rva_str}")

            # Hex dump around key field areas
            for label, off in [("Start area (@416)", 400), ("Duration area (@1856)", 1840),
                               ("Created (@1988)", 1976), ("Priority (@2320)", 2304)]:
                print(f"\n  {label}:")
                for j in range(off, min(len(chunk), off + 64), 8):
                    c = chunk[j:j+8]
                    hex_str = ' '.join(f'{b:02x}' for b in c)
                    i32_0 = struct.unpack_from('<i', c, 0)[0] if len(c) >= 4 else 0
                    i32_1 = struct.unpack_from('<i', c, 4)[0] if len(c) >= 8 else 0
                    marker = ""
                    if j == 416: marker = " << Start"
                    elif j == 1856: marker = " << Duration"
                    elif j == 1988 or j == 1984: marker = " << Created"
                    elif j == 2320: marker = " << Priority"
                    elif j == 1840: marker = " << ConstraintType"
                    print(f"    +{j:04x}: {hex_str:<24s} i32=[{i32_0:10d}, {i32_1:10d}]{marker}")

    # Restore Priority
    task.SetField(188743705, "500")
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
