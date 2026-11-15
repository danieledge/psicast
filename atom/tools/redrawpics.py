#!/usr/bin/env python3
# redrawpics.py MODEL MANIFEST OUTDIR [id...] - each picture of a picture pack's manifest (its "src")
# redrawn in colour by a Gemini image model, its own shape kept, as OUTDIR/app-id.png, and the
# manifest's "img" set to it. One without a "hint" gets one first from a Gemini text model (and
# keeps it: edit it and redraw that one to do better). A sprite (one with a "mask") is redrawn on
# white and given the mask's shape as its transparency. styles.json beside the manifest, if there,
# gives each program's look ({"chess": "the pieces are one wooden set..."}), so its pieces match.
# Reads $GOOGLE_API_KEY. The pictures are their programs' authors': keep them local
# (pictures_local/, never committed). MIT licence.
import base64, io, json, os, sys, threading, urllib.request
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageDraw, ImageFilter, ImageOps
RATIOS = {"1:1": 1, "4:3": 4/3, "3:4": 3/4, "3:2": 1.5, "2:3": 2/3, "5:4": 1.25, "4:5": 0.8, "16:9": 16/9, "9:16": 9/16, "21:9": 21/9}
TEXT_MODEL = "gemini-flash-latest"
model, man_path, outdir = sys.argv[1:4]
only = set(sys.argv[4:])
here = os.path.dirname(os.path.abspath(man_path))
man = json.load(open(man_path))
styles = json.load(open(os.path.join(here, "styles.json"))) if os.path.exists(os.path.join(here, "styles.json")) else {}
lock = threading.Lock()
os.makedirs(outdir, exist_ok=True)


def gemini(name, parts, config, timeout=240):
    body = {"contents": [{"parts": parts}], "generationConfig": config}
    req = urllib.request.Request("https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent" % name,
                                 data=json.dumps(body).encode(), method="POST",
                                 headers={"x-goog-api-key": os.environ["GOOGLE_API_KEY"], "Content-Type": "application/json"})
    return json.load(urllib.request.urlopen(req, timeout=timeout))


def png(im):
    buf = io.BytesIO(); im.convert("RGB").save(buf, "PNG")
    return {"inline_data": {"mime_type": "image/png", "data": base64.b64encode(buf.getvalue()).decode()}}


def cutout(g, mask, px):
    """a sprite's transparency: the mask's shape, two Psion pixels round it
    either way decided by the redraw - its white ground (what joins the
    edge, near white) clear, the rest kept - its edge drawn in a little, so
    that no white of the ground shows round it, and softened"""
    pad = lambda m: ImageOps.expand(m, 3, 0)         # (so that a mask to the picture's edge shrinks there too)
    unpad = lambda m: m.crop((3, 3, m.width - 3, m.height - 3))
    inner = unpad(pad(mask).filter(ImageFilter.MinFilter(5))).resize(g.size, Image.BILINEAR).point(lambda v: 255 if v > 127 else 0)
    outer = unpad(pad(mask).filter(ImageFilter.MaxFilter(3))).resize(g.size, Image.BILINEAR).point(lambda v: 255 if v > 127 else 0)
    rgb = g.convert("RGB")
    hsv = rgb.convert("HSV").split()                  # (white, or a pale grey shadow on it)
    near = Image.eval(Image.merge("RGB", (Image.eval(hsv[1], lambda v: 255 if v < 28 else 0), Image.eval(hsv[2], lambda v: 255 if v >= 205 else 0),
                                          Image.new("L", g.size, 255))).convert("L"), lambda v: 255 if v >= 254 else 0)
    ground = near.copy()                              # (the near-white that reaches the edge)
    W, H = g.size
    for x, y in [(x, 0) for x in range(0, W, 8)] + [(x, H - 1) for x in range(0, W, 8)] + \
                [(0, y) for y in range(0, H, 8)] + [(W - 1, y) for y in range(0, H, 8)]:
        if ground.getpixel((x, y)) == 255:
            ImageDraw.floodfill(ground, (x, y), 128)
    bg = Image.eval(ground, lambda v: 255 if v == 128 else 0)
    keep = Image.composite(Image.new("L", g.size, 0), outer, bg)      # (in reach of the mask, not ground)
    al = Image.composite(Image.new("L", g.size, 255), keep, inner)    # (well inside it: always)
    k = max(3, round(px * 0.35) | 1)
    return al.filter(ImageFilter.MinFilter(k)).filter(ImageFilter.GaussianBlur(max(0.6, px * 0.25)))


def redraw(e):
    out = os.path.join(outdir, "%s-%s.png" % (e["app"], e["id"]))
    im = Image.open(os.path.join(here, e["src"])).convert("L"); w, h = im.size; a = w / h
    mask = Image.open(os.path.join(here, e["mask"])).convert("L") if e.get("mask") else None
    if mask:                                         # (a sprite: on white, where it does not draw)
        im = Image.composite(im, Image.new("L", im.size, 255), mask)
    name, r = min(RATIOS.items(), key=lambda kv: abs(kv[1] - a) / kv[1])
    k = max(1, 960 // max(w, h))
    big = im.resize((w * k, h * k), Image.NEAREST)
    pad = abs(r - a) / r > 0.12
    if pad:                                          # (on white margins, to the model's nearest shape)
        W, H = (round(h * k * r), h * k) if r > a else (w * k, round(w * k / r))
        c = Image.new("L", (W, H), 255); c.paste(big, ((W - w * k) // 2, (H - h * k) // 2)); big = c
    style = styles.get(e["app"], "")
    if not e.get("hint"):                            # (what it is, in a sentence, for the prompt)
        ask = ("This is a graphic from the 1990s Psion palmtop game '%s', %dx%d pixels in four greys%s, shown enlarged. "
               "In one sentence, say plainly what it shows, as a brief for an artist redrawing it in colour: the "
               "objects, their arrangement, any lettering exactly, and the colours they should be. %s"
               % (e["app"], w, h, ", a sprite drawn over the game's screen (the white round it is not part of it)" if mask else "", style))
        try:
            res = gemini(TEXT_MODEL, [{"text": ask}, png(big)], {"temperature": 0.2}, 300)
            e["hint"] = "".join(p.get("text", "") for p in res["candidates"][0]["content"]["parts"]).strip()
        except Exception as ex:
            print(e["id"], "no hint:", str(ex)[:200]); return
    kind = ("a small picture from a 1990s palmtop computer game, shown in only four shades of grey with dithering"
            if not mask and w * h > 20000 else
            "a small graphic from a 1990s palmtop computer game (a sprite, icon or game piece), drawn in four "
            "greys as pixel art and shown enlarged")
    prompt = ("This is %s. Redraw it as a clean, detailed, high-resolution image in natural, rich colour. Keep exactly "
              "the same scene, composition, framing, subjects, their sizes, outlines and positions, and the same style "
              "(a photo stays a photo, a cartoon stays a cartoon, a game piece stays a game piece, now finely rendered "
              "instead of pixelated); do not add or remove anything, and add no border or watermark. Keep any lettering "
              "exactly as it is, the same words and letters. It shows: %s." % (kind, e["hint"]))
    if style:
        prompt += " " + style
    if mask or w * h <= 20000:
        prompt += (" The regular checks of dots in it are only the old screen's way of showing a shade of grey (dithering):"
                   " draw them as smooth shading in the right colour, never as a chequered pattern or carving.")
    if mask:
        prompt += " It sits on plain white: keep the background plain white with no shadow on it, and the object exactly the same outline and size, filling it."
    elif pad:
        prompt += " The picture sits on plain white margins: keep the margins plain white and the picture the same size within them."
    try:
        res = gemini(model, [{"text": prompt}, png(big)], {"responseModalities": ["IMAGE"], "imageConfig": {"aspectRatio": name}})
        part = next(p for c in res.get("candidates", []) for p in c.get("content", {}).get("parts", []) if "inlineData" in p)
    except Exception as ex:
        print(e["id"], "failed:", str(ex)[:200]); return
    g = Image.open(io.BytesIO(base64.b64decode(part["inlineData"]["data"]))).convert("RGB")
    if pad:                                          # (the picture's part of it, back out)
        sx, sy = g.width / big.width, g.height / big.height
        x0, y0 = (big.width - w * k) // 2, (big.height - h * k) // 2
        g = g.crop((round(x0 * sx), round(y0 * sy), round((x0 + w * k) * sx), round((y0 + h * k) * sy)))
    if mask:
        g = g.convert("RGBA"); g.putalpha(cutout(g, mask, g.width / w))
    g.save(out)
    with lock:
        e["img"] = os.path.relpath(out, here)
        json.dump(man, open(man_path, "w"), indent=1)
    print(e["app"], e["id"], "%dx%d" % (w, h), name, "padded" if pad else "", "->", g.size, flush=True)


todo = []
for e in man:
    if only and e["id"] not in only and e["app"] + "/" + e["id"] not in only and e["app"] not in only:
        continue
    out = os.path.join(outdir, "%s-%s.png" % (e["app"], e["id"]))
    if os.path.exists(out) and not only:
        if not e.get("img"):
            e["img"] = os.path.relpath(out, here)
        continue
    todo.append(e)
print(len(todo), "to redraw", flush=True)
with ThreadPoolExecutor(6) as ex:
    list(ex.map(redraw, todo))
json.dump(man, open(man_path, "w"), indent=1)
