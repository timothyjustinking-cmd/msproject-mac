"""Dump every resource of a PE file to <out>/<type>/<name>_<lang>.bin and write an index.tsv
with the first bytes of each so the formats can be identified."""
import os
import sys
import pefile

path, out = sys.argv[1], sys.argv[2]
pe = pefile.PE(path, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "index.tsv"), "w", encoding="utf-8") as idx:
    idx.write("type\tname\tlang\tbytes\thead\n")
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        tname = str(t.name) if t.name else str(pefile.RESOURCE_TYPE.get(t.id, t.id))
        os.makedirs(os.path.join(out, tname), exist_ok=True)
        for e in t.directory.entries:
            ename = str(e.name) if e.name else str(e.id)
            for lang in e.directory.entries:
                d = lang.data.struct
                data = pe.get_data(d.OffsetToData, d.Size)
                fn = f"{ename}_{lang.id}.bin"
                with open(os.path.join(out, tname, fn), "wb") as f:
                    f.write(data)
                head = data[:24].hex()
                idx.write(f"{tname}\t{ename}\t{lang.id}\t{len(data)}\t{head}\n")
print("ok", path)
