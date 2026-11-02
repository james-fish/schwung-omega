/* Omega browser bench.
 *
 * The same C engine that ships as dsp.so runs here as WebAssembly (AudioWorklet, or a
 * ScriptProcessor fallback). The page is planned from the ENGINE's own get_param("ui_hierarchy")
 * and get_param("chain_params") exactly as Schwung does (chain metadata wins, visible_if gates
 * the model pages), and the cells are drawn by Omega's REAL canvas.js widgets and cards.js
 * cards. Built-in Schwung widgets (dial, enum box, number) are close approximations.
 * A 16-step kick sequencer + 125..150 BPM transport drive the engine's beat clock.
 */
(function () {
  "use strict";
  var engine = null, starting = false, running = false;
  var vals = {};            // key -> number (or string for read-only views)
  var meta = {};            // key -> merged param meta
  var levels = [];          // [{id, name, params, knobs}]
  var page = 0, touched = null, peakV = 0, seqStep = -1;
  var steps = [2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0];   // 0 off, 1 on, 2 accent

  /* ------------------------------------------------------------------ engine */
  function b64ToBytes(b64) {
    var bin = atob(b64), out = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }
  var WASI = (function () {
    var o = {};
    ["fd_close", "fd_fdstat_get", "fd_fdstat_set_flags", "fd_prestat_get", "fd_prestat_dir_name", "fd_read",
      "fd_readdir", "fd_seek", "fd_write", "path_filestat_get", "path_open", "proc_exit"].forEach(function (n) {
      o[n] = n === "proc_exit" ? function (c) { throw new Error("exit " + c); } : function () { return 8; };
    });
    return { wasi_snapshot_preview1: o };
  })();
  function scriptEngine(ctx) {
    var x = new WebAssembly.Instance(new WebAssembly.Module(b64ToBytes(OMEGA_WASM_B64)), WASI).exports;
    if (x._initialize) x._initialize();
    if (x.wa_init(ctx.sampleRate) !== 1) throw new Error("wa_init failed");
    function put(ptr, cap, s) { var m = new Uint8Array(x.memory.buffer), b = new TextEncoder().encode(s), n = Math.min(b.length, cap - 1); m.set(b.subarray(0, n), ptr); m[ptr + n] = 0; }
    var sp = ctx.createScriptProcessor(1024, 0, 2), pk = 0, n = 0;
    sp.onaudioprocess = function (e) {
      var l = e.outputBuffer.getChannelData(0), r = e.outputBuffer.getChannelData(1);
      for (var o = 0; o < l.length; o += 128) {
        x.wa_render(128);
        var pcm = new Int16Array(x.memory.buffer, x.wa_pcm(), 256);
        for (var i = 0; i < 128; i++) { l[o + i] = pcm[2 * i] / 32768; r[o + i] = pcm[2 * i + 1] / 32768; pk = Math.max(pk, Math.abs(l[o + i])); }
      }
      if (++n >= 2) { onStat({ peak: pk, step: x.wa_seq_step() }); pk = 0; n = 0; }
    };
    sp.connect(ctx.destination);
    return {
      set: function (k, v) { put(x.wa_key(), x.wa_key_cap(), k); put(x.wa_val(), x.wa_val_cap(), String(v)); x.wa_set(); },
      get: function (k) { put(x.wa_key(), x.wa_key_cap(), k); var r = x.wa_get(); return Promise.resolve(r < 0 ? null : new TextDecoder().decode(new Uint8Array(x.memory.buffer, x.wa_out(), r))); },
      tempo: function (bpm, run) { x.wa_set_tempo(bpm, run ? 1 : 0); },
      seq: function (a, b) { x.wa_seq(a, b); },
      mode: "ScriptProcessor"
    };
  }
  async function startEngine() {
    try { if (navigator.audioSession) navigator.audioSession.type = "playback"; } catch (e) { /* older Safari */ }
    var Ctx = window.AudioContext || window.webkitAudioContext;
    var ctx = new Ctx({ sampleRate: 44100, latencyHint: "interactive" });
    var node = null;
    var src = OMEGA_WORKLET;
    var urls = ["data:text/javascript;base64," + btoa(src), URL.createObjectURL(new Blob([src], { type: "text/javascript" }))];
    for (var u = 0; u < urls.length && !node && ctx.audioWorklet; u++) {
      try {
        await ctx.audioWorklet.addModule(urls[u]);
        node = new AudioWorkletNode(ctx, "omega", { numberOfInputs: 0, outputChannelCount: [2],
          processorOptions: { wasm: b64ToBytes(OMEGA_WASM_B64).buffer } });
      } catch (e) { node = null; console.warn("worklet route failed:", e.message); }
    }
    if (node) {
      var info = await new Promise(function (res) { node.port.onmessage = function (e) { if (e.data.t === "ready") res(e.data); }; });
      if (!info.ok) throw new Error("engine failed: " + info.error);
      node.connect(ctx.destination);
      var pend = new Map(), id = 1;
      node.port.onmessage = function (e) {
        var d = e.data;
        if (d.t === "got") { var f = pend.get(d.id); pend.delete(d.id); if (f) f(d.val); }
        else if (d.t === "stat") onStat(d);
      };
      engine = {
        set: function (k, v) { node.port.postMessage({ t: "set", key: k, val: String(v) }); },
        get: function (k) { return new Promise(function (res) { var i = id++; pend.set(i, res); node.port.postMessage({ t: "get", id: i, key: k }); }); },
        tempo: function (bpm, run) { node.port.postMessage({ t: "tempo", bpm: bpm, run: run }); },
        seq: function (a, b) { node.port.postMessage({ t: "seq", steps: a, accent: b }); },
        mode: "AudioWorklet"
      };
    } else engine = scriptEngine(ctx);
    engine.ctx = ctx;
    await ctx.resume();
    await loadContract();
    pushSeq();
    engine.tempo(bpm(), running);
    document.getElementById("engineinfo").textContent = "Engine: " + engine.mode + " · " + ctx.sampleRate + " Hz · same C as the Move's dsp.so, compiled to WebAssembly.";
  }

  function onStat(d) {
    peakV = Math.max(peakV * 0.6, d.peak || 0);
    document.getElementById("peak").style.width = Math.min(100, peakV * 100) + "%";
    if (d.step !== undefined && d.step !== seqStep) { seqStep = d.step; paintSeq(); }
  }

  /* ---------------------------------------------------------------- contract */
  async function loadContract() {
    var ui = JSON.parse(await engine.get("ui_hierarchy"));
    var cp = JSON.parse(await engine.get("chain_params"));
    var chain = {}; cp.forEach(function (p) { chain[p.key] = p; });
    var inline = {};
    levels = [];
    Object.keys(ui.levels).forEach(function (id) {
      var L = ui.levels[id], ps = [];
      (L.params || []).forEach(function (p) { if (p && typeof p === "object" && p.key) { if (!inline[p.key]) inline[p.key] = p; ps.push(p); } });
      levels.push({ id: id, name: L.name || L.label || id, params: ps, knobs: L.knobs || [] });
    });
    meta = {};
    Object.keys(inline).concat(Object.keys(chain)).forEach(function (k) {
      if (meta[k]) return;
      var m = {}, i = inline[k] || {}, c = chain[k] || {}, f;
      for (f in i) m[f] = i[f];
      for (f in c) m[f] = c[f];      // chain_params wins, as on the host
      m.key = k;
      meta[k] = m;
    });
    var keys = Object.keys(meta);
    for (var j = 0; j < keys.length; j++) {
      var r = await engine.get(keys[j]);
      if (r !== null && r !== "") vals[keys[j]] = Number(r);
      else if (meta[keys[j]]["default"] !== undefined) vals[keys[j]] = Number(meta[keys[j]]["default"]);
    }
    buildPageSel();
    renderEncoders();
  }
  function visibleKnobs(L) {
    var gate = {};
    L.params.forEach(function (p) { if (p.visible_if) gate[p.key] = p.visible_if; });
    var out = [];
    L.knobs.forEach(function (k) {
      var g = gate[k];
      if (g && Math.round(vals[g.param] || 0) !== g.equals) return;
      out.push(k);
    });
    return out.slice(0, 8);
  }

  /* ------------------------------------------------------------- param values */
  function lo(m) { return m.type === "enum" ? 0 : Number(m.min !== undefined ? m.min : 0); }
  function hi(m) { return m.type === "enum" ? (m.options || [""]).length - 1 : Number(m.max !== undefined ? m.max : 1); }
  function setParam(k, v, quiet) {
    var m = meta[k]; if (!m) return;
    v = Math.max(lo(m), Math.min(hi(m), v));
    if (m.type === "enum" || m.type === "int") v = Math.round(v);
    var changed = vals[k] !== v;
    vals[k] = v;
    if (engine && changed) engine.set(k, m.type === "float" ? v.toFixed(4) : String(v));
    if (!quiet) { touched = { key: k, at: performance.now() }; renderEncoders(); }
    if (k === "model") { renderEncoders(); }
  }
  function fmt(k) {
    var m = meta[k], v = vals[k];
    if (!m || v === undefined || isNaN(v)) return "--";
    if (m.type === "enum") { var o = m.short_options || m.options || []; return String(o[Math.round(v)] || ""); }
    if (m.type === "int") return String(Math.round(v));
    if (m.unit === "Hz") return Math.round(v) + " Hz";
    if (m.unit === "%" || (lo(m) === 0 && hi(m) === 1)) return Math.round(v * 100) + "%";
    return v.toFixed(2);
  }
  function fullFmt(k) {
    var m = meta[k];
    if (m && m.type === "enum") return String((m.options || [])[Math.round(vals[k])] || "");
    try { var d = O.describe(k, vals); if (d && !/^\d+%$/.test(d)) return d; } catch (e) { /* fallthrough */ }
    return fmt(k);
  }

  /* ---------------------------------------------------------------- OLED */
  /*@@FONT@@*/
  var FB = new Uint8Array(128 * 64);
  function makeCtx(ox, oy, w, h) {
    function px(x, y, c) {
      x = Math.round(x); y = Math.round(y);
      if (x < 0 || y < 0 || x >= w || y >= h) return;
      var X = ox + x, Y = oy + y;
      if (X < 0 || Y < 0 || X >= 128 || Y >= 64) return;
      FB[Y * 128 + X] = c ? 1 : 0;
    }
    var ctx = {
      width: w, height: h,
      fillRect: function (x, y, ww, hh, c) {
        if (c === undefined) c = 1;
        for (var j = 0; j < hh; j++) for (var i = 0; i < ww; i++) px(x + i, y + j, c);
      },
      setPixel: function (x, y, c) { px(x, y, c === undefined ? 1 : c); },
      textWidth: function (s) { return Math.max(0, String(s).length * 4 - 1); },
      print: function (x, y, s, c) {
        s = String(s).toUpperCase();
        if (c === undefined) c = 1;
        for (var k = 0; k < s.length; k++) {
          var g = FONT[s[k]] || FONT["?"];
          for (var j = 0; j < 5; j++) for (var i = 0; i < 3; i++) if (g[j * 3 + i] === "1") px(x + k * 4 + i, y + 1 + j, c);
        }
      },
      line: function (x0, y0, x1, y1, c) {
        x0 = Math.round(x0); y0 = Math.round(y0); x1 = Math.round(x1); y1 = Math.round(y1);
        var dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
        for (var n = 0; n < 512; n++) {
          px(x0, y0, c === undefined ? 1 : c);
          if (x0 === x1 && y0 === y1) break;
          var e2 = 2 * err;
          if (e2 >= dy) { err += dy; x0 += sx; }
          if (e2 <= dx) { err += dx; y0 += sy; }
        }
      },
      drawArc: function (cx, cy, r, start, sweep, c) {
        var n = Math.max(8, Math.ceil(Math.abs(sweep) / 6));
        for (var i = 0; i <= n; i++) {
          var a = (start + sweep * i / n) * Math.PI / 180;
          px(cx + Math.sin(a) * r, cy - Math.cos(a) * r, c === undefined ? 1 : c);
        }
      },
      drawCircle: function (cx, cy, r, c) { ctx.drawArc(cx, cy, r, 0, 360, c); },
      fillCircle: function (cx, cy, r, c) {
        for (var y = -r; y <= r; y++) { var xx = Math.round(Math.sqrt(r * r - y * y)); ctx.fillRect(cx - xx, cy + y, 2 * xx + 1, 1, c); }
      }
    };
    return ctx;
  }
  function groupsOf(keys) {
    var out = [];
    for (var row = 0; row < 2; row++) {
      var i = 0;
      while (i < 4) {
        var k = keys[row * 4 + i], viz = k && meta[k] && meta[k].viz, g = viz && viz.group, span = 1;
        if (g) while (i + span < 4) { var k2 = keys[row * 4 + i + span], v2 = k2 && meta[k2] && meta[k2].viz; if (v2 && v2.group === g) span++; else break; }
        out.push({ row: row, col: i, span: span, key: k, viz: viz });
        i += span;
      }
    }
    return out;
  }
  function drawDial(ctx, w, h, k) {
    var m = meta[k], n = (vals[k] - lo(m)) / ((hi(m) - lo(m)) || 1);
    var cx = Math.floor(w / 2), cy = Math.floor(h / 2) + 1, r = Math.min(7, Math.floor(h / 2));
    for (var a = 210; a <= 510; a += 18) ctx.setPixel(cx + Math.sin(a * Math.PI / 180) * r, cy - Math.cos(a * Math.PI / 180) * r, 1);
    ctx.drawArc(cx, cy, r, 210, n * 300, 1);
    var ang = (210 + n * 300) * Math.PI / 180;
    ctx.line(cx, cy, cx + Math.sin(ang) * (r - 2), cy - Math.cos(ang) * (r - 2), 1);
  }
  function drawCell(cell, keys) {
    var x0 = cell.col * 32, y0 = 11 + cell.row * 26, w = cell.span * 32, wh = 17, k = cell.key;
    if (!k || !meta[k]) return;
    var m = meta[k], viz = cell.viz, g = makeCtx(x0 + 1, y0, w - 2, wh);
    var kind = viz && viz.kind;
    if (kind && kind.indexOf("custom:") === 0 && canvas_overlay.widgetKinds[kind]) {
      var gk = viz.group ? keys.filter(function (q) { return q && meta[q] && meta[q].viz && meta[q].viz.group === viz.group; }) : [k];
      try { canvas_overlay.widgetKinds[kind](g, { values: vals, group: { keys: gk }, nowMs: performance.now() }); }
      catch (e) { console.warn(kind, e); }
    } else if (m.type === "enum") {
      var t = fmt(k), tw = g.textWidth(t) + 4, bx = Math.max(0, Math.floor((w - 2 - tw) / 2));
      g.fillRect(bx, 3, tw, 1, 1); g.fillRect(bx, 11, tw, 1, 1); g.fillRect(bx, 3, 1, 9, 1); g.fillRect(bx + tw - 1, 3, 1, 9, 1);
      g.print(bx + 2, 4, t, 1);
    } else if (m.type === "int") {
      var s = String(Math.round(vals[k])); g.print(Math.floor((w - 2 - g.textWidth(s)) / 2), 5, s, 1);
    } else drawDial(g, w - 2, wh, k);
    for (var i = 0; i < cell.span; i++) {
      var kk = keys[cell.row * 4 + cell.col + i];
      if (!kk || !meta[kk]) continue;
      var lc = makeCtx((cell.col + i) * 32, y0 + 18, 32, 7);
      var lab = String(meta[kk].short_name || meta[kk].name || kk).toUpperCase().slice(0, 7);
      var isT = touched && touched.key === kk && performance.now() - touched.at < 1500;
      if (isT) { lab = fmt(kk).toUpperCase().slice(0, 7); lc.fillRect(0, 0, 32, 7, 1); lc.print(Math.floor((32 - lc.textWidth(lab)) / 2), 0, lab, 0); }
      else lc.print(Math.floor((32 - lc.textWidth(lab)) / 2), 0, lab, 1);
    }
  }
  function drawOled() {
    FB.fill(0);
    var full = makeCtx(0, 0, 128, 64);
    if (!levels.length) {
      full.print(40, 22, "OMEGA", 1);
      full.print(14, 34, "TAP START AUDIO", 1);
    } else {
      var L = levels[page], keys = visibleKnobs(L), now = performance.now();
      var t = touched && now - touched.at < 1500 && keys.indexOf(touched.key) >= 0 ? touched : null;
      if (t) { full.fillRect(0, 0, 128, 8, 1); full.print(1, 1, String(meta[t.key].name).slice(0, 18), 0); var hv = fullFmt(t.key); full.print(127 - full.textWidth(hv), 1, hv, 0); }
      else {
        full.print(1, 1, L.name, 1);
        var pn = (page + 1) + "/" + levels.length; full.print(127 - full.textWidth(pn), 1, pn, 1);
        for (var i = 0; i < 128; i += 2) full.setPixel(i, 9, 1);
      }
      groupsOf(keys).forEach(function (c) { drawCell(c, keys); });
      if (t && meta[t.key].card_script) {
        var cw = 104, ch = 40, cx = 12, cy = 14;
        full.fillRect(cx - 2, cy - 2, cw + 4, ch + 4, 0);
        full.fillRect(cx - 2, cy - 2, cw + 4, 1, 1); full.fillRect(cx - 2, cy + ch + 1, cw + 4, 1, 1);
        full.fillRect(cx - 2, cy - 2, 1, ch + 4, 1); full.fillRect(cx + cw + 1, cy - 2, 1, ch + 4, 1);
        try { omega_card(makeCtx(cx, cy, cw, ch), { name: meta[t.key].name, value: fullFmt(t.key), raw: vals[t.key], values: vals, w: cw, h: ch, nowMs: now }); }
        catch (e) { console.warn(e); }
      }
    }
    var cv = document.getElementById("oled"), c = cv.getContext("2d");
    var img = c.createImageData(128, 64);
    for (var j = 0; j < 128 * 64; j++) {
      var on = FB[j];
      img.data[j * 4] = on ? 207 : 4; img.data[j * 4 + 1] = on ? 227 : 6; img.data[j * 4 + 2] = on ? 255 : 10; img.data[j * 4 + 3] = 255;
    }
    var tmp = drawOled.tmp || (drawOled.tmp = document.createElement("canvas"));
    tmp.width = 128; tmp.height = 64; tmp.getContext("2d").putImageData(img, 0, 0);
    c.imageSmoothingEnabled = false;
    c.drawImage(tmp, 0, 0, 512, 256);
  }

  /* ------------------------------------------------------------- encoders */
  function arcSvg(n) {
    var a0 = -150, a1 = a0 + 300 * Math.max(0, Math.min(1, n));
    function p(a) { var r = a * Math.PI / 180; return (23 + 18 * Math.sin(r)).toFixed(1) + " " + (25 - 18 * Math.cos(r)).toFixed(1); }
    var big = a1 - a0 > 180 ? 1 : 0;
    return '<svg viewBox="0 0 46 46"><path d="M' + p(a0) + ' A18 18 0 1 1 ' + p(150) + '" fill="none" stroke="#34343c" stroke-width="4" stroke-linecap="round"/>' +
      (n > 0.001 ? '<path d="M' + p(a0) + ' A18 18 0 ' + big + ' 1 ' + p(a1) + '" fill="none" stroke="#ff5a36" stroke-width="4" stroke-linecap="round"/>' : "") +
      '<circle cx="23" cy="25" r="11" fill="#2b2b31"/><line x1="23" y1="25" x2="' + (23 + 9 * Math.sin(a1 * Math.PI / 180)).toFixed(1) + '" y2="' + (25 - 9 * Math.cos(a1 * Math.PI / 180)).toFixed(1) + '" stroke="#e9e6df" stroke-width="2.5" stroke-linecap="round"/></svg>';
  }
  function renderEncoders() {
    var host = document.getElementById("encoders");
    var keys = levels.length ? visibleKnobs(levels[page]) : [];
    if (host.children.length !== 8) {
      host.innerHTML = "";
      for (var i = 0; i < 8; i++) { var d = document.createElement("div"); d.className = "enc"; d.dataset.i = i; host.appendChild(d); dragify(d); }
    }
    for (var j = 0; j < 8; j++) {
      var el = host.children[j], k = keys[j];
      el.dataset.key = k || "";
      if (!k || !meta[k]) { el.className = "enc empty"; el.innerHTML = arcSvg(0) + '<div class="nm">—</div><div class="vv">&nbsp;</div>'; continue; }
      var m = meta[k], n = (vals[k] - lo(m)) / ((hi(m) - lo(m)) || 1);
      el.className = "enc" + (touched && touched.key === k && performance.now() - touched.at < 1500 ? " held" : "");
      el.innerHTML = arcSvg(n) + '<div class="nm">' + (m.short_name || m.name) + '</div><div class="vv">' + fmt(k) + "</div>";
    }
  }
  function nudge(k, units, fine) {
    var m = meta[k]; if (!m) return;
    if (m.type === "enum" || m.type === "int") setParam(k, vals[k] + Math.sign(units) * Math.max(1, Math.round(Math.abs(units))));
    else setParam(k, vals[k] + units * (hi(m) - lo(m)) / (fine ? 600 : 120));
  }
  function dragify(el) {
    var acc = 0, lastY = 0, lastTap = 0;
    el.addEventListener("pointerdown", function (e) {
      var k = el.dataset.key; if (!k) return;
      e.preventDefault(); el.setPointerCapture(e.pointerId); lastY = e.clientY; acc = 0;
      touched = { key: k, at: performance.now() };
      var tnow = performance.now();
      if (tnow - lastTap < 300 && meta[k] && meta[k]["default"] !== undefined) setParam(k, Number(meta[k]["default"]));
      lastTap = tnow;
    });
    el.addEventListener("pointermove", function (e) {
      var k = el.dataset.key; if (!k || !el.hasPointerCapture(e.pointerId)) return;
      var m = meta[k], dy = lastY - e.clientY; lastY = e.clientY;
      var discrete = m.type === "enum" || m.type === "int", per = discrete ? 14 : 2;
      acc += dy;
      while (Math.abs(acc) >= per) { var s = Math.sign(acc); nudge(k, discrete ? s : s, e.shiftKey); acc -= s * per; }
      touched = { key: k, at: performance.now() };
    });
    el.addEventListener("wheel", function (e) {
      var k = el.dataset.key; if (!k) return;
      e.preventDefault(); nudge(k, e.deltaY < 0 ? 1 : -1, e.shiftKey);
    }, { passive: false });
  }

  /* --------------------------------------------------------------- pages */
  function buildPageSel() {
    var sel = document.getElementById("pagesel"); sel.innerHTML = "";
    levels.forEach(function (L, i) { var o = document.createElement("option"); o.value = i; o.textContent = (i + 1) + ". " + L.name; sel.appendChild(o); });
    sel.value = page;
  }
  function goPage(i) {
    if (!levels.length) return;
    page = (i + levels.length) % levels.length;
    document.getElementById("pagesel").value = page;
    touched = null;
    renderEncoders();
  }

  /* ---------------------------------------------------------- sequencer */
  function bpm() { return +document.getElementById("bpm").value || 132; }
  function masks() {
    var on = 0, ac = 0;
    for (var i = 0; i < 16; i++) { if (steps[i]) on |= 1 << (15 - i); if (steps[i] === 2) ac |= 1 << (15 - i); }
    return [on, ac];
  }
  function pushSeq() { if (engine) { var m = masks(); engine.seq(m[0], m[1]); } }
  function paintSeq() {
    var host = document.getElementById("seq");
    for (var i = 0; i < 16; i++) {
      var b = host.children[i];
      b.className = "step" + (steps[i] === 1 ? " on" : steps[i] === 2 ? " acc" : "") + (running && i === seqStep ? " now" : "");
    }
  }
  function buildSeq() {
    var host = document.getElementById("seq");
    for (var i = 0; i < 16; i++) {
      (function (i) {
        var b = document.createElement("button"); b.className = "step"; b.setAttribute("aria-label", "Step " + (i + 1));
        b.onclick = function () { steps[i] = (steps[i] + 1) % 3; paintSeq(); pushSeq(); };
        host.appendChild(b);
      })(i);
    }
    paintSeq();
    Array.prototype.forEach.call(document.querySelectorAll("[data-pat]"), function (b) {
      b.onclick = function () {
        var m = parseInt(b.dataset.pat, 16);
        for (var i = 0; i < 16; i++) steps[i] = (m >> (15 - i)) & 1 ? (i === 0 ? 2 : 1) : 0;
        paintSeq(); pushSeq();
      };
    });
  }

  /* -------------------------------------------------------------- scenes */
  var SCENES = [
    { n: "Init", pg: "kick1", d: "Clean kick, rumble + seq off", p: {} },
    { n: "Warehouse Rumble", pg: "groovefx", d: "FM kick, rumble, big room",
      p: { pitch: 48, length: 0.55, curve: 0.82, grv_vol: 0.55, grv_length: 0.72, grv_color: 0.45, grv_hpf: 0.08, grv_drive: 0.25,
        grv_rvmix: 0.45, grv_rvdecay: 0.72, grv_rvtype: 0, grv_rvtone: 0.42, duck: 0.6, cmpdr: 0.3 } },
    { n: "Offbeat Bleeps", pg: "genseq", d: "Bleep voice, offbeat riff",
      p: { grv_genvol: 0.55, grv_gpattern: 6, grv_gvoice: 7, grv_gscale: 3, grv_groot: 55, grv_genfilt: 0.58, grv_gswing: 0.35,
        grv_gaccent: 0.6, grv_goctjump: 0.25, grv_gentaps: 0.15, grv_rvmix: 0.35, grv_rvdecay: 0.62, grv_rvtype: 0, grv_vol: 0.25 } },
    { n: "Acid Roll", pg: "genseq", d: "Acid saw, slides, driven",
      p: { grv_genvol: 1.25, grv_gpattern: 8, grv_gvoice: 3, grv_gscale: 3, grv_groot: 45, grv_genfilt: 0.3, grv_gswing: 0.55,
        grv_gasres: 0.8, grv_gasenv: 0.65, grv_gslide: 0.4, grv_gaccent: 0.85, grv_goctjump: 0.2, grv_gloop: 8,
        grv_rvmix: 0.2, grv_rvtype: 1, duck: 0.7, grv_dlysend: 0.22, grv_dlytime: 3, grv_dlyfb: 0.35, grv_dlytone: 0.65 } },
    { n: "Dub Stabs", pg: "space", d: "Stab chords, dotted echo, tunnel",
      p: { grv_genvol: 0.5, grv_gpattern: 10, grv_gvoice: 8, grv_gchord: 1, grv_gscale: 5, grv_groot: 110, grv_genfilt: 0.46, grv_gswing: 0.45,
        grv_gaccent: 0.5, grv_rvmix: 0.55, grv_rvdecay: 0.82, grv_rvtype: 2, grv_rvtone: 0.38, grv_gentaps: 0.3, grv_vol: 0.3,
        grv_dlysend: 0.45, grv_dlytime: 3, grv_dlyfb: 0.55, grv_dlytone: 0.3, grv_rvdly: 0.8, grv_rvrmb: 0.3 } },
    { n: "Dirt Bunker", pg: "groovefx", d: "Hard kick, 6-bit room, rolling",
      p: { model: 4, fx_type: 0, fx_amt: 0.4, grv_vol: 0.45, grv_rvtype: 3, grv_rvmix: 0.5, grv_rvdecay: 0.6, grv_rvtone: 0.35,
        grv_genvol: 1.2, grv_gvoice: 5, grv_gd1noise: 0.45, grv_gd1nfm: 0.55, grv_gpattern: 7, grv_gscale: 4, grv_groot: 42,
        grv_gnrand: 0.2, grv_genfilt: 0.4, grv_vol: 1.3, cmpdr: 0.45 } }
  ];
  function applyScene(i) {
    if (!engine) return;
    Object.keys(meta).forEach(function (k) {
      var m = meta[k];
      if (m.access === "read" || m["default"] === undefined) return;
      setParam(k, Number(m["default"]), true);
    });
    var p = SCENES[i].p;
    if (p.model !== undefined) setParam("model", p.model, true);
    Object.keys(p).forEach(function (k) { if (k !== "model") setParam(k, p[k], true); });
    Array.prototype.forEach.call(document.getElementById("scenes").children, function (b, j) { b.classList.toggle("sel", j === i); });
    var pi = levels.findIndex(function (L) { return L.id === SCENES[i].pg; });
    if (pi >= 0) goPage(pi);
    renderEncoders();
  }
  function buildScenes() {
    var host = document.getElementById("scenes");
    SCENES.forEach(function (s, i) {
      var b = document.createElement("button");
      b.innerHTML = "<b>" + s.n + "</b><small>" + s.d + "</small>";
      b.onclick = async function () { if (!engine) await power(); applyScene(i); if (!running) toggleRun(); };
      host.appendChild(b);
    });
  }

  /* ------------------------------------------------------------ transport */
  async function power() {
    if (engine || starting) return;
    starting = true;
    var b = document.getElementById("power");
    b.textContent = "Starting…";
    try { await startEngine(); b.textContent = "● Audio on"; b.classList.add("on"); document.getElementById("run").disabled = false; }
    catch (e) { b.textContent = "Audio failed"; console.error(e); document.getElementById("engineinfo").textContent = "Could not start audio: " + e.message; }
    starting = false;
  }
  function toggleRun() {
    if (!engine) return;
    running = !running;
    var r = document.getElementById("run");
    r.textContent = running ? "■ Stop" : "▶ Play"; r.classList.toggle("on", running);
    engine.tempo(bpm(), running);
    if (!running) { seqStep = -1; }
    paintSeq();
  }

  /* -------------------------------------------------------------- polling */
  async function poll() {
    if (engine && levels.length) {
      var r1 = await engine.get("grv_gview"); if (r1 !== null) vals.grv_gview = r1;
      var r2 = await engine.get("grv_gstep"); if (r2 !== null) vals.grv_gstep = Number(r2);
    }
    setTimeout(poll, 90);
  }
  function frame() { drawOled(); requestAnimationFrame(frame); }

  /* ---------------------------------------------------------------- wire */
  document.getElementById("power").onclick = power;
  document.getElementById("run").onclick = toggleRun;
  document.getElementById("bpm").oninput = function () {
    document.getElementById("bpmv").textContent = bpm();
    if (engine) engine.tempo(bpm(), running);
  };
  document.getElementById("jprev").onclick = function () { goPage(page - 1); };
  document.getElementById("jnext").onclick = function () { goPage(page + 1); };
  document.getElementById("pagesel").onchange = function (e) { goPage(+e.target.value); };
  document.addEventListener("keydown", function (e) {
    if (e.target && (e.target.tagName === "INPUT" || e.target.tagName === "SELECT")) return;
    if (e.key === "[") goPage(page - 1);
    else if (e.key === "]") goPage(page + 1);
    else if (e.key === " ") { e.preventDefault(); if (!engine) power().then(toggleRun); else toggleRun(); }
  });
  setInterval(function () { if (touched && performance.now() - touched.at > 1500) { touched = null; renderEncoders(); } }, 250);
  window.__omega = { vals: vals, set: setParam, get meta() { return meta; }, get page() { return page; } };
  buildSeq(); buildScenes(); renderEncoders(); frame(); poll();
})();
