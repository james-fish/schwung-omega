/* Omega: module-drawn knob-cell widgets (Schwung `drawCell`, MODULES.md "Custom widgets").
 *
 * Every picture is what the knob does to the SOUND, drawn in Quark's vocabulary (dotted floor,
 * 3-px position tick, solid stroke = signal, dither = fading energy). (0,0) is the cell box;
 * ctx.width/height its size (it changes: size everything from them). Values arrive, never read.
 *
 *  custom:okick   Model cell: one 32x17 pixel icon per engine (KICK_ART bitmaps).
 *  custom:odive   Pitch/Length/Curve: the pitch dive (Hz over time, log) over the amp envelope.
 *  custom:ofm     FM2: the phase-modulated cycle (Ratio cell) / its sideband spectrum (Index cell).
 *  custom:oalgo   FM4 algorithm: the four operators and their wires, feedback as a loop.
 *  custom:omorph  WTR/ANA/USR morph: the oscillator shape at that morph position.
 *  custom:odrum   PHY beater: a mallet over a drum - felt ball (soft) to hard tip.
 *  custom:oripple PHY shell/tension/damping: the head's ringing, damped standing wave.
 *  custom:odrive  Drive: the transfer curve, bending flat as drive rises.
 *  custom:ocrush  Crush/bit depth: a sine on the staircase it is quantised to.
 *  custom:openv   Pitch-env amount/curve: the pitch drop.
 *  custom:oclick  Transient: the click burst - level, decay and tone (spike density).
 *  custom:olpf    A filter cutoff: its low-pass response.
 *  custom:ofx     FX type/amount/tone: per-type transfer curve and a sine through it.
 *  custom:oband   Taps HPF + LPF: the band the rumble lives in.
 *  custom:otaps   TAP1..4 (+ Decay): the kick and its four ghost kicks on the 16th grid.
 *  custom:oscale  GEN scale: the twelve pitch classes, lit where the scale lives.
 *  custom:ogrid   Note length + swing: the step grid the GEN plays, swung.
 *  custom:oneck   GEN pattern: the "necklace" - every step a bead at its pitch, accents solid,
 *                 rests a knot on the thread, the playhead live (extra: grv_gview, grv_gstep).
 *  custom:opluck  GEN decay: the pluck + body envelope of the current voice.
 *  custom:ogwave  SEQ voice + its two knobs: one picture per voice (see drawGwave).
 *  custom:odice   SEQ ratchet/slide/note-rnd/loop: sixteen dice as they fall, repeating on LOOP.
 *  custom:olfo    LFO speed/amount: a scrolling triangle.
 *  custom:oroom   Reverb mix/decay/tone/type: the room (shed, bunker, tunnel, dirty shed) with a
 *                 ping bouncing in it, and the tail it leaves.
 *  custom:opump   Duck/release/slew: the sidechain pump under four kicks, a dot riding it.
 *  custom:oxover  Duck frequency: the band below it that gets ducked.
 *  custom:odj     DJ filter + reso: LP <- neutral -> HP.
 *  custom:oknee   Compressor drive: the transfer knee and the makeup.
 */
(function () {
  /* @@COMMON@@ */

  var V = O.val, L = O.line;
  function W_(ctx) { return ctx.width; }
  function H_(ctx) { return ctx.height; }
  function now(o) { return typeof o.nowMs === "number" ? o.nowMs : 0; }

  /* ---------------------------------------------------------------- kick */
  /* Model cell: one 32x17 1-bit icon per engine (the host prints the name). Drawn 1:1 (or an
   * integer multiple) centred in the cell, down to 30x15 where only the empty border is cut; a
   * smaller cell ORs source blocks so thin lines survive. */
  var KICK_W = 32, KICK_H = 17;
  var KICK_ART = [
    /* FM2 */ [0x00000000, 0x1ffe0000, 0x10020000, 0x10020000, 0x173a0000, 0x152be000, 0x152a1000, 0x152a0800, 0x1dee0400, 0x10027ffc, 0x1ffe4004, 0x00005ce4, 0x000054a4, 0x000054a4, 0x000077bc, 0x00007ffc, 0x00000000],
    /* FM4 */ [0x00000000, 0x0f83e000, 0x10441000, 0x107c1e00, 0x10441200, 0x0f83e200, 0x02008220, 0x02008210, 0x03ff83f8, 0x02008210, 0x02008220, 0x0f83e200, 0x10441200, 0x107c1e00, 0x10441000, 0x0f83e000, 0x00000000],
    /* WTR */ [0x00000000, 0x00000000, 0x0700007c, 0x08800000, 0x10400000, 0x1020007c, 0x20200100, 0x00100100, 0x0008027c, 0x00080400, 0x00040400, 0x0004087c, 0x00021000, 0x00012000, 0x0000c07c, 0x00000000, 0x00000000],
    /* PHY */ [0x00000000, 0x00800000, 0x00e00000, 0x00700400, 0x001c0400, 0x000fc800, 0x0003c300, 0x0001c400, 0x00000000, 0x01ffffc0, 0x03ffffe0, 0x02000020, 0x03ffffe0, 0x02200420, 0x02200420, 0x00700e00, 0x00000000],
    /* HRD */ [0x00000000, 0x01000000, 0x01000000, 0x01000000, 0x03100000, 0x02900000, 0x02920000, 0x02b24000, 0x04ae4a00, 0x7cadb7fe, 0x00ada400, 0x00690000, 0x00480000, 0x00400000, 0x00400000, 0x00000000, 0x00000000],
    /* DIG */ [0x00000000, 0x00800010, 0x00000800, 0x407e0000, 0x00420000, 0x1f4203c2, 0x11420240, 0x11438240, 0x11408270, 0x11408210, 0x1140821e, 0x71c08200, 0x00008200, 0x0000fe02, 0x00000000, 0x04100040, 0x00000000],
    /* TRS */ [0x00000000, 0x00000000, 0x02000000, 0x02000000, 0x03800000, 0x05800000, 0x05400000, 0x04400000, 0x047c0000, 0x0807f000, 0x08001f80, 0x780000fe, 0x00000000, 0x10410410, 0x10410410, 0x10410410, 0x00000000],
    /* ANA */ [0x00000000, 0x00000000, 0x000ff000, 0x00100800, 0x00100400, 0x00200400, 0x00400200, 0x00400100, 0x00800100, 0x00800080, 0x01000080, 0x02000040, 0x7c00003e, 0x00000000, 0x7ffffffe, 0x00000000, 0x00000000],
    /* USR */ [0x00000000, 0x00000000, 0x1f800000, 0x107fc040, 0x10004140, 0x1fffc140, 0x10004550, 0x10004550, 0x10005554, 0x10005554, 0x10005554, 0x10004550, 0x10004550, 0x10004140, 0x10004140, 0x1fffc040, 0x00000000]
  ];
  function drawKick(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var bm = KICK_ART[O.clamp(Math.round(V(v, "model")), 0, KICK_ART.length - 1)];
    function on(x, y) { return (bm[y] >>> (KICK_W - 1 - x)) & 1; }
    var s = Math.min(W / KICK_W, H / KICK_H), x, y;
    if (W >= KICK_W - 2 && H >= KICK_H - 2) {   /* the 1-px empty border may fall outside */
      var k = Math.max(1, Math.floor(s)), ox = Math.floor((W - KICK_W * k) / 2), oy = Math.floor((H - KICK_H * k) / 2);
      for (y = 0; y < KICK_H; y++) for (x = 0; x < KICK_W; x++) {
        if (!on(x, y)) continue;
        var x1 = x; while (x1 + 1 < KICK_W && on(x1 + 1, y)) x1++;
        ctx.fillRect(ox + x * k, oy + y * k, (x1 - x + 1) * k, k, 1); x = x1;
      }
      return;
    }
    var dw = Math.max(1, Math.round(KICK_W * s)), dh = Math.max(1, Math.round(KICK_H * s));
    var ox2 = Math.floor((W - dw) / 2), oy2 = Math.floor((H - dh) / 2);
    for (y = 0; y < dh; y++) for (x = 0; x < dw; x++) {
      var sx0 = Math.floor(x * KICK_W / dw), sx1 = Math.floor((x + 1) * KICK_W / dw);
      var sy0 = Math.floor(y * KICK_H / dh), sy1 = Math.floor((y + 1) * KICK_H / dh), hit = 0;
      for (var yy = sy0; yy < Math.max(sy1, sy0 + 1) && !hit; yy++)
        for (var xx = sx0; xx < Math.max(sx1, sx0 + 1) && !hit; xx++) hit = on(xx, yy);
      if (hit) O.px(ctx, ox2 + x, oy2 + y);
    }
  }
  function drawDive(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var f0 = V(v, "pitch"), len = V(v, "length"), cv = V(v, "curve");
    var T = O.kickLenS(len), top = 1, bot = H - 2;
    var lo = Math.log(25), hi = Math.log(1300);
    function yOf(f) { return bot - (Math.log(f) - lo) / (hi - lo) * (bot - top); }
    /* amp envelope as dither underneath */
    /* time axis is square-law warped so the fast dive gets room */
    function tOf(x) { var u = x / (W - 1); return T * u * u; }
    for (var x = 0; x < W; x++) {
      var t = tOf(x), a = O.kickAmp(t, len);
      if (a > 0.04) O.dither(ctx, x, bot - Math.round(a * (bot - top) * 0.55), bot, a > 0.5 ? 0.3 : 0.15);
    }
    /* f0 floor */
    O.dotted(ctx, 0, Math.round(yOf(f0)), W);
    /* the dive */
    var lx = null, ly = null;
    for (var x2 = 0; x2 < W; x2++) {
      var tt = tOf(x2), y = Math.round(yOf(O.kickHz(tt, f0, cv)));
      if (lx !== null) L(ctx, lx, ly, x2, y);
      lx = x2; ly = y;
    }
    O.dotted(ctx, 0, H - 1, W);
  }

  /* ----------------------------------------------------------------- FM2 */
  function besselJ(n, x) {
    var s = 0, f = 1;
    for (var k = 0; k < 12; k++) {
      var d = 1; for (var j = 1; j <= k; j++) d *= j; var e = 1; for (var j2 = 1; j2 <= k + n; j2++) e *= j2;
      s += (k & 1 ? -1 : 1) * Math.pow(x / 2, 2 * k + n) / (d * e);
    }
    return s;
  }
  function drawFm(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {}, key = (o.group && o.group.keys && o.group.keys[0]) || "fm_ratio";
    var ratio = 0.5 + 7.5 * V(v, "fm_ratio"), idx = 8 * V(v, "fm_index");
    if (key === "fm_index") {
      /* sidebands at |1 +- k*ratio|, heights |J_k(I)| */
      O.dotted(ctx, 0, H - 1, W);
      for (var k = -6; k <= 6; k++) {
        var f = Math.abs(1 + k * ratio); if (f > 12) continue;
        var x = Math.round(Math.log(1 + f) / Math.log(13) * (W - 2)) + 1;
        var a = Math.min(1, Math.abs(besselJ(Math.abs(k), idx)));
        var h = Math.round(a * (H - 2));
        if (h > 0) ctx.fillRect(x, H - 1 - h, 1, h, 1);
      }
      return;
    }
    var w2 = Math.round(V(v, "op2_wave"));
    O.dotted(ctx, 0, Math.floor((H - 1) / 2), W);
    O.plot(ctx, function (t) {
      var m = Math.sin(2 * Math.PI * t * ratio);
      if (w2 === 1) m = Math.sin(m * 2.5); else if (w2 === 2) m = 2 / Math.PI * Math.asin(m);
      return Math.sin(2 * Math.PI * t + Math.max(0.6, idx) * m);
    }, 0, 0, W, H, 1);
  }
  /* ----------------------------------------------------------------- FM4 */
  function drawAlgo(ctx, o) {
    var v = o.values || {};
    O.algoDiagram(ctx, V(v, "fm4_algo"), V(v, "fm4_feedback"), 1, 1, W_(ctx) - 6, H_(ctx) - 2, 3);
  }

  /* ----------------------------------------------------- shapes & curves */
  function cellKey(o) { return (o.group && o.group.keys && o.group.keys[0]) || ""; }
  function drawMorph(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = O.val(o.values || {}, cellKey(o), 0.5);
    O.dotted(ctx, 0, Math.floor((H - 1) / 2), W);
    O.plot(ctx, function (t) { return O.morphWave(v, t); }, 0, 0, W, H - 3, 1.5);
    O.dotted(ctx, 0, H - 1, W);
    O.tick(ctx, 1 + v * (W - 3), H - 1);
  }
  function drawDrum(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var beat = V(v, "phy_beater"), shell = V(v, "phy_shell"), tens = V(v, "phy_headtens");
    var dw = Math.round(W * (0.45 + 0.4 * shell)), dx = W - dw - 1, top = Math.round(H * 0.45), bot = H - 1;
    /* shell */
    ctx.fillRect(dx, top, 1, bot - top + 1, 1); ctx.fillRect(dx + dw - 1, top, 1, bot - top + 1, 1);
    O.dotted(ctx, dx, bot, dw);
    /* head: sags at low tension */
    var sag = Math.round((1 - tens) * 3);
    for (var x = 0; x < dw; x++) {
      var u = x / (dw - 1) * 2 - 1;
      O.px(ctx, dx + x, top + Math.round(sag * (1 - u * u)));
    }
    /* mallet: soft felt ball (big) -> hard tip (small) */
    var r = Math.max(1, Math.round((1 - beat) * 3)), cx = dx + Math.round(dw / 2), cy = top - r - 2 + sag;
    L(ctx, cx + r, cy - 1, Math.max(1, cx - Math.round(W * 0.35)), 0);
    if (typeof ctx.fillCircle === "function" && r > 1) ctx.fillCircle(cx, cy, r, 1);
    else ctx.fillRect(cx - r, cy - r, 2 * r + 1, 2 * r + 1, 1);
  }
  function drawRipple(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var shell = V(v, "phy_shell"), tens = V(v, "phy_headtens"), damp = V(v, "phy_damping");
    var cyc = 2 + tens * 9, dec = 0.6 + damp * 5, amp = 0.55 + 0.45 * shell, mid = (H - 1) / 2;
    O.dotted(ctx, 0, Math.round(mid), W);
    var lx = null, ly = null;
    for (var x = 0; x < W; x++) {
      var t = x / (W - 1), e = Math.exp(-t * dec) * amp;
      var s = Math.sin(2 * Math.PI * cyc * t) * 0.8 + Math.sin(2 * Math.PI * cyc * 1.59 * t) * 0.2 * shell;
      var y = Math.round(mid - s * e * mid);
      if (lx !== null) L(ctx, lx, ly, x, y);
      lx = x; ly = y;
      if (x % 3 === 0) { O.px(ctx, x, Math.round(mid - e * mid)); O.px(ctx, x, Math.round(mid + e * mid)); }
    }
  }
  function drawSub(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var lvl = V(v, "ana_sublvl", 0.7), dec = V(v, "ana_subdec", 0.6), mid = (H - 1) / 2, tau = 0.08 + dec * dec * 0.9;
    O.dotted(ctx, 0, Math.round(mid), W);
    var lx = null, ly = null;
    for (var x = 0; x < W; x++) {
      var t = x / (W - 1) * 0.6, e = lvl * Math.exp(-t / tau);
      var y = Math.round(mid - Math.sin(2 * Math.PI * 5 * x / (W - 1)) * e * mid);
      if (lx !== null) L(ctx, lx, ly, x, y);
      lx = x; ly = y;
      if (x % 3 === 0) O.px(ctx, x, Math.round(mid + e * mid));
    }
  }
  function drawDrive(ctx, o) {
    var W = W_(ctx), H = H_(ctx), amt = O.val(o.values || {}, cellKey(o), 0);
    var s = Math.min(H, W) - 1, x0 = Math.floor((W - s) / 2);
    O.dotted(ctx, x0, Math.floor(s / 2), s + 1);
    O.vdotted(ctx, x0 + Math.floor(s / 2), 0, s + 1);
    var lx = null, ly = null;
    for (var i = 0; i <= s; i++) {
      var x = i / s * 2 - 1, y = Math.round((0.5 - 0.5 * O.driveCurve(x, amt)) * s);
      if (lx !== null) L(ctx, x0 + lx, ly, x0 + i, y);
      lx = i; ly = y;
    }
  }
  function drawCrush(ctx, o) {
    var W = W_(ctx), H = H_(ctx), k = cellKey(o), v = O.val(o.values || {}, k, 0.5);
    var bits = k === "dig_bitdepth" ? 1.5 + v * 6 : 7 - v * 5.5, steps = Math.max(2, Math.round(Math.pow(2, bits) / 2));
    O.dotted(ctx, 0, Math.floor((H - 1) / 2), W);
    var lx = null, ly = null;
    for (var x = 0; x < W; x++) {
      var s = Math.round(Math.sin(2 * Math.PI * x / (W - 1)) * steps) / steps;
      var y = Math.round((0.5 - 0.5 * s * 0.95) * (H - 1));
      if (lx !== null) { if (y !== ly) { L(ctx, lx, ly, x, ly); L(ctx, x, ly, x, y); } else L(ctx, lx, ly, x, y); }
      lx = x; ly = y;
    }
  }
  function drawPenv(ctx, o) {
    var W = W_(ctx), H = H_(ctx), k = cellKey(o), v = O.val(o.values || {}, k, 0.5);
    var depth = k === "trs_curve" ? 1 : 0.15 + 0.85 * v, pw = k === "trs_curve" ? 0.3 + v * 6 : 3;
    O.dotted(ctx, 0, H - 1, W);
    var lx = null, ly = null;
    for (var x = 0; x < W; x++) {
      var t = x / (W - 1), y = Math.round(H - 1 - Math.pow(1 - t, pw) * depth * (H - 2));
      if (lx !== null) L(ctx, lx, ly, x, y);
      lx = x; ly = y;
    }
  }
  function drawClick(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {}, k = cellKey(o);
    var lvl, dec, tone;
    if (k === "trs_tone") { lvl = 0.9; dec = V(v, "trs_tdec"); tone = V(v, "trs_tone"); }
    else { lvl = 0.25 + 0.75 * V(v, "attack"); dec = V(v, "trs_dec"); tone = V(v, "trs_tne"); }
    var mid = Math.floor((H - 1) / 2), tau = 0.05 + dec * 0.6, step = Math.max(1, Math.round(4 - tone * 3));
    O.dotted(ctx, 0, mid, W);
    for (var x = 0; x < W; x += step) {
      var t = x / (W - 1), e = lvl * Math.exp(-t / tau);
      var r = O.hash(x, 3) * 2 - 1, h = Math.round(e * mid * (0.35 + 0.65 * Math.abs(r)));
      if (h < 1) continue;
      if (r >= 0) ctx.fillRect(x, mid - h, 1, h, 1); else ctx.fillRect(x, mid + 1, 1, h, 1);
    }
  }
  function drawLpf(ctx, o) {
    var W = W_(ctx), H = H_(ctx), k = cellKey(o), v = O.val(o.values || {}, k, 0.5);
    var fc = O.logHz(v, 30, 20000), res = k === "grv_genfilt" ? 0.62 : 0.2;
    O.dotted(ctx, 0, H - 1, W);
    O.response(ctx, function (f) { var m = O.svfMag("lp", f, fc, res); return k === "grv_genfilt" ? m * m : m; }, 0, 0, W, H - 1);
  }
  function drawFx(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var type = V(v, "fx_type"), amt = V(v, "fx_amt"), tone = V(v, "fx_tone");
    var s = H - 1, x0 = 0;
    O.dotted(ctx, x0, Math.floor(s / 2), s + 1);
    var lx = null, ly = null;
    for (var i = 0; i <= s; i++) {
      var y = Math.round((0.5 - 0.5 * O.clamp(O.fxCurve(type, i / s * 2 - 1, amt), -1, 1)) * s);
      if (lx !== null) L(ctx, x0 + lx, ly, x0 + i, y);
      lx = i; ly = y;
    }
    var x1 = s + 4, w = W - x1 - 6;
    O.plot(ctx, function (t) { return O.fxCurve(type, Math.sin(2 * Math.PI * t) * 0.9, amt); }, x1, 0, w, H, 2);
    /* tone: a tilt bar on the right edge (dark low, bright high) */
    var tx = W - 3, ty = Math.round((1 - tone) * (H - 3));
    O.vdotted(ctx, tx + 1, 0, H);
    ctx.fillRect(tx, ty, 3, 3, 1);
  }
  function drawBand(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var hpv = V(v, "grv_hpf"), hpf = hpv <= 0.001 ? 0 : O.logHz(hpv, 20, 2000), lpf = O.logHz(V(v, "grv_color"), 30, 20000);
    function mag(f) { var m = O.svfMag("lp", f, lpf, 0.15); if (hpf > 0) { var r = f / hpf; m *= r / Math.sqrt(1 + r * r); } return m * m; }
    O.dotted(ctx, 0, H - 1, W);
    for (var x = 0; x < W; x++) {
      var f = 20 * Math.pow(1000, x / (W - 1)), m = mag(f);
      if (m > 0.5) O.dither(ctx, x, H - 1 - Math.round((H - 2) * 0.45), H - 1, 0.2);
    }
    O.response(ctx, mag, 0, 0, W, H - 1);
  }
  function drawTaps(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var len = V(v, "grv_length"), tau = 0.08 + len * len * 1.5, spq = 0.125;   /* 16th @120 */
    var gap = (W - 3) / 4.6, base = H - 1;
    O.dotted(ctx, 0, base, W);
    /* the kick itself */
    ctx.fillRect(0, 0, 2, base, 1);
    /* decay envelope */
    for (var x = 2; x < W; x += 2) { var t = (x / gap) * spq; O.px(ctx, x, base - Math.round(Math.exp(-t / tau) * (base - 1))); }
    var wid = Math.max(1, Math.round(1 + len * gap * 0.8));
    for (var k = 1; k <= 4; k++) {
      var lvl = V(v, "grv_tap" + k) * Math.exp(-(k * spq) / tau);
      var cx = Math.round(k * gap), h = Math.round(lvl * (base - 1));
      for (var dx = 0; dx < wid; dx++) {
        var hh = Math.round(h * Math.exp(-dx / Math.max(1, wid * 0.45)));
        if (hh > 0) { if (dx === 0) ctx.fillRect(cx, base - hh, 1, hh, 1); else O.dither(ctx, cx + dx, base - hh, base - 1, 0.5); }
      }
    }
  }

  /* ------------------------------------------------------------------ GEN */
  function drawScale(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {}, sc = O.scaleTones(V(v, "grv_gscale"));
    var cw = Math.max(2, Math.floor(W / 12)), x0 = Math.floor((W - cw * 12) / 2);
    O.dotted(ctx, 0, H - 1, W);
    if (!sc) {   /* Unquantized: a continuous glide */
      L(ctx, x0, H - 2, x0 + cw * 12 - 1, 1);
      return;
    }
    var BLACK = [1, 3, 6, 8, 10];
    for (var p = 0; p < 12; p++) {
      var on = sc.indexOf(p) >= 0, blk = BLACK.indexOf(p) >= 0, x = x0 + p * cw;
      var top = blk ? 1 : Math.round(H * 0.35);
      if (on) ctx.fillRect(x, top, cw - 1, H - 2 - top, 1);
      else O.px(ctx, x + Math.floor((cw - 1) / 2), H - 3);
    }
    O.tick(ctx, x0 + Math.floor(cw / 2) - 0, 1);
  }
  var NLEN = [6, 4, 3, 2, 1.5, 1, 0.5];
  function drawGrid(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var mult = NLEN[O.clamp(Math.round(V(v, "grv_gnotelen")), 0, 6)], sw = (V(v, "grv_gswingamt") - 0.5) * 2 * 0.08;
    var px16 = (W - 1) / 16, t = 0, i = 0;
    O.dotted(ctx, 0, H - 1, W);
    for (var b = 0; b <= 4; b++) O.vdotted(ctx, Math.round(b * 4 * px16), 0, H - 1);
    while (t < 16 && i < 64) {
      var d = mult * ((i & 1) ? 1 - sw * 4 : 1 + sw * 4);
      var x = Math.round(t * px16) + 1, w = Math.max(1, Math.round(d * px16) - 2);
      ctx.fillRect(x, 3, Math.min(w, W - x), H - 6, 1);
      t += d; i++;
    }
  }
  /* the necklace: beads on a thread */
  function drawNeck(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var steps = O.decodeView(O.raw(v, "grv_gview"));
    if (!steps) {   /* no read yet: say so, never invent a pattern */
      O.dotted(ctx, 0, Math.floor(H / 2), W);
      ctx.print(1, Math.floor(H / 2) - 3, "--", 1);
      return;
    }
    var n = steps.length, voice = Math.round(V(v, "grv_gvoice")), acc = V(v, "grv_gaccent"), oct = V(v, "grv_goctjump");
    var cur = O.has(v, "grv_gstep") ? Math.round(V(v, "grv_gstep")) : -1;
    var cw = (W - 2) / n, top = 2, bot = H - 3, mid = (top + bot) / 2;
    function yOf(s) { return Math.round(mid - O.clamp(s, -15, 15) / 15 * (bot - top) / 2 * 1.6); }
    /* the thread: through every bead, sagging at rests */
    var lx = null, ly = null;
    for (var i = 0; i < n; i++) {
      var cx = Math.round(1 + (i + 0.5) * cw), st = steps[i];
      var y = st ? O.clamp(yOf(st.semi), top, bot) : bot;
      if (lx !== null) { for (var xx = lx + 1; xx < cx; xx += 2) O.px(ctx, xx, Math.round(ly + (y - ly) * (xx - lx) / (cx - lx))); }
      lx = cx; ly = y;
    }
    for (var j = 0; j < n; j++) {
      var cx2 = Math.round(1 + (j + 0.5) * cw), s2 = steps[j];
      if (j === cur) ctx.fillRect(Math.round(1 + j * cw), H - 1, Math.max(1, Math.round(cw)), 1, 1);
      if (!s2) { O.px(ctx, cx2, bot + 1); continue; }
      var y2 = O.clamp(yOf(s2.semi), top, bot), big = s2.acc ? 1 + (acc > 0.5 ? 1 : 0) : 0;
      if (cw < 3) big = s2.acc ? 1 : 0;
      if (voice === 1) {          /* bleep: a sparkle */
        O.px(ctx, cx2, y2); if (s2.acc || cw >= 4) { O.px(ctx, cx2 - 1, y2); O.px(ctx, cx2 + 1, y2); O.px(ctx, cx2, y2 - 1); O.px(ctx, cx2, y2 + 1); }
      } else if (voice === 2) {   /* stab: a stacked chord */
        ctx.fillRect(cx2, y2 - 2, 1, 1, 1); ctx.fillRect(cx2, y2, 1, 1, 1); ctx.fillRect(cx2, y2 + 2 <= bot ? y2 + 2 : y2, 1, 1, 1);
        if (s2.acc) ctx.fillRect(cx2 - 1, y2 - 2, 3, 5, 1);
      } else {                    /* bass: round beads, solid when accented */
        var r = s2.acc ? 1 + big : 1;
        if (s2.acc) ctx.fillRect(cx2 - r, y2 - r, 2 * r + 1, 2 * r + 1, 1);
        else { ctx.fillRect(cx2 - 1, y2 - 1, 3, 3, 1); ctx.fillRect(cx2, y2, 1, 1, 0); }
      }
      /* octave-jump ghosts above (illustrative dice) */
      if (oct > 0.01 && O.hash(j, 9) < oct * 0.7) { var gy = Math.max(top - 1, y2 - 5); O.px(ctx, cx2, gy); }
      if (j === cur) { ctx.fillRect(cx2 - 2, y2 - 2, 5, 1, 1); }
    }
  }
  function drawPluck(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var dec = V(v, "grv_gswing"), voice = O.clamp(Math.round(V(v, "grv_gvoice")), 0, 2), sus = [0.5, 0.12, 0.3][voice];
    var tau = 0.004 * Math.pow(40, dec * dec), body = 8 * tau, T = Math.min(0.6, Math.max(0.05, body * 4));
    O.dotted(ctx, 0, H - 1, W);
    var lx = null, ly = null;
    for (var x = 0; x < W; x++) {
      var t = x / (W - 1) * T, a = Math.exp(-t / tau) * (1 - sus) + sus * Math.exp(-t / body);
      var y = Math.round(H - 1 - a * (H - 2));
      if (lx !== null) L(ctx, lx, ly, x, y);
      lx = x; ly = y;
    }
  }
  /* VOICE + its two knobs: one picture per SEQ voice, drawn from the voice's own A/B keys.
   *   Basic   the wave, folded by FOLD          Detune  the wave + the second osc (dotted)
   *   FM      the phase-modulated wave          Acid    the resonant filter, dotted = ENV sweep
   *   Dirt    triangle + noise, cutoff shaking  Dirt 2  FM'd triangle, cutoff shaking
   *   Bleep   a short blip, BLIP = pitch flick  Stab    one lane per chord tone
   * Three ticks on the floor: VOICE, A, B. */
  var GV_KEYS = [["grv_gwave", "grv_gfold"], ["grv_gdtwave", "grv_gdetune"], ["grv_gfmwave", "grv_gfmamt"],
    ["grv_gasres", "grv_gasenv"], ["grv_gaqres", "grv_gaqenv"], ["grv_gd1noise", "grv_gd1nfm"],
    ["grv_gd2fm", "grv_gd2nfm"], ["grv_gblwave", "grv_gblip"], ["grv_gstwave", "grv_gchord"]];
  function drawGwave(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var voice = O.clamp(Math.round(V(v, "grv_gvoice")), 0, 8), hasVoice = O.has(v, "grv_gvoice");
    var A = V(v, GV_KEYS[voice][0]), B = V(v, GV_KEYS[voice][1]);
    var ph = H - 3, mid = Math.floor((ph - 1) / 2), i, x;
    function noise(k) { return O.hash(k, 11) * 2 - 1; }
    switch (voice) {
      case 1: {   /* Detune: second osc dotted. Cents are too small to see, so they are drawn wider */
        var st = O.detuneSt(B), r = st > -1 ? 1 + st * 0.25 : Math.pow(2, st / 12);
        O.plot(ctx, function (t) { return O.wave4(A, t); }, 0, 0, W, ph, 2);
        for (i = 0; i < W; i += 2) {
          var sv = O.wave4(A, i / Math.max(1, W - 1) * 2 * r);
          O.px(ctx, i, Math.round((0.5 - 0.5 * sv) * (ph - 1)));
        }
        break;
      }
      case 2:     /* FM: the carrier's phase pushed by a 1:1 sine */
        O.plot(ctx, function (t) { return O.wave4(A, t + B * B * 1.2 * Math.sin(2 * Math.PI * t)); }, 0, 0, W, ph, 2);
        break;
      case 3: case 4: {   /* Acid: filter response at the cutoff (solid) and at the ENV peak (dotted) */
        var fc = O.logHz(V(v, "grv_genfilt"), 30, 20000), peak = Math.min(18000, fc * (1 + 20 * B * B));
        var res = 0.15 + 0.8 * A, lo = Math.log(30), hi = Math.log(20000), top = 1, bot = ph - 1;
        var ly = null;
        for (x = 0; x < W; x++) {
          var f = Math.exp(lo + (hi - lo) * x / Math.max(1, W - 1));
          var m1 = Math.min(2.5, O.svfMag("lp", f, fc, res)), m2 = Math.min(2.5, O.svfMag("lp", f, peak, res));
          var y1 = Math.round(bot - (bot - top) * m1 / 2.5), y2 = Math.round(bot - (bot - top) * m2 / 2.5);
          if (ly !== null) L(ctx, x - 1, ly, x, y1);
          if (!(x & 1)) O.px(ctx, x, y2);
          ly = y1;
        }
        /* the oscillator in the corner: saw or square */
        var gw = Math.min(10, Math.floor(W / 6)), gx = 1;
        if (voice === 3) { L(ctx, gx, 4, gx + gw / 2, 0); L(ctx, gx + gw / 2, 0, gx + gw / 2, 4); L(ctx, gx + gw / 2, 4, gx + gw, 0); }
        else { L(ctx, gx, 4, gx, 0); L(ctx, gx, 0, gx + gw / 2, 0); L(ctx, gx + gw / 2, 0, gx + gw / 2, 4); L(ctx, gx + gw / 2, 4, gx + gw, 4); }
        break;
      }
      case 5: case 6: {   /* Dirt: triangle (+ noise, or FM'd), the cutoff line shaking with noise */
        var lane = Math.max(3, Math.floor(ph / 4));
        O.plot(ctx, function (t) {
          var p = voice === 6 ? t + A * A * 1.2 * Math.sin(2 * Math.PI * t) : t;
          var w = O.wave4(1 / 3, p);
          if (voice === 5) w += A * 0.9 * noise(Math.floor(t * 47));
          return w;
        }, 0, lane, W, ph - lane, 2);
        for (x = 0; x < W; x += 2) O.px(ctx, x, Math.round(lane / 2 + B * (lane / 2) * noise(x + 5)));
        break;
      }
      case 7: {   /* Bleep: a fast decaying blip, BLIP = how far the pitch flicks at the start */
        O.dotted(ctx, 0, mid, W);
        var lx = null, lyy = null, p2 = 0, cyc = Math.max(4, Math.round(W / 7));
        for (x = 0; x < W; x++) {
          var t2 = x / Math.max(1, W - 1);
          p2 += cyc / W * (1 + 1.2 * B * Math.exp(-t2 * 9));
          var yy = Math.round((0.5 - 0.5 * O.wave4(A, p2) * Math.exp(-t2 * 3.2)) * (ph - 1));
          if (lx !== null) L(ctx, lx, lyy, x, yy);
          lx = x; lyy = yy;
        }
        break;
      }
      case 8: {   /* Stab: one lane per chord tone, highest on top */
        var tones = [0].concat(O.CHORDS[O.clamp(Math.round(B), 0, 3)]).reverse();
        var ln = Math.max(3, Math.floor(ph / tones.length));
        for (var k = 0; k < tones.length; k++) {
          var y0 = k * ln, rr = Math.pow(2, tones[k] / 12);
          O.plot(ctx, function (t) { return O.wave4(A, t * rr); }, 0, y0, W, Math.min(ln, ph - y0), 2);
        }
        break;
      }
      default:    /* Basic: the wave, folded */
        O.plot(ctx, function (t) { return O.fold(O.wave4(A, t), B); }, 0, 0, W, ph, 2);
    }
    O.dotted(ctx, 0, H - 1, W);
    var bpos = voice === 8 ? O.clamp(Math.round(B), 0, 3) / 3 : B;
    if (hasVoice) {
      var tw3 = Math.floor(W / 3) - 3;
      O.tick(ctx, 1 + voice / 8 * tw3, H - 1);
      O.tick(ctx, Math.floor(W / 3) + 1 + A * tw3, H - 1);
      O.tick(ctx, Math.floor(2 * W / 3) + 1 + bpos * tw3, H - 1);
    } else {
      O.tick(ctx, 1 + A * (Math.floor(W / 2) - 3), H - 1);
      O.tick(ctx, Math.floor(W / 2) + 1 + bpos * (Math.floor(W / 2) - 3), H - 1);
    }
  }
  function drawDice(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var rat = V(v, "grv_gratchet"), sld = V(v, "grv_gslide"), nr = V(v, "grv_gnrand"), loop = Math.round(V(v, "grv_gloop"));
    var n = 16, cw = Math.floor((W - 1) / n), x0 = Math.floor((W - cw * n) / 2), bot = H - 2;
    O.dotted(ctx, 0, H - 1, W);
    for (var i = 0; i < n; i++) {
      var di = loop > 0 ? i % loop : i, x = x0 + i * cw;
      if (loop > 0 && i % loop === 0) ctx.fillRect(x, 0, 1, 2, 1);
      var h = bot - 3, y = 3;
      if (O.hash(di, 2) < nr) y = 3 + Math.round((O.hash(di, 3) * 2 - 1) * 2.5) + 2;
      var r = O.hash(di, 6) < rat * 0.8 ? (O.hash(di, 7) < 0.6 ? 2 : 3) : 1, sw = Math.max(1, cw - 1);
      if (O.hash(di, 9) < sld * 0.8 && i > 0) {   /* SLIDE: a tie ramp from the step before */
        L(ctx, x - 1, y + 2, x + sw - 1, y); ctx.fillRect(x, y + 2, sw, bot - y - 2, 1); continue;
      }
      if (r === 1) ctx.fillRect(x, y, sw, bot - y, 1);
      else for (var k = 0; k < r; k++) {
        var sx = x + Math.round(k * sw / r), ww = Math.max(1, Math.round(sw / r) - (sw > r ? 1 : 0));
        ctx.fillRect(sx, y + k * 2, ww, bot - y - k * 2, 1);
      }
    }
  }

  /* -------------------------------------------------------------- groove FX */
  function drawLfo(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var spd = V(v, "grv_lfospd"), amt = V(v, "grv_lfoamt"), hz = 0.05 + spd * spd * 8;
    var cyc = 0.75 + spd * 5, ph = now(o) / 1000 * hz, mid = (H - 1) / 2;
    O.dotted(ctx, 0, Math.round(mid), W);
    var a = Math.max(0.08, amt);
    O.plot(ctx, function (t) { var p = t + ph; p -= Math.floor(p); return (p < 0.5 ? 4 * p - 1 : 3 - 4 * p) * a; }, 0, 0, W, H, cyc);
  }
  /* a ping bouncing inside the room's inner box */
  function ping(ctx, inner, ms, speed) {
    if (inner.w < 3 || inner.h < 3) return;
    var t = ms / 1000 * speed, px = (t * 37) % (2 * (inner.w - 1)), py = (t * 23) % (2 * (inner.h - 1));
    if (px > inner.w - 1) px = 2 * (inner.w - 1) - px;
    if (py > inner.h - 1) py = 2 * (inner.h - 1) - py;
    ctx.fillRect(Math.round(inner.x + px), Math.round(inner.y + py), 2, 2, 1);
  }
  function drawRoom(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var rw = Math.min(Math.round(W * 0.32), Math.round(H * 2.4));
    /* v0.6.2: the room (type) picture sits over RV TYPE, the 4th knob */
    O.tail(ctx, v, 0, 0, W - rw - 3, H);
    var inner = O.room(ctx, V(v, "grv_rvtype"), W - rw, 0, rw, H);
    if (V(v, "grv_rvmix") > 0.001) ping(ctx, inner, now(o), 0.6 + V(v, "grv_rvdecay") * 0.4);
  }
  /* v0.6.2 Space: one meter per send, under its own knob (RMB / SEQ / KICK / DLY) */
  function drawSend(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var ks = ["grv_rvrmb", "grv_rvseq", "grv_rvkick", "grv_rvdly"], cw = W / 4, bot = H - 1;
    O.dotted(ctx, 0, bot, W);
    for (var i = 0; i < 4; i++) {
      var x0 = Math.round(i * cw), x1 = Math.round((i + 1) * cw) - 3, cx = Math.round((x0 + x1) / 2);
      var s = V(v, ks[i]), hh = Math.round(s * (H - 4));
      /* the send: a dithered column rising into a little cloud */
      var bw = Math.max(4, Math.round((x1 - x0) * 0.3));
      for (var x = cx - (bw >> 1); x < cx - (bw >> 1) + bw; x++) if (hh > 0) O.dither(ctx, x, bot - hh, bot - 1, 0.55);
      if (hh > 0) ctx.fillRect(cx - (bw >> 1), bot - hh, bw, 1, 1);
      /* source glyph at the left of each column */
      var gx = x0 + 1, gy = bot - 6;
      if (i === 0) { for (var k = 0; k < 7; k++) O.px(ctx, gx + k, gy + 3 + Math.round(Math.sin(k * 1.4) * 1.5)); }
      else if (i === 1) { ctx.fillRect(gx, gy + 4, 2, 1, 1); ctx.fillRect(gx + 2, gy + 2, 2, 1, 1); ctx.fillRect(gx + 4, gy + 3, 2, 1, 1); ctx.fillRect(gx + 6, gy + 1, 2, 1, 1); }
      else if (i === 2) { ctx.fillRect(gx + 2, gy, 1, 6, 1); ctx.fillRect(gx + 1, gy + 4, 3, 2, 1); }
      else { ctx.fillRect(gx, gy + 1, 1, 5, 1); ctx.fillRect(gx + 3, gy + 3, 1, 3, 1); ctx.fillRect(gx + 6, gy + 4, 1, 2, 1); }
    }
  }
  /* v0.6.2 SEQ delay: the echo train over one bar (dry hit, then repeats by feedback) */
  function drawEcho(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var send = V(v, "grv_dlysend"), fb = V(v, "grv_dlyfb") * 0.92, tone = V(v, "grv_dlytone");
    var t16 = [1, 4 / 3, 2, 3, 4, 6][O.clamp(Math.round(V(v, "grv_dlytime")), 0, 5)];
    var bot = H - 1, span = 16, xs = function (s) { return Math.round(s / span * (W - 2)); };
    O.dotted(ctx, 0, bot, W);
    for (var b = 0; b <= 4; b++) ctx.fillRect(Math.min(W - 1, xs(b * 4)), bot - 1, 1, 2, 1);   /* beats */
    /* the dry note: a dotted outline */
    for (var y = 1; y < bot; y += 2) O.px(ctx, 1, y);
    var a = send, darken = tone < 0.5 ? (0.5 - tone) * 2 : 0, thin = tone > 0.5 ? (tone - 0.5) * 2 : 0;
    for (var k = 1; k <= 24; k++) {
      var x = 1 + xs(k * t16); if (x >= W - 1 || a < 0.02) break;
      var hh = Math.round(a * (H - 3));
      var w = Math.max(1, Math.round(3 * (1 - 0.6 * darken * Math.min(1, k / 3))));
      ctx.fillRect(x, bot - hh, w, hh, 1);
      if (thin > 0.05) for (var yy = bot - hh + 1; yy < bot; yy += 2) ctx.fillRect(x, yy, w, 1, 0);   /* thinner = hollow */
      a *= fb * (1 - 0.25 * darken - 0.15 * thin);
    }
  }
  function drawPump(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var duck = 1 - Math.pow(1 - V(v, "duck"), 2.5), rel = 0.01 + V(v, "duck_rel") * 0.49, smt = V(v, "duck_smt");
    var beat = 60 / 125, T = beat * 2, top = 1, bot = H - 3;
    var atk = 0.002 + smt * 0.03, gains = [];
    for (var x = 0; x < W; x++) {
      var t = (x / (W - 1)) * T, tb = t % beat;
      var dip = tb < atk ? tb / atk : Math.exp(-(tb - atk) / rel);
      gains.push(1 - duck * dip);
    }
    O.dotted(ctx, 0, top, W);
    var lx = null, ly = null;
    for (var x2 = 0; x2 < W; x2++) {
      var y = Math.round(bot - gains[x2] * (bot - top));
      O.dither(ctx, x2, y + 1, bot, 0.3);              /* the energy left after the duck */
      if (lx !== null) L(ctx, lx, ly, x2, y);
      lx = x2; ly = y;
    }
    for (var b = 0; b < 2; b++) { var kx = Math.round(b * beat / T * (W - 1)); ctx.fillRect(kx, H - 1, 3, 1, 1); ctx.fillRect(kx, H - 2, 1, 1, 1); }
    var p = ((now(o) / 1000) % T) / T, ix = Math.round(p * (W - 1));
    ctx.fillRect(ix - 1, Math.round(bot - gains[ix] * (bot - top)) - 1, 3, 3, 1);
  }
  function drawXover(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = O.val(o.values || {}, "duck_bs", 0.5), x = Math.round(v * (W - 1));
    for (var i = 0; i < x; i++) O.dither(ctx, i, 2, H - 2, 0.3);
    ctx.fillRect(x, 0, 1, H, 1);
    O.dotted(ctx, 0, H - 1, W);
  }
  function drawDj(ctx, o) {
    var W = W_(ctx), H = H_(ctx), v = o.values || {};
    var f = V(v, "dj_filt"), res = V(v, "dj_reso") * 0.92;
    var mode = f < 0.48 ? "lp" : f > 0.52 ? "hp" : null;
    var fc = mode === "lp" ? O.logHz(f / 0.48, 120, 18000) : O.logHz((f - 0.52) / 0.48, 30, 3000);
    O.dotted(ctx, 0, H - 1, W);
    O.response(ctx, function (fr) { return mode ? O.svfMag(mode, fr, fc, res) : 1; }, 0, 0, W, H - 1);
    O.tick(ctx, Math.round(f * (W - 1)), H - 2);
  }
  function drawKnee(ctx, o) {
    var W = W_(ctx), H = H_(ctx), a = O.val(o.values || {}, "cmpdr", 0), s = Math.min(W, H) - 1, x0 = Math.floor((W - s) / 2);
    var thr = 1 - a * 0.75, mk = 1 + a * 1.5;
    O.vdotted(ctx, x0, 0, s + 1); O.dotted(ctx, x0, s, s + 1);
    var lx = null, ly = null;
    for (var i = 0; i <= s; i++) {
      var x = i / s, y = x <= thr ? x : thr + (x - thr) / 4;
      y = Math.tanh(y * mk * (1 + a)) / Math.tanh(1 + a * 0.5);
      var yy = Math.round(s - O.clamp(y, 0, 1) * s);
      if (lx !== null) L(ctx, x0 + lx, ly, x0 + i, yy);
      lx = i; ly = yy;
    }
    O.px(ctx, x0 + Math.round(thr * s), s - 1);
  }

  globalThis.canvas_overlay = {
    widgetKinds: {
      "custom:okick": drawKick, "custom:odive": drawDive, "custom:ofm": drawFm, "custom:oalgo": drawAlgo,
      "custom:omorph": drawMorph, "custom:odrum": drawDrum, "custom:oripple": drawRipple,
      "custom:odrive": drawDrive, "custom:osub": drawSub, "custom:ocrush": drawCrush, "custom:openv": drawPenv,
      "custom:oclick": drawClick, "custom:olpf": drawLpf, "custom:ofx": drawFx, "custom:oband": drawBand,
      "custom:otaps": drawTaps, "custom:oscale": drawScale, "custom:ogrid": drawGrid, "custom:oneck": drawNeck,
      "custom:opluck": drawPluck, "custom:ogwave": drawGwave, "custom:odice": drawDice, "custom:olfo": drawLfo,
      "custom:oroom": drawRoom, "custom:osend": drawSend, "custom:oecho": drawEcho, "custom:opump": drawPump, "custom:oxover": drawXover, "custom:odj": drawDj,
      "custom:oknee": drawKnee
    }
  };
})();
