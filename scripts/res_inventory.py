"""Inventory resource types/counts/sizes in one or more PE files."""
import sys
import pefile

for path in sys.argv[1:]:
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    print(f"== {path}")
    if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        print("  (no resources)")
        continue
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        tname = str(t.name) if t.name else pefile.RESOURCE_TYPE.get(t.id, t.id)
        n, size = 0, 0
        for e in t.directory.entries:
            for lang in e.directory.entries:
                n += 1
                size += lang.data.struct.Size
        print(f"  {tname!s:24} entries={n:6} bytes={size:10}")
