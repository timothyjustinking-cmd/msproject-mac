#!/usr/bin/env python3
"""Probe Resource and Assignment runtime storage.

Create a resource, assign it to a task, then search process memory
for the resource name and assignment data to trace the storage structure.

Also map the full prio region layout (prio-128 to prio+256) to find
remaining unknown fields.
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

    # ================================================================
    # PHASE 1: Map prio region fully (prio-128 to prio+256)
    # ================================================================
    print("=" * 80)
    print("PHASE 1: Full prio region dump (prio-128 to prio+256)")
    data = read_mem(handle, prio_addr - 128, 384)
    if data:
        for off in range(0, 384, 8):
            chunk = data[off:off+8]
            qval = struct.unpack_from('<Q', chunk, 0)[0]
            dval = struct.unpack_from('<d', chunk, 0)[0]
            u32a = struct.unpack_from('<I', chunk, 0)[0]
            u32b = struct.unpack_from('<I', chunk, 4)[0]
            rel = off - 128
            hex_str = ' '.join(f'{b:02x}' for b in chunk)

            annotation = ""
            if rel == 0:
                annotation = " <-- PRIORITY (u32)"
            elif rel == -8:
                annotation = " <-- prio-8"
            elif rel == 8:
                annotation = " <-- prio+8 (flags?)"
            elif 112 <= rel <= 192 and (rel - 112) % 16 < 8:
                slot = (rel - 112) // 16
                annotation = f" <-- container_slot[{slot}] tag"
            elif 112 <= rel <= 192 and (rel - 112) % 16 >= 8:
                slot = (rel - 116) // 16
                annotation = f" <-- container_slot[{slot}] ptr"

            if 0x023b00000000 < qval < 0x023c00000000:
                annotation += f" PTR->0x{qval:012x}"
            elif 0x140000000 < qval < 0x1c0000000:
                annotation += f" VTABLE"
            elif abs(dval) > 0 and abs(dval) < 1e15 and dval == dval:
                if dval != 0 and u32a != 0:
                    annotation += f" dbl={dval}"

            print(f"  prio{rel:+4d}: {hex_str}  u32=[{u32a:#010x}, {u32b:#010x}]{annotation}")

    # ================================================================
    # PHASE 2: Create a resource and assign it
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 2: Resource/Assignment probing")

    # Check existing resources
    res_count = proj.Resources.Count
    print(f"Existing resources: {res_count}")
    for i in range(1, min(res_count + 1, 10)):
        r = proj.Resources.Item(i)
        if r is not None:
            print(f"  [{i}] ID={r.ID} Name='{r.Name}' Type={r.Type}")

    # Create a distinctive resource
    res_name = "XYZZY_RES_PROBE_99"
    print(f"\nCreating resource '{res_name}'...")
    try:
        res = proj.Resources.Add(res_name)
        print(f"  Created: ID={res.ID}, UniqueID={res.UniqueID}")
    except Exception as e:
        print(f"  Create failed: {e}")
        # Try to find existing
        res = None
        for i in range(1, proj.Resources.Count + 1):
            r = proj.Resources.Item(i)
            if r and r.Name == res_name:
                res = r
                break
        if not res:
            print("  Could not create or find resource")
            CloseHandle(handle)
            return

    time.sleep(0.3)

    # Search for resource name in memory
    needle = res_name.encode('utf-16-le')
    print(f"\nSearching for resource name '{res_name}' as UTF-16LE...")
    hits = search_memory(handle, needle)
    print(f"Found {len(hits)} hits")
    for addr in hits:
        rel_prio = addr - prio_addr
        rel_tn = addr - tn_addr
        tag = ""
        if abs(rel_prio) < 0x100000:
            tag += f" prio{rel_prio:+d}"
        if 0 <= rel_tn <= 2000:
            tag += " INSIDE_TASKNODE"
        # Check surrounding context
        ctx = read_mem(handle, addr - 16, 80)
        if ctx:
            # Look for header pattern before the string
            hex_pre = ' '.join(f'{b:02x}' for b in ctx[:16])
            print(f"  0x{addr:012x}{tag}")
            print(f"    pre-16: [{hex_pre}]")

    # ================================================================
    # PHASE 3: Assign resource to task
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 3: Creating assignment")

    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    if not task:
        print("ERROR: No tasks found")
        CloseHandle(handle)
        return

    print(f"Task: '{task.Name}' (ID {task.ID})")

    # Read ResourceNames BEFORE assignment
    rn_before = task.ResourceNames
    print(f"ResourceNames before: '{rn_before}'")

    # Assign resource
    print(f"Assigning '{res_name}' to '{task.Name}'...")
    try:
        assign = task.Assignments.Add(task.ID, res.ID)
        print(f"  Assignment created")
        time.sleep(0.3)
    except Exception as e:
        print(f"  Assignment failed: {e}")
        # Try alternative method
        try:
            app.SelectRow(Row=1)
            task.ResourceNames = res_name
            time.sleep(0.3)
            print(f"  Assigned via ResourceNames property")
        except Exception as e2:
            print(f"  Also failed: {e2}")

    # Read after
    rn_after = task.ResourceNames
    print(f"ResourceNames after: '{rn_after}'")

    # Enumerate assignments
    try:
        assigns = task.Assignments
        print(f"Assignment count: {assigns.Count}")
        for j in range(1, assigns.Count + 1):
            a = assigns.Item(j)
            print(f"  [{j}] TaskID={a.TaskID} ResourceID={a.ResourceID} "
                  f"ResourceName='{a.ResourceName}' Units={a.Units} Work={a.Work}")
    except Exception as e:
        print(f"  Enumerate failed: {e}")

    # ================================================================
    # PHASE 4: Differential memory scan
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 4: Differential scan - what changed in prio region?")

    # Re-read prio region after assignment
    data_after = read_mem(handle, prio_addr - 128, 384)
    if data and data_after:
        print("Changes in prio-128 to prio+256:")
        changes = 0
        for off in range(0, 384, 8):
            before = data[off:off+8]
            after = data_after[off:off+8]
            if before != after:
                rel = off - 128
                bval = struct.unpack_from('<Q', before, 0)[0]
                aval = struct.unpack_from('<Q', after, 0)[0]
                hex_b = ' '.join(f'{b:02x}' for b in before)
                hex_a = ' '.join(f'{b:02x}' for b in after)
                print(f"  prio{rel:+4d}: {hex_b} -> {hex_a}")
                changes += 1
        if changes == 0:
            print("  No changes in prio flat buffer region!")
        print(f"  Total changed qwords: {changes}")

    # ================================================================
    # PHASE 5: Search for assignment object structure
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 5: Search for assignment-related data")

    # Search for the resource name again (should have more hits now)
    hits2 = search_memory(handle, needle)
    new_hits = [a for a in hits2 if a not in hits]
    print(f"New resource name hits after assignment: {len(new_hits)}")
    for addr in new_hits:
        print(f"  0x{addr:012x}")
        ctx = read_mem(handle, addr - 32, 96)
        if ctx:
            for coff in range(0, 96, 16):
                chunk = ctx[coff:coff+16]
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                tag = " <<<" if coff == 32 else ""
                print(f"    0x{addr-32+coff:012x}: {hex_str:<48s} |{ascii_str}|{tag}")

    # Check container at prio+164 (dependency graph) for changes
    print("\nContainer at prio+164 after assignment:")
    ptr164 = read_u64(handle, prio_addr + 164)
    if ptr164:
        cont = read_mem(handle, ptr164, 40)
        if cont:
            count1 = struct.unpack_from('<I', cont, 32)[0]
            count2 = struct.unpack_from('<I', cont, 36)[0]
            print(f"  Counts: {count1}, {count2}")

    # Check all 5 containers for count changes
    print("\nAll container counts after assignment:")
    for slot, off in enumerate([116, 132, 148, 164, 180]):
        ptr = read_u64(handle, prio_addr + off)
        if ptr:
            cont = read_mem(handle, ptr, 40)
            if cont:
                c1 = struct.unpack_from('<I', cont, 32)[0]
                c2 = struct.unpack_from('<I', cont, 36)[0]
                print(f"  Slot {slot} (prio+{off}): count={c1},{c2}")

    # ================================================================
    # PHASE 6: Resource object structure
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 6: Resource object inspection")

    # Find the resource's flat buffer (like prio for tasks)
    # Resources have their own storage - find via pointer near resource name hits
    for addr in hits2[:5]:
        # Check if there's a Priority-like value nearby
        # Resources have standard properties too
        ctx = read_mem(handle, addr - 128, 256)
        if ctx:
            # Look for pointers that could be the resource's prio equivalent
            ptrs_found = []
            for coff in range(0, 256, 8):
                qval = struct.unpack_from('<Q', ctx, coff)[0]
                if 0x023b00000000 < qval < 0x023c00000000:
                    ptrs_found.append((coff - 128, qval))
            if ptrs_found:
                print(f"\n  Near resource hit 0x{addr:012x}:")
                for roff, pval in ptrs_found[:8]:
                    print(f"    offset {roff:+4d}: ptr 0x{pval:012x}")

    # ================================================================
    # PHASE 7: Check TASKNODE for assignment data
    # ================================================================
    print("\n" + "=" * 80)
    print("PHASE 7: TASKNODE changes after assignment")

    tn_data = read_mem(handle, tn_addr, 16)
    if tn_data:
        tn_size = struct.unpack_from('<I', tn_data, 0)[0]
        tn_count = struct.unpack_from('<I', tn_data, 4)[0]
        print(f"TASKNODE: size={tn_size}, count={tn_count}")

        # Read full TASKNODE and look for new field IDs
        full_tn = read_mem(handle, tn_addr, min(tn_size + 8, 4096))
        if full_tn:
            print(f"TASKNODE index (first 30 entries):")
            for i in range(min(tn_count, 30)):
                idx_off = 8 + i * 8
                fid = struct.unpack_from('<I', full_tn, idx_off)[0]
                foff = struct.unpack_from('<I', full_tn, idx_off + 4)[0]
                data_off = 8 + tn_count * 8 + foff
                if data_off + 8 <= len(full_tn):
                    raw = full_tn[data_off:data_off+8]
                    hex_str = ' '.join(f'{b:02x}' for b in raw)
                    dval = struct.unpack_from('<d', raw, 0)[0] if len(raw) >= 8 else 0
                    print(f"  [{i:2d}] fid=0x{fid:04x} ({fid:5d}) off={foff:4d} -> [{hex_str}] dbl={dval:.4g}")

    # ================================================================
    # CLEANUP
    # ================================================================
    print("\n" + "=" * 80)
    print("CLEANUP")

    # Remove assignment
    try:
        assigns = task.Assignments
        for j in range(assigns.Count, 0, -1):
            assigns.Item(j).Delete()
        print("  Assignments removed")
    except Exception as e:
        print(f"  Remove assignments failed: {e}")

    # Remove resource
    try:
        # Find and delete the test resource
        for i in range(proj.Resources.Count, 0, -1):
            r = proj.Resources.Item(i)
            if r and r.Name == res_name:
                r.Delete()
                print(f"  Resource '{res_name}' deleted")
                break
    except Exception as e:
        print(f"  Remove resource failed: {e}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
