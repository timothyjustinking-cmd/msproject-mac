#!/usr/bin/env python3
"""Find and verify resource flat buffer by searching for StandardRate value."""

import ctypes
import ctypes.wintypes as wt
import struct
import sys
from datetime import datetime, timedelta

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ('BaseAddress', ctypes.c_void_p),
        ('AllocationBase', ctypes.c_void_p),
        ('AllocationProtect', wt.DWORD),
        ('RegionSize', ctypes.c_size_t),
        ('State', wt.DWORD),
        ('Protect', wt.DWORD),
        ('Type', wt.DWORD),
    ]

class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD), ('cntUsage', wt.DWORD),
        ('th32ProcessID', wt.DWORD),
        ('th32DefaultHeapID', ctypes.POINTER(ctypes.c_ulong)),
        ('th32ModuleID', wt.DWORD), ('cntThreads', wt.DWORD),
        ('th32ParentProcessID', wt.DWORD),
        ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD),
        ('szExeFile', ctypes.c_char * 260),
    ]

def find_pid():
    snap = k32.CreateToolhelp32Snapshot(0x2, 0)
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(pe)
    pids = []
    if k32.Process32First(snap, ctypes.byref(pe)):
        while True:
            if b'WINPROJ' in pe.szExeFile.upper():
                pids.append(pe.th32ProcessID)
            if not k32.Process32Next(snap, ctypes.byref(pe)):
                break
    k32.CloseHandle(snap)
    return pids

def rpm(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]

EPOCH = datetime(1984, 1, 1)
def decode_date(packed):
    if packed == 0xFFFFFFFF: return "NA"
    if packed == 0: return "0"
    d = packed >> 16
    m = (packed & 0xFFFF) // 10
    if d < 1 or d > 50000 or m >= 1440:
        return f"inv(0x{packed:08x})"
    dt = EPOCH + timedelta(days=d - 1)
    return f"{dt.strftime('%Y-%m-%d')} {m // 60:02d}:{m % 60:02d}"

def encode_date(dt_val):
    try:
        py_dt = datetime(dt_val.year, dt_val.month, dt_val.day,
                         dt_val.hour, dt_val.minute)
        d = (py_dt - EPOCH).days + 1
        m = py_dt.hour * 60 + py_dt.minute
        return (d << 16) | (m * 10)
    except:
        return None

def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    pid = pids[0]
    handle = k32.OpenProcess(0x0010 | 0x0400, False, pid)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    r = proj.Resources.Item(1)
    print(f"Resource: '{r.Name}' (UID={r.UniqueID})")
    print(f"  StandardRate={r.StandardRate}, OvertimeRate={r.OvertimeRate}")
    print(f"  MaxUnits={r.MaxUnits}, Type={r.Type}")
    print(f"  Work={r.Work}, ActualWork={r.ActualWork}")
    print(f"  Cost={r.Cost}, ActualCost={r.ActualCost}")
    print(f"  PeakUnits={r.PeakUnits}")

    # Read the resource SC1 field map
    # From analysis: resource has 44 SC1 fields
    # Key fields:
    # Byte 16: MaxUnits (type 5/Work, 8 bytes)
    # Byte 24: StandardRate (type 9/Cost, 8 bytes)
    # Byte 32: OvertimeRate (type 9/Cost, 8 bytes)
    # Byte 80: Work (type 5/Work, 8 bytes)
    # Byte 152: Cost (type 9/Cost, 8 bytes)

    # StandardRate COM value: "$100/h" → 100.0 per hour
    # Internal encoding for Cost type: ×100
    # But StandardRate might be $/min × some factor

    # Let's figure out the StandardRate encoding
    # $100/hour = $1.6667/minute
    # If stored as ×100: 166.67? Or maybe ×6000 ($ per hour × 100)?
    # Or maybe it's stored as cents/minute × 1000?

    # Let's try multiple encodings
    rate = 100.0  # $100/hour
    encodings = {
        'rate_raw': rate,
        'rate×100': rate * 100,             # 10000 (cents/h)
        'rate/60×100': rate / 60 * 100,     # 166.67 (cents/min)
        'rate×1000': rate * 1000,           # 100000
        'rate/60×1000': rate / 60 * 1000,   # 1666.67
        'rate×10000': rate * 10000,         # 1000000
    }

    for name, val in encodings.items():
        print(f"  {name} = {val} → {struct.pack('<d', val).hex()}")

    # MaxUnits COM=1.0, likely stored as 10000.0 like assignment Units
    max_units_target = struct.pack('<d', 10000.0)
    print(f"\n  MaxUnits target (10000.0): {max_units_target.hex()}")

    # Work COM=2400.0 min → stored as 2400000.0
    work_target = struct.pack('<d', 2400.0 * 1000)
    print(f"  Work target (2400000.0): {work_target.hex()}")

    # Search for Work value across all memory (most distinctive)
    MEM_COMMIT = 0x1000
    addr = 0
    regions = []
    mbi = MEMORY_BASIC_INFORMATION()

    while addr < 0x7FFFFFFFFFFF:
        result = k32.VirtualQueryEx(handle, ctypes.c_void_p(addr),
                                     ctypes.byref(mbi), ctypes.sizeof(mbi))
        if result == 0: break
        base_addr = mbi.BaseAddress or 0
        region_size = mbi.RegionSize or 0
        if mbi.State == MEM_COMMIT and region_size > 0:
            regions.append((base_addr, region_size))
        next_addr = base_addr + region_size
        if next_addr <= addr: break
        addr = next_addr

    # The resource SC1 field map shows MaxUnits at byte 16 and Work at byte 80
    # If we find Work (2400000.0), the record base is at found_addr - 80
    # Then MaxUnits should be at found_addr - 80 + 16 = found_addr - 64

    # But Work=2400000.0 is also in the task and assignment records.
    # We need to find a location where the resource-specific fields check out.

    # Let's search for the MaxUnits=10000.0 double and check nearby for resource-specific structure
    target = max_units_target
    print(f"\nSearching for MaxUnits=10000.0...")

    locs = []
    for base, size in regions:
        if size > 0x10000000: continue
        data = rpm(handle, base, min(size, 0x1000000))
        if not data: continue
        pos = 0
        while pos < len(data) - 8:
            idx = data.find(target, pos)
            if idx < 0: break
            locs.append(base + idx)
            pos = idx + 8

    print(f"Found {len(locs)} locations with 10000.0")

    # For each location, check if byte 16 has MaxUnits=10000.0
    # meaning record base = loc - 16
    # Then check byte 80 for Work=2400000.0 and byte 152 for Cost

    work_bytes = struct.pack('<d', 2400000.0)

    good_hits = []
    for loc in locs:
        rec_base = loc - 16
        data = rpm(handle, rec_base, 280)
        if not data or len(data) < 280: continue

        # Check MaxUnits at byte 16
        max_u = struct.unpack_from('<d', data, 16)[0]
        if abs(max_u - 10000.0) > 0.01: continue

        # Check Work at byte 80
        work_v = struct.unpack_from('<d', data, 80)[0]
        if abs(work_v - 2400000.0) > 0.01: continue

        # Check booleans at bytes 4-8 (should be small values, not pointers)
        b4 = struct.unpack_from('<I', data, 4)[0]
        b8 = struct.unpack_from('<I', data, 8)[0]
        if b4 > 0xFFFF or b8 > 0xFFFF: continue  # skip pointer-like

        # Strong candidate
        cost_v = struct.unpack_from('<d', data, 152)[0]
        std_rate = struct.unpack_from('<d', data, 24)[0]
        ot_rate = struct.unpack_from('<d', data, 32)[0]

        good_hits.append((rec_base, max_u, work_v, cost_v, std_rate, ot_rate))

    print(f"Strong candidates: {len(good_hits)}")

    for rec_base, max_u, work_v, cost_v, std_rate, ot_rate in good_hits[:5]:
        print(f"\n  === Record base 0x{rec_base:012x} ===")
        data = rpm(handle, rec_base, 280)

        # Read ALL fields from the resource SC1 map
        print(f"  Byte   0: (unknown)          = {struct.unpack_from('<I', data, 0)[0]}")
        print(f"  Byte   4: BoolFlags          = {data[4:10].hex()}")
        print(f"  Byte  16: MaxUnits           = {max_u} (COM={r.MaxUnits})")
        print(f"  Byte  24: StandardRate       = {std_rate}")
        print(f"  Byte  32: OvertimeRate       = {ot_rate}")
        print(f"  Byte  40: CostPerUse         = {struct.unpack_from('<d', data, 40)[0]}")
        print(f"  Byte  48: (unknown rate)     = {struct.unpack_from('<d', data, 48)[0]}")
        print(f"  Byte  56: (unknown rate)     = {struct.unpack_from('<d', data, 56)[0]}")

        # CalendarGuid at byte 64 (16 bytes)
        print(f"  Byte  64: CalendarGuid       = {data[64:80].hex()}")

        # Work fields
        print(f"  Byte  80: Work               = {work_v} (COM={r.Work}*1000={r.Work*1000})")
        print(f"  Byte  88: ActualWork         = {struct.unpack_from('<d', data, 88)[0]}")
        print(f"  Byte  96: OvertimeWork       = {struct.unpack_from('<d', data, 96)[0]}")
        print(f"  Byte 104: ActualOTWork       = {struct.unpack_from('<d', data, 104)[0]}")
        print(f"  Byte 112: RegularWork        = {struct.unpack_from('<d', data, 112)[0]}")
        print(f"  Byte 120: RemainingWork      = {struct.unpack_from('<d', data, 120)[0]}")
        print(f"  Byte 128: RemainingOTWork    = {struct.unpack_from('<d', data, 128)[0]}")
        print(f"  Byte 136: (unknown work)     = {struct.unpack_from('<d', data, 136)[0]}")
        print(f"  Byte 144: BudgetWork         = {struct.unpack_from('<d', data, 144)[0]}")

        # Cost fields
        print(f"  Byte 152: Cost               = {cost_v} (COM={r.Cost}*100={r.Cost*100})")
        print(f"  Byte 160: ActualCost         = {struct.unpack_from('<d', data, 160)[0]}")
        print(f"  Byte 168: OvertimeCost       = {struct.unpack_from('<d', data, 168)[0]}")
        print(f"  Byte 176: ActualOTCost       = {struct.unpack_from('<d', data, 176)[0]}")
        print(f"  Byte 184: RemainingCost      = {struct.unpack_from('<d', data, 184)[0]}")
        print(f"  Byte 192: RemainingOTCost    = {struct.unpack_from('<d', data, 192)[0]}")
        print(f"  Byte 200: BudgetCost         = {struct.unpack_from('<d', data, 200)[0]}")

        # Dates
        for doff in [248, 252]:
            dv = struct.unpack_from('<I', data, doff)[0]
            print(f"  Byte {doff}: Date              = 0x{dv:08x} ({decode_date(dv)})")

        # PeakUnits at byte 256
        peak = struct.unpack_from('<d', data, 256)[0]
        print(f"  Byte 256: PeakUnits          = {peak} (COM={r.PeakUnits})")

        # Validate key fields
        score = 0
        total = 0

        def v(name, mem, com, tol=0.01):
            nonlocal score, total
            total += 1
            ok = abs(mem - com) < tol if isinstance(com, float) else mem == com
            if ok: score += 1
            return "OK" if ok else f"MISMATCH"

        print(f"\n  Validation:")
        print(f"    MaxUnits:     {v('mu', max_u, r.MaxUnits * 10000)}")
        print(f"    Work:         {v('w', work_v, r.Work * 1000)}")
        print(f"    Cost:         {v('c', cost_v, r.Cost * 100)}")
        peak_check = v('p', peak, r.PeakUnits * 10000)
        print(f"    PeakUnits:    {peak_check}")

        act_work = struct.unpack_from('<d', data, 88)[0]
        print(f"    ActualWork:   {v('aw', act_work, r.ActualWork * 1000)}")
        act_cost = struct.unpack_from('<d', data, 160)[0]
        print(f"    ActualCost:   {v('ac', act_cost, r.ActualCost * 100)}")
        rem_work = struct.unpack_from('<d', data, 120)[0]
        print(f"    RemainingWork:{v('rw', rem_work, r.RemainingWork * 1000)}")
        rem_cost = struct.unpack_from('<d', data, 184)[0]
        print(f"    RemainingCost:{v('rc', rem_cost, r.RemainingCost * 100)}")

        print(f"    Score: {score}/{total}")

        # Full hex dump
        print(f"\n  Hex dump (0-272):")
        for i in range(0, 272, 16):
            hex_str = ' '.join(f'{data[i+j]:02x}' for j in range(min(16, 272-i)))
            print(f"    +{i:03d}: {hex_str}")

    k32.CloseHandle(handle)
    print("\nDone.")

if __name__ == '__main__':
    main()
