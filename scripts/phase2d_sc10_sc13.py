#!/usr/bin/env python3
"""Probe SC10 (computed) and SC13 (structural) storage.

SC10: 61 fields — computed at read time (Work, Cost, PercentComplete, etc.)
SC13: 207 fields — structural relationships (Predecessors, Successors,
      ResourceNames, OutlineChildren, etc.)

SC10 hypothesis: No persistent storage, calculated from other fields.
SC13 hypothesis: Pointer to linked data structure (list of task IDs or similar).
"""

import ctypes
import ctypes.wintypes
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
VirtualQueryEx = k32.VirtualQueryEx

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.wintypes.DWORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", ctypes.wintypes.DWORD),
        ("Protect", ctypes.wintypes.DWORD),
        ("Type", ctypes.wintypes.DWORD),
    ]

MEM_COMMIT = 0x1000

def get_pid():
    import subprocess
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WINPROJ.EXE', '/FO', 'CSV', '/NH'],
                       capture_output=True, text=True)
    for line in r.stdout.strip().split('\n'):
        if 'WINPROJ' in line:
            return int(line.split(',')[1].strip('"'))
    return None

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]

def search_memory(handle, needle_bytes, max_results=20):
    results = []
    addr = 0
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)
    while addr < 0x7FFFFFFFFFFF:
        ret = VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size)
        if ret == 0: break
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize or 0
        if mbi.State == MEM_COMMIT and size > 0 and size < 0x10000000:
            protect = mbi.Protect & 0xFF
            if protect not in (0x01, 0x00):
                data = read_mem(handle, base, size)
                if data:
                    offset = 0
                    while True:
                        idx = data.find(needle_bytes, offset)
                        if idx == -1: break
                        results.append(base + idx)
                        if len(results) >= max_results: return results
                        offset = idx + 1
        next_addr = base + max(size, 0x1000)
        if next_addr <= addr: break
        addr = next_addr
    return results


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    # Get tasks
    tasks = []
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            tasks.append((i, t))

    prio_addr = 0x23b7ef6dd3c
    tn_addr = 0x23b6384bad0

    print(f"Tasks: {len(tasks)}")
    for idx, t in tasks:
        print(f"  [{idx}] {t.Name}")

    # SC10: Read computed fields
    print(f"\n{'='*80}")
    print("SC10 COMPUTED FIELDS")
    print("These should be calculated, not stored persistently.")

    task = tasks[0][1]
    sc10_fields = [
        ('Work', lambda t: t.Work),
        ('Cost', lambda t: t.Cost),
        ('PercentComplete', lambda t: t.PercentComplete),
        ('PercentWorkComplete', lambda t: t.PercentWorkComplete),
        ('CV', lambda t: t.CV),
        ('SV', lambda t: t.SV),
    ]
    for name, getter in sc10_fields:
        try:
            val = getter(task)
            print(f"  {name} = {val}")
        except Exception as e:
            print(f"  {name} = ERROR: {e}")

    # Try to set PercentComplete to a distinctive value and search
    try:
        task.PercentComplete = 42
        time.sleep(0.2)
        print(f"\n  PercentComplete set to 42, read-back: {task.PercentComplete}")
    except Exception as e:
        print(f"  Setting PercentComplete failed: {e}")

    # Search for 42 as various encodings
    # As double: 42.0
    needle_dbl = struct.pack('<d', 42.0)
    dbl_hits = search_memory(handle, needle_dbl)
    print(f"\n  42.0 as double: {len(dbl_hits)} hits (too many to be useful)")

    # Reset
    task.PercentComplete = 0

    # SC13: Read structural fields
    print(f"\n{'='*80}")
    print("SC13 STRUCTURAL FIELDS")

    # First, let's create a predecessor relationship
    if len(tasks) >= 2:
        task1 = tasks[0][1]
        task2 = tasks[1][1] if len(tasks) > 1 else None

        if task2:
            print(f"\n  Task 1: '{task1.Name}' (ID {task1.ID})")
            print(f"  Task 2: '{task2.Name}' (ID {task2.ID})")

            # Read predecessor info BEFORE adding
            try:
                pred_before = task2.Predecessors
                print(f"  Task 2 Predecessors (before): '{pred_before}'")
            except Exception as e:
                print(f"  Predecessors read error: {e}")
                pred_before = ""

            # Add predecessor: Task 2 depends on Task 1
            try:
                dep = task2.TaskDependencies.Add(task1)
                print(f"  Added dependency: Task 2 <- Task 1 (type={dep.Type})")
                time.sleep(0.3)
            except Exception as e:
                print(f"  Add dependency failed: {e}")

            # Read predecessor info AFTER
            try:
                pred_after = task2.Predecessors
                succ_t1 = task1.Successors
                print(f"  Task 2 Predecessors: '{pred_after}'")
                print(f"  Task 1 Successors: '{succ_t1}'")
            except Exception as e:
                print(f"  Read error: {e}")

            # Search for predecessor string
            if pred_after:
                needle = pred_after.encode('utf-16-le')
                hits = search_memory(handle, needle)
                print(f"\n  Predecessors string '{pred_after}' UTF-16 hits: {len(hits)}")
                for addr in hits:
                    in_tn = " INSIDE_TN" if tn_addr <= addr <= tn_addr + 1490 else ""
                    print(f"    0x{addr:012x} (prio{addr-prio_addr:+d}){in_tn}")

            # Read ResourceNames
            try:
                rn = task1.ResourceNames
                print(f"\n  Task 1 ResourceNames: '{rn}'")
            except Exception as e:
                print(f"  ResourceNames error: {e}")

            # Read OutlineChildren
            try:
                oc = task1.OutlineChildren
                if oc:
                    print(f"  Task 1 OutlineChildren count: {oc.Count}")
            except Exception as e:
                print(f"  OutlineChildren error: {e}")

            # Now check Task2's TaskDependencies collection structure
            try:
                deps = task2.TaskDependencies
                print(f"\n  Task 2 dependencies count: {deps.Count}")
                for j in range(1, deps.Count + 1):
                    d = deps.Item(j)
                    print(f"    [{j}] From='{d.From.Name}' To='{d.To.Name}' Type={d.Type} Lag={d.Lag}")
            except Exception as e:
                print(f"  Dependencies enumeration error: {e}")

            # Check if Predecessors info is stored in the containers at prio+116-180
            # Specifically check the container at prio+164 which had count=47 (different from others at count=3)
            print(f"\n{'='*80}")
            print("Container at prio+164 (count=47, different from others):")
            ptr164_data = read_mem(handle, prio_addr + 164, 8)
            if ptr164_data:
                ptr164 = struct.unpack('<Q', ptr164_data)[0]
                print(f"  prio+164 -> 0x{ptr164:012x}")
                cont = read_mem(handle, ptr164, 128)
                if cont:
                    for doff in range(0, 128, 16):
                        chunk = cont[doff:doff+16]
                        hex_str = ' '.join(f'{b:02x}' for b in chunk)
                        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                        print(f"    +{doff:3d}: {hex_str:<48s} |{ascii_str}|")

                    # Follow pointers
                    for coff in range(0, 120, 8):
                        qval = struct.unpack_from('<Q', cont, coff)[0]
                        if 0x023b00000000 < qval < 0x023c00000000 and qval != ptr164:
                            sub = read_mem(handle, qval, 64)
                            if sub:
                                # Check for task IDs or dependency data
                                print(f"\n    ptr+{coff} -> 0x{qval:012x}:")
                                for soff in range(0, 64, 16):
                                    chunk = sub[soff:soff+16]
                                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                                    print(f"      +{soff:3d}: {hex_str}")

            # Remove the dependency
            print(f"\nRemoving dependency...")
            try:
                deps = task2.TaskDependencies
                for j in range(deps.Count, 0, -1):
                    deps.Item(j).Delete()
                print(f"  Removed. Predecessors: '{task2.Predecessors}'")
            except Exception as e:
                print(f"  Remove failed: {e}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
