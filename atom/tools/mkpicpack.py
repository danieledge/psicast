#!/usr/bin/env python3
"""mkpicpack.py MANIFEST.json OUT.json.gz [--scale 3] [--sprite-scale 5] [--format avif] [--quality N]

The viewer's picture pack (Replace known pictures, 0.22): pictures that
programs draw (PsiTris's title pictures, Chess's, Monopoly's) with a redrawn
version of each to show in their place. MANIFEST is a list of
  {"app": "psitris", "id": "sled", "src": "the picture as the program has it
   (a PNG in greys: 0..255, the Psion's 16 greys as grey * 17)",
   "img": "its redrawn version (any size, the same shape)",
   "mask": "a sprite's: a PNG, white where it draws (optional)"}
The pack, gzipped: "PCP2", the length of the JSON that follows (u32, little
end first), the JSON, then the images one after another:
  {"v": 2, "type": "image/avif", "pics": [{"app", "id", "w", "h",
     "g": the Psion greys, two to a byte (low nibble first), base64,
     "a": anchors [[y, x, hash, n]]: runs of n pixels (16, or 8 in a small
          sprite) in rows across the picture, FNV-1a of their greys, for the
          viewer to find it by; never where a sprite does not draw,
     "m": a sprite's mask, a bit a pixel (1 where it draws, low bit first), base64,
     "img": [where in the images, its length], at scale times the Psion's size}]}
(Version 1, still read by the viewer, was the JSON alone, each "img" a data:
URL.) AVIF at 50 is as good to see as WebP at 70 and a third smaller: the
Atom has 1.5 MB for a pack.
The pictures are the programs' authors': a pack is for your own Atom (POST it
to /pack), never for the repository. MIT licence.
"""
import argparse, base64, gzip, io, json, os, sys
from PIL import Image

RUN = 16


def fnv(vals):
    h = 0x811C9DC5
    for v in vals:
        h = ((h ^ v) * 0x01000193) & 0xFFFFFFFF
    return h


def anchors(g, w, h, m=None, n=10):
    """runs in n rows spread down the picture, in each row the one with the
    most different greys in each third of it (one if it is narrow), so that
    a picture partly covered (Chess's background, tiled behind its board and
    toolbar) is still found by those that show; never one whose first,
    second and last are one grey, nor one off a sprite's mask; 8-pixel ones
    if 16 do not fit"""
    for run in (RUN, 8):
        out = []
        parts = 3 if w >= run * 6 else 1
        rows = sorted({min(h - 1, int(h * (k + 0.5) / n)) for k in range(n)})
        if h > n:                                # (the others, for a sprite the rows above miss)
            rows += [y for y in range(h) if y not in rows]
        for y in rows:
            if len(out) >= n * parts:
                break
            for part in range(parts):
                x0, x1 = w * part // parts, w * (part + 1) // parts
                best = None
                for x in range(x0, min(x1, w - run + 1)):
                    if m and not all(m[y * w + x:y * w + x + run]):
                        continue
                    r = g[y * w + x:y * w + x + run]
                    if r[0] == r[1] == r[run - 1]:
                        continue                 # (the viewer passes over these, flat or not, to save time)
                    d = len(set(r)) * 100 + sum(1 for i in range(run - 1) if r[i] != r[i + 1])
                    if best is None or d > best[0]:
                        best = (d, x, r)
                if best and len(set(best[2])) > 1:
                    out.append([y, best[1], fnv(best[2]), run])
        if len(out) >= min(4, n):
            return sorted(out)
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest"); ap.add_argument("out")
    ap.add_argument("--scale", type=float, default=3); ap.add_argument("--quality", type=int, default=0)
    ap.add_argument("--format", choices=("avif", "webp"), default="avif")
    ap.add_argument("--sprite-scale", type=float, default=8)
    a = ap.parse_args()
    pics, blobs = [], bytearray()
    here = os.path.dirname(os.path.abspath(a.manifest))      # (its paths are from its own folder)
    for e in json.load(open(a.manifest)):
        src = Image.open(os.path.join(here, e["src"])).convert("L")
        w, h = src.size
        g = [round(v / 17) for v in src.getdata()]
        if not e.get("img"):
            print("%s/%s: not redrawn yet, left out" % (e["app"], e["id"])); continue
        m = None
        if e.get("mask"):
            m = [1 if v > 127 else 0 for v in Image.open(os.path.join(here, e["mask"])).convert("L").getdata()]
        nib = bytearray((g[i] | ((g[i + 1] if i + 1 < len(g) else 0) << 4)) for i in range(0, len(g), 2))
        im = Image.open(os.path.join(here, e["img"])).convert("RGBA" if m else "RGB")
        sc = min(6, max(a.scale, 360 / max(w, h)))      # (a small one larger: it is shown big on a phone)
        if m:
            sc = a.sprite_scale                         # (a piece on a board: drawn often, and looked at closely)
        tw = min(im.width, round(w * sc)); th = max(1, round(tw * h / w))
        im = im.resize((tw, th), Image.LANCZOS)
        buf = io.BytesIO()
        if a.format == "avif":
            im.save(buf, "AVIF", quality=a.quality or (50 if m or w * h < 20000 else 42), speed=4)   # (a large one: much the most of a pack)
        else:
            im.save(buf, "WEBP", quality=a.quality or 70, method=6)
        anc = anchors(g, w, h, m)
        if len(anc) < 2:
            print("%s/%s: nothing to find it by, left out" % (e["app"], e["id"])); continue
        q = {"app": e["app"], "id": e["id"], "w": w, "h": h, "g": base64.b64encode(bytes(nib)).decode(),
             "a": anc, "img": [len(blobs), buf.tell()]}
        blobs += buf.getvalue()
        if m:
            q["m"] = base64.b64encode(bytes(sum(m[i + j] << j for j in range(8) if i + j < len(m)) for i in range(0, len(m), 8))).decode()
        pics.append(q)
        print("%-10s %-14s %3dx%-3d anchors %2d  image %4dx%-4d %6d bytes" % (e["app"], e["id"], w, h, len(anc), tw, th, buf.tell()))
    js = json.dumps({"v": 2, "type": "image/" + a.format, "pics": pics}, separators=(",", ":")).encode()
    raw = b"PCP2" + len(js).to_bytes(4, "little") + js + bytes(blobs)
    gz = gzip.compress(raw, 9)
    open(a.out, "wb").write(gz)
    print("%d pictures, %d bytes (%d gzipped)" % (len(pics), len(raw), len(gz)))


if __name__ == "__main__":
    main()
