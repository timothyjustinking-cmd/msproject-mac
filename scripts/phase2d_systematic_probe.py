#!/usr/bin/env python3
"""Systematically probe class 1 task fields in the flat buffer.

For each COM-settable field: set distinctive value, read flat buffer, find it.
This maps class_slot -> flat buffer offset for all accessible fields.

Known anchor: Priority (i16, class_slot=13) at prio_addr = 0x23b7ef6dd3c
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


def diff_buffers(before, after, base_addr, prio_addr):
    """Return list of (abs_addr, prio_offset, old_byte, new_byte)."""
    changes = []
    for i in range(min(len(before), len(after))):
        if before[i] != after[i]:
            addr = base_addr + i
            rel = addr - prio_addr
            changes.append((addr, rel, before[i], after[i]))
    return changes


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

    print(f"PID: {pid}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    prio_addr = 0x23b7ef6dd3c
    buf_start = prio_addr - 0x300  # 768 bytes before
    buf_size = 0x800               # 2048 bytes total

    # Verify Priority is still at prio_addr
    prio_data = read_mem(handle, prio_addr, 2)
    if prio_data:
        cur_prio = struct.unpack('<h', prio_data)[0]
        print(f"Current Priority at {prio_addr:#x}: {cur_prio}")
        if cur_prio != task.Priority:
            print(f"WARNING: Mismatch! COM says {task.Priority}")
            print("Flat buffer may have moved. Trying to re-find...")
            # Set a distinctive priority and search
            old_prio = task.Priority
            task.Priority = 777
            time.sleep(0.3)
            # Search in a wider region
            for search_start in range(prio_addr - 0x100000, prio_addr + 0x100000, 0x10000):
                data = read_mem(handle, search_start, 0x10000)
                if not data:
                    continue
                pat = struct.pack('<h', 777)
                pos = 0
                while True:
                    idx = data.find(pat, pos)
                    if idx == -1:
                        break
                    # Check if prio+2 has PercentComplete (whatever it is)
                    candidate = search_start + idx
                    # Verify: change Priority to 500 and confirm
                    task.Priority = 500
                    time.sleep(0.1)
                    check = read_mem(handle, candidate, 2)
                    if check and struct.unpack('<h', check)[0] == 500:
                        task.Priority = 777
                        time.sleep(0.1)
                        recheck = read_mem(handle, candidate, 2)
                        if recheck and struct.unpack('<h', recheck)[0] == 777:
                            prio_addr = candidate
                            buf_start = prio_addr - 0x300
                            print(f"FOUND Priority at new address: {prio_addr:#x}")
                            break
                    task.Priority = 777
                    time.sleep(0.1)
                    pos = idx + 2
                else:
                    continue
                break
            task.Priority = old_prio
            time.sleep(0.3)

    # Reset to clean state
    print("\n=== Resetting to clean state ===")
    task.Duration = 2400    # 5 days
    task.Priority = 500
    task.PercentComplete = 0
    task.FixedCost = 0.0
    task.Marked = False
    task.Milestone = False
    task.HideBar = False
    try:
        task.ConstraintType = 0  # ASAP
    except:
        pass
    time.sleep(0.5)

    # Read baseline buffer
    baseline = read_mem(handle, buf_start, buf_size)
    if not baseline:
        print("FAILED to read baseline buffer")
        return

    # Probe each field type
    results = []

    # === COST FIELDS (type=5, doubles, class_slot 0-6) ===
    # These are i64/double getter (0x7ddf30), stored as dbl*100
    cost_probes = [
        # (name, class_slot, set_func, reset_func, search_pattern_func)
        ("FixedCost", 5,
         lambda t: setattr(t, 'FixedCost', 77777.0),
         lambda t: setattr(t, 'FixedCost', 0.0),
         lambda: struct.pack('<d', 7777700.0)),  # *100
    ]

    # Try to set Work (requires resource assignment - may fail)
    # Skip Work-related fields for now

    print("\n=== PROBING COST FIELDS ===")
    for name, slot, set_fn, reset_fn, pat_fn in cost_probes:
        before = read_mem(handle, buf_start, buf_size)
        set_fn(task)
        time.sleep(0.3)
        after = read_mem(handle, buf_start, buf_size)

        # Search for the pattern
        pattern = pat_fn()
        idx = after.find(pattern)
        if idx >= 0:
            addr = buf_start + idx
            rel = addr - prio_addr
            print(f"  {name} (slot {slot}): dbl*100 at prio{rel:+d} ({addr:#x})")
            results.append((name, slot, rel, 'dbl*100'))

        # Also show all changed bytes
        changes = diff_buffers(before, after, buf_start, prio_addr)
        if changes:
            # Group contiguous changes
            groups = []
            current_group = [changes[0]]
            for c in changes[1:]:
                if c[0] == current_group[-1][0] + 1:
                    current_group.append(c)
                else:
                    groups.append(current_group)
                    current_group = [c]
            groups.append(current_group)

            for g in groups:
                start_rel = g[0][1]
                end_rel = g[-1][1]
                old_bytes = bytes(c[2] for c in g)
                new_bytes = bytes(c[3] for c in g)
                print(f"    changed prio{start_rel:+d} to prio{end_rel:+d} ({len(g)} bytes): "
                      f"{old_bytes.hex()} -> {new_bytes.hex()}")

        reset_fn(task)
        time.sleep(0.3)

    # === I16 FIELDS (type=2, class_slot 13, 17, 18, 35, 44, 45, 48, ...) ===
    print("\n=== PROBING I16 FIELDS ===")
    i16_probes = [
        ("Priority", 13,
         lambda t: setattr(t, 'Priority', 777),
         lambda t: setattr(t, 'Priority', 500),
         777),
        ("PercentComplete", 17,
         lambda t: setattr(t, 'PercentComplete', 67),
         lambda t: setattr(t, 'PercentComplete', 0),
         67),
        ("PercentWorkComplete", 18,
         lambda t: setattr(t, 'PercentWorkComplete', 33),
         lambda t: setattr(t, 'PercentWorkComplete', 0),
         33),
        ("Type", 35,
         lambda t: setattr(t, 'Type', 1),  # FixedDuration
         lambda t: setattr(t, 'Type', 0),  # FixedUnits
         1),
        ("FixedCostAccrual", 48,
         lambda t: setattr(t, 'FixedCostAccrual', 2),  # End
         lambda t: setattr(t, 'FixedCostAccrual', 3),  # Prorated (default)
         2),
    ]

    for name, slot, set_fn, reset_fn, value in i16_probes:
        before = read_mem(handle, buf_start, buf_size)
        try:
            set_fn(task)
        except Exception as e:
            print(f"  {name} (slot {slot}): FAILED to set - {e}")
            continue
        time.sleep(0.3)
        after = read_mem(handle, buf_start, buf_size)

        # Search for i16 value
        pattern = struct.pack('<h', value)
        all_matches = []
        pos = 0
        while True:
            idx = after.find(pattern, pos)
            if idx == -1:
                break
            # Check this was actually a change
            if idx + 2 <= len(before):
                old_val = struct.unpack_from('<h', before, idx)[0]
                new_val = struct.unpack_from('<h', after, idx)[0]
                if old_val != new_val:
                    addr = buf_start + idx
                    rel = addr - prio_addr
                    all_matches.append((rel, addr))
            pos = idx + 1

        if all_matches:
            for rel, addr in all_matches:
                print(f"  {name} (slot {slot}): i16={value} at prio{rel:+d} ({addr:#x})")
                results.append((name, slot, rel, f'i16={value}'))
        else:
            # Show all changes
            changes = diff_buffers(before, after, buf_start, prio_addr)
            print(f"  {name} (slot {slot}): value {value} not found as i16 change, "
                  f"{len(changes)} bytes changed:")
            for addr, rel, old, new in changes[:20]:
                print(f"    prio{rel:+d}: {old:#04x} -> {new:#04x}")

        reset_fn(task)
        time.sleep(0.3)

    # === DURATION FIELDS (type=3, i32) ===
    print("\n=== PROBING I32/DURATION FIELDS ===")
    i32_probes = [
        ("Duration", 15,
         lambda t: setattr(t, 'Duration', 3333),
         lambda t: setattr(t, 'Duration', 2400),
         3333, 2400),
        ("ActualDuration", 14,
         # Can't set ActualDuration directly, comes from PercentComplete
         None, None, None, None),
        ("LevelingDelay", 10,
         lambda t: setattr(t, 'LevelingDelay', 4800),
         lambda t: setattr(t, 'LevelingDelay', 0),
         4800, 0),
    ]

    for name, slot, set_fn, reset_fn, value, reset_val in i32_probes:
        if set_fn is None:
            print(f"  {name} (slot {slot}): skipped (not directly settable)")
            continue
        before = read_mem(handle, buf_start, buf_size)
        try:
            set_fn(task)
        except Exception as e:
            print(f"  {name} (slot {slot}): FAILED to set - {e}")
            continue
        time.sleep(0.3)
        after = read_mem(handle, buf_start, buf_size)

        # Search for i32 transition
        pattern_new = struct.pack('<i', value)
        pattern_old = struct.pack('<i', reset_val)

        all_matches = []
        pos = 0
        while True:
            idx = after.find(pattern_new, pos)
            if idx == -1:
                break
            if idx + 4 <= len(before):
                if before[idx:idx+4] == pattern_old:
                    addr = buf_start + idx
                    rel = addr - prio_addr
                    all_matches.append((rel, addr))
            pos = idx + 1

        if all_matches:
            for rel, addr in all_matches:
                print(f"  {name} (slot {slot}): i32={value} at prio{rel:+d} ({addr:#x})")
                results.append((name, slot, rel, f'i32={value}'))
        else:
            changes = diff_buffers(before, after, buf_start, prio_addr)
            # Filter out the version counter and flags
            sig_changes = [(a, r, o, n) for a, r, o, n in changes if r not in (-264, -267, -266)]
            print(f"  {name} (slot {slot}): i32={value} not found as transition, "
                  f"{len(sig_changes)} significant bytes changed:")
            # Group contiguous changes
            if sig_changes:
                groups = []
                current_group = [sig_changes[0]]
                for c in sig_changes[1:]:
                    if c[0] == current_group[-1][0] + 1:
                        current_group.append(c)
                    else:
                        groups.append(current_group)
                        current_group = [c]
                groups.append(current_group)
                for g in groups[:10]:
                    start_rel = g[0][1]
                    old_bytes = bytes(c[2] for c in g)
                    new_bytes = bytes(c[3] for c in g)
                    if len(g) == 4:
                        old_i32 = struct.unpack('<i', old_bytes)[0]
                        new_i32 = struct.unpack('<i', new_bytes)[0]
                        print(f"    prio{start_rel:+d}: i32 {old_i32} -> {new_i32}")
                    else:
                        print(f"    prio{start_rel:+d} ({len(g)} bytes): {old_bytes.hex()} -> {new_bytes.hex()}")

        reset_fn(task)
        time.sleep(0.3)

    # === BOOLEAN FIELDS (type=11) ===
    print("\n=== PROBING BOOLEAN FIELDS ===")
    bool_probes = [
        ("Milestone", 12),
        ("Marked", 28),
        ("Rollup", 29),
        ("HideBar", 34),
        ("EffortDriven", 38),
    ]

    for name, slot in bool_probes:
        before = read_mem(handle, buf_start, buf_size)
        try:
            setattr(task, name, True)
        except Exception as e:
            print(f"  {name} (slot {slot}): FAILED to set - {e}")
            continue
        time.sleep(0.3)
        after = read_mem(handle, buf_start, buf_size)

        changes = diff_buffers(before, after, buf_start, prio_addr)
        # Filter out version counter
        sig_changes = [(a, r, o, n) for a, r, o, n in changes if r != -264]

        print(f"  {name} (slot {slot}): {len(sig_changes)} non-counter bytes changed:")
        for addr, rel, old, new in sig_changes[:15]:
            print(f"    prio{rel:+d}: {old:#04x} -> {new:#04x} (bit changes: {old ^ new:#010b})")

        setattr(task, name, False)
        time.sleep(0.3)

    # === DATE FIELDS (type=19, no byte_offset) ===
    print("\n=== PROBING DATE FIELDS (flat buffer only) ===")
    # ActualStart, ActualFinish, Resume, Stop - these use getter 0x4b2b00
    # ConstraintDate (slot 8) also has no byte_offset
    # We can set ConstraintDate by setting ConstraintType first

    # Try setting ActualStart via PercentComplete > 0
    before = read_mem(handle, buf_start, buf_size)
    task.PercentComplete = 50
    time.sleep(0.3)
    after = read_mem(handle, buf_start, buf_size)

    changes = diff_buffers(before, after, buf_start, prio_addr)
    sig_changes = [(a, r, o, n) for a, r, o, n in changes if r not in (-264, -267, -266)]

    print(f"  PercentComplete 0->50 (triggers ActualStart, ActualDuration changes):")
    print(f"    {len(sig_changes)} significant bytes changed")
    if sig_changes:
        groups = []
        current_group = [sig_changes[0]]
        for c in sig_changes[1:]:
            if c[0] == current_group[-1][0] + 1:
                current_group.append(c)
            else:
                groups.append(current_group)
                current_group = [c]
        groups.append(current_group)
        for g in groups[:15]:
            start_rel = g[0][1]
            old_bytes = bytes(c[2] for c in g)
            new_bytes = bytes(c[3] for c in g)
            if len(g) == 4:
                old_i32 = struct.unpack('<i', old_bytes)[0]
                new_i32 = struct.unpack('<i', new_bytes)[0]
                # Check if it looks like a date
                if 0x3d000000 <= (new_i32 & 0xFFFFFFFF) <= 0x3e000000:
                    print(f"    prio{start_rel:+d}: DATE {old_i32:#x} -> {new_i32:#x}")
                else:
                    print(f"    prio{start_rel:+d}: i32 {old_i32} -> {new_i32} ({old_i32:#x} -> {new_i32:#x})")
            elif len(g) == 8:
                old_dbl = struct.unpack('<d', old_bytes)[0]
                new_dbl = struct.unpack('<d', new_bytes)[0]
                print(f"    prio{start_rel:+d}: dbl {old_dbl:.2f} -> {new_dbl:.2f}")
            elif len(g) == 2:
                old_i16 = struct.unpack('<h', old_bytes)[0]
                new_i16 = struct.unpack('<h', new_bytes)[0]
                print(f"    prio{start_rel:+d}: i16 {old_i16} -> {new_i16}")
            else:
                print(f"    prio{start_rel:+d} ({len(g)} bytes): {old_bytes.hex()} -> {new_bytes.hex()}")

    task.PercentComplete = 0
    time.sleep(0.3)

    # === SUMMARY ===
    print(f"\n{'='*80}")
    print("SUMMARY: Confirmed flat buffer offsets (relative to Priority)")
    print(f"{'='*80}")
    results.sort(key=lambda x: x[2])
    for name, slot, rel, desc in results:
        print(f"  prio{rel:+d}: {name} (class_slot={slot}) [{desc}]")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
