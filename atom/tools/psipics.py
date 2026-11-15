#!/usr/bin/env python3
"""psipics.py: every picture and graphic of EPOC R5 programs, for the
viewer's picture pack (Replace known pictures).

  psipics.py extract OUTDIR INPUT...   every bitmap of each INPUT (a .sis, a
                                       folder of a program's files, or one
                                       file) as a PNG, with OUTDIR/manifest.json
  psipics.py sheet OUTDIR              a contact sheet of each program's
                                       (OUTDIR/sheet-app.png), and of the
                                       redraws beside them once there are some
  psipics.py redraw OUTDIR [id...]     each one redrawn in colour (redrawpics.py)
  psipics.py pack OUTDIR [OUT.json.gz] the pack (mkpicpack.py), for POST /pack
  psipics.py all OUTDIR INPUT...       all four, one after another

What extract finds: every multi-bitmap file (.mbm, and the ones without a
name's ending, like Monopoly's Bitmaps/), and any other file (an .aif's
icons, an .app's own) searched for bitmaps byte by byte. A bitmap followed
by one its size in black and white only, black where it draws, is its mask:
a sprite drawn over something else (Monopoly's tokens, dice, houses). A
masked bitmap holding several pieces (a strip of a die's turns, a sheet of
icons) is cut into one per piece, since the program draws them one at a
time. Pictures already in OUTDIR/manifest.json, the same greys, keep their
entry (its id, its hint, its redraw).

The pictures are their programs' authors': keep OUTDIR out of every
repository (pictures_local/ is git-ignored). MIT licence.
"""
import hashlib, json, os, struct, subprocess, sys, zlib
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
UID_MBM = (0x10000037, 0x10000042)
BPPS = (1, 2, 4, 8)


# ----- a .sis's files -----
def sis_files(d):
    """[(name, bytes)] of an EPOC R5 .sis, the ones inside a .sis in it too"""
    u = lambda o: struct.unpack_from('<I', d, o)[0]
    h = lambda o: struct.unpack_from('<H', d, o)[0]
    nlang, nfiles, opts = h(0x12), h(0x14), h(0x24)
    p, out = u(0x34), []
    for k in range(nfiles):
        t = u(p)
        if t not in (0, 1):
            break                                # (an option or condition record: no more files)
        ftype, fdet, sl, sp, dl, dp = struct.unpack_from('<6I', d, p + 4)
        n = 1 if t == 0 else nlang
        q = p + 28
        lens = struct.unpack_from('<%dI' % n, d, q); q += 4 * n
        ptrs = struct.unpack_from('<%dI' % n, d, q); q += 4 * n
        if u(0x20) >= 200:
            q += 4 * n + 8
        raw = d[dp:dp + dl]
        name = (raw.decode('utf-16-le', 'replace') if opts & 1 else raw.decode('latin-1')).replace('\\', '/')
        for i in range(n):
            b = d[ptrs[i]:ptrs[i] + lens[i]]
            if lens[i] and not opts & 8:
                try: b = zlib.decompress(b)
                except zlib.error: pass
            if ftype == 1 or not b:
                continue                         # (a text shown at install)
            nm = name if n == 1 else name + '.%d' % i
            if is_sis(b):
                out += [(nm + '/' + a, c) for a, c in sis_files(b)]
            else:
                out.append((nm, b))
        p = q
    return out


def is_sis(b):
    return len(b) > 0x40 and struct.unpack_from('<I', b, 8)[0] == 0x10000419


# ----- bitmaps -----
def bitmap_at(d, o):
    """the bitmap whose 40-byte header is at o, as (w, h, bpp, greys 0..15) or None"""
    if o + 40 > len(d):
        return None
    size, hl, w, h, tx, ty, bpp, col, pal, comp = struct.unpack_from('<10I', d, o)
    if hl != 40 or not (0 < w <= 2048 and 0 < h <= 2048) or bpp not in BPPS or comp > 1 or col > 1:
        return None
    if size < 40 or o + size > len(d) or tx > 100000 or ty > 100000:
        return None
    data = d[o + hl:o + size]
    stride = ((w * bpp + 31) // 32) * 4
    if comp == 1:                                # byte RLE
        out = bytearray(); i = 0
        while i < len(data) and len(out) < stride * h:
            c = data[i]; i += 1
            if c < 0x80: out += bytes([data[i]]) * (c + 1) if i < len(data) else b''; i += 1
            else: m = 256 - c; out += data[i:i + m]; i += m
        data = bytes(out)
    if len(data) < stride * h:
        return None
    top = (1 << bpp) - 1
    g = []
    for y in range(h):
        row = data[y * stride:(y + 1) * stride]
        for x in range(w):
            if bpp == 8:
                v = row[x]
            else:
                bit = x * bpp
                v = (row[bit // 8] >> (bit % 8)) & top
            g.append(round(v * 15 / top))
    return w, h, bpp, g


def file_bitmaps(d):
    """the bitmaps of a file: a multi-bitmap file's in its order, any other's by a search"""
    if len(d) > 20 and struct.unpack_from('<2I', d, 0) == UID_MBM:
        tr = struct.unpack_from('<I', d, 16)[0]
        if tr + 4 <= len(d):
            n = struct.unpack_from('<I', d, tr)[0]
            offs = [struct.unpack_from('<I', d, tr + 4 + 4 * k)[0] for k in range(min(n, (len(d) - tr - 4) // 4))]
            return [b for b in (bitmap_at(d, o) for o in offs) if b]
    out, o = [], 0
    while True:
        o = d.find(b'\x28\x00\x00\x00', o + 1)  # (the header's length, 40, is its second word)
        if o < 4:
            return out
        b = bitmap_at(d, o - 4)
        if b:
            out.append(b)


def mask_of(a, b):
    """b as a's mask (a list of 0/1, 1 where a draws), or None: b is a's size (or a little larger),
    in black and white only (a toolbar icon's is on a dithered ground, a chess
    piece's on black, so a's own ground says nothing); a one of two
    black-and-white pictures only if b's ink covers all a's"""
    w, h = a[0], a[1]
    if not (w <= b[0] <= w + 8 and h <= b[1] <= h + 8) or a[3] == b[3]:
        return None                              # (Monopoly's dice's is a few pixels larger)
    bg = [b[3][y * b[0] + x] for y in range(h) for x in range(w)]
    if sum(1 for v in bg if v in (0, 15)) < 0.9 * len(bg) or len(set(bg)) < 2:
        return None                              # (mostly black and white: its greys are ink or not)
    best = None
    for ink in (0, 15):                          # (black where it draws, as a rule; the other way round too)
        m = [1 if (v < 8) == (ink == 0) else 0 for v in bg]
        if not 0 < sum(m) < len(m):
            continue
        drawn = [i for i, v in enumerate(a[3]) if v != 15]
        cover = sum(m[i] for i in drawn) / max(1, len(drawn))
        if best is None or cover > best[0] or (cover == best[0] and ink == 0):
            best = (cover, m)
    if best is None or (not set(a[3]) - {0, 15} and best[0] < 0.97):
        return None
    return best[1]


def pieces(w, h, m):
    """the bounding boxes of the mask's pieces (8-connected, ones a pixel apart one piece)"""
    seen = bytearray(w * h); boxes = []
    for s in range(w * h):
        if not m[s] or seen[s]:
            continue
        st = [s]; seen[s] = 1; x0 = x1 = s % w; y0 = y1 = s // w
        while st:
            i = st.pop(); x, y = i % w, i // w
            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    X, Y = x + dx, y + dy
                    if 0 <= X < w and 0 <= Y < h and m[Y * w + X] and not seen[Y * w + X]:
                        seen[Y * w + X] = 1; st.append(Y * w + X)
        boxes.append([x0, y0, x1 + 1, y1 + 1])
    merged = True
    while merged:                                # (a light bulb's rays, a die's spots: one piece)
        merged = False
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                a, b = boxes[i], boxes[j]
                if a[0] - 1 <= b[2] and b[0] - 1 <= a[2] and a[1] - 1 <= b[3] and b[1] - 1 <= a[3]:
                    boxes[i] = [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]
                    del boxes[j]; merged = True; break
            if merged:
                break
    return sorted(boxes, key=lambda b: (b[1] // 8, b[0]))


def mask_row_sheet(w, h, g):
    """[(box, mask)] of a sheet of square cells in three rows whose first row
    is the others' masks (Chess's pieces: masks, the white ones, the black
    ones), or None"""
    if h % 3 or h < 42 or w % (h // 3) or w // (h // 3) < 2:      # (cells of 14 or more: a toolbar's 10s are not)
        return None
    c = h // 3
    top = g[:c * w]
    if set(top) - {0, 15} or len(set(top)) < 2:
        return None
    ink = 0 if top.count(0) < 0.7 * len(top) else 15
    out = []
    for i in range(w // c):
        m = [1 if v == ink else 0 for v in crop(w, g, (i * c, 0, i * c + c, c))]
        if sum(m):
            out += [((i * c, r * c, i * c + c, r * c + c), m) for r in (1, 2)]
    return out


def square_stack(w, h, g):
    """the boxes of a picture that is squares one above the other (Chess's
    boards, a size's in a picture), or None"""
    if w < 64 or h % w or not 2 <= h // w <= 8:
        return None
    return [(0, r * w, w, r * w + w) for r in range(h // w)]


def crop(w, g, box):
    x0, y0, x1, y1 = box
    return [g[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]


def save_grey(path, w, h, g):
    im = Image.new('L', (w, h)); im.putdata([v * 17 for v in g]); im.save(path)


def save_mask(path, w, h, m):
    im = Image.new('L', (w, h)); im.putdata([255 if v else 0 for v in m]); im.save(path)


def key(w, h, g, m):
    return hashlib.sha1(bytes([w & 255, w >> 8, h & 255, h >> 8]) + bytes(g) + bytes(m or [])).hexdigest()


def slug(s):
    s = ''.join(c if c.isalnum() else '-' for c in s.lower()).strip('-')
    while '--' in s:
        s = s.replace('--', '-')
    return s or 'x'


def inputs(paths):
    """[(app, file name, bytes)] of each input"""
    out = []
    for p in paths:
        p = os.path.expanduser(p)
        if os.path.isdir(p):
            app = slug(os.path.basename(os.path.normpath(p)))
            for root, _, fs in sorted(os.walk(p)):
                for f in sorted(fs):
                    full = os.path.join(root, f); d = open(full, 'rb').read()
                    rel = os.path.relpath(full, p)
                    out += [(app, rel + '/' + a, b) for a, b in sis_files(d)] if is_sis(d) else [(app, rel, d)]
        else:
            d = open(p, 'rb').read()
            app = slug(os.path.splitext(os.path.basename(p))[0])
            out += [(app, a, b) for a, b in sis_files(d)] if is_sis(d) else [(app, os.path.basename(p), d)]
    return out


def extract(outdir, paths):
    os.makedirs(outdir, exist_ok=True)
    man_path = os.path.join(outdir, 'manifest.json')
    old = json.load(open(man_path)) if os.path.exists(man_path) else []
    known = {}                                   # (the entries there already, by their greys)
    for e in old:
        try:
            im = Image.open(os.path.join(outdir, e['src'])).convert('L')
            m = None
            if e.get('mask'):
                m = [1 if v > 127 else 0 for v in Image.open(os.path.join(outdir, e['mask'])).convert('L').getdata()]
            known[key(im.width, im.height, [round(v / 17) for v in im.getdata()], m)] = e
        except OSError:
            pass
    man, seen, counts = list(old), set(known), {}
    ids = {(e['app'], e['id']) for e in man}
    for app, name, d in inputs(paths):
        bms = file_bitmaps(d)
        if not bms:
            continue
        stem, ext = os.path.splitext(os.path.basename(name))
        base = slug(stem if ext.lower() in ('', '.mbm') else stem + ext)   # (Chess.mbm's chess-2, Chess.aif's chess-aif-2)
        os.makedirs(os.path.join(outdir, 'orig', app), exist_ok=True)
        k = 0
        while k < len(bms):
            a = bms[k]
            w, h, bpp, g = a
            m = mask_of(a, bms[k + 1]) if k + 1 < len(bms) and not mask_row_sheet(w, h, g) else None
            parts, sheet, stack = [], mask_row_sheet(w, h, g), square_stack(w, h, g)
            if sheet:
                m = None
                for j, (b, bm) in enumerate(sheet):
                    parts.append(('%s-%d-%d' % (base, k, j), b[2] - b[0], b[3] - b[1], crop(w, g, b), bm))
            elif m:
                boxes = pieces(w, h, m)
                if len(boxes) > 1 and len(boxes) <= 64 and not name.lower().endswith('.aif'):   # (an icon is one)
                    for j, b in enumerate(boxes):
                        parts.append(('%s-%d-%d' % (base, k, j), b[2] - b[0], b[3] - b[1], crop(w, g, b), crop(w, m, b)))
                else:
                    parts.append(('%s-%d' % (base, k), w, h, g, m))
            elif stack:
                for j, b in enumerate(stack):
                    parts.append(('%s-%d-%d' % (base, k, j), b[2] - b[0], b[3] - b[1], crop(w, g, b), None))
            else:
                parts.append(('%s-%d' % (base, k), w, h, g, None))
            for pid, pw, ph, pg, pm in parts:
                if len(set(pg)) < 2 or pw * ph < 64:
                    continue                     # (blank, or a dot)
                kk = key(pw, ph, pg, pm)
                if kk in seen:
                    continue
                seen.add(kk)
                n = 2
                while (app, pid) in ids:         # (two files of one name: Chess has a board's in each language)
                    pid = '%s.%d' % (pid.rsplit('.', 1)[0] if n > 2 else pid, n); n += 1
                ids.add((app, pid))
                src = 'orig/%s/%s.png' % (app, pid)
                save_grey(os.path.join(outdir, src), pw, ph, pg)
                e = {'app': app, 'id': pid, 'src': src, 'from': '%s #%d' % (name, k), 'w': pw, 'h': ph, 'bpp': bpp,
                     'kind': 'sprite' if pm else 'picture'}
                if pm:
                    e['mask'] = 'orig/%s/%s-mask.png' % (app, pid)
                    save_mask(os.path.join(outdir, e['mask']), pw, ph, pm)
                man.append(e); counts[app] = counts.get(app, 0) + 1
            k += 2 if m else 1
    json.dump(man, open(man_path, 'w'), indent=1)
    for app, n in sorted(counts.items()):
        print('%-12s %3d new' % (app, n))
    print('%d in %s (%d new)' % (len(man), man_path, sum(counts.values())))


# ----- contact sheets -----
def sheet(outdir):
    man = json.load(open(os.path.join(outdir, 'manifest.json')))
    try: font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 12)
    except OSError: font = ImageFont.load_default()
    for app in sorted({e['app'] for e in man}):
        es = [e for e in man if e['app'] == app]
        W, x, y, rowh, pos = 1400, 8, 8, 0, []
        for e in es:
            im = Image.open(os.path.join(outdir, e['src'])).convert('RGB')
            k = max(1, min(4, 120 // max(im.size)))          # (small ones bigger)
            im = im.resize((im.width * k, im.height * k), Image.NEAREST)
            if e.get('mask'):                                # (where it does not draw: pink)
                mk = Image.open(os.path.join(outdir, e['mask'])).convert('L').resize(im.size, Image.NEAREST)
                im = Image.composite(im, Image.new('RGB', im.size, (255, 190, 200)), mk)
            red = None
            if e.get('img') and os.path.exists(os.path.join(outdir, e['img'])):
                red = Image.open(os.path.join(outdir, e['img'])).convert('RGBA').resize(im.size, Image.LANCZOS)
            cw = im.width * (2 if red else 1) + (6 if red else 0)
            cw = min(cw, W - 16)
            if x + cw + 8 > W:
                x, y, rowh = 8, y + rowh + 22, 0
            pos.append((e, im, red, x, y, cw)); x += max(cw, 60) + 12; rowh = max(rowh, im.height)
        sh = Image.new('RGB', (W, y + rowh + 30), (40, 40, 46)); d = ImageDraw.Draw(sh)
        for e, im, red, x, y, cw in pos:
            d.text((x, y), e['id'][:max(6, (max(cw, 60) + 8) // 7)], fill=(220, 220, 220), font=font)
            sh.paste(im, (x, y + 15))
            if red:
                bg = Image.new('RGBA', red.size, (255, 190, 200, 255)); bg.alpha_composite(red)
                sh.paste(bg.convert('RGB'), (x + im.width + 6, y + 15))
        p = os.path.join(outdir, 'sheet-%s.png' % app); sh.save(p); print(p, '%d graphics' % len(es))


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ('extract', 'sheet', 'redraw', 'pack', 'all'):
        print(__doc__); sys.exit(2)
    cmd, outdir, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
    man = os.path.join(outdir, 'manifest.json')
    if cmd in ('extract', 'all'):
        extract(outdir, rest)
    if cmd in ('sheet', 'all'):
        sheet(outdir)
    if cmd in ('redraw', 'all'):
        subprocess.run([sys.executable, os.path.join(HERE, 'redrawpics.py'), 'gemini-3.1-flash-image', man,
                        os.path.join(outdir, 'color')] + (rest if cmd == 'redraw' else []), check=True)
        sheet(outdir)
    if cmd in ('pack', 'all'):
        out = rest[0] if cmd == 'pack' and rest else os.path.join(outdir, 'pictures.json.gz')
        subprocess.run([sys.executable, os.path.join(HERE, 'mkpicpack.py'), man, out], check=True)


if __name__ == '__main__':
    main()
