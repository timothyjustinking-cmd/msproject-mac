#!/usr/bin/env python3
"""Probe SC9 (lookup) field storage.

SC9 has 120 Task fields including:
- Hyperlink, HyperlinkAddress, HyperlinkHref, HyperlinkSubAddress, HyperlinkScreenTip
- Calendar
- Estimated (boolean)
- OutlineCode1-10
- Text fields mapped as lookups

Strategy: Set Hyperlink and HyperlinkAddress to distinctive values,
search memory, and find where they're stored.
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
    tn_addr = 0x23b6384bad0

    # Set hyperlink fields
    marker_hl = "HYPERLINK_MARKER_77"
    marker_addr = "http://XYZZY-PLUGH-99.example.com"
    marker_sub = "SUB_ANCHOR_88"

    print("Setting Hyperlink fields...")
    try:
        task.HyperlinkAddress = marker_addr
        print(f"  HyperlinkAddress = '{marker_addr}'")
    except Exception as e:
        print(f"  HyperlinkAddress FAILED: {e}")

    try:
        task.Hyperlink = marker_hl
        print(f"  Hyperlink = '{marker_hl}'")
    except Exception as e:
        print(f"  Hyperlink FAILED: {e}")

    try:
        task.HyperlinkSubAddress = marker_sub
        print(f"  HyperlinkSubAddress = '{marker_sub}'")
    except Exception as e:
        print(f"  HyperlinkSubAddress FAILED: {e}")

    time.sleep(0.5)

    # Verify via COM read-back
    print(f"\nRead-back:")
    print(f"  Hyperlink = '{task.Hyperlink}'")
    print(f"  HyperlinkAddress = '{task.HyperlinkAddress}'")
    print(f"  HyperlinkSubAddress = '{task.HyperlinkSubAddress}'")

    # Search for HyperlinkAddress (most distinctive)
    needle = marker_addr.encode('utf-16-le')
    print(f"\nSearching for HyperlinkAddress UTF-16LE...")
    hits = search_memory(handle, needle)
    print(f"Found {len(hits)} hits")

    for addr in hits:
        rel_prio = addr - prio_addr
        rel_tn = addr - tn_addr
        ctx = read_mem(handle, addr - 48, 160)
        if not ctx: continue

        print(f"\n  0x{addr:016x} (prio{rel_prio:+d}, tn{rel_tn:+d}):")
        for off in range(0, min(160, len(ctx)), 16):
            chunk = ctx[off:off+16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            tag = " <<< STRING" if off <= 48 < off + 16 else ""
            print(f"    0x{addr-48+off:012x}: {hex_str:<48s} |{ascii_str}|{tag}")

        # Check for BSTR length prefix
        hdr = read_mem(handle, addr - 4, 4)
        if hdr:
            blen = struct.unpack('<I', hdr)[0]
            expected = len(marker_addr) * 2
            if blen == expected:
                print(f"    -> BSTR (len={blen})")

        # Check if inside TASKNODE
        if tn_addr <= addr <= tn_addr + 1490:
            print(f"    -> INSIDE TASKNODE at offset 0x{addr - tn_addr:x}")

    # Also search for Hyperlink name
    needle_hl = marker_hl.encode('utf-16-le')
    print(f"\nSearching for Hyperlink name UTF-16LE...")
    hits_hl = search_memory(handle, needle_hl)
    print(f"Found {len(hits_hl)} hits")
    for addr in hits_hl:
        print(f"  0x{addr:016x} (prio{addr-prio_addr:+d}, tn{addr-tn_addr:+d})")
        if tn_addr <= addr <= tn_addr + 1490:
            print(f"    -> INSIDE TASKNODE at offset 0x{addr - tn_addr:x}")

    # Also check: is the hyperlink data in a DIFFERENT TASKNODE?
    # Search for the marker and then look for a TASKNODE header nearby
    if hits:
        first_hit = hits[0]
        scan_start = first_hit - 4096
        scan_data = read_mem(handle, scan_start, 8192)
        if scan_data:
            print(f"\nScanning for TASKNODE header near first hit 0x{first_hit:012x}...")
            for scan_off in range(0, 4096, 4):
                size_val = struct.unpack_from('<I', scan_data, scan_off)[0]
                count_val = struct.unpack_from('<I', scan_data, scan_off + 4)[0]
                if 100 < size_val < 0x10000 and 2 < count_val < 200:
                    idx_end = scan_off + 8 + count_val * 8
                    if idx_end < len(scan_data):
                        fid0 = struct.unpack_from('<I', scan_data, scan_off + 8)[0]
                        if (fid0 >> 16) == 0x0b40:
                            abs_hdr = scan_start + scan_off
                            rel_to_str = first_hit - abs_hdr
                            print(f"  Header at 0x{abs_hdr:012x}: size={size_val}, count={count_val}, first_fid=0x{fid0:08x}")
                            print(f"    String at record+0x{rel_to_str:x}")
                            # Print first 5 index entries
                            for e in range(min(count_val, 5)):
                                efid = struct.unpack_from('<I', scan_data, scan_off + 8 + e*8)[0]
                                eoff = struct.unpack_from('<I', scan_data, scan_off + 8 + e*8 + 4)[0]
                                print(f"    entry[{e}]: fid=0x{efid:08x}, off=0x{eoff:04x}")
                            break

    # Reset
    print("\nResetting...")
    task.Hyperlink = ""
    task.HyperlinkAddress = ""
    task.HyperlinkSubAddress = ""
    time.sleep(0.3)

    CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
