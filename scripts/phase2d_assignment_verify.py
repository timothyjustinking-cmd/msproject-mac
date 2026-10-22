#!/usr/bin/env python3
"""Verify assignment flat buffer layout against COM values.

Read assignment COM properties and search for matching values in memory
to find the assignment record base address, then verify the full layout.
"""

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

def encode_date(dt_val):
    if dt_val is None:
        return 0xFFFFFFFF
    try:
        py_dt = datetime(dt_val.year, dt_val.month, dt_val.day,
                         dt_val.hour, dt_val.minute, dt_val.second)
        d = (py_dt - EPOCH).days + 1
        m = py_dt.hour * 60 + py_dt.minute
        if d >= 1:
            return (d << 16) | (m * 10)
    except:
        pass
    return None

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
    print(f"Module base: 0x{mod_base:x}")

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    # Get all assignments
    print("\n=== Assignments ===\n")
    assignments = []
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if not t:
            continue
        for j in range(1, t.Assignments.Count + 1):
            a = t.Assignments.Item(j)
            assignments.append(a)
            print(f"  Assignment {len(assignments)}: Task '{t.Name}' (UID={a.TaskUniqueID}) "
                  f"<-> Resource '{a.ResourceName}' (UID={a.ResourceUniqueID})")
            print(f"    UniqueID={a.UniqueID}, Units={a.Units}")

            for prop in ['Work', 'ActualWork', 'RegularWork', 'RemainingWork',
                         'Start', 'Finish', 'Cost', 'ActualCost', 'RemainingCost',
                         'Delay', 'LevelingDelay', 'CostRateTable', 'Confirmed',
                         'ResponsePending']:
                try:
                    v = getattr(a, prop)
                    if prop in ('Start', 'Finish'):
                        packed = encode_date(v)
                        print(f"    {prop:20s} = {decode_date(packed)} (0x{packed:08x})")
                    else:
                        print(f"    {prop:20s} = {v}")
                except Exception as e:
                    print(f"    {prop:20s} = ERROR: {e}")

    if not assignments:
        print("  No assignments found!")
        k32.CloseHandle(handle)
        return

    # Search for assignment records in heap memory
    # The assignment flat buffer should contain TaskUniqueID at byte 0 and
    # ResourceUniqueID at byte 4. Search for these u32 pairs.
    a = assignments[0]
    task_uid = a.TaskUniqueID
    res_uid = a.ResourceUniqueID

    print(f"\n=== Searching for assignment record ===")
    print(f"Looking for TaskUniqueID={task_uid} (at byte 0) + ResourceUniqueID={res_uid} (at byte 4)")

    # We know task records are in heap around 0x23b7ef6xxxx range
    # Assignment records might be nearby. Let's search the task record area.
    task_prio_addr = 0x23b7ef6dd3c
    search_start = task_prio_addr - 0x100000
    search_end = task_prio_addr + 0x100000
    CHUNK = 0x20000

    found_addrs = []
    for base_addr in range(search_start, search_end, CHUNK):
        data = rpm(handle, base_addr, CHUNK)
        if not data:
            continue
        for off in range(0, len(data) - 8, 4):
            v1 = struct.unpack_from('<I', data, off)[0]
            v2 = struct.unpack_from('<I', data, off + 4)[0]
            if v1 == task_uid and v2 == res_uid:
                addr = base_addr + off
                found_addrs.append(addr)

    print(f"Found {len(found_addrs)} candidate locations")

    for addr in found_addrs[:5]:
        print(f"\n  --- Candidate at 0x{addr:012x} ---")
        data = rpm(handle, addr, 256)
        if not data:
            continue

        # Read expected fields per the formula layout
        task_uid_mem = struct.unpack_from('<I', data, 0)[0]
        res_uid_mem = struct.unpack_from('<I', data, 4)[0]
        print(f"    byte 0: TaskUniqueID = {task_uid_mem} (expect {task_uid})")
        print(f"    byte 4: ResourceUniqueID = {res_uid_mem} (expect {res_uid})")

        # Check if Units (Work type, 8 bytes double) is at byte 56
        units_dbl = struct.unpack_from('<d', data, 56)[0]
        com_units = a.Units
        print(f"    byte 56: Units double = {units_dbl} (COM Units = {com_units})")

        # Work at byte 64
        work_dbl = struct.unpack_from('<d', data, 64)[0]
        com_work = a.Work
        print(f"    byte 64: Work double = {work_dbl} (COM Work = {com_work} min, "
              f"×1000 = {com_work * 1000})")

        # ActualWork at byte 72
        awork_dbl = struct.unpack_from('<d', data, 72)[0]
        print(f"    byte 72: ActualWork = {awork_dbl}")

        # Start at byte 104
        start_packed = struct.unpack_from('<I', data, 104)[0]
        print(f"    byte 104: Start = 0x{start_packed:08x} ({decode_date(start_packed)})")

        # Finish at byte 108
        finish_packed = struct.unpack_from('<I', data, 108)[0]
        print(f"    byte 108: Finish = 0x{finish_packed:08x} ({decode_date(finish_packed)})")

        # Delay at byte 112
        delay_val = struct.unpack_from('<I', data, 112)[0]
        print(f"    byte 112: Delay = {delay_val}")

        # Cost at byte 128
        cost_dbl = struct.unpack_from('<d', data, 128)[0]
        com_cost = a.Cost
        print(f"    byte 128: Cost = {cost_dbl} (COM Cost = {com_cost}, ×100 = {com_cost * 100})")

        # CostRateTable at byte 152
        crt = struct.unpack_from('<H', data, 152)[0]
        print(f"    byte 152: CostRateTable = {crt}")

        # Check match quality
        matches = 0
        checks = 0
        if units_dbl == com_units:
            matches += 1
        checks += 1
        if work_dbl == com_work * 1000:
            matches += 1
        checks += 1
        if cost_dbl == com_cost * 100:
            matches += 1
        checks += 1
        print(f"    Match score: {matches}/{checks}")

    k32.CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
