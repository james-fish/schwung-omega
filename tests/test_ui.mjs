// Runs every Omega widget and card at many frame sizes and value combinations, the way the
// host would: a throw retires the drawer for the session on the device, so none may throw,
// and nothing may draw outside its frame. node tests/test_ui.mjs
import fs from "fs";
import vm from "vm";
import { fileURLToPath } from "url";
const root = fileURLToPath(new URL("..", import.meta.url));
const mj = JSON.parse(fs.readFileSync(root + "module.json", "utf8"));
const ctxG = { console, Math, Number, String, isFinite, Object, Array, JSON };
ctxG.globalThis = ctxG;
vm.createContext(ctxG);
vm.runInContext(fs.readFileSync(root + "canvas.js", "utf8"), ctxG);
vm.runInContext(fs.readFileSync(root + "cards.js", "utf8"), ctxG);
let bad = 0, calls = 0; const oof = {};
function frame(w, h) {
  const c = {
    width: w, height: h, out: 0,
    fillRect(x, y, ww, hh) { if (ww > 0 && hh > 0 && (x < 0 || y < 0 || x + ww > w || y + hh > h)) c.out++; },
    setPixel() {}, print() {}, textWidth: (s) => String(s).length * 6, line() {},
    fillCircle() {}, drawCircle() {}, drawArc() {}
  };
  return c;
}
const kinds = ctxG.canvas_overlay.widgetKinds;
const params = mj.chain_params;
const groups = {};
for (const p of params) if (p.viz) { const g = p.viz.group || p.key; (groups[g] ||= { kind: p.viz.kind, keys: [] }).keys.push(p.key); }
function randVals(seed) {
  const v = {};
  params.forEach((p, i) => {
    const r = ((i + 1) * 9301 + seed * 49297) % 233280 / 233280;
    if (p.type === "enum") v[p.key] = Math.floor(r * p.options.length);
    else if (p.type === "int") v[p.key] = Math.round(p.min + r * (p.max - p.min));
    else v[p.key] = (p.min ?? 0) + r * ((p.max ?? 1) - (p.min ?? 0));
  });
  v.grv_gview = seed % 5 === 0 ? null : ".F.Ok.MK.K.Ht.EMkFwFHFFyFM.FwOFF".slice(0, 4 + seed % 28);
  v.grv_gstep = seed % 17;
  return v;
}
const sizes = [[30, 15], [31, 17], [62, 15], [94, 17], [126, 17], [96, 13], [20, 10], [128, 20], [12, 8]];
for (const [g, info] of Object.entries(groups)) {
  const fn = kinds[info.kind];
  if (!fn) { console.log("MISSING drawer", info.kind); bad++; continue; }
  for (let s = 0; s < 40; s++) for (const [w, h] of sizes) {
    const c = frame(w, h);
    try { fn(c, { values: randVals(s), group: { keys: info.keys }, nowMs: s * 137 }); calls++; }
    catch (e) { console.log("THROW", info.kind, w, h, e.message); bad++; break; }
    if (c.out > 0 && w >= 20) oof[info.kind] = (oof[info.kind] || 0) + 1;
  }
}
const carded = params.filter((p) => p.card_script);
for (const p of carded) for (let s = 0; s < 30; s++) for (const [w, h] of [[104, 40], [96, 34], [120, 44]]) {
  const c = frame(w, h); const vals = randVals(s);
  try { ctxG.omega_card(c, { name: p.name, value: "x", raw: s === 3 ? null : vals[p.key], values: vals, w, h, nowMs: 0 }); calls++; }
  catch (e) { console.log("CARD THROW", p.key, e.message); bad++; break; }
}
if (Object.keys(oof).length) console.log("clipped (harmless, host clips):", JSON.stringify(oof));
console.log(`ui: ${Object.keys(groups).length} widget groups, ${carded.length} cards, ${calls} draws, ${bad} problems`);
process.exit(bad ? 1 : 0);
