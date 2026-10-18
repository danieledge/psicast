#!/usr/bin/env python3
# shareshots.py - the Sharing page's pictures from render_screens (BMPs in
# the first folder: the firmware's pages too, which are left out) as PNGs in
# the second, at the LCD's own size (<name>.png) and four times it
# (<name>-4x.png), and a sheet of the 4x ones. MIT licence.
import glob
import os
import shutil
import sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
big = []
for p in sorted(glob.glob(os.path.join(src, '3?-share-*.bmp'))):
    name = os.path.basename(p)[:-4]
    im = Image.open(p).convert('RGB')
    im.save(os.path.join(out, name + '.png'))
    b = im.resize((im.width * 4, im.height * 4), Image.NEAREST)
    b.save(os.path.join(out, name + '-4x.png'))
    big.append(b)
if big:
    w, h = big[0].size
    sheet = Image.new('RGB', (len(big) * (w + 16) + 16, h + 32), (40, 40, 40))
    for i, b in enumerate(big):
        sheet.paste(b, (16 + i * (w + 16), 16))
    sheet.save(os.path.join(out, 'sheet.png'))
shutil.rmtree(src, ignore_errors=True)
print('%d pictures in %s' % (len(big), out))
