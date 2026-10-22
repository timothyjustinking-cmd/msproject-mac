#!/usr/bin/env python3
"""Comprehensive mapping of the flat buffer around Priority.

Known locations in flat buffer:
  0x23b7ef6dd08: FixedCost (dbl*100)
  0x23b7ef6dd3c: Priority (i16)

Strategy: Set multiple fields to distinctive values, then dump the flat buffer
to find all of them at once.
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

def get_image_base(pid):
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    hProcess = OpenProcess(0x0410 | 0x0010, False, pid)
    if not hProcess: return None
    hMods = (ctypes.c_void_p * 1024)()
    cbNeeded = ctypes.c_ulong()
    if not psapi.EnumProcessModulesEx(hProcess, ctypes.byref(hMods), ctypes.sizeof(hMods),
                                       ctypes.byref(cbNeeded), 0x03):
        CloseHandle(hProcess); return None
    base = hMods[0]
    CloseHandle(hProcess)
    return base

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    ok = ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n))
    if not ok: return None
    return buf.raw[:n.value]


def main():
    pid = get_pid()
    if not pid:
        print("ERROR: WINPROJ.EXE not running"); sys.exit(1)

    IMAGE_BASE = get_image_base(pid)
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

    print(f"PID: {pid}, Image base: {IMAGE_BASE:#x}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    prio_addr = 0x23b7ef6dd3c  # Priority (i16)
    fixedcost_addr = 0x23b7ef6dd08  # FixedCost (dbl*100)

    # Step 1: Set distinctive values in many fields
    print(f"\n=== Setting probe values ===")
    task.Duration = 2400  # 5 days (reset to known state)
    task.Priority = 777
    task.FixedCost = 54321.0     # *100 = 5432100.0
    task.PercentComplete = 67    # *10 = 670
    task.Marked = True           # 1 or -1
    task.Milestone = True        # 1 or -1
    task.HideBar = True
    try:
        task.ConstraintType = 1  # Must Finish On
    except:
        pass
    time.sleep(0.5)

    print(f"  Priority = {task.Priority}")
    print(f"  FixedCost = {task.FixedCost}")
    print(f"  PercentComplete = {task.PercentComplete}")
    print(f"  Marked = {task.Marked}")
    print(f"  Milestone = {task.Milestone}")
    print(f"  HideBar = {task.HideBar}")
    print(f"  Duration = {task.Duration}")

    # Step 2: Read a large window around the flat buffer
    buffer_start = prio_addr - 0x200  # 512 bytes before Priority
    buffer_size = 0x600               # 1536 bytes total
    data = read_mem(handle, buffer_start, buffer_size)
    if not data:
        print("FAILED to read flat buffer")
        return

    print(f"\n=== FLAT BUFFER DUMP ({buffer_start:#x} to {buffer_start + buffer_size:#x}) ===")
    print(f"  Priority at offset +{prio_addr - buffer_start:#x} from dump start")
    print(f"  FixedCost at offset +{fixedcost_addr - buffer_start:#x} from dump start")

    # Step 3: Search for probe values in the buffer
    probe_values = [
        ("Priority i16=777",      struct.pack('<h', 777)),
        ("Priority i32=777",      struct.pack('<i', 777)),
        ("FixedCost dbl*100",     struct.pack('<d', 5432100.0)),
        ("FixedCost dbl raw",     struct.pack('<d', 54321.0)),
        ("PercComplete i16=670",  struct.pack('<h', 670)),
        ("PercComplete i16=67",   struct.pack('<h', 67)),
        ("Duration i32=2400",     struct.pack('<i', 2400)),
        ("Duration i16=2400",     struct.pack('<h', 2400)),
        ("Marked i16=-1",         struct.pack('<h', -1)),
        ("Marked i16=1",          struct.pack('<h', 1)),
        ("Marked i32=-1",         struct.pack('<i', -1)),
        ("Milestone i16=-1",      struct.pack('<h', -1)),
        ("Milestone i16=1",       struct.pack('<h', 1)),
        ("HideBar i16=-1",        struct.pack('<h', -1)),
        ("HideBar i16=1",         struct.pack('<h', 1)),
        ("ConstraintType i16=1",  struct.pack('<h', 1)),
        ("Date 0x3d12xxxx",       None),  # special: scan for dates
    ]

    print(f"\n=== SEARCHING FOR PROBE VALUES ===")
    for label, pattern in probe_values:
        if pattern is None:
            # Date scan
            for off in range(0, len(data) - 4, 2):
                val = struct.unpack_from('<I', data, off)[0]
                if 0x3d100000 <= val <= 0x3d200000:
                    abs_addr = buffer_start + off
                    rel = abs_addr - prio_addr
                    print(f"  Date at {abs_addr:#x} (prio{rel:+d}): {val:#x}")
            continue

        pos = 0
        while True:
            idx = data.find(pattern, pos)
            if idx == -1:
                break
            abs_addr = buffer_start + idx
            rel = abs_addr - prio_addr
            # Check if this is in the non-zero data area (skip areas of all zeros)
            ctx = data[max(0,idx-4):min(len(data),idx+len(pattern)+4)]
            ctx_hex = ' '.join(f'{b:02x}' for b in ctx)
            print(f"  {label} at {abs_addr:#x} (prio{rel:+d}): [{ctx_hex}]")
            pos = idx + 1

    # Step 4: Annotated hex dump of the data area
    print(f"\n=== ANNOTATED HEX DUMP ===")
    # Only show non-zero regions
    for off in range(0, len(data), 16):
        chunk = data[off:off+16]
        if all(b == 0 for b in chunk):
            continue
        abs_addr = buffer_start + off
        rel = abs_addr - prio_addr
        hex_str = ' '.join(f'{b:02x}' for b in chunk)

        # Annotate known values
        annotations = []
        if abs_addr <= prio_addr < abs_addr + 16:
            annotations.append("<<PRIORITY>>")
        if abs_addr <= fixedcost_addr < abs_addr + 16:
            annotations.append("<<FIXEDCOST>>")

        # Check for code/heap pointers
        for j in range(0, min(16, len(chunk)) - 7, 8):
            val = struct.unpack_from('<Q', chunk, j)[0]
            if IMAGE_BASE < val < IMAGE_BASE + 0x2000000:
                rva = val - IMAGE_BASE
                annotations.append(f"code_ptr(RVA {rva:#x})")
            elif 0x200000000000 < val < 0x800000000000:
                annotations.append(f"heap_ptr({val:#x})")

        ann = ' '.join(annotations)
        print(f"  {abs_addr:#x} (prio{rel:+d}): {hex_str}  {ann}")

    # Step 5: Now do a diff — change Priority to 500 and see what changes in the buffer
    print(f"\n=== DIFFERENTIAL: Priority 777 -> 500 ===")
    before = read_mem(handle, buffer_start, buffer_size)
    task.Priority = 500
    time.sleep(0.3)
    after = read_mem(handle, buffer_start, buffer_size)

    if before and after:
        for i in range(min(len(before), len(after))):
            if before[i] != after[i]:
                abs_addr = buffer_start + i
                rel = abs_addr - prio_addr
                print(f"  Changed at {abs_addr:#x} (prio{rel:+d}): {before[i]:#04x} -> {after[i]:#04x}")

    # Step 6: Change FixedCost to 0 and see what changes
    print(f"\n=== DIFFERENTIAL: FixedCost 54321 -> 0 ===")
    before = read_mem(handle, buffer_start, buffer_size)
    task.FixedCost = 0.0
    time.sleep(0.3)
    after = read_mem(handle, buffer_start, buffer_size)

    if before and after:
        for i in range(min(len(before), len(after))):
            if before[i] != after[i]:
                abs_addr = buffer_start + i
                rel = abs_addr - prio_addr
                print(f"  Changed at {abs_addr:#x} (prio{rel:+d}): {before[i]:#04x} -> {after[i]:#04x}")

    # Step 7: Change Marked to False and see what changes
    print(f"\n=== DIFFERENTIAL: Marked True -> False ===")
    before = read_mem(handle, buffer_start, buffer_size)
    task.Marked = False
    time.sleep(0.3)
    after = read_mem(handle, buffer_start, buffer_size)

    if before and after:
        for i in range(min(len(before), len(after))):
            if before[i] != after[i]:
                abs_addr = buffer_start + i
                rel = abs_addr - prio_addr
                print(f"  Changed at {abs_addr:#x} (prio{rel:+d}): {before[i]:#04x} -> {after[i]:#04x}")

    # Step 8: Change Milestone to False
    print(f"\n=== DIFFERENTIAL: Milestone True -> False ===")
    before = read_mem(handle, buffer_start, buffer_size)
    task.Milestone = False
    time.sleep(0.3)
    after = read_mem(handle, buffer_start, buffer_size)

    if before and after:
        for i in range(min(len(before), len(after))):
            if before[i] != after[i]:
                abs_addr = buffer_start + i
                rel = abs_addr - prio_addr
                print(f"  Changed at {abs_addr:#x} (prio{rel:+d}): {before[i]:#04x} -> {after[i]:#04x}")

    # Reset everything
    task.Priority = 500
    task.FixedCost = 0.0
    task.PercentComplete = 0
    task.Marked = False
    task.Milestone = False
    task.HideBar = False
    try:
        task.ConstraintType = 0
    except:
        pass
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
