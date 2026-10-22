#!/usr/bin/env python3
"""Map the container holding SC9 compound records.

Strategy: Set hyperlinks on multiple tasks, find the cluster of compound records,
read the full container to understand the data structure.
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

def search_pointer(handle, target_addr, max_results=10):
    needle = struct.pack('<Q', target_addr)
    return search_memory(handle, needle, max_results)


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    # Get all tasks
    tasks = []
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            tasks.append(t)

    prio_addr = 0x23b7ef6dd3c  # Task 1 prio

    # Set hyperlinks on first task only
    task = tasks[0]
    marker = "QQZZ_HYPERLINK_PROBE_99"
    task.Hyperlink = marker
    task.HyperlinkAddress = "http://QQZZ-99.test.example.com"
    task.HyperlinkSubAddress = "anchor99"
    time.sleep(0.3)

    # Find the compound record via the marker
    needle = marker.encode('utf-16-le')
    hits = search_memory(handle, needle)
    print(f"Found {len(hits)} hits for '{marker}'")

    # For each hit, read the container context (2KB before, 2KB after)
    for h_addr in sorted(hits):
        # Try to find the record header before this string
        # We expect: [header 16-20 bytes][comp_0_header 12 bytes][string]
        # So header is ~28 bytes before string start
        pre_data = read_mem(handle, h_addr - 256, 256)
        if not pre_data:
            continue

        # Look for the component count pattern: 04 00 00 00 followed by byte_len, 00 00 00 00, 0c 00 00 00
        found_hdr = False
        for scan in range(200, 0, -4):
            if scan + 16 >= len(pre_data):
                continue
            val = struct.unpack_from('<I', pre_data, scan)[0]
            if val == 4:  # component count
                # Check if followed by comp_0 header
                next_blen = struct.unpack_from('<I', pre_data, scan + 4)[0]
                next_cid = struct.unpack_from('<I', pre_data, scan + 8)[0]
                next_hdr = struct.unpack_from('<I', pre_data, scan + 12)[0]
                if next_cid == 0 and next_hdr == 12 and 0 < next_blen < 1000:
                    # This is the component count
                    hdr_addr = h_addr - 256 + scan
                    rec_header_start = hdr_addr - 12  # 12 bytes before count: [hash][hash][size_tag]
                    print(f"\nHit 0x{h_addr:012x}: Record header at 0x{rec_header_start:012x}")
                    found_hdr = True

                    # Read from before header
                    big_pre = read_mem(handle, rec_header_start - 128, 128)
                    if big_pre:
                        print(f"\n128 bytes BEFORE record header at 0x{rec_header_start:012x}:")
                        for off in range(0, 128, 16):
                            chunk = big_pre[off:off+16]
                            hex_str = ' '.join(f'{b:02x}' for b in chunk)
                            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                            abs_a = rec_header_start - 128 + off
                            print(f"  0x{abs_a:012x}: {hex_str:<48s} |{ascii_str}|")

                    # Read the full record header
                    full_hdr = read_mem(handle, rec_header_start, 32)
                    if full_hdr:
                        print(f"\nRecord header (32 bytes):")
                        for off in range(0, 32, 8):
                            chunk = full_hdr[off:off+8]
                            hex_str = ' '.join(f'{b:02x}' for b in chunk)
                            u32a = struct.unpack_from('<I', chunk, 0)[0]
                            u32b = struct.unpack_from('<I', chunk, 4)[0] if len(chunk) >= 8 else 0
                            print(f"  +{off:2d}: {hex_str}  ({u32a}, {u32b})")

                    # Search for pointers to this record header
                    print(f"\nSearching for pointers to header 0x{rec_header_start:012x}...")
                    ptrs = search_pointer(handle, rec_header_start, 10)
                    print(f"  Found {len(ptrs)} pointers")
                    for p in ptrs:
                        print(f"  0x{p:012x} (prio{p-prio_addr:+d})")
                        ctx = read_mem(handle, p - 32, 80)
                        if ctx:
                            for off in range(0, 80, 16):
                                chunk = ctx[off:off+16]
                                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                                tag = " <<< PTR" if off <= 32 < off + 16 else ""
                                print(f"    0x{p-32+off:012x}: {hex_str}{tag}")

                    # Also search for pointer to record header + some offsets
                    for delta in [4, 8, 12, 16, -4, -8]:
                        ptrs2 = search_pointer(handle, rec_header_start + delta, 5)
                        if ptrs2:
                            print(f"\n  Pointers to header{delta:+d} (0x{rec_header_start+delta:012x}): {len(ptrs2)}")
                            for p in ptrs2:
                                print(f"    0x{p:012x} (prio{p-prio_addr:+d})")

                    break

        if not found_hdr:
            print(f"\nHit 0x{h_addr:012x}: Could not find record header")
            # Show 128 bytes before the string
            for off in range(0, min(256, len(pre_data)), 16):
                chunk = pre_data[off:off+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                abs_a = h_addr - 256 + off
                print(f"  0x{abs_a:012x}: {hex_str:<48s} |{ascii_str}|")

        # Only analyze first few hits
        if sorted(hits).index(h_addr) >= 3:
            break

    # Phase 2: Also check — are compound records accessible from the TASKNODE?
    # Read the TASKNODE and check for pointers out to the compound record area
    print(f"\n{'='*80}")
    print("Checking TASKNODE for SC9 pointers...")
    tn_addr = 0x23b6384bad0
    tn_data = read_mem(handle, tn_addr, 1490)
    if tn_data:
        # Look for any field_ids that might be SC9
        # SC9 field_ids from the master: Hyperlink=0x0b40008a, HyperlinkAddress=0x0b40008b, etc.
        # Let's check if any SC9 field_ids appear in the TASKNODE index
        tn_count = struct.unpack_from('<I', tn_data, 4)[0]
        sc9_in_tn = []
        for i in range(tn_count):
            fid = struct.unpack_from('<I', tn_data, 8 + i * 8)[0]
            lower = fid & 0xFFFF
            # SC9 field_ids from storage_classes.md
            if lower in [0x008a, 0x008b, 0x008c, 0x008d, 0x008e,  # Hyperlink*
                         0x00d0, 0x0133, 0x0134, 0x0135,  # Calendar, OutlineCode
                         0x014e, 0x014f, 0x0150, 0x0151]:
                off = struct.unpack_from('<I', tn_data, 8 + i * 8 + 4)[0]
                sc9_in_tn.append((i, fid, off, lower))

        if sc9_in_tn:
            print(f"SC9 fields found in TASKNODE: {len(sc9_in_tn)}")
            for i, fid, off, lower in sc9_in_tn:
                print(f"  [{i}] fid=0x{fid:08x} (lower=0x{lower:04x}), offset=0x{off:x}")
        else:
            print("No SC9 field_ids found in TASKNODE index")

    # Phase 3: Check the 5 container pointers at prio+116-180
    print(f"\n{'='*80}")
    print("Re-checking container pointers at prio+116-180...")
    for slot, off in enumerate([116, 132, 148, 164, 180]):
        ptr_data = read_mem(handle, prio_addr + off, 8)
        if ptr_data:
            ptr = struct.unpack('<Q', ptr_data)[0]
            print(f"\n  prio+{off}: -> 0x{ptr:012x}")
            # Read the container
            cont = read_mem(handle, ptr, 128)
            if cont:
                # Check for pointers to the hyperlink compound record area
                for coff in range(0, 120, 8):
                    qval = struct.unpack_from('<Q', cont, coff)[0]
                    if 0x023b00000000 < qval < 0x023c00000000:
                        # This is a valid heap pointer — check if it's near any hit
                        for h in sorted(hits)[:5]:
                            if abs(qval - h) < 0x10000:
                                print(f"    +{coff}: 0x{qval:012x} — NEAR compound record 0x{h:012x} (delta={qval-h:+d})")

                # Also show first 64 bytes
                for doff in range(0, 64, 16):
                    chunk = cont[doff:doff+16]
                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                    print(f"    +{doff:3d}: {hex_str}")

    # Reset
    print("\n\nResetting...")
    task.Hyperlink = ""
    task.HyperlinkAddress = ""
    task.HyperlinkSubAddress = ""
    CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
