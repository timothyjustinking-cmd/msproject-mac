#!/usr/bin/env python3
"""Walk tree containers in the flat buffer to find string storage.

5 container objects at prio+116 to prio+180.
Each is a tree/map with sentinel node pattern (self.left=self.right=self.parent=self for empty).
Follow child pointers to find entries containing string BSTRs.
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
    data = read_mem(handle, addr, 8)
    if not data: return 0
    return struct.unpack('<Q', data)[0]

def read_u32(handle, addr):
    data = read_mem(handle, addr, 4)
    if not data: return 0
    return struct.unpack('<I', data)[0]

def is_valid_ptr(p):
    return 0x10000 < p < 0x7FFFFFFFFFFF

def try_utf16(data, max_chars=100):
    """Try to decode data as null-terminated UTF-16LE."""
    try:
        for i in range(0, min(max_chars*2, len(data))-1, 2):
            if data[i] == 0 and data[i+1] == 0:
                s = data[:i].decode('utf-16-le')
                if all(c.isprintable() or c in ' \t\n\r' for c in s):
                    return s
                return None
        return None
    except:
        return None


def walk_tree_node(handle, node_addr, sentinel_addr, depth=0, visited=None):
    """Walk a tree node (std::set/map style). Returns list of node data."""
    if visited is None:
        visited = set()
    if node_addr in visited or node_addr == sentinel_addr or not is_valid_ptr(node_addr):
        return []
    if depth > 20:
        return []
    visited.add(node_addr)

    # Read node: left(8), right(8), parent(8), color(1?), then key/value data
    data = read_mem(handle, node_addr, 128)
    if not data:
        return []

    left = struct.unpack_from('<Q', data, 0)[0]
    right = struct.unpack_from('<Q', data, 8)[0]
    parent = struct.unpack_from('<Q', data, 16)[0]

    # Node data starts after the tree links (typically at +24 or +32)
    results = []

    # Collect this node's data
    results.append((node_addr, data))

    # Walk left and right subtrees
    results.extend(walk_tree_node(handle, left, sentinel_addr, depth+1, visited))
    results.extend(walk_tree_node(handle, right, sentinel_addr, depth+1, visited))

    return results


def analyze_container(handle, container_addr, label):
    """Analyze a tree container and its entries."""
    data = read_mem(handle, container_addr, 64)
    if not data:
        print(f"\n{label}: UNREADABLE at 0x{container_addr:016x}")
        return

    # Sentinel: first 3 pointers are self-referencing when tree is empty
    p0 = struct.unpack_from('<Q', data, 0)[0]
    p1 = struct.unpack_from('<Q', data, 8)[0]
    p2 = struct.unpack_from('<Q', data, 16)[0]
    misc = struct.unpack_from('<Q', data, 24)[0]
    count = struct.unpack_from('<I', data, 32)[0]
    cap = struct.unpack_from('<I', data, 36)[0]

    is_empty = (p0 == container_addr and p1 == container_addr and p2 == container_addr)

    print(f"\n{label}: 0x{container_addr:016x}")
    print(f"  p0=0x{p0:016x} p1=0x{p1:016x} p2=0x{p2:016x}")
    print(f"  misc=0x{misc:016x} count={count} cap/flags={cap}")
    print(f"  Empty: {is_empty}")

    if is_empty or count == 0:
        return

    # The tree root is one of the child pointers
    # For std::set/map, the sentinel's parent is the root
    # But this appears to use: child ptrs at +48 and +56
    child0 = struct.unpack_from('<Q', data, 48)[0]
    child1 = struct.unpack_from('<Q', data, 56)[0]

    print(f"  child0=0x{child0:016x} child1=0x{child1:016x}")

    # Walk from child0 (treating container_addr as sentinel)
    nodes = []
    if is_valid_ptr(child0) and child0 != container_addr:
        nodes.extend(walk_tree_node(handle, child0, container_addr))
    if is_valid_ptr(child1) and child1 != container_addr:
        nodes.extend(walk_tree_node(handle, child1, container_addr, visited=set(n[0] for n in nodes)))

    print(f"  Found {len(nodes)} tree nodes")

    for node_addr, node_data in nodes:
        print(f"\n  Node 0x{node_addr:016x}:")
        # Dump first 96 bytes
        for off in range(0, min(96, len(node_data)), 16):
            chunk = node_data[off:off+16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            print(f"    +{off:03d}: {hex_str}  |{ascii_str}|")

        # Look for strings at various offsets in node data
        for str_off in range(24, min(96, len(node_data)) - 4, 4):
            # Check for BSTR length prefix
            blen = struct.unpack_from('<I', node_data, str_off)[0]
            if 2 <= blen <= 200 and blen % 2 == 0 and str_off + 4 + blen <= len(node_data):
                s = try_utf16(node_data[str_off+4:str_off+4+blen+2])
                if s:
                    print(f"    ==> BSTR at +{str_off}: \"{s}\" (len={blen})")

        # Check for pointers to strings
        for ptr_off in range(24, min(80, len(node_data)) - 7, 8):
            ptr = struct.unpack_from('<Q', node_data, ptr_off)[0]
            if is_valid_ptr(ptr):
                # Read at ptr, check for BSTR
                sub = read_mem(handle, ptr - 4, 260)
                if sub:
                    blen = struct.unpack_from('<I', sub, 0)[0]
                    if 2 <= blen <= 200 and blen % 2 == 0:
                        s = try_utf16(sub[4:4+blen+2])
                        if s:
                            print(f"    ==> Ptr at +{ptr_off} -> BSTR: \"{s}\" (len={blen})")

                # Also check direct UTF-16 at ptr
                sub2 = read_mem(handle, ptr, 128)
                if sub2:
                    s = try_utf16(sub2)
                    if s and len(s) > 2:
                        print(f"    ==> Ptr at +{ptr_off} -> UTF-16: \"{s}\"")


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

    # Read 5 container pointers
    raw = read_mem(handle, prio_addr + 116, 80)
    if not raw:
        print("ERROR: Can't read")
        CloseHandle(handle)
        return

    containers = []
    for i in range(5):
        ptr = struct.unpack_from('<Q', raw, i * 16)[0]
        containers.append((116 + i * 16, ptr))

    # Analyze first 3 containers (most likely to have strings)
    for off, ptr in containers[:3]:
        if is_valid_ptr(ptr):
            analyze_container(handle, ptr, f"Container at prio+{off}")

    # Reset
    task.Name = "Task 1"
    task.Contact = ""
    time.sleep(0.3)

    CloseHandle(handle)
    print("\nDone.")


if __name__ == '__main__':
    main()
