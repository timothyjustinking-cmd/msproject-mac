#!/usr/bin/env python3
"""Phase 2d: Decode the flat data block containing Priority/PercentComplete.

Priority=777 at 0x23b7ef6dd3c, PercentComplete=42 at +2.
This is a flat buffer. Decode the surrounding area to map all class 1 fields.
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

    # Set MULTIPLE distinctive probe values
    task.SetField(188743705, "777")     # Priority (i16)
    task.SetField(188743712, "42%")     # PercentComplete (i16)
    task.SetField(188743751, "Yes")     # Marked (bool/i16)
    task.SetField(188743688, "$12345")  # FixedCost (i64)
    time.sleep(0.5)

    # Differential scan to locate Priority
    regions = scan_all_regions(handle)
    task.SetField(188743705, "987")
    time.sleep(0.5)
    hits_a = set(find_all_pattern(handle, regions, struct.pack('<h', 987)))
    task.SetField(188743705, "123")
    time.sleep(0.5)
    hits_b = set(find_all_pattern(handle, regions, struct.pack('<h', 123)))
    task.SetField(188743705, "777")
    time.sleep(0.3)

    prio_addrs = sorted(hits_a & hits_b)
    print(f"Priority differential: {len(prio_addrs)} hits")

    # Filter: look for one where value=777 and NOT in direct-storage block
    real_pa = None
    for pa in prio_addrs:
        ctx = read_mem(handle, pa - 32, 64)
        if not ctx:
            continue
        val = struct.unpack_from('<h', ctx, 32)[0]
        fid_check = struct.unpack_from('<I', ctx, 32 - 24)[0]
        if val == 777 and (fid_check & 0xFFFF0000) != 0x0b400000:
            real_pa = pa
            break

    if not real_pa:
        print("ERROR: No real priority found")
        task.SetField(188743705, "500")
        task.SetField(188743712, "0%")
        task.SetField(188743751, "No")
        task.SetField(188743688, "$0")
        CloseHandle(handle)
        return

    print(f"Real Priority at {real_pa:#x}")

    # Read a BIG chunk: 2KB before, 4KB after priority
    before = 2048
    after = 4096
    scan_start = real_pa - before
    chunk = read_mem(handle, scan_start, before + after)
    prio_off = before

    # FULL hex dump of 512 bytes around priority, annotated
    print(f"\n{'='*100}")
    print(f"HEX DUMP: 512 bytes centered on Priority ({real_pa:#x})")
    print(f"{'='*100}")
    for j in range(max(0, prio_off - 256), min(len(chunk), prio_off + 256), 8):
        c = chunk[j:j+8]
        hex_str = ' '.join(f'{b:02x}' for b in c)
        i16_0 = struct.unpack_from('<h', c, 0)[0] if len(c) >= 2 else 0
        i16_1 = struct.unpack_from('<h', c, 2)[0] if len(c) >= 4 else 0
        i32_0 = struct.unpack_from('<i', c, 0)[0] if len(c) >= 4 else 0
        i32_1 = struct.unpack_from('<i', c, 4)[0] if len(c) >= 8 else 0
        u64 = struct.unpack_from('<Q', c, 0)[0] if len(c) >= 8 else 0
        rel = j - prio_off

        ann = ""
        if i16_0 == 777: ann = " << PRIORITY=777"
        elif i16_0 == 42 and rel >= 0: ann = " << %Complete=42?"
        elif i32_0 == 24000: ann = " << Duration=24000?"
        elif u64 == 1234500: ann = " << FixedCost?"
        elif IMAGE_BASE < u64 < IMAGE_BASE + 0x2000000:
            rva = u64 - IMAGE_BASE
            ann = f" << PTR RVA={rva:#x}"

        print(f"  prio{rel:+5d}: {hex_str}  i16=[{i16_0:6d},{i16_1:6d}] i32=[{i32_0:10d},{i32_1:10d}]{ann}")

    # Now search more patterns
    print(f"\n{'='*100}")
    print(f"SEARCHING for known values in chunk")
    print(f"{'='*100}")

    # Duration = 24000 (i32)
    dur_pat = struct.pack('<i', 24000)
    pos = 0
    while pos < len(chunk) - 4:
        p = chunk.find(dur_pat, pos)
        if p == -1: break
        rel = p - prio_off
        print(f"  Duration=24000 at prio{rel:+d}")
        pos = p + 1

    # FixedCost: $12345 = 1234500 as i64 OR as double
    fc_i64 = struct.pack('<q', 1234500)
    fc_double = struct.pack('<d', 1234500.0)
    pos = 0
    while pos < len(chunk) - 8:
        p = chunk.find(fc_i64, pos)
        if p == -1: break
        print(f"  FixedCost=1234500 (i64) at prio{p - prio_off:+d}")
        pos = p + 1
    pos = 0
    while pos < len(chunk) - 8:
        p = chunk.find(fc_double, pos)
        if p == -1: break
        print(f"  FixedCost=1234500.0 (double) at prio{p - prio_off:+d}")
        pos = p + 1

    # ActualDuration: COM says "2.1 days" -> 2.1 * 4800 = 10080 min
    ad_pat = struct.pack('<i', 10080)
    pos = 0
    while pos < len(chunk) - 4:
        p = chunk.find(ad_pat, pos)
        if p == -1: break
        print(f"  ActualDuration=10080 at prio{p - prio_off:+d}")
        pos = p + 1

    # RemainingDuration: COM says "2.9 days" -> 2.9 * 4800 = 13920 min
    rd_pat = struct.pack('<i', 13920)
    pos = 0
    while pos < len(chunk) - 4:
        p = chunk.find(rd_pat, pos)
        if p == -1: break
        print(f"  RemainingDuration=13920 at prio{p - prio_off:+d}")
        pos = p + 1

    # Check for a large pointer BEFORE our area that might be the object pointing here
    print(f"\n{'='*100}")
    print(f"LOOKING FOR OBJECT POINTER (scanning wider area)")
    print(f"{'='*100}")

    # Read 32KB before the data area
    wide_start = real_pa - 32768
    wide_chunk = read_mem(handle, wide_start, 65536)
    if wide_chunk:
        # Look for 8-byte values that point near our priority address
        data_area_start = real_pa - 512  # approximate start of data area
        data_area_end = real_pa + 2048
        for off in range(0, len(wide_chunk) - 8, 8):
            ptr = struct.unpack_from('<Q', wide_chunk, off)[0]
            if data_area_start <= ptr <= data_area_end:
                addr = wide_start + off
                ptr_rel = ptr - real_pa
                # Check if this looks like an object (vtable at this address)
                obj_ctx = read_mem(handle, addr - 64, 128)
                if obj_ctx:
                    vtable = struct.unpack_from('<Q', obj_ctx, 64)[0]
                    is_vtable = IMAGE_BASE < vtable < IMAGE_BASE + 0x2000000
                    rva = vtable - IMAGE_BASE if is_vtable else 0
                    if is_vtable:
                        print(f"  Object at {addr:#x} has vtable RVA={rva:#x}, "
                              f"member at obj+? points to prio{ptr_rel:+d}")
                        # Check which member offset this pointer is at
                        for m_off in range(0, 128, 8):
                            m_ptr = struct.unpack_from('<Q', obj_ctx, m_off)[0]
                            if data_area_start <= m_ptr <= data_area_end:
                                print(f"    obj+{m_off - 64}: -> {m_ptr:#x} (prio{m_ptr - real_pa:+d})")
                                # If member_ptr + 0x910 == priority address
                                if m_ptr + 0x910 == real_pa:
                                    print(f"      *** obj+{m_off-64} + 0x910 = Priority! ***")
                                if m_ptr + 0x740 == real_pa - (0x910 - 0x740):
                                    print(f"      *** obj+{m_off-64} + 0x740 -> Duration area! ***")
                                if m_ptr + 0x1a0 == real_pa - (0x910 - 0x1a0):
                                    print(f"      *** obj+{m_off-64} + 0x1a0 -> Start area! ***")

    # Cleanup
    task.SetField(188743705, "500")
    task.SetField(188743712, "0%")
    task.SetField(188743751, "No")
    task.SetField(188743688, "$0")
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
