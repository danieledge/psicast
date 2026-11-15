/* textmatch.js: find the 5mx's own text (EIKON's ROM fonts) on a PsiCast
 * screen, exactly, for the viewer's "Redraw text sharply"; and the Eikon
 * symbols (the arrows of choice lists and cascades, the tick) for its
 * "Redraw boxes and lines sharply". Plain ES2017, no dependencies; also
 * loads in node (module.exports) for the host test. MIT licence.
 *
 * The fonts are eonfonts.json, made by tools/mkfonts.py from the 5mx ROM's
 * Eon.gdr (format "psicast-fonts", version 1): per font its cell height,
 * ascent, whether it is bold, the variants wanted ("plain"; "bold": EPOC's
 * algorithmic bold, the bitmap drawn twice a pixel apart, a pixel wider)
 * and its glyphs as [char, left adjust, advance, column codes...] (a
 * column code: bit r set where the glyph has ink in row r of the cell).
 *
 *   const tm = TextMatch.create(pack, opts);   // pack: the parsed JSON; opts.only:
 *                                              // "text" or "sym" (those fonts only)
 *   const runs = tm.find(grey, W, H, rect);    // rect {x, y, w, h}, optional
 *
 * find() returns the runs of text wholly inside rect (default: the whole
 * screen): [{font, top, c, bg, start, end, text, glyphs: [[char, pen,
 * glyph]]}] - the font, the cell's top row, the ink's grey and the
 * background's, the columns start..end-1 it takes, and each glyph at its
 * pen (a space has no glyph). glyph: {ch, l, w, mv, cols, y0, y1} (ink in
 * columns pen+l .. pen+l+w-1, rows top+y0 .. top+y1). A symbol is a run of
 * one glyph of a font with sym set.
 *
 * How: the screen becomes column masks per grey (bit y: row y). For a
 * font, a cell top t and an ink grey c, the column codes of the screen
 * are then a shift and a mask, and text is a chain of glyphs at pens p,
 * p + advance, ... whose codes are the screen's exactly (a glyph's ink
 * may reach into its neighbours' columns: ORed), with blank columns
 * before and after. The chain explaining the most ink wins (a search with
 * a memo on the pen and the glyph before). A run counts if it is mostly
 * letters and digits, its background is nowhere near the ink's grey, and
 * it is not a slice of a bigger shape (no ink of its grey just above or
 * below the glyphs where they touch the cell's edge). Biggest first, a
 * run whose ink another has taken is dropped. Glyphs with the same bitmap
 * (I l |, O 0) are told apart by the letters round them.
 */
(function (root) {
  "use strict";

  // identical bitmaps (l / I / |, O / 0 in some sizes): the likelier wins
  const PRI = "etaoinsrhldcumfpgwybvkxjqzETAOINSRHLDCUMFPGWYBVKXJQZ0123456789.,-:()/'";
  const prio = ch => { const i = PRI.indexOf(String.fromCharCode(ch)); return i < 0 ? 999 : i; };
  // the Eikon symbols the viewer draws: by char, what it is (TextMatch.SYMBOLS)
  const SYMBOLS = {
    34: "right", 35: "left", 47: "right", 49: "right", 51: "left", 54: "right", 55: "left",
    44: "up", 45: "down", 50: "right-open", 52: "left-open", 46: "tick", 48: "dot"
  };
  const ALNUM = /[A-Za-z0-9À-ÿ]/;

  function prep(f, bold) {
    const glyphs = [];
    let minL = 0, space = null;
    for (const e of f.glyphs) {
      const ch = e[0], l = e[1];
      let mv = e[2], cols = e.slice(3);
      if (bold) {                                 // (drawn twice, 1 px apart: 1 px wider)
        mv += 1;
        if (cols.length) cols = cols.map((v, j) => v | (j ? cols[j - 1] : 0)).concat([cols[cols.length - 1]]);
      }
      if (!cols.length) { if (ch === 32) space = mv; continue; }
      if (f.sym && !SYMBOLS[ch]) continue;
      let ink = 0, all = 0;
      for (const c of cols) { all |= c; let v = c; while (v) { ink += v & 1; v >>>= 1; } }
      let y0 = 0, y1 = f.cell - 1;
      while (y0 < f.cell && !(all >>> y0 & 1)) y0++;
      while (y1 > 0 && !(all >>> y1 & 1)) y1--;
      glyphs.push({ ch, l, mv, w: cols.length, cols: Uint32Array.from(cols), ink, y0, y1 });
      if (l < minL) minL = l;
    }
    // wider, then inkier first; then the likelier letter
    glyphs.sort((a, b) => b.mv - a.mv || b.ink - a.ink || prio(a.ch) - prio(b.ch));
    const first = new Map(), byL = new Map(), byCh = new Map();
    glyphs.forEach((g, i) => {
      g.i = i;
      byCh.set(g.ch, g);
      const k = g.cols[0];
      if (!first.has(k)) first.set(k, []);
      first.get(k).push(g);
      if (!byL.has(g.l)) byL.set(g.l, { all: [], code: new Map() });
      const e = byL.get(g.l);
      e.all.push(g);
      if (!e.code.has(k)) e.code.set(k, []);
      e.code.get(k).push(g);
    });
    // the font's cap height and x-height (rows of H and x above the baseline), for sizing an outline font
    const top = ch => { const g = byCh.get(ch); return g ? f.ascent - g.y0 : 0; };
    return {
      name: f.name, cell: f.cell, ascent: f.ascent, bold: bold ? 2 : f.bold ? 1 : 0, sym: !!f.sym,
      cap: top(72), xh: top(120), glyphs, first, byL, ls: Array.from(byL.keys()), byCh, minL,
      space: space || 4, mask: f.cell >= 32 ? 0xffffffff : ((1 << f.cell) - 1) >>> 0
    };
  }

  function create(pack, opts) {
    if (!pack || pack.format !== "psicast-fonts" || pack.version !== 1) throw new Error("textmatch: not a font pack");
    const fonts = [];
    const only = opts && opts.only;                 // ("text" or "sym": only those fonts)
    for (const f of pack.fonts) {
      if ((only === "text" && f.sym) || (only === "sym" && !f.sym)) continue;
      for (const v of f.variants || ["plain"]) fonts.push(prep(f, v === "bold"));
    }
    fonts.forEach((f, i) => { f.fi = i; });
    return new TextMatch(fonts, opts);
  }

  function TextMatch(fonts, opts) {
    this.fonts = fonts;
    this.maxCell = Math.max.apply(null, fonts.map(f => f.cell));
    this.opts = Object.assign({ minInk: 14, minGlyphs: 2, contrast: 3 }, opts || {});
    this.mbuf = null; this.claim = null;
  }

  TextMatch.prototype.find = function (g, w, h, rect) {
    const R = rect ? { x: Math.max(0, rect.x), y: Math.max(0, rect.y), w: 0, h: 0 } : { x: 0, y: 0, w, h };
    if (rect) { R.w = Math.min(w, rect.x + rect.w) - R.x; R.h = Math.min(h, rect.y + rect.h) - R.y; }
    if (R.w <= 0 || R.h <= 0) return [];
    const WW = (h + 31) >> 5, opt = this.opts;
    // column masks per grey
    const count = new Int32Array(16), masks = [];
    for (let y = R.y; y < R.y + R.h; y++) for (let o = y * w + R.x, e = o + R.w; o < e; o++) count[g[o]]++;
    if (!this.mbuf || this.mbuf.length !== 16 * w * WW) this.mbuf = new Uint32Array(16 * w * WW);
    for (let c = 0; c < 16; c++) {
      masks[c] = null;
      if (count[c] > 0) { masks[c] = this.mbuf.subarray(c * w * WW, (c + 1) * w * WW); masks[c].fill(0, R.x * WW, (R.x + R.w) * WW); }
    }
    // (and per grey the count of its pixels in the rows above each row: a
    // band of rows with too few of a grey is not looked at for it)
    const rows = R.h + 1, before = new Int32Array(16 * rows);
    for (let y = R.y; y < R.y + R.h; y++) {
      const wi = y >> 5, b = 1 << (y & 31), k = y - R.y + 1;
      for (let c = 0; c < 16; c++) before[c * rows + k] = before[c * rows + k - 1];
      for (let x = R.x, o = y * w + R.x; x < R.x + R.w; x++, o++) { const v = g[o]; masks[v][x * WW + wi] |= b; before[v * rows + k]++; }
    }
    // (offset 32: room for a negative left adjust and overhangs)
    const codes = new Uint32Array(w + 64);
    const runs = [];
    for (const f of this.fonts) {
      const H = f.cell, M = f.mask;
      for (let t = R.y; t + H <= R.y + R.h; t++) {
        const wi = t >> 5, s = t & 31, two = s + H > 32;
        for (let c = 0; c < 16; c++) {
          const m = masks[c];
          if (!m || before[c * rows + t - R.y + H] - before[c * rows + t - R.y] < (f.sym ? 8 : opt.minInk)) continue;
          let any = 0;
          for (let x = R.x; x < R.x + R.w; x++) {
            let v = m[x * WW + wi] >>> s;
            if (two) v |= m[x * WW + wi + 1] << (32 - s);
            v = (v & M) >>> 0;
            codes[x + 32] = v; any |= v;
          }
          if (!any) continue;
          codes.fill(0, 0, R.x + 32); codes.fill(0, R.x + R.w + 32);
          let skipTo = -1;
          for (let x = R.x; x < R.x + R.w; x++) {
            const v = codes[x + 32];
            if (!v || codes[x + 31] || x < skipTo) continue;
            const cand = f.first.get(v);
            if (!cand) continue;
            let best = null;
            for (const g0 of cand) {
              const run = grow(f, codes, x - g0.l, g0, R);
              if (run && (!best || run.ink > best.ink)) best = run;
            }
            if (best) {
              best.font = f; best.top = t; best.c = c;
              if (this.accept(best, g, w, h)) { runs.push(best); skipTo = best.end; }
            }
          }
        }
      }
    }
    return this.resolve(runs, w, h);
  };

  // the best chain of glyphs from pen p0 with g0 first (most ink explained,
  // ending cleanly): {glyphs, ink, start, end, n} or null. Only the
  // previous glyph's overhang reaches past a pen, so the memo is on (pen,
  // previous glyph). A symbol is one glyph on its own
  function grow(f, codes, p0, g0, R) {
    const minL = f.minL, O = 32, X0 = R.x, X1 = R.x + R.w;
    const prevAt = (pg, pp, x) => { if (!pg) return 0; const j = x - pp - pg.l; return j >= 0 && j < pg.w ? pg.cols[j] : 0; };
    const fits = (g, p, pg, pp) => {
      const a = p + g.l, b = a + g.w, end = p + g.mv + minL;
      if (a < X0 || b > X1) return false;
      if ((g.cols[0] & ~codes[a + O]) !== 0) return false;
      const lo = Math.min(p + minL, a), hi = Math.max(end, b);
      for (let x = lo; x < hi; x++) {
        const e = prevAt(pg, pp, x) | (x >= a && x < b ? g.cols[x - a] : 0), c = codes[x + O];
        if (x < end ? e !== c : (e & ~c) !== 0) return false;
      }
      return true;
    };
    const blankTo = (from, to, pg, pp) => { for (let x = from; x < to; x++) if (prevAt(pg, pp, x) !== codes[x + O]) return false; return true; };
    const endOk = (p, pg, pp) => blankTo(p + minL, Math.max(p + 1, pg ? pp + pg.l + pg.w : 0), pg, pp) && !codes[p + O];
    if (f.sym) {                                    // (its ink exactly, a blank column either side)
      const a = p0 + g0.l, e = a + g0.w;
      if (a < X0 || e > X1 || codes[a - 1 + O] || codes[e + O]) return null;
      for (let j = 0; j < g0.w; j++) if (codes[a + j + O] !== g0.cols[j]) return null;
      return { glyphs: [[g0.ch, p0, g0]], ink: g0.ink, start: a, end: e, n: 1 };
    }
    if (!fits(g0, p0, null, 0)) return null;
    const memo = new Map();
    const solve = (p, pg, pp, gi) => {               // the best after glyph pg at pp (the next pen p)
      const key = p * 4096 + gi;
      if (memo.has(key)) return memo.get(key);
      memo.set(key, null);
      let best = endOk(p, pg, pp) ? { ink: 0, seq: null } : null;
      for (let k = 0; k <= 2; k++) {                  // (up to two spaces between words)
        const q = p + k * f.space;
        if (k > 0 && !blankTo(p + minL, q + minL, pg, pp)) break;
        for (const l of f.ls) {
          const e = f.byL.get(l), ov = k ? 0 : prevAt(pg, pp, q + l);
          const list = ov ? e.all : e.code.get(codes[q + l + O]);
          if (!list) continue;
          for (const g of list) {
            if (!fits(g, q, k ? null : pg, pp)) continue;
            if (k && !blankTo(q + minL, q, pg, pp)) continue;
            const rest = solve(q + g.mv, g, q, g.i);
            if (!rest) continue;
            const ink = g.ink + rest.ink;
            if (!best || ink > best.ink) best = { ink, seq: { k, q, g, rest } };
          }
        }
      }
      memo.set(key, best);
      return best;
    };
    const r = solve(p0 + g0.mv, g0, p0, g0.i);
    if (!r) return null;
    const out = [[g0.ch, p0, g0]];
    let s = r.seq, p = p0 + g0.mv;
    while (s) {
      for (let k = 0; k < s.k; k++) out.push([32, p + k * f.space, null]);
      out.push([s.g.ch, s.q, s.g]); p = s.q + s.g.mv; s = s.rest.seq;
    }
    let n = 0;
    for (const q of out) if (q[2]) n++;
    return { glyphs: out, ink: g0.ink + r.ink, start: Math.min(p0, p0 + g0.l), end: p, n };
  }

  TextMatch.prototype.accept = function (run, g, w, h) {
    const o = this.opts, f = run.font, c = run.c, t = run.top;
    if (!f.sym) {
      if (run.ink < o.minInk) return false;
      let alnum = 0;
      for (const q of run.glyphs) if (q[2] && ALNUM.test(String.fromCharCode(q[0]))) alnum += q[2].ink;
      if (alnum < 0.6 * run.ink) return false;
      if (run.n < o.minGlyphs) {                    // (one letter: a big one, not an I l 1 | i !)
        const ch = String.fromCharCode(run.glyphs[0][0]);
        if (run.ink < 24 || !/[A-Za-z0-9]/.test(ch) || /[Il1|i!]/.test(ch)) return false;
      }
    }
    // the background: nothing near the ink's grey in the run's box
    const hist = new Int32Array(16);
    const x0 = Math.max(0, run.start), x1 = Math.min(w, run.end);
    for (let y = t; y < t + f.cell; y++) for (let x = x0, q = y * w + x0; x < x1; x++, q++) {
      const v = g[q];
      if (v === c) continue;
      if (Math.abs(v - c) < o.contrast) return false;
      hist[v]++;
    }
    let bg = c === 0 ? 15 : 0;
    for (let i = 0; i < 16; i++) if (i !== c && hist[i] > hist[bg]) bg = i;
    run.bg = bg;
    if (!f.sym) {
      // cut off by something in front (a menu, a shadow): what shows of its
      // first letter may look like another glyph (the tail of an f like `),
      // so it starts at its first letter or digit; likewise at its end
      // (cut: the column right next to its ink has a grey that is neither the ink's nor one of the background's, a tenth of it or more)
      let back = 0;
      for (let i = 0; i < 16; i++) back += hist[i];
      const cut = x => { if (x < 0 || x >= w) return false; for (let y = t; y < t + f.cell; y++) { const v = g[y * w + x]; if (v !== c && hist[v] < .1 * back) return true; } return false; };
      const G = run.glyphs, letter = q => q[2] && ALNUM.test(String.fromCharCode(q[0]));
      const inks = G.filter(q => q[2]), i0 = inks[0], i1 = inks[inks.length - 1];
      let a = 0, b = G.length;
      if (cut(i0[1] + i0[2].l - 1)) while (a < b && !letter(G[a])) a++;
      if (cut(i1[1] + i1[2].l + i1[2].w)) while (b > a && !letter(G[b - 1])) b--;
      if (a > 0 || b < G.length) {
        if (a >= b) return false;
        run.glyphs = G.slice(a, b);
        const f0 = run.glyphs[0], fl = run.glyphs[run.glyphs.length - 1];
        run.start = f0[1] + Math.min(0, f0[2].l);
        run.end = Math.max(fl[1] + fl[2].mv, fl[1] + fl[2].l + fl[2].w);
        run.ink = 0; run.n = 0;
        for (const q of run.glyphs) if (q[2]) { run.ink += q[2].ink; run.n++; }
        if (run.ink < o.minInk) return false;
      }
      // nothing but thin strokes (I l 1 | ! i . , : '): a picture's lines as often as text
      if (run.glyphs.every(q => !q[2] || /[Il1|!i.,:';]/.test(String.fromCharCode(q[0])))) return false;
    }
    // not a slice of a bigger shape: where a glyph's ink touches the cell's
    // top or bottom row, the pixel beyond is not the ink's grey
    for (const q of run.glyphs) {
      const gl = q[2];
      if (!gl) continue;
      for (let j = 0; j < gl.w; j++) {
        const x = q[1] + gl.l + j, v = gl.cols[j];
        if ((v & 1) && t > 0 && g[(t - 1) * w + x] === c) return false;
        if ((v >>> (f.cell - 1) & 1) && t + f.cell < h && g[(t + f.cell) * w + x] === c) return false;
      }
    }
    // a letter on its own, or a symbol: only its ink's grey and the
    // background 2 columns either side (not a piece of a picture)
    if (f.sym || run.n < o.minGlyphs) {
      for (let y = t; y < t + f.cell; y++) for (let x = Math.max(0, run.start - 2); x < Math.min(w, run.end + 2); x++) {
        const v = g[y * w + x];
        if (v !== c && v !== bg) return false;
      }
    }
    return true;
  };

  // glyphs with the same bitmap and advance (I l |, O 0): by the letters round them
  function disambiguate(r) {
    const f = r.font;
    if (!f.twins) {
      f.twins = new Map();
      const by = new Map();
      for (const g of f.glyphs) { const k = g.l + "," + Array.from(g.cols).join(","); if (!by.has(k)) by.set(k, []); by.get(k).push(g); }
      for (const list of by.values()) if (list.length > 1) for (const g of list) f.twins.set(g.ch, list);
    }
    const G = r.glyphs, ch = i => (i >= 0 && i < G.length ? String.fromCharCode(G[i][0]) : "");
    const lower = s => /[a-zß-ÿ]/.test(s), upper = s => /[A-ZÀ-Þ]/.test(s), letter = s => /[A-Za-zÀ-ÿ]/.test(s);
    for (let i = 0; i < G.length; i++) {
      const tw = f.twins.get(G[i][0]);
      if (!tw) continue;
      const last = i === G.length - 1 || G[i + 1][0] === 32, cur = tw.find(g => g.ch === G[i][0]);
      const opts = tw.filter(g => last || g.mv === cur.mv).map(g => String.fromCharCode(g.ch)), p = ch(i - 1), n = ch(i + 1);
      let want = null;
      if (opts.includes("l") && opts.includes("I")) {
        // the word: all capitals -> I; else l inside a word, and at its
        // start I before a consonant (It, In, Is), l before a vowel (list, line)
        let s = i, e = i;
        while (s > 0 && letter(ch(s - 1))) s--;
        while (e < G.length - 1 && letter(ch(e + 1))) e++;
        let lo = false;
        for (let k = s; k <= e; k++) { if (f.twins.has(G[k][0])) continue; if (lower(ch(k))) lo = true; }
        if (!lo) want = "I";
        else if (i > s) want = upper(n) ? "I" : "l";
        else want = /[aeiouy]/.test(n) ? "l" : "I";
      } else if (opts.includes("0") && opts.includes("O")) want = /[0-9]/.test(p + n) ? "0" : "O";
      if (want && opts.includes(want)) G[i][0] = want.charCodeAt(0);
    }
  }

  // biggest first; a run whose ink another run already explains is dropped
  TextMatch.prototype.resolve = function (runs, w, h) {
    runs.sort((a, b) => b.ink - a.ink || b.font.cell - a.font.cell);
    if (!this.claim || this.claim.length !== w * h) this.claim = new Uint8Array(w * h);
    const claim = this.claim, out = [], all = [];
    for (const r of runs) {
      const px = [];
      for (const q of r.glyphs) {
        const gl = q[2];
        if (!gl) continue;
        for (let j = 0; j < gl.w; j++) {
          const v = gl.cols[j], x = q[1] + gl.l + j;
          for (let k = gl.y0; k <= gl.y1; k++) if (v >>> k & 1) px.push((r.top + k) * w + x);
        }
      }
      let taken = false;
      for (const q of px) if (claim[q]) { taken = true; break; }
      if (taken) continue;
      for (const q of px) claim[q] = 1;
      all.push(px);
      r.px = px;
      out.push(r);
    }
    for (const px of all) for (const q of px) claim[q] = 0;
    for (const r of out) {
      if (!r.font.sym) disambiguate(r);
      r.text = String.fromCharCode.apply(null, r.glyphs.map(q => q[0]));
    }
    return out;
  };

  const api = { create, SYMBOLS };
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.TextMatch = api;
})(typeof self !== "undefined" ? self : this);
