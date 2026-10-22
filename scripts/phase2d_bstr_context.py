#!/usr/bin/env python3
"""Examine all UTF-16 hits for Name string, especially the cluster at 0x23b7ec6XXXX.
Also try differential: change Name and see which locations change.
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

def search_memory(handle, needle_bytes, max_results=30):
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
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    prio_addr = 0x23b7ef6dd3c

    # Phase 1: Set distinctive name and find all hits
    marker1 = "XYZZY_NAME_42"
    task.Name = marker1
    time.sleep(0.5)

    needle1 = marker1.encode('utf-16-le')
    print(f"Phase 1: Name = '{marker1}'")
    hits1 = search_memory(handle, needle1)
    print(f"Found {len(hits1)} hits")

    # Dump context for each hit
    for addr in hits1:
        rel = addr - prio_addr
        ctx = read_mem(handle, addr - 32, 128)
        if not ctx:
            print(f"\n  0x{addr:016x}: UNREADABLE")
            continue

        print(f"\n  0x{addr:016x} (prio{rel:+d}):")
        for off in range(0, min(128, len(ctx)), 16):
            chunk = ctx[off:off+16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            abs_addr = addr - 32 + off
            tag = " <<< STRING START" if off == 32 else ""
            print(f"    0x{abs_addr:012x}: {hex_str:<48s} |{ascii_str}|{tag}")

    # Phase 2: Change name and find which addresses change
    print(f"\n{'='*60}")
    marker2 = "ABCDE_FGHIJ_99"
    task.Name = marker2
    time.sleep(0.5)

    needle2 = marker2.encode('utf-16-le')
    print(f"Phase 2: Name = '{marker2}'")
    hits2 = search_memory(handle, needle2)
    print(f"Found {len(hits2)} hits")

    # Check which phase-1 addresses now have the new string
    survived = []
    for addr in hits1:
        data = read_mem(handle, addr, len(needle2) + 2)
        if data and needle2 in data:
            survived.append(addr)

    new_addrs = [a for a in hits2 if a not in hits1]

    print(f"\nPhase 1 addrs that now contain new string: {len(survived)}")
    for a in survived:
        print(f"  0x{a:016x} (prio{a-prio_addr:+d})")

    print(f"\nNew addrs in phase 2 only: {len(new_addrs)}")
    for a in new_addrs:
        rel = a - prio_addr
        print(f"  0x{a:016x} (prio{rel:+d})")
        # Dump context
        ctx = read_mem(handle, a - 32, 96)
        if ctx:
            for off in range(0, min(96, len(ctx)), 16):
                chunk = ctx[off:off+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                abs_addr = a - 32 + off
                print(f"    0x{abs_addr:012x}: {hex_str}")

    # Phase 3: Search for pointer to the new BSTR
    # Find BSTR with correct length
    bstr_expected = len(marker2) * 2
    for addr in hits2:
        hdr = read_mem(handle, addr - 4, 4)
        if hdr:
            blen = struct.unpack('<I', hdr)[0]
            if blen == bstr_expected:
                print(f"\n  New BSTR at 0x{addr:016x}")
                # Search for ptrs to it
                ptr_bytes = struct.pack('<Q', addr)
                refs = search_memory(handle, ptr_bytes, max_results=5)
                if refs:
                    for r in refs:
                        print(f"    Referenced from 0x{r:016x} (prio{r-prio_addr:+d})")
                else:
                    print(f"    No pointer refs found")

    # Phase 4: Check if the cluster addresses (0x23b7ec6XXXX) have predictable offsets
    cluster = [a for a in hits2 if (a >> 20) == (0x23b7ec6f >> 4)]
    if len(cluster) >= 2:
        cluster.sort()
        print(f"\nCluster analysis ({len(cluster)} hits in 0x23b7ec6XXXX range):")
        for i, a in enumerate(cluster):
            delta = cluster[i] - cluster[i-1] if i > 0 else 0
            print(f"  0x{a:016x} delta={delta}")

    # Reset
    task.Name = "Task 1"
    time.sleep(0.3)
    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
