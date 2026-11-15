#!/usr/bin/env python3
# test_partial.py - the viewer's partial redraws: the Psion's screen changes
# a piece at a time (as a game or a new program does), and what #screen
# shows after each change, with no whole redraw asked for, must be what a
# whole redraw then shows - in each Upscaling, with and without the sharp
# redraws, in landscape (where Smooth and High-res are used). Dan saw
# pieces of earlier screens stay on the phone (PsiTris over the World map,
# 24 Oct 2026). Needs Python's playwright and its Chromium.
#   make -C hosttest hostmodem libpccodec.so && python3 hosttest/test_partial.py
# MIT licence.
import os, random, socket, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_cast_e2e as e2e
import cast_page_shot as shot
from playwright.sync_api import sync_playwright

HERE = os.path.dirname(os.path.abspath(__file__))


def grab(page):
    return page.evaluate("""() => {
        const c = document.getElementById('screen'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
        return [c.width, c.height, Array.from(d.filter((_, i) => i % 4 !== 3 && (i >> 2) % 3 === 0))]; }""")


def differ(a, b):
    """the share of sampled device pixels whose colour differs by more than 24"""
    if a[0] != b[0] or a[1] != b[1]:
        return 1.0
    x, y, n = a[2], b[2], 0
    for i in range(0, len(x), 3):
        if abs(x[i] - y[i]) + abs(x[i + 1] - y[i + 1]) + abs(x[i + 2] - y[i + 2]) > 24:
            n += 1
    return n / max(1, len(x) // 3)


def band(ps, rnd, y0, y1, x0=0, x1=640, kind="photo"):
    """rows y0..y1-1, columns x0..x1-1 of the Psion's screen: a dithered
    picture, or text-like dark and white"""
    for y in range(y0, y1):
        for x in range(x0 & ~1, x1, 2):
            if kind == "photo":
                a, b = rnd.randrange(16), rnd.randrange(16)
            else:
                a = b = 0 if (x // 6 + y // 9) % 3 == 0 else 15
            ps.fb[y * 320 + x // 2] = a | (b << 4)


def main():
    tmp = tempfile.mkdtemp()
    sockpath = os.path.join(tmp, "modem.sock")
    link, view, http = e2e.free_port(), e2e.free_port(), e2e.free_port()
    env = dict(os.environ, HOSTMODEM_CAST="1", HOSTMODEM_CASTPORT=str(link), HOSTMODEM_CASTVIEW=str(view),
               HOSTMODEM_HTTP=str(http))
    log = open(os.path.join(tmp, "hostmodem.log"), "w")
    proc = subprocess.Popen([os.path.join(HERE, "hostmodem"), sockpath], env=env, stderr=log, stdout=log)
    fails = []
    try:
        for _ in range(100):
            if os.path.exists(sockpath):
                break
            time.sleep(0.05)
        ser = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        ser.connect(sockpath)
        with sync_playwright() as p:
            b = p.chromium.launch()
            ctx = b.new_context(viewport={"width": 844, "height": 390}, device_scale_factor=3, is_mobile=True, has_touch=True)
            page = ctx.new_page()
            page.goto("http://127.0.0.1:%d/screen" % http)
            page.wait_for_timeout(800)
            ps = e2e.Psion(socket.create_connection(("127.0.0.1", link)), "My 5mx")
            ps.hello()
            ps.send(e2e.frame(0x05, (695).to_bytes(2, "little") + (280).to_bytes(2, "little")))
            time.sleep(0.2)
            eon = shot.eon_fonts()
            rnd = random.Random(5)
            for up in ("pixels", "smooth", "hires"):
                for lay in (False, True):
                    shot.draw_eikon(ps, eon)
                    band(ps, rnd, 120, 200, 300, 560)
                    ps.update()
                    shot.setting(page, up=up, text=lay, chrome=lay)
                    shot.settle(page, 1200)
                    worst = 0.0
                    # a piece at a time: a picture band, a game's well, a new program's screen in strips
                    steps = [(30, 90, 0, 640, "photo"), (100, 140, 200, 420, "text"), (0, 240, 260, 380, "photo")]
                    steps += [(y, y + 24, 0, 640, "text" if y % 48 else "photo") for y in range(0, 240, 24)]
                    for (y0, y1, x0, x1, kind) in steps:
                        band(ps, rnd, y0, y1, x0, x1, kind)
                        ps.update()
                        page.wait_for_timeout(350)
                        got = grab(page)
                        page.evaluate("() => window.psicastDebug.redraw()")
                        page.wait_for_timeout(300)
                        whole = grab(page)
                        worst = max(worst, differ(got, whole))
                    path = page.evaluate("() => window.psicastPerf.path")
                    name = "%s%s" % (up, " + sharp redraws" if lay else "")
                    ok = worst < 0.002
                    print("%s %s: path %s, worst %.2f%% of the screen stale" % ("PASS" if ok else "FAIL", name, path, worst * 100))
                    if not ok:
                        fails.append(name)
            b.close()
    finally:
        proc.terminate()
    print("test_partial: %s" % ("FAIL: " + ", ".join(fails) if fails else "all pass"))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
