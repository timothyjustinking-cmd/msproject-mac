#!/usr/bin/env python3
"""Verify assignment flat buffer at discovered address against COM values."""

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
    try:
        py_dt = datetime(dt_val.year, dt_val.month, dt_val.day,
                         dt_val.hour, dt_val.minute)
        d = (py_dt - EPOCH).days + 1
        m = py_dt.hour * 60 + py_dt.minute
        return (d << 16) | (m * 10)
    except:
        return None

def decode_date(packed):
    if packed == 0xFFFFFFFF: return "NA"
    if packed == 0: return "0"
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
    t1 = proj.Tasks.Item(1)
    a = t1.Assignments.Item(1)

    # Known record base from analysis
    rec_base = 0x023b7f67a470

    # Read 256 bytes
    data = rpm(handle, rec_base, 256)
    if not data:
        print("ERROR: Could not read memory"); return

    print(f"Assignment Record Base: 0x{rec_base:012x}")
    print(f"{'='*80}")

    # Verify each SC1 field
    results = []
    total = 0
    matched = 0

    def check(byte_off, name, mem_val, com_val, fmt=""):
        nonlocal total, matched
        total += 1
        if isinstance(com_val, float):
            ok = abs(mem_val - com_val) < 0.01
        else:
            ok = mem_val == com_val
        if ok:
            matched += 1
        status = "OK" if ok else "MISMATCH"
        if fmt:
            print(f"  byte {byte_off:>3d}: {name:20s} = {fmt} ({status})")
        else:
            print(f"  byte {byte_off:>3d}: {name:20s} = mem={mem_val} com={com_val} ({status})")

    # Byte 0: TaskUniqueID
    v = struct.unpack_from('<I', data, 0)[0]
    check(0, 'TaskUniqueID', v, a.TaskUniqueID,
          f"mem={v} com={a.TaskUniqueID}")

    # Byte 4: ResourceUniqueID
    v = struct.unpack_from('<I', data, 4)[0]
    check(4, 'ResourceUniqueID', v, a.ResourceUniqueID,
          f"mem={v} com={a.ResourceUniqueID}")

    # Byte 8-23: TaskGuid (16 bytes)
    guid1 = data[8:24].hex()
    print(f"  byte   8: TaskGuid           = {guid1}")

    # Byte 24-39: ResourceGuid (16 bytes)
    guid2 = data[24:40].hex()
    print(f"  byte  24: ResourceGuid       = {guid2}")

    # Byte 40: type29 (4 bytes)
    v = struct.unpack_from('<I', data, 40)[0]
    print(f"  byte  40: type29             = {v}")

    # Byte 44-49: Boolean flags
    bools = data[44:50]
    print(f"  byte  44: BoolFlags          = {bools.hex()} ({' '.join(f'{b:08b}' for b in bools)})")

    # Check specific booleans
    # Confirmed is at bit offset 1412 within the cache.
    # byte_off_raw=1412, so bit within boolean region:
    # Boolean region starts at byte 44 (byte_off_raw=1408 for first bool)
    # Confirmed at byte_off_raw=1412 → bit 1412-1408=4 within byte 44
    confirmed_bit = (data[44] >> 4) & 1
    print(f"    Confirmed (bit 4 of byte 44) = {confirmed_bit} (COM={a.Confirmed})")

    response_bit = (data[44] >> 5) & 1
    print(f"    ResponsePending (bit 5 of byte 44) = {response_bit} (COM={a.ResponsePending})")

    # Byte 56: Units (double)
    v = struct.unpack_from('<d', data, 56)[0]
    com_units = a.Units
    print(f"  byte  56: Units              = mem={v} com={com_units}")
    print(f"            Ratio: {v/com_units if com_units else 'N/A'} (internal encoding)")

    # Byte 64: Work (double)
    v = struct.unpack_from('<d', data, 64)[0]
    check(64, 'Work', v, a.Work * 1000,
          f"mem={v} com={a.Work}*1000={a.Work*1000}")

    # Byte 72: ActualWork
    v = struct.unpack_from('<d', data, 72)[0]
    check(72, 'ActualWork', v, a.ActualWork * 1000,
          f"mem={v} com={a.ActualWork}*1000={a.ActualWork*1000}")

    # Byte 80: RegularWork
    v = struct.unpack_from('<d', data, 80)[0]
    check(80, 'RegularWork', v, a.RegularWork * 1000,
          f"mem={v} com={a.RegularWork}*1000={a.RegularWork*1000}")

    # Byte 88: RemainingWork
    v = struct.unpack_from('<d', data, 88)[0]
    check(88, 'RemainingWork', v, a.RemainingWork * 1000,
          f"mem={v} com={a.RemainingWork}*1000={a.RemainingWork*1000}")

    # Byte 96: Unknown date
    v = struct.unpack_from('<I', data, 96)[0]
    print(f"  byte  96: UnknownDate1       = 0x{v:08x} ({decode_date(v)})")

    # Byte 100: Unknown date
    v = struct.unpack_from('<I', data, 100)[0]
    print(f"  byte 100: UnknownDate2       = 0x{v:08x} ({decode_date(v)})")

    # Byte 104: Start
    v = struct.unpack_from('<I', data, 104)[0]
    start_packed = encode_date(a.Start)
    check(104, 'Start', v, start_packed,
          f"mem=0x{v:08x}({decode_date(v)}) com=0x{start_packed:08x}")

    # Byte 108: Finish
    v = struct.unpack_from('<I', data, 108)[0]
    finish_packed = encode_date(a.Finish)
    check(108, 'Finish', v, finish_packed,
          f"mem=0x{v:08x}({decode_date(v)}) com=0x{finish_packed:08x}")

    # Byte 112: Delay
    v = struct.unpack_from('<I', data, 112)[0]
    check(112, 'Delay', v, a.Delay,
          f"mem={v} com={a.Delay}")

    # Byte 116: Unknown i16
    v = struct.unpack_from('<H', data, 116)[0]
    print(f"  byte 116: Unknown_i16        = {v}")

    # Byte 120: LevelingDelay
    v = struct.unpack_from('<I', data, 120)[0]
    check(120, 'LevelingDelay', v, a.LevelingDelay,
          f"mem={v} com={a.LevelingDelay}")

    # Byte 128: Cost
    v = struct.unpack_from('<d', data, 128)[0]
    check(128, 'Cost', v, a.Cost * 100,
          f"mem={v} com={a.Cost}*100={a.Cost*100}")

    # Byte 136: ActualCost
    v = struct.unpack_from('<d', data, 136)[0]
    check(136, 'ActualCost', v, a.ActualCost * 100,
          f"mem={v} com={a.ActualCost}*100={a.ActualCost*100}")

    # Byte 144: RemainingCost
    v = struct.unpack_from('<d', data, 144)[0]
    check(144, 'RemainingCost', v, a.RemainingCost * 100,
          f"mem={v} com={a.RemainingCost}*100={a.RemainingCost*100}")

    # Byte 152: CostRateTable
    v = struct.unpack_from('<H', data, 152)[0]
    check(152, 'CostRateTable', v, a.CostRateTable,
          f"mem={v} com={a.CostRateTable}")

    # Byte 154: Unknown i16
    v = struct.unpack_from('<H', data, 154)[0]
    print(f"  byte 154: Unknown_i16_2      = {v}")

    # Check beyond the SC1 map
    print(f"\n  --- Beyond SC1 map ---")
    for off in range(156, 200, 8):
        v = struct.unpack_from('<d', data, off)[0]
        if v != 0:
            print(f"  byte {off:>3d}: double = {v}")
    for off in range(156, 200, 4):
        v = struct.unpack_from('<I', data, off)[0]
        if v != 0 and v < 0x10000:
            print(f"  byte {off:>3d}: u32 = {v} (0x{v:08x})")

    print(f"\n{'='*80}")
    print(f"RESULT: {matched}/{total} SC1 fields validated against COM")

    # Full hex dump
    print(f"\nFull hex dump (0-192):")
    for i in range(0, 192, 16):
        hex_str = ' '.join(f'{data[i+j]:02x}' for j in range(min(16, 192-i)))
        print(f"  +{i:03d}: {hex_str}")

    k32.CloseHandle(handle)
    print("\nDone.")

if __name__ == '__main__':
    main()
