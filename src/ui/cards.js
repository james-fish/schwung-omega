/* Omega: parameter cards (Schwung `card_script`), raised while a knob is held or turning.
 *
 * One drawer serves every carded knob. The header is the parameter and its value; the body is a
 * picture that needs more room than a cell: the FM4 operator graph, the filter route, the scale
 * on a keyboard, the GEN pattern and voice, the groove FX chain, and the warehouse. o.raw may be
 * null (the module did not answer): then only the name is drawn, never a made-up picture.
 */
(function () {
  /* @@COMMON@@ */

  function header(ctx, o, text) {
    var W = o.w, t = O.fit(ctx, text, Math.floor(W * 0.55));
    ctx.print(1, 0, O.fit(ctx, o.name, W - O.tw(ctx, t) - 6), 1);
    ctx.print(W - 1 - O.tw(ctx, t), 0, t, 1);
    O.dotted(ctx, 0, 9, W);
  }
  function chip(ctx, x, y, label, on) {
    var w = O.tw(ctx, label) + 4;
    if (on) { ctx.fillRect(x, y, w, 9, 1); ctx.print(x + 2, y + 1, label, 0); }
    else { O.box(ctx, x, y, w, 9); ctx.print(x + 2, y + 1, label, 1); }
    return x + w;
  }
  function arrow(ctx, x0, x1, y) { O.line(ctx, x0, y, x1, y); O.px(ctx, x1 - 1, y - 1); O.px(ctx, x1 - 1, y + 1); }

  var VOICE_TXT = ["Wave + fold", "2 osc, detune/sub", "1:1 FM pluck", "303 saw, slide",
    "303 square, slide", "Tri + noise grit", "FM tri + noise", "Blip, +2 oct", "Chord stab"];
  var PAT_TXT = ["Seeded scale notes", "Walk up the scale", "Walk down", "Up and back", "Root, then leaps",
    "Root pedal + walk", "Hypnotic offbeats", "16th roll + pops", "303-ish line", "Rise, then answer",
    "Dotted minimal stabs", "Bouncing octaves"];

  globalThis.omega_card = function (ctx, o) {
    var W = o.w, H = o.h, key = null;
    if (o.raw === null || o.raw === undefined) { ctx.print(1, 0, String(o.name || ""), 1); ctx.print(1, 12, "--", 1); return; }
    var values = {};
    for (var k in (o.values || {})) values[k] = o.values[k];
    /* which key raised the card: match by name against the keys we card */
    var CARDED = { "FM4 ALGO": "fm4_algo", "FILT RTE": "filter_route", "GEN SCALE": "grv_gscale", "GEN PATTERN": "grv_gpattern",
      "GEN VOICE": "grv_gvoice", "SEQ VOICE": "grv_gvoice", "SEQ PATTERN": "grv_gpattern", "ROUTE": "grv_route", "RV TYPE": "grv_rvtype", "RV DECAY": "grv_rvdecay",
      "Algorithm": "fm4_algo", "Filter Route": "filter_route", "SCALE": "grv_gscale", "PATTERN": "grv_gpattern",
      "VOICE": "grv_gvoice" };
    key = o.key || CARDED[o.name] || null;
    if (!key) { header(ctx, o, String(o.value || "")); return; }
    values[key] = o.raw;
    var v = O.val(values, key), top = 12, bh = H - top - 1;
    header(ctx, o, O.describe(key, values));

    switch (key) {
      case "fm4_algo": {
        O.algoDiagram(ctx, v, O.val(values, "fm4_feedback"), 4, top + 1, Math.min(60, W - 40), bh - 2, 5);
        ctx.print(W - 34, top + 2, "OUT =", 1);
        ctx.fillRect(W - 34, top + 13, 5, 5, 1); ctx.print(W - 27, top + 12, "CAR", 1);
        O.box(ctx, W - 34, top + 21, 5, 5); ctx.print(W - 27, top + 20, "MOD", 1);
        return;
      }
      case "filter_route": {
        var r = Math.round(v), y1 = top + 3, y2 = top + 16;
        var a = chip(ctx, 2, y1, "SYN", false), b = chip(ctx, 2, y2, "TRS", false);
        var fx = Math.round(W * 0.45);
        chip(ctx, fx, Math.round((y1 + y2) / 2), "FILT", true);
        if (r === 0 || r === 2) arrow(ctx, a + 1, fx - 1, y1 + 4); else arrow(ctx, a + 1, W - 12, y1 + 4);
        if (r === 1 || r === 2) arrow(ctx, b + 1, fx - 1, y2 + 4); else arrow(ctx, b + 1, W - 12, y2 + 4);
        ctx.print(W - 10, Math.round((y1 + y2) / 2) + 1, ">", 1);
        return;
      }
      case "grv_gscale": {
        var tones = O.scaleTones(v), WH = [0, 2, 4, 5, 7, 9, 11], BL = [1, 3, -1, 6, 8, 10];
        var kw = Math.floor((W - 4) / 7);
        for (var i = 0; i < 7; i++) {
          var x = 2 + i * kw, on = !tones || tones.indexOf(WH[i]) >= 0;
          O.box(ctx, x, top + 1, kw + 1, bh - 1);
          if (on) ctx.fillRect(x + 2, top + bh - 7, kw - 3, 5, 1);
        }
        for (var j = 0; j < 6; j++) {
          if (BL[j] < 0) continue;
          var bx = 2 + (j + 1) * kw - Math.floor(kw / 3), on2 = !tones || tones.indexOf(BL[j]) >= 0;
          ctx.fillRect(bx - 1, top + 1, Math.floor(kw * 2 / 3) + 2, Math.round(bh * 0.55), 0);
          O.box(ctx, bx - 1, top + 1, Math.floor(kw * 2 / 3) + 2, Math.round(bh * 0.55));
          if (on2) ctx.fillRect(bx, top + 2, Math.floor(kw * 2 / 3), Math.round(bh * 0.55) - 2, 1);
        }
        return;
      }
      case "grv_gpattern": case "grv_gvoice": {
        var steps = O.decodeView(O.raw(values, "grv_gview"));
        var txt = key === "grv_gpattern" ? PAT_TXT[O.clamp(Math.round(v), 0, 11)] : VOICE_TXT[O.clamp(Math.round(v), 0, 8)];
        ctx.print(1, top, O.fit(ctx, txt, W - 2), 1);
        var gy = top + 10, gh = H - gy - 1;
        if (!steps) { O.dotted(ctx, 0, gy + Math.floor(gh / 2), W); return; }
        var n = Math.min(16, steps.length), cw = Math.floor((W - 2) / n);
        for (var s = 0; s < n; s++) {
          var st = steps[s], x2 = 1 + s * cw;
          if (!st) { O.px(ctx, x2 + Math.floor(cw / 2), H - 2); continue; }
          var h = O.clamp(Math.round((st.semi + 15) / 30 * (gh - 2)) + 2, 2, gh);
          if (st.acc) ctx.fillRect(x2, H - 1 - h, cw - 1, h, 1);
          else { O.box(ctx, x2, H - 1 - h, cw - 1, h); }
        }
        return;
      }
      case "grv_route": {
        var ORD = [["GRV", "DRV", "REV"], ["GRV", "REV", "DRV"], ["REV", "GRV", "DRV"], ["DRV", "GRV", "REV"]][O.clamp(Math.round(v), 0, 3)];
        var cx = 2, y = top + Math.floor((bh - 9) / 2);
        for (var q = 0; q < 3; q++) {
          var nx = chip(ctx, cx, y, ORD[q], ORD[q] === "GRV");
          if (q < 2) arrow(ctx, nx + 1, nx + 9, y + 4);
          cx = nx + 11;
        }
        return;
      }
      case "grv_rvtype": case "grv_rvdecay": {
        var rw = Math.min(46, Math.round(W * 0.42));
        O.room(ctx, O.val(values, "grv_rvtype"), 1, top + 1, rw, bh - 1);
        var tv = {}; for (var kk in values) tv[kk] = values[kk];
        if (O.val(tv, "grv_rvmix") < 0.3) tv.grv_rvmix = 0.6;   /* show the shape even at low MIX */
        O.tail(ctx, tv, rw + 5, top + 1, W - rw - 6, bh - 1);
        return;
      }
    }
    var bar = Math.round(O.clamp(v, 0, 1) * (W - 4));
    ctx.fillRect(2, top + Math.round(bh / 2), W - 4, 1, 1);
    ctx.fillRect(2, top + Math.round(bh / 2) - 1, Math.max(1, bar), 3, 1);
  };
})();
