#!/usr/bin/env python3
# mkfonts.py - the 5mx's ROM fonts for the PsiCast viewer's "Redraw text
# sharply": the glyph bitmaps of the fonts EIKON draws its interface in,
# read from the SDK's eon14.gdr (byte for byte the 5mx ROM's Eon.gdr), for
# src/textmatch.js to find text on the Psion's screen exactly.
#   python3 tools/mkfonts.py [path/to/eon14.gdr]
# writes src/eonfonts_local.json and src/castfonts_local.h (the JSON
# gzipped, as C, for the firmware: castpage.h includes it when it is
# there). The bitmaps are Monotype's (Arial) and Psion's (the Eikon
# symbols), so both files are local ones: committed in the private
# repository, left out of the public one by tools/export-public.sh (a
# build without them answers 404 and the viewer dims the setting).
# MIT licence (this script; not the font data it writes).
#
# eon14.gdr is an EPOC font store (UIDs 0x10000037, 0x10000039; little
# endian; stream ids are file offsets):
#   header: 4 x u32 UIDs and checksum, u32 root stream
#   root:   u32 version, u32 collection uid, u32 pixel aspect, u32 data stream, copyright
#   data:   u32 n bitmaps, each: u32 uid, i8 posture, i8 weight, i8 flag, u8 cell
#           height, u8 ascent, u8 max width, u8 max normal width, u32 encoding,
#           u32 n sections, each: u16 first, u16 last, u32 offsets stream, u32 bitmaps stream;
#           then the typefaces (name, flags, [bitmap uid, width factor, height factor])
#   offsets: u32 n, per character: u16 offset, i8 ascent, i8 height, i8 left
#            adjust, i8 move (the advance), i8 right adjust, pad
#   bitmaps: u32 length, then per glyph (from a byte) an LSB-first bit stream:
#            1 bit flag, 4 bits count; flag 0: one row repeated count times,
#            1: count rows; a row is width (move - left - right) bits
import gzip, json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
GDR = os.path.join(HERE, "..", "..", "..", "psion_cpp_sdk_linux", "epoc_cpp_sdk", "epoc32", "release", "wins", "rel",
                   "z", "system", "fonts", "eon14.gdr")

# the fonts EIKON's interface uses on the 5mx, by bitmap uid: (name, the
# variants wanted: "plain" the bitmap, "bold" EPOC's algorithmic bold of
# it, drawn twice a pixel apart), and the Eikon symbols (the arrows of
# choice lists and cascades, the tick)
WANT = [
    (0x100001f3, "Arial", ["plain"]),             # cell 11: shortcuts, status lines
    (0x100001f0, "Arial", ["plain"]),             # cell 11 bold: buttons, column titles (a real bold)
    (0x100001f5, "Arial", ["plain", "bold"]),     # cell 15: dialogs, menus, lists
    (0x100001f6, "Arial", ["plain", "bold"]),     # cell 18: the System screen
    (0x100000f5, "Toolbar", ["bold"]),            # the toolbar's captions
    (0x100000cd, "Eikon", ["plain"]),             # symbols
]


def parse(d):
    u32 = lambda o: struct.unpack_from("<I", d, o)[0]
    if u32(0) != 0x10000037 or u32(4) != 0x10000039:
        raise SystemExit("mkfonts: not an EPOC font store")
    root = u32(16)
    copyright_at = root + 16
    o = u32(root + 12)
    n = u32(o); o += 4
    bitmaps = {}
    for _ in range(n):
        uid = u32(o); o += 4
        post, wt, flag, cell, asc, maxw, maxn = struct.unpack_from("<bbbBBBB", d, o); o += 7
        o += 4                                     # (the encoding)
        ns = u32(o); o += 4
        glyphs = {}
        for _ in range(ns):
            first, last, offs, bms = struct.unpack_from("<HHII", d, o); o += 12
            cnt, base = u32(offs), bms + 4
            for i in range(cnt):
                off, ga, gh, gl, gm, gr, _ = struct.unpack_from("<Hbbbbbb", d, offs + 4 + i * 8)
                w = gm - gl - gr
                rows, bit = [], (base + off) * 8

                def rd(k):
                    nonlocal bit
                    v = 0
                    for j in range(k):
                        v |= ((d[bit >> 3] >> (bit & 7)) & 1) << j
                        bit += 1
                    return v
                if w > 0 and gh > 0:
                    while len(rows) < gh:
                        many, c = rd(1), rd(4)
                        if many:
                            rows += [rd(w) for _ in range(c)]
                        else:
                            rows += [rd(w)] * c
                    rows = rows[:gh]
                if gm == 0 and gh == 0 and w == 0:
                    continue
                glyphs[first + i] = dict(asc=ga, h=gh, l=gl, mv=gm, w=w, rows=rows)
        bitmaps[uid] = dict(bold=wt, cell=cell, ascent=asc, glyphs=glyphs)
    return bitmaps, copyright_at


def copyright(d):
    # (the store's copyright lines, as plain text)
    s = d.decode("latin1")
    i = s.find("Copyright")
    if i < 0:
        return ""
    parts = re.findall(r"[ -~\xae]{4,}", s[i:i + 240])
    return re.sub(r"(?<=\.)\d+$", "", " ".join(p.replace("\xae", "").strip() for p in parts[:3]))


def columns(b, g):
    # a glyph as column codes over its font's cell rows (bit r: row r from the cell's top)
    top = b["ascent"] - g["asc"]
    cols = []
    for j in range(g["w"]):
        v = 0
        for i, row in enumerate(g["rows"]):
            if row >> j & 1:
                v |= 1 << (top + i)
        cols.append(v)
    return cols


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else GDR
    if not os.path.exists(path):
        raise SystemExit("mkfonts: no %s (the EPOC R5 SDK)" % path)
    d = open(path, "rb").read()
    bitmaps, _ = parse(d)
    fonts = []
    for uid, name, variants in WANT:
        b = bitmaps[uid]
        gl = []
        for c, g in sorted(b["glyphs"].items()):
            if c < 32 or c > 255:
                continue
            gl.append([c, g["l"], g["mv"]] + columns(b, g))
        fonts.append({"name": name, "uid": "%08x" % uid, "cell": b["cell"], "ascent": b["ascent"],
                      "bold": 1 if b["bold"] else 0, "variants": variants, "sym": 1 if name == "Eikon" else 0, "glyphs": gl})
    pack = {"format": "psicast-fonts", "version": 1, "from": "eon14.gdr (the 5mx ROM's Eon.gdr)",
            "copyright": copyright(d), "fonts": fonts}
    js = json.dumps(pack, separators=(",", ":")).encode()
    open(os.path.join(SRC, "eonfonts_local.json"), "wb").write(js)
    gz = gzip.compress(js, compresslevel=9, mtime=0)
    lines = ["\t" + ",".join("0x%02x" % x for x in gz[i:i + 20]) + "," for i in range(0, len(gz), 20)]
    out = ["// castfonts_local.h - generated by tools/mkfonts.py from the SDK's eon14.gdr: do not edit.",
           "// The 5mx ROM fonts' glyphs (gzipped JSON) for the viewer's sharp text. The",
           "// bitmaps are Monotype's and Psion's: a local file, not published.",
           "#ifndef ATOM_CASTFONTS_LOCAL_H", "#define ATOM_CASTFONTS_LOCAL_H", "",
           "static const unsigned char kCastFontsGz[] = {", "\n".join(lines), "\t};",
           "static const unsigned kCastFontsGzLen = %d;" % len(gz), "", "#endif", ""]
    open(os.path.join(SRC, "castfonts_local.h"), "w").write("\n".join(out))
    print("eonfonts_local.json: %d fonts, %d glyphs, %d bytes (%d gzipped)" %
          (len(fonts), sum(len(f["glyphs"]) for f in fonts), len(js), len(gz)))


if __name__ == "__main__":
    sys.exit(main())
