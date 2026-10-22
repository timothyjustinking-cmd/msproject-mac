#!/usr/bin/env python3
"""Find the task record base by searching for the RAW stored value of Duration.

Key discovery: Duration getter reads [record+0x740] and multiplies by 10.
So if COM Duration = 24000 (5 days × 4800), stored value = 24000/10 = 2400 = 0x960.

If we find 2400 in memory, record_base = that_address - 0x740.
Then we can verify: record_base + 0x7c4 should contain the Created date,
record_base + 0x730 should contain ConstraintType raw value,
record_base + 0x74a should contain DurationUnits raw value.
"""

import ctypes
import struct
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
    if not hProcess: return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess); return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
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
        if result == 0: break
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE:
            if 0x1000 <= mbi.RegionSize <= 0x10000000:
                regions.append((mbi.BaseAddress, mbi.RegionSize))
        addr = (mbi.BaseAddress or 0) + mbi.RegionSize
        if addr <= (mbi.BaseAddress or 0): break
    return regions

def find_all_pattern(handle, regions, pattern):
    hits = []
    for base, size in regions:
        data = read_mem(handle, base, size)
        if not data: continue
        offset = 0
        while True:
            pos = data.find(pattern, offset)
            if pos == -1: break
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

    # Read current COM values for reference
    print(f"\nCOM field values:")
    for name, fid in [('Duration', 188743709), ('Created', 188743773),
                       ('Start', 188743715), ('Finish', 188743716),
                       ('ConstraintType', 188743697), ('Priority', 188743705),
                       ('PercentComplete', 188743712), ('FreeSlack', 188743701),
                       ('Milestone', 188743704), ('Cost', 188743685),
                       ('FixedCost', 188743688)]:
        try:
            val = str(task.GetField(fid))
            print(f"  {name:<20s} = '{val}'")
        except:
            pass

    # Step 1: Differential scan on Duration raw value
    # Duration getter multiplies stored value by 10
    # COM Duration should be "5 days" = 24000 ticks
    # So stored = 24000 / 10 = 2400
    # But wait — let me change duration to verify the ×10 factor
    # Use a custom duration approach: set FixedCost to something unique first

    # Actually, use differential: change the task's Duration by changing %Complete
    # Current state: Duration = 24000 (5 days)
    # Stored = 2400 (if ×10 hypothesis is correct)

    regions = scan_all_regions(handle)
    print(f"\n{len(regions)} regions, {sum(s for _,s in regions)/1024/1024:.0f} MB")

    # Search for 2400 (0x960) as i32 — the hypothetical raw Duration
    print(f"\n{'='*80}")
    print(f"Searching for raw Duration = 2400 (0x960)")
    print(f"{'='*80}")

    raw_dur_pat = struct.pack('<i', 2400)
    hits_2400 = find_all_pattern(handle, regions, raw_dur_pat)
    print(f"  {len(hits_2400)} hits for i32=2400")

    # For each hit, check if record_base = hit - 0x740 makes sense
    # At record_base + 0x7c4 there should be a date32 (Created date)
    # At record_base + 0x730 there should be ConstraintType raw (likely 0 for ASAP)
    # At record_base + 0x74a there should be DurationUnits raw value

    # First try differential: change Duration and see which 2400 values change
    # We can't set Duration directly, but we can change PercentComplete which changes ActualDuration
    # Actually, Duration itself is constant at 24000. Let's instead set it differently.

    # Alternative: find Created date (which is unique) at record+0x7c4
    # Created getter reads [RAX+0x7c4] directly (no multiplication)
    # COM Created = a specific date value
    created_val = task.GetField(188743773)
    print(f"\n  Created COM value: '{created_val}'")

    # For each Duration hit at 2400, compute candidate record_base and read record_base+0x7c4
    valid_candidates = []
    for hit_addr in hits_2400:
        record_base = hit_addr - 0x740
        # Read values at key offsets
        test_data = read_mem(handle, record_base + 0x700, 0x300)
        if not test_data:
            continue

        # Duration at offset 0x740 - 0x700 = 0x40
        dur_raw = struct.unpack_from('<i', test_data, 0x40)[0]
        # Created at offset 0x7c4 - 0x700 = 0xC4
        created_raw = struct.unpack_from('<I', test_data, 0xC4)[0]
        # ConstraintType at offset 0x730 - 0x700 = 0x30
        ct_raw = struct.unpack_from('<H', test_data, 0x30)[0]
        # DurationUnits at offset 0x74a - 0x700 = 0x4a
        du_raw = struct.unpack_from('<H', test_data, 0x4A)[0]
        # FreeSlack at 0x740 too (same byte_offset as Duration!)
        # Actually FreeSlack should be at a different class_slot. Same getter.

        # Check validity: Created should be a date32 (0x3d1xxxxx range for 2026 dates)
        is_date = 0x3d100000 <= created_raw <= 0x3d200000

        if is_date:
            # Also read some more fields
            # Read a larger chunk
            big_data = read_mem(handle, record_base, 0xA00)
            if not big_data:
                continue

            # byte_off 0x1a0: Start/EarlyStart dates
            start_raw = struct.unpack_from('<I', big_data, 0x1a0)[0]
            is_start_date = 0x3d100000 <= start_raw <= 0x3d200000

            valid_candidates.append({
                'record_base': record_base,
                'dur_hit': hit_addr,
                'dur_raw': dur_raw,
                'created_raw': created_raw,
                'ct_raw': ct_raw,
                'du_raw': du_raw,
                'start_raw': start_raw,
                'start_is_date': is_start_date,
            })

    print(f"\n  Candidates with valid Created date: {len(valid_candidates)}")
    for c in valid_candidates[:10]:
        print(f"\n  record_base = {c['record_base']:#x} (Duration at {c['dur_hit']:#x})")
        print(f"    +0x740: Duration raw = {c['dur_raw']} (×10 = {c['dur_raw']*10})")
        print(f"    +0x7c4: Created raw  = {c['created_raw']:#x}")
        print(f"    +0x730: ConstraintType raw = {c['ct_raw']}")
        print(f"    +0x74a: DurationUnits raw  = {c['du_raw']}")
        print(f"    +0x1a0: Start raw    = {c['start_raw']:#x} (is_date={c['start_is_date']})")

        if c['start_is_date']:
            print(f"    *** STRONG CANDIDATE: both Created and Start are valid dates! ***")

            # Read and dump key areas of this record
            big_data = read_mem(handle, c['record_base'], 0xA00)
            if big_data:
                print(f"\n    Key byte_offset fields:")
                # All known byte_offsets from the getter analysis
                offsets = [
                    (0x1a0, 'date32', 'Start/EarlyStart'),
                    (0x730, 'i16', 'ConstraintType'),
                    (0x740, 'i32', 'Duration/FreeSlack'),
                    (0x74a, 'i16', 'DurationUnits'),
                    (0x7c4, 'date32', 'Created'),
                    (0x910, 'ptr/vtable', 'byte_off_0x910'),
                ]
                for off, typ, label in offsets:
                    if off + 8 <= len(big_data):
                        val_i32 = struct.unpack_from('<i', big_data, off)[0]
                        val_u32 = struct.unpack_from('<I', big_data, off)[0]
                        val_i16 = struct.unpack_from('<h', big_data, off)[0]
                        val_u64 = struct.unpack_from('<Q', big_data, off)[0]
                        if typ == 'date32':
                            print(f"    +{off:#06x}: {val_u32:#010x} ({label})")
                        elif typ == 'i16':
                            print(f"    +{off:#06x}: {val_i16} ({label})")
                        elif typ == 'i32':
                            print(f"    +{off:#06x}: {val_i32} (×10={val_i32*10}) ({label})")
                        else:
                            print(f"    +{off:#06x}: {val_u64:#018x} ({label})")

                # Also check for a vtable pointer at record_base+0
                vtable_ptr = struct.unpack_from('<Q', big_data, 0)[0]
                if IMAGE_BASE < vtable_ptr < IMAGE_BASE + 0x2000000:
                    vtable_rva = vtable_ptr - IMAGE_BASE
                    print(f"\n    VTABLE at record+0: RVA {vtable_rva:#x}")
                    # Read vtable and check offset 0x910
                    vtable_data = read_mem(handle, vtable_ptr, 0x920)
                    if vtable_data and len(vtable_data) >= 0x918:
                        fn_at_910 = struct.unpack_from('<Q', vtable_data, 0x910)[0]
                        fn_rva = fn_at_910 - IMAGE_BASE if IMAGE_BASE < fn_at_910 < IMAGE_BASE + 0x2000000 else 0
                        print(f"    vtable[0x910]: {fn_at_910:#x} (RVA {fn_rva:#x})")
                else:
                    print(f"\n    record+0: {vtable_ptr:#x} (NOT a vtable)")

                # Hex dump around Start area (0x1a0)
                print(f"\n    Hex around Start (0x1a0):")
                for j in range(0x190, min(0x1D0, len(big_data)), 8):
                    vals = big_data[j:j+8]
                    hex_str = ' '.join(f'{b:02x}' for b in vals)
                    i32_0 = struct.unpack_from('<I', vals, 0)[0]
                    i32_1 = struct.unpack_from('<I', vals, 4)[0] if len(vals) >= 8 else 0
                    print(f"      +{j:#06x}: {hex_str}  u32=[{i32_0:#010x}, {i32_1:#010x}]")

                # Hex dump around Duration area (0x740)
                print(f"\n    Hex around Duration (0x740):")
                for j in range(0x720, min(0x7E0, len(big_data)), 8):
                    vals = big_data[j:j+8]
                    hex_str = ' '.join(f'{b:02x}' for b in vals)
                    i32_0 = struct.unpack_from('<i', vals, 0)[0]
                    i32_1 = struct.unpack_from('<i', vals, 4)[0] if len(vals) >= 8 else 0
                    i16_0 = struct.unpack_from('<h', vals, 0)[0]
                    i16_1 = struct.unpack_from('<h', vals, 2)[0] if len(vals) >= 4 else 0
                    print(f"      +{j:#06x}: {hex_str}  i32=[{i32_0:10d}, {i32_1:10d}]  i16=[{i16_0:6d},{i16_1:6d}]")

                # Around Priority area (0x910)
                print(f"\n    Hex around 0x910:")
                for j in range(0x900, min(0x940, len(big_data)), 8):
                    vals = big_data[j:j+8]
                    hex_str = ' '.join(f'{b:02x}' for b in vals)
                    i16_0 = struct.unpack_from('<h', vals, 0)[0]
                    i16_1 = struct.unpack_from('<h', vals, 2)[0]
                    print(f"      +{j:#06x}: {hex_str}  i16=[{i16_0:6d},{i16_1:6d}]")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
