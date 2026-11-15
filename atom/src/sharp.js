/* sharp.js: the PsiCast viewer's sharp redraws, drawn over the upscaled
 * picture: "Redraw boxes and lines sharply" and "Redraw text sharply"
 * (viewer.html finds what to draw: the role pass's parts, textmatch.js's
 * runs; this draws it). Loaded the first time either is on. Plain ES2017,
 * no dependencies. MIT licence.
 *
 *   PsiSharp.draw(ctx, sx, sy, S)
 *
 * ctx: #screen's 2D context; sx, sy: its device pixels a Psion pixel; S:
 * {W, H, px (the model's colours), dpx (the display's: faces flat, the
 * redrawn ink taken out), little (endianness), parts (the role pass's
 * shapes: a box with a border k wide, or a triangle), runs (textmatch.js's),
 * text, chrome (the two settings), symbols (TextMatch.SYMBOLS)}.
 * - A border: its pixels as they are, as rectangles on whole device pixels
 *   (runs along rows across the top and bottom, down columns at the sides),
 *   so a line keeps the Psion's width with hard edges. A triangle, or an
 *   Eikon symbol: a clean shape in its ink's colour.
 * - Text: each glyph in the phone's font, centred on the Psion glyph's ink,
 *   on its baseline, cut to the run's box, in the colour the model has for
 *   its ink (the theme's, for its part).
 */
(function (root) {
  "use strict";
  let S = null;
  const cssCache = new Map();
  function cssOf(v) {
    const little = S.little;
    let s = cssCache.get(v);
    if (!s) {
      const c = little ? [v & 255, v >>> 8 & 255, v >>> 16 & 255] : [v >>> 24, v >>> 16 & 255, v >>> 8 & 255];
      s = "rgb(" + c.join(",") + ")";
      cssCache.set(v, s);
    }
    return s;
  }
  function drawChrome(c, sx, sy) {
    const W = S.W, H = S.H, dpx = S.dpx, px = S.px;
    const X = x => Math.round(x * sx), Y = y => Math.round(y * sy);
    let last = -1;
    const fill = (v, x0, y0, x1, y1) => {             // Psion pixels x0..x1-1, y0..y1-1, on whole device pixels
      if (v !== last) { c.fillStyle = cssOf(v); last = v; }
      c.fillRect(X(x0), Y(y0), X(x1) - X(x0), Y(y1) - Y(y0));
    };
    const rows = (x0, y0, x1, y1) => {                // a band across: runs along each row
      x0 = Math.max(0, x0); y0 = Math.max(0, y0); x1 = Math.min(W - 1, x1); y1 = Math.min(H - 1, y1);
      for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1;) {
        const v = dpx[y * W + x];
        let e = x;
        while (e < x1 && dpx[y * W + e + 1] === v) e++;
        fill(v, x, y, e + 1, y + 1);
        x = e + 1;
      }
    };
    const cols = (x0, y0, x1, y1) => {                // a band down: runs down each column
      x0 = Math.max(0, x0); y0 = Math.max(0, y0); x1 = Math.min(W - 1, x1); y1 = Math.min(H - 1, y1);
      for (let x = x0; x <= x1; x++) for (let y = y0; y <= y1;) {
        const v = dpx[y * W + x];
        let e = y;
        while (e < y1 && dpx[(e + 1) * W + x] === v) e++;
        fill(v, x, y, x + 1, e + 1);
        y = e + 1;
      }
    };
    for (const p of S.parts) {
      if (p.tri) { shapeOf(c, p.tri, X(p.x0), Y(p.y0), X(p.x1 + 1), Y(p.y1 + 1), cssOf(px[p.px[0]]), sx); last = -1; continue; }
      const k = p.k, t = p.noTop ? p.y0 : p.y0 + k, b = p.noBottom ? p.y1 : p.y1 - k;
      if (!p.noTop) rows(p.x0, p.y0, p.tx1 || p.x1, p.y0 + k - 1);
      if (!p.noBottom) rows(p.x0, p.y1 - k + 1, p.x1, p.y1);
      if (b >= t) { cols(p.x0, t, p.x0 + k - 1, b); cols(p.x1 - k + 1, t, p.x1, b); }
      if (p.rows) for (const y of p.rows) rows(p.rx0, y, p.rx1, y);
    }
    for (const r of S.runs) {
      if (!r.font.sym) continue;
      const [ch, pen, g] = r.glyphs[0], x0 = pen + g.l;
      shapeOf(c, S.symbols[ch], X(x0), Y(r.top + g.y0), X(x0 + g.w), Y(r.top + g.y1 + 1), cssOf(px[r.px[0]]), sx);
    }
  }
  // an arrow, the tick or a dot in the device box L,T..R,B
  function shapeOf(c, kind, L, T, R, B, col, sx) {
    const mx = (L + R) / 2, my = (T + B) / 2;
    c.fillStyle = col; c.strokeStyle = col;
    c.beginPath();
    if (kind === "up") { c.moveTo(mx, T); c.lineTo(R, B); c.lineTo(L, B); }
    else if (kind === "down") { c.moveTo(L, T); c.lineTo(R, T); c.lineTo(mx, B); }
    else if (kind === "left") { c.moveTo(L, my); c.lineTo(R, T); c.lineTo(R, B); }
    else if (kind === "right") { c.moveTo(L, T); c.lineTo(R, my); c.lineTo(L, B); }
    else if (kind === "right-open" || kind === "left-open") {
      const i = sx / 2;
      if (kind === "right-open") { c.moveTo(L + i, T + i); c.lineTo(R - i, my); c.lineTo(L + i, B - i); }
      else { c.moveTo(R - i, T + i); c.lineTo(L + i, my); c.lineTo(R - i, B - i); }
      c.closePath(); c.lineWidth = sx; c.lineJoin = "miter"; c.stroke();
      return;
    } else if (kind === "tick") {
      const w = R - L, h = B - T;
      c.moveTo(L + w * .06, T + h * .62); c.lineTo(L + w * .3, T + h * .94); c.lineTo(L + w * .95, T + h * .05);
      c.lineWidth = sx * 1.8; c.lineCap = "round"; c.lineJoin = "round"; c.stroke();
      return;
    } else if (kind === "dot") { c.arc(mx, my, Math.min(R - L, B - T) / 2, 0, Math.PI * 2); }
    else return;
    c.closePath(); c.fill();
  }
  // the phone's fonts for the Psion's: Helvetica Neue (Medium for the regular
  // faces, which are 2 pixels thick on the Psion) where there is one (iOS),
  // else Arial with a thin outline; bold for bold; condensed bold for the toolbar
  const SANS = '"Helvetica Neue", Helvetica, Arial, "Liberation Sans", sans-serif';
  const NARROW = '"HelveticaNeue-CondensedBold", "Helvetica Neue Condensed Bold", "Arial Narrow", "Liberation Sans Narrow", "Nimbus Sans Narrow", ' + SANS;
  const WIN1252 = { 0x80: "\u20ac", 0x85: "\u2026", 0x91: "\u2018", 0x92: "\u2019", 0x93: "\u201c", 0x94: "\u201d", 0x95: "\u2022", 0x96: "\u2013", 0x97: "\u2014", 0x99: "\u2122" };
  const chrOf = ch => ch >= 0x80 && ch < 0xa0 ? WIN1252[ch] || "?" : String.fromCharCode(ch);
  let neue = null;
  const fontCache = new Map(), measureCache = new Map();
  function fontFor(c, f, sy) {
    const key = f.name + f.cell + "." + f.bold + "|" + sy.toFixed(3);
    let o = fontCache.get(key);
    if (o) return o;
    if (neue === null) {
      c.font = "40px monospace"; const a = c.measureText("mmmmlllliiii").width;
      c.font = '40px "Helvetica Neue", monospace'; neue = c.measureText("mmmmlllliiii").width !== a;
    }
    const toolbar = f.name === "Toolbar", fam = toolbar ? NARROW : SANS;
    const weight = toolbar || f.bold ? "bold" : neue ? "500" : "normal";
    c.font = weight + " 100px " + fam;
    const capR = c.measureText("H").actualBoundingBoxAscent / 100 || .72, xR = c.measureText("x").actualBoundingBoxAscent / 100 || .52;
    const em = f.cap && f.xh ? (f.cap * sy / capR + f.xh * sy / xR) / 2 : f.cell * sy * .8;
    o = { css: weight + " " + em.toFixed(2) + "px " + fam, stroke: !toolbar && !f.bold && !neue && f.cell >= 15 ? em * .045 : 0 };
    fontCache.set(key, o);
    return o;
  }
  function drawText(c, sx, sy) {
    const px = S.px;
    const X = x => Math.round(x * sx), Y = y => Math.round(y * sy);
    c.textBaseline = "alphabetic"; c.textAlign = "left";
    for (const r of S.runs) {
      if (r.font.sym) continue;
      const f = fontFor(c, r.font, sy), col = cssOf(px[r.px[0]]);
      c.save();
      c.beginPath();
      c.rect(X(r.start - 1), Y(r.top), X(r.end + 1) - X(r.start - 1), Y(r.top + r.font.cell) - Y(r.top));
      c.clip();
      c.font = f.css; c.fillStyle = col;
      if (f.stroke) { c.strokeStyle = col; c.lineWidth = f.stroke; c.lineJoin = "round"; }
      const base = (r.top + r.font.ascent) * sy;
      for (const q of r.glyphs) {
        const g = q[2];
        if (!g) continue;
        const t = chrOf(q[0]), mk = f.css + t;
        let m = measureCache.get(mk);
        if (!m) { const mm = c.measureText(t); m = [mm.actualBoundingBoxLeft, mm.actualBoundingBoxRight]; measureCache.set(mk, m); }
        const x = (q[1] + g.l + g.w / 2) * sx - (m[1] - m[0]) / 2;
        c.fillText(t, x, base);
        if (f.stroke) c.strokeText(t, x, base);
      }
      c.restore();
    }
  }

  function draw(c, sx, sy, state) {
    S = state;
    if (S.chrome) drawChrome(c, sx, sy);
    if (S.text) drawText(c, sx, sy);
  }
  const api = { draw };
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.PsiSharp = api;
})(typeof self !== "undefined" ? self : this);
