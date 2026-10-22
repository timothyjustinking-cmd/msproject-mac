#!/usr/bin/env python3
"""Map ALL boolean flag bits in the flat buffer.

Known: prio-212 bit0=Milestone, prio-211 bit1=Marked, prio-209 bit2=Rollup,
       prio-208 bit0=HideBar, prio-207 bit3=EffortDriven

Need to probe all COM-settable booleans to find their flag byte + bit position.
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


def find_flag_changes(before, after, buf_start, prio_addr):
    """Find single-byte changes that look like flag toggles."""
    results = []
    for i in range(min(len(before), len(after))):
        if before[i] != after[i]:
            addr = buf_start + i
            rel = addr - prio_addr
            xor = before[i] ^ after[i]
            results.append((rel, before[i], after[i], xor))
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
    task = None
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t is not None:
            task = t
            break

    print(f"PID: {pid}")
    print(f"Task: ID={task.ID} Name='{task.Name}'")

    prio_addr = 0x23b7ef6dd3c
    # Read the flags region (prio-270 to prio-200)
    flag_start = prio_addr - 270
    flag_size = 100  # covers prio-270 to prio-170

    # Reset to clean state
    task.Duration = 2400
    task.Priority = 500
    task.PercentComplete = 0
    task.FixedCost = 0.0
    task.Marked = False
    task.Milestone = False
    task.HideBar = False
    try:
        task.ConstraintType = 0
    except:
        pass
    time.sleep(0.5)

    # Also try via SetField for booleans
    # class_slot -> (field_name, com_name, field_id, can_set_directly)
    bool_fields = [
        # (name, class_slot, method_to_set_True, method_to_set_False)
        ("Critical", 9, None, None),  # computed, can't set
        ("Milestone", 12,
         lambda t: setattr(t, 'Milestone', True),
         lambda t: setattr(t, 'Milestone', False)),
        ("FixedDuration", 19,
         lambda t: setattr(t, 'Type', 2),  # FixedDuration = Type=2
         lambda t: setattr(t, 'Type', 0)),  # FixedUnits = Type=0
        ("Marked", 28,
         lambda t: setattr(t, 'Marked', True),
         lambda t: setattr(t, 'Marked', False)),
        ("Rollup", 29,
         lambda t: setattr(t, 'Rollup', True),
         lambda t: setattr(t, 'Rollup', False)),
        ("Summary", 30, None, None),  # computed based on subtasks
        ("HideBar", 34,
         lambda t: setattr(t, 'HideBar', True),
         lambda t: setattr(t, 'HideBar', False)),
        ("Recurring", 36, None, None),  # read-only
        ("EffortDriven", 38,
         lambda t: setattr(t, 'EffortDriven', True),
         lambda t: setattr(t, 'EffortDriven', False)),
    ]

    # Also try fields settable via SetField
    # These are unnamed boolean fields or less common ones
    setfield_bools = [
        # (name, class_slot, field_id_dec)
        ("LevelAssignments", 37, 188743773),
        ("LevelingCanSplit", 39, 188743774),
        ("IgnoreResourceCalendar", 40, 188743880),  # Not sure if this is right
    ]

    # Read wider buffer for cost changes too
    wide_start = prio_addr - 300
    wide_size = 900

    print("\n=== PROBING BOOLEAN FIELDS ===")
    print(f"{'Name':25s} {'Slot':4s} {'Offset':8s} {'Bit':10s} {'Old':5s} {'New':5s}")
    print("-" * 60)

    flag_map = {}  # (offset, bit) -> field_name

    for name, slot, set_true, set_false in bool_fields:
        if set_true is None:
            print(f"  {name:25s} {slot:4d}  -- skipped (not directly settable)")
            continue

        before = read_mem(handle, wide_start, wide_size)
        set_true(task)
        time.sleep(0.3)
        after = read_mem(handle, wide_start, wide_size)

        changes = find_flag_changes(before, after, wide_start, prio_addr)
        # Filter: look for single-bit changes in the flag region (prio-270 to prio-200)
        flag_changes = []
        other_changes = []
        for rel, old, new, xor in changes:
            if -270 <= rel <= -200 and rel != -264:  # exclude counter
                flag_changes.append((rel, old, new, xor))
            elif rel != -264:
                other_changes.append((rel, old, new, xor))

        if flag_changes:
            for rel, old, new, xor in flag_changes:
                # Find which bit(s) changed
                for bit in range(8):
                    if xor & (1 << bit):
                        direction = "SET" if (new >> bit) & 1 else "CLR"
                        print(f"  {name:25s} {slot:4d}  prio{rel:+d}  bit{bit} {direction:3s}  "
                              f"{old:#04x}  {new:#04x}")
                        flag_map[(rel, bit)] = name
        else:
            print(f"  {name:25s} {slot:4d}  NO flag changes in flag region")
            if other_changes:
                for rel, old, new, xor in other_changes[:5]:
                    print(f"    prio{rel:+d}: {old:#04x} -> {new:#04x}")

        set_false(task)
        time.sleep(0.3)

    # Try SetField-based booleans
    print("\n=== PROBING SetField BOOLEANS ===")
    for name, slot, fid in setfield_bools:
        before = read_mem(handle, wide_start, wide_size)
        try:
            task.SetField(fid, "Yes")
        except Exception as e:
            # Try numeric
            try:
                task.SetField(fid, 1)
            except Exception as e2:
                print(f"  {name:25s} {slot:4d}  FAILED: {e2}")
                continue
        time.sleep(0.3)
        after = read_mem(handle, wide_start, wide_size)

        changes = find_flag_changes(before, after, wide_start, prio_addr)
        flag_changes = [(r, o, n, x) for r, o, n, x in changes
                        if -270 <= r <= -200 and r != -264]

        if flag_changes:
            for rel, old, new, xor in flag_changes:
                for bit in range(8):
                    if xor & (1 << bit):
                        direction = "SET" if (new >> bit) & 1 else "CLR"
                        print(f"  {name:25s} {slot:4d}  prio{rel:+d}  bit{bit} {direction:3s}  "
                              f"{old:#04x}  {new:#04x}")
                        flag_map[(rel, bit)] = name
        else:
            print(f"  {name:25s} {slot:4d}  NO flag changes")
            non_counter = [(r, o, n, x) for r, o, n, x in changes if r != -264]
            for rel, old, new, xor in non_counter[:5]:
                print(f"    prio{rel:+d}: {old:#04x} -> {new:#04x}")

        # Reset
        try:
            task.SetField(fid, "No")
        except:
            try:
                task.SetField(fid, 0)
            except:
                pass
        time.sleep(0.3)

    # Also probe via named COM properties that might be booleans
    print("\n=== PROBING OTHER BOOLEAN PROPERTIES ===")
    extra_bools = [
        ("DisplayAsSummary", lambda t: setattr(t, 'DisplayAsSummary', True),
         lambda t: setattr(t, 'DisplayAsSummary', False)),
        ("Active", lambda t: setattr(t, 'Active', False),
         lambda t: setattr(t, 'Active', True)),
        ("Manual", lambda t: setattr(t, 'Manual', True),
         lambda t: setattr(t, 'Manual', False)),
    ]

    for name, set_fn, reset_fn in extra_bools:
        before = read_mem(handle, wide_start, wide_size)
        try:
            set_fn(task)
        except Exception as e:
            print(f"  {name:25s} FAILED: {e}")
            continue
        time.sleep(0.3)
        after = read_mem(handle, wide_start, wide_size)

        changes = find_flag_changes(before, after, wide_start, prio_addr)
        flag_changes = [(r, o, n, x) for r, o, n, x in changes
                        if -270 <= r <= -200 and r != -264]

        if flag_changes:
            for rel, old, new, xor in flag_changes:
                for bit in range(8):
                    if xor & (1 << bit):
                        direction = "SET" if (new >> bit) & 1 else "CLR"
                        print(f"  {name:25s}       prio{rel:+d}  bit{bit} {direction:3s}  "
                              f"{old:#04x}  {new:#04x}")
                        flag_map[(rel, bit)] = name
        else:
            print(f"  {name:25s} NO flag changes")
            non_counter = [(r, o, n, x) for r, o, n, x in changes if r != -264]
            if non_counter:
                for rel, old, new, xor in non_counter[:5]:
                    print(f"    prio{rel:+d}: {old:#04x} -> {new:#04x}")

        reset_fn(task)
        time.sleep(0.3)

    # Summary
    print(f"\n{'='*60}")
    print("BOOLEAN FLAG MAP")
    print(f"{'='*60}")
    for (offset, bit), name in sorted(flag_map.items()):
        print(f"  prio{offset:+d} bit{bit}: {name}")

    # Final: dump the complete flags region
    print(f"\n=== Final flag bytes (prio-270 to prio-200) ===")
    data = read_mem(handle, prio_addr - 270, 70)
    if data:
        for off in range(0, 70):
            val = data[off]
            rel = off - 270
            # Annotate known flags
            bits_labeled = []
            for bit in range(8):
                key = (rel, bit)
                if key in flag_map:
                    bits_labeled.append(f"bit{bit}={flag_map[key]}")
            ann = ", ".join(bits_labeled) if bits_labeled else ""
            if val != 0 or ann:
                print(f"    prio{rel:+d}: {val:#04x} ({val:08b})  {ann}")

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
