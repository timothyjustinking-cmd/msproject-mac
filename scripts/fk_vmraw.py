"""Dump the raw VarMeta header/entries of a Task table (W5 diagnostic)."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_lib as L

path = sys.argv[1] if len(sys.argv) > 1 else "corpus/probe/base.mpp"
s = L.load(path)
vm = L.stream(s, "TBkndTask", "VarMeta")
print("VarMeta len", len(vm))
print("N (header 24):", (len(vm) - 24) / 12, " (header 32 + trailer):",
      (len(vm) - 36) / 12)
for i in range(0, min(len(vm), 96), 4):
    v = struct.unpack_from("<I", vm, i)[0]
    print("%4d: %-11s u32=%10d 0x%08x" % (i, vm[i:i + 4].hex(" "), v, v))
print("\nentries at +24 as (uid, offset, type):")
for i in range(min(4, (len(vm) - 24) // 12)):
    uid, off, typ = struct.unpack_from("<3I", vm, 24 + i * 12)
    print("  %2d uid=%-6d off=%-6d type=0x%08x" % (i, uid, off, typ))
print("last entry:", struct.unpack_from("<3I", vm, len(vm) - 12))
