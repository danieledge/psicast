#!/usr/bin/env python3
# mkhomepic.py - the picture on the LCD's Sharing page (castlcd.h): the
# 5mx's System screen, tools/system5mx.png (the emulator's capture just
# after boot, 640x240 greys), box-averaged down to 128x48 (each pixel the
# mean of a 5x5 block of the screen's 16 greys) and kept at 16 greys, 4 bits
# a pixel, the left pixel in the low nibble (the screen model's own
# packing): src/casthome.h, 3 KB, committed so a build needs no Python. The
# page shows the same picture whatever the Psion's screen holds: a live one
# would be too small to read and only cost redraws.
#   python3 tools/mkhomepic.py
# MIT licence (see LICENSE at the top of the repository).
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "system5mx.png")
OUT = os.path.join(HERE, "..", "src", "casthome.h")
W, H = 128, 48


def main():
    im = Image.open(SRC).convert("L")
    sw, sh = im.size
    px = im.load()
    grey = [[(px[x, y] * 15 + 127) // 255 for x in range(sw)] for y in range(sh)]
    out = bytearray(W * H // 2)
    for oy in range(H):
        y0, y1 = oy * sh // H, (oy + 1) * sh // H
        for ox in range(W):
            x0, x1 = ox * sw // W, (ox + 1) * sw // W
            s = sum(grey[y][x] for y in range(y0, y1) for x in range(x0, x1))
            n = (x1 - x0) * (y1 - y0)
            g = (2 * s + n) // (2 * n)                   # (the mean grey, rounded)
            i = (oy * W + ox) // 2
            out[i] |= g << 4 if ox & 1 else g
    lines = []
    for i in range(0, len(out), 24):
        lines.append("\t" + ",".join("0x%02x" % b for b in out[i:i + 24]) + ",")
    with open(OUT, "w") as f:
        f.write("// casthome.h - made by tools/mkhomepic.py from tools/system5mx.png: the\n"
                "// 5mx's System screen at %dx%d, 16 greys (0 black .. 15 white), 4 bits a\n"
                "// pixel, the left pixel in the low nibble. Do not edit.\n"
                "#ifndef ATOM_CASTHOME_H\n#define ATOM_CASTHOME_H\n\n"
                "static const int kCastHomeW = %d, kCastHomeH = %d;\n"
                "static const unsigned char kCastHome[] = {\n%s\n\t};\n\n#endif\n" % (W, H, W, H, "\n".join(lines)))
    print("mkhomepic: %s, %d bytes" % (os.path.relpath(OUT), len(out)))


if __name__ == "__main__":
    main()
