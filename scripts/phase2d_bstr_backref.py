#!/usr/bin/env python3
"""Find BSTR string in memory, then search for pointers TO it.

Strategy:
1. Set task Name to "XYZZY_NAME_42"
2. Search memory for UTF-16LE "XYZZY_NAME_42"
3. Find hits with BSTR length prefix (at addr-4)
4. For each BSTR, search memory for 8-byte pointers TO that address
5. This reveals the data structure that references the string
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
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    prio_addr = 0x23b7ef6dd3c

    marker = "XYZZY_NAME_42"
    task.Name = marker
    task.WBS = "WBS_TEST_99"
    task.Contact = "CONTACT_XYZ"
    time.sleep(0.5)

    print(f"Task: Name='{task.Name}', WBS='{task.WBS}', Contact='{task.Contact}'")

    # Step 1: Find all BSTR instances of the marker string
    needle = marker.encode('utf-16-le')
    expected_bstr_len = len(marker) * 2  # 28 bytes

    print(f"\nSearching for UTF-16LE '{marker}' ({expected_bstr_len} bytes)...")
    hits = search_memory(handle, needle)
    print(f"Found {len(hits)} hits")

    bstr_addrs = []
    for addr in hits:
        # Check for BSTR length prefix at addr-4
        hdr = read_mem(handle, addr - 4, 4)
        if hdr:
            blen = struct.unpack('<I', hdr)[0]
            if blen == expected_bstr_len:
                print(f"  BSTR at 0x{addr:016x} (len prefix = {blen})")
                bstr_addrs.append(addr)
            else:
                print(f"  Hit at 0x{addr:016x} (prefix = {blen}, not BSTR)")

    if not bstr_addrs:
        print("No BSTR instances found!")
        CloseHandle(handle)
        return

    # Step 2: For each BSTR, search for pointers TO it
    print(f"\n=== Searching for pointers to {len(bstr_addrs)} BSTR addresses ===")

    for bstr_addr in bstr_addrs:
        ptr_bytes = struct.pack('<Q', bstr_addr)
        print(f"\nSearching for ptrs to 0x{bstr_addr:016x}...")
        ptr_hits = search_memory(handle, ptr_bytes, max_results=10)
        print(f"  Found {len(ptr_hits)} pointer references")

        for ref_addr in ptr_hits:
            rel = ref_addr - prio_addr
            print(f"\n  Ref at 0x{ref_addr:016x} (prio{rel:+d})")

            # Read context around the reference
            ctx = read_mem(handle, ref_addr - 64, 192)
            if ctx:
                print(f"  Context dump (ref at +64):")
                for off in range(0, min(192, len(ctx)), 16):
                    chunk = ctx[off:off+16]
                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    marker_str = " <-- PTR" if off <= 64 < off + 16 else ""
                    print(f"    0x{ref_addr-64+off:012x} +{off:03d}: {hex_str:<48s} |{ascii_str}|{marker_str}")

                # Check if this ref is inside a known structure
                # Look for other pointers nearby that might be part of the same object
                for nearby_off in range(0, min(128, len(ctx)) - 7, 8):
                    ptr = struct.unpack_from('<Q', ctx, nearby_off)[0]
                    if 0x10000 < ptr < 0x7FFFFFFFFFFF and ptr != bstr_addr:
                        # Quick check if any of these nearby ptrs point to our other strings
                        sub = read_mem(handle, ptr - 4, 260)
                        if sub:
                            blen = struct.unpack_from('<I', sub, 0)[0]
                            if 2 <= blen <= 200 and blen % 2 == 0:
                                try:
                                    s = sub[4:4+blen].decode('utf-16-le')
                                    if s and all(c.isprintable() or c in ' \t\n\r' for c in s):
                                        print(f"    Nearby +{nearby_off:03d} -> BSTR: \"{s}\"")
                                except: pass

    # Also search for WBS string
    wbs_needle = "WBS_TEST_99".encode('utf-16-le')
    wbs_bstr_len = len("WBS_TEST_99") * 2
    print(f"\n=== WBS string search ===")
    wbs_hits = search_memory(handle, wbs_needle, max_results=10)
    for addr in wbs_hits:
        hdr = read_mem(handle, addr - 4, 4)
        if hdr:
            blen = struct.unpack('<I', hdr)[0]
            if blen == wbs_bstr_len:
                print(f"  WBS BSTR at 0x{addr:016x}")
                # Search for ptr to it
                wbs_ptr_bytes = struct.pack('<Q', addr)
                wbs_refs = search_memory(handle, wbs_ptr_bytes, max_results=5)
                for ref in wbs_refs:
                    rel = ref - prio_addr
                    print(f"    Referenced from 0x{ref:016x} (prio{rel:+d})")

    # Reset
    task.Name = "Task 1"
    task.WBS = ""
    task.Contact = ""
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
