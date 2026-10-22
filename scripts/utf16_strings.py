"""Extract printable UTF-16LE strings (>= min chars) from every .bin under a resource dump.
Output TSV: type, resource name, offset, string."""
import os
import re
import sys

root, out = sys.argv[1], sys.argv[2]
minlen = int(sys.argv[3]) if len(sys.argv) > 3 else 4
pat = re.compile(rb"(?:[\x20-\x7e\xa0-\xff]\x00|[\x00-\xff][\x01-\x2f]){%d,}" % minlen)
n = 0
with open(out, "w", encoding="utf-8") as o:
    o.write("type\tres\toffset\tstring\n")
    for t in sorted(os.listdir(root)):
        d = os.path.join(root, t)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            data = open(os.path.join(d, fn), "rb").read()
            for m in pat.finditer(data):
                b = m.group(0)
                if m.start() % 2:
                    continue
                s = b.decode("utf-16-le", "replace")
                if sum(c.isalpha() for c in s) < max(3, len(s) // 3):
                    continue
                s = s.replace("\t", " ").replace("\r", " ").replace("\n", " ")
                o.write(f"{t}\t{fn[:-4]}\t{m.start()}\t{s}\n")
                n += 1
print(n, "strings")
