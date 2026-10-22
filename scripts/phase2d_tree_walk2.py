#!/usr/bin/env python3
"""Follow container data pointers at +48/+56 to find string storage.

Container structure (64 bytes):
  +0:  sentinel ptr (self = no siblings)
  +8:  sentinel ptr
  +16: sentinel ptr
  +24: type tag / vtable fragment
  +32: count (u32)
  +36: capacity (u32)
  +40: hash / flags (8 bytes)
  +48: data ptr A
  +56: data ptr B

Follow data ptrs A and B through multiple levels of indirection.
"""

import ctypes
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

def is_valid_ptr(p):
    return 0x10000 < p < 0x7FFFFFFFFFFF

def try_bstr(handle, addr):
    """Try to read a BSTR at addr (string data starts at addr, length prefix at addr-4)."""
    hdr = read_mem(handle, addr - 4, 4)
    if not hdr: return None
    blen = struct.unpack('<I', hdr)[0]
    if blen < 2 or blen > 2000 or blen % 2 != 0: return None
    data = read_mem(handle, addr, blen + 2)
    if not data: return None
    try:
        s = data[:blen].decode('utf-16-le')
        if all(c.isprintable() or c in ' \t\n\r' for c in s):
            return s
    except:
        pass
    return None

def try_utf16(handle, addr, max_chars=100):
    """Try to read null-terminated UTF-16LE at addr."""
    data = read_mem(handle, addr, max_chars * 2 + 2)
    if not data: return None
    try:
        for i in range(0, len(data) - 1, 2):
            if data[i] == 0 and data[i+1] == 0:
                if i == 0: return None
                s = data[:i].decode('utf-16-le')
                if all(c.isprintable() or c in ' \t\n\r' for c in s):
                    return s
                return None
    except:
        pass
    return None

def dump_hex(data, base_addr, indent="    "):
    """Print hex dump."""
    for off in range(0, len(data), 16):
        chunk = data[off:off+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        addr = base_addr + off
        print(f"{indent}0x{addr:012x} +{off:03d}: {hex_str:<48s}  |{ascii_str}|")


def follow_ptr_chain(handle, addr, depth=0, visited=None, label=""):
    """Recursively follow pointer chains looking for strings."""
    if visited is None:
        visited = set()
    if addr in visited or depth > 6 or not is_valid_ptr(addr):
        return
    visited.add(addr)

    data = read_mem(handle, addr, 128)
    if not data:
        return

    prefix = "  " * depth
    print(f"{prefix}[depth={depth}] 0x{addr:012x} {label}")
    dump_hex(data[:64], addr, prefix + "  ")

    # Check if this IS a string
    s = try_bstr(handle, addr)
    if s:
        print(f"{prefix}  >>> BSTR: \"{s}\"")
        return
    s = try_utf16(handle, addr)
    if s and len(s) > 2:
        print(f"{prefix}  >>> UTF-16: \"{s}\"")
        return

    # Check for BSTR at addr-4 pointing into this data
    hdr4 = read_mem(handle, addr - 4, 4)
    if hdr4:
        blen = struct.unpack('<I', hdr4)[0]
        if 2 <= blen <= 200 and blen % 2 == 0:
            s = try_utf16(handle, addr, blen // 2 + 1)
            if s:
                print(f"{prefix}  >>> BSTR (prefix at -4): \"{s}\"")
                return

    # Extract pointers from this data block and follow them
    ptrs_found = []
    for off in range(0, min(64, len(data)) - 7, 8):
        ptr = struct.unpack_from('<Q', data, off)[0]
        if is_valid_ptr(ptr) and ptr != addr:
            # Check if pointer target is a string first (quick check)
            s = try_bstr(handle, ptr)
            if s:
                print(f"{prefix}  +{off:03d} -> BSTR: \"{s}\"")
                continue
            s = try_utf16(handle, ptr)
            if s and len(s) > 2:
                print(f"{prefix}  +{off:03d} -> UTF-16: \"{s}\"")
                continue
            ptrs_found.append((off, ptr))

    # Follow non-string pointers up to 4 per level
    for off, ptr in ptrs_found[:4]:
        if ptr not in visited:
            follow_ptr_chain(handle, ptr, depth + 1, visited, f"from +{off}")


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

    task.Name = "XYZZY_NAME_42"
    task.WBS = "9.8.7"
    task.Contact = "CONTACT_XYZ"
    time.sleep(0.3)

    print(f"Task: Name='{task.Name}', WBS='{task.WBS}', Contact='{task.Contact}'")
    print(f"prio_addr=0x{prio_addr:x}")

    # Read the 5 container pointers (at 16-byte intervals: ptr + padding)
    raw = read_mem(handle, prio_addr + 112, 96)
    if not raw:
        print("ERROR: Can't read pointer region")
        CloseHandle(handle)
        return

    # Show the raw pointer region first
    print("\n=== Pointer region raw (prio+112 to prio+208) ===")
    dump_hex(raw, prio_addr + 112)

    # Extract container pointers
    print("\n=== Following container data pointers ===")
    for i in range(5):
        base_off = i * 16
        # Each 16-byte slot: try ptr at +0, +4, +8
        ptr = struct.unpack_from('<Q', raw, base_off)[0]
        ptr4 = struct.unpack_from('<Q', raw, base_off + 4)[0] if base_off + 12 <= len(raw) else 0
        ptr8 = struct.unpack_from('<Q', raw, base_off + 8)[0] if base_off + 16 <= len(raw) else 0

        container_off = 112 + base_off
        print(f"\n{'='*60}")
        print(f"Container slot {i} (prio+{container_off}):")
        print(f"  ptr@+0 = 0x{ptr:016x}  valid={is_valid_ptr(ptr)}")

        if not is_valid_ptr(ptr):
            continue

        # Read container header
        cdata = read_mem(handle, ptr, 64)
        if not cdata:
            print("  UNREADABLE")
            continue

        print(f"\n  Container at 0x{ptr:012x}:")
        dump_hex(cdata, ptr, "    ")

        count = struct.unpack_from('<I', cdata, 32)[0]
        print(f"\n  count={count}")

        # Follow data pointers at +48 and +56
        data_a = struct.unpack_from('<Q', cdata, 48)[0]
        data_b = struct.unpack_from('<Q', cdata, 56)[0]

        if is_valid_ptr(data_a):
            print(f"\n  --- Data A (container+48) = 0x{data_a:012x} ---")
            follow_ptr_chain(handle, data_a, depth=0, label="data_a")

        if is_valid_ptr(data_b) and data_b != data_a:
            print(f"\n  --- Data B (container+56) = 0x{data_b:012x} ---")
            follow_ptr_chain(handle, data_b, depth=0, label="data_b")

        if i >= 1:
            print("\n  (stopping after 2 containers for brevity)")
            break

    # Reset
    task.Name = "Task 1"
    task.Contact = ""
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
