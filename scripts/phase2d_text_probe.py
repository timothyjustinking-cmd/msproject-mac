#!/usr/bin/env python3
"""Map text/string field storage in the flat buffer.

Known: heap pointers at prio+112 to prio+192.
Text fields: Name, Notes, Contact, Hyperlink, etc.

Strategy: Read pointers, set distinctive text values, see what changes.
Then dereference pointers to see string encoding.
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


def read_ptrs(handle, prio_addr, start_rel, count):
    addr = prio_addr + start_rel
    data = read_mem(handle, addr, count * 8)
    if not data:
        return []
    result = []
    for i in range(count):
        val = struct.unpack_from('<Q', data, i * 8)[0]
        rel = start_rel + i * 8
        result.append((rel, val))
    return result


def deref_string(handle, ptr, max_len=200):
    """Try to read a string at the given pointer."""
    if ptr == 0 or ptr > 0x7FFFFFFFFFFF:
        return None

    data = read_mem(handle, ptr, max_len * 2 + 4)
    if not data:
        return None

    # Try UTF-16LE (common for Windows COM strings)
    try:
        # Look for null terminator in UTF-16
        for i in range(0, len(data) - 1, 2):
            if data[i] == 0 and data[i+1] == 0:
                return data[:i].decode('utf-16-le')
        return data[:min(60, len(data))].decode('utf-16-le', errors='replace')
    except:
        pass

    # Try ASCII/ANSI
    try:
        for i in range(len(data)):
            if data[i] == 0:
                return data[:i].decode('ascii')
        return data[:60].decode('ascii', errors='replace')
    except:
        return None


def dump_pointer_region(handle, prio_addr, label, start=80, end=256, task=None):
    print(f"\n=== {label} ===")
    ptrs = read_ptrs(handle, prio_addr, start, (end - start) // 8)
    for rel, ptr in ptrs:
        if ptr != 0:
            # Try to dereference and read string
            s = deref_string(handle, ptr)
            if s:
                print(f"  prio+{rel:3d}: 0x{ptr:016x} -> \"{s[:60]}\"")
            else:
                print(f"  prio+{rel:3d}: 0x{ptr:016x} (not a readable string)")
        else:
            print(f"  prio+{rel:3d}: NULL")


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

    print(f"Task: ID={task.ID} Name='{task.Name}'")
    prio_addr = 0x23b7ef6dd3c

    # Reset
    task.Duration = 2400
    task.PercentComplete = 0
    task.FixedCost = 0.0
    time.sleep(0.3)

    # Step 1: Dump pointer region with current Name
    print(f"\n--- COM Text Values ---")
    text_props = ['Name', 'Notes', 'Contact', 'HyperlinkAddress',
                  'HyperlinkSubAddress', 'HyperlinkHref', 'WBS', 'OutlineNumber']
    for prop in text_props:
        try:
            val = getattr(task, prop)
            val_str = str(val)[:60] if val else "(empty)"
            print(f"  {prop:25s} = {val_str}")
        except Exception as e:
            print(f"  {prop:25s} = ERROR: {e}")

    # Wider scan to find all pointers
    print("\n--- Pointer scan (prio+16 to prio+300) ---")
    dump_pointer_region(handle, prio_addr, "STATE 1: Original Name", 16, 300, task)

    # Step 2: Change Name to something distinctive
    task.Name = "XYZZY_TEST_12345"
    time.sleep(0.3)

    dump_pointer_region(handle, prio_addr, "STATE 2: After Name='XYZZY_TEST_12345'", 16, 300, task)

    # Step 3: Set Notes
    task.Notes = "NOTES_PROBE_67890"
    time.sleep(0.3)

    dump_pointer_region(handle, prio_addr, "STATE 3: After Notes='NOTES_PROBE_67890'", 16, 300, task)

    # Step 4: Set Contact
    task.Contact = "CONTACT_ABCDE"
    time.sleep(0.3)

    dump_pointer_region(handle, prio_addr, "STATE 4: After Contact='CONTACT_ABCDE'", 16, 300, task)

    # Step 5: Set WBS
    # WBS is typically auto-generated but can be set
    try:
        task.WBS = "1.2.3.4"
        time.sleep(0.3)
        dump_pointer_region(handle, prio_addr, "STATE 5: After WBS='1.2.3.4'", 16, 300, task)
    except Exception as e:
        print(f"\nWBS set failed: {e}")

    # Step 6: Set Hyperlink fields
    try:
        task.HyperlinkAddress = "https://example.com/test"
        time.sleep(0.3)
        dump_pointer_region(handle, prio_addr, "STATE 6: After HyperlinkAddress", 16, 300, task)
    except Exception as e:
        print(f"\nHyperlinkAddress set failed: {e}")

    # Step 7: Also scan the region BEFORE Priority for any text pointers
    print("\n--- Pointer scan (prio-280 to prio-200) ---")
    ptrs = read_ptrs(handle, prio_addr, -280, 10)
    for rel, ptr in ptrs:
        if ptr != 0:
            s = deref_string(handle, ptr)
            if s:
                print(f"  prio{rel:+4d}: 0x{ptr:016x} -> \"{s[:60]}\"")
            else:
                print(f"  prio{rel:+4d}: 0x{ptr:016x}")

    # Clean up
    task.Name = "Task 1"
    task.Notes = ""
    task.Contact = ""
    try:
        task.HyperlinkAddress = ""
    except:
        pass
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
