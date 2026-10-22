#!/usr/bin/env python3
"""Find assignment record by searching for known double values."""

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

class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD),
        ('th32ProcessID', wt.DWORD), ('GlblcntUsage', wt.DWORD),
        ('ProccntUsage', wt.DWORD),
        ('modBaseAddr', ctypes.POINTER(ctypes.c_byte)),
        ('modBaseSize', wt.DWORD), ('hModule', wt.HMODULE),
        ('szModule', ctypes.c_char * 256),
        ('szExePath', ctypes.c_char * 260),
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

def get_base(pid):
    snap = k32.CreateToolhelp32Snapshot(0x8 | 0x10, pid)
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(me)
    base = None
    if k32.Module32First(snap, ctypes.byref(me)):
        while True:
            if b'WINPROJ' in me.szModule.upper():
                base = ctypes.cast(me.modBaseAddr, ctypes.c_void_p).value
                break
            if not k32.Module32Next(snap, ctypes.byref(me)):
                break
    k32.CloseHandle(snap)
    return base

def rpm(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]

EPOCH = datetime(1984, 1, 1)
def decode_date(packed):
    if packed == 0xFFFFFFFF:
        return "NA"
    if packed == 0:
        return "0"
    d = packed >> 16
    m = (packed & 0xFFFF) // 10
    if d < 1 or d > 50000 or m >= 1440:
        return f"inv(0x{packed:08x})"
    dt = EPOCH + timedelta(days=d - 1)
    return f"{dt.strftime('%Y-%m-%d')} {m // 60:02d}:{m % 60:02d}"

def main():
    pids = find_pid()
    if not pids:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    pid = pids[0]
    mod_base = get_base(pid)
    handle = k32.OpenProcess(0x0010 | 0x0400, False, pid)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    # Get assignment values
    t1 = proj.Tasks.Item(1)
    a = t1.Assignments.Item(1)

    work_val = a.Work  # 2400.0 minutes
    cost_val = a.Cost  # 4000.0
    units_val = a.Units  # 1.0

    # The Work double in task flat buffer is stored as min×1000 = 2400000.0
    # But assignment might use different encoding. Let's search for multiple possibilities.
    search_doubles = {
        'Work×1000': work_val * 1000,     # 2400000.0
        'Work_raw': work_val,              # 2400.0
        'Cost×100': cost_val * 100,        # 400000.0
        'Cost_raw': cost_val,              # 4000.0
        'Units': units_val,                # 1.0
    }

    print(f"Search targets:")
    for name, val in search_doubles.items():
        packed = struct.pack('<d', val)
        print(f"  {name} = {val} -> bytes: {packed.hex()}")

    # Work×1000 is the most distinctive value (2400000.0)
    target = struct.pack('<d', work_val * 1000)  # 2400000.0

    # Search heap area
    task_prio = 0x23b7ef6dd3c
    search_start = task_prio - 0x200000
    search_end = task_prio + 0x200000
    CHUNK = 0x40000

    print(f"\nSearching for Work×1000={work_val*1000} in range "
          f"0x{search_start:x} to 0x{search_end:x}...")

    found = []
    for base_addr in range(search_start, search_end, CHUNK):
        data = rpm(handle, base_addr, CHUNK)
        if not data:
            continue
        for off in range(0, len(data) - 8, 8):
            if data[off:off+8] == target:
                addr = base_addr + off
                found.append(addr)

    print(f"Found {len(found)} locations with Work×1000={work_val*1000}")

    for addr in found[:10]:
        # The formula says Work is at byte 64 in the assignment record
        # So the record base should be at addr - 64
        rec_base = addr - 64
        data = rpm(handle, rec_base, 256)
        if not data:
            continue

        # Check TaskUniqueID at byte 0
        tuid = struct.unpack_from('<I', data, 0)[0]
        ruid = struct.unpack_from('<I', data, 4)[0]
        units = struct.unpack_from('<d', data, 56)[0]
        work = struct.unpack_from('<d', data, 64)[0]
        actual_work = struct.unpack_from('<d', data, 72)[0]
        reg_work = struct.unpack_from('<d', data, 80)[0]
        rem_work = struct.unpack_from('<d', data, 88)[0]
        start = struct.unpack_from('<I', data, 104)[0]
        finish = struct.unpack_from('<I', data, 108)[0]
        cost = struct.unpack_from('<d', data, 128)[0]
        actual_cost = struct.unpack_from('<d', data, 136)[0]
        rem_cost = struct.unpack_from('<d', data, 144)[0]
        crt = struct.unpack_from('<H', data, 152)[0]

        score = 0
        checks = 0

        def check(name, mem_val, expect, tol=0.01):
            nonlocal score, checks
            checks += 1
            ok = abs(mem_val - expect) < tol if isinstance(expect, float) else mem_val == expect
            if ok:
                score += 1
            return "OK" if ok else "MISMATCH"

        results = []
        results.append(f"TaskUID={tuid} {check('tuid', tuid, a.TaskUniqueID)}")
        results.append(f"ResUID={ruid} {check('ruid', ruid, a.ResourceUniqueID)}")
        results.append(f"Units={units} {check('units', units, units_val)}")
        results.append(f"Work={work} {check('work', work, work_val*1000)}")
        results.append(f"ActualWork={actual_work} {check('aw', actual_work, 0.0)}")
        results.append(f"RegWork={reg_work} {check('rw', reg_work, 2400000.0)}")
        results.append(f"RemWork={rem_work} {check('rmw', rem_work, 2400000.0)}")
        results.append(f"Start={decode_date(start)} {check('st', start, 0x3bf912c0)}")
        results.append(f"Finish={decode_date(finish)} {check('fi', finish, 0x3bfd27d8)}")
        results.append(f"Cost={cost} {check('cost', cost, cost_val*100)}")
        results.append(f"ActualCost={actual_cost} {check('ac', actual_cost, 0.0)}")
        results.append(f"RemCost={rem_cost} {check('rc', rem_cost, cost_val*100)}")
        results.append(f"CRT={crt} {check('crt', crt, 0)}")

        if score >= 5:
            print(f"\n  *** STRONG MATCH at 0x{rec_base:012x} ({score}/{checks}) ***")
            for r in results:
                print(f"    {r}")

            # Hex dump of first 160 bytes
            print(f"\n  Hex dump (first 160 bytes):")
            for i in range(0, 160, 16):
                hex_str = ' '.join(f'{data[i+j]:02x}' for j in range(min(16, 160-i)))
                print(f"    +{i:03d}: {hex_str}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
