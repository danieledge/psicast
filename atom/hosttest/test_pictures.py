#!/usr/bin/env python3
# test_pictures.py - Replace known pictures with sprites (0.22): a pack made
# by tools/mkpicpack.py of made-up pictures (a board, two pieces that differ
# in a few pixels, each with its mask, a picture), the Psion drawing the
# board and the pieces on it as a game does (only where a piece's mask is),
# and what the viewer finds and shows: each piece, the board under them, the
# board's own colour where they cover it, a piece moved, none of it on a
# screen with nothing of theirs. Needs Python's playwright and its Chromium.
#   make -C hosttest hostmodem libpccodec.so && python3 hosttest/test_pictures.py
# MIT licence.
import json, os, random, socket, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_cast_e2e as e2e
from PIL import Image
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "tools")
fails = []


def check(ok, what):
    print("%s %s" % ("PASS" if ok else "FAIL", what))
    if not ok:
        fails.append(what)


def make_pack(tmp):
    rnd = random.Random(7)
    q = lambda: rnd.choice((0, 5, 10, 15))
    # a board: 8 by 8 squares of 25, the dark ones dithered, a frame
    bw = 200
    board = [15] * (bw * bw)
    for y in range(bw):
        for x in range(bw):
            if x < 2 or y < 2 or x >= bw - 2 or y >= bw - 2:
                board[y * bw + x] = 0
            elif ((x // 25) + (y // 25)) % 2:
                board[y * bw + x] = 10 if (x + y) % 2 else 5
    # two pieces: a round one, and the same with a few pixels changed
    pw = 25
    mask = [1 if (x - 12) ** 2 + (y - 12) ** 2 <= 100 else 0 for y in range(pw) for x in range(pw)]
    piece = [q() if mask[i] else 15 for i in range(pw * pw)]
    piece2 = list(piece)
    for i in rnd.sample([i for i in range(pw * pw) if mask[i]], 25):
        piece2[i] = (piece2[i] + 5) % 20 if piece2[i] != 15 else 0
    photo = [q() for _ in range(180 * 120)]
    pics = {"board": (bw, bw, board, None, (200, 30, 30)), "piece": (pw, pw, piece, mask, (30, 30, 220)),
            "piece2": (pw, pw, piece2, mask, (30, 200, 30)), "photo": (180, 120, photo, None, (220, 200, 0))}
    man = []
    for name, (w, h, g, m, col) in pics.items():
        im = Image.new("L", (w, h)); im.putdata([v * 17 for v in g]); im.save(os.path.join(tmp, name + ".png"))
        red = Image.new("RGBA" if m else "RGB", (w * 4, h * 4), col + ((255,) if m else ()))
        e = {"app": "test", "id": name, "src": name + ".png", "img": name + "-c.png"}
        if m:
            mk = Image.new("L", (w, h)); mk.putdata([255 if v else 0 for v in m]); mk.save(os.path.join(tmp, name + "-m.png"))
            red.putalpha(mk.resize(red.size, Image.NEAREST))
            e["mask"] = name + "-m.png"
        red.save(os.path.join(tmp, e["img"]))
        man.append(e)
    json.dump(man, open(os.path.join(tmp, "manifest.json"), "w"))
    out = os.path.join(tmp, "pack.json.gz")
    subprocess.run([sys.executable, os.path.join(TOOLS, "mkpicpack.py"), os.path.join(tmp, "manifest.json"), out],
                   check=True, stdout=subprocess.DEVNULL)
    return out, pics


def put(ps, x0, y0, w, h, g, m=None):
    for y in range(h):
        for x in range(w):
            if m and not m[y * w + x]:
                continue
            X, Y = x0 + x, y0 + y
            b = ps.fb[Y * 320 + X // 2]
            ps.fb[Y * 320 + X // 2] = (b & 0xf0) | g[y * w + x] if X % 2 == 0 else (b & 0x0f) | (g[y * w + x] << 4)


def colour_at(page, x, y):
    """the #screen colour at the middle of the Psion's pixel x, y"""
    return page.evaluate("""([x, y]) => { const c = document.getElementById('screen');
        const d = c.getContext('2d').getImageData(Math.floor((x + 0.5) * c.width / 640), Math.floor((y + 0.5) * c.height / 240), 1, 1).data;
        return [d[0], d[1], d[2]]; }""", [x, y])


def near(c, want, tol=60):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def main():
    tmp = tempfile.mkdtemp()
    pack, pics = make_pack(tmp)
    sockpath = os.path.join(tmp, "modem.sock")
    link, view, http = e2e.free_port(), e2e.free_port(), e2e.free_port()
    env = dict(os.environ, HOSTMODEM_CAST="1", HOSTMODEM_CASTPORT=str(link), HOSTMODEM_CASTVIEW=str(view),
               HOSTMODEM_HTTP=str(http), HOSTMODEM_PICTURES=pack)
    log = open(os.path.join(tmp, "hostmodem.log"), "w")
    proc = subprocess.Popen([os.path.join(HERE, "hostmodem"), sockpath], env=env, stderr=log, stdout=log)
    try:
        for _ in range(100):
            if os.path.exists(sockpath):
                break
            time.sleep(0.05)
        ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        ser.connect(sockpath)
        with sync_playwright() as p:
            b = p.chromium.launch()
            ctx = b.new_context(viewport={"width": 1280, "height": 600})
            ctx.add_init_script("localStorage.setItem('psicast.pictures', 'true'); localStorage.setItem('psicast.upscale', '\"pixels\"')")
            page = ctx.new_page()
            page.goto("http://127.0.0.1:%d/screen" % http)
            page.wait_for_timeout(800)
            ps = e2e.Psion(socket.create_connection(("127.0.0.1", link)), "My 5mx")
            ps.hello()
            ps.send(e2e.frame(0x05, (695).to_bytes(2, "little") + (280).to_bytes(2, "little")))
            time.sleep(0.3)
            found = lambda: sorted((m["id"], m["x"], m["y"]) for m in page.evaluate("() => window.psicastPictures()"))

            bw, _, board, _, _ = pics["board"]
            pw, _, piece, mask, _ = pics["piece"]
            piece2 = pics["piece2"][2]
            BX, BY = 220, 20
            put(ps, BX, BY, bw, bw, board)
            at = [(0, 0), (1, 0), (3, 2), (7, 7), (4, 6)]
            for c, r in at:
                put(ps, BX + c * 25, BY + r * 25, pw, pw, piece, mask)
            put(ps, BX + 5 * 25, BY + 1 * 25, pw, pw, piece2, mask)
            ps.update()
            page.wait_for_timeout(900)
            got = found()
            want = sorted([("board", BX, BY)] + [("piece", BX + c * 25, BY + r * 25) for c, r in at] + [("piece2", BX + 125, BY + 25)])
            check(got == want, "the board, each piece where it is, the one with a few pixels changed told apart (%s)" % got)
            check(near(colour_at(page, BX + 12, BY + 12), (30, 30, 220)), "a piece is shown redrawn")
            check(near(colour_at(page, BX + 125 + 12, BY + 25 + 12), (30, 200, 30)), "the other piece, its own redraw")
            check(near(colour_at(page, BX + 1, BY + 1 + 25 * 2), (200, 30, 30)), "the board is shown redrawn")
            # (a corner of a piece's square, off its mask: the board's, not the Psion's)
            check(near(colour_at(page, BX + 25 * 3 + 1, BY + 25 * 2 + 1), (200, 30, 30)), "beside a piece, off its mask: the board's redraw")

            # a piece moves: off (3, 2), the board's square back; on to (2, 5)
            put(ps, BX + 75, BY + 50, pw, pw, [board[(50 + y) * bw + 75 + x] for y in range(pw) for x in range(pw)])
            put(ps, BX + 50, BY + 125, pw, pw, piece, mask)
            ps.update()
            page.wait_for_timeout(900)
            got = found()
            want = sorted([("board", BX, BY)] + [("piece", BX + c * 25, BY + r * 25) for c, r in at if (c, r) != (3, 2)] +
                          [("piece", BX + 50, BY + 125), ("piece2", BX + 125, BY + 25)])
            check(got == want, "a piece moved: gone from where it was, found where it is (%s)" % got)
            check(near(colour_at(page, BX + 75 + 12, BY + 50 + 12), (200, 30, 30)), "where it was: the board's redraw")

            # a picture tiled as Chess tiles its background, behind a board and a
            # toolbar: whole, two-thirds of one beside the board, a strip off the edge
            for i in range(len(ps.fb)):
                ps.fb[i] = 0xff
            ph = pics["photo"]
            for tx in (0, 180, 360, 540):
                for y in range(120):
                    for x in range(180):
                        if 0 <= tx + x < 640:
                            X = tx + x; v = ph[2][y * 180 + x]; old = ps.fb[(60 + y) * 320 + X // 2]
                            ps.fb[(60 + y) * 320 + X // 2] = (old & 0xf0) | v if X % 2 == 0 else (old & 0x0f) | (v << 4)
            put(ps, 180, 20, bw, bw, board)
            put(ps, 590, 0, 50, 240, [15] * (50 * 240))
            ps.update()
            page.wait_for_timeout(900)
            got = found()
            if os.environ.get("PICDEBUG"):
                print(page.evaluate("() => window.psicastPictures()"))
            check(got == [("board", 180, 20), ("photo", 0, 60), ("photo", 360, 60), ("photo", 540, 60)],
                  "a tiled picture: whole, partly behind the board, partly off the screen (%s)" % got)
            check(near(colour_at(page, 450, 120), (220, 200, 0)), "the part beside the board, redrawn")
            check(near(colour_at(page, 560, 120), (220, 200, 0)), "the strip at the edge, redrawn")
            c, white = colour_at(page, 610, 120), colour_at(page, 630, 5)
            check(near(c, white, 12), "the toolbar over it stays the Psion's (%s)" % c)

            # the switch off: nothing replaced, the Psion's own picture back at once; on again
            page.evaluate("() => document.getElementById('tPics').click()")
            page.wait_for_timeout(300)
            check(not page.evaluate("() => localStorage.getItem('psicast.pictures') === 'true'"), "switched off, and remembered")
            check(found() == [], "switched off: nothing replaced (%s)" % found())
            c = colour_at(page, 450, 120)
            check(not near(c, (220, 200, 0)), "switched off: the Psion's own picture shown (%s)" % c)
            page.evaluate("() => document.getElementById('tPics').click()")
            ps.update()
            page.wait_for_timeout(900)
            check(len(found()) == 4, "switched on again: replaced again (%s)" % found())

            # a picture partly covered by a dialog, and then a screen of none of theirs
            ph = pics["photo"]
            for i in range(len(ps.fb)):
                ps.fb[i] = 0xff
            put(ps, 10, 100, ph[0], ph[1], ph[2])
            put(ps, 120, 180, 60, 30, [15] * 1800)
            ps.update()
            page.wait_for_timeout(900)
            got = found()
            check(got == [("photo", 10, 100)], "a picture with a corner covered (%s)" % got)
            c = colour_at(page, 150, 195)
            white = colour_at(page, 600, 20)
            check(near(c, white, 12), "what covers it stays the Psion's (%s, its white %s)" % (c, white))
            rnd = random.Random(3)
            for i in range(len(ps.fb)):
                ps.fb[i] = rnd.choice((0x00, 0x55, 0xaa, 0xff, 0x5a, 0xa5, 0x0f, 0xf0))
            ps.update()
            page.wait_for_timeout(900)
            got = found()
            check(got == [], "nothing found on a screen of none of theirs (%s)" % got)
            b.close()
    finally:
        proc.terminate()
    print("test_pictures: %s" % ("FAIL: " + "; ".join(fails) if fails else "all pass"))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
