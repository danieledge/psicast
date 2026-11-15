#!/usr/bin/env python3
# cast_page_shot.py - PsiCast's viewer page (src/viewer.html, as the
# firmware serves it) in headless Chromium at iPhone sizes, against
# hostmodem and the fake Psion of test_cast_e2e.py: screenshots in portrait
# and landscape, with control and the key bar, in each theme, and checks
# that the page's canvas shows the Psion's picture pixel for pixel and that
# a tap on it reaches the Psion. Needs Python's playwright and its Chromium.
#   make -C hosttest hostmodem libpccodec.so && python3 hosttest/cast_page_shot.py [outdir]
# MIT licence.
import os, socket, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_cast_e2e as e2e
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))


def draw_screen(ps):
    """Something that looks like a Psion: a Word document, the toolbar, a dialog."""
    from PIL import Image, ImageDraw, ImageFont
    im = Image.new("L", (640, 240), 255)
    d = ImageDraw.Draw(im)
    try:
        f = ImageFont.truetype("DejaVuSans.ttf", 11)
        fb = ImageFont.truetype("DejaVuSans-Bold.ttf", 11)
    except OSError:
        f = fb = ImageFont.load_default()
    d.rectangle((0, 0, 639, 15), fill=0)
    d.text((6, 1), "Word   File  Edit  View  Insert  Text  Tools", font=fb, fill=255)
    lines = ["Dear reader,", "", "PsiCast shows this screen on the phone, live: the Atom",
             "decodes each update and sends every viewer what changed.",
             "Scrolling text goes as a move, so it costs almost nothing.", "",
             "Yours, the Psion Series 5mx"]
    for i, t in enumerate(lines):
        d.text((14, 24 + i * 15), t, font=f, fill=0)
    # the toolbar on the right, in greys
    d.rectangle((560, 16, 639, 239), fill=200)
    for i, t in enumerate(("Bold", "Italic", "Style", "Print")):
        d.rectangle((566, 24 + i * 48, 633, 64 + i * 48), fill=240, outline=60)
        d.text((574, 36 + i * 48), t, font=f, fill=40)
    d.text((14, 222), "Page 1 of 1   Words 54", font=f, fill=110)
    px = im.load()
    for y in range(240):
        for x in range(0, 640, 2):
            a, b = px[x, y] * 15 // 255, px[x + 1, y] * 15 // 255
            ps.fb[y * 320 + x // 2] = a | (b << 4)


def put_greys(ps, g):
    """A 640x240 list of greys (0..15) into the fake Psion's frame buffer."""
    for y in range(240):
        for x in range(0, 640, 2):
            ps.fb[y * 320 + x // 2] = g[y][x] | (g[y][x + 1] << 4)


FONTS = os.path.join(HERE, "..", "src", "eonfonts_local.json")


def eon_fonts():
    """The 5mx's ROM fonts (tools/mkfonts.py; a local file), or None."""
    import json
    return json.load(open(FONTS)) if os.path.exists(FONTS) else None


def eon_put(g, pack, name, cell, bold, x, y, s, c, fake=False):
    """s in a ROM font (fake: EPOC's algorithmic bold) at pen x, cell top y, in grey c, into g (rows)."""
    f = next(f for f in pack["fonts"] if f["name"] == name and f["cell"] == cell and bool(f["bold"]) == bool(bold))
    by = {e[0]: e for e in f["glyphs"]}
    for ch in s:
        e = by[ord(ch)]
        cols, mv = e[3:], e[2]
        if fake and cols:
            cols = [v | (cols[j - 1] if j else 0) for j, v in enumerate(cols)] + [cols[-1]]
            mv += 1
        for j, v in enumerate(cols):
            for k in range(cell):
                if v >> k & 1 and 0 <= y + k < 240 and 0 <= x + e[1] + j < 640:
                    g[y + k][x + e[1] + j] = c
        x += mv
    return x


def draw_eikon(ps, eon=None):
    """A 5mx screen drawn the way EIKON draws it, in 4 greys (the geometry of a
    real one, from the emulator): a page of text, a dialog with its title bar,
    a selected prompt and a button, a menu (a dialog's border, no title bar),
    a scroll bar (arrows, thumb, shaft) and the toolbar with its buttons.
    #cccccc is a checkerboard of greys 10 and 15. With eon (the ROM fonts),
    the writing is in the 5mx's own fonts, and the dialog has a choice
    list with its arrows (the Eikon font's). Returns the points the Series 7
    and Modern themes' checks look at."""
    from PIL import Image, ImageDraw, ImageFont
    try:
        f = ImageFont.truetype("DejaVuSans.ttf", 11)
    except OSError:
        f = ImageFont.load_default()
    g = [[15] * 640 for _ in range(240)]

    def box(x0, y0, x1, y1, v):
        for y in range(max(0, y0), min(240, y1 + 1)):
            for x in range(max(0, x0), min(640, x1 + 1)):
                g[y][x] = v

    def light(x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                g[y][x] = 10 if (x + y) & 1 else 15

    def text(x, y, t, small=False):
        if eon:
            if small:
                eon_put(g, eon, "Arial", 11, 1, x, y + 1, t, 0)
            else:
                eon_put(g, eon, "Arial", 15, 0, x, y - 1, t, 0)
            return
        im = Image.new("L", (640, 240), 0)
        ImageDraw.Draw(im).text((x, y), t, font=f, fill=255)
        px = im.load()
        for yy in range(max(0, y - 2), min(240, y + 16)):
            for xx in range(x, min(640, x + 8 * len(t))):
                if px[xx, yy] > 127:
                    g[yy][xx] = 0

    def button(x, y, r, b, label=None):
        box(x, y, r, b, 0)                         # 1 black all round
        box(x + 1, y + 1, r - 2, y + 2, 15)        # 2 white at the top and left
        box(x + 1, y + 1, x + 2, b - 2, 15)
        box(r - 2, y + 1, r - 1, b - 1, 5)         # 2 dark grey at the right and bottom
        box(x + 1, b - 2, r - 1, b - 1, 5)
        g[y + 1][r - 2] = 15
        g[b - 2][x + 1] = 15
        light(x + 3, y + 3, r - 3, b - 3)
        if label:
            text(x + 8, y + 5, label, small=True)

    def sbutton(x, y, b, arrow=0):                 # a scroll bar's arrow or thumb: 23 wide
        box(x, y, x + 22, b, 0)
        box(x + 1, y + 1, x + 21, b - 1, 15)
        box(x + 20, y + 2, x + 21, b - 1, 5)
        box(x + 2, b - 2, x + 21, b - 1, 5)
        light(x + 3, y + 3, x + 19, b - 3)
        for k in range(5):                         # (its arrow: up, or down)
            yy = (y + 7 + k) if arrow < 0 else (b - 7 - k) if arrow > 0 else None
            if yy is not None:
                box(x + 11 - k, yy, x + 11 + k, yy, 0)

    def thick(x0, y0, r, b):                       # EThickDeepRaisedWithOutline
        box(x0, y0, r, b, 0)
        box(x0 + 2, y0 + 2, r - 2, b - 2, 15)
        box(r - 4, y0 + 2, r - 2, b - 2, 5)
        box(x0 + 2, b - 4, r - 2, b - 2, 5)
        box(x0 + 4, y0 + 4, r - 5, y0 + 4, 10)
        box(x0 + 4, y0 + 4, x0 + 4, b - 5, 10)
        box(x0 + 5, y0 + 5, r - 5, b - 5, 15)

    for i, t in enumerate(("Dear reader,", "PsiCast shows this screen on the phone, live.", "A page stays black on white.")):
        text(10, 4 + i * 15, t)
    # a dialog
    thick(60, 50, 459, 200)
    light(65, 55, 454, 74)
    text(72, 58, "About this Psion")
    box(65, 75, 454, 75, 10)                     # (the grey line under the title)
    text(80, 90, "PsiCast shows the screen on a phone")
    button(220, 160, 290, 182, "Continue")
    text(80, 110, "Theme")                       # the selected prompt: black, white writing
    for y in range(108, 125):
        for x in range(76, 141):
            g[y][x] = 0 if g[y][x] else 15
    # a scroll bar down the page: its arrows, a white shaft, the thumb
    box(530, 60, 530, 230, 0)
    box(552, 60, 552, 230, 0)
    box(531, 61, 551, 229, 15)
    sbutton(530, 60, 83, -1)
    sbutton(530, 120, 160)
    sbutton(530, 184, 207, 1)
    sbutton(530, 207, 230, 1)
    # a list box, the scroll bar its right-hand side, with its selected row
    # drawn right up to the frame and the scroll bar (black lines either end)
    box(463, 60, 463, 230, 0)
    box(463, 60, 530, 60, 0)
    box(463, 230, 530, 230, 0)
    text(468, 70, "Inbox")
    text(468, 141, "Sent")
    for y in range(140, 157):
        for x in range(464, 530):
            g[y][x] = 0 if g[y][x] else 15
    # a menu: the same border, no title
    thick(330, 4, 520, 44)
    text(342, 14, "Preferences...")
    # a selection without the focus: dark grey, white writing (a cascade's parent)
    box(320, 110, 400, 126, 5)
    text(326, 112, "Week")
    for y in range(110, 127):
        for x in range(320, 401):
            if g[y][x] == 0:
                g[y][x] = 15
    # a busy message, bottom left: a black box with white writing, let into
    # a black border by a white line (and a grey one on its shadowed sides)
    box(4, 214, 139, 235, 0)
    box(6, 216, 137, 233, 15)
    box(7, 233, 137, 233, 10)
    box(137, 217, 137, 233, 10)
    text(12, 219, "Opening file...")
    for y in range(217, 233):
        for x in range(7, 137):
            g[y][x] = 0 if g[y][x] else 15
    # a dithered light grey strip on the page (a menu bar's)
    light(200, 215, 300, 235)
    # the toolbar
    box(570, 0, 570, 239, 0)
    box(570, 0, 639, 0, 0)
    box(639, 0, 639, 239, 0)
    box(570, 239, 639, 239, 0)
    text(590, 6, "Word")
    for k, t in enumerate(("Bold", "Italic", "Print")):
        button(570, 25 + 36 * k, 639, 61 + 36 * k, t)
    if eon:
        # a choice list in the dialog: its arrows, and its value in a box
        sym = next(f for f in eon["fonts"] if f["sym"])
        eon_put(g, eon, sym["name"], sym["cell"], 0, 160, 128, "3", 0)
        box(176, 127, 300, 145, 10)
        box(177, 128, 299, 144, 15)
        eon_put(g, eon, "Arial", 15, 0, 182, 129, "Classic", 0)
        eon_put(g, eon, sym["name"], sym["cell"], 0, 306, 128, "1", 0)
    put_greys(ps, g)
    return {"title": (70, 70), "titletext": next((x, y) for y in range(55, 75) for x in range(72, 200) if g[y][x] == 0),
            "body": (68, 140), "face": (224, 164), "tbar": (600, 3), "tface": (575, 30), "page": (5, 100),
            "menu": (500, 30), "sel": (76, 108), "seltext": next((x, y) for y in range(110, 124) for x in range(80, 140) if g[y][x] == 15),
            "lsel": (465, 141), "lframe": (463, 100), "shaft": (540, 100), "sface": next((x, y) for x in range(540, 545) for y in (140,) if g[y][x] == 10),
            "dsel": (322, 112), "busy": (9, 218), "dither": (250, 225),
            "grey": g}


def put_rows(ps, g, y0, y1):
    """Rows y0..y1-1 of a 640x240 list of greys into the fake Psion's frame buffer."""
    for y in range(y0, y1):
        for x in range(0, 640, 2):
            ps.fb[y * 320 + x // 2] = g[y][x] | (g[y][x + 1] << 4)


MODERN_STOPS = [[0, [29, 36, 51]], [5, [107, 115, 133]], [10, [176, 183, 196]], [15, [251, 250, 247]]]
MODERN_PALE = [239, 241, 243]                   # (its colour for a checkerboard of 10 and 15)


def mrgb(v):
    """The Modern theme's colour for grey v."""
    for a, b in zip(MODERN_STOPS, MODERN_STOPS[1:]):
        if a[0] <= v <= b[0]:
            return [round(a[1][i] + (b[1][i] - a[1][i]) * (v - a[0]) / (b[0] - a[0])) for i in range(3)]


def mflat(g, x, y, rgb=None, pale=None):
    """The Modern theme's colour (or, with rgb and pale, another flat
    theme's) for the pixel at x, y of the greys g (a list of rows) with no
    role: viewer.html's paintRow for a flat theme."""
    rgb = rgb or mrgb
    pale = MODERN_PALE if pale is None else pale
    v = g[y][x]
    l = g[y][x - 1] if x > 0 else -1
    r = g[y][x + 1] if x + 1 < 640 else -1
    a = g[y - 1][x] if y > 0 else -1
    b = g[y + 1][x] if y + 1 < 240 else -1
    t = l if l == r and l >= 0 else a if a == b and a >= 0 else l if l >= 0 and l in (a, b) else r if r >= 0 and r in (a, b) else -1
    if t < 0 or t == v:
        return rgb(v)
    nv = [l, r, a, b].count(v)
    if nv == 0 or (nv == 1 and abs(t - v) <= 5 and [l, r, a, b].count(t) == 3):
        dv = sum(1 for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)) if 0 <= x + dx < 640 and 0 <= y + dy < 240 and g[y + dy][x + dx] == v)
        if dv >= 2:
            return pale if pale and {v, t} == {10, 15} else [(rgb(v)[i] + rgb(t)[i]) >> 1 for i in range(3)]
    return rgb(v)


def screen_px(page, x0, y0, x1, y1):
    """#screen's device pixels under Psion pixels x0,y0..x1-1,y1-1 (inset by a device pixel), as RGB triples."""
    return page.evaluate("""([x0, y0, x1, y1]) => {
        const c = document.getElementById('screen'), sx = c.width / 640, sy = c.height / 240;
        const X0 = Math.ceil(x0 * sx) + 1, Y0 = Math.ceil(y0 * sy) + 1, X1 = Math.floor(x1 * sx) - 1, Y1 = Math.floor(y1 * sy) - 1;
        const d = c.getContext('2d').getImageData(X0, Y0, X1 - X0, Y1 - Y0).data, o = [];
        for (let i = 0; i < d.length; i += 4) o.push([d[i], d[i + 1], d[i + 2]]);
        return o; }""", [x0, y0, x1, y1])


def settle(page, ms=900):
    page.wait_for_timeout(ms)
    page.evaluate("() => window.psicastDebug.redraw()")
    page.wait_for_timeout(250)


def frames(page, n=6):
    """The mean time of n whole presents, in ms, and the path they took."""
    return page.evaluate("""(n) => new Promise(ok => {
        const p = window.psicastPerf, n0 = p.n, ms0 = p.ms;
        let k = 0;
        const go = () => { if (k++ >= n) return ok([(p.ms - ms0) / Math.max(1, p.n - n0), p.path]); window.psicastDebug.redraw(); requestAnimationFrame(() => setTimeout(go, 30)); };
        go(); })""", n)


def setting(page, up=None, text=None, chrome=None, theme=None):
    """The viewer's settings, through its sheet, as a person would."""
    page.evaluate("() => document.getElementById('bMore').click()")
    page.wait_for_timeout(150)
    if theme:
        page.click('.sw[data-id="%s"]' % theme)
    if up:
        page.click('#upscale button[data-up="%s"]' % up)
    on = lambda i: page.evaluate("i => document.getElementById(i).classList.contains('on')", i)
    if text is not None and on("tText") != text:
        page.click("#tText")
    if chrome is not None and on("tChrome") != chrome:
        page.click("#tChrome")
    page.click("#bClose")


def layer_checks(b, iphone, url, page, ps, out, shots):
    """Upscaling (High-res: the xBRZ shader) and the sharp redraws (boxes and
    lines, text), on a screen drawn the way EIKON draws it in the 5mx's own
    fonts: the settings, the paths taken, what is and isn't redrawn, the
    model untouched, a lost WebGL context, timings, and screenshots in
    landscape full screen and portrait."""
    # (the setting as it was: Smooth the Psion's pixels, off -> Pixels, on -> Smooth)
    for old, want in ((False, "pixels"), (True, "smooth")):
        c2 = b.new_context(**iphone)
        p2 = c2.new_page()
        p2.add_init_script("localStorage.setItem('psicast.smooth', '%s')" % ("true" if old else "false"))
        p2.goto(url)
        p2.wait_for_timeout(400)
        got = p2.evaluate("() => JSON.parse(localStorage.getItem('psicast.upscale'))")
        e2e.check(got == want, "the old Smooth setting %s becomes Upscaling %s (%r)" % (old, want, got))
        c2.close()
    # the settings as they start: Smooth, the redraws off
    page.evaluate("() => document.getElementById('bMore').click()")
    page.wait_for_timeout(200)
    st = page.evaluate("""() => ({ on: [...document.querySelectorAll('#upscale button.on')].map(b => b.dataset.up),
        off: [...document.querySelectorAll('#upscale button.off')].map(b => b.dataset.up),
        text: document.getElementById('tText').classList.contains('on'), chrome: document.getElementById('tChrome').classList.contains('on'),
        note: document.getElementById('upNote').textContent, gl2: !!document.createElement('canvas').getContext('webgl2') })""")
    page.evaluate("() => document.getElementById('upscale').scrollIntoView({ block: 'start' })")
    page.wait_for_timeout(150)
    page.locator(".card").first.screenshot(path=os.path.join(out, "10-settings-viewing.png"))
    shots.append("10-settings-viewing.png")
    page.click("#bClose")
    e2e.check(st["on"] == ["smooth"] and not st["text"] and not st["chrome"], "Upscaling starts as Smooth, the redraws off (%r)" % st)
    e2e.check((st["off"] == []) == st["gl2"], "High-res is dimmed only without WebGL 2 (%r)" % st)
    gl2 = st["gl2"]
    # without WebGL 2, High-res is dimmed and says why
    nb = b.browser_type.launch(args=["--disable-webgl", "--disable-3d-apis"])
    np_ = nb.new_context(**iphone).new_page()
    np_.goto(url)
    np_.wait_for_timeout(400)
    np_.evaluate("() => document.getElementById('bMore').click()")
    np_.wait_for_timeout(200)
    np_.click('#upscale button[data-up="hires"]')
    no = np_.evaluate("""() => ({ off: document.querySelector('#upscale button[data-up=hires]').classList.contains('off'),
        on: [...document.querySelectorAll('#upscale button.on')].map(b => b.dataset.up), note: document.getElementById('upNote').textContent })""")
    e2e.check(no["off"] and no["on"] == ["smooth"] and "WebGL 2" in no["note"], "without WebGL 2, High-res is dimmed with the reason, and not taken (%r)" % no)
    nb.close()
    print("cast_page_shot: WebGL 2 in this Chromium: %s" % gl2)
    eon = eon_fonts()
    if not eon:
        print("cast_page_shot: no src/eonfonts_local.json: the sharp text is not checked")
    saved = bytes(ps.fb)
    pts = draw_eikon(ps, eon)
    ps.update()
    setting(page, theme="paper")
    settle(page)
    page.screenshot(path=os.path.join(out, "11-landscape-full-smooth.png"))
    shots.append("11-landscape-full-smooth.png")
    ms_smooth = frames(page)
    # High-res: the shader's path (here WebGL 2 is SwiftShader's, on the CPU)
    setting(page, up="hires")
    settle(page, 1500)
    ms_hires = frames(page)
    if gl2:
        e2e.check(ms_hires[1] == "hires", "High-res draws with the shader (%r)" % (ms_hires,))
    page.screenshot(path=os.path.join(out, "12-landscape-full-hires.png"))
    shots.append("12-landscape-full-hires.png")
    dither = (210, 219, 290, 232)                   # (the dithered strip on the page: nothing the layers redraw)
    pic0 = screen_px(page, *dither)
    # ...and the sharp boxes, lines and text
    setting(page, text=True, chrome=True)
    settle(page, 1800)
    ms_all = frames(page)
    runs = page.evaluate("() => window.psicastDebug.runs()")
    parts = page.evaluate("() => window.psicastDebug.parts()")
    texts = [r["text"] for r in runs]
    print("cast_page_shot: the layers found %d runs %r and the parts %r" % (len(runs), texts, parts))
    e2e.check(parts.count("k5") >= 2 and parts.count("k3") >= 5 and "up" in parts and "down" in parts,
              "the sharp boxes and lines: the dialog's and menu's borders, the buttons, the scroll bar's arrows (%r)" % parts)
    if eon:
        for t in ("About this Psion", "PsiCast shows the screen on a phone", "Classic", "Continue", "Theme", "<left>", "<right>"):
            e2e.check(t in texts, "the sharp text finds %r (%r)" % (t, texts))
    page.screenshot(path=os.path.join(out, "13-landscape-full-hires-sharp.png"))
    shots.append("13-landscape-full-hires-sharp.png")
    page.locator("#frame").screenshot(path=os.path.join(out, "13-frame-hires-sharp.png"))
    shots.append("13-frame-hires-sharp.png")
    e2e.check(screen_px(page, *dither) == pic0, "the redraws leave what they don't recognise as it was (the dithered strip)")
    # faces and title bars: one flat light grey (Paper: the mean of 10 and 15), not the dither
    for what, box in (("a button's face", (224, 164, 227, 178)), ("the title bar", (300, 57, 450, 73))):
        px = screen_px(page, *box)
        e2e.check(px and all(p == [212, 212, 212] for p in px), "%s is flat (%r)" % (what, sorted(set(map(tuple, px)))[:4]))
    # the model is untouched: screenshots and videos are the Psion's picture exactly
    model = page.evaluate("() => { const d = document.getElementById('model').getContext('2d').getImageData(0, 0, 640, 240).data, o = []; for (let i = 0; i < d.length; i += 4) o.push(d[i]); return o; }")
    want = [v * 17 for byte in ps.fb for v in (byte & 15, byte >> 4)]
    e2e.check(model == want, "with every redraw on, the model is still the Psion's picture pixel for pixel")
    # Modern: the parts in its colours, flat, the text in the theme's colour for its part
    setting(page, theme="modern")
    settle(page, 1200)
    page.screenshot(path=os.path.join(out, "14-landscape-full-hires-sharp-modern.png"))
    shots.append("14-landscape-full-hires-sharp-modern.png")
    px = screen_px(page, 300, 57, 450, 73)
    e2e.check(px and all(p == [31, 58, 95] for p in px), "Modern: the title bar is flat navy (%r)" % sorted(set(map(tuple, px)))[:4])
    # a change to a line of text: only its rows are looked at again
    setting(page, theme="paper")
    settle(page, 600)
    g = pts["grey"]
    for y in range(80, 106):
        for x in range(76, 450):
            g[y][x] = 15
    eon_put(g, eon, "Arial", 15, 0, 80, 89, "PsiCast shows the screen at last", 0) if eon else None
    put_rows(ps, g, 80, 106)
    ps.update()
    page.wait_for_timeout(1200)
    m = page.evaluate("() => [window.psicastPerf.match, window.psicastPerf.matchRows]")
    texts = [r["text"] for r in page.evaluate("() => window.psicastDebug.runs()")]
    if eon:
        e2e.check("PsiCast shows the screen at last" in texts and "PsiCast shows the screen on a phone" not in texts,
                  "a line that changed is read again (%r)" % texts)
    e2e.check(m[1] is not None and m[1] < 100, "...and only the rows round it (%r rows, %.1f ms)" % (m[1], m[0] or 0))
    print("cast_page_shot: presents: Smooth %.1f ms, High-res %.1f ms (%s), High-res with the redraws %.1f ms; a strip of %d rows matched in %.1f ms"
          % (ms_smooth[0], ms_hires[0], ms_hires[1], ms_all[0], m[1], m[0]))
    # a lost WebGL context: Smooth meanwhile, High-res again when it is back
    if gl2:
        page.evaluate("() => { window.__lose = window.psicastDebug.hires().debugLose(); }")
        settle(page, 400)
        lost = page.evaluate("() => window.psicastPerf.path")
        page.evaluate("() => window.__lose && window.__lose.restoreContext()")
        settle(page, 800)
        back = page.evaluate("() => window.psicastPerf.path")
        e2e.check(lost == "smooth" and back == "hires", "a lost WebGL context: Smooth meanwhile, then High-res again (%s, %s)" % (lost, back))
    # every Upscaling with the redraws on
    for up in ("pixels", "smooth"):
        setting(page, up=up)
        settle(page, 600)
        e2e.check(page.evaluate("() => window.psicastPerf.path") == up, "Upscaling %s with the redraws on draws that way" % up)
        print("cast_page_shot: presents: %s with the redraws %.1f ms" % (up, frames(page)[0]))
        page.locator("#frame").screenshot(path=os.path.join(out, "15-frame-%s-sharp.png" % up))
        shots.append("15-frame-%s-sharp.png" % up)
    # portrait (below 2x: Pixels whatever is chosen), before and after
    setting(page, up="hires", text=False, chrome=False)
    page.click("#exit")
    page.wait_for_timeout(400)
    page.set_viewport_size({"width": 390, "height": 844})
    settle(page, 900)
    page.screenshot(path=os.path.join(out, "16-portrait-plain.png"))
    shots.append("16-portrait-plain.png")
    setting(page, text=True, chrome=True)
    settle(page, 1500)
    e2e.check(page.evaluate("() => window.psicastPerf.path") == "pixels", "portrait, below 2x: sharp bilinear (Pixels)")
    page.screenshot(path=os.path.join(out, "17-portrait-hires-sharp.png"))
    shots.append("17-portrait-hires-sharp.png")
    # (back as it was: landscape, full screen)
    setting(page, up="smooth", text=False, chrome=False, theme="lcd")
    page.set_viewport_size({"width": 844, "height": 390})
    page.wait_for_timeout(300)
    page.click("#bFull")
    ps.fb[:] = saved
    ps.update()
    page.wait_for_timeout(600)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "screens", "cast")
    os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp()
    sockpath = os.path.join(tmp, "modem.sock")
    link, view, http = e2e.free_port(), e2e.free_port(), e2e.free_port()
    env = dict(os.environ, HOSTMODEM_CAST="1", HOSTMODEM_CASTPORT=str(link), HOSTMODEM_CASTVIEW=str(view),
               HOSTMODEM_HTTP=str(http))
    log = open(os.path.join(tmp, "hostmodem.log"), "w")
    proc = subprocess.Popen([os.path.join(HERE, "hostmodem"), sockpath], env=env, stderr=log, stdout=log)
    shots = []
    try:
        for _ in range(100):
            if os.path.exists(sockpath):
                break
            time.sleep(0.05)
        ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        ser.connect(sockpath)                      # (the serial line: idle here)
        url = "http://127.0.0.1:%d/screen" % http
        with sync_playwright() as p:
            b = p.chromium.launch()
            iphone = dict(viewport={"width": 390, "height": 844}, device_scale_factor=3, is_mobile=True, has_touch=True,
                          user_agent="Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1")
            ctx = b.new_context(**iphone)
            page = ctx.new_page()
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
            # no Psion yet: "Waiting for the Psion..."
            page.goto(url)
            page.wait_for_timeout(1200)
            page.screenshot(path=os.path.join(out, "1-waiting.png"))
            shots.append("1-waiting.png")
            e2e.check("Waiting for the Psion" in page.inner_text("#veil"), "the page says it is waiting for the Psion")
            # the Psion connects (the TCP route) and shares its screen
            ps = e2e.Psion(socket.create_connection(("127.0.0.1", link)), "My 5mx")
            ps.hello()
            ps.send(e2e.frame(0x05, (695).to_bytes(2, "little") + (280).to_bytes(2, "little")))   # DEVINFO
            time.sleep(0.2)
            draw_screen(ps)
            ps.update()
            ps.status()
            page.wait_for_timeout(1500)
            page.screenshot(path=os.path.join(out, "2-portrait.png"))
            shots.append("2-portrait.png")
            # the canvas, read back: the Psion's picture exactly (5mx LCD theme, the default)
            same = page.evaluate("""() => {
                const c = document.getElementById('model'), d = c.getContext('2d').getImageData(0, 0, 640, 240).data;
                const ink = [27, 36, 26], paper = [186, 196, 164], g = [];
                for (let i = 0; i < 640 * 240; i++) {
                    let best = 0, bd = 1e9;
                    for (let k = 0; k < 16; k++) {
                        let e = 0;
                        for (let j = 0; j < 3; j++) { const v = Math.round(ink[j] + (paper[j] - ink[j]) * k / 15); e += Math.abs(v - d[i * 4 + j]); }
                        if (e < bd) { bd = e; best = k; }
                    }
                    g.push(best);
                }
                return g;
            }""")
            want = []
            for byte in ps.fb:
                want += [byte & 15, byte >> 4]
            e2e.check(same == want, "the page's canvas is the Psion's picture, pixel for pixel")
            # scrolling text on the Psion: the page follows
            for i in range(3):
                ps.fb[24 * 320:200 * 320] = ps.fb[39 * 320:215 * 320]
                ps.update()
                time.sleep(0.15)
            page.wait_for_timeout(600)
            # control, with the key bar (taken by itself when the Psion allows it,
            # since 0.17: the button only if not)
            page.wait_for_timeout(1500)
            if not page.evaluate("() => document.body.classList.contains('control')"):
                page.click("#bControl")
                page.wait_for_timeout(1500)
            page.screenshot(path=os.path.join(out, "3-control.png"))
            shots.append("3-control.png")
            box = page.locator("#screen").bounding_box()
            page.mouse.click(box["x"] + box["width"] * 100 / 640, box["y"] + box["height"] * 50 / 240)
            page.click('.k[data-sc="148"]')      # Menu
            page.click('.k[data-app="0"]')       # System, in the digitiser's margins from DEVINFO
            page.wait_for_timeout(500)
            e2e.check((1, -23, 260) in ps.inputs, "the System icon is a tap at -23,260 (%r)" % ps.inputs[-4:])
            e2e.check((1, 100, 50) in ps.inputs and (4, 0x94, 0) in ps.inputs, "a tap and Menu from the page reach the Psion (%r)" % ps.inputs[:6])
            # the themes
            page.click("#bMore")
            page.wait_for_timeout(300)
            page.screenshot(path=os.path.join(out, "4-settings.png"))
            shots.append("4-settings.png")
            for t in ("amber", "green", "dark", "s7", "modern", "warm", "mint", "paper"):
                page.click('.sw[data-id="%s"]' % t)
                page.wait_for_timeout(150)
                page.locator("#frame").screenshot(path=os.path.join(out, "5-theme-%s.png" % t))
                shots.append("5-theme-%s.png" % t)
            # the Series 7 theme's role pass: a screen with no dialog or toolbar
            # looks exactly as its plain greys would (checkerboards flat)...
            rgb = """() => { const d = document.getElementById('model').getContext('2d').getImageData(0, 0, 640, 240).data, o = [];
                for (let i = 0; i < 640 * 240; i++) o.push([d[i * 4], d[i * 4 + 1], d[i * 4 + 2]]); return o; }"""
            page.click('.sw[data-id="s7"]')
            page.wait_for_timeout(400)
            got = page.evaluate(rgb)
            want = [v for byte in ps.fb for v in (byte & 15, byte >> 4)]
            rows = [want[y * 640:(y + 1) * 640] for y in range(240)]
            s7 = lambda v: [v * 17] * 3
            bad = [i for i in range(640 * 240) if got[i] != mflat(rows, i % 640, i // 640, s7, [])]
            e2e.check(not bad, "Series 7: a screen with no dialog or toolbar is its plain greys (%d pixels differ)" % len(bad))
            # ...and a real one's dialog title is navy, its button and the toolbar's are blue
            saved = bytes(ps.fb)
            pts = draw_eikon(ps)
            ps.update()
            page.wait_for_timeout(600)
            page.locator("#frame").screenshot(path=os.path.join(out, "5-theme-s7-dialog.png"))
            shots.append("5-theme-s7-dialog.png")
            got = page.evaluate(rgb)
            at = lambda k: got[pts[k][1] * 640 + pts[k][0]]
            for k, c, what in (("title", [0, 0, 128], "a dialog's title bar is navy"),
                               ("titletext", [255, 255, 255], "its title is white"),
                               ("body", [200, 200, 200], "the dialog is light grey"),
                               ("face", [150, 150, 180], "its button is blue-grey"),
                               ("tbar", [200, 200, 200], "the toolbar is light grey"),
                               ("tface", [150, 150, 180], "the toolbar's buttons are blue-grey"),
                               ("page", [255, 255, 255], "the page stays white"),
                               ("menu", [255, 255, 255], "a menu (no title bar) stays white"),
                               ("sel", [0, 0, 0], "a selection stays black"),
                               ("lsel", [0, 0, 0], "a list's selected row stays black"),
                               ("shaft", [255, 255, 255], "a scroll bar's shaft stays white"),
                               ("sface", [170, 170, 170], "a scroll bar's thumb stays grey"),
                               ("dsel", [85, 85, 85], "a selection without the focus stays dark grey"),
                               ("busy", [0, 0, 0], "a busy message stays black"),
                               ("dither", [212, 212, 212], "a dithered light grey is one flat grey")):
                e2e.check(at(k) == c, "Series 7: %s (%r at %r)" % (what, at(k), pts[k]))
            # the Modern theme: its own colours for the same parts, and for the
            # selection, the scroll bar and the menu; the page is its greys
            page.click('.sw[data-id="modern"]')
            page.wait_for_timeout(600)
            page.locator("#frame").screenshot(path=os.path.join(out, "5-theme-modern-dialog.png"))
            shots.append("5-theme-modern-dialog.png")
            got = page.evaluate(rgb)
            for k, c, what in (("title", [31, 58, 95], "a dialog's title bar is deep navy"),
                               ("titletext", [255, 255, 255], "its title is white"),
                               ("body", [238, 241, 245], "the dialog is a cool light grey"),
                               ("face", [221, 228, 238], "its button is blue-grey"),
                               ("tbar", [227, 232, 239], "the toolbar is light blue-grey"),
                               ("tface", [221, 228, 238], "the toolbar's buttons are blue-grey"),
                               ("sel", [15, 118, 110], "a selection is teal"),
                               ("seltext", [251, 250, 247], "its writing stays white"),
                               ("lsel", [15, 118, 110], "a list's selected row, up to its frame and scroll bar, is teal"),
                               ("lframe", [29, 36, 51], "the list's frame away from it is the theme's ink"),
                               ("shaft", [233, 237, 242], "a scroll bar's shaft is pale"),
                               ("sface", [197, 207, 220], "its thumb and arrows are slate"),
                               ("menu", [244, 246, 249], "a menu is near-white"),
                               ("page", [251, 250, 247], "the page is the theme's paper"),
                               ("dsel", [64, 140, 132], "a selection without the focus is a muted teal"),
                               ("busy", [29, 36, 51], "a busy message is ink"),
                               ("dither", MODERN_PALE, "a dithered light grey is one pale tint")):
                e2e.check(at(k) == c, "Modern: %s (%r at %r)" % (what, at(k), pts[k]))
            g = pts["grey"]
            bad = [(x, y) for y in range(0, 48) for x in range(0, 326) if got[y * 640 + x] != mflat(g, x, y)]
            e2e.check(not bad, "Modern: the page's writing is the theme's greys, nothing else (%r)" % bad[:3])
            # a big change comes a strip at a time, top first: a dialog whose
            # lower rows have not arrived has its title bar and body already
            ps.fb[:] = saved
            ps.update()
            page.wait_for_timeout(400)
            put_rows(ps, g, 0, 100)              # (the dialog's border starts at row 50; its body is white from row 76)
            ps.update()
            page.wait_for_timeout(600)
            got = page.evaluate(rgb)
            e2e.check(at("title") == [31, 58, 95] and at("titletext") == [255, 255, 255], "Modern: a dialog half arrived has its title bar (%r)" % at("title"))
            e2e.check(got[90 * 640 + 68] == [238, 241, 245] and got[140 * 640 + 68] == [251, 250, 247],
                      "Modern: ...and its body as far as it goes (%r, %r)" % (got[90 * 640 + 68], got[140 * 640 + 68]))
            put_rows(ps, g, 100, 240)
            ps.update()
            page.wait_for_timeout(600)
            got = page.evaluate(rgb)
            e2e.check(at("body") == [238, 241, 245] and at("face") == [221, 228, 238], "Modern: ...and the rest when it comes (%r)" % at("body"))
            # the pass itself, in this Chromium, on that screen: well under the 150 ms between passes
            src = open(os.path.join(HERE, "..", "src", "viewer.html")).read()
            code = src[src.index("const R_TITLE"):src.index("// ----- the frame's size")]
            ms = page.evaluate("""([code, grey]) => {
                const f = new Function(code + '; return findRoles;')(), g = new Uint8Array(grey.flat());
                const role = new Uint8Array(640 * 240), stack = new Int32Array(640 * 240);
                for (let i = 0; i < 5; i++) f(g, 640, 240, role, stack);
                const t = performance.now();
                for (let i = 0; i < 20; i++) f(g, 640, 240, role, stack);
                return (performance.now() - t) / 20; }""", [code, pts["grey"]])
            e2e.check(ms < 10, "Series 7: the role pass takes %.1f ms" % ms)
            print("cast_page_shot: the role pass takes %.2f ms" % ms)
            ps.fb[:] = saved
            ps.update()
            # ...and on a screen with no EIKON parts, the Modern theme is its plain greys too
            page.wait_for_timeout(600)
            got = page.evaluate(rgb)
            want = [v for byte in ps.fb for v in (byte & 15, byte >> 4)]
            rows = [want[y * 640:(y + 1) * 640] for y in range(240)]
            bad = [i for i in range(640 * 240) if got[i] != mflat(rows, i % 640, i // 640)]
            e2e.check(not bad, "Modern: a screen with no dialog or toolbar is its plain greys (%d pixels differ)" % len(bad))
            page.click('.sw[data-id="lcd"]')
            page.click("#bClose")
            # landscape
            page.set_viewport_size({"width": 844, "height": 390})
            page.wait_for_timeout(500)
            page.screenshot(path=os.path.join(out, "6-landscape.png"))
            shots.append("6-landscape.png")
            page.click("#bFull")
            page.wait_for_timeout(500)
            page.screenshot(path=os.path.join(out, "7-landscape-full.png"))
            shots.append("7-landscape-full.png")
            # full screen is the 5mx's face: the silkscreen icons round the screen, each a tap where the digitiser has it
            e2e.check(page.locator('.sk').count() == 14, "full screen shows the 14 silkscreen icons")
            page.click('.sk[data-silk="11"]')     # Zoom in, on the left strip
            page.wait_for_timeout(400)
            e2e.check((1, -23, 172) in ps.inputs, "the Zoom in icon in full screen is a tap at -23,172 (%r)" % ps.inputs[-4:])
            # drawn at the phone's resolution (3x here), not the Psion's
            dims = page.evaluate("() => { const c = document.getElementById('screen'), r = c.getBoundingClientRect(); return [c.width, Math.round(r.width * devicePixelRatio)]; }")
            e2e.check(dims[0] == dims[1] and dims[0] > 640, "the screen is drawn at the phone's own resolution (%r)" % dims)

            # no room beside the face on a phone on its side: the face fits the
            # stage, and the round buttons float over its corner, fading after 3 s
            fit = page.evaluate("() => { const d = document.getElementById('device').getBoundingClientRect(); return [d.width, d.height, innerWidth, innerHeight]; }")
            e2e.check(fit[0] >= fit[2] - 2 or fit[1] >= fit[3] - 2, "landscape full screen: the face as big as the stage (%r)" % fit)
            e2e.check(page.evaluate("() => document.body.classList.contains('btnfloat')"), "landscape full screen: the buttons float over the face")
            page.wait_for_timeout(3400)
            e2e.check(page.evaluate("() => document.body.classList.contains('btnhide')"), "the floating buttons fade after 3 s")
            # a tap where they were brings them back: not to the Psion, and not a press
            n0 = len(ps.inputs)
            eb = page.evaluate("() => { const r = document.getElementById('exit').getBoundingClientRect(); return [r.x + r.width / 2, r.y + r.height / 2]; }")
            page.mouse.click(eb[0], eb[1])
            page.wait_for_timeout(400)
            e2e.check(not page.evaluate("() => document.body.classList.contains('btnhide')") and page.evaluate("() => document.body.classList.contains('immersive')")
                      and len(ps.inputs) == n0, "a tap brings the buttons back, and goes no further (%r)" % ps.inputs[n0:])
            page.wait_for_timeout(500)
            layer_checks(b, iphone, url, page, ps, out, shots)
            # paused on the Psion, then gone
            page.click("#exit")
            page.wait_for_timeout(400)
            ps.send(e2e.frame(0x03, bytes([77, 0]) + (3 << 20).to_bytes(4, "little") + bytes([50, 0, 120, 0, 1, 1])))
            page.wait_for_timeout(800)
            page.set_viewport_size({"width": 390, "height": 844})
            page.wait_for_timeout(400)
            page.screenshot(path=os.path.join(out, "8-paused.png"))
            shots.append("8-paused.png")
            e2e.check("Psion paused" in page.inner_text("#banner"), "the page says the Psion is paused")
            ps.send(e2e.frame(0x04))
            page.wait_for_timeout(1200)
            page.screenshot(path=os.path.join(out, "9-psion-gone.png"))
            shots.append("9-psion-gone.png")
            # (no Modern pack in this build: its 404 is expected, and harmless)
            if "kCastModIconsGzLen = 0;" in open(os.path.join(HERE, "..", "src", "casticons.h")).read():
                errors = [e for e in errors if not e.startswith("Failed to load resource")]
            e2e.check(not errors, "no script errors (%r)" % errors[:3])
            b.close()
    finally:
        proc.terminate()
        proc.wait(3)
        log.close()
    print("cast_page_shot: %s: %s" % (out, ", ".join(shots)))
    print("cast_page_shot: %d checks, %d failed" % (e2e.checks, e2e.fails))
    return 1 if e2e.fails else 0


if __name__ == "__main__":
    sys.exit(main())
