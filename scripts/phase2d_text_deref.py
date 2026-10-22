#!/usr/bin/env python3
"""Dereference the pointer region to find string storage objects.

The flat buffer at prio+112 to prio+184 has 5 entries at 16-byte intervals.
Each entry contains a 64-bit pointer (at offset +4 within the 16-byte block)
where the high 32 bits are 0x0000023b.
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


def try_read_string(handle, addr, context=""):
    """Try to read a string at addr, trying multiple formats."""
    data = read_mem(handle, addr, 256)
    if not data:
        return f"  {context}0x{addr:016x}: UNREADABLE"

    lines = [f"  {context}0x{addr:016x}:"]

    # Hex dump first 64 bytes
    for off in range(0, min(64, len(data)), 16):
        chunk = data[off:off+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        lines.append(f"    +{off:03d}: {hex_str}  |{ascii_str}|")

    # Try interpreting as various string formats
    # 1. Direct UTF-16LE
    try:
        for i in range(0, min(200, len(data))-1, 2):
            if data[i] == 0 and data[i+1] == 0:
                s = data[:i].decode('utf-16-le')
                if len(s) > 0 and all(c.isprintable() or c in ' \t\n' for c in s):
                    lines.append(f"    => UTF-16 string at +0: \"{s}\"")
                break
    except:
        pass

    # 2. Check for pointer to BSTR (8-byte ptr, then BSTR at *ptr)
    if len(data) >= 8:
        ptr = struct.unpack_from('<Q', data, 0)[0]
        if 0x10000 < ptr < 0x7FFFFFFFFFFF:
            bstr_data = read_mem(handle, ptr - 4, 260)
            if bstr_data:
                bstr_len = struct.unpack_from('<I', bstr_data, 0)[0]
                if 0 < bstr_len < 500 and bstr_len % 2 == 0:
                    try:
                        s = bstr_data[4:4+bstr_len].decode('utf-16-le')
                        if all(c.isprintable() or c in ' \t\n' for c in s):
                            lines.append(f"    => Ptr at +0 -> BSTR(len={bstr_len}): \"{s}\"")
                    except:
                        pass

    # 3. Look for BSTR at various offsets
    for bstr_off in [4, 8, 12, 16, 20, 24, 28, 32]:
        if bstr_off + 4 > len(data):
            break
        blen = struct.unpack_from('<I', data, bstr_off)[0]
        if 0 < blen < 500 and blen % 2 == 0 and bstr_off + 4 + blen <= len(data):
            try:
                s = data[bstr_off+4:bstr_off+4+blen].decode('utf-16-le')
                if len(s) > 0 and all(c.isprintable() or c in ' \t\n' for c in s):
                    lines.append(f"    => BSTR at +{bstr_off}(len={blen}): \"{s}\"")
            except:
                pass

    # 4. Look for pointer at various offsets that leads to string
    for ptr_off in [8, 16, 24, 32, 40, 48]:
        if ptr_off + 8 > len(data):
            break
        ptr = struct.unpack_from('<Q', data, ptr_off)[0]
        if 0x10000 < ptr < 0x7FFFFFFFFFFF:
            sub = read_mem(handle, ptr, 64)
            if sub:
                try:
                    for i in range(0, min(60, len(sub))-1, 2):
                        if sub[i] == 0 and sub[i+1] == 0:
                            s = sub[:i].decode('utf-16-le')
                            if len(s) > 2 and all(c.isprintable() or c in ' \t\n' for c in s):
                                lines.append(f"    => Ptr at +{ptr_off} -> UTF-16: \"{s}\"")
                            break
                except:
                    pass
                # Also try as BSTR (read -4 before ptr)
                bstr_hdr = read_mem(handle, ptr - 4, 68)
                if bstr_hdr:
                    blen = struct.unpack_from('<I', bstr_hdr, 0)[0]
                    if 0 < blen < 500 and blen % 2 == 0:
                        try:
                            s = bstr_hdr[4:4+blen].decode('utf-16-le')
                            if all(c.isprintable() or c in ' \t\n' for c in s):
                                lines.append(f"    => Ptr at +{ptr_off} -> BSTR(len={blen}): \"{s}\"")
                        except:
                            pass

    return '\n'.join(lines)


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

    # Set distinctive values
    task.Name = "XYZZY_PLUGH_42"
    task.WBS = "9.8.7"
    task.Contact = "CONTACT_TEST"
    time.sleep(0.3)

    print(f"Task: Name='{task.Name}', WBS='{task.WBS}', Contact='{task.Contact}'")
    print(f"prio_addr=0x{prio_addr:x}")

    # Read prio+108 to prio+196 as raw bytes (wider range)
    raw = read_mem(handle, prio_addr + 108, 100)
    if not raw:
        print("ERROR: Can't read pointer region")
        CloseHandle(handle)
        return

    print("\n=== Raw pointer region (u32 view) ===")
    for i in range(0, len(raw), 4):
        val = struct.unpack_from('<I', raw, i)[0]
        off = 108 + i
        print(f"  prio+{off:3d}: 0x{val:08x} ({val})")

    # Try reading as 64-bit pointers at every 8-byte boundary
    print("\n=== 64-bit pointer scan ===")
    for i in range(0, len(raw) - 7, 4):
        ptr = struct.unpack_from('<Q', raw, i)[0]
        off = 108 + i
        # Check if this looks like a valid heap pointer (0x0000023bXXXXXXXX)
        if (ptr >> 32) == 0x23b and (ptr & 0xFFFFFFFF) > 0x1000:
            print(f"  prio+{off:3d}: 0x{ptr:016x} <- VALID HEAP PTR")
            # Dereference
            result = try_read_string(handle, ptr)
            print(result)

    # Reset
    task.Name = "Task 1"
    task.Contact = ""
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
