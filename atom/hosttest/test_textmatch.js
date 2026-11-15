// test_textmatch.js: src/textmatch.js (the viewer's sharp text and symbols)
// against screens drawn here with the ROM fonts and real 5mx screens.
//
//   node hosttest/test_textmatch.js [--screens DIR] [--scan NAME...]
//
// The fonts are src/eonfonts_local.json (tools/mkfonts.py, from the SDK):
// a local file, so without it the test is skipped, not failed. The screens
// are the emulator's screenshots, as for test_roles.js (without them only
// the drawn screens are checked). --scan prints every run found on the
// named screens (to grow EXPECT).
//
// The checks: text drawn with the fonts (each face, black on white, white
// on black, on a dithered title bar, the algorithmic bolds, the toolbar's)
// is found exactly, where it was drawn; the symbols; twins (I l, O 0) by
// the letters round them; nothing in a dithered picture, a solid box or a
// slice of bigger text; a word cut off at its start begins at its first
// letter; the runs on real screens; a full screen and a strip are quick.
"use strict";
const fs = require("fs"), path = require("path"), zlib = require("zlib");
const TextMatch = require("../src/textmatch.js");

const here = __dirname;
const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : null; };
const flag = (name) => { const i = args.indexOf(name); if (i >= 0) { args.splice(i, 1); return true; } return false; };
const screensDir = opt("--screens") || process.env.PSION_SCREENS ||
  [path.join(here, "..", "..", "build", "emu"), path.join(here, "..", "..", "..", "psiterm", "build", "emu")].find(d => fs.existsSync(path.join(d, "dt-prefs"))) ||
  path.join(here, "..", "..", "build", "emu");
const scan = flag("--scan");
const fontsPath = path.join(here, "..", "src", "eonfonts_local.json");
if (!fs.existsSync(fontsPath)) { console.log("test_textmatch: SKIP (no src/eonfonts_local.json: python3 tools/mkfonts.py)"); process.exit(0); }
const pack = JSON.parse(fs.readFileSync(fontsPath, "utf8"));
const text = TextMatch.create(pack, { only: "text" }), sym = TextMatch.create(pack, { only: "sym" });
const W = 640, H = 240;

// (the same PNG reader as test_roles.js)
const roles = fs.readFileSync(path.join(here, "test_roles.js"), "utf8");
const readPng = new Function("fs", "zlib", roles.slice(roles.indexOf("function readPng"), roles.indexOf("let fails")) + "; return readPng;")(fs, zlib);

let fails = 0, checks = 0;
const check = (ok, what) => { checks++; if (!ok) { fails++; console.log("FAIL " + what); } };
const label = r => r.font.name + r.font.cell + ["", "b", "B"][r.font.bold];
const show = r => `${label(r)} y${r.top} x${r.start}-${r.end} c${r.c}/bg${r.bg} ${JSON.stringify(r.font.sym ? TextMatch.SYMBOLS[r.glyphs[0][0]] : r.text)}`;

// ---- drawing with the fonts: a string at pen x, cell top y, in grey c ----
function fontOf(name, cell, bold) {
  const f = pack.fonts.find(f => f.name === name && f.cell === cell && (bold === 1 ? f.bold : !f.bold || bold === 2));
  if (!f) throw new Error("no font " + name + cell);
  return f;
}
function put(g, f, bold, x, y, s, c) {
  for (const chr of s) {
    const e = f.glyphs.find(q => q[0] === chr.charCodeAt(0));
    let cols = e.slice(3), mv = e[2];
    if (bold === 2) { mv += 1; if (cols.length) cols = cols.map((v, j) => v | (j ? cols[j - 1] : 0)).concat([cols[cols.length - 1]]); }
    cols.forEach((v, j) => { for (let k = 0; k < f.cell; k++) if (v >>> k & 1) g[(y + k) * W + x + e[1] + j] = c; });
    x += mv;
  }
  return x;
}
const box = (g, x0, y0, x1, y1, v) => { for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) g[y * W + x] = v; };

{
  const g = new Uint8Array(W * H).fill(15);
  const A15 = fontOf("Arial", 15), A11 = fontOf("Arial", 11), A11b = fontOf("Arial", 11, 1), A18 = fontOf("Arial", 18), TB = fontOf("Toolbar", 13);
  put(g, A15, 0, 10, 4, "Preferences and settings", 0);
  box(g, 8, 24, 120, 40, 0); put(g, A11b, 1, 14, 27, "Cancel the job", 15);
  for (let y = 44; y < 62; y++) for (let x = 200; x < 460; x++) g[y * W + x] = (x + y) & 1 ? 10 : 15;
  put(g, A15, 0, 210, 45, "Title on a dither", 0);
  put(g, A18, 2, 10, 64, "Agenda", 0);
  put(g, TB, 2, 580, 30, "Control", 0);
  put(g, A15, 0, 10, 90, "Install All programs: Isle 1001 or O'Neill", 0);
  put(g, A11, 0, 10, 110, "Shift+Ctrl+H", 5);
  // the Eikon symbols: a choice list's arrows, a tick
  const EK = pack.fonts.find(f => f.sym);
  put(g, EK, 0, 300, 70, "3", 0); put(g, EK, 0, 330, 70, "1", 0); put(g, EK, 0, 360, 70, ".", 0);
  // not text: a dithered picture, a solid box, and a word with a line on its top (a slice of something bigger)
  let seed = 7;
  for (let y = 130; y < 200; y++) for (let x = 10; x < 200; x++) { seed = (seed * 1103515245 + 12345) & 0x7fffffff; g[y * W + x] = [0, 5, 10, 15, 3, 7][seed % 6]; }
  box(g, 220, 130, 300, 170, 0);
  put(g, A15, 0, 330, 140, "HHHH", 0); box(g, 330, 139, 380, 139, 0);
  // a word whose start is under a menu's shadow: the f of "f'ward" half hidden
  // (the shadow over the f, right up to the apostrophe)
  const fx = 450, fg = A15.glyphs.find(q => q[0] === 102);
  put(g, A15, 0, fx, 180, "f'ward", 0);
  box(g, fx - 6, 180, fx + fg[2], 194, 5);
  const runs = text.find(g, W, H), syms = sym.find(g, W, H);
  const find = s => runs.find(r => r.text === s);
  for (const [s, font, x, y, c] of [["Preferences and settings", "Arial15", 10, 4, 0], ["Cancel the job", "Arial11b", 14, 27, 15],
      ["Title on a dither", "Arial15", 210, 45, 0], ["Agenda", "Arial18B", 10, 64, 0], ["Control", "Toolbar13B", 580, 30, 0],
      ["Install All programs: Isle 1001 or O'Neill", "Arial15", 10, 90, 0], ["Shift+Ctrl+H", "Arial11", 10, 110, 5]]) {
    const r = find(s);
    check(r && label(r) === font && r.top === y && r.c === c && Math.abs(r.start - x) <= 1,
      `drawn: "${s}" in ${font} at ${x},${y} (${r ? show(r) : runs.map(show).join("; ")})`);
  }
  check(find("Title on a dither") && find("Title on a dither").bg === 10, "drawn: a dithered title bar's text has its light grey behind it");
  check(find("Cancel the job") && find("Cancel the job").bg === 0, "drawn: white writing on black: the background is black");
  const kinds = syms.map(r => TextMatch.SYMBOLS[r.glyphs[0][0]] + "@" + r.start).sort().join(" ");
  check(kinds === "left@300 right@330 tick@" + (360 + 2), "drawn: the symbols (" + kinds + ")");
  const bad = runs.filter(r => (r.top >= 125 && r.top < 200 && r.start < 310) || r.text.indexOf("HHHH") >= 0);
  check(!bad.length, "drawn: nothing in a picture, a box or a slice of something bigger (" + bad.map(show).join("; ") + ")");
  const cutRun = runs.find(r => r.top === 180);
  check(cutRun && /^[a-z]/.test(cutRun.text) && cutRun.text.endsWith("ward"), "drawn: a word cut off at its start begins at a letter (" + (cutRun ? show(cutRun) : "none") + ")");
  check(runs.every(r => r.px.length > 0 && r.px.every(i => g[i] === r.c)), "drawn: every run's ink pixels are its grey");
}

// ---- real screens: [text or symbol, x (start, ±1), cell top] that must be found ----
const EXPECT = {
  // PsiTerm's Preferences: the title, a selected prompt (white on black),
  // the choice list's arrows, the buttons, the toolbar's captions, the status line
  "dt-prefs/end.png": [["Preferences", 137, 16], ["Theme", 138, 43], ["Classic", 295, 43], ["left", 275, 42], ["right", 491, 42],
    ["Blinking cursor", 138, 93], ["Coloured text", 138, 168], ["Dark greys", 295, 168], ["Screen greys\x85", 154, 202], ["Cancel", 299, 202],
    ["OK", 429, 202], ["SSH to", 601, 37], ["Not connected", 6, 227], ["06:51", 532, 227]],
  // PsiMail's File menu: the menu bar, a selected item and its shortcut,
  // dimmed items, the tick, the list behind it, the toolbar
  "pmail/end.png": [["File", 27, 11], ["Check mail (send & receive)", 31, 38], ["Shift+Ctrl+C", 267, 41], ["Disconnect", 31, 110],
    ["Ctrl+U", 267, 113], ["tick", 18, 154], ["Work offline", 31, 154], ["Close", 31, 210], ["Reply/", 601, 67], ["Date", 448, 23]],
  // the System screen: big bold names, white on dark grey, the toolbar, the Extras bar's captions
  "kern1/end.png": [["System", 582, 7], ["Data", 262, 24], ["Welcome to", 83, 71], ["Series 5mx", 83, 90], ["Agenda", 262, 82],
    ["Control", 601, 30], ["panel", 601, 44], ["Comms", 34, 177], ["Bombs", 424, 227]],
  // a dialog with tabs, a choice list, a note in the small font
  "ds-cal/end.png": [["Calendar settings", 49, 15], ["Sync with Agenda", 53, 73], ["left", 215, 72], ["right", 274, 72],
    ["Leave it blank to use the mail account's password", 174, 146], ["(sync first)", 235, 166]],
  // (with their ellipses: the font's own, 0x85)
  "inst1/end.png": [["Installing program", 69, 31], ["Opening installation file\x85", 220, 109], ["Opening file\x85", 518, 213]],
};
// ...and what must not be: [text, cell top]
const NOT = { "pweb/end.png": ["II"], "ds-cal/end.png": ["`ward"] };
function screen(rel) {
  const file = path.join(screensDir, rel);
  if (!fs.existsSync(file)) return null;
  const s = readPng(file);
  return s.w === W && s.h === H ? s.grey : null;
}
let seen = 0;
for (const rel of new Set(Object.keys(EXPECT).concat(Object.keys(NOT)))) {
  const g = screen(rel);
  if (!g) { console.log("(no " + rel + ")"); continue; }
  seen++;
  const runs = text.find(g, W, H).concat(sym.find(g, W, H));
  const name = r => r.font.sym ? TextMatch.SYMBOLS[r.glyphs[0][0]] : r.text;
  for (const [s, x, y] of EXPECT[rel] || [])
    check(runs.some(r => name(r) === s && r.top === y && Math.abs(r.start - x) <= 1), `${rel}: "${s}" at ${x},${y}`);
  for (const s of NOT[rel] || []) check(!runs.some(r => name(r) === s), `${rel}: no "${s}"`);
}
if (scan) for (const n of args) {
  const g = screen(n + "/end.png");
  if (!g) continue;
  console.log("== " + n);
  for (const r of text.find(g, W, H).concat(sym.find(g, W, H)).sort((a, b) => a.top - b.top || a.start - b.start)) console.log("  " + show(r));
}

// ---- timing: a whole screen, and a strip (a change's rows and a line of text either side), warm ----
{
  const g = screen("dt-prefs/end.png");
  if (g) {
    for (let i = 0; i < 3; i++) { text.find(g, W, H); sym.find(g, W, H); }
    let t0 = process.hrtime.bigint();
    for (let i = 0; i < 5; i++) { text.find(g, W, H); sym.find(g, W, H); }
    const full = Number(process.hrtime.bigint() - t0) / 1e6 / 5;
    t0 = process.hrtime.bigint();
    for (let i = 0; i < 20; i++) { const R = { x: 0, y: 70, w: W, h: 56 }; text.find(g, W, H, R); sym.find(g, W, H, R); }
    const strip = Number(process.hrtime.bigint() - t0) / 1e6 / 20;
    console.log(`timing: a whole screen ${full.toFixed(1)} ms, a strip of 56 rows ${strip.toFixed(1)} ms`);
    check(full < 400 && strip < 60, `timing: ${full.toFixed(1)} ms a screen, ${strip.toFixed(1)} ms a strip`);
  }
}

if (!seen) console.log("test_textmatch: no emulator screens at " + screensDir + ": only the drawn screens were checked");
console.log(fails ? `test_textmatch: ${fails} of ${checks} checks FAILED` : `test_textmatch: all ${checks} checks passed`);
process.exit(fails ? 1 : 0);
