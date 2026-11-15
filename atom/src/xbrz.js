/* xbrz.js: the PsiCast viewer's "High-res" upscaling: xBRZ "freescale" as
 * a WebGL 2 shader, drawing the Psion's 640x240 picture straight at the
 * phone's own resolution, with a photo guard. Loaded only when High-res is
 * chosen. Plain ES2017; no dependencies.
 *
 * Licence: GNU General Public License version 3 (the shader below is a
 * port of libretro's glsl-shaders xbrz/shaders/xbrz-freescale.glsl, which
 * carries code from xBRZ, GPL-3.0; see the notices with it). The rest of
 * PsiCast is MIT; this file, loaded on its own, is GPL-3.0.
 *
 *   const r = Xbrz.create(onRestore);   // null without WebGL 2
 *   r.upload(tex, W, H, rects);         // tex: Uint32Array W*H, RGBA bytes,
 *                                       // alpha = the Psion grey * 17; rects:
 *                                       // [{x, y, w, h}] changed, or null: all
 *   r.render(dw, dh, sharp);            // -> its canvas (dw x dh), or null if lost
 *
 * The photo guard: xBRZ turns a dithered picture's dots into worms, so a
 * pixel whose 3x3 neighbourhood has a grey other than EIKON's four (0, 5,
 * 10, 15: the alpha of the texture, so it works under any colour theme)
 * is a photo's: its dither dots are smoothed away instead (dedither: an
 * edge-keeping blur and a light sharpen, 0.22). With sharp set (below 2x)
 * the rest is drawn sharp bilinear (a whole-number blow-up with hard edges,
 * smoothed to the exact size: every Psion pixel the same size). A lost context (the phone
 * short of memory, the page in the background) is given up quietly:
 * render() answers null until it is restored, and onRestore() asks for
 * the whole picture again.
 */
(function (root) {
  "use strict";

  const VS = `#version 300 es
in vec2 pos;
out vec2 vTexCoord;
void main() {
  vTexCoord = vec2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);
  gl_Position = vec4(pos, 0.0, 1.0);
}`;

  /*
     Hyllian's xBR-vertex code and texel mapping

     Copyright (C) 2011/2016 Hyllian - sergiogdb@gmail.com

     Permission is hereby granted, free of charge, to any person obtaining a copy
     of this software and associated documentation files (the "Software"), to deal
     in the Software without restriction, including without limitation the rights
     to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
     copies of the Software, and to permit persons to whom the Software is
     furnished to do so, subject to the following conditions:

     The above copyright notice and this permission notice shall be included in
     all copies or substantial portions of the Software.

     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
     IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
     FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
     AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
     LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
     OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
     THE SOFTWARE.

     This shader also uses code and/or concepts from xBRZ as it appears
     in the Desmume source code. The license for which is as follows:

     ****************************************************************************
     * This file is part of the HqMAME project. It is distributed under         *
     * GNU General Public License: http://www.gnu.org/licenses/gpl-3.0          *
     * Copyright (C) Zenju (zenju AT gmx DOT de) - All Rights Reserved          *
     *                                                                          *
     * Additionally and as a special exception, the author gives permission     *
     * to link the code of this program with the MAME library (or with modified *
     * versions of MAME that use the same license as MAME), and distribute      *
     * linked combinations including the two. You must obey the GNU General     *
     * Public License in all respects for all of the code used other than MAME. *
     * If you modify this file, you may extend this exception to your version   *
     * of the file, but you are not obligated to do so. If you do not wish to   *
     * do so, delete this exception statement from your version.                *
     ****************************************************************************

     Ported to GLSL ES 3.00 for PsiCast (from libretro/glsl-shaders, commit
     cba01a48, xbrz/shaders/xbrz-freescale.glsl), with the photo guard and the
     sharp bilinear path added: GPL-3.0.
  */
  const FS = `#version 300 es
precision highp float;
uniform sampler2D Source;
uniform vec2 SourceSize;
uniform vec2 OutputSize;
uniform int sharp;
in vec2 vTexCoord;
out vec4 FragColor;

#define BLEND_NONE 0
#define BLEND_NORMAL 1
#define BLEND_DOMINANT 2
#define LUMINANCE_WEIGHT 1.0
#define EQUAL_COLOR_TOLERANCE 30.0/255.0
#define STEEP_DIRECTION_THRESHOLD 2.2
#define DOMINANT_DIRECTION_THRESHOLD 3.6

float DistYCbCr(vec3 pixA, vec3 pixB) {
  const vec3 w = vec3(0.2627, 0.6780, 0.0593);
  const float scaleB = 0.5 / (1.0 - w.b);
  const float scaleR = 0.5 / (1.0 - w.r);
  vec3 diff = pixA - pixB;
  float Y = dot(diff.rgb, w);
  float Cb = scaleB * (diff.b - Y);
  float Cr = scaleR * (diff.r - Y);
  return sqrt(((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr));
}
bool IsPixEqual(const vec3 pixA, const vec3 pixB) { return (DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE); }
float get_left_ratio(vec2 center, vec2 origin, vec2 direction, vec2 scale) {
  vec2 P0 = center - origin;
  vec2 proj = direction * (dot(P0, direction) / dot(direction, direction));
  vec2 distv = P0 - proj;
  vec2 orth = vec2(-direction.y, direction.x);
  float side = sign(dot(P0, orth));
  float v = side * length(distv * scale);
  return smoothstep(-sqrt(2.0)/2.0, sqrt(2.0)/2.0, v);
}
#define eq(a,b)  (a == b)
#define neq(a,b) (a != b)
#define T(x,y) texture(Source, coord + px * vec2(x, y))
#define P(x,y) T(x,y).rgb

// sharp bilinear: a whole-number blow-up with hard edges, then smoothly to the size
vec4 sharpBilinear() {
  vec2 prescale = max(floor(OutputSize / SourceSize), vec2(1.0));
  vec2 texel = vTexCoord * SourceSize;
  vec2 tf = floor(texel);
  vec2 s = fract(texel);
  vec2 region = 0.5 - 0.5 / prescale;
  vec2 cd = s - 0.5;
  vec2 f = (cd - clamp(cd, -region, region)) * prescale + 0.5;
  return vec4(texture(Source, (tf + f) / SourceSize).rgb, 1.0);
}
// a grey (the alpha: grey * 17) that is not one of EIKON's four
bool photo(float a) { float g = a * 15.0; return abs(g - 5.0 * floor(g / 5.0 + 0.5)) > 0.5; }

// a photo's dither dots smoothed away (0.22): the 5x5 Psion pixels about
// this point, each weighted by its distance and by how near its own
// neighbourhood's brightness (a 2x2 mean, one linear tap) is to this
// point's - a dot is a grey or two from its neighbours and blends in, an
// edge in the picture is many and stays - then a light unsharp mask (the
// same sum, wider) so it is not soft
vec4 dedither(float span) {
  vec2 texel = vTexCoord * SourceSize - 0.5;
  vec2 base = floor(texel);
  vec2 f = texel - base;
  float ref = texture(Source, (base + 1.0) / SourceSize).a;   // (the 2x2 nearest this point)
  vec4 s1 = vec4(0.0), s2 = vec4(0.0);
  float w1 = 0.0, w2 = 0.0;
  for (int j = -2; j <= 2; j++)
    for (int i = -2; i <= 2; i++) {
      vec2 o = vec2(float(i), float(j));
      vec2 t = base + o;
      vec4 v = texture(Source, (t + 0.5) / SourceSize);
      float m = texture(Source, (t + 1.0) / SourceSize).a;   // (its 2x2 mean, by the linear filter)
      float d = (m - ref) * 15.0;
      float r = exp(-d * d / (2.0 * span * span));            // (range: span greys)
      vec2 dd = o - f;
      float q = dot(dd, dd);
      float g1 = exp(-q / 1.3) * r, g2 = exp(-q / 4.0) * r;
      s1 += v * g1; w1 += g1;
      s2 += v * g2; w2 += g2;
    }
  vec4 a = s1 / max(w1, 1e-4), b = s2 / max(w2, 1e-4);
  // the Psion pixel here far from what is about it (text on a dithered
  // fill, a line through a picture): a detail, not a dot - kept sharp
  float here = texture(Source, (floor(vTexCoord * SourceSize) + 0.5) / SourceSize).a;
  if (abs(here - a.a) * 15.0 > max(6.0, span * 2.25)) return sharpBilinear();
  // (the sharpen only for photos in the in-between greys: EIKON's dithered
  // fills - a button's face, a title bar - stay one flat grey to their edges)
  return vec4(clamp(a.rgb + (span < 2.0 ? 0.45 : 0.0) * (a.rgb - b.rgb), 0.0, 1.0), 1.0);
}
bool photo9(vec2 coord, vec2 px) {
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++)
      if (photo(texture(Source, coord + px * vec2(float(i), float(j))).a)) return true;
  return false;
}
// a picture dithered in EIKON's own four greys (0.22: PsiTris's): the 5x5
// about this pixel has steps of one of them (5) both across and down, many
// of each, or many back and forth one way - text is black on white (15),
// a bevel steps one way only, and once
bool dither4(vec2 coord, vec2 px) {
  float g[25];
  vec3 c[25];
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < 5; i++) {
      vec4 t = texture(Source, coord + px * vec2(float(i - 2), float(j - 2)));
      g[j * 5 + i] = floor(t.a * 15.0 + 0.5);
      c[j * 5 + i] = t.rgb;
    }
  // (a step counts only where the colour shown steps too: a face the box
  // and line redraw has already made flat keeps its greys but not its dots)
  // (and back and forth - a, b, a - many times one way: PsiTris's pictures
  // shade in stripes, a column of each grey in turn; a bevel never turns back)
  int h = 0, v = 0, ah = 0, av = 0;
  for (int j = 0; j < 5; j++)
    for (int i = 0; i < 5; i++) {
      int k = j * 5 + i;
      bool sh = i < 4 && abs(g[k] - g[k + 1]) == 5.0 && any(notEqual(c[k], c[k + 1]));
      bool sv = j < 4 && abs(g[k] - g[k + 5]) == 5.0 && any(notEqual(c[k], c[k + 5]));
      if (sh) { h++; if (i < 3 && g[k + 2] == g[k]) ah++; }
      if (sv) { v++; if (j < 3 && g[k + 10] == g[k]) av++; }
    }
  return (h >= 5 && v >= 5) || ah >= 8 || av >= 8;
}

void main() {
  vec2 px = 1.0 / SourceSize;
  vec2 scale = OutputSize.xy * px;
  vec2 pos = fract(vTexCoord * SourceSize.xy) - vec2(0.5, 0.5);
  vec2 coord = vTexCoord - pos * px;
  float kind = texture(Mask, vec2(coord.x, 1.0 - coord.y)).r;   // (the mask pass's: drawn upside down)
  if (kind > 0.75) { FragColor = dedither(1.5); return; }
  if (kind > 0.25) { FragColor = dedither(4.0); return; }
  if (sharp != 0) { FragColor = sharpBilinear(); return; }

  vec4 tA = T(-1.,-1.), tB = T(0.,-1.), tC = T(1.,-1.), tD = T(-1.,0.), tE = T(0.,0.), tF = T(1.,0.),
       tG = T(-1.,1.), tH = T(0.,1.), tI = T(1.,1.);
  vec3 A = tA.rgb, B = tB.rgb, C = tC.rgb, D = tD.rgb, E = tE.rgb, F = tF.rgb, G = tG.rgb, H = tH.rgb, I = tI.rgb;

  ivec4 blendResult = ivec4(BLEND_NONE,BLEND_NONE,BLEND_NONE,BLEND_NONE);
  if (!((eq(E,F) && eq(H,I)) || (eq(E,H) && eq(F,I)))) {
    float dist_H_F = DistYCbCr(G, E) + DistYCbCr(E, C) + DistYCbCr(P(0.,2.), I) + DistYCbCr(I, P(2.,0.)) + (4.0 * DistYCbCr(H, F));
    float dist_E_I = DistYCbCr(D, H) + DistYCbCr(H, P(1.,2.)) + DistYCbCr(B, F) + DistYCbCr(F, P(2.,1.)) + (4.0 * DistYCbCr(E, I));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_H_F) < dist_E_I;
    blendResult.z = ((dist_H_F < dist_E_I) && neq(E,F) && neq(E,H)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }
  if (!((eq(D,E) && eq(G,H)) || (eq(D,G) && eq(E,H)))) {
    float dist_G_E = DistYCbCr(P(-2.,1.), D) + DistYCbCr(D, B) + DistYCbCr(P(-1.,2.), H) + DistYCbCr(H, F) + (4.0 * DistYCbCr(G, E));
    float dist_D_H = DistYCbCr(P(-2.,0.), G) + DistYCbCr(G, P(0.,2.)) + DistYCbCr(A, E) + DistYCbCr(E, I) + (4.0 * DistYCbCr(D, H));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_H) < dist_G_E;
    blendResult.w = ((dist_G_E > dist_D_H) && neq(E,D) && neq(E,H)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }
  if (!((eq(B,C) && eq(E,F)) || (eq(B,E) && eq(C,F)))) {
    float dist_E_C = DistYCbCr(D, B) + DistYCbCr(B, P(1.,-2.)) + DistYCbCr(H, F) + DistYCbCr(F, P(2.,-1.)) + (4.0 * DistYCbCr(E, C));
    float dist_B_F = DistYCbCr(A, E) + DistYCbCr(E, I) + DistYCbCr(P(0.,-2.), C) + DistYCbCr(C, P(2.,0.)) + (4.0 * DistYCbCr(B, F));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_B_F) < dist_E_C;
    blendResult.y = ((dist_E_C > dist_B_F) && neq(E,B) && neq(E,F)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }
  if (!((eq(A,B) && eq(D,E)) || (eq(A,D) && eq(B,E)))) {
    float dist_D_B = DistYCbCr(P(-2.,0.), A) + DistYCbCr(A, P(0.,-2.)) + DistYCbCr(G, E) + DistYCbCr(E, C) + (4.0 * DistYCbCr(D, B));
    float dist_A_E = DistYCbCr(P(-2.,-1.), D) + DistYCbCr(D, H) + DistYCbCr(P(-1.,-2.), B) + DistYCbCr(B, F) + (4.0 * DistYCbCr(A, E));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_B) < dist_A_E;
    blendResult.x = ((dist_D_B < dist_A_E) && neq(E,D) && neq(E,B)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }
  vec3 res = E;
  if (blendResult.z != BLEND_NONE) {
    float dist_F_G = DistYCbCr(F, G);
    float dist_H_C = DistYCbCr(H, C);
    bool doLineBlend = (blendResult.z == BLEND_DOMINANT ||
                !((blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) || (blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||
                  (IsPixEqual(G, H) && IsPixEqual(H, I) && IsPixEqual(I, F) && IsPixEqual(F, C) && !IsPixEqual(E, I))));
    vec2 origin = vec2(0.0, 1.0 / sqrt(2.0));
    vec2 direction = vec2(1.0, -1.0);
    if (doLineBlend) {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_F_G <= dist_H_C) && neq(E,G) && neq(D,G);
      bool haveSteepLine = (STEEP_DIRECTION_THRESHOLD * dist_H_C <= dist_F_G) && neq(E,C) && neq(B,C);
      origin = haveShallowLine? vec2(0.0, 0.25) : vec2(0.0, 0.5);
      direction.x += haveShallowLine? 1.0: 0.0;
      direction.y -= haveSteepLine? 1.0: 0.0;
    }
    vec3 blendPix = mix(H,F, step(DistYCbCr(E, F), DistYCbCr(E, H)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }
  if (blendResult.w != BLEND_NONE) {
    float dist_H_A = DistYCbCr(H, A);
    float dist_D_I = DistYCbCr(D, I);
    bool doLineBlend = (blendResult.w == BLEND_DOMINANT ||
                !((blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) || (blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||
                  (IsPixEqual(A, D) && IsPixEqual(D, G) && IsPixEqual(G, H) && IsPixEqual(H, I) && !IsPixEqual(E, G))));
    vec2 origin = vec2(-1.0 / sqrt(2.0), 0.0);
    vec2 direction = vec2(1.0, 1.0);
    if (doLineBlend) {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_H_A <= dist_D_I) && neq(E,A) && neq(B,A);
      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_D_I <= dist_H_A) && neq(E,I) && neq(F,I);
      origin = haveShallowLine? vec2(-0.25, 0.0) : vec2(-0.5, 0.0);
      direction.y += haveShallowLine? 1.0: 0.0;
      direction.x += haveSteepLine? 1.0: 0.0;
    }
    vec3 blendPix = mix(H,D, step(DistYCbCr(E, D), DistYCbCr(E, H)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }
  if (blendResult.y != BLEND_NONE) {
    float dist_B_I = DistYCbCr(B, I);
    float dist_F_A = DistYCbCr(F, A);
    bool doLineBlend = (blendResult.y == BLEND_DOMINANT ||
                !((blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) || (blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||
                  (IsPixEqual(I, F) && IsPixEqual(F, C) && IsPixEqual(C, B) && IsPixEqual(B, A) && !IsPixEqual(E, C))));
    vec2 origin = vec2(1.0 / sqrt(2.0), 0.0);
    vec2 direction = vec2(-1.0, -1.0);
    if (doLineBlend) {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_B_I <= dist_F_A) && neq(E,I) && neq(H,I);
      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_F_A <= dist_B_I) && neq(E,A) && neq(D,A);
      origin = haveShallowLine? vec2(0.25, 0.0) : vec2(0.5, 0.0);
      direction.y -= haveShallowLine? 1.0: 0.0;
      direction.x -= haveSteepLine? 1.0: 0.0;
    }
    vec3 blendPix = mix(F,B, step(DistYCbCr(E, B), DistYCbCr(E, F)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }
  if (blendResult.x != BLEND_NONE) {
    float dist_D_C = DistYCbCr(D, C);
    float dist_B_G = DistYCbCr(B, G);
    bool doLineBlend = (blendResult.x == BLEND_DOMINANT ||
                !((blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) || (blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||
                  (IsPixEqual(C, B) && IsPixEqual(B, A) && IsPixEqual(A, D) && IsPixEqual(D, G) && !IsPixEqual(E, A))));
    vec2 origin = vec2(0.0, -1.0 / sqrt(2.0));
    vec2 direction = vec2(-1.0, 1.0);
    if (doLineBlend) {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_D_C <= dist_B_G) && neq(E,C) && neq(F,C);
      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_B_G <= dist_D_C) && neq(E,G) && neq(H,G);
      origin = haveShallowLine? vec2(0.0, -0.25) : vec2(0.0, -0.5);
      direction.x -= haveShallowLine? 1.0: 0.0;
      direction.y += haveSteepLine? 1.0: 0.0;
    }
    vec3 blendPix = mix(D,B, step(DistYCbCr(E, B), DistYCbCr(E, D)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }
  FragColor = vec4(res, 1.0);
}`;

  // (0.22) the mask pass: once a Psion pixel, not once a phone pixel, what
  // kind it is - 1 a photo in the in-between greys, 0.5 a picture dithered
  // in EIKON's four, 0 neither - for the main pass to look up
  const CUT = FS.indexOf("void main() {");
  const FS_MAIN = FS.slice(0, CUT) + "uniform sampler2D Mask;\n" + FS.slice(CUT);
  const FS_MASK = FS.slice(0, CUT) + `void main() {
  vec2 px = 1.0 / SourceSize;
  vec2 coord = (floor(vTexCoord * SourceSize) + 0.5) * px;
  FragColor = vec4(photo9(coord, px) ? 1.0 : dither4(coord, px) ? 0.5 : 0.0, 0.0, 0.0, 1.0);
}`;

  function create(onRestore) {
    const cv = document.createElement("canvas");
    const attrs = { alpha: false, antialias: false, depth: false, stencil: false, premultipliedAlpha: false, preserveDrawingBuffer: false };
    let gl = cv.getContext("webgl2", attrs);
    if (!gl) return null;
    let prog = null, progM = null, tex = null, mask = null, fbo = null, buf = null, loc = null, locM = null;
    let tw = 0, th = 0, lost = false, maskStale = true;
    function init() {
      const sh = (type, src) => {
        const s = gl.createShader(type);
        gl.shaderSource(s, src); gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS) && !gl.isContextLost()) throw new Error("xbrz: " + gl.getShaderInfoLog(s));
        return s;
      };
      const link = fs => {
        const p = gl.createProgram();
        gl.attachShader(p, sh(gl.VERTEX_SHADER, VS));
        gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fs));
        gl.bindAttribLocation(p, 0, "pos");
        gl.linkProgram(p);
        if (!gl.getProgramParameter(p, gl.LINK_STATUS) && !gl.isContextLost()) throw new Error("xbrz: " + gl.getProgramInfoLog(p));
        return p;
      };
      progM = link(FS_MASK);
      gl.useProgram(progM);
      locM = { ss: gl.getUniformLocation(progM, "SourceSize") };
      gl.uniform1i(gl.getUniformLocation(progM, "Source"), 0);
      prog = link(FS_MAIN);
      gl.useProgram(prog);
      loc = { src: gl.getUniformLocation(prog, "Source"), ss: gl.getUniformLocation(prog, "SourceSize"),
              os: gl.getUniformLocation(prog, "OutputSize"), sharp: gl.getUniformLocation(prog, "sharp") };
      gl.uniform1i(gl.getUniformLocation(prog, "Mask"), 1);
      mask = gl.createTexture();
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, mask);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      fbo = gl.createFramebuffer();
      buf = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, buf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
      tex = gl.createTexture();
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      // (linear: xBRZ samples at the texels' centres, so it sees them exact; sharp bilinear needs it)
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.uniform1i(loc.src, 0);
      tw = th = 0;
    }
    cv.addEventListener("webglcontextlost", e => { e.preventDefault(); lost = true; }, false);
    cv.addEventListener("webglcontextrestored", () => {
      try { init(); lost = false; if (onRestore) onRestore(); } catch (e) { lost = true; }
    }, false);
    init();
    return {
      canvas: cv,
      get lost() { return lost || gl.isContextLost(); },
      upload(data, W, H, rects) {
        if (this.lost) return;
        const bytes = new Uint8Array(data.buffer, data.byteOffset, W * H * 4);
        gl.bindTexture(gl.TEXTURE_2D, tex);
        if (tw !== W || th !== H || !rects) {
          gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0); gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0); gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 0);
          if (tw !== W || th !== H) {
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, W, H, 0, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
            gl.activeTexture(gl.TEXTURE1);
            gl.bindTexture(gl.TEXTURE_2D, mask);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, W, H, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
            gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
            gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, mask, 0);
            gl.bindFramebuffer(gl.FRAMEBUFFER, null);
            gl.activeTexture(gl.TEXTURE0);
          } else gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, W, H, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
          tw = W; th = H;
          maskStale = true;
          return;
        }
        // the whole rows the changes are in, from their own offset: not
        // UNPACK_ROW_LENGTH and the SKIPs, which Safari on the iPhone got
        // wrong (pieces of earlier screens stayed: Dan, 24 Oct 2026)
        let y0 = H, y1 = 0;
        for (const r of rects) { y0 = Math.min(y0, r.y); y1 = Math.max(y1, r.y + r.h); }
        if (y1 > y0) maskStale = true;
        if (y1 > y0)
          gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, y0, W, y1 - y0, gl.RGBA, gl.UNSIGNED_BYTE, bytes.subarray(y0 * W * 4, y1 * W * 4));
      },
      render(dw, dh, sharp) {
        if (this.lost || !tw) return null;
        if (cv.width !== dw || cv.height !== dh) { cv.width = dw; cv.height = dh; }
        if (maskStale) {
          gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
          gl.viewport(0, 0, tw, th);
          gl.useProgram(progM);
          gl.uniform2f(locM.ss, tw, th);
          gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          maskStale = false;
        }
        gl.viewport(0, 0, dw, dh);
        gl.useProgram(prog);
        gl.uniform2f(loc.ss, tw, th);
        gl.uniform2f(loc.os, dw, dh);
        gl.uniform1i(loc.sharp, sharp ? 1 : 0);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        return cv;
      },
      // (the test's: lose the context on purpose, and get it back)
      debugLose() { const x = gl.getExtension("WEBGL_lose_context"); if (x) { x.loseContext(); return x; } return null; }
    };
  }

  const api = { create };
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.Xbrz = api;
})(typeof self !== "undefined" ? self : this);
