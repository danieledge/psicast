# mbm.py FILE.mbm OUTPREFIX - each bitmap of an EPOC multi-bitmap file as a PNG (greys 0..15 kept in the .npy-free way: L mode, grey*17)
import struct, sys
from PIL import Image
d = open(sys.argv[1], 'rb').read()
u = lambda o: struct.unpack_from('<I', d, o)[0]
tr = u(16); n = u(tr)
for k in range(n):
    o = u(tr + 4 + 4 * k)
    size, hl, w, h, tx, ty, bpp, col, pal, comp = struct.unpack_from('<10I', d, o)
    data = d[o + hl:o + size]
    if comp == 1:                            # byte RLE
        out = bytearray(); i = 0
        while i < len(data):
            c = data[i]; i += 1
            if c < 0x80: out += bytes([data[i]]) * (c + 1); i += 1
            else: m = 256 - c; out += data[i:i + m]; i += m
        data = bytes(out)
    elif comp != 0:
        print(k, 'compression', comp, 'not read'); continue
    stride = ((w * bpp + 31) // 32) * 4
    im = Image.new('L', (w, h))
    for y in range(h):
        row = data[y * stride:(y + 1) * stride]
        for x in range(w):
            bit = x * bpp
            v = (row[bit // 8] >> (bit % 8)) & ((1 << bpp) - 1) if bpp < 8 else row[x]
            im.putpixel((x, y), v * 255 // ((1 << bpp) - 1))
    im.save('%s-%d.png' % (sys.argv[2], k))
    print(k, '%dx%d' % (w, h), 'bpp', bpp, 'compression', comp)
