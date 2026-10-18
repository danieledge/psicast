// test_roles.js: the viewer's role pass and its painting (src/viewer.html:
// findRoles, paintRow and the theme tables) against real 5mx screens.
//
//   node hosttest/test_roles.js [--screens DIR] [--boxes NAME...]
//
// The screens are the emulator's screenshots (640x240 greyscale PNGs, white
// = 153, or 255; some sets show black as level 1 and dark grey as 6, and
// are read as 0 and 5). Without them the screen checks are skipped, not
// failed. --boxes prints the boxes of every role found on the named
// screens (to grow EXPECT).
//
// The checks: each box in EXPECT holds at least so many pixels of its role
// (and a box with 0 holds none: a busy message is not a selection); a
// dialog whose lower rows have not arrived yet (a big change comes a strip
// at a time, top first) already has its title bar and body; the holes in
// writing; the flat painting of checkerboards; a pass is quick.
"use strict";
const fs = require("fs"), path = require("path"), zlib = require("zlib");

const here = __dirname;
const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : null; };
const flag = (name) => { const i = args.indexOf(name); if (i >= 0) { args.splice(i, 1); return true; } return false; };
const screensDir = opt("--screens") || path.join(here, "..", "..", "..", "build", "emu");
const boxes = flag("--boxes");

// ---- the viewer's code: the themes and tables, then the role pass and the painting ----
const src = fs.readFileSync(path.join(here, "..", "src", "viewer.html"), "utf8");
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b); if (i < 0 || j < i) throw new Error("viewer.html: no " + a); return src.slice(i, j); };
const themes = cut("const THEMES = [", "// ----- the screen:");
const roles = cut("const R_TITLE", "// ----- the frame's size");
const paint = cut("function paintRow", "function paintAll");
const V = new Function("store", "W", "H", "grey", "role", "roleGrey", "px",
  themes + roles + paint + "; return { THEMES, themeRgb, UI_COLOURS, makeLut, lut, setTheme: t => { theme = t; }, findRoles, paintRow, " +
  "R: { TITLE: R_TITLE, TTEXT: R_TTEXT, DLG: R_DLG, TBAR: R_TBAR, FACE: R_FACE, DIM: R_DIM, SET: R_SET, SEL: R_SEL, SHAFT: R_SHAFT, SFACE: R_SFACE, MENU: R_MENU, SEL2: R_SEL2 } };");
const W = 640, H = 240;
const G = { grey: new Uint8Array(W * H), role: new Uint8Array(W * H), roleGrey: new Uint8Array(W * H), px: new Uint32Array(W * H) };
const v = V({ get: (k, d) => d, set() {} }, W, H, G.grey, G.role, G.roleGrey, G.px);
const NAME = Object.fromEntries(Object.entries(v.R).map(([n, r]) => [r, n]));

// ---- the expected roles: screen, then [role, x0, y0, x1, y1, at least this many pixels (0: none)] ----
const EXPECT = {
  // PsiTerm's Preferences dialog: its title bar and title, its body, a
  // selected prompt, its three buttons; the toolbar and its buttons
  "dt-prefs/end.png": [
    ["TITLE", 129, 13, 510, 32, 6000], ["TTEXT", 138, 16, 146, 27, 40], ["DLG", 129, 34, 510, 226, 50000],
    ["SEL", 137, 42, 192, 58, 600], ["FACE", 152, 199, 248, 215, 1200], ["FACE", 390, 199, 486, 215, 1400],
    ["TBAR", 571, 1, 638, 24, 1200], ["FACE", 573, 28, 636, 58, 1300], ["FACE", 573, 138, 636, 169, 1500],
    ["MENU", 0, 0, 639, 239, 0], ["SEL2", 0, 0, 639, 239, 0],
    // the holes in the prompts' writing are the body's (2726 without); not
    // those in an edit box's writing, which stays white
    ["DLG", 137, 62, 260, 85, 2740], ["DLG", 296, 67, 368, 82, 0],
  ],
  // the System screen: the file's name highlighted in dark grey, in a black line;
  // the toolbar's buttons' faces, the holes in their writing too (2614 without)
  "kern1/end.png": [["SEL2", 80, 68, 194, 111, 3800], ["SEL", 0, 0, 639, 239, 0], ["TITLE", 0, 0, 639, 239, 0],
    ["FACE", 600, 14, 636, 133, 2700]],
  // ...and in black, with the focus
  "castc/end.png": [["SEL", 80, 68, 194, 111, 3800], ["SEL2", 0, 0, 639, 239, 0]],
  // PsiMail with its File menu open: the menu, its selected item, the
  // message list's selected row behind (drawn up to the column line and
  // the scroll bar), the scroll bar's thumb and shaft
  "pmail/end.png": [
    ["MENU", 15, 7, 360, 230, 50000], ["SEL", 17, 35, 358, 56, 6000], ["SEL", 364, 57, 543, 80, 3500],
    ["SFACE", 549, 47, 565, 126, 1200], ["SHAFT", 547, 130, 567, 191, 1200], ["TITLE", 0, 0, 639, 239, 0],
  ],
  // PsiTerm's tmux menu: the pane, the selected item; nothing else selected
  "ptmenu/end.png": [["MENU", 343, 7, 553, 202, 28000], ["SEL", 345, 35, 551, 56, 3500], ["SEL", 0, 60, 639, 239, 0]],
  // a file dialog (no title bar on the screen): buttons, the Disk prompt, two scroll bars
  "fbword2/end.png": [["FACE", 365, 21, 420, 39, 700], ["SEL", 34, 23, 69, 39, 380], ["SFACE", 182, 52, 198, 171, 1800],
    ["SFACE", 527, 177, 543, 194, 250], ["TITLE", 0, 0, 639, 239, 0]],
  // installing: a dialog with its picture, and a busy message bottom right
  // (a black box in a black border, a light line between): not a selection
  "inst1/end.png": [["TITLE", 61, 28, 578, 47, 9000], ["DLG", 61, 49, 578, 211, 70000], ["FACE", 297, 184, 341, 200, 550],
    ["SEL", 500, 200, 639, 239, 0], ["SEL2", 500, 200, 639, 239, 0]],
  // a cascade: the open cascade's selected item; its parent is a dark grey
  // row whose writing touches its left edge
  "logmenu/end.png": [["MENU", 85, 71, 350, 234, 30000], ["SEL", 87, 73, 348, 94, 5000], ["SEL2", 362, 150, 630, 169, 4000]],
  // PsiCast's own screen: a set toolbar button (Control)
  "cast10/end.png": [["SET", 572, 100, 638, 134, 1900], ["FACE", 573, 28, 636, 58, 1600]],
  // PsiMail's calendar settings: a dialog with tabs
  "ds-cal/end.png": [["TITLE", 41, 12, 597, 31, 10000], ["DLG", 41, 33, 597, 227, 30000], ["SEL", 52, 72, 199, 88, 1700]],
  // PsiWeb with a menu: the menu, the scroll bar
  "pweb/end.png": [["MENU", 176, 7, 463, 80, 8500], ["SEL", 178, 35, 461, 56, 5000], ["SHAFT", 548, 130, 568, 192, 1200]],
  // an infoprint at the top right: a message, not a selection
  "ptdsa/end.png": [["SEL", 20, 5, 639, 32, 0]],
};

// ---- a small PNG reader: 8-bit, as the emulator writes ----
function readPng(file) {
  const b = fs.readFileSync(file);
  let p = 8, w = 0, h = 0, depth = 0, type = 0;
  const idat = [];
  while (p < b.length) {
    const len = b.readUInt32BE(p), t = b.toString("ascii", p + 4, p + 8);
    const data = b.subarray(p + 8, p + 8 + len);
    if (t === "IHDR") { w = data.readUInt32BE(0); h = data.readUInt32BE(4); depth = data[8]; type = data[9]; }
    else if (t === "IDAT") idat.push(data);
    else if (t === "IEND") break;
    p += 12 + len;
  }
  const chans = { 0: 1, 2: 3, 4: 2, 6: 4 }[type];
  if (depth !== 8 || !chans) throw new Error(file + ": not an 8-bit PNG");
  const raw = zlib.inflateSync(Buffer.concat(idat)), stride = w * chans;
  const out = new Uint8Array(w * h * chans);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], s = y * (stride + 1) + 1, o = y * stride;
    for (let x = 0; x < stride; x++) {
      const a = x >= chans ? out[o + x - chans] : 0, up = y ? out[o - stride + x] : 0, c = x >= chans && y ? out[o - stride + x - chans] : 0;
      let q = raw[s + x];
      if (f === 1) q += a; else if (f === 2) q += up; else if (f === 3) q += (a + up) >> 1;
      else if (f === 4) { const pa = Math.abs(up - c), pb = Math.abs(a - c), pc = Math.abs(a + up - 2 * c); q += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      out[o + x] = q & 255;
    }
  }
  let max = 0;
  for (let i = 0; i < w * h; i++) if (out[i * chans] > max) max = out[i * chans];
  const white = max > 160 ? 255 : 153, grey = new Uint8Array(w * h);
  for (let i = 0; i < w * h; i++) grey[i] = Math.min(15, Math.round(out[i * chans] / white * 15));
  let zero = 0, one = 0;                       // (black as level 1, dark grey as 4 or 6: some sets)
  for (let i = 0; i < w * h; i++) { if (grey[i] === 0) zero++; else if (grey[i] === 1) one++; }
  if (!zero && one) for (let i = 0; i < w * h; i++) { if (grey[i] === 1) grey[i] = 0; else if (grey[i] === 4 || grey[i] === 6) grey[i] = 5; }
  return { w, h, grey };
}

let fails = 0, checks = 0;
const fail = (s) => { fails++; console.log("FAIL " + s); };
const stack = new Int32Array(W * H);
function rolesOf(grey) { v.findRoles(grey, W, H, G.role, stack); return G.role; }
function count(role, r, x0, y0, x1, y1) {
  let n = 0;
  for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) if (role[y * W + x] === r) n++;
  return n;
}
function screen(rel) {
  const file = path.join(screensDir, rel);
  if (!fs.existsSync(file)) return null;
  const s = readPng(file);
  return s.w === W && s.h === H ? s.grey : null;
}

// ---- the screens ----
let seen = 0;
for (const rel of Object.keys(EXPECT)) {
  const grey = screen(rel);
  if (!grey) { console.log("(no " + rel + ")"); continue; }
  seen++;
  const role = rolesOf(grey);
  for (const [name, x0, y0, x1, y1, least] of EXPECT[rel]) {
    checks++;
    const n = count(role, v.R[name], x0, y0, x1, y1);
    if (least ? n < least : n) fail(`${rel}: ${n} pixels of ${name} in ${x0},${y0}-${x1},${y1}, ${least ? "at least " + least : "none"} expected`);
  }
}
if (boxes) {
  for (const name of args) {
    const grey = screen(name + "/end.png");
    if (!grey) continue;
    const role = rolesOf(grey), done = new Uint8Array(W * H);
    for (let i = 0; i < W * H; i++) {
      if (!role[i] || done[i]) continue;
      const r = role[i], q = [i];
      let x0 = W, x1 = 0, y0 = H, y1 = 0, n = 0;
      done[i] = 1;
      while (q.length) {
        const o = q.pop(), x = o % W, y = (o / W) | 0;
        n++;
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
        for (const d of [-1, 1, -W, W]) {
          const p = o + d;
          if (p >= 0 && p < W * H && !done[p] && role[p] === r && Math.abs((p % W) - x) <= 1) { done[p] = 1; q.push(p); }
        }
      }
      if (n >= 40) console.log(`${name}: ${NAME[r]} ${x0},${y0}-${x1},${y1} (${n})`);
    }
  }
}

// ---- a dialog arriving a strip at a time: PsiTerm's Preferences (its
// border runs down column 124 from row 12 to 227) over PsiTerm's own
// screen, with only its first rows there yet ----
{
  const base = screen("pterm/end.png"), dlg = screen("dt-prefs/end.png");
  if (base && dlg) {
    // (with its title bar whole and a row of its body, it is coloured as
    // far as it goes; with the title bar alone, not yet)
    for (const [rows, title] of [[30, false], [36, true], [60, true], [120, true], [240, true]]) {
      const grey = new Uint8Array(base);
      grey.set(dlg.subarray(0, rows * W), 0);
      const role = rolesOf(grey);
      const last = Math.min(226, rows - 2), t = count(role, v.R.TITLE, 129, 13, 510, 32), b = count(role, v.R.DLG, 129, 34, 510, last);
      checks++;
      if ((t >= 6000) !== title) fail(`a dialog with ${rows} rows arrived: ${t} pixels of title bar`);
      checks++;
      if (title ? b < .4 * 382 * (last - 33) : b) fail(`a dialog with ${rows} rows arrived: ${b} pixels of body down to row ${last}`);
      checks++;
      if (count(role, v.R.DLG, 129, rows + 1, 510, 239)) fail(`a dialog with ${rows} rows arrived: body below them`);
    }
    // the rows below the border's end being whatever was there, not white: no dialog yet
    const grey = new Uint8Array(base);
    grey.set(dlg.subarray(0, 60 * W), 0);
    grey.fill(0, 59 * W + 130, 59 * W + 500);
    checks++;
    if (count(rolesOf(grey), v.R.TITLE, 129, 13, 510, 32)) fail("a dialog whose border ends on a black row got a title bar");
  }
}

// ---- the painting: Modern is flat (a checkerboard of two greys is one tint) ----
{
  const modern = v.THEMES.find(t => t.id === "modern");
  v.setTheme(modern);
  v.makeLut();
  const little = new Uint8Array(new Uint32Array([1]).buffer)[0] === 1;
  const rgb = c => little ? [c & 255, (c >> 8) & 255, (c >> 16) & 255] : [c >>> 24, (c >> 16) & 255, (c >> 8) & 255];
  const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
  const plain = g => v.themeRgb(modern, g), pale = modern.flat[0][2];
  G.grey.fill(15); G.role.fill(0);
  // a dithered light grey box with black writing and a dark grey shadow,
  // a dithered mid grey, a solid grey, lines 1 wide
  for (let y = 20; y < 60; y++) for (let x = 20; x < 120; x++) G.grey[y * W + x] = (x + y) & 1 ? 10 : 15;
  for (let y = 30; y < 40; y++) for (let x = 40; x < 60; x++) G.grey[y * W + x] = 0;
  for (let y = 20; y < 60; y++) G.grey[y * W + 120] = 5;
  for (let y = 80; y < 100; y++) for (let x = 20; x < 60; x++) G.grey[y * W + x] = (x + y) & 1 ? 0 : 15;
  for (let y = 80; y < 100; y++) for (let x = 80; x < 120; x++) G.grey[y * W + x] = 10;
  for (let y = 120; y < 140; y++) for (let x = 20; x < 60; x++) G.grey[y * W + x] = x & 1 ? 10 : 15;
  // a dot (the dot of an i), and a hole in a letter
  G.grey[130 * W + 200] = 0;
  for (let y = 128; y < 133; y++) for (let x = 218, o = y * W + x; x < 223; x++, o++) G.grey[o] = x === 220 && y === 130 ? 15 : 0;
  for (let y = 0; y < H; y++) v.paintRow(y, 0, W, false);
  const at = (x, y) => rgb(G.px[y * W + x]);
  const want = [
    [25, 25, pale, "a dithered light grey is the pale tint"], [20, 24, pale, "...to its edge"], [24, 20, pale, "...and its top edge"],
    [20, 20, plain(15), "its corner pixel is itself"], [118, 59, pale, "...its far edge is the tint"],
    [39, 35, pale, "...up to its writing"], [61, 35, pale, "...on both sides"], [50, 35, plain(0), "its writing is ink"],
    [120, 30, plain(5), "a dark grey line beside it is dark grey"], [121, 30, plain(15), "the paper past it is paper"],
    [30, 90, [0, 1, 2].map(i => (plain(0)[i] + plain(15)[i]) >> 1), "a dithered mid grey is the mean of its two"],
    [90, 90, plain(10), "a solid grey is its own colour"], [80, 90, plain(10), "...at its edge"],
    [30, 130, plain(15), "lines 1 wide are not a checkerboard"], [31, 130, plain(10), "...either of them"],
    [200, 130, plain(0), "a dot is ink"], [220, 130, plain(15), "the hole in a letter is paper"],
  ];
  for (const [x, y, c, what] of want) {
    checks++;
    if (!same(at(x, y), c)) fail(`Modern: ${what} (${at(x, y)} at ${x},${y}, ${c} expected)`);
  }
  // a stale role (its grey has changed) is painted as no role: the grey's
  // own colour (no longer one of the checkerboard)
  G.role[25 * W + 25] = v.R.TITLE; G.roleGrey[25 * W + 25] = 15; G.grey[25 * W + 25] = 10;
  v.paintRow(25, 0, W, true);
  checks++;
  if (!same(at(25, 25), plain(10))) fail("a role whose grey has changed is painted as none (" + at(25, 25) + ")");
  G.roleGrey[25 * W + 25] = 10;
  v.paintRow(25, 0, W, true);
  checks++;
  if (!same(at(25, 25), v.UI_COLOURS.modern[v.R.TITLE])) fail("a role whose grey is as found is painted (" + at(25, 25) + ")");
  // Paper is not flat: the greys exactly
  v.setTheme(v.THEMES.find(t => t.id === "paper"));
  v.makeLut();
  G.role.fill(0);
  for (let y = 0; y < H; y++) v.paintRow(y, 0, W, false);
  checks++;
  let bad = 0;
  for (let i = 0; i < W * H; i++) if (!same(rgb(G.px[i]), [G.grey[i] * 17, G.grey[i] * 17, G.grey[i] * 17])) bad++;
  if (bad) fail(`Paper: ${bad} pixels are not their greys`);
}

// ---- timing: the pass on a busy screen, warm ----
{
  const grey = screen("dt-prefs/end.png");
  if (grey) {
    for (let i = 0; i < 10; i++) rolesOf(grey);
    const n = 40, t0 = process.hrtime.bigint();
    for (let i = 0; i < n; i++) rolesOf(grey);
    const ms = Number(process.hrtime.bigint() - t0) / 1e6 / n;
    checks++;
    if (ms > 15) fail(`the role pass takes ${ms.toFixed(1)} ms`);
    console.log(`timing: the role pass on a dialog ${ms.toFixed(2)} ms`);
  }
}

if (!seen) console.log("test_roles: no emulator screens at " + screensDir + ": only the painting was checked");
console.log(fails ? `test_roles: ${fails} of ${checks} checks FAILED` : `test_roles: all ${checks} checks passed`);
process.exit(fails ? 1 : 0);
