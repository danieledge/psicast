# unsis.py SIS OUTDIR - the files of an EPOC R5 .sis (simple and multi-language records)
import os, struct, sys, zlib
d = open(sys.argv[1], 'rb').read()
u = lambda o: struct.unpack_from('<I', d, o)[0]
h = lambda o: struct.unpack_from('<H', d, o)[0]
nlang, nfiles, opts = h(0x12), h(0x14), h(0x24)
p = u(0x34)
print('languages', nlang, 'files', nfiles, 'options %#x' % opts)
for k in range(nfiles):
    t = u(p)
    if t not in (0, 1):
        print('record type', t, 'not read; stopping'); break
    ftype, fdet, sl, sp, dl, dp = struct.unpack_from('<6I', d, p + 4)
    n = 1 if t == 0 else nlang
    q = p + 28
    lens = struct.unpack_from('<%dI' % n, d, q); q += 4 * n
    ptrs = struct.unpack_from('<%dI' % n, d, q); q += 4 * n
    if u(0x20) >= 200:                       # (EPOC 6's: the original lengths and a MIME type)
        q += 4 * n + 8
    raw = d[dp:dp + dl]
    name = raw.decode('utf-16-le', 'replace') if opts & 1 else raw.decode('latin-1')
    name = name.replace('\\', '/').split('/')[-1] or 'file%d' % k
    for i in range(n):
        b = d[ptrs[i]:ptrs[i] + lens[i]]
        if lens[i] and not opts & 8:
            try: b = zlib.decompress(b)
            except zlib.error: pass
        out = os.path.join(sys.argv[2], name if n == 1 else '%d-%s' % (i, name))
        if ftype != 1 and b:                 # (1: a text shown at install)
            open(out, 'wb').write(b)
        print('%-24s type %d  %7d bytes' % (name, ftype, len(b)))
    p = q
