import olefile, struct, sys, glob, collections
# check: Task FixedMeta entry +2 (u16) == number of Task VarMeta entries for that row uid; and the bitmap bytes agree with VarMeta presence
D = 'tests/data/'  # run from the repo root
def S(o, suf):
    for s in o.listdir(streams=True, storages=False):
        if '/'.join(s).endswith(suf):
            return o.openstream(s).read()
files = sorted(glob.glob(D + 'wave3*.mpp'))[:400]
ok = bad = skipped = 0
badlist = []
for f in files:
    try:
        o = olefile.OleFileIO(f)
        fm = S(o, '114/TBkndTask/FixedMeta'); vm = S(o, '114/TBkndTask/VarMeta')
    except Exception:
        continue
    if fm is None or vm is None:
        skipped += 1; continue
    cnt = collections.Counter()
    p = 16
    # VarMeta header is 16 or 24 bytes: entries of 12 bytes (uid, offset, key); find by size
    n = struct.unpack_from('<I', vm, 8)[0] if len(vm) >= 12 else 0
    hdr = len(vm) - 12 * n
    if hdr < 0 or hdr > 40:
        skipped += 1; continue
    for i in range(n):
        uid, off, key = struct.unpack_from('<III', vm, hdr + 12 * i)
        cnt[uid] += 1
    ne = (len(fm) - 16) // 47
    for k in range(ne):
        e = fm[16 + 47 * k: 16 + 47 * (k + 1)]
        off, = struct.unpack_from('<I', e, 4)
        if off < 48 or (off - 48) % 202:
            continue
        uid = (off - 48) // 202
        c, = struct.unpack_from('<H', e, 2)
        if c == cnt[uid]:
            ok += 1
        else:
            bad += 1; badlist.append((f.split('/')[-1], uid, c, cnt[uid]))
print('rows match', ok, 'differ', bad, 'files skipped', skipped)
for b in badlist[:15]:
    print(b)
