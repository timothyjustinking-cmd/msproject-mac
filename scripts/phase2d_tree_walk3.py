#!/usr/bin/env python3
"""Follow container pointers at correct offsets (prio+116,132,148,164,180).

Each 16-byte slot: [u32 tag][u64 ptr][u32 pad]
Pointers at prio+116, +132, +148, +164, +180 target container objects.
Each container has data at +48/+56. Follow all levels to find BSTRs.
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

def read_ptr(handle, addr):
    d = read_mem(handle, addr, 8)
    if not d: return 0
    return struct.unpack('<Q', d)[0]

def is_valid_ptr(p):
    return 0x10000 < p < 0x7FFFFFFFFFFF

def try_bstr_at(handle, str_addr):
    """Try reading BSTR where str_addr points to char data (length at str_addr-4)."""
    hdr = read_mem(handle, str_addr - 4, 4)
    if not hdr: return None
    blen = struct.unpack('<I', hdr)[0]
    if blen < 2 or blen > 2000 or blen % 2 != 0: return None
    data = read_mem(handle, str_addr, blen + 2)
    if not data or len(data) < blen: return None
    try:
        s = data[:blen].decode('utf-16-le')
        if all(c.isprintable() or c in ' \t\n\r' for c in s):
            return s
    except: pass
    return None

def try_utf16_at(handle, addr, max_chars=100):
    """Try reading null-terminated UTF-16LE."""
    data = read_mem(handle, addr, max_chars * 2 + 2)
    if not data: return None
    try:
        for i in range(0, min(len(data) - 1, max_chars * 2), 2):
            if data[i] == 0 and data[i+1] == 0:
                if i == 0: return None
                s = data[:i].decode('utf-16-le')
                if all(c.isprintable() or c in ' \t\n\r' for c in s):
                    return s
                return None
    except: pass
    return None

def dump_hex(data, base_addr, indent="  "):
    for off in range(0, min(len(data), 96), 16):
        chunk = data[off:off+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"{indent}0x{base_addr+off:012x} +{off:03d}: {hex_str:<48s} |{ascii_str}|")

def scan_for_strings(handle, addr, size=128, indent="    "):
    """Scan a data block for string pointers and inline BSTRs."""
    data = read_mem(handle, addr, size)
    if not data: return

    found = []
    # Check each 8-byte aligned offset for pointers
    for off in range(0, min(size, len(data)) - 7, 8):
        ptr = struct.unpack_from('<Q', data, off)[0]
        if is_valid_ptr(ptr):
            s = try_bstr_at(handle, ptr)
            if s:
                found.append((off, 'BSTR*', ptr, s))
                continue
            s = try_utf16_at(handle, ptr)
            if s and len(s) > 1:
                found.append((off, 'UTF16*', ptr, s))
                continue
            # Try pointer-to-pointer (double deref)
            ptr2 = read_ptr(handle, ptr)
            if is_valid_ptr(ptr2):
                s = try_bstr_at(handle, ptr2)
                if s:
                    found.append((off, 'BSTR**', ptr2, s))
                    continue
                s = try_utf16_at(handle, ptr2)
                if s and len(s) > 1:
                    found.append((off, 'UTF16**', ptr2, s))
                    continue

    # Check for inline BSTR (u32 length followed by UTF-16)
    for off in range(0, min(size, len(data)) - 5, 4):
        blen = struct.unpack_from('<I', data, off)[0]
        if 2 <= blen <= 200 and blen % 2 == 0 and off + 4 + blen <= len(data):
            try:
                s = data[off+4:off+4+blen].decode('utf-16-le')
                if all(c.isprintable() or c in ' \t\n\r' for c in s) and len(s) > 1:
                    found.append((off, 'inline_BSTR', addr + off, s))
            except: pass

    for off, kind, target, s in found:
        print(f"{indent}+{off:03d} {kind} -> 0x{target:012x}: \"{s}\"")

    return found


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

    # Read the 5 container pointers at correct offsets
    ptr_offsets = [116, 132, 148, 164, 180]
    container_ptrs = []
    for off in ptr_offsets:
        ptr = read_ptr(handle, prio_addr + off)
        container_ptrs.append((off, ptr))
        print(f"prio+{off}: 0x{ptr:016x} valid={is_valid_ptr(ptr)}")

    # Also read the u32 tags before each pointer
    for off in [112, 128, 144, 160, 176]:
        tag_data = read_mem(handle, prio_addr + off, 4)
        if tag_data:
            tag = struct.unpack('<I', tag_data)[0]
            print(f"prio+{off} tag: 0x{tag:08x}")

    # Analyze each container
    for off, ptr in container_ptrs:
        print(f"\n{'='*70}")
        print(f"CONTAINER at prio+{off} -> 0x{ptr:012x}")
        print(f"{'='*70}")

        if not is_valid_ptr(ptr):
            print("  INVALID PTR")
            continue

        cdata = read_mem(handle, ptr, 80)
        if not cdata:
            print("  UNREADABLE")
            continue

        dump_hex(cdata, ptr)

        # Parse container header
        sentinel = [struct.unpack_from('<Q', cdata, i*8)[0] for i in range(3)]
        type_tag = struct.unpack_from('<Q', cdata, 24)[0]
        count = struct.unpack_from('<I', cdata, 32)[0]
        cap = struct.unpack_from('<I', cdata, 36)[0]
        val40 = struct.unpack_from('<Q', cdata, 40)[0]
        data_a = struct.unpack_from('<Q', cdata, 48)[0]
        data_b = struct.unpack_from('<Q', cdata, 56)[0]

        print(f"\n  sentinel[0..2] all=self: {all(s == ptr for s in sentinel)}")
        print(f"  type_tag=0x{type_tag:016x}  count={count}  cap={cap}")
        print(f"  val40=0x{val40:016x}")
        print(f"  data_a=0x{data_a:016x} valid={is_valid_ptr(data_a)}")
        print(f"  data_b=0x{data_b:016x} valid={is_valid_ptr(data_b)}")

        # Follow data_a
        if is_valid_ptr(data_a):
            print(f"\n  --- data_a -> 0x{data_a:012x} ---")
            adata = read_mem(handle, data_a, 128)
            if adata:
                dump_hex(adata[:96], data_a, "    ")
                print("\n  String scan of data_a:")
                scan_for_strings(handle, data_a, 128, "    ")

                # Also follow pointers from data_a
                for doff in range(0, min(80, len(adata)) - 7, 8):
                    sub_ptr = struct.unpack_from('<Q', adata, doff)[0]
                    if is_valid_ptr(sub_ptr) and sub_ptr != data_a and sub_ptr != ptr:
                        sub_data = read_mem(handle, sub_ptr, 64)
                        if sub_data:
                            # Quick string check at sub
                            s = try_bstr_at(handle, sub_ptr)
                            if s:
                                print(f"    data_a+{doff} -> 0x{sub_ptr:012x} -> BSTR: \"{s}\"")
                            else:
                                print(f"    data_a+{doff} -> 0x{sub_ptr:012x}:")
                                dump_hex(sub_data[:48], sub_ptr, "      ")
                                scan_for_strings(handle, sub_ptr, 64, "      ")

        # Follow data_b
        if is_valid_ptr(data_b) and data_b != data_a:
            print(f"\n  --- data_b -> 0x{data_b:012x} ---")
            bdata = read_mem(handle, data_b, 128)
            if bdata:
                dump_hex(bdata[:96], data_b, "    ")
                print("\n  String scan of data_b:")
                scan_for_strings(handle, data_b, 128, "    ")

        # Also try reading val40 as a pointer
        if is_valid_ptr(val40):
            print(f"\n  --- val40 -> 0x{val40:012x} ---")
            v40data = read_mem(handle, val40, 64)
            if v40data:
                dump_hex(v40data[:48], val40, "    ")
                scan_for_strings(handle, val40, 64, "    ")

    # Reset
    task.Name = "Task 1"
    task.Contact = ""
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
