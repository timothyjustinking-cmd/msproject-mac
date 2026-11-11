import re, sys, struct, olefile
# predict the new row's FixedMeta bitmap (+8..+46) from the setter calls on it: value fields < 305 always, flag fields (descriptor dword0 != 0) only when the value is 1; compare with the saved entry
rx = re.compile(r'C (\S+) tbl (\w+) row (\d+) fld (\d+) d (\S+) v (\S+)')
D = 'tests/data/'  # run from the repo root
for name in sys.argv[1:]:
    calls = [m.groups() for m in (rx.match(l.strip()) for l in open('spec/writers/evidence/trace-r6i1-wave35-g3-insert-%s.txt' % name)) if m]
    nr = max(int(c[2]) for c in calls)
    by = bytearray(39)
    for pos, tbl, row, fld, d, v in calls:
        if int(row) != nr: continue
        fl = int(fld); dd = d.split('.')
        if fl >= 305: continue
        if int(dd[0], 16) == 0 or (int(v, 16) & 0xff) == 1:
            by[fl // 8] |= 1 << (fl % 8)
    o = olefile.OleFileIO(D + 'wave35-g3-insert-%s.mpp' % name)
    fm = None
    for s in o.listdir(streams=True, storages=False):
        if '/'.join(s).endswith('114/TBkndTask/FixedMeta'):
            fm = o.openstream(s).read()
    ne = (len(fm) - 16) // 47
    e = fm[16 + 47 * (ne - 1): 16 + 47 * ne]
    print(name, 'predicted', by.hex()); print(' ' * len(name), 'saved    ', e[8:47].hex(), 'MATCH' if bytes(by) == e[8:47] else 'DIFF', 'count word', struct.unpack_from('<H', e, 2)[0])
