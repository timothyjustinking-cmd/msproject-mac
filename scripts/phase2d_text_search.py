#!/usr/bin/env python3
"""Find where Name string is stored by searching process memory.

SC4 fields (Name, WBS, Contact) aren't in the flat buffer.
Set Name to a distinctive string, then scan all committed memory
for that string in UTF-16LE encoding.
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
    """Search all committed memory for needle_bytes."""
    results = []
    addr = 0
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)

    while addr < 0x7FFFFFFFFFFF:
        ret = VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), mbi_size)
        if ret == 0:
            break

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
                        if idx == -1:
                            break
                        hit_addr = base + idx
                        results.append(hit_addr)
                        if len(results) >= max_results:
                            return results
                        offset = idx + 1

        next_addr = base + max(size, 0x1000)
        if next_addr <= addr:
            break
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

    # Set a VERY distinctive name
    marker = "XYZZY_PLUGH_42"
    task.Name = marker
    time.sleep(0.5)

    print(f"Task Name set to: '{marker}'")
    print(f"Prio addr: 0x{prio_addr:x}")

    # Search for UTF-16LE encoded string
    needle_utf16 = marker.encode('utf-16-le')
    print(f"Searching for UTF-16LE: {needle_utf16.hex()}")

    hits = search_memory(handle, needle_utf16)
    print(f"\nFound {len(hits)} UTF-16LE matches:")
    for addr in hits:
        rel = addr - prio_addr
        # Read context around hit
        ctx = read_mem(handle, addr - 32, 128)
        if ctx:
            # Show hex dump
            hex_before = ' '.join(f'{b:02x}' for b in ctx[:32])
            hex_match = ' '.join(f'{b:02x}' for b in ctx[32:32+len(needle_utf16)])
            hex_after = ' '.join(f'{b:02x}' for b in ctx[32+len(needle_utf16):64])
            print(f"  0x{addr:016x} (prio{rel:+d})")
            print(f"    before: {hex_before}")
            print(f"    match:  {hex_match}")
            print(f"    after:  {hex_after}")

            # Check for length prefix or BSTR header
            # BSTR has 4-byte length prefix (in bytes, not chars)
            pre4 = struct.unpack_from('<I', ctx, 28)[0] if len(ctx) >= 32 else 0
            pre2 = struct.unpack_from('<H', ctx, 30)[0] if len(ctx) >= 32 else 0
            expected_len = len(marker) * 2  # UTF-16 byte count
            if pre4 == expected_len:
                print(f"    *** BSTR detected: length prefix = {pre4} bytes at addr-4 ***")
            elif pre2 == len(marker):
                print(f"    *** Pascal-style: char count = {pre2} at addr-2 ***")

    # Also search for ASCII
    needle_ascii = marker.encode('ascii')
    print(f"\nSearching for ASCII: {needle_ascii.hex()}")
    hits_ascii = search_memory(handle, needle_ascii)
    # Filter out UTF-16 hits (they contain the ASCII bytes interleaved)
    pure_ascii = []
    for addr in hits_ascii:
        if addr not in hits:
            # Check if it's really ASCII (not part of UTF-16)
            ctx = read_mem(handle, addr, len(marker) * 2)
            if ctx and ctx[1] != 0:  # Not UTF-16 (second byte would be 0 for ASCII range)
                pure_ascii.append(addr)

    print(f"Found {len(pure_ascii)} pure ASCII matches:")
    for addr in pure_ascii:
        rel = addr - prio_addr
        ctx = read_mem(handle, addr - 16, 64)
        if ctx:
            hex_before = ' '.join(f'{b:02x}' for b in ctx[:16])
            hex_match = ' '.join(f'{b:02x}' for b in ctx[16:16+len(marker)])
            print(f"  0x{addr:016x} (prio{rel:+d})")
            print(f"    before: {hex_before}")
            print(f"    match:  {hex_match}")

    # Reset
    task.Name = "Task 1"
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
