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


def draw_eikon(ps):
    """A 5mx screen drawn the way EIKON draws it, in 4 greys (the geometry of a
    real one, from the emulator): a page of text, a dialog with its title bar,
    a selected prompt and a button, a menu (a dialog's border, no title bar),
    a scroll bar (arrows, thumb, shaft) and the toolbar with its buttons.
    #cccccc is a checkerboard of greys 10 and 15. Returns the points the
    Series 7 and Modern themes' checks look at."""
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

    def text(x, y, t):
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
            text(x + 8, y + 5, label)

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
            # control, with the key bar
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
