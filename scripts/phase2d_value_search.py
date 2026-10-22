#!/usr/bin/env python3
"""Fast value search: set a distinctive field value, then search all memory for it.

Much faster than full-memory diff since we only read each region once.
"""

import ctypes
import struct
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

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

def scan_readable_regions(handle):
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


def search_pattern(handle, regions, pattern):
    """Search all readable memory for a byte pattern. Returns list of addresses."""
    hits = []
    for base, size in regions:
        data = read_mem(handle, base, size)
        if not data:
            continue
        pos = 0
        while True:
            idx = data.find(pattern, pos)
            if idx == -1:
                break
            hits.append(base + idx)
            pos = idx + 1
    return hits


def search_and_verify(handle, regions, task, field_name, set_func, reset_func,
                       value1, value1_bytes_list, value2, value2_bytes_list):
    """
    1. Set field to value1 (distinctive)
    2. Search for value1's byte patterns
    3. Set field to value2 (different distinctive)
    4. Check which value1 locations now have value2
    """
    print(f"\n{'='*80}")
    print(f"PROBING: {field_name}")
    print(f"{'='*80}")

    # Set to value1
    set_func(task, value1)
    time.sleep(0.3)

    # Search for all value1 patterns
    all_v1_hits = {}
    for label, pat in value1_bytes_list:
        hits = search_pattern(handle, regions, pat)
        all_v1_hits[label] = hits
        print(f"  After set to {value1}: {label} found at {len(hits)} locations")
        for h in hits[:5]:
            print(f"    {h:#x}")

    # Set to value2
    set_func(task, value2)
    time.sleep(0.3)

    # Check which value1 locations now have value2
    print(f"\n  Checking which locations changed to {value2}:")
    for (v1_label, v1_pat), (v2_label, v2_pat) in zip(value1_bytes_list, value2_bytes_list):
        v1_hits = all_v1_hits.get(v1_label, [])
        vlen = len(v2_pat)
        confirmed = []
        for addr in v1_hits:
            data = read_mem(handle, addr, vlen)
            if data == v2_pat:
                confirmed.append(addr)
        if confirmed:
            print(f"  CONFIRMED {v1_label}->{v2_label} at {len(confirmed)} locations:")
            for c in confirmed:
                # Read context
                ctx = read_mem(handle, c - 16, 48)
                hex_str = ' '.join(f'{b:02x}' for b in ctx) if ctx else "?"
                print(f"    {c:#x}: {hex_str}")

    # Reset
    reset_func(task)
    time.sleep(0.2)


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

    # Reset to clean state
    task.Duration = 2400
    task.Priority = 500
    task.PercentComplete = 0
    task.Marked = False
    task.Milestone = False
    task.FixedCost = 0.0
    task.HideBar = False
    time.sleep(0.5)

    regions = scan_readable_regions(handle)
    total_mb = sum(s for _, s in regions) / 1024 / 1024
    print(f"\n{len(regions)} readable regions, {total_mb:.0f} MB total")

    # PROBE 1: FixedCost — should be double or double*100
    # Use very distinctive value: $77777.77
    search_and_verify(handle, regions, task, "FixedCost",
        lambda t, v: setattr(t, 'FixedCost', v),
        lambda t: setattr(t, 'FixedCost', 0.0),
        77777.77,
        [
            ("dbl=77777.77",     struct.pack('<d', 77777.77)),
            ("dbl=7777777.0",    struct.pack('<d', 7777777.0)),  # *100
        ],
        33333.33,
        [
            ("dbl=33333.33",     struct.pack('<d', 33333.33)),
            ("dbl=3333333.0",    struct.pack('<d', 3333333.0)),  # *100
        ],
    )

    # PROBE 2: Duration — minutes
    # Use distinctive: 3333 minutes
    search_and_verify(handle, regions, task, "Duration",
        lambda t, v: setattr(t, 'Duration', v),
        lambda t: setattr(t, 'Duration', 2400),
        3333,
        [
            ("i32=3333",  struct.pack('<i', 3333)),
            ("i16=3333",  struct.pack('<h', 3333)),
        ],
        4444,
        [
            ("i32=4444",  struct.pack('<i', 4444)),
            ("i16=4444",  struct.pack('<h', 4444)),
        ],
    )

    # PROBE 3: Marked — boolean
    # Can't use distinctive values (only True/False)
    # Instead: search for field_id 188743751 (0x0B400557) near the storage
    print(f"\n{'='*80}")
    print(f"PROBING: Marked (boolean) via TASKNODE field_id search")
    print(f"{'='*80}")
    # Look for the field_id in all memory (TASKNODE index should have it)
    fid_bytes = struct.pack('<I', 188743751)  # Marked field_id
    fid_hits = search_pattern(handle, regions, fid_bytes)
    print(f"  Field ID 188743751 (Marked) found at {len(fid_hits)} locations:")
    for h in fid_hits[:20]:
        # Check if this is a [field_id, offset] pair in a TASKNODE index
        pair_data = read_mem(handle, h, 8)
        if pair_data:
            fid, off = struct.unpack_from('<II', pair_data, 0)
            # Also read backwards to see if there's a TASKNODE header
            print(f"    {h:#x}: fid={fid:#x}, off_or_next={off}")

    # PROBE 4: Milestone — use differential with known TASKNODE blocks
    # Search for Milestone field_id
    print(f"\n{'='*80}")
    print(f"PROBING: Milestone via field_id search")
    print(f"{'='*80}")
    fid_bytes = struct.pack('<I', 188743704)  # Milestone
    fid_hits = search_pattern(handle, regions, fid_bytes)
    print(f"  Field ID 188743704 (Milestone) found at {len(fid_hits)} locations:")
    for h in fid_hits[:20]:
        pair_data = read_mem(handle, h, 8)
        if pair_data:
            fid, off = struct.unpack_from('<II', pair_data, 0)
            print(f"    {h:#x}: fid={fid:#x}, off_or_next={off}")

    # PROBE 5: Look for TASKNODE headers near the Priority flat buffer
    print(f"\n{'='*80}")
    print(f"SEARCHING: TASKNODE headers near Priority ({0x23b7ef6dd3c:#x})")
    print(f"{'='*80}")
    prio_addr = 0x23b7ef6dd3c
    # Read 64KB before Priority
    big_data = read_mem(handle, prio_addr - 0x10000, 0x20000)
    if big_data:
        for off in range(0, len(big_data) - 16, 4):
            size_val = struct.unpack_from('<I', big_data, off)[0]
            count_val = struct.unpack_from('<I', big_data, off + 4)[0]
            if 100 < size_val < 0x20000 and 3 <= count_val <= 500:
                valid = 0
                for j in range(min(count_val, 50)):
                    pair_off = off + 8 + j * 8
                    if pair_off + 8 > len(big_data): break
                    fid = struct.unpack_from('<I', big_data, pair_off)[0]
                    doff = struct.unpack_from('<I', big_data, pair_off + 4)[0]
                    if 0x0b400000 <= fid <= 0x0b41ffff and doff < size_val:
                        valid += 1
                if valid >= 5:
                    header_addr = prio_addr - 0x10000 + off
                    dist = prio_addr - header_addr
                    print(f"  TASKNODE at {header_addr:#x}: size={size_val}, count={count_val}, valid={valid}, dist_to_prio={dist:#x}")
                    # Show a few entries
                    for j in range(min(count_val, 5)):
                        pair_off = off + 8 + j * 8
                        if pair_off + 8 > len(big_data): break
                        fid = struct.unpack_from('<I', big_data, pair_off)[0]
                        doff = struct.unpack_from('<I', big_data, pair_off + 4)[0]
                        print(f"    [{j}] fid={fid:#x} ({fid}), data_off={doff}")

    # PROBE 6: Look for direct-storage 2D block entries with FixedCost field_id
    print(f"\n{'='*80}")
    print(f"SEARCHING: Direct-storage 2D block for FixedCost")
    print(f"{'='*80}")
    # FixedCost field_id = 188743688 = 0x0B400008
    fc_fid_bytes = struct.pack('<I', 188743688)
    fc_hits = search_pattern(handle, regions, fc_fid_bytes)
    print(f"  FixedCost field_id hits: {len(fc_hits)}")
    for h in fc_hits[:10]:
        # Read 40 bytes starting at this address (direct-storage entry)
        entry = read_mem(handle, h, 40)
        if entry:
            hex_str = ' '.join(f'{b:02x}' for b in entry)
            fid = struct.unpack_from('<I', entry, 0)[0]
            print(f"    {h:#x}: [{hex_str}]")
            # Check if next 4 bytes at +4 could be a value
            if len(entry) >= 16:
                v_dbl = struct.unpack_from('<d', entry, 4)[0]
                v_i32 = struct.unpack_from('<i', entry, 4)[0]
                print(f"      +4: dbl={v_dbl:.2f}, i32={v_i32}")

    # PROBE 7: HideBar field_id search
    print(f"\n{'='*80}")
    print(f"SEARCHING: HideBar field_id")
    print(f"{'='*80}")
    hb_fid_bytes = struct.pack('<I', 188743789)  # HideBar
    hb_hits = search_pattern(handle, regions, hb_fid_bytes)
    print(f"  HideBar field_id {188743789:#x} hits: {len(hb_hits)}")
    for h in hb_hits[:10]:
        entry = read_mem(handle, h, 16)
        if entry:
            hex_str = ' '.join(f'{b:02x}' for b in entry)
            print(f"    {h:#x}: [{hex_str}]")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
