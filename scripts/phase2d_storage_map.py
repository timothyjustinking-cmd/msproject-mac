#!/usr/bin/env python3
"""Map the storage structures around known field addresses.

Known addresses (from full-memory diff):
  Priority (i16=500): 0x23b7ef6dd3c  (flat buffer)
  PercentComplete (i16=420=42*10): 0x23b02d0047d
  Duration (i32=2400): 0x23b75edc834 + 0x740 = 0x23b75edcf74  (getter record)

Examine surrounding memory to understand structure type.
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


def dump_around(handle, addr, label, IMAGE_BASE, before=0x100, after=0x100):
    """Dump memory around an address and try to identify the structure."""
    print(f"\n{'='*80}")
    print(f"STRUCTURE ANALYSIS: {label}")
    print(f"Address: {addr:#x}")
    print(f"{'='*80}")

    start = addr - before
    data = read_mem(handle, start, before + after)
    if not data:
        print("  FAILED to read memory")
        return

    # Look for vtable pointers (should be at object start)
    print(f"\n  Searching for vtable ptrs in [{start:#x}, {start + len(data):#x}]:")
    for off in range(0, len(data) - 8, 8):
        val = struct.unpack_from('<Q', data, off)[0]
        if IMAGE_BASE < val < IMAGE_BASE + 0x2000000:
            rva = val - IMAGE_BASE
            abs_addr = start + off
            marker = " <== FIELD IS HERE" if abs_addr <= addr < abs_addr + 8 else ""
            print(f"    {abs_addr:#x} (+{off:#x}): ptr -> RVA {rva:#x}{marker}")

    # Look for field_ids (0x0b40xxxx range)
    print(f"\n  Field IDs (0x0b40xxxx) in range:")
    for off in range(0, len(data) - 4, 4):
        val = struct.unpack_from('<I', data, off)[0]
        if 0x0b400000 <= val <= 0x0b41ffff:
            abs_addr = start + off
            fid = val
            marker = " <== NEAR FIELD" if abs(abs_addr - addr) < 16 else ""
            print(f"    {abs_addr:#x} (+{off:#x}): field_id={fid:#x} ({fid}){marker}")

    # Check for TASKNODE header pattern: [size:u32, count:u32] + N*8 index
    # The index entries are [field_id:u32, data_offset:u32]
    print(f"\n  TASKNODE header check (looking for [size, count] + field_id index):")
    for search_off in range(0, min(before, len(data) - 16), 4):
        size_val = struct.unpack_from('<I', data, search_off)[0]
        count_val = struct.unpack_from('<I', data, search_off + 4)[0]
        if 100 < size_val < 0x10000 and 1 <= count_val <= 500:
            # Check if the next entries look like [field_id, offset] pairs
            valid_pairs = 0
            for j in range(count_val):
                pair_off = search_off + 8 + j * 8
                if pair_off + 8 > len(data):
                    break
                fid = struct.unpack_from('<I', data, pair_off)[0]
                doff = struct.unpack_from('<I', data, pair_off + 4)[0]
                if 0x0b400000 <= fid <= 0x0b41ffff and doff < size_val:
                    valid_pairs += 1
            if valid_pairs >= 2:
                abs_addr = start + search_off
                print(f"    TASKNODE at {abs_addr:#x}: size={size_val}, count={count_val}, valid_pairs={valid_pairs}")
                # Dump first few pairs
                for j in range(min(count_val, 10)):
                    pair_off = search_off + 8 + j * 8
                    if pair_off + 8 > len(data):
                        break
                    fid = struct.unpack_from('<I', data, pair_off)[0]
                    doff = struct.unpack_from('<I', data, pair_off + 4)[0]
                    print(f"      [{j}] field_id={fid:#x} ({fid}), data_off={doff}")

    # Hex dump of the area around the field address
    print(f"\n  Hex dump around field address ({addr:#x}):")
    field_off_in_data = addr - start
    dump_start = max(0, field_off_in_data - 64)
    dump_end = min(len(data), field_off_in_data + 64)
    for j in range(dump_start, dump_end, 16):
        chunk = data[j:j+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        abs_addr = start + j
        marker = ""
        if abs_addr <= addr < abs_addr + 16:
            marker = "  <== FIELD"
        print(f"    {abs_addr:#x}: {hex_str}{marker}")


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
    print(f"  Priority={task.Priority}, Duration={task.Duration}, PercentComplete={task.PercentComplete}")

    # The three known field addresses
    known = [
        (0x23b7ef6dd3c, "Priority (i16=500, flat buffer)"),
        (0x23b02d0047d, "PercentComplete (i16=420=42*10)"),
        (0x23b75edcf74, "Duration (i32=2400 at getter_record+0x740)"),
    ]

    # Reset PercentComplete to ensure 420 is stored
    if task.PercentComplete != 42:
        task.PercentComplete = 42
        time.sleep(0.3)

    for addr, label in known:
        dump_around(handle, addr, label, IMAGE_BASE, before=0x200, after=0x100)

    # Also verify the values at the addresses
    print(f"\n{'='*80}")
    print(f"VALUE VERIFICATION")
    print(f"{'='*80}")

    # Priority
    data = read_mem(handle, 0x23b7ef6dd3c, 2)
    if data:
        val = struct.unpack_from('<h', data, 0)[0]
        print(f"  Priority at {0x23b7ef6dd3c:#x}: i16={val} (expected 500)")

    # PercentComplete
    data = read_mem(handle, 0x23b02d0047d, 2)
    if data:
        val = struct.unpack_from('<h', data, 0)[0]
        print(f"  PercentComplete at {0x23b02d0047d:#x}: i16={val} (expected 420=42*10)")

    # Duration
    data = read_mem(handle, 0x23b75edcf74, 4)
    if data:
        val = struct.unpack_from('<i', data, 0)[0]
        print(f"  Duration at {0x23b75edcf74:#x}: i32={val} (expected 2400)")

    # Now do a clean Duration test: 5 days -> 10 days
    print(f"\n{'='*80}")
    print(f"DURATION TARGETED TEST: 5d -> 10d")
    print(f"{'='*80}")

    # Read before
    record_base = 0x23b75edc834
    before_data = read_mem(handle, record_base + 0x730, 0x40)
    print(f"  Before (record+0x730 to +0x770):")
    for j in range(0, min(0x40, len(before_data) if before_data else 0), 8):
        chunk = before_data[j:j+8]
        vals = struct.unpack_from('<II', chunk, 0)
        print(f"    +{0x730+j:#06x}: {' '.join(f'{b:02x}' for b in chunk)}  u32=[{vals[0]:#x}, {vals[1]:#x}]")

    # Set to 10 days = 4800 minutes
    task.Duration = 4800
    time.sleep(0.5)
    print(f"  After Duration={task.Duration}")

    after_data = read_mem(handle, record_base + 0x730, 0x40)
    print(f"  After (record+0x730 to +0x770):")
    for j in range(0, min(0x40, len(after_data) if after_data else 0), 8):
        chunk = after_data[j:j+8]
        vals = struct.unpack_from('<II', chunk, 0)
        print(f"    +{0x730+j:#06x}: {' '.join(f'{b:02x}' for b in chunk)}  u32=[{vals[0]:#x}, {vals[1]:#x}]")

    # Check what changed
    if before_data and after_data:
        for i in range(min(len(before_data), len(after_data))):
            if before_data[i] != after_data[i]:
                print(f"  CHANGED at record+{0x730+i:#x}: {before_data[i]:#04x} -> {after_data[i]:#04x}")

    # Reset
    task.Duration = 2400
    time.sleep(0.3)

    # Now reset PercentComplete
    task.PercentComplete = 0
    time.sleep(0.2)

    # ==========================================
    # EXPLORE: What's in the PercentComplete region?
    # Look for TASKNODE structure (sparse block)
    # ==========================================
    print(f"\n{'='*80}")
    print(f"EXPLORING PercentComplete region for TASKNODE structure")
    print(f"{'='*80}")

    pc_addr = 0x23b02d0047d
    # Search backwards for a TASKNODE header
    big_data = read_mem(handle, pc_addr - 0x1000, 0x2000)
    if big_data:
        print(f"  Read 8KB around {pc_addr:#x}")
        # Look for [size:u32, count:u32] headers
        for off in range(0, len(big_data) - 16, 4):
            size_val = struct.unpack_from('<I', big_data, off)[0]
            count_val = struct.unpack_from('<I', big_data, off + 4)[0]
            if 100 < size_val < 0x10000 and 1 <= count_val <= 500:
                valid = 0
                for j in range(min(count_val, 50)):
                    pair_off = off + 8 + j * 8
                    if pair_off + 8 > len(big_data): break
                    fid = struct.unpack_from('<I', big_data, pair_off)[0]
                    doff = struct.unpack_from('<I', big_data, pair_off + 4)[0]
                    if 0x0b400000 <= fid <= 0x0b41ffff and doff < size_val:
                        valid += 1
                if valid >= 3:
                    header_addr = pc_addr - 0x1000 + off
                    print(f"\n  TASKNODE FOUND at {header_addr:#x}: size={size_val}, count={count_val}, valid_pairs={valid}")
                    dist = pc_addr - header_addr
                    print(f"    Distance from PercentComplete: {dist} bytes ({dist:#x})")

                    # Dump the index
                    for j in range(min(count_val, 30)):
                        pair_off = off + 8 + j * 8
                        if pair_off + 8 > len(big_data): break
                        fid = struct.unpack_from('<I', big_data, pair_off)[0]
                        doff = struct.unpack_from('<I', big_data, pair_off + 4)[0]
                        # Calculate absolute data address
                        data_addr = header_addr + 8 + count_val * 8 + doff
                        marker = " <== PercentComplete?" if abs(data_addr - pc_addr) < 8 else ""
                        print(f"      [{j:2d}] fid={fid:#010x} ({fid}) off={doff:4d} -> data@{data_addr:#x}{marker}")

    # ==========================================
    # EXPLORE: What's in the Priority region?
    # ==========================================
    print(f"\n{'='*80}")
    print(f"EXPLORING Priority region structure")
    print(f"{'='*80}")

    prio_addr = 0x23b7ef6dd3c
    big_data = read_mem(handle, prio_addr - 0x1000, 0x2000)
    if big_data:
        print(f"  Read 8KB around {prio_addr:#x}")
        # Look for the direct-storage 2D block pattern: 40-byte entries with field_ids
        # Pattern: [field_id:u32][val_a][size_a:u32][val_b][size_b:u32]
        for off in range(0, len(big_data) - 40, 4):
            fid = struct.unpack_from('<I', big_data, off)[0]
            if fid == 188743705:  # Priority field_id
                abs_addr = prio_addr - 0x1000 + off
                print(f"\n  Priority field_id FOUND at {abs_addr:#x} (offset {off} in buffer)")
                # Dump the 40-byte entry and surrounding entries
                entry_start = off
                for e in range(-3, 5):
                    e_off = entry_start + e * 40
                    if 0 <= e_off < len(big_data) - 40:
                        entry = big_data[e_off:e_off+40]
                        efid = struct.unpack_from('<I', entry, 0)[0]
                        hex_str = ' '.join(f'{b:02x}' for b in entry)
                        e_addr = prio_addr - 0x1000 + e_off
                        marker = " <== PRIORITY" if e == 0 else ""
                        is_fid = " (FIELD)" if 0x0b400000 <= efid <= 0x0b41ffff else ""
                        print(f"    {e_addr:#x}: [{hex_str}] fid={efid:#x}{is_fid}{marker}")

        # Also look for TASKNODE-style index
        for off in range(0, len(big_data) - 16, 4):
            size_val = struct.unpack_from('<I', big_data, off)[0]
            count_val = struct.unpack_from('<I', big_data, off + 4)[0]
            if 100 < size_val < 0x10000 and 1 <= count_val <= 500:
                valid = 0
                for j in range(min(count_val, 50)):
                    pair_off = off + 8 + j * 8
                    if pair_off + 8 > len(big_data): break
                    fid = struct.unpack_from('<I', big_data, pair_off)[0]
                    doff = struct.unpack_from('<I', big_data, pair_off + 4)[0]
                    if 0x0b400000 <= fid <= 0x0b41ffff and doff < size_val:
                        valid += 1
                if valid >= 3:
                    header_addr = prio_addr - 0x1000 + off
                    dist = prio_addr - header_addr
                    print(f"\n  TASKNODE at {header_addr:#x}: size={size_val}, count={count_val}, dist from Priority={dist:#x}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
