import olefile, struct, sys
D = 'tests/data/'  # run from the repo root
def S(f, suf):
    o = olefile.OleFileIO(D + f)
    for s in o.listdir(streams=True, storages=False):
        if '/'.join(s).endswith(suf):
            return o.openstream(s).read()
def ents(f):
    vm = S(f, '114/TBkndTask/VarMeta'); v2 = S(f, '114/TBkndTask/Var2Data')
    n = struct.unpack_from('<I', vm, 8)[0]; hdr = len(vm) - 12 * n
    r = {}
    for i in range(n):
        uid, off, key = struct.unpack_from('<III', vm, hdr + 12 * i)
        ln, = struct.unpack_from('<I', v2, off)
        r[(uid, key)] = v2[off + 4: off + 4 + ln].hex()
    return r
for p in sys.argv[1:]:
    a = ents(p + '-control.mpp'); b = ents(p + '.mpp')
    print('=====', p, 'entries', len(a), '->', len(b))
    for k in sorted(set(b) - set(a)):
        print('  new uid %d key %08x payload %s' % (k[0], k[1], b[k]))
    for k in sorted(set(a) & set(b)):
        if a[k] != b[k]:
            print('  chg uid %d key %08x %s -> %s' % (k[0], k[1], a[k], b[k]))
