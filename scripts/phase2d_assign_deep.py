#!/usr/bin/env python3
"""Deep-dive into assignment object structure.

Focus on:
1. What's at prio+216 (pointer that changed when assignment was created)
2. Decode the 0x41424F80 value at prio-80/-64
3. Trace the full assignment object layout
4. Map the TASKNODE before/after differential
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

def read_u32(handle, addr):
    d = read_mem(handle, addr, 4)
    if not d or len(d) < 4: return None
    return struct.unpack('<I', d)[0]

def read_u64(handle, addr):
    d = read_mem(handle, addr, 8)
    if not d or len(d) < 8: return None
    return struct.unpack('<Q', d)[0]

def read_double(handle, addr):
    d = read_mem(handle, addr, 8)
    if not d or len(d) < 8: return None
    return struct.unpack('<d', d)[0]

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

def dump_region(handle, addr, size, label="", base_label=0):
    data = read_mem(handle, addr, size)
    if not data:
        print(f"  Can't read 0x{addr:012x}")
        return None
    if label:
        print(f"\n{label} (0x{addr:012x}):")
    for off in range(0, min(len(data), size), 16):
        chunk = data[off:off+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"  +{off + base_label:4d}: {hex_str:<48s} |{ascii_str}|")
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

    prio_addr = 0x23b7ef6dd3c
    tn_addr = 0x23b6384bad0

    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    # ================================================================
    # PHASE 1: Snapshot BEFORE assignment
    # ================================================================
    print("=" * 80)
    print("PHASE 1: Snapshot before assignment")

    # Read flat buffer region
    buf_before = read_mem(handle, prio_addr - 128, 384)

    # Read TASKNODE
    tn_header = read_mem(handle, tn_addr, 8)
    tn_size_before = struct.unpack_from('<I', tn_header, 0)[0] if tn_header else 0
    tn_count_before = struct.unpack_from('<I', tn_header, 4)[0] if tn_header else 0
    tn_before = read_mem(handle, tn_addr, min(tn_size_before + 8, 4096))
    print(f"TASKNODE before: size={tn_size_before}, count={tn_count_before}")

    # Read prio+216 pointer
    ptr216_before = read_u64(handle, prio_addr + 216)
    print(f"prio+216 before: 0x{ptr216_before:012x}" if ptr216_before else "prio+216: None")

    # Follow prio+216
    if ptr216_before and 0x023b00000000 < ptr216_before < 0x023c00000000:
        dump_region(handle, ptr216_before, 128, "Object at prio+216 (before)")

    # Read some COM values for correlation
    print(f"\nTask '{task.Name}' values before assignment:")
    print(f"  Work = {task.Work}")
    print(f"  Duration = {task.Duration}")
    print(f"  Start = {task.Start}")
    print(f"  Finish = {task.Finish}")
    print(f"  ResourceNames = '{task.ResourceNames}'")
    print(f"  Assignments count = {task.Assignments.Count}")

    # ================================================================
    # PHASE 2: Decode the values at prio-80..prio-56
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 2: Decode values in zero-padded region (prio-128 to prio-8)")

    if buf_before:
        for off_from_prio in range(-128, 0, 4):
            buf_off = off_from_prio + 128
            if buf_off + 4 <= len(buf_before):
                u32 = struct.unpack_from('<I', buf_before, buf_off)[0]
                if u32 != 0 and u32 != 0xFFFFFFFF:
                    # Try to decode
                    tag = ""
                    if off_from_prio % 8 == 0 and buf_off + 8 <= len(buf_before):
                        dval = struct.unpack_from('<d', buf_before, buf_off)[0]
                        if abs(dval) > 0 and abs(dval) < 1e15:
                            tag += f" dbl={dval}"
                        i64 = struct.unpack_from('<q', buf_before, buf_off)[0]
                        tag += f" i64={i64}"
                    print(f"  prio{off_from_prio:+4d}: u32=0x{u32:08x} ({u32}){tag}")

    # ================================================================
    # PHASE 3: Create assignment
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 3: Creating resource + assignment")

    res_name = "PROBE_ASSIGN_XQ77"
    try:
        res = proj.Resources.Add(res_name)
        print(f"Resource created: ID={res.ID}")
    except:
        res = None
        for i in range(1, proj.Resources.Count + 1):
            r = proj.Resources.Item(i)
            if r and r.Name == res_name:
                res = r
                break

    if not res:
        print("ERROR: Can't create resource")
        CloseHandle(handle)
        return

    # Create assignment
    try:
        task.Assignments.Add(task.ID, res.ID)
        time.sleep(0.5)
        print(f"Assignment created. Work={task.Work} Duration={task.Duration}")
    except Exception as e:
        print(f"Assignment failed: {e}")
        try:
            task.ResourceNames = res_name
            time.sleep(0.5)
        except:
            pass

    # ================================================================
    # PHASE 4: Snapshot AFTER assignment
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 4: Snapshot after assignment")

    buf_after = read_mem(handle, prio_addr - 128, 384)

    # Flat buffer diff
    if buf_before and buf_after:
        print("Flat buffer changes:")
        for off in range(0, 384, 4):
            u32_b = struct.unpack_from('<I', buf_before, off)[0]
            u32_a = struct.unpack_from('<I', buf_after, off)[0]
            if u32_b != u32_a:
                rel = off - 128
                # Show as u32 and try double at aligned offsets
                tag = ""
                if off % 8 == 0 and off + 8 <= 384:
                    dval_b = struct.unpack_from('<d', buf_before, off)[0]
                    dval_a = struct.unpack_from('<d', buf_after, off)[0]
                    tag = f" dbl: {dval_b} -> {dval_a}"
                print(f"  prio{rel:+4d}: 0x{u32_b:08x} -> 0x{u32_a:08x}{tag}")

    # TASKNODE diff
    tn_header2 = read_mem(handle, tn_addr, 8)
    tn_size_after = struct.unpack_from('<I', tn_header2, 0)[0] if tn_header2 else 0
    tn_count_after = struct.unpack_from('<I', tn_header2, 4)[0] if tn_header2 else 0
    print(f"\nTASKNODE after: size={tn_size_after}, count={tn_count_after}")
    print(f"  Size delta: {tn_size_after - tn_size_before}")
    print(f"  Count delta: {tn_count_after - tn_count_before}")

    if tn_count_after > tn_count_before:
        tn_after = read_mem(handle, tn_addr, min(tn_size_after + 8, 4096))
        if tn_after and tn_before:
            # Show new field IDs
            old_fids = set()
            for i in range(tn_count_before):
                idx_off = 8 + i * 8
                fid = struct.unpack_from('<I', tn_before, idx_off)[0]
                old_fids.add(fid)

            print("  New TASKNODE field IDs:")
            for i in range(tn_count_after):
                idx_off = 8 + i * 8
                fid = struct.unpack_from('<I', tn_after, idx_off)[0]
                foff = struct.unpack_from('<I', tn_after, idx_off + 4)[0]
                if fid not in old_fids:
                    data_off = 8 + tn_count_after * 8 + foff
                    if data_off + 8 <= len(tn_after):
                        raw = tn_after[data_off:data_off+8]
                        hex_str = ' '.join(f'{b:02x}' for b in raw)
                        dval = struct.unpack_from('<d', raw, 0)[0] if len(raw) >= 8 else 0
                        print(f"    fid=0x{fid:04x} ({fid:5d}) off={foff:4d} -> [{hex_str}] dbl={dval:.6g}")

    # Follow prio+216
    ptr216_after = read_u64(handle, prio_addr + 216)
    print(f"\nprio+216 after: 0x{ptr216_after:012x}" if ptr216_after else "prio+216: None")
    if ptr216_after and 0x023b00000000 < ptr216_after < 0x023c00000000:
        dump_region(handle, ptr216_after, 128, "Object at prio+216 (after)")

    # ================================================================
    # PHASE 5: Follow the assignment chain
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 5: Assignment chain trace")

    # Search for assignment-specific data
    # The assignment's Work = 2400.0 (40 hours). Search for this as double
    work_dbl = struct.pack('<d', 2400.0)
    work_hits = search_memory(handle, work_dbl, 20)
    print(f"Work=2400.0 as double: {len(work_hits)} hits")
    for addr in work_hits:
        rel = addr - prio_addr
        tag = f" prio{rel:+d}" if abs(rel) < 0x100000 else ""
        print(f"  0x{addr:012x}{tag}")

    # Decode the mystery value at prio-80
    print("\nDecoding mystery value 0x41424F80:")
    val = 0x41424F80
    print(f"  As u32: {val}")
    print(f"  As i32: {ctypes.c_int32(val).value}")
    print(f"  As float: {struct.unpack('<f', struct.pack('<I', val))[0]}")

    # Check if it's a duration/date in internal format
    # MS Project dates: minutes since 1/1/1984 in some formats
    # but maybe this is in a different unit
    f32 = struct.unpack('<f', struct.pack('<I', val))[0]
    print(f"  Float value: {f32}")
    print(f"  If date-minutes/10 from 1984: {val / 10 / 60 / 24 / 365.25:.1f} years from 1984")

    # Check if Work in 1/10 minutes is stored nearby in the flat buffer
    # Work = 2400 min -> 24000 in 1/10 min = 0x5DC0
    work_tenth = 24000
    print(f"\nSearching flat buffer for Work={work_tenth} (0x{work_tenth:08x}) as u32...")
    if buf_after:
        for off in range(0, 384, 4):
            u32 = struct.unpack_from('<I', buf_after, off)[0]
            if u32 == work_tenth:
                print(f"  Found at prio{off - 128:+d}")

    # Also check if 0x41424F80 appears as a double somewhere meaningful
    # What if prio-80 is split into two separate fields at prio-80 and prio-76?
    print("\nFlat buffer -80 region in detail (u32 aligned):")
    if buf_after:
        for off_from_prio in range(-88, -48, 4):
            buf_off = off_from_prio + 128
            u32 = struct.unpack_from('<I', buf_after, buf_off)[0]
            print(f"  prio{off_from_prio:+4d}: u32=0x{u32:08x} ({u32})")

    # ================================================================
    # PHASE 6: Check container slot[0] tag more carefully
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 6: Container analysis post-assignment")

    # The tag at prio+112 changed from 0x00600003 to 0x00600004
    tag_before = struct.unpack_from('<I', buf_before, 112 + 128)[0] if buf_before else 0
    tag_after = struct.unpack_from('<I', buf_after, 112 + 128)[0] if buf_after else 0
    print(f"Slot[0] tag: 0x{tag_before:08x} -> 0x{tag_after:08x}")
    print(f"  Low 16 bits (counter?): {tag_before & 0xFFFF} -> {tag_after & 0xFFFF}")
    print(f"  High 16 bits (type?): 0x{tag_before >> 16:04x} -> 0x{tag_after >> 16:04x}")

    # Check all 5 containers' full state
    for slot, off in enumerate([116, 132, 148, 164, 180]):
        ptr = read_u64(handle, prio_addr + off)
        if ptr and 0x023b00000000 < ptr < 0x023c00000000:
            cont = read_mem(handle, ptr, 40)
            if cont:
                c1 = struct.unpack_from('<I', cont, 32)[0]
                c2 = struct.unpack_from('<I', cont, 36)[0]
                print(f"  Slot {slot} (prio+{off}): count={c1},{c2} ptr=0x{ptr:012x}")

    # ================================================================
    # PHASE 7: Trace assignment object from new memory hits
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 7: Tracing assignment object")

    # Search for resource name
    needle = res_name.encode('utf-16-le')
    res_hits = search_memory(handle, needle)
    print(f"Resource name hits: {len(res_hits)}")

    for addr in res_hits:
        # Read 128 bytes before the string to find the object header
        ctx = read_mem(handle, addr - 128, 256)
        if not ctx:
            continue

        print(f"\nHit at 0x{addr:012x}:")
        # Look for pointers in the 128 bytes before the string
        ptrs = []
        for coff in range(0, 128, 8):
            qval = struct.unpack_from('<Q', ctx, coff)[0]
            if 0x023b00000000 < qval < 0x023c00000000:
                ptrs.append((coff - 128, qval))
            elif 0x140000000 < qval < 0x1c0000000:
                ptrs.append((coff - 128, f"VTABLE 0x{qval:012x}"))

        if ptrs:
            for roff, pval in ptrs:
                if isinstance(pval, str):
                    print(f"  offset {roff:+4d}: {pval}")
                else:
                    print(f"  offset {roff:+4d}: ptr 0x{pval:012x}")
                    # Dereference and show first 16 bytes
                    sub = read_mem(handle, pval, 16)
                    if sub:
                        hex_str = ' '.join(f'{b:02x}' for b in sub)
                        print(f"            -> [{hex_str}]")

        # Show 32 bytes before the string
        pre = ctx[96:128]
        hex_str = ' '.join(f'{b:02x}' for b in pre)
        print(f"  pre-32: [{hex_str}]")
        # Show immediate prefix (likely string header)
        pre16 = ctx[112:128]
        hex_str = ' '.join(f'{b:02x}' for b in pre16)
        print(f"  pre-16: [{hex_str}]")
        # Parse counted string header
        if len(pre16) >= 16:
            u32_0 = struct.unpack_from('<I', pre16, 0)[0]
            u32_4 = struct.unpack_from('<I', pre16, 4)[0]
            u32_8 = struct.unpack_from('<I', pre16, 8)[0]
            u32_c = struct.unpack_from('<I', pre16, 12)[0]
            expected_chars = len(res_name)
            expected_bytes = expected_chars * 2
            if u32_4 == expected_chars or u32_8 == expected_chars or u32_c == expected_chars:
                print(f"  -> Counted string: char_count matches at one of the u32 positions")
            if u32_4 == expected_bytes or u32_8 == expected_bytes or u32_c == expected_bytes:
                print(f"  -> Counted string: byte_count matches at one of the u32 positions")
            print(f"  -> u32s: [{u32_0:#x}, {u32_4:#x}, {u32_8:#x}, {u32_c:#x}]")
            print(f"  -> Expected: char_count={expected_chars}, byte_count={expected_bytes}")

    # ================================================================
    # CLEANUP
    # ================================================================
    print("\n" + "=" * 80)
    print("CLEANUP")

    try:
        assigns = task.Assignments
        for j in range(assigns.Count, 0, -1):
            assigns.Item(j).Delete()
        print("  Assignments removed")
    except Exception as e:
        print(f"  Remove assignments failed: {e}")

    try:
        for i in range(proj.Resources.Count, 0, -1):
            r = proj.Resources.Item(i)
            if r and r.Name == res_name:
                r.Delete()
                print(f"  Resource deleted")
                break
    except Exception as e:
        print(f"  Remove resource failed: {e}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
