#!/usr/bin/env python3
"""Trace the pointer chain from prio+116 container to SC9 compound records.

Key finding: prio+116 container has a pointer at +80 near the compound record area.
Follow that chain to understand SC9 storage architecture.
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

def read_ptr(handle, addr):
    d = read_mem(handle, addr, 8)
    if not d: return None
    return struct.unpack('<Q', d)[0]

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

def dump_hex(handle, addr, size, label=""):
    data = read_mem(handle, addr, size)
    if not data:
        print(f"  Can't read 0x{addr:012x}")
        return None
    if label:
        print(f"\n{label} at 0x{addr:012x}:")
    for off in range(0, min(size, len(data)), 16):
        chunk = data[off:off+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"  +{off:4d} (0x{addr+off:012x}): {hex_str:<48s} |{ascii_str}|")
    return data


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

    # Set hyperlink on task
    marker = "ZZ_CHAIN_TRACE_55"
    task.Hyperlink = marker
    task.HyperlinkAddress = "http://chain-trace-55.example"
    task.HyperlinkSubAddress = "sub55"
    time.sleep(0.3)

    # Find the compound record
    needle = marker.encode('utf-16-le')
    hits = search_memory(handle, needle)
    print(f"Compound record hits: {len(hits)}")

    # Find the lowest-address hit (likely the authoritative one or near it)
    # Actually, we want the one closest to the container cluster
    # Container at prio+116 had pointer at +80 going to 0x23b72f0e540
    # Let me re-read the container to get its current state

    print(f"\n{'='*80}")
    print("CONTAINER AT PRIO+116")

    cont_ptr = read_ptr(handle, prio_addr + 116)
    print(f"prio+116 -> 0x{cont_ptr:012x}")

    cont_data = dump_hex(handle, cont_ptr, 128, "Container")
    if not cont_data:
        CloseHandle(handle)
        return

    # Parse container structure
    print(f"\nContainer fields:")
    for off in range(0, 120, 8):
        qval = struct.unpack_from('<Q', cont_data, off)[0]
        if qval == cont_ptr:
            print(f"  +{off:3d}: self-ref (sentinel)")
        elif 0x023b00000000 < qval < 0x023c00000000:
            print(f"  +{off:3d}: ptr 0x{qval:012x}")
        elif 0x140000000 < qval < 0x1c0000000:
            print(f"  +{off:3d}: code/vtable 0x{qval:012x}")
        else:
            u32a = struct.unpack_from('<I', cont_data, off)[0]
            u32b = struct.unpack_from('<I', cont_data, off+4)[0]
            if u32a < 1000 or u32b < 1000:
                print(f"  +{off:3d}: u32({u32a}, {u32b})")

    # Follow ALL pointers from the container
    print(f"\n{'='*80}")
    print("FOLLOWING CONTAINER POINTERS:")

    for off in range(0, 120, 8):
        qval = struct.unpack_from('<Q', cont_data, off)[0]
        if qval == cont_ptr:
            continue
        if 0x023b00000000 < qval < 0x023c00000000:
            data = read_mem(handle, qval, 256)
            if data:
                print(f"\n--- Container+{off} -> 0x{qval:012x} ---")
                # Check for compound records nearby — look for UTF-16 markers
                has_marker = False
                for sub_off in range(0, min(256, len(data)) - 4, 2):
                    try:
                        s = data[sub_off:sub_off+10].decode('utf-16-le', errors='ignore')
                        if 'ZZ_CH' in s or 'http:' in s:
                            has_marker = True
                            break
                    except:
                        pass

                # Show first 128 bytes
                for doff in range(0, min(128, len(data)), 16):
                    chunk = data[doff:doff+16]
                    hex_str = ' '.join(f'{b:02x}' for b in chunk)
                    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    print(f"  +{doff:3d}: {hex_str:<48s} |{ascii_str}|")

                # Follow secondary pointers (one level deep)
                for sub_off in range(0, min(128, len(data)) - 7, 8):
                    sub_ptr = struct.unpack_from('<Q', data, sub_off)[0]
                    if 0x023b00000000 < sub_ptr < 0x023c00000000 and sub_ptr != qval:
                        # Check if this secondary pointer is near any hit
                        for h in hits:
                            if abs(sub_ptr - h) < 0x100000:
                                print(f"  +{sub_off}: secondary ptr 0x{sub_ptr:012x} NEAR compound hit 0x{h:012x} (delta={sub_ptr-h:+d})")

    # Direct approach: find the object CONTAINING the compound record
    # by looking at the 256-byte region just before the first hit near the container area
    print(f"\n{'='*80}")
    print("OBJECT CONTAINING COMPOUND RECORD:")

    # Find the compound record hit that's in the 0x23b72... range (near container pointers)
    for h in sorted(hits):
        if 0x023b72000000 < h < 0x023b73000000:
            # This is the hit in the container range
            # Read 1KB before it to find the object header
            pre = read_mem(handle, h - 1024, 1024)
            if not pre:
                continue

            print(f"\nCompound record at 0x{h:012x}, scanning backwards for object header...")

            # Look for pointers back to the task/prio area
            for scan_off in range(1016, 0, -8):
                qval = struct.unpack_from('<Q', pre, scan_off)[0]
                if abs(qval - prio_addr) < 0x10000:
                    abs_addr = h - 1024 + scan_off
                    print(f"  Found task ptr at 0x{abs_addr:012x} (rec-0x{h-abs_addr:x}): -> 0x{qval:012x} (prio{qval-prio_addr:+d})")

            # Also look for vtable-like pointers (0x14... range)
            for scan_off in range(1016, 0, -8):
                qval = struct.unpack_from('<Q', pre, scan_off)[0]
                if 0x140000000 < qval < 0x1c0000000:
                    abs_addr = h - 1024 + scan_off
                    print(f"  Found vtable at 0x{abs_addr:012x} (rec-0x{h-abs_addr:x}): 0x{qval:012x}")

            # Show the 256 bytes just before the compound record
            print(f"\n256 bytes before compound record:")
            pre_256 = pre[-256:]
            base_addr = h - 256
            for doff in range(0, 256, 16):
                chunk = pre_256[doff:doff+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                abs_a = base_addr + doff
                print(f"  0x{abs_a:012x}: {hex_str:<48s} |{ascii_str}|")

            break

    # Reset
    print("\n\nResetting...")
    task.Hyperlink = ""
    task.HyperlinkAddress = ""
    task.HyperlinkSubAddress = ""
    CloseHandle(handle)
    print("Done.")


if __name__ == '__main__':
    main()
