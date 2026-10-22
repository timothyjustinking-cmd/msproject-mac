#!/usr/bin/env python3
"""Map the 5 container pointers at prio+116-180 and identify their purposes.

Previous findings:
- prio+116: SC9/SC7 compound records (hyperlinks, notes RTF)
- prio+132: Similar to +116 but with count=3, self-referencing sentinel
- prio+148: Similar, different type_tag
- prio+164: DIFFERENT — count=47, manages dependency/relationship graph (SC13)
- prio+180: Similar to +116/+132
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

def try_utf16(data, max_len=60):
    try:
        for i in range(0, min(max_len*2, len(data))-1, 2):
            if data[i] == 0 and data[i+1] == 0:
                if i == 0: return '""'
                s = data[:i].decode('utf-16-le')
                if all(c.isprintable() or c in ' \t' for c in s):
                    return f'"{s}"'
                return None
        return None
    except:
        return None


def analyze_container(handle, cont_addr, label):
    """Deep analysis of a container object."""
    data = read_mem(handle, cont_addr, 192)
    if not data:
        print(f"  Can't read container at 0x{cont_addr:012x}")
        return

    print(f"\n{'='*80}")
    print(f"CONTAINER {label} at 0x{cont_addr:012x}")

    # Parse structure
    p0 = struct.unpack_from('<Q', data, 0)[0]
    p8 = struct.unpack_from('<Q', data, 8)[0]
    p16 = struct.unpack_from('<Q', data, 16)[0]
    type_tag = struct.unpack_from('<Q', data, 24)[0]
    count1 = struct.unpack_from('<I', data, 32)[0]
    count2 = struct.unpack_from('<I', data, 36)[0]

    is_sentinel = (p0 == cont_addr and p8 == cont_addr and p16 == cont_addr)
    print(f"  Sentinel: {'yes' if is_sentinel else 'no'}")
    print(f"  Type tag: 0x{type_tag:016x}")
    print(f"  Counts: {count1}, {count2}")

    # Hash/vtable at +40
    hash_val = struct.unpack_from('<Q', data, 40)[0]
    print(f"  Hash/vtable at +40: 0x{hash_val:016x}")

    # Pointers at +48, +56
    ptr48 = struct.unpack_from('<Q', data, 48)[0]
    ptr56 = struct.unpack_from('<Q', data, 56)[0]
    print(f"  Ptr +48: 0x{ptr48:012x}")
    print(f"  Ptr +56: 0x{ptr56:012x}")

    # Follow linked-list from +48
    if 0x023b00000000 < ptr48 < 0x023c00000000:
        child = read_mem(handle, ptr48, 128)
        if child:
            # Each list node has: next, prev, data...
            c_next = struct.unpack_from('<Q', child, 0)[0]
            c_prev = struct.unpack_from('<Q', child, 8)[0]
            print(f"\n  List node +48 -> 0x{ptr48:012x}:")
            print(f"    next: 0x{c_next:012x}")
            print(f"    prev: 0x{c_prev:012x}")

            # Check for field_ids or strings in the node data
            for off in range(16, 128, 8):
                qval = struct.unpack_from('<Q', child, off)[0]
                u32a = struct.unpack_from('<I', child, off)[0]
                u32b = struct.unpack_from('<I', child, off+4)[0]

                if 0x023b00000000 < qval < 0x023c00000000:
                    # Try to dereference and check for strings
                    deref = read_mem(handle, qval, 64)
                    if deref:
                        s = try_utf16(deref)
                        if s:
                            print(f"    +{off}: ptr 0x{qval:012x} -> {s}")
                        else:
                            # Show first 16 bytes
                            hex_str = ' '.join(f'{b:02x}' for b in deref[:16])
                            print(f"    +{off}: ptr 0x{qval:012x} -> [{hex_str}]")
                elif u32a < 0x10000 and u32a > 0:
                    print(f"    +{off}: u16 val 0x{u32a:04x} ({u32a})")

    # Extended dump
    print(f"\n  Full 192-byte dump:")
    for off in range(0, 192, 16):
        chunk = data[off:off+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"    +{off:3d}: {hex_str:<48s} |{ascii_str}|")

    # Follow ALL pointers and catalog them
    print(f"\n  Pointer catalog:")
    for off in range(0, 184, 8):
        qval = struct.unpack_from('<Q', data, off)[0]
        if qval == cont_addr:
            print(f"    +{off:3d}: SELF")
        elif 0x023b00000000 < qval < 0x023c00000000:
            sub = read_mem(handle, qval, 16)
            if sub:
                hex_str = ' '.join(f'{b:02x}' for b in sub)
                print(f"    +{off:3d}: 0x{qval:012x} -> [{hex_str}]")
        elif 0x140000000 < qval < 0x1c0000000:
            print(f"    +{off:3d}: VTABLE 0x{qval:012x}")


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)
    handle = OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)
    if not handle:
        print("ERROR: Can't open process"); sys.exit(1)

    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    prio_addr = 0x23b7ef6dd3c

    # Read and analyze all 5 containers
    for slot, off in enumerate([116, 132, 148, 164, 180]):
        ptr = read_ptr(handle, prio_addr + off)
        if ptr:
            analyze_container(handle, ptr, f"prio+{off} (slot {slot})")

    # Now read the 16-byte slots at prio+112 to prio+196 to see the full structure
    print(f"\n{'='*80}")
    print("RAW SLOT DATA (prio+112 to prio+196):")
    raw = read_mem(handle, prio_addr + 112, 96)
    if raw:
        for off in range(0, 96, 16):
            chunk = raw[off:off+16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            abs_off = 112 + off
            # Parse as u32 + ptr
            u32_val = struct.unpack_from('<I', chunk, 0)[0]
            ptr_val = struct.unpack_from('<Q', chunk, 4)[0] if len(chunk) >= 12 else 0
            print(f"  prio+{abs_off:3d}: {hex_str}")
            if off % 16 == 0:
                tag = struct.unpack_from('<I', chunk, 0)[0]
                ptr = struct.unpack_from('<Q', chunk, 4)[0]
                extra = struct.unpack_from('<I', chunk, 12)[0]
                print(f"           tag=0x{tag:08x}, ptr=0x{ptr:012x}, extra=0x{extra:08x}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
